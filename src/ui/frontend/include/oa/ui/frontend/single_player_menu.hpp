// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What SINGLE.GUI offers once the panel has loaded.
#pragma once

#include "oa/ui/frontend/options.hpp"

namespace oa::ui::frontend {

/// Which entries SINGLE.GUI can open.
///
/// Each flag starts on. A setup turns an entry off and never turns one back on.
struct SinglePlayerOffers {
    bool saved_games = true; ///< LOADGAME.GUI is in the game data
    bool any_mission = true; ///< the any-mission screen is in the game data
    bool skirmish = true;    ///< the base game can play a skirmish
};

/// Greys or hides the SINGLE.GUI entries the game cannot open.
///
/// Load Game is greyed when saved games are not offered. Any Mission is
/// hidden when it is not offered. Skirmish is greyed when it is not offered,
/// which is the Total Annihilation demo (1997) as the base game: the button
/// stays on the menu, takes no press and its key does nothing. New Campaign,
/// Options and Previous Menu are left as the menu authored them.
///
/// @param[in,out] panel The loaded SINGLE.GUI.
/// @param offers What this game can open.
void apply_single_player_offers(Panel& panel, SinglePlayerOffers offers) noexcept;

} // namespace oa::ui::frontend
