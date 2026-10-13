// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Shown text for a map list. The name a row selects stays with the caller.
#include "oa/ui/frontend_multiplayer/map_list.hpp"

#include "oa/data/languages/interface_text.hpp"
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

} // namespace

std::vector<std::string> map_list_texts(std::span<const MapListFact> rows) {
    std::vector<std::string> plain;
    plain.reserve(rows.size());
    for (const MapListFact& row : rows)
        plain.push_back(plain_text(row));

    std::vector<std::string> texts;
    texts.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        std::string text;
        if (!rows[index].pack) {
            text = plain[index];
        } else {
            bool shared = false;
            for (std::size_t other = 0; other < plain.size(); ++other) {
                if (other != index && plain[other] == plain[index]) {
                    shared = true;
                    break;
                }
            }
            if (!shared) {
                text = plain[index];
            } else {
                const std::string mark = pack_mark(rows[index]);
                text = mark.empty() ? plain[index] : plain[index] + " (" + mark + ")";
            }
        }
        if (!rows[index].blocked_reason.empty()) {
            const std::string reason = shown_reason(rows[index].blocked_reason);
            if (!reason.empty())
                text.append("  ").append(reason);
        }
        texts.push_back(std::move(text));
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
