// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Looks up one host without holding the caller past a cancel or a time limit.
#pragma once

#include "oa/netgame/http/client.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace oa::netgame::http {

/// One IPv4 lookup.
struct LookupResult {
    Failure failure = Failure::none; ///< none when `ip` was written
    std::string detail;              ///< why it failed, for a log
    uint8_t ip[4]{};                 ///< the address, when the lookup worked
};

/// Resolves a host to one IPv4 address.
///
/// A numeric address is read on this thread. A name runs on a detached helper
/// that keeps its own copy of the lookup; this thread waits in slices of at
/// most 50 milliseconds and returns on cancel or when `wait_ms` has passed.
/// The helper then finishes on its own.
///
/// @param host the host, as the address names it
/// @param resolver the name resolver; null uses the sockets' IPv4 resolver
/// @param cancel read between waits; null cannot cancel
/// @param wait_ms how long to wait, in milliseconds
/// @return the address, or why it is not available
[[nodiscard]] LookupResult
lookup_ipv4(std::string host, Resolver resolver, const std::atomic<bool>* cancel, uint32_t wait_ms);

} // namespace oa::netgame::http
