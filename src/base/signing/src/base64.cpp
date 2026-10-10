// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "base64.hpp"

namespace oa::base::signing {
namespace {

constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Returns the value of one base64 character, or -1 when it is not in the alphabet.
///
/// @param character a character of the text
/// @return 0 to 63, or -1
[[nodiscard]] int alphabet_value(char character) noexcept {
    if (character >= 'A' && character <= 'Z')
        return character - 'A';
    if (character >= 'a' && character <= 'z')
        return character - 'a' + 26;
    if (character >= '0' && character <= '9')
        return character - '0' + 52;
    if (character == '+')
        return 62;
    if (character == '/')
        return 63;
    return -1;
}

} // namespace

bool base64_encode(std::span<const uint8_t> bytes, std::span<char> text) noexcept {
    const size_t encoded_size = base64_encoded_size(bytes.size());
    if (text.size() != encoded_size)
        return false;
    size_t read = 0;
    size_t written = 0;
    while (read + 3 <= bytes.size()) {
        const uint32_t value = (uint32_t{bytes[read]} << 16) | (uint32_t{bytes[read + 1]} << 8) |
                               uint32_t{bytes[read + 2]};
        text[written] = alphabet[(value >> 18) & 63];
        text[written + 1] = alphabet[(value >> 12) & 63];
        text[written + 2] = alphabet[(value >> 6) & 63];
        text[written + 3] = alphabet[value & 63];
        read += 3;
        written += 4;
    }
    if (read < bytes.size()) {
        uint32_t value = uint32_t{bytes[read]} << 16;
        const bool two_bytes = read + 1 < bytes.size();
        if (two_bytes)
            value |= uint32_t{bytes[read + 1]} << 8;
        text[written] = alphabet[(value >> 18) & 63];
        text[written + 1] = alphabet[(value >> 12) & 63];
        if (two_bytes) {
            text[written + 2] = alphabet[(value >> 6) & 63];
            text[written + 3] = '=';
        } else {
            text[written + 2] = '=';
            text[written + 3] = '=';
        }
    }
    return true;
}

std::optional<size_t> base64_decode(std::string_view text, std::span<uint8_t> bytes) noexcept {
    if (text.size() % 4 != 0)
        return std::nullopt;
    size_t written = 0;
    for (size_t index = 0; index < text.size(); index += 4) {
        int values[4]{};
        int padding = 0;
        for (int place = 0; place < 4; ++place) {
            const char character = text[index + static_cast<size_t>(place)];
            if (character == '=') {
                padding = 4 - place;
                for (int rest = place; rest < 4; ++rest) {
                    if (text[index + static_cast<size_t>(rest)] != '=')
                        return std::nullopt;
                }
                break;
            }
            const int value = alphabet_value(character);
            if (value < 0)
                return std::nullopt;
            values[place] = value;
        }
        if (padding > 2)
            return std::nullopt;
        if (padding > 0 && index + 4 != text.size())
            return std::nullopt;
        if (padding == 1 && (values[2] & 0x03) != 0)
            return std::nullopt;
        if (padding == 2 && (values[1] & 0x0f) != 0)
            return std::nullopt;
        const size_t count = padding == 0 ? 3 : (padding == 1 ? 2 : 1);
        if (written + count > bytes.size())
            return std::nullopt;
        bytes[written] = static_cast<uint8_t>((values[0] << 2) | (values[1] >> 4));
        if (count > 1)
            bytes[written + 1] = static_cast<uint8_t>((values[1] << 4) | (values[2] >> 2));
        if (count > 2)
            bytes[written + 2] = static_cast<uint8_t>((values[2] << 6) | values[3]);
        written += count;
    }
    return written;
}

} // namespace oa::base::signing
