// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// No system TLS in this build. An https address is read as http by the client.

#include "tls.hpp"

namespace oa::netgame::http {
namespace {

TlsIoResult unavailable() {
    TlsIoResult result;
    result.kind = TlsIoResult::Kind::failed;
    result.failure = Failure::https_unsupported;
    result.detail = "This system cannot use HTTPS";
    return result;
}

} // namespace

HttpsTransport probe_https_transport() noexcept {
    return HttpsTransport::none;
}

TlsSession* tls_open(
    intptr_t /*fd*/,
    std::string_view /*host*/,
    bool /*ip_literal*/,
    Failure& failure,
    std::string& detail
) {
    failure = Failure::https_unsupported;
    detail = "This system cannot use HTTPS";
    return nullptr;
}

void tls_close(TlsSession* /*session*/) noexcept {
}

TlsIoResult tls_handshake(TlsSession* /*session*/) {
    return unavailable();
}

TlsIoResult tls_read(TlsSession* /*session*/, uint8_t* /*bytes*/, std::size_t /*size*/) {
    return unavailable();
}

TlsIoResult tls_write(TlsSession* /*session*/, const uint8_t* /*bytes*/, std::size_t /*size*/) {
    return unavailable();
}

HttpsExchange fetch_https_hop(
    const Request& /*request*/,
    const oa::formats::url::Url& /*url*/,
    Method /*method*/,
    const std::vector<uint8_t>& /*body*/,
    std::string_view /*user_agent*/,
    BodySink& /*sink*/,
    const FetchClock& /*clock*/
) {
    HttpsExchange exchange;
    exchange.failure = Failure::https_unsupported;
    exchange.detail = "This system cannot use HTTPS";
    return exchange;
}

} // namespace oa::netgame::http
