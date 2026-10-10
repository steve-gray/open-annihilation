// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A push parser for one HTTP response. It is fed bytes as they arrive and
// does not touch a socket. The body it reports is dechunked and still
// encoded: gzip is decoded by the caller.
#pragma once

#include "oa/netgame/http/client.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oa::netgame::http {

/// What the parser has ready after one feed or a close.
enum class ReadKind : uint8_t {
    need_more, ///< feed more bytes, or finish when the peer has closed
    interim,   ///< a 1xx response was skipped
    head,      ///< the final status and headers are ready
    body,      ///< dechunked body bytes are ready
    finished,  ///< the response is complete
    failed,    ///< the response failed; failure() says why
};

/// One event from the parser.
///
/// `bytes` is set for a body event and stays valid until the next feed or finish.
struct ReadPiece {
    ReadKind kind = ReadKind::need_more; ///< which event this is
    std::span<const uint8_t> bytes{};    ///< the body piece, for a body event
};

/// Reads one response from bytes pushed in as they arrive.
class ResponseReader {
  public:

    /// Prepares a reader for one response.
    ///
    /// @param limits the header and body bounds
    /// @param method the request method; HEAD has no body
    /// @param accept_gzip whether a gzip content coding is accepted
    ResponseReader(Limits limits, Method method, bool accept_gzip);

    /// Feeds arrived bytes and reports the next event.
    ///
    /// `taken` is how many bytes of `incoming` this call consumed. A body
    /// span stays valid until the next feed or finish.
    ///
    /// @param incoming the bytes that arrived
    /// @param[out] taken how many of them were consumed
    /// @return the next event
    [[nodiscard]] ReadPiece feed(std::span<const uint8_t> incoming, std::size_t& taken);

    /// Tells the reader the peer closed the connection.
    ///
    /// A body that was being read to the close finishes. A short counted or
    /// chunked body, or a response that had not started, is connection_lost.
    ///
    /// @return finished, or failed
    [[nodiscard]] ReadPiece finish();

    /// Returns the status of the final response.
    ///
    /// @return the status, or 0 before the final status line
    [[nodiscard]] int status() const;

    /// Returns the final response's headers.
    ///
    /// @return the headers, in the order they were sent
    [[nodiscard]] const std::vector<Header>& headers() const;

    /// Returns the Content-Length that was sent, when there was one.
    ///
    /// @return the length before decoding, or nothing when it was absent
    [[nodiscard]] std::optional<uint64_t> content_length() const;

    /// Tells whether the body was chunked.
    ///
    /// @return true when Transfer-Encoding was chunked
    [[nodiscard]] bool chunked() const;

    /// Tells whether the body is gzip.
    ///
    /// @return true when Content-Encoding was gzip
    [[nodiscard]] bool gzip() const;

    /// Tells whether the connection can carry another request.
    ///
    /// True only after a complete response that was not read to the close and
    /// that did not ask for the connection to end.
    ///
    /// @return true when the connection may be kept
    [[nodiscard]] bool reusable() const;

    /// Returns why the response failed.
    ///
    /// @return the failure, or none
    [[nodiscard]] Failure failure() const;

    /// Returns the English detail of a failure.
    ///
    /// @return the detail; empty when the response has not failed
    [[nodiscard]] const std::string& detail() const;

  private:

    enum class Phase : uint8_t {
        status_line,
        headers,
        counted,
        chunk_size,
        chunk_data,
        chunk_cr,
        chunk_lf,
        trailers,
        until_close,
        done,
        failed,
    };

    enum class LineTake : uint8_t { ready, need_more, too_large };

    /// Copies bytes into the line buffer until a line break or the input ends.
    ///
    /// @param incoming the bytes still to read
    /// @param[in,out] taken how many of them have been consumed
    /// @param[in,out] counted bytes already counted against `budget`
    /// @param budget the most bytes this section may hold
    /// @return ready when a line was finished
    [[nodiscard]] LineTake take_line(
        std::span<const uint8_t> incoming,
        std::size_t& taken,
        std::size_t& counted,
        std::size_t budget
    );

    /// Checks the headers and chooses how the body is framed.
    ///
    /// @return the head event, an interim event, or a failure
    [[nodiscard]] ReadPiece finish_headers();

    /// Records a failure and returns it.
    ///
    /// @param failure why the response failed
    /// @param detail the English detail
    /// @return a failed piece
    [[nodiscard]] ReadPiece fail(Failure failure, std::string detail);

    /// A finished piece.
    ///
    /// @return finished
    [[nodiscard]] ReadPiece succeed();

    Limits limits_{};
    Method method_ = Method::get;
    bool accept_gzip_ = false;
    Phase phase_ = Phase::status_line;
    int status_ = 0;
    int version_ = 0;
    std::vector<Header> headers_{};
    std::optional<uint64_t> content_length_{};
    bool length_conflict_ = false;
    bool length_malformed_ = false;
    bool saw_length_ = false;
    bool chunked_ = false;
    bool bad_transfer_ = false;
    bool gzip_ = false;
    bool bad_encoding_ = false;
    bool saw_encoding_ = false;
    bool connection_close_ = false;
    bool connection_keep_alive_ = false;
    bool keep_alive_ = false;
    bool close_delimited_ = false;
    bool complete_ = false;
    uint64_t body_remaining_ = 0;
    uint64_t chunk_remaining_ = 0;
    uint64_t body_emitted_ = 0;
    std::size_t section_bytes_ = 0;
    std::string line_{};
    std::vector<uint8_t> emit_{};
    Failure failure_ = Failure::none;
    std::string detail_{};
};

} // namespace oa::netgame::http
