// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Decoding the window icon's PNG file with the engine's PNG reader.
#include "oa/app/window_icon.hpp"

#include "oa/formats/png.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

// Bits per sample of the icon's pixels.
constexpr uint8_t icon_bit_depth = 8;

// Offset of a pixel's alpha byte.
constexpr std::size_t alpha_byte = 3;

/// Keeps the first error the PNG reader reports; warnings are left out.
///
/// @param user the std::string that keeps it
/// @param text the error
void keep_first_error(void* user, const char* text) {
    auto& error = *static_cast<std::string*>(user);
    if (error.empty())
        error = text != nullptr && *text != '\0' ? text : "unreadable PNG";
}

/// Ignores a warning of the PNG reader.
void ignore_warning(void*, const char*) {
}

} // namespace

bool decode_window_icon(std::span<const uint8_t> png, WindowIcon& icon, std::string& error) {
    namespace png_format = oa::formats::png;
    icon = WindowIcon{};
    error.clear();
    if (!png_format::has_signature(png)) {
        error = "not a PNG file";
        return false;
    }
    const png_format::Messages messages{&error, ignore_warning, keep_first_error};
    png_format::Info info;
    if (!png_format::read_info(png, messages, &info))
        return false;
    const png_format::Header& header = info.header;
    if (header.color_type != png_format::ColorType::rgb_alpha ||
        header.bit_depth != icon_bit_depth) {
        error = "not an 8-bit RGBA image";
        return false;
    }
    if (header.width != window_icon_size || header.height != window_icon_size) {
        error = "not " + std::to_string(window_icon_size) + " by " +
                std::to_string(window_icon_size) + " pixels";
        return false;
    }
    const std::size_t row = png_format::row_bytes(header, {});
    std::vector<uint8_t> pixels(row * header.height);
    const png_format::Progress progress = png_format::read_image(png, info, {}, messages, pixels);
    if (progress == png_format::Progress::none || !error.empty()) {
        if (error.empty())
            error = "the image data ends early";
        return false;
    }
    icon.width = header.width;
    icon.height = header.height;
    icon.pixels = std::move(pixels);
    return true;
}

WindowIcon visible_part(const WindowIcon& icon) {
    const std::size_t row_bytes = std::size_t{icon.width} * window_icon_pixel_bytes;
    if (icon.width == 0 || icon.height == 0 || icon.pixels.size() / row_bytes < icon.height)
        return {};
    uint32_t left = icon.width;
    uint32_t top = icon.height;
    uint32_t right = 0;
    uint32_t bottom = 0;
    for (uint32_t y = 0; y < icon.height; ++y)
        for (uint32_t x = 0; x < icon.width; ++x)
            if (icon.pixels
                    [y * row_bytes + std::size_t{x} * window_icon_pixel_bytes + alpha_byte] != 0) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
    if (left >= right || top >= bottom)
        return {};
    WindowIcon part;
    part.width = right - left;
    part.height = bottom - top;
    const std::size_t part_row_bytes = std::size_t{part.width} * window_icon_pixel_bytes;
    part.pixels.reserve(part_row_bytes * part.height);
    for (uint32_t y = top; y < bottom; ++y) {
        const auto first =
            icon.pixels.begin() + static_cast<std::ptrdiff_t>(
                                      y * row_bytes + std::size_t{left} * window_icon_pixel_bytes
                                  );
        part.pixels.insert(
            part.pixels.end(), first, first + static_cast<std::ptrdiff_t>(part_row_bytes)
        );
    }
    return part;
}

WindowIcon area_scaled(const WindowIcon& icon, uint32_t side) {
    const std::size_t source_row = std::size_t{icon.width} * window_icon_pixel_bytes;
    if (side == 0 || icon.width == 0 || icon.height == 0 || source_row == 0 ||
        icon.pixels.size() / source_row < icon.height)
        return {};
    if (side == icon.width && side == icon.height)
        return icon;
    WindowIcon scaled;
    scaled.width = side;
    scaled.height = side;
    scaled.pixels.assign(std::size_t{side} * side * window_icon_pixel_bytes, 0);
    const uint64_t source_columns = icon.width;
    const uint64_t source_rows = icon.height;
    const uint64_t dest_side = side;
    for (uint32_t row = 0; row < side; ++row) {
        const uint64_t shown_row = row;
        const uint64_t first_row = shown_row * source_rows / dest_side;
        const uint64_t end_row = std::max(first_row + 1, (shown_row + 1) * source_rows / dest_side);
        for (uint32_t column = 0; column < side; ++column) {
            const uint64_t shown_column = column;
            const uint64_t first_column = shown_column * source_columns / dest_side;
            const uint64_t end_column =
                std::max(first_column + 1, (shown_column + 1) * source_columns / dest_side);
            uint64_t alpha_sum = 0;
            uint64_t red_sum = 0;
            uint64_t green_sum = 0;
            uint64_t blue_sum = 0;
            for (uint64_t from_row = first_row; from_row < end_row; ++from_row) {
                const uint8_t* from =
                    icon.pixels.data() +
                    static_cast<std::size_t>(from_row * source_columns + first_column) *
                        window_icon_pixel_bytes;
                for (uint64_t from_column = first_column; from_column < end_column; ++from_column) {
                    const uint64_t alpha = from[alpha_byte];
                    alpha_sum += alpha;
                    red_sum += static_cast<uint64_t>(from[0]) * alpha;
                    green_sum += static_cast<uint64_t>(from[1]) * alpha;
                    blue_sum += static_cast<uint64_t>(from[2]) * alpha;
                    from += window_icon_pixel_bytes;
                }
            }
            if (alpha_sum == 0)
                continue;
            const uint64_t count = (end_row - first_row) * (end_column - first_column);
            uint8_t* to = scaled.pixels.data() +
                          (static_cast<std::size_t>(row) * side + column) * window_icon_pixel_bytes;
            to[0] = static_cast<uint8_t>((red_sum + alpha_sum / 2) / alpha_sum);
            to[1] = static_cast<uint8_t>((green_sum + alpha_sum / 2) / alpha_sum);
            to[2] = static_cast<uint8_t>((blue_sum + alpha_sum / 2) / alpha_sum);
            to[3] = static_cast<uint8_t>((alpha_sum + count / 2) / count);
        }
    }
    return scaled;
}

} // namespace oa::app
