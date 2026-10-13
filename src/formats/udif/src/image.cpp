// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// UDIF: the koly trailer, the blkx mish tables and zlib chunks.
#include "oa/formats/udif.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace oa::formats::udif {
namespace {

constexpr uint32_t k_koly = 0x6b6f6c79;
constexpr uint32_t k_mish = 0x6d697368;
constexpr uint32_t k_version = 4;
constexpr uint32_t k_mish_version = 1;
constexpr uint64_t k_trailer_bytes = 512;
constexpr uint64_t k_sector_bytes = 512;
constexpr uint64_t k_max_xml_bytes = 32ull << 20;
constexpr uint64_t k_max_runs = 1000000;
constexpr uint64_t k_max_chunk_bytes = 64ull << 20;
constexpr uint64_t k_max_sectors = 1ull << 31;
constexpr std::size_t k_cache_entries = 4;
constexpr uint32_t k_raw = 0x00000001;
constexpr uint32_t k_ignore = 0x00000002;
constexpr uint32_t k_zero = 0x00000000;
constexpr uint32_t k_comment = 0x7ffffffe;
constexpr uint32_t k_terminator = 0xffffffff;
constexpr uint32_t k_zlib = 0x80000005;
constexpr uint32_t k_adc = 0x80000004;
constexpr uint32_t k_bzip2 = 0x80000006;
constexpr uint32_t k_lzfse = 0x80000007;
constexpr uint32_t k_lzma = 0x80000008;

/// The koly trailer, counted from its first byte.
constexpr std::size_t k_off_signature = 0;
constexpr std::size_t k_off_version = 4;
constexpr std::size_t k_off_header_size = 8;
constexpr std::size_t k_off_data_fork = 24;
constexpr std::size_t k_off_segment_count = 60;
constexpr std::size_t k_off_xml = 216;
constexpr std::size_t k_off_xml_length = 224;
constexpr std::size_t k_off_sectors = 492;

/// A mish table. The checksum is skipped: a known image is recognised by
/// the file's own digest, and each chunk is inflated or copied on its own.
constexpr std::size_t k_mish_sector = 8;
constexpr std::size_t k_mish_count = 16;
constexpr std::size_t k_mish_data = 24;
constexpr std::size_t k_mish_runs = 200;
constexpr std::size_t k_mish_header = 204;
constexpr std::size_t k_run_bytes = 40;

void fail(std::string* error, const char* message) {
    if (error != nullptr)
        *error = message;
}

[[nodiscard]] bool add_u64(uint64_t left, uint64_t right, uint64_t& sum) noexcept {
    if (left > std::numeric_limits<uint64_t>::max() - right)
        return false;
    sum = left + right;
    return true;
}

[[nodiscard]] bool mul_u64(uint64_t left, uint64_t right, uint64_t& product) noexcept {
    if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left)
        return false;
    product = left * right;
    return true;
}

[[nodiscard]] uint32_t load_be32(const uint8_t* bytes) noexcept {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

[[nodiscard]] uint64_t load_be64(const uint8_t* bytes) noexcept {
    return (static_cast<uint64_t>(load_be32(bytes)) << 32) |
           static_cast<uint64_t>(load_be32(bytes + 4));
}

[[nodiscard]] bool read_exact(
    const ReadHooks& hooks, uint64_t offset, uint8_t* out, size_t count, uint64_t file_bytes
) {
    if (hooks.read == nullptr)
        return false;
    if (offset > file_bytes || static_cast<uint64_t>(count) > file_bytes - offset)
        return false;
    return hooks.read(hooks.context, offset, out, count);
}

/// A message for a chunk kind this reader will not inflate, or null when
/// the kind is raw, zlib, zeroes, filler, a comment or the terminator.
[[nodiscard]] const char* refused_kind(uint32_t kind) noexcept {
    switch (kind) {
    case k_zero:
    case k_raw:
    case k_ignore:
    case k_comment:
    case k_terminator:
    case k_zlib:
        return nullptr;
    case k_adc:
        return "The disk image uses ADC compression, which this reader does not read";
    case k_bzip2:
        return "The disk image uses bzip2 compression, which this reader does not read";
    case k_lzfse:
        return "The disk image uses LZFSE compression, which this reader does not read";
    case k_lzma:
        return "The disk image uses LZMA compression, which this reader does not read";
    default:
        return "The disk image uses an unknown compression, which this reader does not read";
    }
}

[[nodiscard]] int base64_value(char digit) noexcept {
    if (digit >= 'A' && digit <= 'Z')
        return digit - 'A';
    if (digit >= 'a' && digit <= 'z')
        return digit - 'a' + 26;
    if (digit >= '0' && digit <= '9')
        return digit - '0' + 52;
    if (digit == '+')
        return 62;
    if (digit == '/')
        return 63;
    return -1;
}

[[nodiscard]] bool decode_base64(std::string_view text, std::vector<uint8_t>& out) {
    std::string packed;
    packed.reserve(text.size());
    for (char digit : text) {
        if (digit == ' ' || digit == '\n' || digit == '\r' || digit == '\t')
            continue;
        packed.push_back(digit);
    }
    if (packed.size() % 4 != 0)
        return false;
    out.clear();
    out.reserve(packed.size() / 4 * 3);
    for (std::size_t index = 0; index < packed.size(); index += 4) {
        const bool last = index + 4 == packed.size();
        int values[4];
        for (int slot = 0; slot < 4; ++slot) {
            const char digit = packed[index + static_cast<std::size_t>(slot)];
            if (digit == '=') {
                if (!last || slot < 2)
                    return false;
                values[slot] = 0;
                continue;
            }
            if (last && slot > 0 && packed[index + static_cast<std::size_t>(slot) - 1] == '=')
                return false;
            const int value = base64_value(digit);
            if (value < 0)
                return false;
            values[slot] = value;
        }
        const uint32_t triple =
            (static_cast<uint32_t>(values[0]) << 18) | (static_cast<uint32_t>(values[1]) << 12) |
            (static_cast<uint32_t>(values[2]) << 6) | static_cast<uint32_t>(values[3]);
        out.push_back(static_cast<uint8_t>((triple >> 16) & 0xff));
        if (packed[index + 2] != '=')
            out.push_back(static_cast<uint8_t>((triple >> 8) & 0xff));
        if (packed[index + 3] != '=')
            out.push_back(static_cast<uint8_t>(triple & 0xff));
    }
    return true;
}

/// The range of one element, content only, after matching nested tags.
[[nodiscard]] bool element_body(
    std::string_view text,
    std::size_t open_at,
    std::string_view open_name,
    std::string_view close_tag,
    std::size_t& body_begin,
    std::size_t& body_end
) {
    const std::size_t open_end = text.find('>', open_at);
    if (open_end == std::string_view::npos || open_end < open_at)
        return false;
    body_begin = open_end + 1;
    int depth = 1;
    std::size_t cursor = body_begin;
    const std::string nested_open = std::string("<") + std::string(open_name);
    while (cursor < text.size() && depth > 0) {
        if (text.compare(cursor, nested_open.size(), nested_open) == 0 &&
            cursor + nested_open.size() < text.size()) {
            const char mark = text[cursor + nested_open.size()];
            if (mark == '>' || mark == ' ' || mark == '\n' || mark == '\t') {
                ++depth;
                cursor += nested_open.size();
                continue;
            }
        }
        if (text.compare(cursor, close_tag.size(), close_tag) == 0) {
            --depth;
            if (depth == 0) {
                body_end = cursor;
                return true;
            }
            cursor += close_tag.size();
            continue;
        }
        ++cursor;
    }
    return false;
}

struct Chunk {
    uint64_t disk_sector{};
    uint64_t sector_count{};
    uint32_t kind{};
    uint64_t file_offset{};
    uint64_t compressed_bytes{};
};

[[nodiscard]] bool
inflate_zlib(const uint8_t* compressed, std::size_t compressed_bytes, std::vector<uint8_t>& plain) {
    if (plain.empty())
        return false;
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return false;
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed));
    stream.avail_in = static_cast<uInt>(compressed_bytes);
    stream.next_out = plain.data();
    stream.avail_out = static_cast<uInt>(plain.size());
    const int result = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    inflateEnd(&stream);
    return result == Z_STREAM_END && produced == plain.size();
}

struct CacheEntry {
    uint64_t file_offset{};
    uint32_t used{};
    std::vector<uint8_t> plain;
};

} // namespace

struct Image::State {
    ReadHooks hooks{};
    uint64_t file_bytes{};
    uint64_t disk_bytes{};
    std::vector<Chunk> chunks;
    std::vector<CacheEntry> cache;
    uint32_t cache_clock{1};

    [[nodiscard]] bool load(std::string* error);
    [[nodiscard]] bool parse_xml(std::string_view xml, uint64_t data_fork, std::string* error);
    [[nodiscard]] bool
    parse_mish(const std::vector<uint8_t>& table, uint64_t data_fork, std::string* error);
    [[nodiscard]] const Chunk* find_chunk(uint64_t sector) const;
    [[nodiscard]] bool copy_chunk(
        const Chunk& chunk, uint64_t chunk_offset, std::span<uint8_t> out, std::string* error
    );
    [[nodiscard]] bool
    inflated(const Chunk& chunk, std::span<const uint8_t>& plain, std::string* error);
};

Image::Image() = default;

Image::~Image() = default;

Image::Image(Image&&) noexcept = default;

Image& Image::operator=(Image&&) noexcept = default;

bool Image::open(ReadHooks hooks, uint64_t file_bytes, std::string* error) {
    if (state_ != nullptr)
        return true;
    auto state = std::make_unique<State>();
    state->hooks = hooks;
    state->file_bytes = file_bytes;
    if (!state->load(error))
        return false;
    state_ = std::move(state);
    return true;
}

uint64_t Image::disk_bytes() const {
    return state_ == nullptr ? 0 : state_->disk_bytes;
}

bool Image::read(uint64_t offset, std::span<uint8_t> out, std::string* error) {
    if (state_ == nullptr) {
        fail(error, "The disk image could not be read");
        return false;
    }
    if (out.empty())
        return true;
    if (offset > state_->disk_bytes || out.size() > state_->disk_bytes - offset) {
        fail(error, "The disk image could not be read");
        return false;
    }
    std::size_t filled = 0;
    while (filled < out.size()) {
        const uint64_t position = offset + static_cast<uint64_t>(filled);
        const uint64_t sector = position / k_sector_bytes;
        const uint64_t within = position % k_sector_bytes;
        const Chunk* chunk = state_->find_chunk(sector);
        if (chunk == nullptr) {
            const uint64_t next = [&]() {
                for (const Chunk& candidate : state_->chunks) {
                    if (candidate.disk_sector > sector)
                        return candidate.disk_sector;
                }
                return state_->disk_bytes / k_sector_bytes;
            }();
            uint64_t gap = 0;
            if (!mul_u64(next - sector, k_sector_bytes, gap) || gap < within) {
                fail(error, "The disk image could not be read");
                return false;
            }
            const uint64_t holes = gap - within;
            const std::size_t count = static_cast<std::size_t>(
                std::min<uint64_t>(holes, static_cast<uint64_t>(out.size() - filled))
            );
            std::memset(out.data() + filled, 0, count);
            filled += count;
            continue;
        }
        const uint64_t into = (sector - chunk->disk_sector) * k_sector_bytes + within;
        uint64_t chunk_bytes = 0;
        if (!mul_u64(chunk->sector_count, k_sector_bytes, chunk_bytes) || into > chunk_bytes) {
            fail(error, "The disk image could not be read");
            return false;
        }
        const uint64_t avail = chunk_bytes - into;
        const std::size_t count = static_cast<std::size_t>(
            std::min<uint64_t>(avail, static_cast<uint64_t>(out.size() - filled))
        );
        if (!state_->copy_chunk(*chunk, into, out.subspan(filled, count), error))
            return false;
        filled += count;
    }
    return true;
}

bool Image::State::load(std::string* error) {
    if (hooks.read == nullptr || file_bytes < k_trailer_bytes) {
        fail(error, "The disk image could not be read");
        return false;
    }
    uint8_t trailer[k_trailer_bytes];
    if (!read_exact(hooks, file_bytes - k_trailer_bytes, trailer, k_trailer_bytes, file_bytes)) {
        fail(error, "The disk image could not be read");
        return false;
    }
    if (load_be32(trailer + k_off_signature) != k_koly ||
        load_be32(trailer + k_off_version) != k_version ||
        load_be32(trailer + k_off_header_size) != k_trailer_bytes) {
        fail(error, "The disk image is not a UDIF image");
        return false;
    }
    const uint32_t segment_count = load_be32(trailer + k_off_segment_count);
    if (segment_count > 1) {
        fail(
            error,
            "The disk image is split into more than one file, which this reader does not read"
        );
        return false;
    }
    const uint64_t sectors = load_be64(trailer + k_off_sectors);
    if (sectors == 0 || sectors > k_max_sectors || !mul_u64(sectors, k_sector_bytes, disk_bytes)) {
        fail(error, "The disk image could not be read");
        return false;
    }
    const uint64_t data_fork = load_be64(trailer + k_off_data_fork);
    if (data_fork > file_bytes) {
        fail(error, "The disk image could not be read");
        return false;
    }
    const uint64_t xml_offset = load_be64(trailer + k_off_xml);
    const uint64_t xml_length = load_be64(trailer + k_off_xml_length);
    uint64_t xml_end = 0;
    if (xml_length > k_max_xml_bytes || !add_u64(xml_offset, xml_length, xml_end) ||
        xml_end > file_bytes) {
        fail(error, "The disk image block map could not be read");
        return false;
    }
    if (xml_length == 0) {
        fail(error, "The disk image has no block map");
        return false;
    }
    std::string xml(static_cast<std::size_t>(xml_length), '\0');
    if (!read_exact(
            hooks,
            xml_offset,
            reinterpret_cast<uint8_t*>(xml.data()),
            static_cast<std::size_t>(xml_length),
            file_bytes
        )) {
        fail(error, "The disk image could not be read");
        return false;
    }
    if (!parse_xml(xml, data_fork, error))
        return false;
    if (chunks.empty()) {
        fail(error, "The disk image has no block map");
        return false;
    }
    std::sort(chunks.begin(), chunks.end(), [](const Chunk& left, const Chunk& right) {
        return left.disk_sector < right.disk_sector;
    });
    for (std::size_t index = 1; index < chunks.size(); ++index) {
        uint64_t end = 0;
        if (!add_u64(chunks[index - 1].disk_sector, chunks[index - 1].sector_count, end) ||
            end > chunks[index].disk_sector) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
    }
    return true;
}

bool Image::State::parse_xml(std::string_view xml, uint64_t data_fork, std::string* error) {
    constexpr std::string_view key = "<key>blkx</key>";
    const std::size_t key_at = xml.find(key);
    if (key_at == std::string_view::npos) {
        fail(error, "The disk image has no block map");
        return false;
    }
    const std::size_t array_at = xml.find("<array", key_at + key.size());
    if (array_at == std::string_view::npos) {
        fail(error, "The disk image block map could not be read");
        return false;
    }
    std::size_t array_begin = 0;
    std::size_t array_end = 0;
    if (!element_body(xml, array_at, "array", "</array>", array_begin, array_end)) {
        fail(error, "The disk image block map could not be read");
        return false;
    }
    const std::string_view array = xml.substr(array_begin, array_end - array_begin);
    std::size_t cursor = 0;
    while (cursor < array.size()) {
        const std::size_t dict_at = array.find("<dict", cursor);
        if (dict_at == std::string_view::npos)
            break;
        std::size_t dict_begin = 0;
        std::size_t dict_end = 0;
        if (!element_body(array, dict_at, "dict", "</dict>", dict_begin, dict_end)) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        const std::string_view dict = array.substr(dict_begin, dict_end - dict_begin);
        cursor = dict_end + 7;
        constexpr std::string_view data_key = "<key>Data</key>";
        const std::size_t data_key_at = dict.find(data_key);
        if (data_key_at == std::string_view::npos)
            continue;
        const std::size_t data_at = dict.find("<data", data_key_at + data_key.size());
        if (data_at == std::string_view::npos) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        std::size_t data_begin = 0;
        std::size_t data_end = 0;
        if (!element_body(dict, data_at, "data", "</data>", data_begin, data_end)) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        std::vector<uint8_t> table;
        if (!decode_base64(dict.substr(data_begin, data_end - data_begin), table)) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        if (table.size() < 4 || load_be32(table.data()) != k_mish)
            continue;
        if (!parse_mish(table, data_fork, error))
            return false;
    }
    return true;
}

bool Image::State::parse_mish(
    const std::vector<uint8_t>& table, uint64_t data_fork, std::string* error
) {
    if (table.size() < k_mish_header || load_be32(table.data() + 4) != k_mish_version) {
        fail(error, "The disk image block map could not be read");
        return false;
    }
    const uint64_t table_sector = load_be64(table.data() + k_mish_sector);
    const uint64_t table_count = load_be64(table.data() + k_mish_count);
    const uint64_t table_data = load_be64(table.data() + k_mish_data);
    const uint32_t run_count = load_be32(table.data() + k_mish_runs);
    uint64_t runs_bytes = 0;
    if (static_cast<uint64_t>(run_count) > k_max_runs ||
        !mul_u64(run_count, k_run_bytes, runs_bytes) ||
        table.size() < k_mish_header + static_cast<std::size_t>(runs_bytes)) {
        fail(error, "The disk image block map could not be read");
        return false;
    }
    uint64_t table_end = 0;
    if (!add_u64(table_sector, table_count, table_end)) {
        fail(error, "The disk image block map could not be read");
        return false;
    }
    for (uint32_t index = 0; index < run_count; ++index) {
        const uint8_t* run =
            table.data() + k_mish_header + static_cast<std::size_t>(index) * k_run_bytes;
        const uint32_t kind = load_be32(run);
        if (const char* refused = refused_kind(kind)) {
            fail(error, refused);
            return false;
        }
        if (kind == k_terminator || kind == k_comment)
            continue;
        const uint64_t relative = load_be64(run + 8);
        const uint64_t count = load_be64(run + 16);
        const uint64_t compressed_offset = load_be64(run + 24);
        const uint64_t compressed_length = load_be64(run + 32);
        if (count == 0 || chunks.size() >= k_max_runs) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        uint64_t disk_sector = 0;
        uint64_t sector_end = 0;
        uint64_t uncompressed = 0;
        if (!add_u64(table_sector, relative, disk_sector) ||
            !add_u64(disk_sector, count, sector_end) || sector_end > disk_bytes / k_sector_bytes ||
            !mul_u64(count, k_sector_bytes, uncompressed)) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        if (relative > table_count || count > table_count - relative) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        const bool stored = kind == k_raw || kind == k_zlib;
        if (stored && (uncompressed > k_max_chunk_bytes || compressed_length > k_max_chunk_bytes ||
                       compressed_length == 0)) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        if (kind == k_raw && compressed_length != uncompressed) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        if ((kind == k_zero || kind == k_ignore) && compressed_length != 0) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        uint64_t file_offset = 0;
        uint64_t file_end = 0;
        if (!add_u64(data_fork, table_data, file_offset) ||
            !add_u64(file_offset, compressed_offset, file_offset) ||
            !add_u64(file_offset, compressed_length, file_end) || file_end > file_bytes) {
            fail(error, "The disk image block map could not be read");
            return false;
        }
        Chunk chunk;
        chunk.disk_sector = disk_sector;
        chunk.sector_count = count;
        chunk.kind = kind;
        chunk.file_offset = file_offset;
        chunk.compressed_bytes = compressed_length;
        chunks.push_back(chunk);
    }
    return true;
}

const Chunk* Image::State::find_chunk(uint64_t sector) const {
    if (chunks.empty())
        return nullptr;
    std::size_t lower = 0;
    std::size_t upper = chunks.size();
    while (lower < upper) {
        const std::size_t middle = lower + (upper - lower) / 2;
        if (chunks[middle].disk_sector <= sector)
            lower = middle + 1;
        else
            upper = middle;
    }
    if (lower == 0)
        return nullptr;
    const Chunk& chunk = chunks[lower - 1];
    uint64_t end = 0;
    if (!add_u64(chunk.disk_sector, chunk.sector_count, end) || sector >= end)
        return nullptr;
    return &chunk;
}

bool Image::State::inflated(
    const Chunk& chunk, std::span<const uint8_t>& plain, std::string* error
) {
    for (CacheEntry& entry : cache) {
        if (entry.file_offset == chunk.file_offset && !entry.plain.empty()) {
            entry.used = cache_clock++;
            plain = entry.plain;
            return true;
        }
    }
    uint64_t plain_bytes = 0;
    if (!mul_u64(chunk.sector_count, k_sector_bytes, plain_bytes) ||
        plain_bytes > k_max_chunk_bytes || plain_bytes == 0) {
        fail(error, "The disk image could not be read");
        return false;
    }
    std::vector<uint8_t> compressed(static_cast<std::size_t>(chunk.compressed_bytes));
    if (!read_exact(hooks, chunk.file_offset, compressed.data(), compressed.size(), file_bytes)) {
        fail(error, "The disk image could not be read");
        return false;
    }
    std::vector<uint8_t> decoded(static_cast<std::size_t>(plain_bytes));
    if (compressed.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max()) ||
        decoded.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max()) ||
        !inflate_zlib(compressed.data(), compressed.size(), decoded)) {
        fail(error, "The disk image could not be read");
        return false;
    }
    if (cache.size() >= k_cache_entries) {
        std::size_t oldest = 0;
        for (std::size_t index = 1; index < cache.size(); ++index) {
            if (cache[index].used < cache[oldest].used)
                oldest = index;
        }
        cache.erase(cache.begin() + static_cast<std::ptrdiff_t>(oldest));
    }
    CacheEntry entry;
    entry.file_offset = chunk.file_offset;
    entry.used = cache_clock++;
    entry.plain = std::move(decoded);
    cache.push_back(std::move(entry));
    plain = cache.back().plain;
    return true;
}

bool Image::State::copy_chunk(
    const Chunk& chunk, uint64_t chunk_offset, std::span<uint8_t> out, std::string* error
) {
    if (out.empty())
        return true;
    if (chunk.kind == k_zero || chunk.kind == k_ignore) {
        std::memset(out.data(), 0, out.size());
        return true;
    }
    if (chunk.kind == k_raw) {
        return read_exact(
                   hooks, chunk.file_offset + chunk_offset, out.data(), out.size(), file_bytes
               ) ||
               (fail(error, "The disk image could not be read"), false);
    }
    if (chunk.kind != k_zlib) {
        fail(error, "The disk image could not be read");
        return false;
    }
    std::span<const uint8_t> plain;
    if (!inflated(chunk, plain, error))
        return false;
    if (chunk_offset > plain.size() ||
        out.size() > plain.size() - static_cast<std::size_t>(chunk_offset)) {
        fail(error, "The disk image could not be read");
        return false;
    }
    std::memcpy(out.data(), plain.data() + static_cast<std::size_t>(chunk_offset), out.size());
    return true;
}

} // namespace oa::formats::udif
