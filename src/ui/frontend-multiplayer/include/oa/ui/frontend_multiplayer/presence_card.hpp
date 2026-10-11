// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The presence card: what the card of an OA player's badge in the battle
// room says (presence_card), from that player's presence record. It is
// pure: it reads the lobby and says what the card shows.
#pragma once

#include "oa/netgame/presence.hpp"
#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace oa::ui::frontend_multiplayer {

/// What this machine has of the map pack the host plays, as the card's Map
/// line says it.
enum class PackState {
    unknown,       ///< not known here: the line names the pack alone
    installed,     ///< installed here: "✓ installed", the line in lime
    missing,       ///< not installed here: "· not installed"
    other_release, ///< another release of it is installed here: "· another release here", in amber
};

/// The colour a line's value is drawn in.
enum class CardTone : uint8_t {
    plain, ///< the text colour
    amber, ///< the lock colour: something differs
    lime,  ///< the accent: all is well
};

/// One line of the card: a label and its value, both already translated.
struct PresenceCardLine {
    std::string label{}; ///< "Engine", "Mod", "Rules", "Game", "View", "Dev mode" or "Map"
    std::string value{}; ///< what the player's presence record says
    CardTone tone{};     ///< the value's colour
};

/// What the card of one player's OA badge says, every word translated.
struct PresenceCard {
    std::string title{};                   ///< the player's name in capitals
    std::string aside{};                   ///< "<platform> · <architecture>"; empty without them
    std::vector<PresenceCardLine> lines{}; ///< the lines, top to bottom
    std::vector<std::string>
        hacks{};              ///< the first game hacks' ids, at most presence_card_listed_hacks
    std::string hacks_more{}; ///< "· <n> more" for the game hacks not listed; empty for none
};

/// The game hacks the card lists by id; the others are counted in PresenceCard::hacks_more.
inline constexpr std::size_t presence_card_listed_hacks = 3;
/// Returns what the card of a slot's player says, from that player's
/// presence record alone (lobby_presence_record): a setup block's bytes are
/// never read for it.
///
/// The title is the player's name in capitals. A player whose record has
/// not come, an OA build before 0.8 that sends none, gets one line,
/// "Engine: Open Annihilation (before 0.8)". With a record the lines are, in
/// order:
/// - Engine: "Open Annihilation <version>", " (development build)" in place
///   of a version's "-<label>"; the aside says "<platform> · <architecture>";
/// - Mod: "<name> <version> · rev <packaging revision>", or "Total
///   Annihilation 3.1c" for a record without a mod;
/// - Rules: "The host's" on the host's card; otherwise "Differ from the
///   host's" in amber (lobby_rules_differ_from_host), with " (rev <n>)" when
///   the host's record names the same mod at packaging revision n, another;
///   "Same as the host's" when the two sim hashes are known and equal (a
///   3.1c host plays the plain baseline's); "Unknown" otherwise;
/// - Game: how many hacks change the game; View: how many hacks change only
///   the view; Dev mode: "On", with " · <n> overrides" when it counts any,
///   or "Off". A record that leaves out one of these fields leaves out its
///   line;
/// - Map, on the host's card when its record names a map pack: "<name, or
///   the key> <version>", then by `pack_state` " ✓ installed" in lime,
///   " · another release here" in amber, " · not installed", or nothing
///   when the state is not known.
/// The first presence_card_listed_hacks game hacks' ids follow, and
/// "· <n> more" for the rest. Every label and fixed word goes through the
/// lobby's translation (lobby_translated).
///
/// @param lobby the battle room
/// @param slot the player's slot, 0..9
/// @param pack_state what this machine has of a map pack; unset counts as unknown
/// @return the card; nothing for a slot without the OA badge (lobby_row_badge)
[[nodiscard]] std::optional<PresenceCard> presence_card(
    Lobby& lobby,
    int32_t slot,
    const std::function<PackState(const netgame::PresenceMapPack&)>& pack_state
);

} // namespace oa::ui::frontend_multiplayer
