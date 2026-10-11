// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// English texts network play's notices passes to the interface catalogue. oa-tool joins
// this list with the other modules' lists.

#pragma once

#include <string_view>

namespace oa::netgame::match {

/// English texts this module passes to the interface catalogue
/// (oa/data/languages/interface_text.hpp). oa-tool sorts the modules'
/// lists together and keeps one of each text.
inline constexpr std::string_view interface_words[] = {
    "Autopause: only the host can unpause",
    "Timeout: reject {player} ({yes} yes/{no} no/{voters}, {seconds}s)",
    "Vote: reject {player} ({yes} yes/{no} no/{voters}, {seconds}s)",
    "{player} has cheats enabled",
    "{player} rejected",
    "{player} reports VerCheck issues with {count} other players",
    "{player} stays in the game",
    "{player} tried to unpause.",
};

} // namespace oa::netgame::match
