// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// English texts the first-run setup passes to the interface catalogue. oa-tool joins
// this list with the other modules' lists.

#pragma once

#include <string_view>

namespace oa::ui::first_run {

/// English texts this module passes to the interface catalogue
/// (oa/data/languages/interface_text.hpp). oa-tool sorts the modules'
/// lists together and keeps one of each text.
inline constexpr std::string_view interface_words[] = {
    "A random 128-bit number, made the first time you download from a registry, not when the "
    "game starts. Each registry that asks has its own. Core Prime asks for one. It is sent "
    "only when asking that registry for a download. Turning it off forgets it, and downloads "
    "from that registry stop. Built-in languages and files you open keep working.",
    "A random install ID, one per registry, is sent only when downloading from that registry.",
    "Active",
    "Back",
    "Built in",
    "Cancel download",
    "Check for content updates is set to Never, and Core Prime's install ID is turned off.",
    "Check for content updates stays at Automatically, and Core Prime's install ID stays on.",
    "Checking…",
    "Choose your language.",
    "Couldn't download",
    "Downloads are off, so language packs stay offline.",
    "Downloads, updates and a number only you hold",
    "Downloading",
    "Get",
    "Get language packs, mods and map packs from Core Prime.",
    "Get, update and switch mods, map packs and languages. The number is how many mods you have.",
    "Installed",
    "Library",
    "Lists of what is new are checked as the game starts and when the Library opens.",
    "Next",
    "Not now",
    "ONLINE",
    "ONLINE FEATURES",
    "Offline",
    "Open Annihilation's own settings: controls, graphics, the interface's size, downloads and "
    "your files.",
    "Other languages download from Core Prime. The first download makes a random install ID, "
    "which the next step explains.",
    "Previous",
    "Retry",
    "Settings",
    "Skip setup",
    "Start playing",
    "THE MAIN MENU",
    "The Library is where you get mods, map packs and languages. Settings is where you change "
    "how Open Annihilation looks and plays. Everything else on this menu is the game's own.",
    "The catalogue could not be read, so language packs stay offline.",
    "Turn on online features",
    "Two buttons for everything Open Annihilation adds",
    "WELCOME",
    "Welcome",
    "Welcome to Open Annihilation",
    "Your install ID",
    "{step} · {index} OF {count}",
    "of",
};

} // namespace oa::ui::first_run
