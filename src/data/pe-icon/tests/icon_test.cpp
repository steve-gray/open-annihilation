// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Program-file icons: a valid image, a missing resource section, a truncated
// directory, an offset outside that section, a directory loop and an AND
// mask, all from files built in memory. With --data, the installed game's
// program file yields a 32 by 32 icon.

#include "oa/data/pe_icon.hpp"
#include "oa/test/check.hpp"
#include "oa/test/game_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using oa::data::pe_icon::Icon;
using oa::data::pe_icon::read_program_icon;

constexpr uint32_t kRaw = 352;
constexpr uint32_t kResourceRva = 0x1000;
constexpr uint32_t kBitmapHeader = 40;

void store_le16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void store_le32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

/// One image of an icon group: a square uncompressed bitmap and its mask.
struct Image {
    uint16_t id{1};
    uint32_t side{2};
    uint16_t bits{4};
    /// B, G, R of palette entry 1. Entry 0 is black. Used for 1, 4 and 8 bits.
    uint8_t red{0};
    uint8_t green{255};
    uint8_t blue{0};
    /// Set bits of the mask are clear. Bit 0 is the top left of the first byte.
    uint8_t mask{0};
    /// The 32-bit image's own alpha, which the mask replaces.
    uint8_t pixel_alpha{0};
};

uint64_t row_stride(uint32_t width, uint32_t bits) {
    return ((static_cast<uint64_t>(width) * bits + 31U) / 32U) * 4U;
}

std::vector<uint8_t> bitmap_of(const Image& image) {
    const uint32_t colours = image.bits <= 8 ? (1U << image.bits) : 0U;
    const uint64_t colour_stride = row_stride(image.side, image.bits);
    const uint64_t mask_stride = row_stride(image.side, 1);
    const uint64_t size = kBitmapHeader + static_cast<uint64_t>(colours) * 4U +
                          colour_stride * image.side + mask_stride * image.side;
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size), 0);
    store_le32(bytes, 0, kBitmapHeader);
    store_le32(bytes, 4, image.side);
    store_le32(bytes, 8, image.side * 2U);
    store_le16(bytes, 12, 1);
    store_le16(bytes, 14, image.bits);
    if (colours > 1) {
        bytes[kBitmapHeader + 4] = image.blue;
        bytes[kBitmapHeader + 5] = image.green;
        bytes[kBitmapHeader + 6] = image.red;
    }
    uint8_t* colour = bytes.data() + kBitmapHeader + colours * 4U;
    if (image.bits == 32) {
        for (uint32_t row = 0; row < image.side; ++row) {
            uint8_t* pixel = colour + row * colour_stride;
            pixel[0] = image.blue;
            pixel[1] = image.green;
            pixel[2] = image.red;
            pixel[3] = image.pixel_alpha;
        }
    } else if (image.bits == 8) {
        for (uint32_t row = 0; row < image.side; ++row)
            for (uint32_t column = 0; column < image.side; ++column)
                colour[row * colour_stride + column] = 1;
    } else if (image.bits == 4) {
        for (uint32_t row = 0; row < image.side; ++row)
            for (uint32_t column = 0; column < image.side; ++column) {
                uint8_t& byte = colour[row * colour_stride + column / 2U];
                if ((column % 2U) == 0)
                    byte = static_cast<uint8_t>(byte | 0x10U);
                else
                    byte = static_cast<uint8_t>(byte | 0x01U);
            }
    }
    uint8_t* mask = colour + colour_stride * image.side;
    if (image.mask != 0)
        mask[(image.side - 1U) * mask_stride] = image.mask;
    return bytes;
}

void append_directory(std::vector<uint8_t>& bytes, uint16_t ids) {
    const std::size_t at = bytes.size();
    bytes.resize(at + 16, 0);
    store_le16(bytes, at + 14, ids);
}

void append_entry(std::vector<uint8_t>& bytes, uint32_t id, uint32_t target, bool directory) {
    const std::size_t at = bytes.size();
    bytes.resize(at + 8, 0);
    store_le32(bytes, at, id);
    store_le32(bytes, at + 4, directory ? (target | 0x80000000U) : target);
}

void append_data(std::vector<uint8_t>& bytes, uint32_t rva, uint32_t size) {
    const std::size_t at = bytes.size();
    bytes.resize(at + 16, 0);
    store_le32(bytes, at, rva);
    store_le32(bytes, at + 4, size);
}

/// A program file whose resource section holds one icon group of `images`.
std::vector<uint8_t> program_with(const std::vector<Image>& images) {
    std::vector<std::vector<uint8_t>> bitmaps;
    bitmaps.reserve(images.size());
    for (const Image& image : images)
        bitmaps.push_back(bitmap_of(image));

    // Root, one directory and language for the group, and the same for each
    // image. Offsets are filled once every table's place is known.
    std::vector<uint8_t> resources;
    append_directory(resources, static_cast<uint16_t>(1U + images.size()));
    const std::size_t root_entries = resources.size();
    resources.resize(root_entries + (1U + images.size()) * 8U, 0);

    std::vector<std::size_t> type_dirs;
    std::vector<std::size_t> languages;
    std::vector<std::size_t> data_entries;
    const auto add_type = [&](uint16_t id_count) {
        type_dirs.push_back(resources.size());
        append_directory(resources, id_count);
        const std::size_t entries = resources.size();
        resources.resize(entries + static_cast<std::size_t>(id_count) * 8U, 0);
        return entries;
    };
    // Icon images first (type 3), then the group (type 14): id entries are
    // stored in order.
    const std::size_t icon_entries = add_type(static_cast<uint16_t>(images.size()));
    const std::size_t group_entries = add_type(1);
    for (std::size_t index = 0; index < images.size(); ++index) {
        languages.push_back(resources.size());
        append_directory(resources, 1);
        append_entry(resources, 0, 0, false);
    }
    languages.push_back(resources.size());
    append_directory(resources, 1);
    append_entry(resources, 0, 0, false);
    for (std::size_t index = 0; index < images.size() + 1U; ++index) {
        data_entries.push_back(resources.size());
        append_data(resources, 0, 0);
    }
    const std::size_t group_at = resources.size();
    resources.resize(group_at + 6U + images.size() * 14U, 0);
    store_le16(resources, group_at + 2, 1);
    store_le16(resources, group_at + 4, static_cast<uint16_t>(images.size()));
    std::vector<std::size_t> bitmap_at;
    for (std::size_t index = 0; index < images.size(); ++index) {
        const std::size_t entry = group_at + 6U + index * 14U;
        const uint32_t side = images[index].side;
        resources[entry] = side >= 256 ? uint8_t{0} : static_cast<uint8_t>(side);
        resources[entry + 1] = resources[entry];
        store_le16(resources, entry + 6, images[index].bits);
        store_le32(resources, entry + 8, static_cast<uint32_t>(bitmaps[index].size()));
        store_le16(resources, entry + 12, images[index].id);
        bitmap_at.push_back(resources.size());
        resources.insert(resources.end(), bitmaps[index].begin(), bitmaps[index].end());
    }

    const auto point = [&](std::size_t entry, uint32_t id, uint32_t target, bool directory) {
        store_le32(resources, entry, id);
        store_le32(resources, entry + 4, directory ? (target | 0x80000000U) : target);
    };
    point(root_entries, 3, static_cast<uint32_t>(type_dirs[0]), true);
    point(root_entries + 8, 14, static_cast<uint32_t>(type_dirs[1]), true);
    for (std::size_t index = 0; index < images.size(); ++index) {
        point(
            icon_entries + index * 8,
            images[index].id,
            static_cast<uint32_t>(languages[index]),
            true
        );
        point(languages[index] + 16, 0, static_cast<uint32_t>(data_entries[index]), false);
        store_le32(
            resources, data_entries[index], kResourceRva + static_cast<uint32_t>(bitmap_at[index])
        );
        store_le32(
            resources, data_entries[index] + 4, static_cast<uint32_t>(bitmaps[index].size())
        );
    }
    point(group_entries, 1, static_cast<uint32_t>(languages.back()), true);
    point(languages.back() + 16, 0, static_cast<uint32_t>(data_entries.back()), false);
    store_le32(resources, data_entries.back(), kResourceRva + static_cast<uint32_t>(group_at));
    store_le32(resources, data_entries.back() + 4, static_cast<uint32_t>(6U + images.size() * 14U));

    std::vector<uint8_t> image(kRaw + resources.size(), 0);
    image[0] = 'M';
    image[1] = 'Z';
    store_le32(image, 0x3c, 64);
    store_le32(image, 64, 0x00004550U);
    store_le16(image, 70, 1);
    store_le16(image, 84, 224);
    store_le16(image, 88, 0x10b);
    store_le32(image, 180, 16);
    store_le32(image, 200, kResourceRva);
    store_le32(image, 204, static_cast<uint32_t>(resources.size()));
    image[312] = '.';
    image[313] = 'r';
    image[314] = 's';
    image[315] = 'r';
    image[316] = 'c';
    store_le32(image, 320, static_cast<uint32_t>(resources.size()));
    store_le32(image, 324, kResourceRva);
    store_le32(image, 328, static_cast<uint32_t>(resources.size()));
    store_le32(image, 332, kRaw);
    std::copy(resources.begin(), resources.end(), image.begin() + kRaw);
    return image;
}

const uint8_t* pixel_of(const Icon& icon, uint32_t column, uint32_t row) {
    return icon.rgba.data() + (static_cast<std::size_t>(row) * icon.width + column) * 4U;
}

void test_valid_icon() {
    const std::vector<uint8_t> image = program_with({Image{}});
    const std::optional<Icon> icon = read_program_icon(image);
    OA_CHECK(icon.has_value());
    if (!icon)
        return;
    OA_CHECK(icon->width == 2 && icon->height == 2);
    OA_CHECK(icon->rgba.size() == 16);
    const uint8_t* pixel = pixel_of(*icon, 0, 0);
    OA_CHECK(pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0 && pixel[3] == 255);
}

void test_largest_entry() {
    const std::vector<uint8_t> image = program_with({
        Image{1, 4, 8},
        Image{2, 8, 4, 255, 0, 0},
    });
    const std::optional<Icon> icon = read_program_icon(image);
    OA_CHECK(icon.has_value());
    if (!icon)
        return;
    OA_CHECK(icon->width == 8 && icon->height == 8);
    const uint8_t* pixel = pixel_of(*icon, 3, 3);
    OA_CHECK(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
}

void test_missing_resource_section() {
    std::vector<uint8_t> image = program_with({Image{}});
    store_le32(image, 200, 0);
    store_le32(image, 204, 0);
    OA_CHECK(!read_program_icon(image));
    store_le32(image, 180, 2);
    OA_CHECK(!read_program_icon(image));
}

void test_truncated_directory() {
    std::vector<uint8_t> image = program_with({Image{}});
    store_le32(image, 204, 8);
    store_le32(image, 320, 8);
    OA_CHECK(!read_program_icon(image));
}

void test_offset_out_of_range() {
    std::vector<uint8_t> image = program_with({Image{}});
    // The first data entry's address is past the resource section.
    const uint32_t resources = kRaw;
    bool patched = false;
    for (std::size_t at = resources; at + 8 < image.size(); ++at) {
        const uint32_t value = static_cast<uint32_t>(image[at]) |
                               (static_cast<uint32_t>(image[at + 1]) << 8U) |
                               (static_cast<uint32_t>(image[at + 2]) << 16U) |
                               (static_cast<uint32_t>(image[at + 3]) << 24U);
        if (value < kResourceRva || value >= kResourceRva + (image.size() - resources))
            continue;
        store_le32(image, at, kResourceRva + static_cast<uint32_t>(image.size()));
        patched = true;
        break;
    }
    OA_CHECK(patched);
    OA_CHECK(!read_program_icon(image));
}

void test_directory_loop() {
    std::vector<uint8_t> image = program_with({Image{}});
    // The icon group's directory points back at the root.
    store_le32(image, kRaw + 28, 0x80000000U);
    OA_CHECK(!read_program_icon(image));
}

void test_mask() {
    Image clear;
    clear.mask = 0x80;
    const std::optional<Icon> masked = read_program_icon(program_with({clear}));
    OA_CHECK(masked.has_value());
    if (masked) {
        OA_CHECK(pixel_of(*masked, 0, 0)[3] == 0);
        OA_CHECK(pixel_of(*masked, 1, 0)[3] == 255);
    }
    Image wide;
    wide.bits = 32;
    wide.pixel_alpha = 0;
    wide.mask = 0;
    wide.red = 10;
    wide.green = 20;
    wide.blue = 30;
    const std::optional<Icon> opaque = read_program_icon(program_with({wide}));
    OA_CHECK(opaque.has_value());
    if (opaque) {
        const uint8_t* pixel = pixel_of(*opaque, 0, 0);
        OA_CHECK(pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30 && pixel[3] == 255);
    }
    wide.pixel_alpha = 255;
    wide.mask = 0x80;
    const std::optional<Icon> hidden = read_program_icon(program_with({wide}));
    OA_CHECK(hidden.has_value());
    if (hidden)
        OA_CHECK(pixel_of(*hidden, 0, 0)[3] == 0);
}

void test_every_prefix() {
    const std::vector<uint8_t> image = program_with({Image{}});
    OA_CHECK(read_program_icon(image).has_value());
    for (std::size_t length = 0; length < image.size(); ++length) {
        const std::optional<Icon> icon =
            read_program_icon(std::span<const uint8_t>(image.data(), length));
        OA_CHECK(!icon.has_value());
    }
    OA_CHECK(!read_program_icon({}));
}

bool same_word(std::string_view text, std::string_view word) {
    if (text.size() != word.size())
        return false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto left = static_cast<unsigned char>(text[index]);
        const auto right = static_cast<unsigned char>(word[index]);
        if (std::tolower(left) != std::tolower(right))
            return false;
    }
    return true;
}

/// The installed game's program, matched by its stem and its extension apart.
std::optional<std::filesystem::path> program_file(const std::filesystem::path& folder) {
    std::error_code error;
    for (std::filesystem::directory_iterator entry{folder, error}, end; !error && entry != end;
         entry.increment(error)) {
        std::error_code kind;
        if (!entry->is_regular_file(kind) || kind)
            continue;
        const std::filesystem::path path = entry->path();
        std::string extension = path.extension().string();
        if (!extension.empty() && extension.front() == '.')
            extension.erase(extension.begin());
        if (same_word(path.stem().string(), "totala") && same_word(extension, "exe"))
            return path;
    }
    return std::nullopt;
}

void test_installed_game() {
    const std::filesystem::path folder = oa::test::require_game_directory("a program icon");
    const std::optional<std::filesystem::path> program = program_file(folder);
    OA_CHECK(program.has_value());
    if (!program)
        return;
    std::ifstream input{*program, std::ios::binary};
    OA_CHECK(static_cast<bool>(input));
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    input.seekg(0, std::ios::beg);
    OA_CHECK(length > 0);
    if (length <= 0)
        return;
    std::vector<uint8_t> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    OA_CHECK(input.gcount() == static_cast<std::streamsize>(bytes.size()));
    const std::optional<Icon> icon = read_program_icon(bytes);
    OA_CHECK(icon.has_value());
    if (!icon)
        return;
    OA_CHECK(icon->width == 32 && icon->height == 32);
    OA_CHECK(icon->rgba.size() == 32U * 32U * 4U);
    bool clear = false;
    bool opaque = false;
    for (std::size_t at = 3; at < icon->rgba.size(); at += 4) {
        clear = clear || icon->rgba[at] == 0;
        opaque = opaque || icon->rgba[at] == 255;
    }
    OA_CHECK(clear && opaque);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        test_installed_game();
    } else {
        test_valid_icon();
        test_largest_entry();
        test_missing_resource_section();
        test_truncated_directory();
        test_offset_out_of_range();
        test_directory_loop();
        test_mask();
        test_every_prefix();
    }
    return oa::test::check_exit_status();
}
