// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// One icon image: the group's largest entry, then its bitmap and AND mask.
// Where the bytes lie is formats::pe's lookup.
#include "oa/data/pe_icon.hpp"

#include "oa/base/bytes.hpp"
#include "oa/formats/pe.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace oa::data::pe_icon {
namespace {

using base::bytes::load_le16;
using base::bytes::load_le32;
namespace pe = oa::formats::pe;

// Icon resources, and one icon group's directory.
constexpr uint32_t group_icon_type = 14;
constexpr uint32_t icon_type = 3;
constexpr uint64_t group_header_size = 6;
constexpr uint64_t group_entry_size = 14;
constexpr uint16_t group_type_icon = 1;
constexpr uint16_t most_group_entries = 32;
constexpr uint32_t icon_edge = 256;

// A device-independent bitmap. An icon's height counts the colour rows and
// the mask rows, so the picture is half of it. Rows run from the bottom.
constexpr uint32_t bitmap_header_size = 40;
constexpr uint32_t bitmap_rgb = 0;
constexpr uint64_t bitmap_width_field = 4;
constexpr uint64_t bitmap_height_field = 8;
constexpr uint64_t bitmap_planes_field = 12;
constexpr uint64_t bitmap_bit_count_field = 14;
constexpr uint64_t bitmap_compression_field = 16;
constexpr uint64_t bitmap_colours_field = 32;
constexpr uint32_t colour_bytes = 4;

// Tests whether `bytes` bytes from `offset` lie within `total`.
[[nodiscard]] bool fits(uint64_t total, uint64_t offset, uint64_t bytes) noexcept {
    return offset <= total && bytes <= total - offset;
}

// The bytes a lookup names, when they lie inside the file.
[[nodiscard]] std::optional<std::span<const uint8_t>>
bytes_of(std::span<const uint8_t> image, pe::FileRange range) noexcept {
    if (!fits(image.size(), range.offset, range.size))
        return std::nullopt;
    return image.subspan(
        static_cast<std::size_t>(range.offset), static_cast<std::size_t>(range.size)
    );
}

struct GroupChoice {
    uint16_t id{};
    uint32_t area{};
    uint16_t bits{};
};

[[nodiscard]] std::optional<GroupChoice> largest_entry(std::span<const uint8_t> group) noexcept {
    if (group.size() < group_header_size)
        return std::nullopt;
    if (load_le16(group.data() + 2) != group_type_icon)
        return std::nullopt;
    const uint16_t count = load_le16(group.data() + 4);
    if (count == 0 || count > most_group_entries)
        return std::nullopt;
    if (!fits(group.size(), group_header_size, static_cast<uint64_t>(count) * group_entry_size))
        return std::nullopt;
    std::optional<GroupChoice> best;
    for (uint16_t index = 0; index < count; ++index) {
        const uint64_t entry = group_header_size + static_cast<uint64_t>(index) * group_entry_size;
        const uint32_t width = group[entry] == 0 ? icon_edge : group[entry];
        const uint32_t height = group[entry + 1] == 0 ? icon_edge : group[entry + 1];
        const uint16_t bits = load_le16(group.data() + entry + 6);
        const uint16_t id = load_le16(group.data() + entry + 12);
        const uint32_t area = width * height;
        if (!best || area > best->area || (area == best->area && bits > best->bits))
            best = GroupChoice{id, area, bits};
    }
    return best;
}

[[nodiscard]] uint64_t row_stride(uint32_t width, uint32_t bits) noexcept {
    return ((static_cast<uint64_t>(width) * bits + 31U) / 32U) * 4U;
}

[[nodiscard]] bool masked_clear(const uint8_t* row, uint32_t column) noexcept {
    const uint8_t byte = row[column / 8U];
    const uint8_t shift = static_cast<uint8_t>(7U - (column % 8U));
    return ((byte >> shift) & 1U) != 0;
}

[[nodiscard]] std::optional<Icon> decode_dib(std::span<const uint8_t> bytes) {
    if (bytes.size() < bitmap_header_size || load_le32(bytes.data()) != bitmap_header_size)
        return std::nullopt;
    const int32_t stored_width = static_cast<int32_t>(load_le32(bytes.data() + bitmap_width_field));
    const int32_t stored_height =
        static_cast<int32_t>(load_le32(bytes.data() + bitmap_height_field));
    if (stored_width <= 0 || stored_height <= 0 || (stored_height % 2) != 0)
        return std::nullopt;
    const auto width = static_cast<uint32_t>(stored_width);
    const auto height = static_cast<uint32_t>(stored_height / 2);
    if (width > icon_edge || height > icon_edge || height == 0)
        return std::nullopt;
    if (load_le16(bytes.data() + bitmap_planes_field) != 1)
        return std::nullopt;
    const uint16_t bits = load_le16(bytes.data() + bitmap_bit_count_field);
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32)
        return std::nullopt;
    if (load_le32(bytes.data() + bitmap_compression_field) != bitmap_rgb)
        return std::nullopt;
    uint32_t colours = 0;
    if (bits <= 8) {
        colours = load_le32(bytes.data() + bitmap_colours_field);
        const uint32_t room = 1U << bits;
        if (colours == 0)
            colours = room;
        if (colours > room)
            return std::nullopt;
    }
    const uint64_t palette = static_cast<uint64_t>(colours) * colour_bytes;
    const uint64_t colour_stride = row_stride(width, bits);
    const uint64_t mask_stride = row_stride(width, 1);
    const uint64_t pixels = palette + colour_stride * height + mask_stride * height;
    if (!fits(bytes.size(), bitmap_header_size, pixels))
        return std::nullopt;
    const uint8_t* table = bytes.data() + bitmap_header_size;
    const uint8_t* colours_at = table + palette;
    const uint8_t* mask_at = colours_at + colour_stride * height;
    Icon icon;
    icon.width = width;
    icon.height = height;
    icon.rgba.assign(static_cast<std::size_t>(width) * height * colour_bytes, 0);
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* colour_row = colours_at + (height - 1U - row) * colour_stride;
        const uint8_t* mask_row = mask_at + (height - 1U - row) * mask_stride;
        uint8_t* out = icon.rgba.data() + static_cast<std::size_t>(row) * width * colour_bytes;
        for (uint32_t column = 0; column < width; ++column, out += colour_bytes) {
            uint8_t red = 0;
            uint8_t green = 0;
            uint8_t blue = 0;
            if (bits == 1 || bits == 4 || bits == 8) {
                uint32_t index = 0;
                if (bits == 8) {
                    index = colour_row[column];
                } else if (bits == 4) {
                    const uint8_t packed = colour_row[column / 2U];
                    index = (column % 2U) == 0 ? (packed >> 4U) : (packed & 0x0fU);
                } else {
                    index = masked_clear(colour_row, column) ? 1U : 0U;
                }
                if (index >= colours)
                    return std::nullopt;
                const uint8_t* colour = table + static_cast<std::size_t>(index) * colour_bytes;
                blue = colour[0];
                green = colour[1];
                red = colour[2];
            } else if (bits == 24) {
                const uint8_t* colour = colour_row + static_cast<std::size_t>(column) * 3U;
                blue = colour[0];
                green = colour[1];
                red = colour[2];
            } else {
                const uint8_t* colour = colour_row + static_cast<std::size_t>(column) * 4U;
                blue = colour[0];
                green = colour[1];
                red = colour[2];
            }
            out[0] = red;
            out[1] = green;
            out[2] = blue;
            out[3] = masked_clear(mask_row, column) ? uint8_t{0} : uint8_t{255};
        }
    }
    return icon;
}

} // namespace

std::optional<Icon> read_program_icon(std::span<const uint8_t> image) {
    if (image.size() > most_file_bytes)
        return std::nullopt;
    const pe::FirstResource group = pe::first_resource(image, group_icon_type);
    if (group.error != pe::Error::none)
        return std::nullopt;
    const std::optional<std::span<const uint8_t>> group_bytes = bytes_of(image, group.range);
    if (!group_bytes)
        return std::nullopt;
    const std::optional<GroupChoice> chosen = largest_entry(*group_bytes);
    if (!chosen)
        return std::nullopt;
    const pe::ResourceLookup icon_resource =
        pe::find_resource(image, pe::NumericResource{icon_type, chosen->id, false, 0});
    if (icon_resource.error != pe::Error::none)
        return std::nullopt;
    const std::optional<std::span<const uint8_t>> icon = bytes_of(image, icon_resource.range);
    if (!icon)
        return std::nullopt;
    return decode_dib(*icon);
}

} // namespace oa::data::pe_icon
