// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Shown text for a map list. The name a row selects stays with the caller.
#include "oa/ui/frontend_multiplayer/map_list.hpp"

#include "oa/data/campaign/directory_list.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/map_pack/map_name.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::frontend_multiplayer {

namespace {

/// The words a row would show before a shared title gains the pack's name.
///
/// @param row the map
/// @return those words
std::string plain_text(const MapListFact& row) {
    if (!row.pack)
        return std::string(row.key);
    if (!row.title.empty())
        return std::string(row.title);
    if (!row.file_title.empty())
        return std::string(row.file_title);
    // The key stays name@package for selection. Players see the map's own name.
    if (const auto split = oa::data::map_pack::split_pack_map_name(row.key))
        return std::string(split->stem);
    return std::string(row.key);
}

/// The pack's name a shared title is marked with, or the pack's id.
///
/// @param row the pack map
/// @return the mark; empty when the row has neither
std::string pack_mark(const MapListFact& row) {
    if (!row.pack_name.empty())
        return std::string(row.pack_name);
    const auto split = oa::data::map_pack::split_pack_map_name(row.key);
    if (!split)
        return {};
    return std::string(split->id);
}

/// The greyed reason in the language being shown.
///
/// @param english the English reason, with its name already filled
/// @return the words the row shows; empty when there is no reason
std::string shown_reason(std::string_view english) {
    if (english.empty())
        return {};
    if (english == "for the TA demo" || english == "doesn't fit")
        return std::string(oa::data::languages::interface_text(english));
    constexpr std::string_view needs = "needs ";
    constexpr std::string_view for_name = "for ";
    if (english.starts_with(needs)) {
        std::string text(oa::data::languages::interface_text("needs {name}"));
        const std::string field = "{name}";
        if (const std::size_t at = text.find(field); at != std::string::npos)
            text.replace(at, field.size(), english.substr(needs.size()));
        return text;
    }
    if (english.starts_with(for_name)) {
        std::string text(oa::data::languages::interface_text("for {name}"));
        const std::string field = "{name}";
        if (const std::size_t at = text.find(field); at != std::string::npos)
            text.replace(at, field.size(), english.substr(for_name.size()));
        return text;
    }
    return std::string(english);
}

/// The name each row shows, before a greyed row's reason.
///
/// @param rows the maps, in list order
/// @return one name per row
std::vector<std::string> shown_names(std::span<const MapListFact> rows) {
    std::vector<std::string> plain;
    plain.reserve(rows.size());
    for (const MapListFact& row : rows)
        plain.push_back(plain_text(row));

    std::vector<std::string> names;
    names.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        std::string text = plain[index];
        if (rows[index].pack) {
            bool shared = false;
            for (std::size_t other = 0; other < plain.size(); ++other) {
                if (other != index && plain[other] == plain[index]) {
                    shared = true;
                    break;
                }
            }
            if (shared) {
                const std::string mark = pack_mark(rows[index]);
                if (!mark.empty())
                    text.append(" (").append(mark).append(")");
            }
        }
        names.push_back(std::move(text));
    }
    return names;
}

} // namespace

std::vector<std::string> map_list_texts(std::span<const MapListFact> rows) {
    std::vector<std::string> texts = shown_names(rows);
    for (std::size_t index = 0; index < rows.size() && index < texts.size(); ++index) {
        if (rows[index].blocked_reason.empty())
            continue;
        const std::string reason = shown_reason(rows[index].blocked_reason);
        if (!reason.empty())
            texts[index].append("  ").append(reason);
    }
    return texts;
}

std::vector<std::size_t> map_list_order(std::span<const MapListFact> rows) {
    const std::size_t count = rows.size();
    if (count > static_cast<std::size_t>(oa::data::campaign::kSortEntryLimit))
        throw std::invalid_argument("map list exceeds the game's sorting capacity");
    const std::vector<std::string> shown = shown_names(rows);
    if (shown.size() != count)
        throw std::invalid_argument("map list exceeds the game's sorting capacity");

    std::size_t shown_bytes = 0;
    std::size_t index_bytes = 0;
    std::vector<std::string> places;
    places.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::string& name = shown[index];
        if (name.find('\0') != std::string::npos || name.size() >= 256)
            throw std::invalid_argument("map name cannot fit the game's map field");
        if (name.size() + 1 > oa::data::campaign::kSortListBytes - shown_bytes)
            throw std::invalid_argument("map list exceeds the game's sorting byte capacity");
        shown_bytes += name.size() + 1;
        places.push_back(std::to_string(index));
        if (places.back().size() + 1 > oa::data::campaign::kSortListBytes - index_bytes)
            throw std::invalid_argument("map list exceeds the game's sorting byte capacity");
        index_bytes += places.back().size() + 1;
    }

    std::string shown_list;
    std::string index_list;
    shown_list.reserve(shown_bytes);
    index_list.reserve(index_bytes);
    for (std::size_t index = 0; index < count; ++index) {
        shown_list.append(shown[index]);
        shown_list.push_back('\0');
        index_list.append(places[index]);
        index_list.push_back('\0');
    }
    oa::data::campaign::sort_paired_lists(
        shown_list.data(), index_list.data(), nullptr, static_cast<int32_t>(count)
    );

    std::vector<std::size_t> order;
    order.reserve(count);
    std::size_t at = 0;
    for (std::size_t step = 0; step < count; ++step) {
        if (at >= index_list.size())
            throw std::invalid_argument("map list exceeds the game's sorting capacity");
        const char* text = index_list.c_str() + at;
        const std::size_t length = std::strlen(text);
        std::size_t place = 0;
        for (std::size_t digit = 0; digit < length; ++digit) {
            const char glyph = text[digit];
            if (glyph < '0' || glyph > '9')
                throw std::invalid_argument("map list exceeds the game's sorting capacity");
            place = place * 10 + static_cast<std::size_t>(glyph - '0');
        }
        if (place >= count)
            throw std::invalid_argument("map list exceeds the game's sorting capacity");
        order.push_back(place);
        at += length + 1;
    }
    return order;
}

int32_t map_list_row(
    std::span<const std::string> texts, std::span<const std::string> keys, std::string_view query
) {
    if (query.empty())
        return -1;
    for (std::size_t index = 0; index < texts.size(); ++index)
        if (texts[index] == query)
            return static_cast<int32_t>(index);
    if (keys.size() != texts.size())
        return -1;
    for (std::size_t index = 0; index < keys.size(); ++index)
        if (keys[index] == query)
            return static_cast<int32_t>(index);
    return -1;
}

MapRefusalParts map_refusal_parts(std::string_view reason) {
    constexpr std::string_view demo = "This map is for the TA demo.";
    if (reason == demo)
        return {MapRefusalKind::ready_demo, {}};
    constexpr std::string_view for_head = "This map is for ";
    if (reason.starts_with(for_head) && reason.ends_with('.') &&
        reason.size() > for_head.size() + 1)
        return {
            MapRefusalKind::ready_for,
            reason.substr(for_head.size(), reason.size() - for_head.size() - 1)
        };
    constexpr std::string_view needs_head = "Needs ";
    constexpr std::string_view needs_mid = ", which is off.";
    if (reason.starts_with(needs_head)) {
        const std::size_t mid = reason.find(needs_mid);
        if (mid != std::string_view::npos && mid >= needs_head.size())
            return {
                MapRefusalKind::ready_needs,
                reason.substr(needs_head.size(), mid - needs_head.size())
            };
    }
    return {};
}

} // namespace oa::ui::frontend_multiplayer
