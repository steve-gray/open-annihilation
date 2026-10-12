// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The built-in description of every base game and expansion: which files
// each one is made of, how big they are, and the SHA-256 values a copy is
// known by. Copying, checking, mounting and the Library all read this
// table. It holds names, sizes and hashes only.
#pragma once

#include "oa/base/sha256.hpp"

#include <cstddef>
#include <span>
#include <stdint.h>
#include <string_view>
#include <vector>

namespace oa::data::content_bundles {

/// Whether a bundle is a base game or an expansion.
enum class Kind : uint8_t {
    base_game, ///< the game the player plays; exactly one is chosen
    expansion, ///< adds to a base game, and may need other expansions
};

/// One file of a bundle.
///
/// `copy_name` is the path written under the bundle's folder, with `/`
/// between its parts (`Data/2.zrb`, `music/7.mp3`). `source_names` are the
/// paths an install may use for the same file. Matching ignores ASCII case
/// and treats `\` as `/`.
struct ContentFile {
    /// Path written into the bundle folder.
    std::string_view copy_name{};
    /// Paths a source may use, matched without regard to ASCII case.
    std::span<const std::string_view> source_names{};
    /// Size in bytes.
    uint64_t size{};
    /// SHA-256 values this file is known by. One file may have several.
    std::span<const base::sha256::Digest> sha256{};
};

/// One base game or expansion.
struct Bundle {
    /// Stable id (`totala`, `core-contingency`). Compared exactly.
    std::string_view id{};
    /// Name shown to the player, in English.
    std::string_view english_name{};
    /// Base game or expansion.
    Kind kind{};
    /// Base games this expansion extends. Empty for a base game.
    std::span<const std::string_view> extends{};
    /// Expansions that must be on before this one. Empty when it needs none.
    std::span<const std::string_view> needs{};
    /// Files copied into the bundle's folder.
    std::span<const ContentFile> files{};
};

/// A known installer the game can be read from, without running it.
enum class InstallerKind : uint8_t {
    windows,   ///< the Windows installer of the Commander Pack
    macos,     ///< the macOS installer package
    older_mac, ///< the disk image for older Macs
};

/// One known installer, recognised by its size and SHA-256.
struct Installer {
    /// The file name the installer is published under.
    std::string_view file_name{};
    /// Which of the three installers this is.
    InstallerKind kind{};
    /// Size in bytes.
    uint64_t size{};
    /// The installer's SHA-256.
    base::sha256::Digest sha256{};
};

/// A file found by name and size.
///
/// The pointers address the built-in table and stay valid for the process.
struct FileMatch {
    /// The bundle the file belongs to. Null when nothing matched.
    const Bundle* bundle{};
    /// The file. Null when nothing matched.
    const ContentFile* file{};

    /// Reports whether a file matched.
    ///
    /// @return true when `file` is set
    [[nodiscard]] explicit operator bool() const noexcept { return file != nullptr; }
};

/// Returns every base game and expansion, in the built-in order.
///
/// @return the table; each id appears once
[[nodiscard]] std::span<const Bundle> bundles() noexcept;

/// Finds a bundle by its id.
///
/// @param id the bundle id, compared exactly
/// @return the bundle, or null when the id is not in the table
[[nodiscard]] const Bundle* find_bundle(std::string_view id) noexcept;

/// Returns what `ids` need, including themselves.
///
/// An expansion needs the base games it extends and the expansions it
/// requires, and those needs are followed in turn. The result is in the
/// built-in order. An id that is not a bundle is left out.
///
/// @param ids the bundles being turned on
/// @return those bundles and everything they need
[[nodiscard]] std::vector<std::string_view>
requirement_closure(std::span<const std::string_view> ids);

/// Returns what goes off with `ids`, including themselves.
///
/// A bundle goes off when it extends or requires one of these, directly or
/// through another bundle. The result is in the built-in order. An id that
/// is not a bundle is left out.
///
/// @param ids the bundles being turned off
/// @return those bundles and every bundle that needs one of them
[[nodiscard]] std::vector<std::string_view> removal_closure(std::span<const std::string_view> ids);

/// Returns the set after turning `id` on.
///
/// The result holds `enabled`, everything `id` needs, and every expansion
/// whose only missing need, in `enabled` as it was, is `id`. Bundles stay
/// in the built-in order. An unknown id leaves the known bundles of
/// `enabled` unchanged.
///
/// @param enabled the bundles that are on
/// @param id the bundle being turned on
/// @return the bundles that are on afterwards
[[nodiscard]] std::vector<std::string_view>
after_turning_on(std::span<const std::string_view> enabled, std::string_view id);

/// Returns the set after turning `id` off.
///
/// `id` and every bundle that needs it go off. When `id` is not among
/// `enabled`, the known bundles of `enabled` are returned unchanged.
///
/// @param enabled the bundles that are on
/// @param id the bundle being turned off
/// @return the bundles that are on afterwards
[[nodiscard]] std::vector<std::string_view>
after_turning_off(std::span<const std::string_view> enabled, std::string_view id);

/// Finds a file by a source path and its size.
///
/// The path is matched without regard to ASCII case, and `\` is read as
/// `/`, against the file's copy name and each source name. A path with no
/// directory is also matched against the file's own last name. The size
/// has to be the file's size.
///
/// @param source_name the path or file name in the source
/// @param size the file's size in bytes
/// @return the bundle and the file, or a null match
[[nodiscard]] FileMatch match_file(std::string_view source_name, uint64_t size) noexcept;

/// Reports whether a digest is one this file is known by.
///
/// @param file a file from the table
/// @param digest the SHA-256 of the bytes
/// @return true when `digest` is one of the file's known hashes
[[nodiscard]] bool known_hash(const ContentFile& file, const base::sha256::Digest& digest) noexcept;

/// Returns the known installers.
///
/// @return the three Commander Pack installers
[[nodiscard]] std::span<const Installer> installers() noexcept;

/// Finds a known installer by its size.
///
/// @param size the file's size in bytes
/// @return the installer of that size, or null
[[nodiscard]] const Installer* find_installer(uint64_t size) noexcept;

/// Reports whether a digest is the installer's known SHA-256.
///
/// @param installer a known installer
/// @param digest the SHA-256 of the file
/// @return true when the digest is the installer's
[[nodiscard]] bool
known_installer_hash(const Installer& installer, const base::sha256::Digest& digest) noexcept;

/// Returns what is wrong with the built-in table.
///
/// Every dependency names a real bundle, an expansion extends a base game,
/// a requirement is an expansion, and following either reaches no cycle.
/// File names and sizes identify one file. The demo is extended by nothing.
///
/// @return empty when the table holds; otherwise a description of the first fault
[[nodiscard]] std::string_view table_fault() noexcept;

} // namespace oa::data::content_bundles
