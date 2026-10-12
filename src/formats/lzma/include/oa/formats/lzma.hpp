// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// LZMA and LZMA2, decoded a piece at a time.
//
// An LZMA stream starts with the 13-byte properties header: one properties
// byte, the dictionary size as 4 little-endian bytes, and the uncompressed
// size as 8 little-endian bytes (every byte 0xff when the size is not
// stated). The compressed bytes follow. An LZMA2 stream starts with one
// dictionary-property byte (0 to 40) and then the raw LZMA2 bytes, ending
// with a zero control byte.
//
// The dictionary either header names is refused when it is larger than
// max_dictionary_bytes. A stream that is not LZMA or LZMA2, or that ends
// before it has finished, fails without writing past the output given to it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::formats::lzma {

/// The largest dictionary a stream may ask for, in bytes.
inline constexpr uint32_t max_dictionary_bytes = 256u << 20;

/// Which coding a stream uses.
enum class Kind : uint8_t {
    lzma,  ///< properties header, then LZMA
    lzma2, ///< one dictionary-property byte, then LZMA2
};

/// What one feed of a stream did.
enum class FeedStatus : uint8_t {
    ready,       ///< consumed input or wrote output, and the stream is not finished
    finished,    ///< the stream ended; further feeds stay finished
    need_input,  ///< the bytes so far are not enough to go on
    need_output, ///< the output span has no room
    error,       ///< the stream is not valid; further feeds stay in error
};

/// How many bytes one feed used, and what to do next.
struct Feed {
    size_t consumed{};                    ///< input bytes taken
    size_t produced{};                    ///< output bytes written
    FeedStatus status{FeedStatus::error}; ///< what the feed did
};

/// Decodes one LZMA or LZMA2 stream from pieces of input into pieces of output.
class Decoder {
  public:

    /// Prepares a decoder for one stream of `kind`.
    ///
    /// @param kind which coding the bytes use
    explicit Decoder(Kind kind);

    /// Frees the dictionary the stream asked for.
    ~Decoder();

    /// A decoder holds the stream's dictionary, so it is not copied.
    Decoder(const Decoder&) = delete;

    /// A decoder holds the stream's dictionary, so it is not copied.
    Decoder& operator=(const Decoder&) = delete;

    /// Decodes as much of `input` as fits in `output`.
    ///
    /// A feed that can neither take input nor write output does nothing.
    /// Bytes left unconsumed stay for the next feed, in the same order.
    ///
    /// @param input the next compressed bytes, which may be empty
    /// @param output room for the decoded bytes, which may be empty
    /// @return the bytes used and whether the stream finished or failed
    [[nodiscard]] Feed feed(std::span<const uint8_t> input, std::span<uint8_t> output);

    /// Marks the end of the input.
    ///
    /// @return finished when the stream had already ended, otherwise error
    [[nodiscard]] FeedStatus finish();

  private:

    /// Allocates from the properties header and starts the decode.
    ///
    /// @return false when the header is not a supported stream
    [[nodiscard]] bool open();

    Kind kind_;
    bool opened_{false};
    bool finished_{false};
    bool failed_{false};
    uint8_t header_[13]{};
    uint8_t header_filled_{0};
    bool size_known_{false};
    uint64_t uncompressed_size_{0};
    uint64_t produced_total_{0};
    void* state_{nullptr};
};
} // namespace oa::formats::lzma
