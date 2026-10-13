// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/gaf.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gaf = oa::formats::gaf;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
}

void put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[at + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[at + 3] = static_cast<uint8_t>(value >> 24U);
}

void frame_header(
    std::vector<uint8_t>& bytes,
    std::size_t at,
    uint16_t width,
    uint16_t height,
    int16_t origin_x,
    int16_t origin_y,
    uint8_t transparency,
    bool compressed,
    uint8_t layers,
    uint8_t special,
    uint32_t data
) {
    put16(bytes, at, width);
    put16(bytes, at + 2, height);
    put16(bytes, at + 4, static_cast<uint16_t>(origin_x));
    put16(bytes, at + 6, static_cast<uint16_t>(origin_y));
    bytes[at + 8] = transparency;
    bytes[at + 9] = compressed ? 1 : 0;
    bytes[at + 10] = layers;
    bytes[at + 11] = special;
    put32(bytes, at + 16, data);
}

std::vector<uint8_t> one_frame_file(std::size_t frame_at = 64) {
    std::vector<uint8_t> bytes(256);
    put32(bytes, 0, 0x00010100U);
    // The high 16 bits are nonzero on purpose: only the signed low 16 bits count.
    put32(bytes, 4, 0xabcd0001U);
    put32(bytes, 12, 16);
    put16(bytes, 16, 1);
    constexpr std::string_view name = "TEST";
    for (std::size_t i = 0; i < name.size(); ++i)
        bytes[24 + i] = static_cast<uint8_t>(name[i]);
    put32(bytes, 56, static_cast<uint32_t>(frame_at));
    // High half is ignored by the duration lookup.
    put32(bytes, 60, 0xbeef0007U);
    return bytes;
}

void raw_fields_and_low_words() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 3, 2, -2, 4, 9, false, 0, 1, 88);
    bytes[88] = 1;
    bytes[89] = 9;
    bytes[90] = 2;
    bytes[91] = 3;
    bytes[92] = 4;
    bytes[93] = 5;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "raw GAF did not parse");
    require(
        result.archive->raw_sequence_count == 0xabcd0001U &&
            result.archive->header_sequence_count == 1 && result.archive->sequences.size() == 1,
        "signed-low16 sequence count was not preserved"
    );
    const auto& frame = result.archive->sequences[0].frames[0];
    require(
        result.archive->sequences[0].name == "TEST" && frame.duration == 7 &&
            frame.origin_x == -2 && frame.origin_y == 4,
        "sequence name, duration low word, or signed origins disagreed"
    );
    require(
        frame.layer_count == 0 && frame.special_render_flag == 1 &&
            frame.pixels == std::vector<uint8_t>({1, 9, 2, 3, 4, 5}),
        "separate layer-count and special-render bytes or raw pixels disagreed"
    );
    const auto rendered = gaf::render_normal(frame);
    require(
        rendered.ok() && rendered.frame->pixels == std::vector<uint8_t>({1, 9, 2, 3, 4, 5}),
        "top-level normal render incorrectly routed its own special flag"
    );
}

void compressed_commands() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 6, 1, 0, 0, 0, true, 0, 0, 88);
    // literal(2): 10,11; transparent(1); repeat(3): 7, covering six pixels.
    put16(bytes, 88, 6);
    bytes[90] = 4;
    bytes[91] = 10;
    bytes[92] = 11;
    bytes[93] = 3;
    bytes[94] = 10;
    bytes[95] = 7;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "compressed GAF did not parse");
    require(
        result.archive->sequences[0].frames[0].pixels == std::vector<uint8_t>({10, 11, 0, 7, 7, 7}),
        "row command decoding disagreed"
    );

    auto short_row = one_frame_file();
    frame_header(short_row, 64, 4, 1, 0, 0, 9, true, 0, 0, 88);
    put16(short_row, 88, 0); // fully-transparent-row encoding
    const auto short_result = gaf::parse(short_row);
    require(
        short_result.ok() && short_result.archive->sequences[0].frames[0].pixels ==
                                 std::vector<uint8_t>({9, 9, 9, 9}),
        "zero-length compressed row was not fully transparent"
    );

    auto clipped_run = one_frame_file();
    frame_header(clipped_run, 64, 2, 1, 0, 0, 0, true, 0, 0, 88);
    put16(clipped_run, 88, 2);
    clipped_run[90] = 14; // repeat four, clipped to the two-pixel destination
    clipped_run[91] = 5;
    const auto clipped_result = gaf::parse(clipped_run);
    require(
        clipped_result.ok() &&
            clipped_result.archive->sequences[0].frames[0].pixels == std::vector<uint8_t>({5, 5}),
        "compressed run was not clipped to the frame width"
    );
}

void layered_origins_clipping_and_special_route() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 4, 3, 2, 1, 0, false, 2, 0, 88);
    put32(bytes, 88, 96);
    put32(bytes, 92, 128);
    // Child 0 lands at (-1,0), one column past the frame. Index 0 is transparent.
    frame_header(bytes, 96, 3, 2, 3, 1, 0, false, 0, 0, 160);
    bytes[160] = 1;
    bytes[161] = 2;
    bytes[162] = 3;
    bytes[163] = 4;
    bytes[164] = 0;
    bytes[165] = 6;
    // Child 1 lands at (2,1) over child 0.
    frame_header(bytes, 128, 2, 2, 0, 0, 9, false, 0, 0, 166);
    bytes[166] = 7;
    bytes[167] = 9;
    bytes[168] = 8;
    bytes[169] = 5;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "layered GAF did not parse");
    const auto& frame = result.archive->sequences[0].frames[0];
    require(
        frame.layer_count == 2 && frame.special_render_flag == 0 && frame.layers.size() == 2,
        "byte-sized layer model disagreed"
    );
    const auto rendered = gaf::render_normal(frame);
    require(rendered.ok(), rendered.error ? rendered.error->message : "layer render failed");
    // The canvas grows one column to the left and the origin moves with it,
    // so child 0's first column, which used to fall outside, is kept.
    require(
        rendered.frame->width == 5 && rendered.frame->height == 3 &&
            rendered.frame->origin_x == 3 && rendered.frame->origin_y == 1 &&
            rendered.frame->pixels ==
                std::vector<uint8_t>({1, 2, 3, 0, 0, 4, 0, 6, 7, 0, 0, 0, 0, 8, 5}) &&
            rendered.frame->coverage ==
                std::vector<uint8_t>({1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1, 1}),
        "layer origin subtraction, transparency, or the whole child disagreed"
    );
    // Drawing at a point P puts child 0's pixel (0, 0) at P minus that
    // child's origin: one column left of the hotspot and on its row.
    const int held_x = rendered.frame->origin_x + 0 - frame.layers[0].origin_x;
    const int held_y = rendered.frame->origin_y + 0 - frame.layers[0].origin_y;
    require(
        held_x == 0 && held_y == 0 && rendered.frame->pixels[0] == 1,
        "a layer past the frame was not kept at its origin"
    );

    auto special = frame;
    special.layers[1].special_render_flag = 1;
    const auto rejected = gaf::render_normal(special);
    require(
        !rejected.ok() && rejected.error->code == gaf::ErrorCode::unsupported_special_render,
        "special-render child route was not explicit"
    );
}

void compressed_literal_transparency_index_is_still_written() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 3, 1, 0, 0, 0, false, 2, 0, 88);
    put32(bytes, 88, 96);
    put32(bytes, 92, 128);
    frame_header(bytes, 96, 3, 1, 0, 0, 0, false, 0, 0, 152);
    bytes[152] = 7;
    bytes[153] = 7;
    bytes[154] = 7;
    frame_header(bytes, 128, 3, 1, 0, 0, 9, true, 0, 0, 155);
    put16(bytes, 155, 5);
    bytes[157] = 0; // one literal pixel
    bytes[158] = 9; // equal to metadata transparency index, but still written
    bytes[159] = 3; // skip one, retaining the lower layer
    bytes[160] = 6; // repeat two (clipped to one remaining destination pixel)
    bytes[161] = 9;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "coverage GAF did not parse");
    const auto& literal = result.archive->sequences[0].frames[0].layers[1];
    require(
        literal.pixels == std::vector<uint8_t>({9, 9, 9}) &&
            literal.coverage == std::vector<uint8_t>({1, 0, 1}),
        "compressed literal/repeat/skip coverage was lost"
    );
    const auto rendered = gaf::render_normal(result.archive->sequences[0].frames[0]);
    require(
        rendered.ok() && rendered.frame->pixels == std::vector<uint8_t>({9, 7, 9}),
        "compressed transparency-index writes or skip did not preserve lower layer"
    );
    require(
        rendered.frame->coverage == std::vector<uint8_t>({1, 1, 1}),
        "render output discarded compressed literal transparency coverage"
    );

    const auto isolated = gaf::render_normal(literal);
    require(
        isolated.ok() && isolated.frame->pixels == std::vector<uint8_t>({9, 9, 9}) &&
            isolated.frame->coverage == std::vector<uint8_t>({1, 0, 1}),
        "render output cannot distinguish literal transparency-index writes from skips"
    );
}

void frame_at_rejects_out_of_range_indices() {
    std::vector<uint8_t> bytes(160);
    put32(bytes, 0, 0x00010100U);
    put32(bytes, 4, 1);
    put32(bytes, 12, 16);
    put16(bytes, 16, 2);
    put32(bytes, 56, 80);
    put32(bytes, 60, 3);
    put32(bytes, 64, 120);
    put32(bytes, 68, 9);
    frame_header(bytes, 80, 1, 1, 0, 0, 0, false, 0, 0, 104);
    bytes[104] = 11;
    frame_header(bytes, 120, 1, 1, 1, 2, 4, false, 0, 0, 144);
    bytes[144] = 22;

    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "two-frame GAF did not parse");
    const auto* sequence = &result.archive->sequences[0];
    require(
        gaf::frame_at(nullptr, -1) == nullptr, "negative index on a null sequence was not null"
    );
    require(
        gaf::frame_at(nullptr, 0) == nullptr, "nonnegative index on a null sequence was not null"
    );
    require(gaf::frame_at(sequence, -1) == nullptr, "negative frame_at index was accepted");
    require(gaf::frame_at(sequence, 2) == nullptr, "index equal to the frame count was accepted");
    require(gaf::frame_at(sequence, 0x7fffffff) == nullptr, "large frame_at index was accepted");
    const auto* first = gaf::frame_at(sequence, 0);
    const auto* second = gaf::frame_at(sequence, 1);
    require(
        first == &sequence->frames[0] && first->duration == 3 &&
            first->pixels == std::vector<uint8_t>({11}),
        "frame_at did not return the first frame-list entry"
    );
    require(
        second == &sequence->frames[1] && second->origin_x == 1 && second->origin_y == 2 &&
            second->pixels == std::vector<uint8_t>({22}),
        "frame_at did not return the second frame-list entry"
    );

    auto empty_bytes = one_frame_file();
    put16(empty_bytes, 16, 0);
    const auto empty = gaf::parse(empty_bytes);
    require(
        empty.ok() && gaf::frame_at(&empty.archive->sequences[0], 0) == nullptr,
        "zero frame count did not reject index 0"
    );
}

void malformed_and_bounded_inputs() {
    require(!gaf::parse(std::vector<uint8_t>(11)).ok(), "truncated header was accepted");

    auto cycle = one_frame_file();
    frame_header(cycle, 64, 1, 1, 0, 0, 0, false, 1, 0, 88);
    put32(cycle, 88, 64);
    const auto cycle_result = gaf::parse(cycle);
    require(
        !cycle_result.ok() && cycle_result.error->code == gaf::ErrorCode::pointer_cycle,
        "layer pointer cycle was not rejected"
    );

    auto too_large = one_frame_file();
    frame_header(too_large, 64, 65535, 65535, 0, 0, 0, false, 0, 0, 88);
    const auto large_result = gaf::parse(too_large);
    require(
        !large_result.ok() && large_result.error->code == gaf::ErrorCode::pixel_limit,
        "pathological frame allocation was not bounded"
    );

    auto bad_run = one_frame_file();
    frame_header(bad_run, 64, 2, 1, 0, 0, 0, true, 0, 0, 88);
    put16(bad_run, 88, 1);
    bad_run[90] = 14; // repeat command without its required value byte
    const auto run_result = gaf::parse(bad_run);
    require(
        !run_result.ok() && run_result.error->code == gaf::ErrorCode::malformed_compression,
        "oversized compressed run was accepted"
    );

    auto negative_count = std::vector<uint8_t>(12);
    put32(negative_count, 4, 0x0000ffffU);
    const auto negative = gaf::parse(negative_count);
    require(
        negative.ok() && negative.archive->header_sequence_count == -1 &&
            negative.archive->sequences.empty(),
        "signed sequence-count loop behavior disagreed"
    );
}

// A frame's size is refused before anything of it is decoded: past 4096 by
// 4096 pixels, or past the side a caller allows; a frame whose rows are not
// in the file fails before its buffers are made.
void frame_bounds_come_before_decoding() {
    auto wide = one_frame_file();
    frame_header(wide, 64, 4097, 4096, 0, 0, 0, true, 0, 0, 88);
    const auto wide_result = gaf::parse(wide);
    require(
        !wide_result.ok() && wide_result.error->code == gaf::ErrorCode::pixel_limit,
        "a frame over 4096 by 4096 pixels was accepted"
    );

    auto glyph = one_frame_file();
    glyph.resize(88 + 129);
    frame_header(glyph, 64, 129, 1, 0, 0, 0, false, 0, 0, 88);
    const auto glyph_result = gaf::parse(glyph, gaf::PixelData::decoded, 128);
    require(
        !glyph_result.ok() && glyph_result.error->code == gaf::ErrorCode::side_limit &&
            glyph_result.error->offset == 64,
        "a frame wider than the caller allows was accepted"
    );
    frame_header(glyph, 64, 128, 1, 0, 0, 0, false, 0, 0, 88);
    require(
        gaf::parse(glyph, gaf::PixelData::decoded, 128).ok(), "a frame within the side was refused"
    );

    // 4096 rows need 8192 bytes of row lengths; the file holds 168.
    auto rowless = one_frame_file();
    frame_header(rowless, 64, 4096, 4096, 0, 0, 0, true, 0, 0, 88);
    const auto rowless_result = gaf::parse(rowless);
    require(
        !rowless_result.ok() && rowless_result.error->code == gaf::ErrorCode::truncated,
        "a frame whose rows lie past the file was accepted"
    );
}

// Nine 4096 x 4096 frames decode to 288 MiB of pixels and coverage, past
// what a parse may keep; a checked parse keeps none of them and loads the file.
void checked_parse_keeps_no_decoded_total() {
    constexpr std::size_t frames = 9;
    constexpr uint16_t side = 4096;
    constexpr std::size_t frame_at = 56 + frames * 8;
    constexpr std::size_t rows_at = frame_at + 24;
    auto bytes = one_frame_file();
    bytes.resize(rows_at + std::size_t{side} * 2);
    put16(bytes, 16, frames);
    for (std::size_t index = 0; index < frames; ++index) {
        put32(bytes, 56 + index * 8, static_cast<uint32_t>(frame_at));
        put32(bytes, 60 + index * 8, 1);
    }
    // Every row is empty, which leaves it transparent.
    frame_header(bytes, frame_at, side, side, 0, 0, 0, true, 0, 0, rows_at);
    const auto checked = gaf::parse(bytes, gaf::PixelData::checked);
    require(
        checked.ok() && checked.archive->sequences[0].frames.size() == frames &&
            checked.archive->sequences[0].frames[0].pixels.empty(),
        checked.error ? checked.error->message : "a checked parse kept pixels"
    );
}

/// The file a ranged render reads, the reads it made, and whether they fail.
struct FileReads {
    const std::vector<uint8_t>* file{};
    std::size_t reads{};
    bool fail{};
};

/// Copies bytes of the file a FileReads names; past its end, or when told
/// to fail, reads nothing.
bool read_file(void* context, uint32_t offset, std::span<uint8_t> output) {
    auto& reads = *static_cast<FileReads*>(context);
    ++reads.reads;
    const auto& file = *reads.file;
    if (reads.fail || offset > file.size() || output.size() > file.size() - offset)
        return false;
    std::copy_n(file.begin() + offset, output.size(), output.begin());
    return true;
}

/// Checks that the first frame of a file renders from its byte ranges as it
/// renders decoded, reading the file `expected_reads` times.
void require_ranged_render_matches(
    const std::vector<uint8_t>& bytes, std::size_t expected_reads, std::string_view message
) {
    const auto decoded = gaf::parse(bytes);
    const auto checked = gaf::parse(bytes, gaf::PixelData::checked);
    require(decoded.ok() && checked.ok(), message);
    const auto& frame = checked.archive->sequences[0].frames[0];
    FileReads reads{&bytes};
    const auto ranged = gaf::render_ranged(frame, {&reads, read_file});
    const auto normal = gaf::render_normal(decoded.archive->sequences[0].frames[0]);
    require(ranged.ok() && normal.ok(), message);
    require(
        ranged.frame->width == normal.frame->width &&
            ranged.frame->height == normal.frame->height &&
            ranged.frame->origin_x == normal.frame->origin_x &&
            ranged.frame->origin_y == normal.frame->origin_y &&
            ranged.frame->transparency_index == normal.frame->transparency_index &&
            ranged.frame->pixels == normal.frame->pixels &&
            ranged.frame->coverage == normal.frame->coverage,
        message
    );
    require(reads.reads == expected_reads, message);
}

// A raw frame: its pixel data is width * height bytes where its record
// points, and it renders from them as decoded, the transparency index
// left uncovered.
void ranged_raw_frame() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 3, 2, -2, 4, 9, false, 0, 0, 88);
    for (std::size_t index = 0; index < 6; ++index)
        bytes[88 + index] = static_cast<uint8_t>(index == 1 ? 9 : index + 1);
    const auto checked = gaf::parse(bytes, gaf::PixelData::checked);
    require(checked.ok(), "a raw frame did not parse");
    const auto& frame = checked.archive->sequences[0].frames[0];
    require(
        frame.pixel_data_offset == 88 && frame.pixel_data_bytes == 6 && frame.pixels.empty(),
        "a raw frame's pixel data range disagreed"
    );
    require_ranged_render_matches(bytes, 1, "a raw frame rendered from its range disagreed");
}

// A compressed frame: its pixel data is every row with its length word, an
// empty row included.
void ranged_compressed_frame() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 6, 2, 1, 1, 0, true, 0, 0, 88);
    // literal(2): 10,11; transparent(1); repeat(3): 7; then an empty row.
    put16(bytes, 88, 6);
    bytes[90] = 4;
    bytes[91] = 10;
    bytes[92] = 11;
    bytes[93] = 3;
    bytes[94] = 10;
    bytes[95] = 7;
    put16(bytes, 96, 0);
    const auto checked = gaf::parse(bytes, gaf::PixelData::checked);
    require(checked.ok(), "a compressed frame did not parse");
    const auto& frame = checked.archive->sequences[0].frames[0];
    require(
        frame.pixel_data_offset == 88 && frame.pixel_data_bytes == 10,
        "a compressed frame's pixel data range disagreed"
    );
    require_ranged_render_matches(bytes, 1, "a compressed frame rendered from its range disagreed");

    // The rows read when the frame is drawn are decoded as parse decodes
    // them: a literal run past the row's bytes is refused where it starts
    // in the file.
    auto changed = bytes;
    changed[90] = 0xfc;
    FileReads reads{&changed};
    const auto refused = gaf::render_ranged(frame, {&reads, read_file});
    require(
        !refused.ok() && refused.error->code == gaf::ErrorCode::malformed_compression &&
            refused.error->offset == 90,
        "rows that no longer decode were drawn"
    );
}

// A frame of two layers, each read on its own: raw layers placed by their
// origins, including a column past the frame, and a raw layer under a
// compressed one whose literal of the transparency index still covers it.
void ranged_layered_frame() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 4, 3, 2, 1, 0, false, 2, 0, 88);
    put32(bytes, 88, 96);
    put32(bytes, 92, 128);
    frame_header(bytes, 96, 3, 2, 3, 1, 0, false, 0, 0, 160);
    for (std::size_t index = 0; index < 6; ++index)
        bytes[160 + index] = static_cast<uint8_t>(index == 4 ? 0 : index + 1);
    frame_header(bytes, 128, 2, 2, 0, 0, 9, false, 0, 0, 166);
    bytes[166] = 7;
    bytes[167] = 9;
    bytes[168] = 8;
    bytes[169] = 5;
    require_ranged_render_matches(bytes, 2, "raw layers rendered from their ranges disagreed");

    auto mixed = one_frame_file();
    frame_header(mixed, 64, 3, 1, 0, 0, 0, false, 2, 0, 88);
    put32(mixed, 88, 96);
    put32(mixed, 92, 128);
    frame_header(mixed, 96, 3, 1, 0, 0, 0, false, 0, 0, 152);
    mixed[152] = 7;
    mixed[153] = 7;
    mixed[154] = 7;
    frame_header(mixed, 128, 3, 1, 0, 0, 9, true, 0, 0, 155);
    put16(mixed, 155, 5);
    mixed[157] = 0;
    mixed[158] = 9;
    mixed[159] = 3;
    mixed[160] = 6;
    mixed[161] = 9;
    require_ranged_render_matches(
        mixed, 2, "a raw and a compressed layer rendered from their ranges disagreed"
    );

    const auto checked = gaf::parse(mixed, gaf::PixelData::checked);
    require(
        checked.ok() && gaf::decoded_bytes(checked.archive->sequences[0].frames[0]) == 12 &&
            gaf::decoded_bytes(*checked.archive) == 12,
        "the decoded bytes of a layered frame disagreed"
    );
}

// A read that fails, or no read at all, renders nothing.
void ranged_failed_read() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 2, 1, 0, 0, 0, false, 0, 0, 88);
    bytes[88] = 3;
    bytes[89] = 4;
    const auto checked = gaf::parse(bytes, gaf::PixelData::checked);
    require(checked.ok(), "a raw frame did not parse");
    const auto& frame = checked.archive->sequences[0].frames[0];
    FileReads reads{&bytes};
    reads.fail = true;
    const auto failed = gaf::render_ranged(frame, {&reads, read_file});
    require(
        !failed.ok() && failed.error->code == gaf::ErrorCode::unreadable &&
            failed.error->offset == 88 && reads.reads == 1,
        "a frame whose read failed was rendered"
    );
    const auto unread = gaf::render_ranged(frame, gaf::ReadHooks{});
    require(
        !unread.ok() && unread.error->code == gaf::ErrorCode::unreadable,
        "a frame with no read was rendered"
    );
}

/// Returns a simple frame filled with `pixels`. A pixel equal to `key` is uncovered.
gaf::Frame simple_layer(
    uint16_t width,
    uint16_t height,
    int16_t origin_x,
    int16_t origin_y,
    uint8_t key,
    std::vector<uint8_t> pixels
) {
    gaf::Frame frame;
    frame.width = width;
    frame.height = height;
    frame.origin_x = origin_x;
    frame.origin_y = origin_y;
    frame.transparency_index = key;
    frame.pixels = std::move(pixels);
    frame.coverage.assign(frame.pixels.size(), 1);
    for (std::size_t index = 0; index < frame.pixels.size(); ++index)
        if (frame.pixels[index] == key)
            frame.coverage[index] = 0;
    return frame;
}

/// Returns a composite frame holding `layers`.
gaf::Frame composite_layer(
    uint16_t width,
    uint16_t height,
    int16_t origin_x,
    int16_t origin_y,
    uint8_t key,
    std::vector<gaf::Frame> layers
) {
    gaf::Frame frame;
    frame.width = width;
    frame.height = height;
    frame.origin_x = origin_x;
    frame.origin_y = origin_y;
    frame.transparency_index = key;
    frame.layer_count = static_cast<uint8_t>(layers.size());
    frame.layers = std::move(layers);
    return frame;
}

/// Paints `source` onto a canvas of the frame's own size, dropping every row
/// and column outside it: what a fitted composite rendered as before the
/// canvas grew to hold layers that reach past the frame.
void paint_inside_frame(
    const gaf::Frame& source,
    std::vector<uint8_t>& pixels,
    std::vector<uint8_t>& coverage,
    int32_t left,
    int32_t top,
    uint16_t dest_w,
    uint16_t dest_h
) {
    if (!source.layers.empty()) {
        for (const auto& layer : source.layers) {
            paint_inside_frame(
                layer,
                pixels,
                coverage,
                left + static_cast<int32_t>(source.origin_x) - static_cast<int32_t>(layer.origin_x),
                top + static_cast<int32_t>(source.origin_y) - static_cast<int32_t>(layer.origin_y),
                dest_w,
                dest_h
            );
        }
        return;
    }
    const int64_t first_x = std::max<int64_t>(0, -static_cast<int64_t>(left));
    const int64_t end_x =
        std::min<int64_t>(source.width, static_cast<int64_t>(dest_w) - static_cast<int64_t>(left));
    for (int32_t y = 0; y < static_cast<int32_t>(source.height); ++y) {
        const auto destination_y = top + y;
        if (destination_y < 0 || destination_y >= static_cast<int32_t>(dest_h))
            continue;
        const std::size_t source_row = static_cast<std::size_t>(y) * source.width;
        const std::size_t destination_row = static_cast<std::size_t>(destination_y) * dest_w;
        for (int64_t x = first_x; x < end_x; ++x) {
            const std::size_t source_index = source_row + static_cast<std::size_t>(x);
            if (source.coverage[source_index] == 0)
                continue;
            const std::size_t destination_index =
                destination_row + static_cast<std::size_t>(left + x);
            pixels[destination_index] = source.pixels[source_index];
            coverage[destination_index] = 1;
        }
    }
}

/// A composite whose layers sit inside its rectangle keeps that rectangle.
void require_fitted(const gaf::Frame& frame, std::string_view message) {
    const auto rendered = gaf::render_normal(frame);
    require(rendered.ok(), message);
    require(
        rendered.frame->width == frame.width && rendered.frame->height == frame.height &&
            rendered.frame->origin_x == frame.origin_x &&
            rendered.frame->origin_y == frame.origin_y,
        message
    );
    const std::size_t count = static_cast<std::size_t>(frame.width) * frame.height;
    std::vector<uint8_t> pixels(count, frame.transparency_index);
    std::vector<uint8_t> coverage(count, 0);
    paint_inside_frame(frame, pixels, coverage, 0, 0, frame.width, frame.height);
    require(rendered.frame->pixels == pixels && rendered.frame->coverage == coverage, message);
}

/// Every composite whose layers fit the frame, the shape shipped frames have,
/// renders the same size, origin and pixels as a canvas clipped to the frame.
void fitted_layers_match_the_frame() {
    // Two layers overlap inside a 4 by 3 frame. The later layer's transparent
    // index does not cover the earlier pixel under it.
    const auto overlapped = composite_layer(
        4,
        3,
        5,
        6,
        0,
        {simple_layer(3, 2, 4, 5, 0, {1, 2, 3, 4, 0, 6}), simple_layer(2, 2, 4, 5, 9, {7, 9, 8, 5})}
    );
    require_fitted(overlapped, "overlapping layers inside the frame changed it");
    const auto overlapped_drawn = gaf::render_normal(overlapped);
    require(
        overlapped_drawn.ok() && overlapped_drawn.frame->pixels ==
                                     std::vector<uint8_t>({0, 0, 0, 0, 0, 7, 2, 3, 0, 8, 5, 6}),
        "overlapping layers inside the frame drew the wrong pixels"
    );

    // A layer the same size as the frame, placed on its edges.
    require_fitted(
        composite_layer(3, 2, 1, -2, 0, {simple_layer(3, 2, 1, -2, 0, {1, 2, 3, 4, 5, 6})}),
        "a layer flush with the frame changed it"
    );
    // A layer whose origin is left of the frame's still lands inside it.
    require_fitted(
        composite_layer(4, 3, 1, 1, 0, {simple_layer(2, 2, -1, 0, 0, {8, 8, 8, 8})}),
        "a layer placed inside by a smaller origin changed the frame"
    );
    // A nested layer, itself a composite, stays inside the frame and the
    // frame between them.
    const auto nested = composite_layer(
        5, 4, 2, 2, 0, {composite_layer(3, 3, 2, 2, 0, {simple_layer(2, 2, 1, 1, 0, {8, 8, 8, 8})})}
    );
    require_fitted(nested, "a nested layer inside the frame changed it");
    const auto nested_drawn = gaf::render_normal(nested);
    require(
        nested_drawn.ok() &&
            nested_drawn.frame->pixels ==
                std::vector<uint8_t>({0, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0}),
        "a nested layer inside the frame drew the wrong pixels"
    );
}

/// A layer that reaches past the frame on every side is drawn whole, at the
/// same place relative to the origin as drawing that layer at the frame's point.
void layer_past_every_side_is_drawn_whole() {
    std::vector<uint8_t> pixels(36);
    for (std::size_t index = 0; index < pixels.size(); ++index)
        pixels[index] = static_cast<uint8_t>(index + 1);
    const auto child = simple_layer(6, 6, 3, 3, 0, pixels);
    const auto frame = composite_layer(4, 4, 2, 2, 0, {child});
    const auto rendered = gaf::render_normal(frame);
    require(rendered.ok(), rendered.error ? rendered.error->message : "wide layer did not render");
    require(
        rendered.frame->width == 6 && rendered.frame->height == 6 &&
            rendered.frame->origin_x == 3 && rendered.frame->origin_y == 3,
        "a layer past every side did not grow the frame and its origin together"
    );
    const auto ranged = gaf::render_ranged(frame, gaf::ReadHooks{});
    require(
        ranged.ok() && ranged.frame->width == rendered.frame->width &&
            ranged.frame->height == rendered.frame->height &&
            ranged.frame->origin_x == rendered.frame->origin_x &&
            ranged.frame->origin_y == rendered.frame->origin_y &&
            ranged.frame->pixels == rendered.frame->pixels &&
            ranged.frame->coverage == rendered.frame->coverage,
        "a layer past every side rendered differently from its byte range"
    );
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 6; ++column) {
            const int canvas_x = rendered.frame->origin_x + column - child.origin_x;
            const int canvas_y = rendered.frame->origin_y + row - child.origin_y;
            require(
                canvas_x >= 0 && canvas_y >= 0 && canvas_x < rendered.frame->width &&
                    canvas_y < rendered.frame->height,
                "part of a layer past every side fell outside the rendered frame"
            );
            const auto at = static_cast<std::size_t>(canvas_y) * rendered.frame->width +
                            static_cast<std::size_t>(canvas_x);
            const auto source =
                static_cast<std::size_t>(row) * child.width + static_cast<std::size_t>(column);
            require(
                rendered.frame->pixels[at] == child.pixels[source] &&
                    rendered.frame->coverage[at] == 1,
                "a layer past every side was not drawn at its origin"
            );
        }
    }

    // A nested layer past the left edge, through a composite that itself fits.
    const auto grandchild = simple_layer(2, 2, 4, 3, 0, {5, 6, 7, 8});
    const auto through =
        composite_layer(6, 6, 3, 3, 0, {composite_layer(4, 4, 3, 3, 0, {grandchild})});
    const auto through_drawn = gaf::render_normal(through);
    require(through_drawn.ok(), "a nested layer past the frame did not render");
    require(
        through_drawn.frame->width == 7 && through_drawn.frame->height == 6 &&
            through_drawn.frame->origin_x == 4 && through_drawn.frame->origin_y == 3,
        "a nested layer past the frame did not move only the origin it grew past"
    );
    const int nest_x = through_drawn.frame->origin_x + 0 - grandchild.origin_x;
    const int nest_y = through_drawn.frame->origin_y + 0 - grandchild.origin_y;
    require(
        nest_x == 0 && nest_y == 0 &&
            through_drawn.frame->pixels
                    [static_cast<std::size_t>(nest_y) * 7 + static_cast<std::size_t>(nest_x)] == 5,
        "a nested layer past the frame was not drawn at its origin"
    );
}

/// A canvas that would pass the frame's pixel limit is refused before it is allocated.
void rendered_frame_keeps_the_pixel_limit() {
    gaf::Frame wide;
    wide.width = 4096;
    wide.height = 4096;
    gaf::Frame shifted = wide;
    shifted.origin_x = -1;
    const auto frame = composite_layer(4096, 4096, 0, 0, 0, {wide, shifted});
    const auto rendered = gaf::render_normal(frame);
    require(
        !rendered.ok() && rendered.error->code == gaf::ErrorCode::pixel_limit,
        "a canvas past the frame pixel limit was drawn"
    );
}

} // namespace

int main() {
    try {
        raw_fields_and_low_words();
        compressed_commands();
        layered_origins_clipping_and_special_route();
        fitted_layers_match_the_frame();
        layer_past_every_side_is_drawn_whole();
        rendered_frame_keeps_the_pixel_limit();
        compressed_literal_transparency_index_is_still_written();
        frame_at_rejects_out_of_range_indices();
        malformed_and_bounded_inputs();
        frame_bounds_come_before_decoding();
        checked_parse_keeps_no_decoded_total();
        ranged_raw_frame();
        ranged_compressed_frame();
        ranged_layered_frame();
        ranged_failed_read();
    } catch (const std::exception& error) {
        std::cerr << "sprite-format test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sprite-format tests passed\n";
    return 0;
}
