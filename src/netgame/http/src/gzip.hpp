// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Incremental gzip inflate for one response body.
#pragma once

#include "oa/netgame/http/client.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include <zlib.h>

namespace oa::netgame::http {

/// Decodes one gzip body as its compressed bytes arrive.
class GzipDecoder {
  public:

    /// Prepares a decoder that has not seen a byte.
    GzipDecoder();

    /// Releases the inflate state.
    ~GzipDecoder();

    GzipDecoder(const GzipDecoder&) = delete;
    GzipDecoder& operator=(const GzipDecoder&) = delete;

    /// Decodes the next compressed bytes.
    ///
    /// Bytes that fit in the limit are appended to `decoded`. A piece that
    /// would pass the limit appends only what fits.
    ///
    /// @param compressed the next compressed bytes
    /// @param[out] decoded the decoded bytes of this call
    /// @param already decoded bytes already produced for this body
    /// @param limit the most decoded bytes
    /// @return none, too_large, or decode_failed
    [[nodiscard]] Failure push(
        std::span<const uint8_t> compressed,
        std::vector<uint8_t>& decoded,
        uint64_t already,
        uint64_t limit
    );

    /// Finishes the body. No more compressed bytes will arrive.
    ///
    /// @param[out] decoded the last decoded bytes
    /// @param already decoded bytes already produced for this body
    /// @param limit the most decoded bytes
    /// @return none, too_large, or decode_failed
    [[nodiscard]] Failure finish(std::vector<uint8_t>& decoded, uint64_t already, uint64_t limit);

    /// Tells whether the gzip stream has ended.
    ///
    /// @return true after the stream's last byte
    [[nodiscard]] bool ended() const;

  private:

    /// Inflates one span.
    ///
    /// @param compressed the compressed bytes; empty when finishing
    /// @param[out] decoded the decoded bytes of this call
    /// @param already decoded bytes already produced
    /// @param limit the most decoded bytes
    /// @param finishing true when no more compressed bytes will arrive
    /// @return none, too_large, or decode_failed
    [[nodiscard]] Failure inflate_some(
        std::span<const uint8_t> compressed,
        std::vector<uint8_t>& decoded,
        uint64_t already,
        uint64_t limit,
        bool finishing
    );

    z_stream stream_{};
    bool started_ = false;
    bool ended_ = false;
};

} // namespace oa::netgame::http
