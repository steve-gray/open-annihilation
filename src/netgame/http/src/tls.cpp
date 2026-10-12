// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Remembers the TLS probe. The platform file does the probe.

#include "tls.hpp"

namespace oa::netgame::http {

HttpsTransport https_transport() noexcept {
    static const HttpsTransport transport = probe_https_transport();
    return transport;
}

bool tls_available() noexcept {
    return https_transport() != HttpsTransport::none;
}

} // namespace oa::netgame::http
