// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The program file's headers, its resource directory and one icon image.
#include "oa/data/pe_icon.hpp"

#include "oa/base/bytes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace oa::data::pe_icon {
namespace {

using base::bytes::load_le16;
using base::bytes::load_le32;

// The DOS header, and the file offset of the PE signature.
constexpr uint64_t dos_header_size = 64;
constexpr uint16_t dos_signature = 0x5a4d;
constexpr uint64_t pe_offset_field = 0x3c;

constexpr uint32_t pe_signature = 0x00004550;
constexpr uint64_t pe_signature_size = 4;
constexpr uint64_t file_header_size = 20;
constexpr uint64_t section_count_field = 2;
constexpr uint64_t optional_size_field = 16;

constexpr uint16_t pe32_magic = 0x10b;
constexpr uint16_t pe32_plus_magic = 0x20b;
constexpr uint64_t pe32_directory_count_field = 92;
constexpr uint64_t pe32_plus_directory_count_field = 108;
constexpr uint64_t directory_count_size = 4;
constexpr uint64_t pe32_directories = 96;
constexpr uint64_t pe32_plus_directories = 112;
constexpr uint64_t directory_size = 8;
constexpr uint32_t resource_directory_index = 2;

constexpr uint64_t section_header_size = 40;
constexpr uint64_t section_virtual_size_field = 8;
constexpr uint64_t section_virtual_address_field = 12;
constexpr uint64_t section_raw_size_field = 16;
constexpr uint64_t section_raw_offset_field = 20;
constexpr uint16_t most_sections = 96;

// A resource directory: named entries, then id entries, then each entry's
// name or id and its target. A target with the high bit set is a
// subdirectory; the rest of the word is an offset from the directory's root.
constexpr uint64_t directory_table_size = 16;
constexpr uint64_t named_count_field = 12;
constexpr uint64_t id_count_field = 14;
constexpr uint64_t directory_entry_size = 8;
constexpr uint64_t entry_target_field = 4;
constexpr uint32_t name_is_string = 0x80000000U;
constexpr uint32_t target_is_directory = 0x80000000U;
constexpr uint64_t data_entry_size = 16;
constexpr uint64_t data_size_field = 4;
constexpr uint32_t most_directory_entries = 4096;
constexpr std::size_t directory_levels = 3;

// Icon resources, and one icon group's directory.
constexpr uint16_t group_icon_type = 14;
constexpr uint16_t icon_type = 3;
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

// Where the resource section lies.
struct Resources {
    std::span<const uint8_t> image{};
    uint32_t rva{};
    uint32_t size{};
    uint64_t file_offset{};
};

// Tests whether `bytes` bytes from `offset` lie within `total`.
[[nodiscard]] bool fits(uint64_t total, uint64_t offset, uint64_t bytes) noexcept {
    return offset <= total && bytes <= total - offset;
}

// The file offset of a section's range, when the range lies in that
// section's bytes in the file.
struct SectionSpan {
    uint64_t file_offset{};
    uint32_t size{};
};

[[nodiscard]] std::optional<SectionSpan> section_span(
    std::span<const uint8_t> image,
    uint64_t section_table,
    uint16_t section_count,
    uint32_t rva,
    uint32_t size
) noexcept {
    if (size == 0)
        return std::nullopt;
    for (uint16_t index = 0; index < section_count; ++index) {
        const uint64_t header = section_table + static_cast<uint64_t>(index) * section_header_size;
        const uint32_t virtual_size = load_le32(image.data() + header + section_virtual_size_field);
        const uint32_t raw_size = load_le32(image.data() + header + section_raw_size_field);
        const uint32_t virtual_address =
            load_le32(image.data() + header + section_virtual_address_field);
        const uint32_t raw_offset = load_le32(image.data() + header + section_raw_offset_field);
        const uint32_t held = virtual_size == 0 ? raw_size : std::min(virtual_size, raw_size);
        if (rva < virtual_address)
            continue;
        const uint64_t into = static_cast<uint64_t>(rva) - virtual_address;
        if (into >= held || static_cast<uint64_t>(size) > held - into)
            continue;
        const uint64_t offset = static_cast<uint64_t>(raw_offset) + into;
        if (!fits(image.size(), offset, size))
            continue;
        return SectionSpan{offset, size};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Resources> locate_resources(std::span<const uint8_t> image) noexcept {
    const uint64_t size = image.size();
    if (size < dos_header_size || load_le16(image.data()) != dos_signature)
        return std::nullopt;
    const uint64_t pe_header = load_le32(image.data() + pe_offset_field);
    if (!fits(size, pe_header, pe_signature_size + file_header_size))
        return std::nullopt;
    if (load_le32(image.data() + pe_header) != pe_signature)
        return std::nullopt;
    const uint64_t file_header = pe_header + pe_signature_size;
    const uint16_t section_count = load_le16(image.data() + file_header + section_count_field);
    if (section_count > most_sections)
        return std::nullopt;
    const uint64_t optional_size = load_le16(image.data() + file_header + optional_size_field);
    const uint64_t optional = file_header + file_header_size;
    if (optional_size < 2 || !fits(size, optional, optional_size))
        return std::nullopt;
    const uint16_t magic = load_le16(image.data() + optional);
    uint64_t count_field = 0;
    uint64_t directories = 0;
    if (magic == pe32_magic) {
        count_field = pe32_directory_count_field;
        directories = pe32_directories;
    } else if (magic == pe32_plus_magic) {
        count_field = pe32_plus_directory_count_field;
        directories = pe32_plus_directories;
    } else {
        return std::nullopt;
    }
    if (!fits(optional_size, count_field, directory_count_size))
        return std::nullopt;
    if (load_le32(image.data() + optional + count_field) <= resource_directory_index)
        return std::nullopt;
    const uint64_t resource_entry = directories + resource_directory_index * directory_size;
    if (!fits(optional_size, resource_entry, directory_size))
        return std::nullopt;
    const uint32_t resource_rva = load_le32(image.data() + optional + resource_entry);
    const uint32_t resource_size = load_le32(image.data() + optional + resource_entry + 4);
    if (resource_rva == 0 || resource_size == 0)
        return std::nullopt;
    const uint64_t section_table = optional + optional_size;
    if (!fits(size, section_table, static_cast<uint64_t>(section_count) * section_header_size))
        return std::nullopt;
    const std::optional<SectionSpan> span =
        section_span(image, section_table, section_count, resource_rva, resource_size);
    if (!span)
        return std::nullopt;
    return Resources{image, resource_rva, span->size, span->file_offset};
}

// The bytes of one resource inside the resource section.
[[nodiscard]] std::optional<std::span<const uint8_t>>
resource_bytes(const Resources& resources, uint16_t type, std::optional<uint16_t> id) noexcept {
    const uint64_t extent = resources.size;
    const uint8_t* bytes = resources.image.data() + resources.file_offset;
    uint32_t table = 0;
    std::array<uint32_t, directory_levels> path{};
    for (std::size_t level = 0; level < directory_levels; ++level) {
        path[level] = table;
        if (!fits(extent, table, directory_table_size))
            return std::nullopt;
        const uint32_t named = load_le16(bytes + table + named_count_field);
        const uint32_t ids = load_le16(bytes + table + id_count_field);
        const uint32_t count = named + ids;
        if (count > most_directory_entries)
            return std::nullopt;
        const uint64_t entries = static_cast<uint64_t>(table) + directory_table_size;
        if (!fits(extent, entries, static_cast<uint64_t>(count) * directory_entry_size))
            return std::nullopt;
        std::optional<uint32_t> target;
        bool directory = false;
        // The first icon group, and any group's language, are the first id
        // entry. A named icon is looked up by its id.
        const bool take_first = level == 2 || (level == 1 && !id.has_value());
        const uint32_t wanted = level == 0 ? type : (id ? *id : 0U);
        if (take_first && named >= count)
            return std::nullopt;
        const uint32_t begin = named;
        const uint32_t end = take_first ? named + 1U : count;
        for (uint32_t index = begin; index < end; ++index) {
            const uint64_t entry = entries + static_cast<uint64_t>(index) * directory_entry_size;
            const uint32_t name = load_le32(bytes + entry);
            if ((name & name_is_string) != 0)
                continue;
            if (!take_first && name != wanted)
                continue;
            const uint32_t raw = load_le32(bytes + entry + entry_target_field);
            directory = (raw & target_is_directory) != 0;
            target = raw & ~target_is_directory;
            break;
        }
        if (!target)
            return std::nullopt;
        if (level + 1 < directory_levels) {
            if (!directory)
                return std::nullopt;
            for (std::size_t seen = 0; seen <= level; ++seen)
                if (path[seen] == *target)
                    return std::nullopt;
            table = *target;
            continue;
        }
        if (directory || !fits(extent, *target, data_entry_size))
            return std::nullopt;
        const uint32_t data_rva = load_le32(bytes + *target);
        const uint32_t data_size = load_le32(bytes + *target + data_size_field);
        if (data_rva < resources.rva)
            return std::nullopt;
        const uint64_t into = static_cast<uint64_t>(data_rva) - resources.rva;
        if (into > resources.size || static_cast<uint64_t>(data_size) > resources.size - into)
            return std::nullopt;
        const uint64_t file = resources.file_offset + into;
        if (!fits(resources.image.size(), file, data_size))
            return std::nullopt;
        return resources.image.subspan(static_cast<std::size_t>(file), data_size);
    }
    return std::nullopt;
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
                    const uint8_t byte = colour_row[column / 2U];
                    index = (column % 2U) == 0 ? (byte >> 4U) : (byte & 0x0fU);
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
    const std::optional<Resources> resources = locate_resources(image);
    if (!resources)
        return std::nullopt;
    const std::optional<std::span<const uint8_t>> group =
        resource_bytes(*resources, group_icon_type, std::nullopt);
    if (!group)
        return std::nullopt;
    const std::optional<GroupChoice> chosen = largest_entry(*group);
    if (!chosen)
        return std::nullopt;
    const std::optional<std::span<const uint8_t>> icon =
        resource_bytes(*resources, icon_type, chosen->id);
    if (!icon)
        return std::nullopt;
    return decode_dib(*icon);
}

} // namespace oa::data::pe_icon
