// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// English texts Get Map passes to the interface catalogue. oa-tool joins
// this list with the other modules' lists.

#pragma once

#include <string_view>

namespace oa::ui::frontend_multiplayer {

/// English texts this module passes to the interface catalogue
/// (oa/data/languages/interface_text.hpp). oa-tool sorts the modules'
/// lists together and keeps one of each text.
inline constexpr std::string_view interface_words[] = {
    "Add {registry} to get {pack}",
    "CANCEL",
    "COMMANDER",
    "Cancel",
    "Couldn't get {pack}: {reason}",
    "Downloading {pack} · {done} of {size}",
    "Downloads from {registry} are off (Settings › Downloads).",
    "GET MAP",
    "LOAD",
    "Mod",
    "OK",
    "Off",
    "Open Annihilation",
    "PLAYERS",
    "PREV",
    "Platform",
    "Rules",
    "SELECT",
    "SETTINGS",
    "SIZE",
    "The host's release of {pack} is no longer offered by {registry}.",
    "Total Annihilation 3.1c",
    "UPDATE",
    "View",
    "You have release {mine} of {pack}; the host plays release {theirs}.",
    "{pack} {version} · {size} from {registry}",
};

} // namespace oa::ui::frontend_multiplayer
