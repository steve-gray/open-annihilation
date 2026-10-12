// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HTTPS through the system's own TLS. Nothing here is linked as a TLS
// library: Apple uses the system's URL loading, Windows loads WinHTTP at
// run time, and Linux loads libssl at run time. A missing library is "no
// TLS", not a failure to start.
#pragma once

#include "oa/formats/url.hpp"
#include "oa/netgame/http/client.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace oa::netgame::http {

/// How this process fetches HTTPS.
enum class HttpsTransport : uint8_t {
    none,     ///< no TLS in this build, or the system library is missing
    platform, ///< one hop is the system's HTTPS stack (Apple, Windows)
    socket,   ///< TLS wrapped around the socket client (Linux)
};

/// What the platform file found. Called once.
///
/// @return the transport this process can use
[[nodiscard]] HttpsTransport probe_https_transport() noexcept;

/// The transport chosen at the first call, then remembered.
///
/// @return none, platform or socket
[[nodiscard]] HttpsTransport https_transport() noexcept;

/// True when a fetch can open HTTPS with the system's TLS.
///
/// @return true when https_transport is not none
[[nodiscard]] bool tls_available() noexcept;

/// One TLS session on a socket the caller owns. SSL_free does not close it.
struct TlsSession;

/// What one non-blocking TLS step did.
struct TlsIoResult {
    enum class Kind : uint8_t { ready, want_read, want_write, closed, failed };

    Kind kind = Kind::failed;
    std::ptrdiff_t count = 0;                   ///< bytes, when kind is ready
    Failure failure = Failure::connection_lost; ///< set when kind is failed
    std::string detail;                         ///< why, when kind is failed
};

/// Starts a client handshake on an open TCP socket. The socket stays the caller's.
///
/// @param fd the connected socket
/// @param host the name to check, and to send as the server name when it is not an address
/// @param ip_literal true when host is four decimal numbers
/// @param[out] failure why it could not start
/// @param[out] detail the English reason
/// @return the session, or null
[[nodiscard]] TlsSession* tls_open(
    intptr_t fd, std::string_view host, bool ip_literal, Failure& failure, std::string& detail
);

/// Ends a session. The socket is left open.
///
/// @param session the session; null is ignored
void tls_close(TlsSession* session) noexcept;

/// Drives the handshake one step. A certificate that fails verification is
/// Failure::certificate and is never retried as HTTP.
///
/// @param session the session
/// @return ready when the handshake finished and the certificate was accepted
[[nodiscard]] TlsIoResult tls_handshake(TlsSession* session);

/// Reads what the session has. A short read is ready with a count.
///
/// @param session the session
/// @param bytes the buffer
/// @param size how many bytes it holds
/// @return ready, want_read, want_write, closed or failed
[[nodiscard]] TlsIoResult tls_read(TlsSession* session, uint8_t* bytes, std::size_t size);

/// Writes what the session takes now.
///
/// @param session the session
/// @param bytes the bytes
/// @param size how many
/// @return ready with a count, want_read, want_write or failed
[[nodiscard]] TlsIoResult tls_write(TlsSession* session, const uint8_t* bytes, std::size_t size);

/// The clocks one platform hop shares with the rest of the fetch.
struct FetchClock {
    std::chrono::steady_clock::time_point started{}; ///< the whole fetch
    std::chrono::steady_clock::time_point hop{};     ///< this address
    Timeouts timeouts{};
    const std::atomic<bool>* cancel = nullptr;
};

/// One HTTPS response the system's stack read. Redirects are not followed.
/// The sink is not called for a redirect. A certificate error is certificate,
/// never a request to try HTTP.
struct HttpsExchange {
    Failure failure = Failure::none;
    std::string detail;
    int status = 0;
    std::vector<Header> headers;
    std::optional<uint64_t> content_length;
    std::optional<ContentRange> content_range;
    uint64_t body_bytes = 0;
    bool delivered = false; ///< sink.begin was called
};

/// Fetches one https address with the system's stack and does not follow a redirect.
///
/// Used where https_transport is platform. Elsewhere it reports that this
/// system cannot use HTTPS and does not connect.
///
/// @param request the caller's limits, headers and clocks
/// @param url the https address of this hop
/// @param method the method on this hop
/// @param body the POST body on this hop
/// @param user_agent the User-Agent value
/// @param sink the body sink; not called for a redirect
/// @param clock the fetch's clocks
/// @return the hop
[[nodiscard]] HttpsExchange fetch_https_hop(
    const Request& request,
    const oa::formats::url::Url& url,
    Method method,
    const std::vector<uint8_t>& body,
    std::string_view user_agent,
    BodySink& sink,
    const FetchClock& clock
);

} // namespace oa::netgame::http
