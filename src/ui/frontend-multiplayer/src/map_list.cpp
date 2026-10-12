// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Shown text for a map list. The name a row selects stays with the caller.
#include "oa/ui/frontend_multiplayer/map_list.hpp"

#include "oa/data/map_pack/map_name.hpp"

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

} // namespace

std::vector<std::string> map_list_texts(std::span<const MapListFact> rows) {
    std::vector<std::string> plain;
    plain.reserve(rows.size());
    for (const MapListFact& row : rows)
        plain.push_back(plain_text(row));

    std::vector<std::string> texts;
    texts.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (!rows[index].pack) {
            texts.push_back(plain[index]);
            continue;
        }
        bool shared = false;
        for (std::size_t other = 0; other < plain.size(); ++other) {
            if (other != index && plain[other] == plain[index]) {
                shared = true;
                break;
            }
        }
        if (!shared) {
            texts.push_back(plain[index]);
            continue;
        }
        const std::string mark = pack_mark(rows[index]);
        if (mark.empty())
            texts.push_back(plain[index]);
        else
            texts.push_back(plain[index] + " (" + mark + ")");
    }
    return texts;
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

} // namespace oa::ui::frontend_multiplayer
