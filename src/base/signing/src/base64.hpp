// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// RFC 4648 base64, used for the text form of a catalogue key and signature.
// The decoder is strict: canonical padding, the standard alphabet, and zero
// bits in the unused positions of a final quantum.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace oa::base::signing {

/// Returns how many base64 characters `size` bytes encode to.
///
/// The count includes the `=` padding. An empty input encodes to nothing.
///
/// @param size the number of bytes
/// @return the character count
[[nodiscard]] constexpr size_t base64_encoded_size(size_t size) noexcept {
    return (size + 2) / 3 * 4;
}

/// Writes bytes as base64.
///
/// @param bytes the bytes to encode
/// @param[out] text storage of exactly base64_encoded_size(bytes.size()) characters
/// @return false when `text` is not that size, leaving it unchanged
[[nodiscard]] bool base64_encode(std::span<const uint8_t> bytes, std::span<char> text) noexcept;

/// Reads strict base64 into bytes.
///
/// Accepts only a length that is a multiple of four, the alphabet
/// `A-Z`, `a-z`, `0-9`, `+` and `/`, and `=` padding in the final quantum
/// only, one or two characters, with the unused bits clear. An empty input
/// decodes to nothing.
///
/// @param text the base64 text
/// @param[out] bytes storage at least as large as the decoded bytes
/// @return how many bytes were written; nullopt when `text` is not strict
///         base64 or does not fit in `bytes`
[[nodiscard]] std::optional<size_t>
base64_decode(std::string_view text, std::span<uint8_t> bytes) noexcept;

} // namespace oa::base::signing
