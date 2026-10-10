// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the player is shown and asked while a package installs: each prompt's
// title, text and buttons, and what each button answers. A kind supplies the
// words (KindPrompts). Every text is an English template looked up in the
// interface catalogue (oa/data/languages/interface_text.hpp) and then filled
// with its values, which are never looked up: names, versions, file names,
// paths and sizes. The captions shared with the settings' own dialogs are
// the same English words, so one catalogue entry serves both.
#pragma once

#include "oa/app/package_install.hpp"
#include "oa/ui/engine_settings/prompt.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::package_install {

/// What a prompt's button answers.
enum class Answer : uint8_t {
    cancel,      ///< CANCEL: nothing is done
    ok,          ///< OK: the prompt is done with
    replace,     ///< REPLACE: the folder's version is replaced and kept as its .backup
    alongside,   ///< INSTALL ALONGSIDE: the package goes in a folder of its own
    reinstall,   ///< REINSTALL: the folder's files are installed again
    open_folder, ///< OPEN FOLDER: the folder is shown; the prompt stays
    play_now,    ///< PLAY NOW: the game switches to the mod
};

/// A prompt and what its buttons answer.
struct PackagePrompt {
    oa::ui::engine_settings::Prompt prompt{};
    std::vector<Answer> answers{}; ///< one for each button, left to right
};

/// What an installing prompt is showing.
enum class InstallingPhase : uint8_t {
    checking,  ///< the package file's SHA-256
    unpacking, ///< the package's files
    placing,   ///< the files being put in place
};

/// The words one kind shows. Null asks nothing of that kind.
struct KindPrompts {
    PackagePrompt (*installing)(
        const Incoming& incoming,
        uint64_t done_bytes,
        uint64_t total_bytes,
        InstallingPhase phase,
        std::string_view file_name
    ){};
    PackagePrompt (*question)(
        const InstallPlan& plan,
        const Incoming& incoming,
        std::string_view file_name,
        const std::filesystem::path& root,
        bool played
    ){};
    PackagePrompt (*installed)(
        const Incoming& incoming, const std::filesystem::path& folder, bool play_now
    ){};
    PackagePrompt (*updated)(
        Change change,
        const Incoming& now,
        const InstalledPackage& before,
        const std::filesystem::path& folder,
        const ChangeResult& result,
        bool play_now
    ){};
    std::string (*refusal_text)(const Problem& problem){};
    PackagePrompt (*refused)(
        std::string_view file_name, const Problem& problem, bool change_failed
    ){};
};

/// Returns the prompt shown while a package is checked, unpacked or put in
/// place, from the kind's words.
///
/// @param kind the kind
/// @param incoming what it installs
/// @param done_bytes the bytes hashed or unpacked so far
/// @param total_bytes the package file's size, or the bytes it unpacks to
/// @param phase what the prompt says
/// @param file_name the package's name, which the checking phase names
/// @return the prompt; empty when the kind has none
[[nodiscard]] PackagePrompt installing_prompt(
    const PackageKind& kind,
    const Incoming& incoming,
    uint64_t done_bytes,
    uint64_t total_bytes,
    InstallingPhase phase,
    std::string_view file_name
);

/// Returns the question a plan asks, from the kind's words.
///
/// @param kind the kind
/// @param plan the plan, one of the ask kinds
/// @param incoming what it installs
/// @param file_name the package's name
/// @param root the kind's root folder
/// @param played the target is what the game plays now
/// @return the prompt; empty when the kind has none
[[nodiscard]] PackagePrompt question_prompt(
    const PackageKind& kind,
    const InstallPlan& plan,
    const Incoming& incoming,
    std::string_view file_name,
    const std::filesystem::path& root,
    bool played
);

/// Returns the prompt that says a package was installed in a folder of its own.
///
/// @param kind the kind
/// @param incoming what it installs
/// @param folder its folder
/// @param play_now PLAY NOW is offered
/// @return the prompt; empty when the kind has none
[[nodiscard]] PackagePrompt installed_prompt(
    const PackageKind& kind,
    const Incoming& incoming,
    const std::filesystem::path& folder,
    bool play_now
);

/// Returns the prompt that says a package's folder was updated, installed
/// again or rolled back.
///
/// @param kind the kind
/// @param change what was done
/// @param now what the folder holds now
/// @param before what it held before
/// @param folder the folder
/// @param result what the change did
/// @param play_now PLAY NOW is offered
/// @return the prompt; empty when the kind has none
[[nodiscard]] PackagePrompt updated_prompt(
    const PackageKind& kind,
    Change change,
    const Incoming& now,
    const InstalledPackage& before,
    const std::filesystem::path& folder,
    const ChangeResult& result,
    bool play_now
);

/// Returns the sentence that says why a package was not installed, from the kind's words.
///
/// @param kind the kind
/// @param problem the problem
/// @return the sentence; empty when the kind has none
[[nodiscard]] std::string refusal_text(const PackageKind& kind, const Problem& problem);

/// Returns the prompt that says a package was not installed, from the kind's words.
///
/// @param kind the kind
/// @param file_name the package's name
/// @param problem why
/// @param change_failed a change was tried and undone
/// @return the prompt; empty when the kind has none
[[nodiscard]] PackagePrompt refused_prompt(
    const PackageKind& kind, std::string_view file_name, const Problem& problem, bool change_failed
);

/// Returns the prompt that says a file is not a kind of package the game installs.
///
/// @param file_name the file's name
/// @return the prompt: NOT INSTALLED, one OK button
[[nodiscard]] PackagePrompt unknown_kind_prompt(std::string_view file_name);

} // namespace oa::app::package_install
