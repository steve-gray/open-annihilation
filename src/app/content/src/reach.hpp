// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How a failed fetch is named in a registry check or a refresh log.
#pragma once

#include "oa/netgame/http/client.hpp"

#include <string>

namespace oa::app::content {

/// The sentence a registry window or a refresh log uses for one failed fetch.
///
/// An https address this system had to read as http, and that then served
/// nothing, is named on its own. A rejected certificate keeps the client's
/// own sentence. Every other failure stays "cannot be reached".
///
/// @param response the fetch
/// @return the sentence
[[nodiscard]] inline std::string reach_detail(const oa::netgame::http::Response& response) {
    if (response.failure == oa::netgame::http::Failure::https_needs_tls)
        return oa::netgame::http::failure_text(response.failure);
    if (response.failure == oa::netgame::http::Failure::certificate && !response.detail.empty())
        return response.detail;
    return std::string("cannot be reached: ") + oa::netgame::http::failure_text(response.failure);
}

} // namespace oa::app::content
