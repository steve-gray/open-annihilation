// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Reading and writing the files oa-tool's archive commands use. Each one
// throws Failure, whose message is the one the command prints.

#pragma once

#include "command.hpp"

#include "oa/formats/hpi.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace oa::tool {

/// Reads a whole file into memory.
///
/// A file larger than 256 MiB is refused.
///
/// @param path the file to read
/// @return the file's bytes
/// @throws Failure when the file cannot be read or is larger than 256 MiB
[[nodiscard]] std::vector<uint8_t> read_file(const std::filesystem::path& path);

/// Writes bytes to a file, replacing anything already there.
///
/// @param path the file to write
/// @param bytes the bytes to write
/// @throws Failure when the file cannot be written
void write_file(const std::filesystem::path& path, std::span<const uint8_t> bytes);

/// Writes an image as a PNG when the path ends in `.png`, otherwise as a PPM,
/// then prints the image's size and the path.
///
/// @param path where the image is written
/// @param image the decoded image
/// @param[in,out] output receives the size line
/// @throws Failure when the image cannot be encoded or written
void write_image(const std::filesystem::path& path, const oa::Image& image, Output& output);

} // namespace oa::tool
