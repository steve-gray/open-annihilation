// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The launch files the system opens in the game: a .oalaunch file double-clicked in the file
// manager, opened with the game or dropped on its window, and any other file whose text
// begins a launch, as macOS's `open -f` hands over a launch piped to it, written to a
// temporary .txt file. Each is read as it arrives and waits here until the game takes it:
// before the game's first screen, as the start's own launch, as -c FILE is; at the main menu,
// where the game goes to that launch's battle room; and anywhere else, a match or a game being
// set up among them, where the game tells the player it did not open the file and takes
// nothing. Every function may be called from any thread.
#pragma once

#include "oa/app/netgame/launch_file.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::netgame {

/// The most bytes of an opened file that are read to tell a launch by what it holds.
inline constexpr std::size_t kOpenedFileProbeBytes = 4096;

/// The frames the main menu shows, settled, before an opened launch leaves it: a start that
/// passes the main menu at its first update takes none there.
inline constexpr uint32_t kOpenedLaunchMenuFrames = 2;

/// A launch file the system opened in the game, read as it arrived, so that a temporary file
/// removed since is still read.
struct OpenedLaunch {
    std::string source{};  ///< the file's path, UTF-8, which a refusal names
    std::string text{};    ///< the file's bytes; empty when it could not be read
    std::string problem{}; ///< why it could not be read, as a refusal says it; empty when it was
};

/// Tells whether a file the system opened in the game is a launch file: a .oalaunch file, by
/// its extension in any case, or any other file whose text begins an Open Annihilation launch
/// (launch_file_kind). TA Forever's INI is taken as a launch only after -c or --launch, or
/// named .oalaunch.
///
/// @param extension the file's extension, without its dot
/// @param first_bytes the file's first bytes, at most kOpenedFileProbeBytes of them
/// @return true for a launch file
[[nodiscard]] bool
is_launch_file(std::string_view extension, std::string_view first_bytes) noexcept;

/// Reads a file the system opened in the game when it is a launch file: its first bytes when
/// its name does not say, then the whole file, at most kLaunchFileLimit bytes.
///
/// @param file the file
/// @return the launch, with the problem when it cannot be read whole; nothing when the file
///         is not a launch file, or cannot be read and is not named one
[[nodiscard]] std::optional<OpenedLaunch> read_opened_launch(const std::filesystem::path& file);

/// Keeps an opened launch file until the game takes it. A file already waiting is not kept
/// twice.
///
/// @param launch the launch, as read_opened_launch read it
void post_opened_launch(OpenedLaunch launch);

/// Takes the launch file that waits longest.
///
/// @return the launch; nothing when none waits
[[nodiscard]] std::optional<OpenedLaunch> take_opened_launch();

/// Tells whether an opened launch file waits.
///
/// @return true while one does
[[nodiscard]] bool opened_launch_waiting();

/// Reads an opened launch file into a plan, as -c FILE reads the file it names.
///
/// @param opened the launch
/// @return the plan; its source is the file's path
/// @throws LaunchRefusal the file could not be read, or read_launch_text refuses it
[[nodiscard]] LaunchPlan read_opened_launch_plan(const OpenedLaunch& opened);

/// What the running game plays, as far as an opened launch's content is compared with it.
struct RunningContent {
    /// The base game and expansions below are those mounted: the game plays the player's
    /// copy of them (Runtime::presence_announcement). Otherwise they are not compared.
    bool bundles_known = false;
    std::string base_game{};               ///< the base game's id
    std::vector<std::string> expansions{}; ///< the expansions that change the rules, by id
    std::string mod_id{};                  ///< the id of the mod in play; empty for none
    std::string mod_folder{};              ///< the name of the mod folder in play; empty for none
};

/// Tells whether a launch names content the running game does not play: a mod other than
/// the one in play, matched by its id or its folder's name without ASCII case, or, where the
/// game plays the player's copy, a base game or expansions other than those mounted. The
/// music, which changes no rule, is not compared. A launch that names no content plays what
/// runs.
///
/// @param launch the launch's content
/// @param running what the game plays
/// @return true when the launch would need the game started again
[[nodiscard]] bool
launch_content_differs(const LaunchContentChoice& launch, const RunningContent& running);

/// Where the running game stands while an opened launch file waits.
enum class GameStanding : uint8_t {
    passing,       ///< the intro, a loading screen or a change of screen: the file waits
    main_menu,     ///< the main menu, settled, with no dialog over it
    single_player, ///< the single-player menu, which goes back to the main menu
    elsewhere,     ///< any other screen: a game being set up, the multiplayer screens, a page
    match,         ///< a match runs
};

/// What the game does with an opened launch file where it stands.
enum class OpenedLaunchStep : uint8_t {
    wait,              ///< leaves the file waiting
    go_to_battle_room, ///< takes the file as the launch and opens its battle room
    back_to_main_menu, ///< goes back to the main menu, where the file is taken
    refuse_in_match,   ///< takes nothing, and says so in the match's message log
    refuse,            ///< takes nothing, and says so in a notice
};

/// Chooses what the game does with an opened launch file where it stands: from the main menu
/// it goes to the launch's battle room, and the single-player menu goes back there first;
/// during a match, a game being set up or any other screen it takes nothing. While a launch
/// is already under way, the main menu and the single-player menu take nothing either.
///
/// @param standing where the game stands
/// @param launch_under_way a launch, from the command line or opened before, is active
/// @return the step
[[nodiscard]] OpenedLaunchStep
opened_launch_step(GameStanding standing, bool launch_under_way) noexcept;

} // namespace oa::app::netgame
