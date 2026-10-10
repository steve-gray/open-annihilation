// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The response reader, fed a whole message and one byte at a time.

#include "response_reader.hpp"

#include "oa/test/check.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using oa::netgame::http::Failure;
using oa::netgame::http::Header;
using oa::netgame::http::Limits;
using oa::netgame::http::Method;
using oa::netgame::http::ReadKind;
using oa::netgame::http::ReadPiece;
using oa::netgame::http::ResponseReader;

/// What one message became.
struct ReadResult {
    Failure failure = Failure::none;
    int status = 0;
    std::vector<Header> headers;
    std::vector<uint8_t> body;
    std::optional<uint64_t> content_length;
    bool chunked = false;
    bool gzip = false;
    bool reusable = false;
    int interims = 0;
};

/// Copies a text into bytes.
///
/// @param text the message
/// @return the bytes
std::vector<uint8_t> as_bytes(std::string_view text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

/// Feeds a message and closes the reader.
///
/// @param bytes the message
/// @param one_byte true to feed one byte at a time
/// @param method the request method
/// @param limits the size limits
/// @param accept_gzip whether gzip is accepted
/// @return the outcome
ReadResult read_message(
    std::span<const uint8_t> bytes, bool one_byte, Method method, Limits limits, bool accept_gzip
) {
    ResponseReader reader(limits, method, accept_gzip);
    ReadResult result;
    std::size_t offset = 0;
    bool stopped = false;
    const std::size_t guard = bytes.size() * 4 + 8;
    std::size_t steps = 0;
    while (offset < bytes.size() && !stopped && steps < guard) {
        ++steps;
        const std::size_t count = one_byte ? 1 : bytes.size() - offset;
        std::size_t taken = 0;
        const ReadPiece piece = reader.feed(bytes.subspan(offset, count), taken);
        if (piece.kind == ReadKind::body)
            result.body.insert(result.body.end(), piece.bytes.begin(), piece.bytes.end());
        if (piece.kind == ReadKind::interim)
            ++result.interims;
        if (piece.kind == ReadKind::failed || piece.kind == ReadKind::finished)
            stopped = true;
        if (taken == 0 && piece.kind == ReadKind::need_more) {
            result.failure = Failure::bad_response;
            return result;
        }
        offset += taken;
    }
    if (steps >= guard && !stopped) {
        result.failure = Failure::bad_response;
        return result;
    }
    if (!stopped) {
        const ReadPiece piece = reader.finish();
        if (piece.kind == ReadKind::body)
            result.body.insert(result.body.end(), piece.bytes.begin(), piece.bytes.end());
    }
    result.failure = reader.failure();
    result.status = reader.status();
    result.headers = reader.headers();
    result.content_length = reader.content_length();
    result.chunked = reader.chunked();
    result.gzip = reader.gzip();
    result.reusable = reader.reusable();
    return result;
}

/// True when the body is that text.
///
/// @param body the body
/// @param text the expected text
/// @return true when every byte matches
bool body_is(const std::vector<uint8_t>& body, std::string_view text) {
    return body.size() == text.size() &&
           std::equal(body.begin(), body.end(), reinterpret_cast<const uint8_t*>(text.data()));
}

/// Reads both ways and checks a whole body.
///
/// @param name the case
/// @param message the bytes
/// @param text the expected body
/// @param reusable whether the connection may be kept
void expect_body(const char* name, std::string_view message, std::string_view text, bool reusable) {
    std::printf("%s\n", name);
    const std::vector<uint8_t> bytes = as_bytes(message);
    for (const bool one_byte : {false, true}) {
        const ReadResult result = read_message(bytes, one_byte, Method::get, Limits{}, false);
        if (result.failure != Failure::none)
            std::fprintf(
                stderr,
                "%s (%s): %d\n",
                name,
                one_byte ? "byte" : "whole",
                static_cast<int>(result.failure)
            );
        OA_CHECK(result.failure == Failure::none);
        OA_CHECK(result.status == 200);
        OA_CHECK(body_is(result.body, text));
        OA_CHECK(result.reusable == reusable);
    }
}

/// Reads both ways and checks a failure.
///
/// @param name the case
/// @param message the bytes
/// @param failure the expected failure
void expect_failure(const char* name, std::string_view message, Failure failure) {
    std::printf("%s\n", name);
    const std::vector<uint8_t> bytes = as_bytes(message);
    for (const bool one_byte : {false, true}) {
        const ReadResult result = read_message(bytes, one_byte, Method::get, Limits{}, false);
        OA_CHECK(result.failure == failure);
    }
}

} // namespace

int main() {
    expect_body(
        "content-length", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", "hello", true
    );
    expect_body(
        "chunked",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
        "hello",
        true
    );
    expect_body("until-close", "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nhello", "hello", false);

    std::printf("interim 100\n");
    {
        const std::vector<uint8_t> bytes = as_bytes(
            "HTTP/1.1 100 Continue\r\nX-Interim: 1\r\n\r\n"
            "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"
        );
        for (const bool one_byte : {false, true}) {
            const ReadResult result = read_message(bytes, one_byte, Method::get, Limits{}, false);
            OA_CHECK(result.failure == Failure::none);
            OA_CHECK(result.status == 200);
            OA_CHECK(result.interims == 1);
            OA_CHECK(body_is(result.body, "hello"));
            OA_CHECK(result.headers.size() == 1);
        }
    }

    std::printf("http/1.0\n");
    {
        const std::vector<uint8_t> plain =
            as_bytes("HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello");
        const std::vector<uint8_t> kept =
            as_bytes("HTTP/1.0 200 OK\r\nContent-Length: 5\r\nConnection: keep-alive\r\n\r\nhello");
        for (const bool one_byte : {false, true}) {
            const ReadResult plain_result =
                read_message(plain, one_byte, Method::get, Limits{}, false);
            OA_CHECK(plain_result.failure == Failure::none);
            OA_CHECK(plain_result.status == 200);
            OA_CHECK(!plain_result.reusable);
            const ReadResult kept_result =
                read_message(kept, one_byte, Method::get, Limits{}, false);
            OA_CHECK(kept_result.failure == Failure::none);
            OA_CHECK(kept_result.reusable);
        }
    }

    std::printf("chunk extension and trailer\n");
    {
        const std::vector<uint8_t> bytes = as_bytes(
            "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
            "5;ext=1\r\nhello\r\n0\r\nX-Trailer: 1\r\n\r\n"
        );
        for (const bool one_byte : {false, true}) {
            const ReadResult result = read_message(bytes, one_byte, Method::get, Limits{}, false);
            OA_CHECK(result.failure == Failure::none);
            OA_CHECK(body_is(result.body, "hello"));
            OA_CHECK(result.chunked);
            OA_CHECK(result.headers.size() == 1);
            OA_CHECK(result.reusable);
        }
    }

    std::printf("bare line feed\n");
    {
        const ReadResult result = read_message(
            as_bytes("HTTP/1.1 200 OK\nContent-Length: 2\n\nok"),
            false,
            Method::get,
            Limits{},
            false
        );
        OA_CHECK(result.failure == Failure::none);
        OA_CHECK(body_is(result.body, "ok"));
    }

    std::printf("head has no body\n");
    {
        const ReadResult result = read_message(
            as_bytes("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
            false,
            Method::head,
            Limits{},
            false
        );
        OA_CHECK(result.failure == Failure::none);
        OA_CHECK(result.body.empty());
        OA_CHECK(result.content_length && *result.content_length == 5);
        OA_CHECK(result.reusable);
    }

    std::printf("identical content lengths\n");
    {
        const ReadResult result = read_message(
            as_bytes("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\nok"),
            false,
            Method::get,
            Limits{},
            false
        );
        OA_CHECK(result.failure == Failure::none);
        OA_CHECK(body_is(result.body, "ok"));
    }

    std::printf("content coding gzip when accepted\n");
    {
        const ReadResult result = read_message(
            as_bytes("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 3\r\n\r\nabc"),
            false,
            Method::get,
            Limits{},
            true
        );
        OA_CHECK(result.failure == Failure::none);
        OA_CHECK(result.gzip);
        OA_CHECK(body_is(result.body, "abc"));
    }

    expect_failure("bad status line", "HTTP/2.0 200 OK\r\n\r\n", Failure::bad_response);
    expect_failure(
        "folded header",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n Folded: yes\r\n\r\n",
        Failure::bad_response
    );
    expect_failure(
        "two content lengths",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        Failure::bad_response
    );
    expect_failure(
        "bad chunk size",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n",
        Failure::bad_response
    );
    expect_failure(
        "short body at close",
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhi",
        Failure::connection_lost
    );
    expect_failure(
        "transfer coding other than chunked",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
        Failure::bad_response
    );
    expect_failure(
        "content coding gzip when not accepted",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 0\r\n\r\n",
        Failure::bad_response
    );

    std::printf("header limit\n");
    {
        const std::string message = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        Limits limits;
        limits.max_header_bytes = message.size() - 1;
        const ReadResult over = read_message(as_bytes(message), false, Method::get, limits, false);
        OA_CHECK(over.failure == Failure::too_large);
        limits.max_header_bytes = message.size();
        const ReadResult exact = read_message(as_bytes(message), true, Method::get, limits, false);
        OA_CHECK(exact.failure == Failure::none);
        OA_CHECK(exact.status == 200);
    }

    std::printf("body limit\n");
    {
        Limits limits;
        limits.max_body_bytes = 4;
        const ReadResult over = read_message(
            as_bytes("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"),
            false,
            Method::get,
            limits,
            false
        );
        OA_CHECK(over.failure == Failure::too_large);
        OA_CHECK(over.body.empty());
        Limits chunk_limits;
        chunk_limits.max_body_bytes = 3;
        const ReadResult chunks = read_message(
            as_bytes(
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: "
                "chunked\r\n\r\n2\r\nab\r\n2\r\ncd\r\n0\r\n\r\n"
            ),
            true,
            Method::get,
            chunk_limits,
            false
        );
        OA_CHECK(chunks.failure == Failure::too_large);
        OA_CHECK(chunks.body.size() == 2);
    }

    return oa::test::check_exit_status();
}
