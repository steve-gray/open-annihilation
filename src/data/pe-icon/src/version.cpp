// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The FileDescription in a program file's version resource. The resource's
// place in the file is formats::pe's lookup; this reads the string tables.
#include "oa/data/pe_icon.hpp"

#include "oa/base/bytes.hpp"
#include "oa/formats/pe.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace oa::data::pe_icon {
namespace {

using base::bytes::load_le16;
namespace pe = oa::formats::pe;

constexpr uint32_t version_type = 16;
constexpr uint64_t block_header_size = 6;
constexpr std::size_t most_blocks = 64;
constexpr int most_depth = 4;

// A version block's place, its key and where its value and children start.
struct Block {
    uint64_t start{};
    uint64_t end{};
    uint16_t value_length{};
    uint16_t kind{};
    uint64_t key{};
    uint64_t key_units{};
    uint64_t value{};
    uint64_t children{};
};

[[nodiscard]] bool fits(uint64_t total, uint64_t offset, uint64_t bytes) noexcept {
    return offset <= total && bytes <= total - offset;
}

[[nodiscard]] uint64_t align4(uint64_t offset) noexcept {
    return (offset + 3U) & ~uint64_t{3U};
}

[[nodiscard]] bool
key_is(std::span<const uint8_t> bytes, const Block& block, std::string_view text) noexcept {
    if (block.key_units != text.size())
        return false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const uint16_t unit = load_le16(bytes.data() + block.key + index * 2U);
        if (unit != static_cast<unsigned char>(text[index]))
            return false;
    }
    return true;
}

// Reads one version block. The value's length is in bytes for the root and
// in words for text. A key that is not terminated, or a value past the
// block, is refused.
[[nodiscard]] std::optional<Block>
read_block(std::span<const uint8_t> bytes, uint64_t start, uint64_t limit) noexcept {
    if (!fits(limit, start, block_header_size))
        return std::nullopt;
    const uint16_t length = load_le16(bytes.data() + start);
    if (length < block_header_size || !fits(limit, start, length))
        return std::nullopt;
    Block block;
    block.start = start;
    block.end = start + length;
    block.value_length = load_le16(bytes.data() + start + 2);
    block.kind = load_le16(bytes.data() + start + 4);
    uint64_t cursor = start + block_header_size;
    block.key = cursor;
    while (cursor + 2 <= block.end) {
        const uint16_t unit = load_le16(bytes.data() + cursor);
        cursor += 2;
        if (unit == 0)
            break;
        if (block.key_units == 256)
            return std::nullopt;
        ++block.key_units;
    }
    if (cursor < start + block_header_size + 2 || load_le16(bytes.data() + cursor - 2) != 0)
        return std::nullopt;
    const uint64_t value = align4(cursor);
    if (value < cursor || value > block.end)
        return std::nullopt;
    const bool root = key_is(bytes, block, "VS_VERSION_INFO");
    const uint64_t value_bytes =
        root || block.kind != 1 ? uint64_t{block.value_length} : uint64_t{block.value_length} * 2U;
    if (!fits(block.end, value, value_bytes))
        return std::nullopt;
    // The value is padded to a 4-byte boundary when the block has room for
    // it. When the value ends on the block's last bytes, that padding
    // belongs to the parent, between this block and the next one.
    uint64_t children = value + value_bytes;
    if ((children & 3U) != 0) {
        const uint64_t aligned = align4(children);
        if (aligned < children)
            return std::nullopt;
        children = aligned <= block.end ? aligned : block.end;
    }
    block.value = value;
    block.children = children;
    return block;
}

// Copies a text value, without a trailing null, into UTF-8. A unit outside
// the basic multilingual plane, or a lone surrogate, is refused.
[[nodiscard]] std::optional<std::string>
text_of(std::span<const uint8_t> bytes, const Block& block) {
    if (block.kind != 1)
        return std::string{};
    uint64_t units = block.value_length;
    while (units > 0 && load_le16(bytes.data() + block.value + (units - 1) * 2U) == 0)
        --units;
    std::string text;
    text.reserve(static_cast<std::size_t>(units));
    for (uint64_t index = 0; index < units; ++index) {
        const uint16_t unit = load_le16(bytes.data() + block.value + index * 2U);
        if (unit >= 0xD800 && unit <= 0xDFFF)
            return std::nullopt;
        if (unit < 0x80) {
            text.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            text.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            text.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else {
            text.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            text.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
    }
    return text;
}

// Walks the string tables. The first FileDescription is kept.
[[nodiscard]] bool collect(
    std::span<const uint8_t> bytes,
    uint64_t start,
    uint64_t limit,
    int depth,
    std::string& description
) {
    if (depth > most_depth)
        return false;
    const std::optional<Block> block = read_block(bytes, start, limit);
    if (!block)
        return false;
    if (description.empty() && key_is(bytes, *block, "FileDescription")) {
        const std::optional<std::string> value = text_of(bytes, *block);
        if (!value)
            return false;
        description = *value;
    }
    uint64_t child = block->children;
    std::size_t seen = 0;
    while (child < block->end) {
        if (block->end - child < block_header_size)
            break;
        // A parent often ends with zero padding so its size is aligned.
        // Those bytes are not another block.
        bool padding = true;
        for (uint64_t at = child; at < block->end; ++at) {
            if (bytes[static_cast<std::size_t>(at)] != 0) {
                padding = false;
                break;
            }
        }
        if (padding)
            break;
        if (++seen > most_blocks)
            return false;
        const std::optional<Block> nested = read_block(bytes, child, block->end);
        if (!nested || nested->end <= child)
            return false;
        if (!collect(bytes, child, block->end, depth + 1, description))
            return false;
        uint64_t next = align4(nested->end);
        if (next < nested->end)
            return false;
        // At most three bytes of padding sit between a block and the
        // parent's end. They are not another block.
        if (next > block->end)
            next = block->end;
        child = next;
    }
    return true;
}

[[nodiscard]] bool version_absent(pe::Error error) noexcept {
    return error == pe::Error::no_resource_directory || error == pe::Error::type_not_found ||
           error == pe::Error::id_not_found || error == pe::Error::language_not_found;
}

} // namespace

FileDescription file_description(std::span<const uint8_t> image) {
    FileDescription result;
    if (image.size() > most_file_bytes) {
        result.text = VersionText::malformed;
        return result;
    }
    const pe::FirstResource version = pe::first_resource(image, version_type);
    if (version.error != pe::Error::none) {
        result.text = version_absent(version.error) ? VersionText::absent : VersionText::malformed;
        return result;
    }
    if (version.range.offset > image.size() ||
        version.range.size > image.size() - version.range.offset) {
        result.text = VersionText::malformed;
        return result;
    }
    const auto bytes = image.subspan(
        static_cast<std::size_t>(version.range.offset), static_cast<std::size_t>(version.range.size)
    );
    std::string description;
    if (bytes.empty() || !collect(bytes, 0, bytes.size(), 0, description)) {
        result.text = VersionText::malformed;
        return result;
    }
    result.text = VersionText::present;
    result.value = std::move(description);
    return result;
}

std::optional<std::size_t>
only_described(std::span<const std::span<const uint8_t>> files, std::string_view description) {
    std::optional<std::size_t> found;
    for (std::size_t index = 0; index < files.size(); ++index) {
        const FileDescription read = file_description(files[index]);
        if (read.text != VersionText::present || read.value != description)
            continue;
        if (found)
            return std::nullopt;
        found = index;
    }
    return found;
}

} // namespace oa::data::pe_icon
