// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Random bytes from the system's generator.
#pragma once

#include <cstdint>
#include <span>

namespace oa::platform::random {

/// Fills a buffer from the system's generator of random bytes.
///
/// Every byte is filled, or none of the buffer is a result: a short read is
/// a failure. An empty buffer is already full and does not ask the system.
/// The bytes come from that generator alone.
///
/// @param[out] bytes the buffer to fill
/// @return false when the generator could not be read
[[nodiscard]] bool fill_random(std::span<uint8_t> bytes) noexcept;

} // namespace oa::platform::random
