// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where an installed package came from. The installer writes .oa-origin.yaml
// into the folder it puts in place: a file the player opened, or one release
// of a registry's catalogue, and the SHA-256 of the package file. The record
// lives in the folder, so a replace, a roll back and crash recovery carry it
// by moving the folder. A folder with no record was installed by an earlier
// version or made by hand; that is not an error.
#pragma once

#include "oa/base/sha256.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace oa::app::package_install {

struct PackageKind;

/// The name of the origin record, at the top of an installed folder.
inline constexpr std::string_view origin_file_name = ".oa-origin.yaml";
/// The most bytes an origin record may hold. A larger file is not a record.
inline constexpr std::size_t origin_most_bytes = 4096;

/// Where an installed package came from.
enum class OriginKind : uint8_t {
    file,      ///< a file the player opened; matched to a release by its SHA-256
    catalogue, ///< one release of a registry's catalogue
};

/// The origin recorded for one installed folder.
struct Origin {
    OriginKind kind{OriginKind::file};
    /// The registry's id. Set only for a catalogue origin.
    std::string registry{};
    /// The package's key in that registry. Set only for a catalogue origin.
    std::string catalogue_id{};
    /// The catalogue release, from 1. Set only for a catalogue origin.
    int64_t release{};
    /// The SHA-256 of the package file as installed. Empty until it is known.
    std::optional<oa::base::sha256::Digest> sha256{};
    /// The UTC date of the install, YYYY-MM-DD. Empty until it is known.
    std::string installed{};
};

/// Writes an origin record.
///
/// The keys are oa-origin, origin, then registry, catalogue-id and release
/// for a catalogue origin, then sha256 and installed. UTF-8, LF line
/// endings, ending in a newline. sha256 and installed must be set.
///
/// @param origin the origin
/// @return the record; empty when sha256 or installed is not set
[[nodiscard]] std::string origin_text(const Origin& origin);

/// Reads an origin record.
///
/// Refuses a text that is not the record's form: an unknown key, a key out
/// of order, a missing key, catalogue keys on a file origin, a registry id
/// the registry rules refuse, an id that contains @, a release outside 1 to
/// 2,147,483,647, a SHA-256 that is not 64 lower-case hex digits, a date
/// that is not a real UTC day, a record version other than 1, a byte that is
/// not UTF-8 or LF, or more than origin_most_bytes bytes.
///
/// @param bytes the record's bytes
/// @param[out] problem why it was refused; may be null
/// @return the origin; nothing when the bytes were refused
[[nodiscard]] std::optional<Origin>
parse_origin(std::span<const uint8_t> bytes, std::string* problem = nullptr);

/// Reads the origin record of an installed folder.
///
/// A folder with no record, a record larger than origin_most_bytes, or a
/// record parse_origin refuses, has none. A missing record is not an error.
///
/// @param folder the installed folder
/// @return the origin; nothing when the folder has no record
[[nodiscard]] std::optional<Origin> read_origin(const std::filesystem::path& folder);

/// Replaces the origin record of an installed folder.
///
/// The folder must hold a package of `kind` and must not be a link. Nothing
/// is written while any hold on the root folder's lock is taken, in this
/// process or by another copy of the game. The record is replaced whole: a
/// failure leaves the previous record as it was.
///
/// @param kind the kind
/// @param root the kind's root folder
/// @param target the folder in the root
/// @param origin the origin; its sha256 and installed must be set
/// @param[out] error why it was not written; may be null
/// @return true when the folder's record was replaced
[[nodiscard]] bool write_origin(
    const PackageKind& kind,
    const std::filesystem::path& root,
    std::string_view target,
    const Origin& origin,
    std::string* error
);

/// Returns the UTC date of a moment, as YYYY-MM-DD.
///
/// @param time the moment
/// @return the date
[[nodiscard]] std::string utc_date_text(std::chrono::system_clock::time_point time);

} // namespace oa::app::package_install
