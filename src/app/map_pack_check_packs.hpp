// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map pack check's two made-up packs, which --check-map-packs and
// --check-map-browser write into the player's own Maps folder
// (runtime_map_packs_check.cpp).
#pragma once

#include <filesystem>
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

} // namespace oa::app
