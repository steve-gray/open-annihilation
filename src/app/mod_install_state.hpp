// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The mod packages a run installs (Runtime::ModInstallState): the package
// being installed, what its plan asks, its unpacking, the prompt over the
// main menu that asks and tells, a question screen of the OA layer
// (QuestionScreen, oa_layer.hpp), and the folders left to delete, which
// runtime_mod_install.cpp drives.
#pragma once

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/prompts.hpp"
#include "oa/app/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace oa::app {

class QuestionScreen;

struct Runtime::ModInstallState {
    /// Where the install of the package taken is.
    enum class Stage : uint8_t {
        idle,      ///< no package is being installed; the prompt, if any, tells
        asking,    ///< its question shows
        unpacking, ///< its files are unpacked, the progress shown
        telling,   ///< what came of it shows
    };
    Stage stage{Stage::idle};
    /// The prompt over the main menu: its screen on the OA layer, or waiting
    /// for it; null while none shows.
    QuestionScreen* prompt{};
    /// What the prompt's buttons answer, left to right.
    std::vector<package_install::Answer> answers;
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
    /// --check-mod-install shows the prompts although nobody watches the run.
    bool check_shows_prompts{};
    /// Prompts this run has shown.
    uint32_t prompts_shown{};
    /// A catalogue map pack or language pack installing with nothing shown,
    /// including off the main menu. A finished install returns to idle
    /// instead of telling.
    bool quiet{};
    /// A catalogue map pack whose plan asks, put back until the main menu
    /// settles, so it is not opened again on every frame off the menu.
    std::filesystem::path catalogue_question{};

    /// Ends a quiet install: no prompt, and idle for the next package.
    ///
    /// @param runtime the runtime, whose OA layer holds any prompt
    /// @return true when the install was quiet
    bool finish_quietly(Runtime& runtime) {
        if (!quiet)
            return false;
        hide(runtime);
        quiet = false;
        stage = Stage::idle;
        return true;
    }

    /// When the hand-off folder is looked in next, in SDL ticks.
    uint64_t next_handoff_ms{};

    /// Shows a prompt over the main menu, counted among those shown: in the
    /// prompt that shows, or in a new one the OA layer shows once it is free
    /// (OaLayer::show_when_free), or at once over the screen that asked.
    ///
    /// @param runtime the runtime
    /// @param made the prompt and what its buttons answer
    /// @param at_once a screen asked it of its own: it shows over that
    ///     screen at once (OaLayer::push)
    void show(Runtime& runtime, package_install::PackagePrompt made, bool at_once = false);

    /// Shows the prompt again with what it says now, such as an unpacking's
    /// progress, as show does but not counted.
    ///
    /// @param runtime the runtime
    /// @param made the prompt and what its buttons answer
    void update(Runtime& runtime, package_install::PackagePrompt made);

    /// Takes the prompt off the OA layer, or out of its queue.
    ///
    /// @param runtime the runtime
    void hide(Runtime& runtime);

    /// Sets a question aside once its prompt goes for any reason but the
    /// install's own: its package waits again in the inbox, and a telling
    /// ends. An unpacking runs on, and its next progress shows a new prompt.
    void set_aside();

    /// Gives the copy the platform made of the package back to it to remove.
    void release_opened_copy();

    /// Ends the install of the package taken: its copy released and the
    /// inbox told.
    void finish_package();
};

} // namespace oa::app
