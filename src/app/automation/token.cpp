// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The automation endpoint's token (token.hpp).
#include "token.hpp"

#include "oa/platform/random.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::app::automation {

std::optional<std::string> make_token() {
    std::array<uint8_t, token_bits / 8> bytes{};
    if (!oa::platform::random::fill_random(bytes))
        return std::nullopt;
    static constexpr char hex[] = "0123456789abcdef";
    std::string token;
    token.reserve(bytes.size() * 2);
    for (const uint8_t byte : bytes) {
        token += hex[byte >> 4];
        token += hex[byte & 0xFU];
    }
    return token;
}

bool token_matches(std::string_view given, std::string_view expected) noexcept {
    if (given.size() != expected.size())
        return false;
    uint8_t difference = 0;
    for (size_t at = 0; at < expected.size(); ++at)
        difference |= static_cast<uint8_t>(given[at] ^ expected[at]);
    return difference == 0;
}

} // namespace oa::app::automation
