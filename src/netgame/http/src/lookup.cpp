// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A name lookup on a detached thread, so cancel and the connect limit stay honest.

#include "lookup.hpp"

#include "oa/base/threads.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/socket_host.hpp"

#include <chrono>
#include <cstring>
#include <memory>

namespace oa::netgame::http {
namespace {

/// The helper's copy of one lookup. The caller and the helper share it.
struct LookupState {
    std::string host;
    Resolver resolver = nullptr;
    std::atomic<bool> done{false};
    bool ok = false;
    uint8_t ip[4]{};
};

/// Runs one name lookup and publishes the address.
///
/// @param argument a heap shared_ptr to the lookup; this thread frees it
void lookup_entry(void* argument) {
    const std::unique_ptr<std::shared_ptr<LookupState>> holder(
        static_cast<std::shared_ptr<LookupState>*>(argument)
    );
    const std::shared_ptr<LookupState> state = *holder;
    uint8_t ip[4]{};
    const bool ok = state->resolver != nullptr
                        ? state->resolver(state->host.c_str(), ip)
                        : oa::netgame::sock::resolve_ipv4(state->host.c_str(), ip);
    if (ok)
        std::memcpy(state->ip, ip, sizeof ip);
    state->ok = ok;
    state->done.store(true, std::memory_order_release);
}

/// Reads four decimal numbers into an address.
///
/// @param host a numeric host is_ipv4_literal accepted
/// @param[out] ip the address
/// @return false when the text is not that form
bool read_literal(std::string_view host, uint8_t ip[4]) {
    std::size_t index = 0;
    std::size_t at = 0;
    while (index < 4) {
        if (at >= host.size() || host[at] < '0' || host[at] > '9')
            return false;
        if (host[at] == '0' && at + 1 < host.size() && host[at + 1] >= '0' && host[at + 1] <= '9')
            return false;
        unsigned value = 0;
        while (at < host.size() && host[at] >= '0' && host[at] <= '9') {
            value = value * 10u + static_cast<unsigned>(host[at] - '0');
            if (value > 255u)
                return false;
            ++at;
        }
        ip[index] = static_cast<uint8_t>(value);
        ++index;
        if (index == 4)
            break;
        if (at >= host.size() || host[at] != '.')
            return false;
        ++at;
    }
    return at == host.size();
}

/// How many milliseconds have passed.
///
/// @param start when the wait began
/// @return the elapsed milliseconds
uint32_t elapsed_ms(std::chrono::steady_clock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    );
    if (elapsed.count() <= 0)
        return 0;
    if (elapsed.count() > static_cast<std::chrono::milliseconds::rep>(UINT32_MAX))
        return UINT32_MAX;
    return static_cast<uint32_t>(elapsed.count());
}

} // namespace

LookupResult lookup_ipv4(
    std::string host, Resolver resolver, const std::atomic<bool>* cancel, uint32_t wait_ms
) {
    LookupResult result;
    if (cancel != nullptr && cancel->load()) {
        result.failure = Failure::cancelled;
        result.detail = "the fetch was cancelled";
        return result;
    }
    if (oa::formats::url::is_ipv4_literal(host)) {
        if (!read_literal(host, result.ip)) {
            result.failure = Failure::cannot_resolve;
            result.detail = "the name could not be resolved";
        }
        return result;
    }
    if (wait_ms == 0) {
        result.failure = Failure::timed_out;
        result.detail = "the time limit ran out before the name resolved";
        return result;
    }

    const auto state = std::make_shared<LookupState>();
    state->host = std::move(host);
    state->resolver = resolver;
    auto* holder = new std::shared_ptr<LookupState>(state);
    if (!oa::base::threads::start_detached_thread(lookup_entry, holder)) {
        delete holder;
        result.failure = Failure::cannot_resolve;
        result.detail = "the name could not be resolved";
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    while (true) {
        if (state->done.load(std::memory_order_acquire)) {
            if (!state->ok) {
                result.failure = Failure::cannot_resolve;
                result.detail = "the name could not be resolved";
                return result;
            }
            std::memcpy(result.ip, state->ip, sizeof result.ip);
            return result;
        }
        if (cancel != nullptr && cancel->load()) {
            result.failure = Failure::cancelled;
            result.detail = "the fetch was cancelled";
            return result;
        }
        const uint32_t waited = elapsed_ms(started);
        if (waited >= wait_ms) {
            result.failure = Failure::timed_out;
            result.detail = "the time limit ran out before the name resolved";
            return result;
        }
        const uint32_t left = wait_ms - waited;
        oa::base::threads::sleep_ms(left < 50 ? left : 50);
    }
}

} // namespace oa::netgame::http
