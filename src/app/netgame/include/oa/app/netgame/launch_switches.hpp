// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The 3.1c command line's network play switches -b -c -e -h -n -p -t -y.
// launch_switch_handler returns the handler oa::app::command_line::parse
// hands them to; the parse applies the intro skip -c, -n and -y ask for.
// enable_netsetup is the setup request a launching extension may make; -y
// does not make it, since this game has no built-in internet lobby to open.
#pragma once

#include "oa/app/command_line.hpp"
#include "oa/ui/frontend_multiplayer/launch_block.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oa::app::netgame::launch {

using oa::ui::frontend_multiplayer::launch::LaunchBlock;
namespace connection_type = oa::ui::frontend_multiplayer::launch::connection_type;

using oa::app::command_line::kTextBytes;

inline constexpr int32_t kDefaultNetTimeoutSeconds = 30;
inline constexpr int32_t kMaximumNetTimeoutSeconds = 300;
inline constexpr int32_t kMaximumSendErrorPercent = 100;
// "-p" followed by an argument that starts with '-'.
inline constexpr int32_t kPacingFromDash = -1;
// "-n<type>": only these connection types are stored; type 1 takes an
// address after ':'.
inline constexpr int32_t kFirstConnectionType = 1;
inline constexpr int32_t kLastConnectionType = 4;
inline constexpr int32_t kAddressedConnectionType = 1;

/// The battle room options "-b WORD" names, in the launch block's codes; 0 names none.
///
/// The words are lock, deathplays, deathends, deathmatch, truelos, circlos,
/// permlos, mapping, fixedloc, cheating and watching, in any case.
struct BattleWords {
    int32_t death_option{}; ///< 1 continues, 2 ends, 3 deathmatch
    int32_t los{};          ///< 1 true, 2 circular, 3 permanent line of sight
    int32_t location{};     ///< 1 fixed start positions
    int32_t mapping{};      ///< 2 the map starts mapped
    int32_t cheats{};       ///< 2 allows the cheat codes
    int32_t watching{};     ///< 2 allows watchers
    uint32_t lock{};        ///< 1 locks the battle room's options

    /// Tells whether any word was named.
    ///
    /// @return true when a field is not 0
    [[nodiscard]] bool named() const noexcept {
        return death_option != 0 || los != 0 || location != 0 || mapping != 0 || cheats != 0 ||
               watching != 0 || lock != 0;
    }
};

// What the network switches set. Only the net timeout, the send-error
// percentage and the notes are reset by a parse; everything else keeps
// earlier values. "-n" and "-h" write the launch block (its connection type,
// address, hosting flag and session name), and "-c" clears it; the *_copy
// fields belong to its second copy, which a launching extension may refresh
// from the first, and which "-c" leaves alone.
struct LaunchSwitches {
    int32_t net_timeout_seconds{kDefaultNetTimeoutSeconds}; // Game.player_timeout_seconds, -t
    int32_t send_error_percent{};                           // Game.send_error_percent, -e
    uint8_t netsetup{};        // the setup request a launching extension makes
    uint8_t send_pacing_set{}; // -p seen with an argument, or a launch's pfs
    int32_t send_pacing{};     // sends per second, -p
    // The game's launch block: -c clears it; -n, -n1:<address>, -h and
    // -h<name> write it; a launch file and a launching extension fill it.
    LaunchBlock block{};
    // "-c FILE" names a launch file, an .oalaunch or TA Forever's INI, told
    // apart by what it holds; "-c -" reads it from standard input. The file
    // is read once the other switches are known; the switches after the last
    // "-c" replace its fields. A "-c" with no word after it names no file.
    bool launch_seen = false;
    std::string launch_path{};
    // "-h" stored a session name since the last "-c". It replaces the
    // launch file's game name and host flag.
    bool session_from_switch = false;
    // "-n" stored a connection type, or an address, since the last "-c".
    // Each replaces the launch file's. An ignored type, such as "-n5", sets
    // neither.
    bool connection_type_from_switch = false;
    bool address_from_switch = false;
    // The battle room options "-b" named since the last "-c". They replace
    // the launch file's, and an option such as --metal replaces them.
    BattleWords battle{};
    int32_t connection_type_copy{};
    char host_game_name_copy[kTextBytes]{};     // each -h name appends
    char connection_address_copy[kTextBytes]{}; // each address appends
    // Lines for the game's log about switches taken without an effect, or
    // with one 3.1c does not have: a switch with no meaning in this game, a
    // "-b" word that names no option, and what a "-c" cleared.
    std::vector<std::string> notes{};
};

/// Returns the switch handler that parses the network switches into `switches`.
///
/// A switch's argument is the text attached to its letter or, when none is,
/// the next token, which is then not parsed as a switch. Its reset sets
/// net_timeout_seconds to kDefaultNetTimeoutSeconds and send_error_percent to
/// 0, and drops the notes. The letters it takes:
///  - "-b <word>" names a battle room option (BattleWords), in any case; a
///    word that names none, or no word, is noted.
///  - "-c <file>" clears the launch block and what -h, -n and -b stored
///    (clear_launch_record), then stores the path of a launch file
///    (launch_path). A separate path is the next token, whatever it starts
///    with; "-c -" stores "-" for standard input; with no token left the
///    path is empty. The file is read later.
///  - "-e <percent>" stores the send-error percentage; outside 0..100 or an
///    argument starting with '-' stores 0.
///  - "-h<name>" hosts under the name (set_host_game); a separate name that is
///    missing, empty or starts with '-' is consumed and nothing is stored.
///    A stored name sets session_from_switch.
///  - "-n<type>[:<address>]" stores the connection type (set_connection_type)
///    and, for type 1, the address after ':' (set_connection_address),
///    setting connection_type_from_switch and address_from_switch.
///  - "-p <n>" stores sends per second, kPacingFromDash for a '-' argument.
///  - "-t <seconds>" stores the net timeout; outside 30..300 or an argument
///    starting with '-' stores kDefaultNetTimeoutSeconds.
///  - "-y" is noted: it opens 3.1c's built-in internet lobby, which this
///    game does not have.
/// "-c", "-n" and "-y" also ask the parse to skip the intro, with or without
/// an argument.
///
/// @param switches Receives the parsed values; must outlive the handler.
/// @return A handler for oa::app::command_line::parse whose context is `switches`.
[[nodiscard]]
oa::app::command_line::SwitchHandler launch_switch_handler(LaunchSwitches* switches) noexcept;

/// Stores the hosting flag and the game name that "-h" gives.
///
/// The name replaces the block's session name but is appended to
/// host_game_name_copy, so a repeated "-h" accumulates there; both keep at
/// most 63 characters.
///
/// @param[in,out] switches Receives block.hosting, block.session_name and host_game_name_copy.
/// @param hosting Nonzero to host; stored as 1 or 0.
/// @param name Game name to host under; null leaves both names unchanged.
void set_host_game(LaunchSwitches& switches, uint32_t hosting, const char* name) noexcept;

/// Stores the address to connect to that "-n1:<address>" gives.
///
/// The address replaces the block's address but is appended to
/// connection_address_copy; both keep at most 63 characters.
///
/// @param[in,out] switches Receives block.address and connection_address_copy.
/// @param address Address text after the ':'.
void set_connection_address(LaunchSwitches& switches, const char* address) noexcept;

/// Stores the connection type that "-n<type>" gives in the block and its copy.
///
/// @param[in,out] switches Receives block.connection_type and connection_type_copy.
/// @param type Connection type; values outside kFirstConnectionType..kLastConnectionType
///             are ignored.
void set_connection_type(LaunchSwitches& switches, int32_t type) noexcept;

/// Requests the network setup the main menu runs on its first update, for a launching extension.
///
/// @param[out] switches Receives netsetup = 1.
void enable_netsetup(LaunchSwitches& switches) noexcept;

/// Clears the launch record as "-c" does, noting what is lost.
///
/// The launch block, and what -h, -n and -b stored since the last "-c", are
/// cleared; the block's second copy is left. When -h, -n or -b had stored
/// something, a note says it is lost.
///
/// @param[in,out] switches the switches
void clear_launch_record(LaunchSwitches& switches);

/// Reads one "-b" word into the battle room options it names.
///
/// @param[in,out] battle the options; the word's field is replaced
/// @param word the word, in any case
/// @return false when the word names no option, which leaves `battle` alone
bool take_battle_word(BattleWords& battle, const char* word) noexcept;

/// Applies the battle room options "-b" named to a launch block.
///
/// @param battle the options; a field that is 0 leaves the block's
/// @param[in,out] block the block
void apply_battle_words(const BattleWords& battle, LaunchBlock& block) noexcept;

} // namespace oa::app::netgame::launch
