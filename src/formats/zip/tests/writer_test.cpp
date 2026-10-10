// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The streaming zip writer: stored and deflated entries read back, a pinned
// stored archive, the 64-bit extension including one entry past 4 GiB, every
// refusal, and a thousand entries under a fixed seed.

#include "oa/base/sha256.hpp"
#include "oa/formats/zip.hpp"
#include "oa/formats/zip/stream.hpp"
#include "oa/formats/zip/writer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace oa::formats::zip;

/// Reads a little-endian 16-bit field.
///
/// @param bytes the first of two bytes
/// @return the value
uint16_t load_le16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (uint16_t{bytes[1]} << 8U));
}

/// Reads a little-endian 32-bit field.
///
/// @param bytes the first of four bytes
/// @return the value
uint32_t load_le32(const uint8_t* bytes) {
    return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8U) | (uint32_t{bytes[2]} << 16U) |
           (uint32_t{bytes[3]} << 24U);
}

int g_failures = 0;

/// Reports a failed check with its line.
///
/// @param condition the checked condition
/// @param what the condition's source text
/// @param line the line of the check
void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// One entry the tests ask the writer for.
struct Item {
    std::string name{};
    std::vector<uint8_t> bytes{};
    Method method{Method::stored};
    bool folder{false};
};

/// An archive gathered in memory by the write hook.
struct VectorOut {
    std::vector<uint8_t> bytes{};
    int fail_at{-1};
    int calls{0};
};

/// Writes into a VectorOut, failing once `calls` reaches `fail_at`.
///
/// @param context the VectorOut
/// @param offset where the bytes go
/// @param bytes the bytes
/// @return false when the hook was asked to fail, or the write leaves a gap
bool write_vector(void* context, uint64_t offset, std::span<const uint8_t> bytes) {
    auto& out = *static_cast<VectorOut*>(context);
    if (out.fail_at >= 0 && out.calls >= out.fail_at)
        return false;
    ++out.calls;
    if (offset > out.bytes.size() || bytes.size() > UINT64_MAX - offset)
        return false;
    const size_t at = static_cast<size_t>(offset);
    if (out.bytes.size() < at + bytes.size())
        out.bytes.resize(at + bytes.size());
    std::copy(bytes.begin(), bytes.end(), out.bytes.begin() + static_cast<std::ptrdiff_t>(at));
    return true;
}

/// Reads an archive held in a vector.
///
/// @param context the vector
/// @param offset where to read
/// @param out filled
/// @return true when the bytes lie in the archive
bool read_vector(void* context, uint64_t offset, std::span<uint8_t> out) {
    const auto& bytes = *static_cast<const std::vector<uint8_t>*>(context);
    if (offset > bytes.size() || bytes.size() - offset < out.size())
        return false;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    return true;
}

/// A sparse archive: bytes outside `[hole_begin, hole_end)` are kept, and
/// that range is read back as zeros.
struct SparseOut {
    uint64_t hole_begin{UINT64_MAX};
    uint64_t hole_end{0};
    uint64_t archive_bytes{0};

    struct Chunk {
        uint64_t offset{};
        std::vector<uint8_t> bytes{};
    };

    std::vector<Chunk> chunks{};
};

/// Writes into a SparseOut, dropping bytes that fall in the hole.
///
/// @param context the SparseOut
/// @param offset where the bytes go
/// @param bytes the bytes
/// @return true when the bytes outside the hole were kept
bool write_sparse(void* context, uint64_t offset, std::span<const uint8_t> bytes) {
    auto& out = *static_cast<SparseOut*>(context);
    size_t index = 0;
    while (index < bytes.size()) {
        const uint64_t at = offset + index;
        if (at >= out.hole_begin && at < out.hole_end) {
            const auto skip =
                static_cast<size_t>(std::min<uint64_t>(bytes.size() - index, out.hole_end - at));
            index += skip;
            continue;
        }
        uint64_t run = bytes.size() - index;
        if (at < out.hole_begin)
            run = std::min<uint64_t>(run, out.hole_begin - at);
        const auto slice = bytes.subspan(index, static_cast<size_t>(run));
        if (!out.chunks.empty() &&
            out.chunks.back().offset + out.chunks.back().bytes.size() == at) {
            out.chunks.back().bytes.insert(
                out.chunks.back().bytes.end(), slice.begin(), slice.end()
            );
        } else if (
            !out.chunks.empty() && at >= out.chunks.back().offset &&
            at <= out.chunks.back().offset + out.chunks.back().bytes.size()
        ) {
            SparseOut::Chunk& chunk = out.chunks.back();
            const size_t local = static_cast<size_t>(at - chunk.offset);
            if (chunk.bytes.size() < local + slice.size())
                chunk.bytes.resize(local + slice.size());
            std::copy(
                slice.begin(), slice.end(), chunk.bytes.begin() + static_cast<std::ptrdiff_t>(local)
            );
        } else {
            out.chunks.push_back(SparseOut::Chunk{at, {slice.begin(), slice.end()}});
        }
        index += static_cast<size_t>(run);
    }
    return true;
}

/// Reads a SparseOut: zeros inside the hole, kept bytes outside it.
///
/// @param context the SparseOut
/// @param offset where to read
/// @param out filled
/// @return true when every byte was in the archive
bool read_sparse(void* context, uint64_t offset, std::span<uint8_t> out) {
    const auto& source = *static_cast<const SparseOut*>(context);
    if (offset > source.archive_bytes || source.archive_bytes - offset < out.size())
        return false;
    size_t index = 0;
    while (index < out.size()) {
        const uint64_t at = offset + index;
        if (at >= source.hole_begin && at < source.hole_end) {
            const auto count =
                static_cast<size_t>(std::min<uint64_t>(out.size() - index, source.hole_end - at));
            std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(index), count, uint8_t{0});
            index += count;
            continue;
        }
        const SparseOut::Chunk* found = nullptr;
        for (const SparseOut::Chunk& chunk : source.chunks) {
            if (at >= chunk.offset && at < chunk.offset + chunk.bytes.size()) {
                found = &chunk;
                break;
            }
        }
        if (found == nullptr)
            return false;
        const size_t local = static_cast<size_t>(at - found->offset);
        const size_t count = std::min(out.size() - index, found->bytes.size() - local);
        std::copy_n(
            found->bytes.begin() + static_cast<std::ptrdiff_t>(local),
            count,
            out.begin() + static_cast<std::ptrdiff_t>(index)
        );
        index += count;
    }
    return true;
}

/// Returns the bytes of a text.
///
/// @param text the text
/// @return its bytes
std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

/// Returns deterministic bytes.
///
/// @param size how many bytes
/// @param seed the generator's starting value
/// @return the bytes
std::vector<uint8_t> noise(size_t size, uint32_t seed) {
    std::vector<uint8_t> out(size);
    for (uint8_t& byte : out) {
        seed = seed * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(seed >> 16U);
    }
    return out;
}

/// Writes items with one writer and returns the archive.
///
/// @param items the entries, in order
/// @param options the writer's options
/// @param[out] error the status when the write fails
/// @param[out] archive the bytes; left empty on failure
/// @return true when the archive was finished
bool write_items(
    const std::vector<Item>& items,
    WriterOptions options,
    ZipError& error,
    std::vector<uint8_t>& archive
) {
    VectorOut out{};
    StreamWriter writer{{&out, write_vector}, options};
    for (const Item& item : items) {
        if (item.folder) {
            if (!writer.add_folder(item.name, error))
                return false;
            continue;
        }
        if (!writer.begin_entry(item.name, item.bytes.size(), item.method, error))
            return false;
        if (!item.bytes.empty() && !writer.write(item.bytes, error))
            return false;
        if (!writer.end_entry(error))
            return false;
    }
    if (!writer.finish(error))
        return false;
    if (writer.bytes_written() != out.bytes.size())
        return false;
    archive = std::move(out.bytes);
    return true;
}

/// Reads one streamed entry whole.
///
/// @param source where the archive's bytes come from
/// @param archive_bytes the archive's size
/// @param entry the entry
/// @param[out] bytes the data; left empty on failure
/// @param[out] error the status when the read fails
/// @return true when the data was read and checked
bool stream_whole(
    const SourceHooks& source,
    uint64_t archive_bytes,
    const StreamEntry& entry,
    std::vector<uint8_t>& bytes,
    ZipError& error
) {
    return read_stream_entry(source, archive_bytes, entry, entry.bytes, bytes, error);
}

/// Reads an archive with the streamed reader and checks it against items.
///
/// @param archive the archive
/// @param items the entries that were written
/// @param source the hooks; a vector hook when context is null
/// @param archive_bytes the size the hooks cover
/// @return true when every entry matched
bool expect_stream(
    const std::vector<Item>& items,
    const SourceHooks& source,
    uint64_t archive_bytes,
    const StreamLimits& limits
) {
    StreamDirectory directory{};
    ZipError error{};
    if (!read_stream_directory(source, archive_bytes, limits, directory, error)) {
        std::fprintf(
            stderr,
            "stream directory: %s (%s)\n",
            zip_status_message(error.status),
            error.entry.c_str()
        );
        return false;
    }
    if (directory.entries.size() != items.size())
        return false;
    for (size_t index = 0; index < items.size(); ++index) {
        const StreamEntry& entry = directory.entries[index];
        const Item& item = items[index];
        if (entry.name != item.name || entry.directory != item.folder)
            return false;
        if (entry.method != (item.folder ? Method::stored : item.method))
            return false;
        if (entry.bytes != item.bytes.size() || entry.crc32 != crc32_of(item.bytes))
            return false;
        std::vector<uint8_t> got{};
        if (!stream_whole(source, archive_bytes, entry, got, error))
            return false;
        if (got != item.bytes)
            return false;
        // The local header carries the real CRC-32, and not a data descriptor.
        std::array<uint8_t, 30> header{};
        if (!source.read_at(source.context, entry.local_header_offset, header))
            return false;
        if ((load_le16(header.data() + 6) & 0x8U) != 0)
            return false;
        if (load_le32(header.data() + 14) != entry.crc32)
            return false;
        const uint32_t compressed = load_le32(header.data() + 18);
        const uint32_t bytes = load_le32(header.data() + 22);
        // Both sizes are sentinels together, or both are the real values.
        const bool sentinel = compressed == 0xFFFFFFFFU || bytes == 0xFFFFFFFFU;
        if (sentinel) {
            if (compressed != 0xFFFFFFFFU || bytes != 0xFFFFFFFFU)
                return false;
        } else if (compressed != entry.compressed_bytes || bytes != entry.bytes) {
            return false;
        }
    }
    return true;
}

/// Reads a small archive with the in-memory reader and checks it.
///
/// @param archive the archive
/// @param items the entries that were written
/// @return true when every entry matched
bool expect_memory(std::span<const uint8_t> archive, const std::vector<Item>& items) {
    CentralDirectory directory{};
    ZipError error{};
    if (!read_directory(archive, directory, error) || directory.entries.size() != items.size())
        return false;
    for (size_t index = 0; index < items.size(); ++index) {
        const Entry& entry = directory.entries[index];
        const Item& item = items[index];
        if (entry.name != item.name || entry.directory != item.folder)
            return false;
        if (entry.crc32 != crc32_of(item.bytes) || entry.bytes != item.bytes.size())
            return false;
        std::vector<uint8_t> got{};
        if (!read_entry(archive, entry, got, error) || got != item.bytes)
            return false;
    }
    return true;
}

/// The fixed stored archive whose bytes are pinned.
///
/// @return its entries
std::vector<Item> pinned_items() {
    return {
        {"readme.txt", bytes_of("hello\n"), Method::stored, false},
        {"dir/", {}, Method::stored, true},
        {"dir/empty", {}, Method::stored, false},
    };
}

void test_stored_matches_memory_writer() {
    const std::vector<Item> items = pinned_items();
    ZipError error{};
    std::vector<uint8_t> archive{};
    CHECK(write_items(items, {}, error, archive));
    std::array<NewEntry, 3> entries{{
        {items[0].name, items[0].bytes},
        {items[1].name, items[1].bytes},
        {items[2].name, items[2].bytes},
    }};
    std::vector<uint8_t> memory{};
    CHECK(write_archive(entries, memory, error));
    CHECK(archive == memory);
    CHECK(expect_memory(archive, items));
    CHECK(expect_stream(items, {&archive, read_vector}, archive.size(), {}));
}

void test_round_trip() {
    const std::string accented = "caf\xc3\xa9.txt";
    const std::vector<Item> items{
        {"notes.txt", bytes_of("a note\n"), Method::stored, false},
        {"pack/", {}, Method::stored, true},
        {"pack/empty", {}, Method::deflated, false},
        {"pack/text.txt", bytes_of(std::string(4000, 'x') + "end\n"), Method::deflated, false},
        {"pack/noise.bin", noise(3000, 9), Method::deflated, false},
        {accented, bytes_of("\xc3\xa9\n"), Method::deflated, false},
    };
    ZipError error{};
    std::vector<uint8_t> archive{};
    CHECK(write_items(items, {}, error, archive));
    CHECK(expect_memory(archive, items));
    CHECK(expect_stream(items, {&archive, read_vector}, archive.size(), {}));
    // Only the name that leaves 7-bit ASCII carries the UTF-8 flag.
    StreamDirectory directory{};
    CHECK(read_stream_directory({&archive, read_vector}, archive.size(), {}, directory, error));
    if (directory.entries.size() == items.size()) {
        std::array<uint8_t, 8> flags{};
        const uint64_t ascii_at = directory.entries[0].local_header_offset + 6;
        const uint64_t utf8_at = directory.entries[5].local_header_offset + 6;
        CHECK(read_vector(&archive, ascii_at, std::span<uint8_t>{flags.data(), 2}));
        CHECK(load_le16(flags.data()) == 0);
        CHECK(read_vector(&archive, utf8_at, std::span<uint8_t>{flags.data(), 2}));
        CHECK(load_le16(flags.data()) == (1U << 11U));
    }
}

void test_deflate_identical() {
    const std::vector<Item> items{
        {"a.txt", bytes_of(std::string(8000, 'a')), Method::deflated, false},
        {"b.bin", noise(5000, 3), Method::deflated, false},
        {"c/", {}, Method::stored, true},
    };
    ZipError error{};
    std::vector<uint8_t> first{};
    std::vector<uint8_t> second{};
    CHECK(write_items(items, {}, error, first));
    CHECK(write_items(items, {}, error, second));
    CHECK(first == second);
    CHECK(!first.empty());
}

void test_pinned_stored() {
    ZipError error{};
    std::vector<uint8_t> archive{};
    CHECK(write_items(pinned_items(), {}, error, archive));
    const auto digest = oa::base::sha256::to_hex(oa::base::sha256::digest_of(archive));
    const std::string_view hex{digest.data(), digest.size()};
    // Pinned after unzip -t and Python's zipfile.ZipFile(...).testzip() both
    // accepted this stored archive. The pin is the writer's own output.
    constexpr std::string_view expected =
        "7371889c1d484c9cbdfb10583c68adad03855afc7476ee5cb47a665fd0528a99";
    if (hex != expected)
        std::fprintf(stderr, "stored sha256 %.*s\n", static_cast<int>(hex.size()), hex.data());
    CHECK(hex == expected);
}

void test_zip64_always() {
    const std::vector<Item> items{
        {"one.txt", bytes_of("one\n"), Method::stored, false},
        {"two.txt", bytes_of(std::string(200, 'z')), Method::deflated, false},
    };
    ZipError error{};
    std::vector<uint8_t> archive{};
    CHECK(write_items(items, WriterOptions{9, Zip64::always}, error, archive));
    CentralDirectory memory{};
    CHECK(!read_directory(archive, memory, error));
    CHECK(error.status == ZipStatus::zip64);
    StreamDirectory directory{};
    CHECK(read_stream_directory({&archive, read_vector}, archive.size(), {}, directory, error));
    CHECK(directory.zip64);
    CHECK(expect_stream(items, {&archive, read_vector}, archive.size(), {}));
}

void test_past_four_gib() {
    constexpr uint64_t size = (uint64_t{4} << 30) + 16;
    SparseOut sparse{};
    StreamWriter writer{{&sparse, write_sparse}};
    ZipError error{};
    CHECK(writer.begin_entry("zeros.bin", size, Method::stored, error));
    sparse.hole_begin = writer.bytes_written();
    sparse.hole_end = sparse.hole_begin + size;
    std::vector<uint8_t> block(size_t{1} << 20, 0);
    for (uint64_t left = size; left > 0;) {
        const size_t piece = static_cast<size_t>(std::min<uint64_t>(left, block.size()));
        CHECK(writer.write(std::span<const uint8_t>{block.data(), piece}, error));
        left -= piece;
    }
    CHECK(writer.end_entry(error));
    const std::vector<uint8_t> tail = bytes_of("beside\n");
    CHECK(writer.begin_entry("tail.txt", tail.size(), Method::stored, error));
    CHECK(writer.write(tail, error));
    CHECK(writer.end_entry(error));
    CHECK(writer.finish(error));
    sparse.archive_bytes = writer.bytes_written();
    CHECK(sparse.archive_bytes > (uint64_t{4} << 30));
    size_t kept = 0;
    for (const SparseOut::Chunk& chunk : sparse.chunks)
        kept += chunk.bytes.size();
    CHECK(kept < (size_t{1} << 20));
    const SourceHooks source{&sparse, read_sparse};
    StreamLimits limits{};
    limits.max_entry_bytes = size;
    StreamDirectory directory{};
    CHECK(read_stream_directory(source, sparse.archive_bytes, limits, directory, error));
    CHECK(directory.zip64);
    CHECK(directory.entries.size() == 2);
    if (directory.entries.size() == 2) {
        CHECK(directory.entries[0].name == "zeros.bin");
        CHECK(directory.entries[0].bytes == size);
        CHECK(directory.entries[0].crc32 != 0);
        CHECK(directory.entries[1].name == "tail.txt");
        CHECK(directory.entries[1].local_header_offset >= (uint64_t{4} << 30));
        std::vector<uint8_t> got{};
        CHECK(stream_whole(source, sparse.archive_bytes, directory.entries[1], got, error));
        CHECK(got == tail);
        EntryStream stream{};
        CHECK(stream.open(source, sparse.archive_bytes, directory.entries[0], error));
        uint64_t seen = 0;
        const auto sink = [](void* context, std::span<const uint8_t> bytes) {
            auto* state = static_cast<std::pair<uint64_t, bool>*>(context);
            state->first += bytes.size();
            if (std::any_of(bytes.begin(), bytes.end(), [](uint8_t byte) { return byte != 0; }))
                state->second = false;
            return true;
        };
        std::pair<uint64_t, bool> state{0, true};
        SinkHooks hooks{&state, sink};
        while (seen < size) {
            const StreamStep step = stream.step(hooks, size_t{1} << 20, error);
            if (step == StreamStep::failed) {
                CHECK(false);
                break;
            }
            seen = stream.output_done();
            if (step == StreamStep::done)
                break;
        }
        CHECK(seen == size);
        CHECK(state.second);
        CHECK(state.first == size);
    }
}

void test_thousand() {
    constexpr int count = 1000;
    uint32_t seed = 0x0A08F15u;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    std::vector<Item> items{};
    items.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        char name[32]{};
        if (index % 17 == 0) {
            std::snprintf(name, sizeof name, "d/%04d/", index);
            items.push_back(Item{name, {}, Method::stored, true});
            continue;
        }
        std::snprintf(name, sizeof name, "f/%04d.txt", index);
        const size_t size = next() % 48;
        Item item{
            name, noise(size, next()), index % 3 == 0 ? Method::deflated : Method::stored, false
        };
        items.push_back(std::move(item));
    }
    ZipError error{};
    std::vector<uint8_t> archive{};
    CHECK(write_items(items, {}, error, archive));
    CHECK(expect_stream(items, {&archive, read_vector}, archive.size(), {}));
}

void test_refusals() {
    ZipError error{};
    VectorOut out{};
    StreamWriter writer{{&out, write_vector}};
    const auto refused = [&](bool ok, ZipStatus status) {
        CHECK(!ok);
        CHECK(error.status == status);
        CHECK(std::string_view{zip_status_message(status)} != "unknown zip status");
    };
    refused(writer.write(bytes_of("x"), error), ZipStatus::entry_open);
    refused(writer.end_entry(error), ZipStatus::entry_open);
    refused(writer.add_folder("folder", error), ZipStatus::unsafe_name);
    refused(writer.begin_entry("../no.txt", 1, Method::stored, error), ZipStatus::unsafe_name);
    refused(writer.begin_entry("/abs.txt", 1, Method::stored, error), ZipStatus::unsafe_name);
    refused(writer.begin_entry("a\\b.txt", 1, Method::stored, error), ZipStatus::unsafe_name);
    refused(writer.begin_entry("CON.txt", 1, Method::stored, error), ZipStatus::unsafe_name);
    refused(
        writer.begin_entry(std::string(513, 'a'), 0, Method::stored, error),
        ZipStatus::name_too_long
    );
    refused(
        writer.begin_entry("bad\xff.txt", 1, Method::stored, error), ZipStatus::bad_name_encoding
    );
    refused(writer.begin_entry("dir/", 1, Method::stored, error), ZipStatus::size_mismatch);
    CHECK(writer.begin_entry("Read.Me", 4, Method::stored, error));
    refused(writer.finish(error), ZipStatus::entry_open);
    CHECK(writer.write(bytes_of("abcd"), error));
    CHECK(writer.end_entry(error));
    refused(writer.begin_entry("read.me", 1, Method::stored, error), ZipStatus::duplicate_name);
    // ASCII case only: these two names are not the same folded.
    CHECK(writer.begin_entry("\xc3\x84.txt", 1, Method::stored, error));
    CHECK(writer.write(bytes_of("A"), error));
    CHECK(writer.end_entry(error));
    CHECK(writer.begin_entry("\xc3\xa4.txt", 1, Method::stored, error));
    CHECK(writer.write(bytes_of("a"), error));
    CHECK(writer.end_entry(error));
    CHECK(writer.begin_entry("short.bin", 4, Method::stored, error));
    CHECK(writer.write(bytes_of("ab"), error));
    refused(writer.end_entry(error), ZipStatus::wrong_size);
    refused(writer.finish(error), ZipStatus::wrong_size);

    VectorOut again{};
    StreamWriter second{{&again, write_vector}};
    CHECK(second.begin_entry("long.bin", 2, Method::stored, error));
    CHECK(second.write(bytes_of("abcd"), error));
    refused(second.end_entry(error), ZipStatus::wrong_size);

    VectorOut failing{};
    failing.fail_at = 1;
    StreamWriter third{{&failing, write_vector}};
    CHECK(third.begin_entry("hook.txt", 4, Method::stored, error));
    refused(third.write(bytes_of("hook"), error), ZipStatus::write_failed);
    refused(third.end_entry(error), ZipStatus::write_failed);
    refused(third.finish(error), ZipStatus::write_failed);

    VectorOut plain{};
    StreamWriter fourth{{&plain, write_vector}};
    CHECK(fourth.begin_entry("ok.txt", 2, Method::deflated, error));
    CHECK(fourth.write(bytes_of("ok"), error));
    CHECK(fourth.end_entry(error));
    CHECK(fourth.finish(error));
    refused(fourth.begin_entry("more.txt", 0, Method::stored, error), ZipStatus::entry_open);
    refused(fourth.finish(error), ZipStatus::entry_open);
    CHECK(expect_stream(
        {{"ok.txt", bytes_of("ok"), Method::deflated, false}},
        {&plain.bytes, read_vector},
        plain.bytes.size(),
        {}
    ));
}

void test_sample_file() {
    const std::vector<Item> items{
        {"readme.txt", bytes_of("A package readme.\n"), Method::deflated, false},
        {"units/", {}, Method::stored, true},
        {"units/arm.txt", bytes_of("Unit.\n"), Method::deflated, false},
        {"caf\xc3\xa9.txt", bytes_of("caf\xc3\xa9\n"), Method::deflated, false},
    };
    ZipError error{};
    std::vector<uint8_t> archive{};
    CHECK(write_items(items, {}, error, archive));
    CHECK(expect_stream(items, {&archive, read_vector}, archive.size(), {}));
    std::FILE* file = std::fopen("writer-sample.zip", "wb");
    CHECK(file != nullptr);
    if (file != nullptr) {
        CHECK(std::fwrite(archive.data(), 1, archive.size(), file) == archive.size());
        CHECK(std::fclose(file) == 0);
    }
}

void test_empty_archive() {
    ZipError error{};
    std::vector<uint8_t> archive{};
    CHECK(write_items({}, {}, error, archive));
    std::vector<uint8_t> memory{};
    CHECK(write_archive({}, memory, error));
    CHECK(archive == memory);
    CHECK(archive.size() == 22);
}

} // namespace

int main() {
    test_sample_file();
    test_stored_matches_memory_writer();
    test_round_trip();
    test_deflate_identical();
    test_pinned_stored();
    test_zip64_always();
    test_empty_archive();
    test_refusals();
    test_thousand();
    test_past_four_gib();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("zip writer: all checks passed\n");
    return 0;
}
