// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The system's generator fills a buffer, and two fills are not the same bytes.
#include "oa/platform/random.hpp"

#include "oa/test/check.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace {

using oa::platform::random::fill_random;

/// A buffer of one length is filled, which an empty one already is.
void check_lengths() {
    const std::span<uint8_t> none;
    OA_CHECK(fill_random(none));

    std::array<uint8_t, 1> one{};
    OA_CHECK(fill_random(one));

    std::array<uint8_t, 16> sixteen{};
    OA_CHECK(fill_random(sixteen));

    std::array<uint8_t, 4096> page{};
    OA_CHECK(fill_random(page));
    bool uniform = true;
    for (const uint8_t byte : page) {
        if (byte != page[0])
            uniform = false;
    }
    OA_CHECK(!uniform);
}

/// Two fills of the same length are not the same bytes.
void check_distinct() {
    std::array<uint8_t, 16> first{};
    std::array<uint8_t, 16> second{};
    OA_CHECK(fill_random(first));
    OA_CHECK(fill_random(second));
    OA_CHECK(first != second);
}

} // namespace

int main() {
    check_lengths();
    check_distinct();
    return oa::test::check_exit_status();
}
