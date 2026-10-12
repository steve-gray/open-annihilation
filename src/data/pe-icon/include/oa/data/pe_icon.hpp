// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The icon a Windows program file carries in its resources, as RGBA. The
// reader looks only at the headers it needs to find the resource section
// and at the bytes of that section. Nothing in the file is run.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
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
/// no icon, a file the reader does not understand, or a file whose offsets
/// do not stay inside its resource section yields nothing. A span longer
/// than most_file_bytes yields nothing without being read.
///
/// @param image the file's bytes
/// @return the icon, or nothing when it cannot be read
[[nodiscard]] std::optional<Icon> read_program_icon(std::span<const uint8_t> image);

} // namespace oa::data::pe_icon
