// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Library's search, run on the player's machine: which entries a search
// keeps and how they rank, and the order of the visible list.
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

/// Orders two candidates as the list does with no search.
///
/// By group, then name, then built-in registries first, then registry id,
/// then kind and key, which leave no two entries equal.
///
/// @param library the Library
/// @param left one candidate
/// @param right another
/// @return true when `left` comes first
bool before_without_search(const Library& library, const Candidate& left, const Candidate& right) {
    if (left.group != right.group)
        return left.group < right.group;
    if (left.name != right.name)
        return left.name < right.name;
    const Entry& left_entry = library.entries[left.index];
    const Entry& right_entry = library.entries[right.index];
    if (left_entry.built_in != right_entry.built_in)
        return left_entry.built_in;
    return left_entry.id < right_entry.id;
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

bool shown_in_tab(const Library& library, const Entry& entry, bool use_tag) {
    if (library.tab == Tab::updates)
        return entry.state == State::update;
    const Kind kind = library.tab == Tab::mods   ? Kind::mod
                      : library.tab == Tab::maps ? Kind::map_pack
                                                 : Kind::language;
    if (entry.id.kind != kind)
        return false;
    switch (library.filter) {
    case Filter::all:
        break;
    case Filter::installed:
        if (entry.state == State::get)
            return false;
        break;
    case Filter::updates:
        if (entry.state != State::update)
            return false;
        break;
    }
    if (!use_tag || library.tag.empty())
        return true;
    if (entry.listing == nullptr)
        return false;
    const std::vector<std::string>& tags = entry.listing->tags;
    return std::find(tags.begin(), tags.end(), library.tag) != tags.end();
}

void update_visible(Library& library) {
    std::vector<Candidate> kept;
    for (std::size_t index = 0; index < library.entries.size(); ++index) {
        const Entry& entry = library.entries[index];
        if (!shown_in_tab(library, entry, true))
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
    library.visible.clear();
    for (const Candidate& candidate : kept)
        library.visible.push_back(candidate.index);
}

} // namespace oa::ui::library
