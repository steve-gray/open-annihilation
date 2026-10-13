// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The words a map list shows for each map, beside the name the game uses.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::frontend_multiplayer {

/// What one map-list row is, before the shown text is chosen.
///
/// The views are read only while `map_list_texts` runs. A base map ignores
/// `title`, `file_title` and `pack_name`: its row is `key`, the file name.
struct MapListFact {
    std::string_view key{};        ///< the name selection, saves and the battle room use
    std::string_view title{};      ///< the pack index's name; empty when it has none
    std::string_view file_title{}; ///< the map file's title, when the source already holds it
    std::string_view pack_name{};  ///< the pack's name
    bool pack = false;             ///< a pack map; a base map's row stays `key`
    /// The short reason a greyed row shows, in English; empty when it can be played.
    std::string_view blocked_reason{};
};

/// The words each row shows, in the same order as `rows`.
///
/// A pack map shows its index name, or the map file's title when the index
/// has none, or `key` when neither is known. A pack row whose words would
/// match another row adds the pack's name in brackets, or the pack's id when
/// the pack has no name. A greyed row then adds its reason after two spaces.
/// Nothing is sorted.
///
/// @param rows the maps, in list order
/// @return one string per row
[[nodiscard]] std::vector<std::string> map_list_texts(std::span<const MapListFact> rows);

/// The row whose shown text or key is `query`, exactly.
///
/// The shown text is tried first, then the key, so a pack map titled like a
/// base map is not chosen by the base map's words. An empty query matches
/// nothing. `keys` is read only when it has one entry per shown text.
///
/// @param texts the words the rows show
/// @param keys the rows' names; empty, or one per text
/// @param query the shown text or the key
/// @return the row, from 0; -1 when none matches
[[nodiscard]] int32_t map_list_row(
    std::span<const std::string> texts, std::span<const std::string> keys, std::string_view query
);

/// How a stored refusal should be shown.
enum class MapRefusalKind : uint8_t {
    ready_demo,  ///< "This map is for the TA demo."
    ready_for,   ///< "This map is for {name}."
    ready_needs, ///< "Needs {name}, which is off." and the Library sentence
    detail,      ///< a fit detail, wrapped as "Doesn't fit …: {reason}"
};

/// The parts of a refusal stored in English.
struct MapRefusalParts {
    MapRefusalKind kind = MapRefusalKind::detail;
    /// The name in a "for" or "needs" sentence; empty otherwise.
    std::string_view name{};
};

/// Reads a refusal stored in English.
///
/// A sentence that starts "This map is " or "Needs " is shown as it is,
/// translated. Anything else is a fit detail.
///
/// @param reason the stored refusal; empty is a detail
/// @return how to show it; `name` points into `reason`
[[nodiscard]] MapRefusalParts map_refusal_parts(std::string_view reason);

} // namespace oa::ui::frontend_multiplayer
