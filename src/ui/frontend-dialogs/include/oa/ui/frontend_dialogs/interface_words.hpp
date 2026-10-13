// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// English texts the content question passes to the interface catalogue.
// oa-tool joins this list with the other modules' lists.

#pragma once

#include <string_view>

namespace oa::ui::frontend_dialogs {

/// English texts this module passes to the interface catalogue
/// (oa/data/languages/interface_text.hpp). oa-tool sorts the modules'
/// lists together and keeps one of each text.
inline constexpr std::string_view interface_words[] = {
    "\"{title}\" was saved with {name}.",
    "\"{title}\" was saved with {name}, which is off.",
    "{name} is not copied, so this save cannot be loaded with it.",
    "{row}: {saved} / {now}",
    "{save_heading} / {now_heading}",
    "Expansions",
    "NOW",
    "Switching to {name} restarts the game's files, then loads this save.",
    "THE SAVE",
    "This run names its content, so it cannot switch.",
    "This save needs other content",
    "Turn on and load",
    "Turning {name} on restarts the game's files, then loads this save. Your other expansions stay "
    "as they are.",
};

} // namespace oa::ui::frontend_dialogs
