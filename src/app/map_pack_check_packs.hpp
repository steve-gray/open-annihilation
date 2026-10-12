// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map pack check's two made-up packs, which --check-map-packs and
// --check-map-browser write into the player's own Maps folder
// (runtime_map_packs_check.cpp).
#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace oa::app {

/// The game name of the check's map that fits the game.
inline constexpr std::string_view map_pack_check_fitting_map = "check_isle@oa-check-isles";
/// The game name of the check's map whose feature file defines a feature the
/// game already has, which breaks the New names rule.
inline constexpr std::string_view map_pack_check_clashing_map = "check_clash@oa-check-clash";

/// Writes the map pack check's two packs into a Maps folder, in place of any
/// earlier copies: one whose map fits the game, and one whose map defines
/// the game's dragon's teeth again.
///
/// Throws std::runtime_error when a file cannot be written.
///
/// @param maps the Maps folder
/// @param model the stem of a model of the game the maps' rock is drawn with
void write_map_pack_check_packs(const std::filesystem::path& maps, std::string_view model);

/// One map of a pack a check writes.
struct MapPackCheckMap {
    std::string_view stem{};  ///< the map's file name, without its extension
    std::string_view title{}; ///< the title the pack's index gives the map
    bool clashes = false;     ///< true to define the game's dragon's teeth again
};

/// Writes one map pack into a folder, in place of any earlier copy.
///
/// The folder is the pack's own folder, not a Maps folder. Every map shares
/// one feature file. The scenario's mission name is the stem, and the
/// manifest's title is `title`. Throws std::runtime_error when a file cannot
/// be written.
///
/// @param folder the pack's folder
/// @param id the pack's id
/// @param pack_name the pack's name
/// @param maps the maps; a map with `clashes` makes the shared feature file clash
/// @param model the stem of a model of the game the maps' rock is drawn with
void write_map_pack_check_folder(
    const std::filesystem::path& folder,
    std::string_view id,
    std::string_view pack_name,
    std::span<const MapPackCheckMap> maps,
    std::string_view model
);

} // namespace oa::app
