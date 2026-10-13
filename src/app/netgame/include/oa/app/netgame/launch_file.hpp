// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A launch file: TA Forever's -c INI, or Open Annihilation's --launch YAML.
// Both fill one launch plan. The header lives under this module's include
// folder; the ticket's file list names it beside the sources.
#pragma once

#include "oa/ui/frontend_multiplayer/launch_block.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::netgame {

/// A launch file the game refuses before a window opens.
///
/// The message names the file, the field and why. The process exits with
/// status 2.
class LaunchRefusal : public std::runtime_error {
  public:

    /// Reports a launch file the game will not start from.
    ///
    /// @param message the line, naming the file, the field and why
    explicit LaunchRefusal(const std::string& message);
};

/// Whether the launch hosts a game or joins one. Unset until a file or the command line says.
enum class LaunchRole : uint8_t {
    unset, ///< neither host nor join
    host,  ///< create the game
    join,  ///< join a game at an address
};

/// When a launched game leaves after the match. Parsed and kept; a later change gives it effect.
enum class QuitAfter : uint8_t {
    unset,      ///< the file did not say
    off,        ///< stay after the game
    game,       ///< leave when the game ends
    after_score ///< leave after the score
};

/// The base game, expansions and mod a launch names for this run.
struct LaunchContentChoice {
    std::string base_game{};               ///< content.base-game; empty when unnamed
    std::vector<std::string> expansions{}; ///< content.expansions
    std::string mod{};                     ///< content.mod, an id or a path
    bool named = false;                    ///< the file has a content group with a choice
};

/// What a launch asks to be told about the game. Parsed and kept.
struct LaunchStatusChoice {
    bool stdout_named = false;               ///< status.stdout was written
    bool standard_output = false;            ///< status.stdout asks for standard output
    std::string file{};                      ///< status.file; empty when unnamed
    QuitAfter quit_after = QuitAfter::unset; ///< status.quit-after-game
};

/// One launch, from either file form: the battle room's block, the direct game, DirectPlay and content.
struct LaunchPlan {
    oa::ui::frontend_multiplayer::launch::LaunchBlock block{};
    LaunchRole role = LaunchRole::unset;
    std::string address{};     ///< the host's address, for a join
    std::string game_name{};   ///< the game's name
    std::string player_name{}; ///< the player's name
    std::string password{};    ///< the game's password; never written to the log
    std::optional<std::array<uint8_t, 16>> dplay_guid{}; ///< wire byte order
    std::optional<uint16_t> dplay_port{};
    LaunchContentChoice content{};
    LaunchStatusChoice status{};
    bool ini = false;     ///< read from TA Forever's INI; a YAML launch is false
    std::string source{}; ///< the file's name, as it was given, or "-" for standard input
};

/// Reads a launch file's text into one plan.
///
/// An INI is a `[totala]` group of `key=value` lines, keys in any case. YAML
/// is read with the mod profile's reader and must start with `oalaunch: 1`.
/// A value the battle room cannot use throws LaunchRefusal. The password is
/// not copied into that message.
///
/// @param source the file's name, used in a refusal
/// @param text the file's bytes
/// @param yaml true for Open Annihilation's YAML, false for TA Forever's INI
/// @return the plan
/// @throws LaunchRefusal the text is not a launch file, or a field cannot be used
[[nodiscard]] LaunchPlan
read_launch_text(std::string_view source, std::string_view text, bool yaml);

/// Reads a launch file from a path, or from standard input when `path` is "-".
///
/// @param path the file, or "-" for standard input
/// @param yaml true for Open Annihilation's YAML, false for TA Forever's INI
/// @return the plan
/// @throws LaunchRefusal the file cannot be read, or read_launch_text refuses it
[[nodiscard]] LaunchPlan read_launch_file(std::string_view path, bool yaml);

} // namespace oa::app::netgame
