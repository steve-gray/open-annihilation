// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// English texts the folder chooser passes to the interface catalogue. oa-tool joins
// this list with the other modules' lists.

#pragma once

#include <string_view>

namespace oa::ui::folder_chooser {

/// English texts this module passes to the interface catalogue
/// (oa/data/languages/interface_text.hpp). oa-tool sorts the modules'
/// lists together and keeps one of each text.
inline constexpr std::string_view interface_words[] = {
    "BACK",
    "Browse…",
    "Choose the folder in the system's own window.",
    "Choose the folder its installer is in, usually Downloads.",
    "Choose the folder that holds totala1.hpi, or the folder that holds the installer of the Total "
    "Annihilation demo (1997).",
    "Choose your Total Annihilation folder",
    "GAME FOLDER",
    "Look through this computer's folders, drives and SD cards.",
    "No folders here.",
    "OPEN ANNIHILATION",
    "Or find the folder yourself:",
    "PARENT FOLDER",
    "PLAY THIS FOLDER",
    "QUIT",
    "The Total Annihilation demo (1997)",
    "This folder can be played: {folder}",
    "Total Annihilation",
    "Total Annihilation is on this computer in more than one place. Choose the one to play.",
    "Total Annihilation was found on this computer. Play it, or choose another folder.",
    "Use the desktop's folder dialog",
};

} // namespace oa::ui::folder_chooser
