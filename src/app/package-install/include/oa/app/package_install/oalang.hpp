// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The oalang kind: a language pack's manifest, what a folder of Languages
// holds, where a language pack installs, the check that each font opens, and
// the words the player is shown.
#pragma once

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/prompts.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace oa::app::package_install::oalang {

/// The extension of a language pack, matched without case.
inline constexpr std::string_view oalang_extension = ".oalang";

/// The folder of the player's own folder that holds installed language packs.
inline constexpr std::string_view languages_folder_name = "Languages";

/// Reads an oalang package's manifest, checks that this build meets
/// requires.engine, that each listed font is an entry of the package, that
/// the warm-up text is UTF-8 within its limit, and that each table and
/// interface.tdf that is present parses. Fills package.incoming.
///
/// @param manifest the manifest's bytes
/// @param context the options, the source name and a reader for the pack's files
/// @param[in,out] package receives what the pack installs
/// @param[out] problem why it was refused
/// @return true when the package can be installed
[[nodiscard]] bool read_manifest(
    std::span<const uint8_t> manifest,
    const ManifestContext& context,
    Package& package,
    Problem& problem
);

/// Reads what a folder in Languages holds: its language.yaml's tag, name,
/// version and packaging.revision.
///
/// @param folder the folder
/// @return what it holds; other when it is not a folder of a language pack
[[nodiscard]] InstalledPackage read_installed(const std::filesystem::path& folder);

/// Reads what a language pack's .backup holds.
///
/// @param folder the pack's folder
/// @return nothing when it has no .backup; else what it holds, of kind
///         package only when it is a folder, not a link, whose language.yaml
///         gives the folder's own tag
[[nodiscard]] std::optional<InstalledPackage> read_backup(const std::filesystem::path& folder);

/// Plans an install from what Languages/<tag> holds.
///
/// Nothing is installed alongside. A missing folder asks before it is made
/// when the player opened the file, and is installed with no question when
/// the package came from a catalogue. The same version and the same
/// non-zero revision asks Reinstall, or reinstalls at once for a catalogue
/// package. Any other version or revision, a revision missing on either
/// side, or a folder that does not hold this pack's language.yaml, asks
/// Replace, or replaces at once for a catalogue package. The replaced
/// folder is kept as .backup.
///
/// @param incoming the pack the package installs, origin included
/// @param hooks what the folder holds
/// @return the plan
[[nodiscard]] InstallPlan plan_install(const Incoming& incoming, const FolderHooks& hooks);

/// Opens each font the staged language.yaml lists. A font that does not
/// open refuses the package; the staging is discarded and the installed
/// pack is kept.
///
/// @param staging the unpacked folder
/// @param package the package
/// @param options the kind's options; this kind reads none of them
/// @param[out] problem why a font was refused
/// @return true when every listed font opens
[[nodiscard]] bool check_staged(
    const std::filesystem::path& staging,
    const Package& package,
    const PackageOptions& options,
    Problem& problem
);

/// Returns the prompt shown while a pack is checked, unpacked or put in place
/// (INSTALLING LANGUAGE).
///
/// @param incoming the pack
/// @param done_bytes the bytes hashed or unpacked so far
/// @param total_bytes the package file's size, or the bytes it unpacks to
/// @param phase what the prompt says
/// @param file_name the package's name, which the checking phase names
/// @return the prompt: CANCEL
[[nodiscard]] PackagePrompt installing_prompt(
    const Incoming& incoming,
    uint64_t done_bytes,
    uint64_t total_bytes,
    InstallingPhase phase,
    std::string_view file_name
);

/// Returns the question a plan asks.
///
/// A new pack the player opened asks "Install the language {endonym}
/// ({English name})?" with Install and Cancel. An update, a reinstall and
/// a replace name the versions. Nothing offers Install alongside.
///
/// @param plan the plan, one of the ask kinds
/// @param incoming the pack
/// @param file_name the package's name
/// @param root the Languages folder
/// @param played unused; a language pack is never what the game plays
/// @return the prompt
[[nodiscard]] PackagePrompt question_prompt(
    const InstallPlan& plan,
    const Incoming& incoming,
    std::string_view file_name,
    const std::filesystem::path& root,
    bool played
);

/// Returns the prompt that says a language pack was installed (LANGUAGE INSTALLED).
///
/// @param incoming the pack
/// @param folder its folder
/// @param play_now unused; PLAY NOW is not offered
/// @return the prompt: OPEN FOLDER, OK
[[nodiscard]] PackagePrompt
installed_prompt(const Incoming& incoming, const std::filesystem::path& folder, bool play_now);

/// Returns the prompt that says a language pack's folder was updated, replaced
/// or installed again (LANGUAGE UPDATED).
///
/// @param change what was done
/// @param now what the folder holds now
/// @param before what it held before
/// @param folder the folder
/// @param result what the change did
/// @param play_now unused; PLAY NOW is not offered
/// @return the prompt: OPEN FOLDER, OK
[[nodiscard]] PackagePrompt updated_prompt(
    Change change,
    const Incoming& now,
    const InstalledPackage& before,
    const std::filesystem::path& folder,
    const ChangeResult& result,
    bool play_now
);

/// Returns the sentence that says why a language pack was not installed.
/// A refusal every kind shares uses the oamod kind's sentence.
///
/// @param problem the problem
/// @return the sentence
[[nodiscard]] std::string refusal_text(const Problem& problem);

/// Returns the prompt that says a language pack was not installed (LANGUAGE
/// NOT INSTALLED): the reason, and at most three of its manifest's diagnostics.
///
/// @param file_name the package's name
/// @param problem why
/// @param change_failed a change was tried and undone
/// @return the prompt: OK
[[nodiscard]] PackagePrompt
refused_prompt(std::string_view file_name, const Problem& problem, bool change_failed);

/// Returns the prompts the kind shows.
///
/// @return the prompts
[[nodiscard]] const KindPrompts& prompts() noexcept;

} // namespace oa::app::package_install::oalang
