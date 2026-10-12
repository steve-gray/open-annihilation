// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// LZMA and LZMA2 fixtures: a whole stream, the same stream one byte at a
// time, and a stream with one damaged byte, which fails.

#include "oa/formats/lzma.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace {

using oa::formats::lzma::Decoder;
using oa::formats::lzma::Feed;
using oa::formats::lzma::FeedStatus;
using oa::formats::lzma::Kind;

/// Reads one fixture.
///
/// @param name the file name under the fixture directory
/// @return the bytes
std::vector<uint8_t> read_fixture(const char* name) {
    const std::filesystem::path path = std::filesystem::path(OA_LZMA_TEST_DATA) / name;
    std::ifstream file(path, std::ios::binary);
    OA_CHECK(file.good());
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()
    );
}

/// Decodes `encoded` in pieces of `piece` bytes into a buffer of `piece` bytes.
///
/// @param kind which coding the bytes use
/// @param encoded the stream
/// @param piece how many input and output bytes one feed is given
/// @param plain the decoded bytes, set when the stream finishes
/// @return true when the stream finishes and finish() agrees
bool decode(
    Kind kind, const std::vector<uint8_t>& encoded, size_t piece, std::vector<uint8_t>* plain
) {
    Decoder decoder(kind);
    plain->clear();
    size_t input_at = 0;
    std::vector<uint8_t> room(piece);
    for (int step_index = 0; step_index < 1000000; ++step_index) {
        const size_t left = encoded.size() - input_at;
        const size_t offer = left < piece ? left : piece;
        const Feed fed = decoder.feed(
            std::span<const uint8_t>(encoded.data() + input_at, offer),
            std::span<uint8_t>(room.data(), room.size())
        );
        input_at += fed.consumed;
        plain->insert(
            plain->end(), room.begin(), room.begin() + static_cast<std::ptrdiff_t>(fed.produced)
        );
        if (fed.status == FeedStatus::finished)
            return decoder.finish() == FeedStatus::finished && input_at <= encoded.size();
        if (fed.status == FeedStatus::error)
            return false;
        if (fed.consumed == 0 && fed.produced == 0)
            return false;
        if (input_at == encoded.size() && fed.status == FeedStatus::need_input)
            return decoder.finish() == FeedStatus::finished;
    }
    return false;
}

/// Checks one fixture whole and one byte at a time.
///
/// @param kind which coding the fixture uses
/// @param name the fixture file name
void expect_round_trip(Kind kind, const char* name) {
    const std::vector<uint8_t> plain = read_fixture("plain.bin");
    const std::vector<uint8_t> encoded = read_fixture(name);
    std::vector<uint8_t> whole;
    std::vector<uint8_t> bytes;
    OA_CHECK(decode(kind, encoded, encoded.size() + 64, &whole));
    OA_CHECK(whole == plain);
    OA_CHECK(decode(kind, encoded, 1, &bytes));
    OA_CHECK(bytes == plain);
}

/// A damaged compressed byte fails, and does not decode as the plain text.
///
/// @param kind which coding the fixture uses
/// @param name the fixture file name
void expect_corrupt(Kind kind, const char* name) {
    std::vector<uint8_t> encoded = read_fixture(name);
    OA_CHECK(encoded.size() > 20);
    encoded[encoded.size() / 2] ^= 0xff;
    std::vector<uint8_t> plain;
    OA_CHECK(!decode(kind, encoded, encoded.size(), &plain));
    OA_CHECK(plain != read_fixture("plain.bin"));
    Decoder decoder(kind);
    const Feed fed = decoder.feed(encoded, std::span<uint8_t>());
    OA_CHECK(
        fed.status == FeedStatus::error || fed.status == FeedStatus::need_output ||
        fed.status == FeedStatus::ready
    );
    OA_CHECK(decoder.finish() == FeedStatus::error || fed.status != FeedStatus::finished);
}

} // namespace

int main() {
    // The same LZMA stream with the plain size written into the header.
    {
        std::vector<uint8_t> encoded = read_fixture("plain.lzma");
        const std::vector<uint8_t> plain = read_fixture("plain.bin");
        for (int index = 0; index < 8; ++index)
            encoded[static_cast<size_t>(5 + index)] =
                static_cast<uint8_t>(plain.size() >> (8 * index));
        std::vector<uint8_t> decoded;
        OA_CHECK(decode(Kind::lzma, encoded, 1, &decoded));
        OA_CHECK(decoded == plain);
    }
    expect_round_trip(Kind::lzma, "plain.lzma");
    expect_round_trip(Kind::lzma2, "plain.lzma2");
    expect_corrupt(Kind::lzma, "plain.lzma");
    expect_corrupt(Kind::lzma2, "plain.lzma2");

    Decoder short_header(Kind::lzma);
    const uint8_t few[] = {0x5d, 0x00, 0x00};
    const Feed fed = short_header.feed(few, std::span<uint8_t>());
    OA_CHECK(fed.status == FeedStatus::need_input);
    OA_CHECK(short_header.finish() == FeedStatus::error);
    return oa::test::check_exit_status();
}
