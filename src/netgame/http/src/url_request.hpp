// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Builds the bytes of one HTTP/1.1 request. Nothing here opens a connection.
#pragma once

#include "oa/formats/url.hpp"
#include "oa/netgame/http/client.hpp"

#include <string_view>
#include <vector>

namespace oa::netgame::http {

/// The bytes of a request, or why it will not be sent.
struct PreparedRequest {
    Failure failure = Failure::none; ///< none when `bytes` is a whole request
    std::string detail;              ///< why it will not be sent, for a log
    std::vector<uint8_t> bytes;      ///< the request line, headers and POST body
};

/// Builds one request.
///
/// The request line, Host, User-Agent, Accept, and a POST's Content-Length
/// and Content-Type are written first. Range and Accept-Encoding follow when
/// the request asks for them. The caller's headers are last. A POST body is
/// never chunked and Expect is never sent.
///
/// @param request the method, body and caller headers
/// @param url the address the request line and Host come from
/// @param user_agent the User-Agent value
/// @return the bytes, or bad_request
[[nodiscard]] PreparedRequest prepare_request(
    const Request& request, const oa::formats::url::Url& url, std::string_view user_agent
);

} // namespace oa::netgame::http
