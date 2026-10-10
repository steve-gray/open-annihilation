// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The oamap kind: a map pack's manifest, what a folder of Maps holds, where
// a map pack installs, the check that every map fits the base game, and the
// words the player is shown.
#pragma once

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/prompts.hpp"
#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace oa::app::package_install::oamap {

/// The extension of a map pack, matched without case.
inline constexpr std::string_view oamap_extension = ".oamap";

/// The folder of the player's own folder that holds installed map packs.
inline constexpr std::string_view maps_folder_name = "Maps";

/// The most bytes of a map's preview. A larger file is refused.
inline constexpr uint64_t most_preview_bytes = 2 << 20;

/// What a staged map pack's fit check needs from the app.
///
/// `PackageOptions::context` points here for this kind. A null `check_base_fit`,
/// or a null context, skips the check: only a test that gives no hooks does.
struct FitHooks {
    void* context{}; ///< passed to check_base_fit
    /// Checks one map in the staging folder against the base game.
    oa::data::map_fit::Fit (*check_base_fit)(
        void* context,
        const std::filesystem::path& staged_folder,
        const oa::data::map_pack::MapEntry& map,
        std::string_view id
    ){};
};

/// Reads an oamap package's manifest in package use, checks that the package
/// holds the files it lists and nothing else, that every preview is a PNG of
/// at most 1024 pixels on a side, and that requires.engine is met by this
/// build. Fills package.incoming.
///
/// @param manifest the manifest's bytes
/// @param context the options, the source name and a reader for the previews
/// @param[in,out] package receives what the pack installs
/// @param[out] problem why it was refused
/// @return true when the package can be installed
[[nodiscard]] bool read_manifest(
    std::span<const uint8_t> manifest,
    const ManifestContext& context,
    Package& package,
    Problem& problem
);

/// Reads what a folder in Maps holds: its oamap.yaml's id, name, version
/// and packaging.revision, in package use.
///
/// @param folder the folder
/// @return what it holds; other when it is not a folder of a map pack
[[nodiscard]] InstalledPackage read_installed(const std::filesystem::path& folder);

/// Reads what a map pack's .backup holds.
///
/// @param folder the pack's folder
/// @return nothing when it has no .backup; else what it holds, of kind
///         package only when it is a folder, not a link, whose oamap.yaml
///         gives the folder's own id
[[nodiscard]] std::optional<InstalledPackage> read_backup(const std::filesystem::path& folder);

/// Plans an install from what Maps/<id> holds and from both origin records.
///
/// A missing folder is installed with no question when the package came from
/// a catalogue, and asks first when the player opened the file. Nothing is
/// installed alongside. A catalogue package whose target's record names the
/// same registry replaces that folder with no question. The same id from
/// another registry asks. A file the player opened asks as a mod does. A
/// folder without this pack's oamap.yaml is left alone and the install refused.
///
/// @param incoming the pack the package installs, origin included
/// @param hooks what the folder holds
/// @return the plan
[[nodiscard]] InstallPlan plan_install(const Incoming& incoming, const FolderHooks& hooks);

/// Checks each unpacked map against the base game, through the FitHooks
/// `options.context` points to. No hooks skips the check.
///
/// @param staging the unpacked folder
/// @param package the package
/// @param options the kind's options; context is a FitHooks or null
/// @param[out] problem why a map was refused
/// @return true when every map fits, or when no hooks were given
[[nodiscard]] bool check_staged(
    const std::filesystem::path& staging,
    const Package& package,
    const PackageOptions& options,
    Problem& problem
);

/// Returns the prompt shown while a pack is checked, unpacked or put in place
/// (INSTALLING MAP PACK).
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
/// A new pack the player opened asks "Install the map pack {name} {version}
/// ({n} maps)?" with Install and Cancel. An update, a reinstall and a
/// replace name the versions, and a replace across registries names both.
/// Nothing offers Install alongside.
///
/// @param plan the plan, one of the ask kinds
/// @param incoming the pack
/// @param file_name the package's name
/// @param root the Maps folder
/// @param played unused; a map pack is never what the game plays
/// @return the prompt
[[nodiscard]] PackagePrompt question_prompt(
    const InstallPlan& plan,
    const Incoming& incoming,
    std::string_view file_name,
    const std::filesystem::path& root,
    bool played
);

/// Returns the prompt that says a map pack was installed (MAP PACK INSTALLED).
///
/// @param incoming the pack
/// @param folder its folder
/// @param play_now unused; PLAY NOW is not offered
/// @return the prompt: OPEN FOLDER, OK
[[nodiscard]] PackagePrompt
installed_prompt(const Incoming& incoming, const std::filesystem::path& folder, bool play_now);

/// Returns the prompt that says a map pack's folder was updated or installed
/// again (MAP PACK UPDATED).
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

/// Returns the sentence that says why a map pack was not installed.
///
/// @param problem the problem
/// @return the sentence
[[nodiscard]] std::string refusal_text(const Problem& problem);

/// Returns the prompt that says a map pack was not installed (MAP PACK NOT
/// INSTALLED): the reason, and at most three of its manifest's diagnostics.
///
/// @param file_name the package's name
/// @param problem why
/// @param change_failed a change was tried and undone
/// @return the prompt: OK
[[nodiscard]] PackagePrompt
refused_prompt(std::string_view file_name, const Problem& problem, bool change_failed);

} // namespace oa::app::package_install::oamap
