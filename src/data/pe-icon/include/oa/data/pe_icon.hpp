// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The icon a Windows program file carries, as RGBA, and the FileDescription
// in its version resource. Where those resources lie is oa::formats::pe.
// Nothing in the file is run.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::pe_icon {

/// The most bytes of a program file the reader accepts. A longer span is
/// refused without being walked.
inline constexpr uint64_t most_file_bytes = 16U * 1024U * 1024U;

/// One icon, 8-bit RGBA, rows from the top.
struct Icon {
    uint32_t width{};            ///< columns
    uint32_t height{};           ///< rows
    std::vector<uint8_t> rgba{}; ///< width * height * 4 bytes
};

/// Reads the first icon group's largest image from a program file.
///
/// The largest image is the one with the greatest width times height, and
/// for a tie the one with the most bits a pixel. Its AND mask is the
/// transparency: a set bit is clear and a clear bit is opaque. A file with
/// no icon, or one the reader does not understand, yields nothing. A span
/// longer than most_file_bytes yields nothing without being read. The
/// resource directory is read by oa::formats::pe.
///
/// @param image the file's bytes
/// @return the icon, or nothing when it cannot be read
[[nodiscard]] std::optional<Icon> read_program_icon(std::span<const uint8_t> image);

/// How a program file's version text was read.
enum class VersionText : uint8_t {
    absent,    ///< the file has no version resource
    malformed, ///< the version resource's string tables cannot be read
    present,   ///< the string tables were read
};

/// The FileDescription of a program file's version resource.
struct FileDescription {
    VersionText text{VersionText::absent}; ///< how the read ended
    /// The text, when text is present and the string tables name one.
    /// Empty when they were read and name none.
    std::string value{};
};

/// Reads FileDescription from a program file's version resource.
///
/// The resource is the first id of type 16 and its first language. Every
/// string table is read. The first FileDescription is kept. A span longer
/// than most_file_bytes is malformed and is not walked.
///
/// @param image the file's bytes
/// @return the description, or absent or malformed
[[nodiscard]] FileDescription file_description(std::span<const uint8_t> image);

/// Returns the index of the only file whose FileDescription is exactly `description`.
///
/// A file with no version resource, a malformed one, or a different
/// description does not count. Nothing when no file matches or more than
/// one does.
///
/// @param files the files' bytes
/// @param description the description, compared exactly
/// @return that file's index, or nothing
[[nodiscard]] std::optional<std::size_t>
only_described(std::span<const std::span<const uint8_t>> files, std::string_view description);

} // namespace oa::data::pe_icon
