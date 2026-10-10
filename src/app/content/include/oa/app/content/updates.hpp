// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Which installed packages a catalogue has a newer release of, and whether
// that release changes the game's rules or needs an Open Annihilation this
// one is not. Nothing here downloads or installs. The Library and Settings
// copy these results into their own rows.
#pragma once

#include "oa/app/content/service.hpp"
#include "oa/app/package_install.hpp"
#include "oa/base/sha256.hpp"
#include "oa/formats/oamod/package_keys.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app::content {

/// One package installed in the player's folder.
///
/// `kind` points at an entry of package_kinds() and stays valid for the life
/// of the program. `rules` is set for a mod whose profile resolves with no
/// player settings and no Developer Mode overrides, and empty when it does
/// not. A language pack or a map pack has no rules hash.
struct InstalledPackage {
    const package_install::PackageKind* kind = nullptr; ///< an entry of the kind table
    std::string key{};                                  ///< the manifest's id
    std::filesystem::path folder{};                     ///< the installed folder
    std::string name{};                                 ///< the name a player sees
    std::string version{};                              ///< the manifest's version, as written
    int64_t revision{}; ///< packaging.revision; 0 when the manifest names none
    /// Where the folder was installed from. Empty when the folder has no record.
    std::optional<package_install::Origin> origin{};
    /// The mod's rules hash, resolved with default options. Empty for a kind
    /// without rules, and when the profile does not resolve.
    std::optional<base::sha256::Digest> rules{};
    bool has_backup = false;      ///< the folder keeps an earlier copy
    std::string backup_version{}; ///< that copy's version; empty when it has none
    int64_t backup_revision{};    ///< that copy's revision; 0 when unknown
};

/// How an installed package relates to the catalogues in effect.
enum class Provenance : uint8_t {
    catalogue, ///< the origin record names a registry and a release
    matched,   ///< a file install whose SHA-256 equals an enabled entry
    own,       ///< the player's own; never offered an update
};

/// The catalogue release an installed package belongs to, or own.
struct Match {
    Provenance provenance{Provenance::own}; ///< how it was decided
    std::string registry{};                 ///< the registry id; empty when own
    std::string key{};                      ///< the package key; empty when own
    int64_t release{};                      ///< the recorded or matched release; 0 when own
};

/// Whether an update changes the rules saved games are tied to.
enum class RulesChange : uint8_t {
    same,    ///< both rules hashes exist and are equal, or the kind has no rules
    changes, ///< both rules hashes exist and differ
    unknown, ///< a mod is missing one of the two hashes
};

/// Why this Open Annihilation cannot install an update.
///
/// At most one reason is reported. An engine requirement that this build
/// does not meet comes before a base, and a base before a hack.
enum class Blocked : uint8_t {
    none,   ///< this build can install it
    engine, ///< requires.engine parses and this build does not meet it
    hacks,  ///< a hack id is missing from the registry or not implemented
    base,   ///< requires.base is set and is not ta-3.1c
};

/// One catalogue release newer than the release an installed package records.
struct Update {
    std::size_t installed{};  ///< an index into the list passed to find_updates
    std::string registry{};   ///< the registry the package came from
    std::string key{};        ///< the package key in that registry
    int64_t from_release{};   ///< the release the install records or was matched to
    int64_t to_release{};     ///< the newer catalogue release
    std::string to_version{}; ///< the newer release's version, as the catalogue writes it
    int64_t to_revision{};    ///< the newer release's revision; 0 when it names none
    uint64_t size{};          ///< the newer package's size, in bytes
    std::optional<base::sha256::Digest> from_rules{}; ///< the installed mod's rules hash
    std::optional<base::sha256::Digest> to_rules{};   ///< the catalogue entry's rules hash
    RulesChange rules{RulesChange::unknown};          ///< whether the rules hash changes
    Blocked blocked{Blocked::none};                   ///< why this build cannot install it
    /// The engine requirement as a player reads it, the base the package
    /// names, or up to three missing hack ids and "N more". Empty when
    /// nothing blocks the update.
    std::string blocked_detail{};
};

/// What this Open Annihilation is, for deciding whether an update can be installed.
struct UpdateRules {
    formats::oamod::EngineVersion engine{}; ///< this build's release
    /// True when this build carries out the hack. Empty counts every hack as missing.
    std::function<bool(std::string_view)> hack_implemented{};
};

/// The rules of the build this file was compiled into.
///
/// The release is OA_ENGINE_VERSION, or 0.0.0 when that text is not a
/// release. A hack counts as carried out when the registry has an entry for
/// it and that entry is implemented. There is no other check.
///
/// @return the rules
[[nodiscard]] UpdateRules this_engine_rules();

/// Lists every installed package of every kind the installer knows.
///
/// Walks `<user folder>/<kind root>` one level, skips a name that starts
/// with '.', and skips anything the kind does not read as a package. Reads
/// each folder's origin record and the copy it keeps. For a mod, resolves
/// its profile with no player settings and remembers the rules hash by the
/// manifest's size and modification time, so a later call does not resolve
/// an unchanged mod again. Changes nothing on disk.
///
/// @param user_folder the player's folder
/// @return the packages, kinds in table order
[[nodiscard]] std::vector<InstalledPackage>
list_installed(const std::filesystem::path& user_folder);

/// Matches an installed package to a catalogue release.
///
/// A catalogue origin matches that record's registry and key at its release,
/// even when the registry is now off or gone. A file origin matches an
/// enabled entry of the same kind whose SHA-256 is the record's, at that
/// entry's release. When two registries list the same bytes, a built-in
/// registry wins, and otherwise the one listed first. Anything else is the
/// player's own. Names, ids and versions are not how this is decided.
///
/// @param installed the package
/// @param snapshot the catalogues in effect
/// @return the match
[[nodiscard]] Match match_installed(const InstalledPackage& installed, const Snapshot& snapshot);

/// Rewrites a matched file install as a catalogue origin.
///
/// The record keeps the file's SHA-256, takes the match's registry, key and
/// release, and dates the install today. The write goes through the
/// installer's origin record, which holds the kind's root lock and replaces
/// the file whole. Nothing is written when the match is not a file matched
/// to a release, when the record has no SHA-256, or while an install holds
/// the lock.
///
/// @param installed the package, as list_installed read it
/// @param match the match for that package
/// @param[out] error why it was not written; may be null
/// @return true when the folder's record was replaced
[[nodiscard]] bool
adopt_match(const InstalledPackage& installed, const Match& match, std::string* error);

/// Finds the updates for installed packages.
///
/// A package matched to a catalogue release, or recorded as one, is an
/// update when that same registry lists the same kind and key at a higher
/// release. A release in another registry is not an update. The rules hash
/// is the same, changed, or unknown; a kind without rules is the same.
/// Results are ordered by kind (mods, then map packs, then language packs)
/// and then by the package's name, ignoring letter case.
///
/// @param installed the packages list_installed returned
/// @param snapshot the catalogues in effect
/// @param rules what this build can install
/// @return the updates, including those this build cannot install
[[nodiscard]] std::vector<Update> find_updates(
    std::span<const InstalledPackage> installed, const Snapshot& snapshot, const UpdateRules& rules
);

/// Counts the updates this build can install.
///
/// @param updates the updates find_updates returned
/// @return how many are not blocked
[[nodiscard]] std::size_t waiting_updates(std::span<const Update> updates);

} // namespace oa::app::content
