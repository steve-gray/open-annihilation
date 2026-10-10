// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The player's own folder's opener and the notices of the engine's own that
// show over a screen (Runtime::UserFolderState): the main menu's notice of
// the saved games' move, the notice of where the game folder was found
// (runtime_found_install.cpp), and the warning that the mod's games cannot
// start (runtime_mod_warning.cpp). Each is a notice screen of the OA layer
// (NoticeScreen, oa_layer.hpp), which shows them one at a time.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/ui/engine_settings/notice.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace oa::app {

struct Runtime::UserFolderState {
    /// Shows folders: the system's file manager in a run someone watches,
    /// else a record of the requests (opened).
    FolderOpenerHooks opener{};
    /// The folders a run nobody watches asked to show, in order.
    std::vector<fs::path> opened;
    /// Frames in a row the main menu has shown as itself.
    uint32_t main_menu_frames{};
    /// The main menu's warning that the mod's games cannot start waits to be
    /// told: once from each start, and again after a refused start the
    /// screen it was refused on could not show it over.
    bool mod_warning_due{true};
    /// Frames in a row the main menu has shown as itself while the mod's
    /// warning waits.
    uint32_t mod_warning_frames{};
    /// Warnings that the mod's games cannot start this run has shown.
    uint32_t mod_warnings_shown{};
    /// --check-user-folder and --check-mod-warning show the main menu's
    /// notices although nobody watches the run.
    bool check_shows_notice{};
    /// Notices this run has shown.
    uint32_t notices_shown{};

    /// Tells whether the run is one nobody watches: unattended, on CI, or on
    /// a video driver that shows no window.
    ///
    /// @param unattended the run is unattended (Options::unattended)
    /// @return true when nobody watches
    [[nodiscard]] static bool unwatched(bool unattended);

    /// Shows a notice over a screen of the front end once the OA layer is
    /// free (OaLayer::show_when_free): darkening that screen, its open button
    /// showing a folder through the player's opener (open_player_folder),
    /// its failure said in amber, and OK, Enter and Escape closing it with
    /// the closing sound; it closes when that screen goes.
    ///
    /// @param runtime the runtime
    /// @param told the notice
    /// @param folder_shown the folder its open button shows
    /// @param over the screen of the game it shows over
    static void show_notice(
        Runtime& runtime, oa::ui::engine_settings::Notice told, fs::path folder_shown, Screen over
    );

    /// Tells whether a notice shows on the OA layer or waits for it, so that
    /// another of these notices waits until it is closed.
    ///
    /// @param runtime the runtime
    /// @return true while one does
    [[nodiscard]] static bool notice_pending(const Runtime& runtime) noexcept;
};

} // namespace oa::app
