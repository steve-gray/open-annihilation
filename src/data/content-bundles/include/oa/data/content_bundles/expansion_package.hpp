// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// An expansion package's manifest (oaexp.yaml) and the check that its files
// are the ones the built-in table names. The manifest's id, name, extends
// and needs are the table's. Each file is the table's by name, size and
// SHA-256. Nothing here opens a file or writes a package.
#pragma once

#include "oa/data/content_bundles.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::content_bundles {

/// Who packed the expansion, as oaexp.yaml names them.
struct ExpansionAuthor {
    /// The name a player sees. 1 to 64 bytes.
    std::string name{};
    /// An email address, when the manifest names one. Empty otherwise.
    std::string email{};
};

/// The packaging block of oaexp.yaml. `revision` is always set when the
/// manifest was read. `date` and `packager` are empty when it omits them.
struct ExpansionPackaging {
    /// The packaging revision, 1 to 65535.
    int32_t revision{};
    /// The day it was packed, `YYYY-MM-DD`. Empty when the manifest omits it.
    std::string date{};
    /// Who packed it, 1 to 64 bytes. Empty when the manifest omits it.
    std::string packager{};
};

/// An expansion manifest that matches the built-in table.
struct ExpansionManifest {
    /// The expansion's id, a built-in expansion.
    std::string id{};
    /// The English name, the table's.
    std::string name{};
    /// The version, 1 to 32 bytes, as written.
    std::string version{};
    /// The base games it extends, in the table's order.
    std::vector<std::string> extends{};
    /// The expansions it needs, in the table's order. Empty when it needs none.
    std::vector<std::string> needs{};
    ExpansionAuthor author{};
    ExpansionPackaging packaging{};
    /// The built-in bundle `id` names. Stays valid for the process.
    const Bundle* bundle{};
};

/// Reads oaexp.yaml.
///
/// `oaexp` is 1. `id` is a built-in expansion. `name` is that expansion's
/// English name. `extends` and `needs` are the table's lists, in order.
/// `author.name` is required. `packaging.revision` is required; `date` and
/// `packager` are optional. A key this reader does not know is refused.
/// The files are not checked here.
///
/// @param text the manifest's bytes
/// @param[out] manifest the manifest; unchanged when the text is refused
/// @param[out] error why it was refused; may be null
/// @return true when the manifest matches the table
[[nodiscard]] bool read_expansion_manifest(
    std::span<const uint8_t> text, ExpansionManifest& manifest, std::string* error
);

/// One file offered with an expansion, already measured.
struct MeasuredExpansionFile {
    /// The copy name, `/` between parts, as the table writes it.
    std::string_view name{};
    /// The size in bytes.
    uint64_t size{};
    /// The SHA-256 of those bytes.
    base::sha256::Digest digest{};
};

/// Why a set of files is not an expansion's files.
enum class ExpansionFault : uint8_t {
    ok,        ///< every table file is here once, and nothing else
    stray,     ///< a file the expansion does not have
    missing,   ///< a table file that is not here
    size,      ///< a file whose size is not the table's
    hash,      ///< a file whose SHA-256 is not one the table knows
    duplicate, ///< the same copy name twice
    unsafe,    ///< a name that is not a copy name
};

/// What is wrong with a set of files, and the file it names.
struct ExpansionFaultReport {
    ExpansionFault fault{ExpansionFault::ok};
    /// The copy name the fault names. Empty when `fault` is ok.
    std::string name{};
};

/// Reports whether `files` are exactly `bundle`'s files.
///
/// Each copy name appears once. The size is the table's size. The digest is
/// one `known_hash` accepts. Nothing else is present. Names are compared
/// exactly, so a different case is a different file.
///
/// @param bundle the expansion, built in or a test's own table
/// @param files the files offered, manifest excluded
/// @return ok, or the first fault: for each offered file, unsafe, then
///         duplicate, then stray; then for each table file, missing, size, hash
[[nodiscard]] ExpansionFaultReport
match_expansion_files(const Bundle& bundle, std::span<const MeasuredExpansionFile> files);

/// Reports whether a bundle carries the manifest's identity.
///
/// The id, the English name, `extends` and `needs` agree, in order. The
/// file list is not compared: a test table may name the same expansion and
/// different bytes. The package the game installs does not use that.
///
/// @param manifest a manifest read by read_expansion_manifest
/// @param bundle the table the files will be checked against
/// @return true when the identity agrees
[[nodiscard]] bool
expansion_identity_matches(const ExpansionManifest& manifest, const Bundle& bundle) noexcept;

} // namespace oa::data::content_bundles
