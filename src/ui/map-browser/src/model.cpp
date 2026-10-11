// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map browser's model: merging the sources, the filters, the order and
// the primary button. Pure: no file, clock, network or word lookup.
#include "oa/ui/map_browser/model.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <stdint.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::ui::map_browser {
namespace {

/// Returns an ASCII letter in lower case; any other byte as it is.
///
/// @param byte the byte
/// @return the byte, lowered
char lowered(char byte) noexcept {
    return byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a') : byte;
}

/// Tells whether a text holds another, ignoring ASCII case.
///
/// @param text the text searched
/// @param part the text sought; empty is held by every text
/// @return true when it is held
bool holds(std::string_view text, std::string_view part) {
    if (part.empty())
        return true;
    const auto found =
        std::search(text.begin(), text.end(), part.begin(), part.end(), [](char left, char right) {
            return lowered(left) == lowered(right);
        });
    return found != text.end();
}

/// Compares two texts ignoring ASCII case.
///
/// @param left a text
/// @param right another
/// @return true when left sorts before right
bool before(std::string_view left, std::string_view right) {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](char one, char other) {
            return static_cast<unsigned char>(lowered(one)) <
                   static_cast<unsigned char>(lowered(other));
        }
    );
}

/// Returns the bytes of the UTF-8 character a text starts with: 1 for an
/// ASCII byte or a byte that starts no character, else 2 to 4.
///
/// @param text the text, not empty
/// @return the character's bytes, at most the text's
std::size_t first_character_bytes(std::string_view text) noexcept {
    const auto lead = static_cast<unsigned char>(text.front());
    std::size_t bytes = 1;
    if ((lead & 0xe0U) == 0xc0U)
        bytes = 2;
    else if ((lead & 0xf0U) == 0xe0U)
        bytes = 3;
    else if ((lead & 0xf8U) == 0xf0U)
        bytes = 4;
    bytes = std::min(bytes, text.size());
    for (std::size_t at = 1; at < bytes; ++at)
        if ((static_cast<unsigned char>(text[at]) & 0xc0U) != 0x80U)
            return 1;
    return bytes;
}

/// Returns where a card sorts by its origin: base, installed, online.
///
/// @param card the card
/// @return 0, 1 or 2
int32_t origin_rank(const MapCard& card) noexcept {
    switch (card.origin) {
    case MapCard::Origin::base:
        return 0;
    case MapCard::Origin::installed:
        return 1;
    case MapCard::Origin::online:
        return 2;
    }
    return 2;
}

/// Tells whether a card's origin is the source a filter shows.
///
/// @param source the filter's source
/// @param card the card
/// @return true when it shows
bool source_matches(Filters::Source source, const MapCard& card) noexcept {
    switch (source) {
    case Filters::Source::all:
        return true;
    case Filters::Source::base:
        return card.origin == MapCard::Origin::base;
    case Filters::Source::installed:
        return card.origin == MapCard::Origin::installed;
    case Filters::Source::online:
        return card.origin == MapCard::Origin::online;
    }
    return true;
}

/// The counts a numplayers list names: each whole number from 2 to
/// most_players, in the order written.
///
/// A token is one piece between commas, with spaces at its ends left out.
/// Anything that is not a whole number in that range is left out.
///
/// @param numplayers the list; empty names none
/// @return the counts, not yet sorted
std::vector<int32_t> counts_named(std::string_view numplayers) {
    std::vector<int32_t> counts;
    std::size_t at = 0;
    while (at <= numplayers.size()) {
        const std::size_t comma = numplayers.find(',', at);
        const std::string_view raw = numplayers.substr(
            at, comma == std::string_view::npos ? std::string_view::npos : comma - at
        );
        std::string_view token = raw;
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
            token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
            token.remove_suffix(1);
        if (!token.empty()) {
            int32_t value = 0;
            const char* const first = token.data();
            const char* const last = first + token.size();
            const std::from_chars_result parsed = std::from_chars(first, last, value);
            if (parsed.ec == std::errc{} && parsed.ptr == last && value >= 2 &&
                value <= most_players)
                counts.push_back(value);
        }
        if (comma == std::string_view::npos)
            break;
        at = comma + 1;
    }
    return counts;
}

/// Tells whether one count matches a players chip.
///
/// @param chip the chip
/// @param count the count
/// @return true when it matches
bool count_matches(int32_t chip, int32_t count) noexcept {
    if (chip >= many_players_chip)
        return count >= many_players_chip && count <= most_players;
    return count == chip;
}

} // namespace

std::vector<MapCard> merge_cards(
    std::vector<MapCard> base, std::vector<MapCard> installed, std::vector<MapCard> online
) {
    std::vector<MapCard> cards;
    cards.reserve(base.size() + installed.size() + online.size());
    std::set<std::string, std::less<>> named;
    const auto take = [&](std::vector<MapCard>& from, MapCard::Origin origin) {
        for (MapCard& card : from) {
            if (!named.insert(card.name).second)
                continue;
            card.origin = origin;
            cards.push_back(std::move(card));
        }
    };
    take(base, MapCard::Origin::base);
    take(installed, MapCard::Origin::installed);
    take(online, MapCard::Origin::online);
    return cards;
}

std::optional<std::pair<int32_t, int32_t>> map_sides(std::string_view size) noexcept {
    std::array<int64_t, 2> sides{};
    std::size_t found = 0;
    std::size_t at = 0;
    while (at < size.size() && found < sides.size()) {
        if (size[at] < '0' || size[at] > '9') {
            ++at;
            continue;
        }
        int64_t value = 0;
        while (at < size.size() && size[at] >= '0' && size[at] <= '9') {
            if (value < int64_t{1} << 30)
                value = value * 10 + (size[at] - '0');
            ++at;
        }
        sides[found++] = value;
    }
    if (found < sides.size())
        return std::nullopt;
    return std::pair<int32_t, int32_t>{
        static_cast<int32_t>(std::min<int64_t>(sides[0], int64_t{1} << 30)),
        static_cast<int32_t>(std::min<int64_t>(sides[1], int64_t{1} << 30))
    };
}

std::optional<char> size_letter(std::string_view size) noexcept {
    const std::optional<std::pair<int32_t, int32_t>> sides = map_sides(size);
    if (!sides)
        return std::nullopt;
    const int32_t longer = std::max(sides->first, sides->second);
    if (longer <= small_side_most)
        return 'S';
    if (longer <= medium_side_most)
        return 'M';
    return 'L';
}

BaseMapPlayers base_map_players(std::string_view numplayers, int32_t start_positions) {
    BaseMapPlayers kept;
    kept.counts = counts_named(numplayers);
    std::sort(kept.counts.begin(), kept.counts.end());
    kept.counts.erase(std::unique(kept.counts.begin(), kept.counts.end()), kept.counts.end());
    kept.from_numplayers = !kept.counts.empty();
    if (!kept.from_numplayers)
        kept.counts.push_back(start_positions);
    kept.players = kept.counts.back();
    return kept;
}

int32_t shown_players(const MapCard& card) noexcept {
    if (card.player_counts.empty())
        return card.players;
    return *std::max_element(card.player_counts.begin(), card.player_counts.end());
}

bool players_match(int32_t chip, const MapCard& card) noexcept {
    if (card.player_counts.empty())
        return count_matches(chip, card.players);
    for (const int32_t count : card.player_counts)
        if (count_matches(chip, count))
            return true;
    return false;
}

bool query_matches(const MapCard& card, std::string_view query) {
    return holds(card.title, query) || holds(card.name, query) || holds(card.pack_name, query);
}

void apply_filters(Browser& browser) {
    const Filters& filters = browser.filters;
    browser.shown.clear();
    for (std::size_t index = 0; index < browser.cards.size(); ++index) {
        const MapCard& card = browser.cards[index];
        if (!source_matches(filters.source, card))
            continue;
        if (!filters.players.empty() &&
            std::none_of(filters.players.begin(), filters.players.end(), [&](int32_t chip) {
                return players_match(chip, card);
            }))
            continue;
        if (!filters.sizes.empty()) {
            const std::optional<char> letter = size_letter(card.size);
            if (!letter || filters.sizes.count(*letter) == 0)
                continue;
        }
        if (!query_matches(card, filters.query))
            continue;
        browser.shown.push_back(index);
    }
    std::stable_sort(
        browser.shown.begin(), browser.shown.end(), [&](std::size_t left, std::size_t right) {
            const MapCard& one = browser.cards[left];
            const MapCard& other = browser.cards[right];
            if (origin_rank(one) != origin_rank(other))
                return origin_rank(one) < origin_rank(other);
            if (before(one.title, other.title))
                return true;
            if (before(other.title, one.title))
                return false;
            return one.name < other.name;
        }
    );
    if (browser.selected &&
        std::find(browser.shown.begin(), browser.shown.end(), *browser.selected) ==
            browser.shown.end())
        browser.selected.reset();
}

bool fits(const MapCard& card) noexcept {
    return card.base_fit != MapCard::Fit::does_not_fit &&
           card.mod_fit != MapCard::Fit::does_not_fit;
}

PrimaryAction primary_action(const Browser& browser) {
    PrimaryAction action;
    if (browser.downloading) {
        action.kind = Primary::downloading;
        return action;
    }
    if (!browser.selected || *browser.selected >= browser.cards.size())
        return action;
    const MapCard& card = browser.cards[*browser.selected];
    const bool online = card.origin == MapCard::Origin::online;
    action.kind = online ? Primary::download_and_select : Primary::select;
    if (!fits(card)) {
        action.reason = card.fit_reason;
        return action;
    }
    action.enabled = true;
    return action;
}

std::string card_word(std::string_view name) {
    std::string word;
    std::size_t at = 0;
    while (at < name.size() && word.size() < most_word_bytes) {
        const std::size_t bytes = first_character_bytes(name.substr(at));
        const char byte = lowered(name[at]);
        const bool kept = bytes == 1 && ((byte >= 'a' && byte <= 'z') ||
                                         (byte >= '0' && byte <= '9') || byte == '-');
        word.push_back(kept ? byte : '-');
        at += bytes;
    }
    return word.empty() ? std::string("map") : word;
}

} // namespace oa::ui::map_browser
