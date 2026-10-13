// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HFS+: the volume header, the catalogue B-tree and the extents overflow tree.
#include "oa/formats/hfsplus.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oa::formats::hfsplus {
namespace {

constexpr uint16_t k_hfs_plus = 0x482B;
constexpr uint16_t k_hfsx = 0x4858;
constexpr uint64_t k_header_offset = 1024;
constexpr uint64_t k_header_bytes = 512;
constexpr uint32_t k_min_block = 512;
constexpr uint32_t k_max_block = 1u << 20;
constexpr uint32_t k_hfs_epoch = 2082844800;
constexpr uint32_t k_root_id = 2;
constexpr uint32_t k_root_parent = 1;
constexpr uint64_t k_max_nodes = 1000000;
constexpr uint32_t k_max_node_bytes = 32768;
constexpr std::size_t k_max_items = 500000;
constexpr uint32_t k_compressed_flag = 0x20;
constexpr uint16_t k_mode_mask = 0170000;
constexpr uint16_t k_symlink_mode = 0120000;
constexpr int8_t k_leaf = -1;
constexpr int8_t k_header_node = 1;
constexpr uint16_t k_folder_record = 1;
constexpr uint16_t k_file_record = 2;
constexpr uint16_t k_folder_thread = 3;
constexpr uint16_t k_file_thread = 4;
constexpr std::size_t k_folder_data = 88;
constexpr std::size_t k_file_data = 248;
constexpr std::size_t k_data_fork = 88;
constexpr uint32_t k_big_keys = 2;
constexpr uint8_t k_data_fork_type = 0;
constexpr uint8_t k_resource_fork_type = 0xFF;

constexpr uint8_t k_apfs_guid[16] = {
    0xEF, 0x57, 0x34, 0x7C, 0x00, 0x00, 0xAA, 0x11, 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC
};

struct Extent {
    uint32_t start{};
    uint32_t count{};
};

struct Span {
    uint64_t logical_block{};
    uint64_t count{};
    uint64_t physical{};
    bool hole{};
};

struct Fork {
    uint64_t logical{};
    Extent extents[8]{};
};

struct Overflow {
    uint32_t file_id{};
    uint8_t fork{};
    uint32_t start_block{};
    Extent extents[8]{};
};

struct FileBody {
    uint32_t id{};
    uint64_t logical{};
    bool compressed{};
    std::vector<Span> spans;
};

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

[[nodiscard]] bool power_of_two(uint32_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] uint16_t load_be16(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

[[nodiscard]] int16_t load_be16s(const uint8_t* bytes) noexcept {
    return static_cast<int16_t>(load_be16(bytes));
}

[[nodiscard]] uint32_t load_be32(const uint8_t* bytes) noexcept {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

[[nodiscard]] uint64_t load_be64(const uint8_t* bytes) noexcept {
    return (static_cast<uint64_t>(load_be32(bytes)) << 32) | load_be32(bytes + 4);
}

[[nodiscard]] uint32_t load_le32(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] uint64_t load_le64(const uint8_t* bytes) noexcept {
    return static_cast<uint64_t>(load_le32(bytes)) |
           (static_cast<uint64_t>(load_le32(bytes + 4)) << 32);
}

[[nodiscard]] int64_t unix_time(uint32_t hfs_date) noexcept {
    if (hfs_date < k_hfs_epoch)
        return 0;
    return static_cast<int64_t>(hfs_date - k_hfs_epoch);
}

void append_utf8(std::string& text, uint32_t code) {
    if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        code = 0xFFFD;
    if (code < 0x80) {
        text.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (code >> 6)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        text.push_back(static_cast<char>(0xE0 | (code >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        text.push_back(static_cast<char>(0xF0 | (code >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

[[nodiscard]] bool
decode_name(const uint8_t* bytes, uint16_t units, std::size_t available, std::string& name) {
    if (units > 255)
        return false;
    const std::size_t bytes_needed = static_cast<std::size_t>(units) * 2;
    if (bytes_needed > available)
        return false;
    name.clear();
    for (uint16_t index = 0; index < units; ++index) {
        const uint16_t unit = load_be16(bytes + static_cast<std::size_t>(index) * 2);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (index + 1 >= units) {
                append_utf8(name, 0xFFFD);
                break;
            }
            const uint16_t low = load_be16(bytes + static_cast<std::size_t>(index + 1) * 2);
            if (low < 0xDC00 || low > 0xDFFF) {
                append_utf8(name, 0xFFFD);
                continue;
            }
            const uint32_t code = 0x10000u + ((static_cast<uint32_t>(unit - 0xD800) << 10) |
                                              static_cast<uint32_t>(low - 0xDC00));
            append_utf8(name, code);
            ++index;
            continue;
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            append_utf8(name, 0xFFFD);
            continue;
        }
        append_utf8(name, unit);
    }
    return true;
}

[[nodiscard]] bool
same_bytes(const uint8_t* left, const uint8_t* right, std::size_t count) noexcept {
    return std::memcmp(left, right, count) == 0;
}

[[nodiscard]] Fork load_fork(const uint8_t* bytes) noexcept {
    Fork fork;
    fork.logical = load_be64(bytes);
    for (int index = 0; index < 8; ++index) {
        const uint8_t* extent = bytes + 16 + static_cast<std::size_t>(index) * 8;
        fork.extents[index].start = load_be32(extent);
        fork.extents[index].count = load_be32(extent + 4);
    }
    return fork;
}

enum class ExtentAdd { stored, ended, bad };

[[nodiscard]] ExtentAdd append_extent(
    std::vector<Span>& spans,
    uint64_t& cursor,
    uint32_t start,
    uint32_t count,
    uint32_t total_blocks,
    std::string* error
) {
    if (count == 0)
        return ExtentAdd::ended;
    const bool hole = start == 0;
    if (!hole && (start > total_blocks || count > total_blocks - start)) {
        fail(error, "The catalogue could not be read");
        return ExtentAdd::bad;
    }
    Span span;
    span.logical_block = cursor;
    span.count = count;
    span.physical = start;
    span.hole = hole;
    if (!add_u64(cursor, count, cursor)) {
        fail(error, "The catalogue could not be read");
        return ExtentAdd::bad;
    }
    spans.push_back(span);
    return ExtentAdd::stored;
}

[[nodiscard]] bool extents_from(
    const Extent extents[8],
    uint64_t& cursor,
    std::vector<Span>& spans,
    uint32_t total_blocks,
    std::string* error
) {
    for (int index = 0; index < 8; ++index) {
        const ExtentAdd added = append_extent(
            spans, cursor, extents[index].start, extents[index].count, total_blocks, error
        );
        if (added == ExtentAdd::bad)
            return false;
        if (added == ExtentAdd::ended)
            return true;
    }
    return true;
}

} // namespace

struct Volume::State {
    ReadHooks outer{};
    uint64_t partition_offset{};
    uint64_t volume_bytes{};
    uint64_t disk_bytes{};
    uint32_t block_size{};
    uint32_t total_blocks{};
    std::vector<CatalogItem> catalog;
    std::vector<FileBody> bodies;
    std::unordered_map<uint32_t, std::size_t> by_id;
    std::unordered_map<uint32_t, std::size_t> body_of;

    [[nodiscard]] bool read_at(uint64_t offset, uint8_t* out, std::size_t count) const;
    [[nodiscard]] bool locate(uint64_t& offset, uint64_t& bytes, std::string* error) const;
    [[nodiscard]] bool parse(std::string* error);
    [[nodiscard]] bool read_spans(
        const std::vector<Span>& spans, uint64_t offset, std::span<uint8_t> out, std::string* error
    ) const;
    [[nodiscard]] bool walk_tree(
        const Fork& fork, bool catalogue, std::vector<Overflow>& overflow, std::string* error
    );
    [[nodiscard]] bool read_node(
        const std::vector<Span>& spans,
        uint64_t logical,
        uint32_t node_size,
        uint32_t index,
        std::vector<uint8_t>& node,
        std::string* error
    ) const;
};

bool Volume::State::read_at(uint64_t offset, uint8_t* out, std::size_t count) const {
    if (outer.read == nullptr)
        return false;
    if (count == 0)
        return true;
    if (offset > volume_bytes || static_cast<uint64_t>(count) > volume_bytes - offset)
        return false;
    uint64_t at = 0;
    if (!add_u64(partition_offset, offset, at))
        return false;
    return outer.read(outer.context, at, out, count);
}

bool Volume::State::read_spans(
    const std::vector<Span>& spans, uint64_t offset, std::span<uint8_t> out, std::string* error
) const {
    std::size_t filled = 0;
    while (filled < out.size()) {
        const uint64_t position = offset + static_cast<uint64_t>(filled);
        const uint64_t block = position / block_size;
        const uint64_t within = position % block_size;
        const Span* span = nullptr;
        const Span* next = nullptr;
        for (const Span& candidate : spans) {
            uint64_t end = 0;
            if (!add_u64(candidate.logical_block, candidate.count, end)) {
                fail(error, "The file could not be read");
                return false;
            }
            if (block >= candidate.logical_block && block < end) {
                span = &candidate;
                break;
            }
            if (candidate.logical_block > block &&
                (next == nullptr || candidate.logical_block < next->logical_block))
                next = &candidate;
        }
        if (span == nullptr) {
            uint64_t hole_bytes = out.size() - filled;
            if (next != nullptr) {
                uint64_t next_byte = 0;
                if (!mul_u64(next->logical_block, block_size, next_byte) || next_byte < position) {
                    fail(error, "The file could not be read");
                    return false;
                }
                hole_bytes = std::min<uint64_t>(hole_bytes, next_byte - position);
            }
            const std::size_t count = static_cast<std::size_t>(hole_bytes);
            std::memset(out.data() + filled, 0, count);
            filled += count;
            continue;
        }
        uint64_t end = 0;
        if (!add_u64(span->logical_block, span->count, end)) {
            fail(error, "The file could not be read");
            return false;
        }
        const uint64_t block_into = block - span->logical_block;
        uint64_t byte_into = 0;
        uint64_t span_bytes = 0;
        if (!mul_u64(block_into, block_size, byte_into) || !add_u64(byte_into, within, byte_into) ||
            !mul_u64(span->count, block_size, span_bytes) || byte_into > span_bytes) {
            fail(error, "The file could not be read");
            return false;
        }
        const uint64_t avail = span_bytes - byte_into;
        const std::size_t count = static_cast<std::size_t>(
            std::min<uint64_t>(avail, static_cast<uint64_t>(out.size() - filled))
        );
        if (span->hole) {
            std::memset(out.data() + filled, 0, count);
        } else {
            uint64_t physical = 0;
            uint64_t volume_off = 0;
            if (!mul_u64(span->physical, block_size, physical) ||
                !add_u64(physical, byte_into, volume_off) ||
                !read_at(volume_off, out.data() + filled, count)) {
                fail(error, "The file could not be read");
                return false;
            }
        }
        filled += count;
    }
    return true;
}

bool Volume::State::read_node(
    const std::vector<Span>& spans,
    uint64_t logical,
    uint32_t node_size,
    uint32_t index,
    std::vector<uint8_t>& node,
    std::string* error
) const {
    uint64_t at = 0;
    if (!mul_u64(index, node_size, at) || at > logical || node_size > logical - at) {
        fail(error, "The catalogue could not be read");
        return false;
    }
    node.assign(node_size, 0);
    return read_spans(spans, at, node, error);
}

bool Volume::State::walk_tree(
    const Fork& fork, bool catalogue, std::vector<Overflow>& overflow, std::string* error
) {
    if (fork.logical == 0)
        return !catalogue || (fail(error, "The catalogue could not be read"), false);
    std::vector<Span> spans;
    uint64_t cursor = 0;
    if (!extents_from(fork.extents, cursor, spans, total_blocks, error))
        return false;
    uint64_t covered = 0;
    if (!mul_u64(cursor, block_size, covered) || covered < fork.logical) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    if (fork.logical < 14) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    uint8_t descriptor[14];
    if (!read_spans(spans, 0, descriptor, error)) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    if (static_cast<int8_t>(descriptor[8]) != k_header_node) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    // The header record begins just after the 14-byte node descriptor.
    uint8_t header_fields[42];
    if (fork.logical < 14 + sizeof header_fields || !read_spans(spans, 14, header_fields, error)) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    const uint32_t node_size = load_be16(header_fields + 18);
    const uint32_t total_nodes = load_be32(header_fields + 22);
    const uint32_t first_leaf = load_be32(header_fields + 10);
    const uint32_t attributes = load_be32(header_fields + 38);
    if (!power_of_two(node_size) || node_size < 512 || node_size > k_max_node_bytes ||
        total_nodes == 0 || total_nodes > k_max_nodes || (attributes & k_big_keys) == 0) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    uint64_t tree_bytes = 0;
    if (!mul_u64(total_nodes, node_size, tree_bytes) || tree_bytes > fork.logical) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    if (first_leaf == 0)
        return true;
    if (first_leaf >= total_nodes) {
        fail(
            error, catalogue ? "The catalogue could not be read" : "The extents could not be read"
        );
        return false;
    }
    std::vector<uint8_t> seen(total_nodes);
    std::vector<uint8_t> node;
    uint32_t index = first_leaf;
    while (index != 0) {
        if (index >= total_nodes || seen[index] != 0) {
            fail(
                error,
                catalogue ? "The catalogue could not be read" : "The extents could not be read"
            );
            return false;
        }
        seen[index] = 1;
        if (!read_node(spans, fork.logical, node_size, index, node, error))
            return false;
        const int8_t kind = static_cast<int8_t>(node[8]);
        const uint16_t records = load_be16(node.data() + 10);
        const uint32_t forward = load_be32(node.data());
        if (kind != k_leaf) {
            fail(
                error,
                catalogue ? "The catalogue could not be read" : "The extents could not be read"
            );
            return false;
        }
        const std::size_t table = static_cast<std::size_t>(records) + 1;
        if (table > node_size / 2 || 14 + table * 2 > node_size) {
            fail(
                error,
                catalogue ? "The catalogue could not be read" : "The extents could not be read"
            );
            return false;
        }
        for (uint16_t record = 0; record < records; ++record) {
            const std::size_t start_at = node_size - 2u * (static_cast<std::size_t>(record) + 1);
            const std::size_t end_at = node_size - 2u * (static_cast<std::size_t>(record) + 2);
            const uint16_t start = load_be16(node.data() + start_at);
            const uint16_t end = load_be16(node.data() + end_at);
            const uint16_t table_at = static_cast<uint16_t>(node_size - 2u * table);
            if (start < 14 || end < start || end > table_at) {
                fail(
                    error,
                    catalogue ? "The catalogue could not be read" : "The extents could not be read"
                );
                return false;
            }
            const uint8_t* rec = node.data() + start;
            const std::size_t rec_bytes = static_cast<std::size_t>(end - start);
            if (rec_bytes < 2) {
                fail(
                    error,
                    catalogue ? "The catalogue record is truncated"
                              : "The extents record is truncated"
                );
                return false;
            }
            const uint16_t key_length = load_be16(rec);
            if ((key_length % 2) != 0 || rec_bytes < 2u + key_length) {
                fail(
                    error,
                    catalogue ? "The catalogue record is truncated"
                              : "The extents record is truncated"
                );
                return false;
            }
            const uint8_t* body = rec + 2;
            const uint8_t* data = rec + 2 + key_length;
            const std::size_t data_bytes = rec_bytes - 2u - key_length;
            if (catalogue) {
                if (key_length < 6) {
                    fail(error, "The catalogue record is truncated");
                    return false;
                }
                const uint32_t parent = load_be32(body);
                const uint16_t name_units = load_be16(body + 4);
                if (static_cast<std::size_t>(key_length) <
                    6u + static_cast<std::size_t>(name_units) * 2) {
                    fail(error, "The catalogue record is truncated");
                    return false;
                }
                std::string key_name;
                if (!decode_name(
                        body + 6, name_units, static_cast<std::size_t>(key_length) - 6, key_name
                    )) {
                    fail(error, "The catalogue record is truncated");
                    return false;
                }
                if (data_bytes < 2) {
                    fail(error, "The catalogue record is truncated");
                    return false;
                }
                const uint16_t type = static_cast<uint16_t>(load_be16s(data));
                if (type == k_folder_record || type == k_file_record) {
                    const std::size_t need = type == k_folder_record ? k_folder_data : k_file_data;
                    if (data_bytes < need) {
                        fail(error, "The catalogue record is truncated");
                        return false;
                    }
                    if (catalog.size() >= k_max_items) {
                        fail(error, "The catalogue could not be read");
                        return false;
                    }
                    CatalogItem item;
                    item.kind = type == k_folder_record ? CatalogItem::Kind::folder
                                                        : CatalogItem::Kind::file;
                    item.parent = parent;
                    item.id = load_be32(data + 8);
                    item.name = std::move(key_name);
                    item.modified = unix_time(load_be32(data + 16));
                    if (type == k_file_record) {
                        item.data_bytes = load_be64(data + k_data_fork);
                        item.compressed = (data[41] & k_compressed_flag) != 0;
                        const uint16_t mode = load_be16(data + 42);
                        item.symlink = (mode & k_mode_mask) == k_symlink_mode;
                        if (by_id.find(item.id) != by_id.end()) {
                            fail(error, "The catalogue could not be read");
                            return false;
                        }
                        FileBody file;
                        file.id = item.id;
                        file.logical = item.data_bytes;
                        file.compressed = item.compressed;
                        uint64_t block_cursor = 0;
                        if (!extents_from(
                                load_fork(data + k_data_fork).extents,
                                block_cursor,
                                file.spans,
                                total_blocks,
                                error
                            ))
                            return false;
                        body_of.emplace(item.id, bodies.size());
                        bodies.push_back(std::move(file));
                    } else if (by_id.find(item.id) != by_id.end()) {
                        fail(error, "The catalogue could not be read");
                        return false;
                    }
                    by_id.emplace(item.id, catalog.size());
                    catalog.push_back(std::move(item));
                } else if (type == k_folder_thread || type == k_file_thread) {
                    if (data_bytes < 10) {
                        fail(error, "The catalogue record is truncated");
                        return false;
                    }
                    const uint16_t thread_units = load_be16(data + 8);
                    std::string thread_name;
                    if (data_bytes < 10u + static_cast<std::size_t>(thread_units) * 2 ||
                        !decode_name(data + 10, thread_units, data_bytes - 10, thread_name)) {
                        fail(error, "The catalogue record is truncated");
                        return false;
                    }
                    if (catalog.size() >= k_max_items) {
                        fail(error, "The catalogue could not be read");
                        return false;
                    }
                    CatalogItem item;
                    item.kind = type == k_folder_thread ? CatalogItem::Kind::folder_thread
                                                        : CatalogItem::Kind::file_thread;
                    item.id = parent;
                    item.parent = load_be32(data + 4);
                    item.name = std::move(thread_name);
                    catalog.push_back(std::move(item));
                } else {
                    fail(error, "The catalogue could not be read");
                    return false;
                }
            } else {
                if (key_length < 10 || data_bytes < 64) {
                    fail(error, "The extents record is truncated");
                    return false;
                }
                Overflow row;
                row.fork = body[0];
                row.file_id = load_be32(body + 2);
                row.start_block = load_be32(body + 6);
                if (row.fork != k_data_fork_type && row.fork != k_resource_fork_type) {
                    fail(error, "The extents could not be read");
                    return false;
                }
                for (int extent = 0; extent < 8; ++extent) {
                    const uint8_t* encoded = data + static_cast<std::size_t>(extent) * 8;
                    row.extents[extent].start = load_be32(encoded);
                    row.extents[extent].count = load_be32(encoded + 4);
                    if (row.extents[extent].count != 0 && row.extents[extent].start != 0 &&
                        (row.extents[extent].start > total_blocks ||
                         row.extents[extent].count > total_blocks - row.extents[extent].start)) {
                        fail(error, "The extents could not be read");
                        return false;
                    }
                }
                overflow.push_back(row);
            }
        }
        index = forward;
    }
    return true;
}

bool Volume::State::parse(std::string* error) {
    if (volume_bytes < k_header_offset + k_header_bytes) {
        fail(error, "The disk image is not an HFS volume");
        return false;
    }
    uint8_t header[k_header_bytes];
    if (!read_at(k_header_offset, header, k_header_bytes)) {
        fail(error, "The disk image could not be read");
        return false;
    }
    const uint16_t signature = load_be16(header);
    const uint16_t version = load_be16(header + 2);
    if ((signature != k_hfs_plus && signature != k_hfsx) || (version != 4 && version != 5)) {
        fail(error, "The disk image is not an HFS volume");
        return false;
    }
    block_size = load_be32(header + 40);
    total_blocks = load_be32(header + 44);
    uint64_t volume_used = 0;
    if (!power_of_two(block_size) || block_size < k_min_block || block_size > k_max_block ||
        total_blocks == 0 || !mul_u64(total_blocks, block_size, volume_used) ||
        volume_used > volume_bytes) {
        fail(error, "The disk image is not an HFS volume");
        return false;
    }
    const Fork catalog_fork = load_fork(header + 272);
    const Fork extents_fork = load_fork(header + 192);
    std::vector<Overflow> overflow;
    if (extents_fork.logical != 0 && !walk_tree(extents_fork, false, overflow, error))
        return false;
    if (!walk_tree(catalog_fork, true, overflow, error))
        return false;
    std::sort(overflow.begin(), overflow.end(), [](const Overflow& left, const Overflow& right) {
        if (left.file_id != right.file_id)
            return left.file_id < right.file_id;
        if (left.fork != right.fork)
            return left.fork < right.fork;
        return left.start_block < right.start_block;
    });
    for (FileBody& body : bodies) {
        uint64_t block_cursor = 0;
        if (!body.spans.empty()) {
            const Span& last = body.spans.back();
            if (!add_u64(last.logical_block, last.count, block_cursor)) {
                fail(error, "The catalogue could not be read");
                return false;
            }
        }
        for (const Overflow& row : overflow) {
            if (row.file_id != body.id || row.fork != k_data_fork_type)
                continue;
            if (row.start_block < block_cursor) {
                fail(error, "The catalogue could not be read");
                return false;
            }
            if (row.start_block > block_cursor) {
                Span hole;
                hole.logical_block = block_cursor;
                hole.count = row.start_block - block_cursor;
                hole.hole = true;
                body.spans.push_back(hole);
                block_cursor = row.start_block;
            }
            if (!extents_from(row.extents, block_cursor, body.spans, total_blocks, error))
                return false;
        }
    }
    for (const CatalogItem& item : catalog) {
        if (item.kind != CatalogItem::Kind::folder && item.kind != CatalogItem::Kind::file)
            continue;
        uint32_t parent = item.parent;
        for (int hop = 0;
             hop < 1024 && parent != k_root_id && parent != k_root_parent && parent != 0;
             ++hop) {
            const auto found = by_id.find(parent);
            if (found == by_id.end() || catalog[found->second].kind != CatalogItem::Kind::folder) {
                fail(error, "The catalogue could not be read");
                return false;
            }
            parent = catalog[found->second].parent;
            if (hop == 1023) {
                fail(error, "The catalogue could not be read");
                return false;
            }
        }
    }
    return true;
}

bool Volume::State::locate(uint64_t& offset, uint64_t& bytes, std::string* error) const {
    auto read_disk = [&](uint64_t at, uint8_t* out, std::size_t count) {
        if (outer.read == nullptr || at > disk_bytes ||
            static_cast<uint64_t>(count) > disk_bytes - at)
            return false;
        return outer.read(outer.context, at, out, count);
    };
    if (disk_bytes >= k_header_offset + 2) {
        uint8_t mark[2];
        if (read_disk(k_header_offset, mark, 2)) {
            const uint16_t signature = load_be16(mark);
            if (signature == k_hfs_plus || signature == k_hfsx) {
                offset = 0;
                bytes = disk_bytes;
                return true;
            }
        }
    }
    if (disk_bytes >= 512 + 92) {
        uint8_t gpt[92];
        if (!read_disk(512, gpt, sizeof gpt)) {
            fail(error, "The disk image could not be read");
            return false;
        }
        if (std::memcmp(gpt, "EFI PART", 8) == 0) {
            const uint32_t header_size = load_le32(gpt + 12);
            if (header_size < 92) {
                fail(error, "The disk image is not an HFS volume");
                return false;
            }
            const uint64_t entry_lba = load_le64(gpt + 72);
            const uint32_t entry_count = load_le32(gpt + 80);
            const uint32_t entry_size = load_le32(gpt + 84);
            if (entry_count > 4096 || entry_size < 128 || entry_size > 4096) {
                fail(error, "The disk image is not an HFS volume");
                return false;
            }
            bool saw_apfs = false;
            std::vector<uint8_t> entry(entry_size);
            for (uint32_t index = 0; index < entry_count; ++index) {
                uint64_t entry_at = 0;
                uint64_t entry_off = 0;
                if (!mul_u64(entry_lba, 512, entry_at) || !mul_u64(index, entry_size, entry_off) ||
                    !add_u64(entry_at, entry_off, entry_at) ||
                    !read_disk(entry_at, entry.data(), entry.size()))
                    continue;
                if (same_bytes(entry.data(), k_apfs_guid, 16))
                    saw_apfs = true;
                const uint64_t start = load_le64(entry.data() + 32);
                const uint64_t end = load_le64(entry.data() + 40);
                if (end < start)
                    continue;
                uint64_t blocks = 0;
                uint64_t byte_off = 0;
                uint64_t byte_len = 0;
                if (!add_u64(end - start, 1, blocks) || !mul_u64(start, 512, byte_off) ||
                    !mul_u64(blocks, 512, byte_len) || byte_off > disk_bytes ||
                    byte_len > disk_bytes - byte_off)
                    continue;
                if (byte_len < k_header_offset + 2)
                    continue;
                uint8_t mark[2];
                if (!read_disk(byte_off + k_header_offset, mark, 2))
                    continue;
                const uint16_t signature = load_be16(mark);
                if (signature == k_hfs_plus || signature == k_hfsx) {
                    offset = byte_off;
                    bytes = byte_len;
                    return true;
                }
            }
            fail(
                error,
                saw_apfs ? "The disk image uses APFS, which this reader does not read"
                         : "The disk image is not an HFS volume"
            );
            return false;
        }
    }
    if (disk_bytes >= 4) {
        uint8_t ddr[4];
        if (read_disk(0, ddr, 4) && load_be16(ddr) == 0x4552) {
            const uint16_t block = load_be16(ddr + 2);
            if (power_of_two(block) && block >= 512 && block <= 4096) {
                std::vector<uint8_t> entry(block);
                uint64_t map_at = 0;
                if (mul_u64(1, block, map_at) && map_at < disk_bytes &&
                    static_cast<uint64_t>(block) <= disk_bytes - map_at &&
                    read_disk(map_at, entry.data(), entry.size()) &&
                    load_be16(entry.data()) == 0x504D) {
                    uint32_t count = load_be32(entry.data() + 4);
                    if (count == 0 || count > 4096)
                        count = 1;
                    for (uint32_t index = 0; index < count; ++index) {
                        uint64_t at = 0;
                        if (!mul_u64(static_cast<uint64_t>(index) + 1, block, at) ||
                            at > disk_bytes || static_cast<uint64_t>(block) > disk_bytes - at ||
                            !read_disk(at, entry.data(), entry.size()))
                            continue;
                        if (load_be16(entry.data()) != 0x504D || entry.size() < 80)
                            continue;
                        const bool hfsx = std::memcmp(entry.data() + 48, "Apple_HFSX", 10) == 0 &&
                                          entry[48 + 10] == 0;
                        const bool hfs = std::memcmp(entry.data() + 48, "Apple_HFS", 9) == 0 &&
                                         entry[48 + 9] == 0;
                        if (!hfsx && !hfs)
                            continue;
                        const uint32_t start = load_be32(entry.data() + 8);
                        const uint32_t blocks = load_be32(entry.data() + 12);
                        uint64_t byte_off = 0;
                        uint64_t byte_len = 0;
                        if (blocks == 0 || !mul_u64(start, block, byte_off) ||
                            !mul_u64(blocks, block, byte_len) || byte_off > disk_bytes ||
                            byte_len > disk_bytes - byte_off || byte_len < k_header_offset + 2)
                            continue;
                        uint8_t mark[2];
                        if (!read_disk(byte_off + k_header_offset, mark, 2))
                            continue;
                        const uint16_t signature = load_be16(mark);
                        if (signature == k_hfs_plus || signature == k_hfsx) {
                            offset = byte_off;
                            bytes = byte_len;
                            return true;
                        }
                    }
                }
            }
        }
    }
    fail(error, "The disk image is not an HFS volume");
    return false;
}

Volume::Volume() = default;

Volume::~Volume() = default;

Volume::Volume(Volume&&) noexcept = default;

Volume& Volume::operator=(Volume&&) noexcept = default;

bool Volume::open(ReadHooks hooks, uint64_t volume_bytes, std::string* error) {
    if (state_ != nullptr)
        return true;
    auto state = std::make_unique<State>();
    state->outer = hooks;
    state->volume_bytes = volume_bytes;
    state->disk_bytes = volume_bytes;
    if (!state->parse(error))
        return false;
    state_ = std::move(state);
    return true;
}

bool Volume::open_disk(ReadHooks hooks, uint64_t disk_bytes, std::string* error) {
    if (state_ != nullptr)
        return true;
    auto state = std::make_unique<State>();
    state->outer = hooks;
    state->disk_bytes = disk_bytes;
    state->volume_bytes = disk_bytes;
    uint64_t offset = 0;
    uint64_t bytes = 0;
    if (!state->locate(offset, bytes, error))
        return false;
    state->partition_offset = offset;
    state->volume_bytes = bytes;
    if (!state->parse(error))
        return false;
    state_ = std::move(state);
    return true;
}

const std::vector<CatalogItem>& Volume::catalog() const {
    static const std::vector<CatalogItem> empty;
    return state_ == nullptr ? empty : state_->catalog;
}

bool Volume::read_file(
    uint32_t id, uint64_t offset, std::span<uint8_t> out, std::string* error
) const {
    if (state_ == nullptr) {
        fail(error, "The file could not be read");
        return false;
    }
    const auto found = state_->body_of.find(id);
    if (found == state_->body_of.end()) {
        fail(error, "The file could not be read");
        return false;
    }
    const FileBody& body = state_->bodies[found->second];
    if (body.compressed) {
        fail(
            error,
            "A file in the disk image is compressed, and this reader does not open compressed files"
        );
        return false;
    }
    if (offset > body.logical || out.size() > body.logical - offset) {
        fail(error, "The file could not be read");
        return false;
    }
    if (out.empty())
        return true;
    return state_->read_spans(body.spans, offset, out, error);
}

} // namespace oa::formats::hfsplus
