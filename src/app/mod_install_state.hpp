// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The mod packages a run installs (Runtime::ModInstallState): the package
// being installed, what its plan asks, its unpacking, the prompt over the
// main menu that asks and tells, and the folders left to delete, which
// runtime_mod_install.cpp drives and draws as an overlay.
#pragma once

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/prompts.hpp"
#include "oa/app/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace oa::app {

struct Runtime::ModInstallState {
    /// Where the install of the package taken is.
    enum class Stage : uint8_t {
        idle,      ///< no package is being installed; the prompt, if any, tells
        asking,    ///< its question shows
        unpacking, ///< its files are unpacked, the progress shown
        telling,   ///< what came of it shows
    };
    Stage stage{Stage::idle};
    /// The prompt over the main menu and what its buttons answer; empty
    /// while none shows.
    std::optional<package_install::PackagePrompt> shown;
    /// The package taken from the inbox, as it was opened.
    std::filesystem::path file;
    /// Where that package came from. installed is set when unpacking starts.
    package_install::Origin origin{};
    /// The copy the platform made of it, which it releases once the install
    /// is done with it; empty when the game reads the file where it is.
    std::filesystem::path opened_copy;
    /// The package, read and checked.
    std::optional<package_install::Package> package;
    /// The kind of the package taken, from package->kind.
    const package_install::PackageKind* kind{};
    package_install::Incoming incoming{}; ///< what the package installs
    package_install::InstallPlan plan{};  ///< where it goes and what it asks
    /// The unpacking while it runs, and until its change is put in place.
    std::unique_ptr<package_install::Unpacking> unpacking;
    std::string target;               ///< the folder in the kind's root the change acts on
    package_install::Change change{}; ///< the change the answer picked
    package_install::InstalledPackage expected{}; ///< what the target held when it was planned
    bool target_played{};                         ///< the target is the mod the game plays
    /// The unpacking is done and the prompt says the files are being put in
    /// place: the change is made on the next frame, after that is drawn.
    bool placing{};
    std::filesystem::path told_folder; ///< the folder OPEN FOLDER shows
    std::filesystem::path play_folder; ///< the folder PLAY NOW plays
    /// The folders to delete, a step each main menu frame.
    package_install::Discarder discarder;
    /// Frames in a row the main menu has shown as itself.
    uint32_t settled_frames{};
    /// The key whose press closed the prompt, until it is released; 0 for none.
    uint32_t latched_key{};
    /// --check-mod-install shows the prompts although nobody watches the run.
    bool check_shows_prompts{};
    /// Prompts this run has shown.
    uint32_t prompts_shown{};
    /// A catalogue map pack installing off the main menu: nothing is shown,
    /// and a finished install returns to idle instead of telling.
    bool quiet{};
    /// A catalogue map pack whose plan asks, put back until the main menu
    /// settles, so it is not opened again on every frame off the menu.
    std::filesystem::path catalogue_question{};

    /// Ends a quiet install: no prompt, and idle for the next package.
    ///
    /// @return true when the install was quiet
    bool finish_quietly() {
        if (!quiet)
            return false;
        shown.reset();
        quiet = false;
        stage = Stage::idle;
        return true;
    }

    /// When the hand-off folder is looked in next, in SDL ticks.
    uint64_t next_handoff_ms{};

    /// Shows a prompt over the main menu.
    ///
    /// @param made the prompt
    void show(package_install::PackagePrompt made);

    /// Gives the copy the platform made of the package back to it to remove.
    void release_opened_copy();

    /// Ends the install of the package taken: its copy released and the
    /// inbox told.
    void finish_package();

    /// Returns where the prompt's top left corner stands on the main menu,
    /// which centres it on the picture.
    ///
    /// @param height the prompt's height
    /// @return the corner, at the picture's scale
    [[nodiscard]] static oa::ui::frontend_renderer::Placement prompt_placement(int32_t height);

    /// The prompt overlay's input: while the prompt shows over the main menu
    /// it takes every input; its buttons answer, and the key that answered
    /// is latched.
    ///
    /// @param context the screen context, with the input
    /// @param state unused
    /// @return 1 when the input was taken
    static int prompt_event(oa::app::ScreenContext* context, void* state);

    /// Sets a question aside once a screen other than the main menu shows:
    /// its package waits again in the inbox.
    ///
    /// @param context the screen context
    /// @param state unused
    static void prompt_tick(oa::app::ScreenContext* context, void* state);

    /// Darkens the main menu and draws the prompt over it.
    ///
    /// @param context the screen context, with the frame
    /// @param state unused
    static void prompt_draw(oa::app::ScreenContext* context, void* state);
};

} // namespace oa::app
