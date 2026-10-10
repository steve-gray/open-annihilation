// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Gzip through zlib's inflate, with the gzip wrapper selected by the window bits.

#include "gzip.hpp"

namespace oa::netgame::http {
namespace {

/// The most compressed bytes handed to inflate at once.
constexpr std::size_t pass_limit = 1u << 20;

/// Failure for a gzip body that is not a gzip body.
///
/// @return decode_failed
Failure corrupt() {
    return Failure::decode_failed;
}

} // namespace

GzipDecoder::GzipDecoder() = default;

GzipDecoder::~GzipDecoder() {
    if (started_)
        inflateEnd(&stream_);
}

bool GzipDecoder::ended() const {
    return ended_;
}

Failure GzipDecoder::push(
    std::span<const uint8_t> compressed,
    std::vector<uint8_t>& decoded,
    uint64_t already,
    uint64_t limit
) {
    decoded.clear();
    return inflate_some(compressed, decoded, already, limit, false);
}

Failure GzipDecoder::finish(std::vector<uint8_t>& decoded, uint64_t already, uint64_t limit) {
    decoded.clear();
    return inflate_some({}, decoded, already, limit, true);
}

Failure GzipDecoder::inflate_some(
    std::span<const uint8_t> compressed,
    std::vector<uint8_t>& decoded,
    uint64_t already,
    uint64_t limit,
    bool finishing
) {
    if (ended_)
        return compressed.empty() ? Failure::none : corrupt();
    if (!started_) {
        stream_ = z_stream{};
        // 15 is the largest window. Adding 16 selects the gzip wrapper.
        if (inflateInit2(&stream_, 15 + 16) != Z_OK)
            return corrupt();
        started_ = true;
    }

    std::size_t offset = 0;
    bool flushed = false;
    while (true) {
        const uint64_t have = already + static_cast<uint64_t>(decoded.size());
        const uint64_t room = have >= limit ? 0 : limit - have;
        uint8_t buffer[4096];
        uint8_t overflow = 0;
        const std::size_t cap =
            room == 0 ? 1 : (room < sizeof buffer ? static_cast<std::size_t>(room) : sizeof buffer);
        const std::size_t left = compressed.size() - offset;
        const std::size_t pass = left < pass_limit ? left : pass_limit;
        const bool last_input = finishing && pass == left;
        Bytef dummy = 0;
        stream_.next_in = pass == 0 ? &dummy : const_cast<Bytef*>(compressed.data() + offset);
        stream_.avail_in = static_cast<uInt>(pass);
        stream_.next_out = room == 0 ? &overflow : buffer;
        stream_.avail_out = static_cast<uInt>(cap);
        const int result = inflate(&stream_, last_input ? Z_FINISH : Z_NO_FLUSH);
        const std::size_t consumed = pass - static_cast<std::size_t>(stream_.avail_in);
        offset += consumed;
        const std::size_t got = cap - static_cast<std::size_t>(stream_.avail_out);
        if (room == 0) {
            if (got > 0)
                return Failure::too_large;
        } else if (got > 0) {
            decoded.insert(decoded.end(), buffer, buffer + got);
        }
        if (result == Z_STREAM_END) {
            ended_ = true;
            if (stream_.avail_in > 0 || offset < compressed.size())
                return corrupt();
            return Failure::none;
        }
        if (result != Z_OK && result != Z_BUF_ERROR)
            return corrupt();
        if (consumed == 0 && got == 0) {
            if (!finishing)
                return offset >= compressed.size() ? Failure::none : corrupt();
            if (!flushed) {
                flushed = true;
                continue;
            }
            return corrupt();
        }
        if (offset < compressed.size())
            continue;
        if (!finishing)
            return Failure::none;
        if (got > 0)
            continue;
        if (!flushed) {
            flushed = true;
            continue;
        }
        return corrupt();
    }
}

} // namespace oa::netgame::http
