// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's search, run on the player's machine: which entries a tab's
// filter and a search keep and how they rank, and the order of the visible
// list.
#include "visible.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::library {

namespace {

/// The bits that mark a UTF-8 continuation byte, and their value in one.
constexpr unsigned char utf8_continuation_mask = 0xc0;
constexpr unsigned char utf8_continuation_bits = 0x80;

/// The byte a search splits its words on.
constexpr char word_separator = ' ';

/// The groups of the order a list takes with no search.
enum class Group : uint8_t {
    update,      ///< an update is offered
    installed,   ///< installed or playing, up to date
    get,         ///< can be installed
    get_blocked, ///< cannot be installed here
};

/// Tells which group an entry sorts in with no search.
///
/// @param entry the entry
/// @return its group
Group group_of(const Entry& entry) {
    switch (entry.state) {
    case State::update:
        return Group::update;
    case State::installed:
    case State::playing:
        return Group::installed;
    case State::get:
        break;
    }
    return entry.can_act ? Group::get : Group::get_blocked;
}

/// Splits a folded search into its words.
///
/// @param query the folded search
/// @return the words, none empty
std::vector<std::string_view> split_words(std::string_view query) {
    std::vector<std::string_view> found;
    std::size_t at = 0;
    while (at < query.size()) {
        const std::size_t end = std::min(query.find(word_separator, at), query.size());
        if (end > at)
            found.push_back(query.substr(at, end - at));
        at = end + 1;
    }
    return found;
}

/// Tells whether a text holds a word.
///
/// @param text the folded text
/// @param word the folded word
/// @return true when the word is in the text
bool holds(std::string_view text, std::string_view word) {
    return text.find(word) != std::string_view::npos;
}

/// An entry's fields as the search reads them, folded.
struct Fields {
    std::string name{};
    std::vector<std::string> tags{};
    std::string author{};
    std::string publisher{};
    std::string summary{};
};

/// Folds an entry's fields.
///
/// @param entry the entry
/// @return its fields; a package no registry lists has only its name
Fields fields_of(const Entry& entry) {
    Fields fields;
    fields.name = folded(entry_name(entry));
    if (entry.listing == nullptr)
        return fields;
    for (const std::string& tag : entry.listing->tags)
        fields.tags.push_back(folded(tag));
    fields.author = folded(entry.listing->author);
    fields.publisher = folded(entry.listing->publisher);
    fields.summary = folded(entry.listing->summary);
    return fields;
}

/// Finds the best field a word is in.
///
/// @param fields the entry's folded fields
/// @param word the folded word
/// @return its class, or nothing when no field holds it
std::optional<MatchClass> class_of(const Fields& fields, std::string_view word) {
    if (holds(fields.name, word))
        return MatchClass::name;
    for (const std::string& tag : fields.tags)
        if (std::string_view(tag).substr(0, word.size()) == word)
            return MatchClass::tag;
    if (holds(fields.author, word) || holds(fields.publisher, word))
        return MatchClass::author;
    if (holds(fields.summary, word))
        return MatchClass::summary;
    return std::nullopt;
}

/// An entry the list keeps, with what it sorts by.
struct Candidate {
    std::size_t index{}; ///< into the entries
    Rank rank{};
    Group group{Group::get};
    std::string name{}; ///< folded
};

/// Returns the day an entry's listing was published.
///
/// @param entry the entry
/// @return the day as written, `YYYY-MM-DD`; empty without a listing or a day
std::string_view published_day(const Entry& entry) {
    return entry.listing != nullptr ? std::string_view(entry.listing->published)
                                    : std::string_view();
}

/// Orders two candidates of the same name by their registries: built-in
/// registries first, then by registry id, then by kind and key, which leave
/// no two entries equal.
///
/// @param library the Library
/// @param left one candidate
/// @param right another
/// @return true when `left` comes first
bool before_by_registry(const Library& library, const Candidate& left, const Candidate& right) {
    const Entry& left_entry = library.entries[left.index];
    const Entry& right_entry = library.entries[right.index];
    if (left_entry.built_in != right_entry.built_in)
        return left_entry.built_in;
    return left_entry.id < right_entry.id;
}

/// Orders two candidates as New does: the later published day first, then
/// by name, then by registry. A day written `YYYY-MM-DD` orders as its text.
///
/// @param library the Library
/// @param left one candidate
/// @param right another
/// @return true when `left` comes first
bool before_newest(const Library& library, const Candidate& left, const Candidate& right) {
    const std::string_view left_day = published_day(library.entries[left.index]);
    const std::string_view right_day = published_day(library.entries[right.index]);
    if (left_day != right_day)
        return left_day > right_day;
    if (left.name != right.name)
        return left.name < right.name;
    return before_by_registry(library, left, right);
}

/// Orders two candidates as the list does with no search.
///
/// Under New, newest first (before_newest). Under the other filters, by
/// group, then name, then registry.
///
/// @param library the Library
/// @param left one candidate
/// @param right another
/// @return true when `left` comes first
bool before_without_search(const Library& library, const Candidate& left, const Candidate& right) {
    if (shown_filter(library) == Filter::newest)
        return before_newest(library, left, right);
    if (left.group != right.group)
        return left.group < right.group;
    if (left.name != right.name)
        return left.name < right.name;
    return before_by_registry(library, left, right);
}

/// Orders two candidates as the list does with a search.
///
/// By the worst class among the words, then their sum, then names that
/// start with the first word, then as with no search.
///
/// @param library the Library
/// @param left one candidate
/// @param right another
/// @return true when `left` comes first
bool before_with_search(const Library& library, const Candidate& left, const Candidate& right) {
    if (left.rank.worst != right.rank.worst)
        return left.rank.worst < right.rank.worst;
    if (left.rank.total != right.rank.total)
        return left.rank.total < right.rank.total;
    if (left.rank.name_starts != right.rank.name_starts)
        return left.rank.name_starts;
    return before_without_search(library, left, right);
}

/// Tells whether a tab holds entries of a kind.
///
/// @param tab the tab
/// @param kind the kind
/// @return true for its own kind on Mods, Maps and Languages, every kind on
///     Updates, and none on Active or Registries
bool tab_holds(Tab tab, Kind kind) {
    switch (tab) {
    case Tab::active:
        return false;
    case Tab::mods:
        return kind == Kind::mod;
    case Tab::maps:
        return kind == Kind::map_pack;
    case Tab::languages:
        return kind == Kind::language;
    case Tab::updates:
        return true;
    case Tab::registries:
        return false;
    }
    return false;
}

/// Tells whether a filter other than New keeps an entry of the tab's kind.
///
/// @param filter the filter
/// @param entry the entry
/// @return true when it keeps it
bool state_kept(Filter filter, const Entry& entry) {
    switch (filter) {
    case Filter::all:
    case Filter::newest:
        return true;
    case Filter::installed:
        return entry.state != State::get;
    case Filter::updates:
        return entry.state == State::update;
    }
    return false;
}

/// Tells whether the base game entry shows under a filter.
///
/// It shows on Mods, under All and Installed, and under no other tab or
/// filter. New never holds it: it is not a package a registry published.
///
/// @param library the Library
/// @param filter the filter
/// @return true when the list keeps it
bool shows_base_game(const Library& library, Filter filter) {
    return library.tab == Tab::mods && (filter == Filter::all || filter == Filter::installed);
}

} // namespace

std::string folded(std::string_view text) {
    std::string result(text);
    for (char& byte : result)
        if (byte >= 'A' && byte <= 'Z')
            byte = static_cast<char>(byte - 'A' + 'a');
    return result;
}

std::string_view capped_query(std::string_view query) {
    if (query.size() <= max_query_bytes)
        return query;
    std::size_t cut = max_query_bytes;
    while (cut > 0 && (static_cast<unsigned char>(query[cut]) & utf8_continuation_mask) ==
                          utf8_continuation_bits)
        --cut;
    return query.substr(0, cut);
}

std::optional<Rank> rank(const Entry& entry, std::string_view query) {
    const std::string search = folded(capped_query(query));
    const std::vector<std::string_view> terms = split_words(search);
    if (terms.empty())
        return Rank{};
    const Fields fields = fields_of(entry);
    Rank result;
    for (const std::string_view word : terms) {
        const std::optional<MatchClass> found = class_of(fields, word);
        if (!found)
            return std::nullopt;
        result.worst = std::max(result.worst, *found);
        result.total += static_cast<uint32_t>(*found);
    }
    result.name_starts =
        std::string_view(fields.name).substr(0, terms.front().size()) == terms.front();
    return result;
}

std::vector<bool> kept_by_filter(const Library& library, Filter filter) {
    std::vector<bool> kept(library.entries.size(), false);
    if (filter != Filter::newest) {
        for (std::size_t index = 0; index < library.entries.size(); ++index) {
            const Entry& entry = library.entries[index];
            if (is_base_game(entry.id))
                kept[index] = shows_base_game(library, filter);
            else
                kept[index] = tab_holds(library.tab, entry.id.kind) && state_kept(filter, entry);
        }
        return kept;
    }
    // New: the tab's listings with a published day, newest first, then cut.
    // The base game entry is not among them.
    std::vector<Candidate> dated;
    for (std::size_t index = 0; index < library.entries.size(); ++index) {
        const Entry& entry = library.entries[index];
        if (is_base_game(entry.id))
            continue;
        if (tab_holds(library.tab, entry.id.kind) && !published_day(entry).empty())
            dated.push_back({index, {}, Group::get, folded(entry_name(entry))});
    }
    std::sort(dated.begin(), dated.end(), [&](const Candidate& left, const Candidate& right) {
        return before_newest(library, left, right);
    });
    for (std::size_t place = 0; place < dated.size() && place < newest_count; ++place)
        kept[dated[place].index] = true;
    return kept;
}

bool has_shown_tag(const Library& library, const Entry& entry) {
    if (library.tag.empty())
        return true;
    if (entry.listing == nullptr)
        return false;
    const std::vector<std::string>& tags = entry.listing->tags;
    return std::find(tags.begin(), tags.end(), library.tag) != tags.end();
}

void update_visible(Library& library) {
    const std::vector<bool> by_filter = kept_by_filter(library, shown_filter(library));
    std::vector<Candidate> kept;
    for (std::size_t index = 0; index < library.entries.size(); ++index) {
        const Entry& entry = library.entries[index];
        if (!by_filter[index] || !has_shown_tag(library, entry))
            continue;
        const std::optional<Rank> ranked = rank(entry, library.query);
        if (!ranked)
            continue;
        kept.push_back({index, *ranked, group_of(entry), folded(entry_name(entry))});
    }
    const bool searching = !split_words(folded(library.query)).empty();
    std::sort(kept.begin(), kept.end(), [&](const Candidate& left, const Candidate& right) {
        return searching ? before_with_search(library, left, right)
                         : before_without_search(library, left, right);
    });
    // The base game entry leads the Mods list whenever the filter and the
    // search keep it, ahead of the order the packages take.
    const auto base_game = std::find_if(kept.begin(), kept.end(), [&](const Candidate& candidate) {
        return is_base_game(library.entries[candidate.index].id);
    });
    if (base_game != kept.end() && base_game != kept.begin())
        std::rotate(kept.begin(), base_game, base_game + 1);
    library.visible.clear();
    for (const Candidate& candidate : kept)
        library.visible.push_back(candidate.index);
}

std::size_t tab_count(const Library& library, Tab tab) {
    if (tab == Tab::registries)
        return library.inputs.registries.size();
    if (tab == Tab::active)
        return 0;
    // Updates counts the updates that can be installed, as the OA button's
    // dot and UPDATE ALL do, not every package its filters can show.
    if (tab == Tab::updates)
        return update_count(library);
    std::size_t count = 0;
    for (const Entry& entry : library.entries)
        if (tab_holds(tab, entry.id.kind) && !is_base_game(entry.id))
            ++count;
    return count;
}

} // namespace oa::ui::library
