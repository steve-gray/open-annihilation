// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The request line and headers a fetch sends.

#include "url_request.hpp"

#include <string>

namespace oa::netgame::http {
namespace {

/// Compares two texts without regard to case.
///
/// @param left one text
/// @param right the other
/// @return true when they are the same letters
bool same_name(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(left[i]);
        unsigned char b = static_cast<unsigned char>(right[i]);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

/// True when the text holds a byte that would break a header line.
///
/// @param text the text
/// @return true for CR, LF or NUL
bool breaks_line(std::string_view text) {
    return text.find('\r') != std::string_view::npos || text.find('\n') != std::string_view::npos ||
           text.find('\0') != std::string_view::npos;
}

/// True when a header name is one the client writes itself.
///
/// @param name the header name
/// @return true for the names prepare_request owns
bool reserved_header(std::string_view name) {
    static constexpr std::string_view names[] = {
        "host",
        "user-agent",
        "accept",
        "content-length",
        "content-type",
        "range",
        "accept-encoding",
        "expect",
        "transfer-encoding",
    };
    for (const std::string_view reserved : names) {
        if (same_name(name, reserved))
            return true;
    }
    return false;
}

/// Appends text.
///
/// @param[in,out] out the buffer
/// @param text the text
void append_text(std::vector<uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

/// A refusal.
///
/// @param detail why the request will not be sent
/// @return bad_request
PreparedRequest refuse(std::string detail) {
    PreparedRequest prepared;
    prepared.failure = Failure::bad_request;
    prepared.detail = std::move(detail);
    return prepared;
}

} // namespace

PreparedRequest prepare_request(
    const Request& request, const oa::formats::url::Url& url, std::string_view user_agent
) {
    if ((request.method == Method::get || request.method == Method::head) && !request.body.empty())
        return refuse("a body is only sent with POST");
    if (request.accept_gzip && request.range_from.has_value())
        return refuse("gzip and a range are not requested together");
    if (breaks_line(user_agent))
        return refuse("the user agent cannot be sent");
    if (request.method == Method::post && breaks_line(request.content_type))
        return refuse("the content type cannot be sent");
    for (const Header& header : request.headers) {
        if (header.name.empty() || header.name.find(':') != std::string::npos ||
            header.name.find(' ') != std::string::npos || breaks_line(header.name))
            return refuse("a header name cannot be sent");
        for (const char byte : header.name) {
            const unsigned char value = static_cast<unsigned char>(byte);
            if (value < 33 || value == 127)
                return refuse("a header name cannot be sent");
        }
        if (breaks_line(header.value))
            return refuse("a header value cannot be sent");
        if (reserved_header(header.name))
            return refuse("the header " + header.name + " is set by the client");
    }

    const char* method = "GET";
    if (request.method == Method::head)
        method = "HEAD";
    else if (request.method == Method::post)
        method = "POST";

    PreparedRequest prepared;
    append_text(prepared.bytes, method);
    append_text(prepared.bytes, " ");
    append_text(prepared.bytes, url.target);
    append_text(prepared.bytes, " HTTP/1.1\r\n");
    append_text(prepared.bytes, "Host: ");
    append_text(prepared.bytes, oa::formats::url::host_header(url));
    append_text(prepared.bytes, "\r\n");
    append_text(prepared.bytes, "User-Agent: ");
    append_text(prepared.bytes, user_agent);
    append_text(prepared.bytes, "\r\n");
    append_text(prepared.bytes, "Accept: */*\r\n");
    if (request.method == Method::post) {
        const uint64_t length = request.body.size();
        append_text(prepared.bytes, "Content-Length: ");
        append_text(prepared.bytes, std::to_string(length));
        append_text(prepared.bytes, "\r\n");
        append_text(prepared.bytes, "Content-Type: ");
        append_text(
            prepared.bytes,
            request.content_type.empty() ? std::string_view("application/octet-stream")
                                         : std::string_view(request.content_type)
        );
        append_text(prepared.bytes, "\r\n");
    }
    if (request.range_from.has_value()) {
        append_text(prepared.bytes, "Range: bytes=");
        append_text(prepared.bytes, std::to_string(*request.range_from));
        append_text(prepared.bytes, "-\r\n");
    }
    if (request.accept_gzip)
        append_text(prepared.bytes, "Accept-Encoding: gzip\r\n");
    for (const Header& header : request.headers) {
        append_text(prepared.bytes, header.name);
        append_text(prepared.bytes, ": ");
        append_text(prepared.bytes, header.value);
        append_text(prepared.bytes, "\r\n");
    }
    append_text(prepared.bytes, "\r\n");
    if (request.method == Method::post)
        prepared.bytes.insert(prepared.bytes.end(), request.body.begin(), request.body.end());
    return prepared;
}

} // namespace oa::netgame::http
