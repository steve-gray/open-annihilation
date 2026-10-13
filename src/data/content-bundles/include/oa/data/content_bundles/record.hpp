// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The .oa-content record of one copied bundle. The text is version 1:
// "oa-content 1", then "bundle <id>", then "source <path>", then one group
// per file of "file", "size", "modified" and "sha256". A file the source
// did not hold follows that group with "not-in-source". A record written
// before that line existed has no such line, and the file reads as held.
// The source escapes backslash, newline and carriage return. A line whose
// first word is not one of those fields is kept for a later reader and
// ignored here. The parser takes bytes and returns a record; it does not
// open a file.
#pragma once

#include "oa/base/sha256.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::content_bundles {

/// The file name of a bundle folder's record.
inline constexpr std::string_view content_record_name = ".oa-content";

/// One file recorded in a bundle folder.
struct ContentRecordFile {
    /// The name it is stored under, relative to the bundle folder.
    std::string copy_name;
    /// Size in bytes.
    uint64_t size{};
    /// Modification time, seconds since 1970.
    int64_t modified{};
    /// SHA-256 of the bytes.
    base::sha256::Digest digest{};
    /// The source did not hold this file. A record with no such line reads
    /// this as false: the file was held.
    bool not_in_source{};
};

/// A bundle folder's record, as text or as read from the folder.
///
/// `present`, `valid` and `io_error` describe a file that was opened.
/// `parse_content_record` leaves them false and sets `valid` only.
struct ContentRecord {
    /// True when the file exists.
    bool present{};
    /// True when the bytes are a record this reader understands.
    bool valid{};
    /// True when the file exists and could not be read.
    bool io_error{};
    /// The bundle id the record names.
    std::string bundle_id;
    /// The source the files were copied from, as the player named it.
    std::string source;
    /// The files recorded so far.
    std::vector<ContentRecordFile> files;
};

/// Reads a record from its text.
///
/// A record this reader does not understand is not valid, and its fields
/// are empty. A field this reader does not know is skipped.
///
/// @param text the record's bytes, as text
/// @return the record; `valid` is false when the text is not a record
[[nodiscard]] ContentRecord parse_content_record(std::string_view text);

/// Writes a record as text.
///
/// An empty file list is still a record. A bundle id or a copy name that
/// holds a newline or a carriage return cannot be written.
///
/// @param record the record; `present`, `valid` and `io_error` are not written
/// @return the text, or nothing when a name cannot be stored on one line
[[nodiscard]] std::optional<std::string> format_content_record(const ContentRecord& record);

/// Reads the record in a bundle folder.
///
/// A missing file is an empty record with `present` false. A file that is
/// not a record is present and not valid. A file that cannot be read, or
/// that is larger than a record is allowed to be, sets `io_error`.
///
/// @param folder the bundle folder
/// @param[out] error why it could not be read; may be null; unset otherwise
/// @return the record
[[nodiscard]] ContentRecord
read_bundle_folder_record(const std::filesystem::path& folder, std::string* error);

} // namespace oa::data::content_bundles
