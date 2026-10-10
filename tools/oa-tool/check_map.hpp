// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A map pack's catalogue facts, and whether each of its maps fits the base
// game and the mods named for the check. The facts follow the keys every
// package shares. The fit is the game's own check, with the map mounted
// from the pack.

#pragma once

#include "command.hpp"

#include "oa/formats/json.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace oa::tool {

/// One mod a map pack is checked against.
struct MapTarget {
    std::filesystem::path folder{}; ///< the mod folder, layered over the game
    std::string key{};              ///< the catalogue key; empty to take it from the folder
};

/// What a map pack's fit check reads.
struct MapCheckRequest {
    std::filesystem::path pack{};     ///< the .oamap file
    std::filesystem::path game_dir{}; ///< the base game's folder
    std::vector<MapTarget> mods{};    ///< mods after the base game, in report order
};

/// Writes a map pack's catalogue facts into an object that is already open.
///
/// Reads the manifest in package use. After the keys every package shares,
/// whose revision is packaging.revision, the facts are summary (the
/// manifest's description), author (author.name), homepage, tags, requires
/// (base and engine as written) and packaging (revision, date and packager).
/// A key the manifest does not have is left out. A null `json` writes the
/// same facts as `key: value` lines.
///
/// @param manifest the oamap.yaml bytes
/// @param json the object the facts are written into; null writes text
/// @param[in,out] output receives the text when `json` is null
/// @param[in,out] problems one sentence appended when the manifest cannot be read
/// @return true when the manifest was read
[[nodiscard]] bool describe_oamap(
    std::span<const uint8_t> manifest,
    oa::formats::json::JsonWriter* json,
    Output& output,
    std::vector<std::string>& problems
);

/// Checks each map against the base game and then each mod.
///
/// The store is the one the game would build for that target: the mod's
/// folder layered first, the game's archives mounted, and the profile's data
/// layout. One map is mounted from the pack at a time and unmounted before
/// the next. A map that does not fit the base game adds one problem. A mod's
/// result is only recorded. A null `json` writes one line per map and target.
///
/// @param request the pack, the game and the mods
/// @param json the object the maps member is written into; null writes text
/// @param[in,out] output receives the text when `json` is null, and a warning
///        when a mod's key has no release
/// @param[in,out] problems one sentence appended for each map that does not
///        fit the base game, and when a target cannot be used
/// @return true when no problem was added
[[nodiscard]] bool check_map_fit(
    const MapCheckRequest& request,
    oa::formats::json::JsonWriter* json,
    Output& output,
    std::vector<std::string>& problems
);

} // namespace oa::tool
