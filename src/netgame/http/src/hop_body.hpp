// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The body of one HTTPS hop the system's stack already split into a status,
// headers and bytes. Redirects are not given to the sink. gzip is decoded
// here, because the stack is asked not to decode it.
#pragma once

#include "fields.hpp"
#include "gzip.hpp"
#include "tls.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::netgame::http {

/// Gives one hop's body to the sink, with the same range and size rules as
/// the socket client.
class HopBody {
  public:

    /// Binds the hop to the caller's request and sink.
    ///
    /// @param request the limits and the range
    /// @param sink the body sink
    /// @param exchange filled as the body arrives
    /// @param final_url the address of this hop
    HopBody(const Request& request, BodySink& sink, HttpsExchange& exchange, std::string final_url)
        : request_(request), sink_(sink), exchange_(exchange), final_url_(std::move(final_url)) {}

    /// Accepts the status and headers. A redirect is recorded and not begun.
    ///
    /// @return false when the hop has failed
    [[nodiscard]] bool take_head() {
        const std::string* encoding = header_value(exchange_.headers, "content-encoding");
        gzip_ = request_.accept_gzip && encoding != nullptr && same_name(*encoding, "gzip");
        const std::string* length = header_value(exchange_.headers, "content-length");
        if (length != nullptr) {
            uint64_t value = 0;
            if (parse_u64(trim_ows(*length), value))
                exchange_.content_length = value;
        }
        if (exchange_.status == 301 || exchange_.status == 302 || exchange_.status == 303 ||
            exchange_.status == 307 || exchange_.status == 308) {
            redirect_ = true;
            return true;
        }
        if (request_.range_from.has_value() && exchange_.status == 206) {
            const std::string* header = header_value(exchange_.headers, "content-range");
            const std::optional<ContentRange> range =
                header == nullptr ? std::nullopt : parse_content_range(*header);
            const bool length_ok =
                !exchange_.content_length.has_value() || gzip_ ||
                (range && range->last >= range->first && range->last - range->first != UINT64_MAX &&
                 *exchange_.content_length == range->last - range->first + 1);
            if (!range || range->first != *request_.range_from || !length_ok) {
                fail(Failure::bad_response, "the content range does not match");
                return false;
            }
            exchange_.content_range = range;
        }
        if (!gzip_ && exchange_.content_length)
            expected_ = exchange_.content_length;
        Response head;
        head.status = exchange_.status;
        head.headers = exchange_.headers;
        head.content_length = exchange_.content_length;
        head.content_range = exchange_.content_range;
        head.final_url = final_url_;
        if (!sink_.begin(head)) {
            fail(Failure::sink_stopped, "the body was stopped");
            return false;
        }
        exchange_.delivered = true;
        delivering_ = true;
        return true;
    }

    /// Accepts the next encoded bytes.
    ///
    /// @param bytes the bytes
    /// @return false when the hop has failed
    [[nodiscard]] bool take_bytes(std::span<const uint8_t> bytes) {
        if (redirect_ || !delivering_ || exchange_.failure != Failure::none)
            return exchange_.failure == Failure::none;
        if (gzip_) {
            std::vector<uint8_t> decoded;
            const Failure decoded_failure =
                decoder_.push(bytes, decoded, exchange_.body_bytes, request_.limits.max_body_bytes);
            if (!give(decoded))
                return false;
            if (decoded_failure != Failure::none) {
                fail(
                    decoded_failure,
                    decoded_failure == Failure::too_large ? "the body is larger than the limit"
                                                          : "the body could not be decoded"
                );
                return false;
            }
            return true;
        }
        return give(bytes);
    }

    /// Finishes a gzip body.
    ///
    /// @return false when the hop has failed
    [[nodiscard]] bool finish() {
        if (redirect_ || !delivering_ || !gzip_ || exchange_.failure != Failure::none)
            return exchange_.failure == Failure::none;
        std::vector<uint8_t> decoded;
        const Failure decoded_failure =
            decoder_.finish(decoded, exchange_.body_bytes, request_.limits.max_body_bytes);
        if (!give(decoded))
            return false;
        if (decoded_failure != Failure::none) {
            fail(
                decoded_failure,
                decoded_failure == Failure::too_large ? "the body is larger than the limit"
                                                      : "the body could not be decoded"
            );
            return false;
        }
        return true;
    }

    /// True when the status is a redirect and the body is discarded.
    ///
    /// @return true for 301, 302, 303, 307 and 308
    [[nodiscard]] bool redirect() const { return redirect_; }

  private:

    /// Records a failure.
    ///
    /// @param failure the failure
    /// @param detail the English reason
    void fail(Failure failure, const char* detail) {
        exchange_.failure = failure;
        exchange_.detail = detail;
    }

    /// Gives one decoded piece to the sink.
    ///
    /// @param bytes the piece
    /// @return false when the sink stopped or the piece passed the limit
    [[nodiscard]] bool give(std::span<const uint8_t> bytes) {
        const uint64_t have = exchange_.body_bytes;
        const uint64_t room =
            have >= request_.limits.max_body_bytes ? 0 : request_.limits.max_body_bytes - have;
        std::span<const uint8_t> piece = bytes;
        if (static_cast<uint64_t>(piece.size()) > room)
            piece = piece.first(static_cast<std::size_t>(room));
        if (!piece.empty()) {
            if (!sink_.write(piece)) {
                fail(Failure::sink_stopped, "the body was stopped");
                return false;
            }
            exchange_.body_bytes += static_cast<uint64_t>(piece.size());
            if (request_.progress != nullptr) {
                Progress progress;
                progress.received = exchange_.body_bytes;
                progress.expected = expected_;
                request_.progress(request_.progress_context, progress);
            }
        }
        if (piece.size() != bytes.size()) {
            fail(Failure::too_large, "the body is larger than the limit");
            return false;
        }
        return true;
    }

    const Request& request_;
    BodySink& sink_;
    HttpsExchange& exchange_;
    std::string final_url_;
    GzipDecoder decoder_;
    std::optional<uint64_t> expected_;
    bool delivering_ = false;
    bool gzip_ = false;
    bool redirect_ = false;
};

} // namespace oa::netgame::http
