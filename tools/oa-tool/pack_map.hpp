// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Packs a map folder into a .oamap. pack.cpp calls pack_map when the folder's
// manifest is oamap.yaml.

#pragma once

#include "command.hpp"

#include <filesystem>
#include <optional>

namespace oa::tool {

/// What one map pack is asked to write.
struct PackMapRequest {
    std::filesystem::path folder;                  ///< the map folder
    std::optional<std::filesystem::path> out;      ///< package path; default name when unset
    bool force{};                                  ///< replace an existing package
    std::optional<std::filesystem::path> game_dir; ///< game data, read for its palette
};

/// Packs a map folder as a .oamap.
///
/// Reads the folder's oamap.yaml header, fills each map's index from its OTA
/// and TNT, writes a palette preview from the game at game_dir, and packs the
/// manifest, the maps' files and the previews. Prints each map's name, what
/// it needs from the game, the files left out, and the package's size and
/// SHA-256.
///
/// @param request the folder, the output, whether to replace it, and the game
/// @param[in,out] output receives the summary, and diagnostics on failure
/// @return exit_done
/// @throws Failure when the folder cannot be packed
[[nodiscard]] int pack_map(const PackMapRequest& request, Output& output);

} // namespace oa::tool
