// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// English texts the map browser passes to the interface catalogue. oa-tool joins
// this list with the other modules' lists.

#pragma once

#include <string_view>

namespace oa::ui::map_browser {

/// English texts this module passes to the interface catalogue
/// (oa/data/languages/interface_text.hpp). oa-tool sorts the modules'
/// lists together and keeps one of each text.
inline constexpr std::string_view interface_words[] = {
    "1 player",
    "2",
    "3",
    "4",
    "5–10",
    "All",
    "Base",
    "Base game",
    "Battle room · Total Annihilation 3.1c",
    "Battle room · playing {mod}",
    "Cancel",
    "Choose a map",
    "Doesn't fit",
    "Doesn't fit {game}",
    "Download and select",
    "Downloading…",
    "Fit with {game} is checked when it's installed",
    "Fits {game} ✓",
    "Installed packs",
    "L",
    "Loading preview",
    "M",
    "Maps",
    "No map matches these filters",
    "No preview",
    "Online",
    "PLAYERS",
    "S",
    "SIZE",
    "Search maps",
    "Select",
    "Skirmish · Total Annihilation 3.1c",
    "Skirmish · playing {mod}",
    "TA 3.1c",
    "{count} players",
    "{players} · {size}",
    "{size} KB",
    "{size} MB",
};

} // namespace oa::ui::map_browser
