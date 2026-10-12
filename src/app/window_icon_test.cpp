// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The embedded app, settings and mods icons each decode to 256 by 256 RGBA
// pixels, transparent in the corners and opaque in the middle, and files
// that are not such an image are refused with a reason. Each icon's visible
// part leaves out only a wholly clear margin, and the three share one size
// so the buttons that draw them match. An area-scaled copy averages the
// pixels that land on each result pixel, and a clear corner stays clear.
#include "oa/app/window_icon.hpp"
#include "oa/formats/png.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

using namespace oa::app;

// Offset of the alpha byte in a pixel.
constexpr uint32_t alpha_byte = 3;

uint8_t alpha_at(const WindowIcon& icon, uint32_t x, uint32_t y) {
    return icon.pixels
        [(static_cast<std::size_t>(y) * icon.width + x) * window_icon_pixel_bytes + alpha_byte];
}

std::vector<uint8_t> encoded(oa::formats::png::ColorType color_type, uint32_t size) {
    oa::formats::png::Header header;
    header.width = size;
    header.height = size;
    header.bit_depth = 8;
    header.color_type = color_type;
    const std::vector<uint8_t> rows(oa::formats::png::row_bytes(header, {}) * size, 0x80);
    std::vector<uint8_t> file;
    CHECK(oa::formats::png::write(oa::formats::png::Image{header, {}, rows}, &file));
    return file;
}

void test_embedded_icon(std::span<const uint8_t> png) {
    WindowIcon icon;
    std::string error;
    CHECK(decode_window_icon(png, icon, error));
    CHECK(error.empty());
    CHECK(icon.width == window_icon_size && icon.height == window_icon_size);
    CHECK(
        icon.pixels.size() ==
        std::size_t{window_icon_size} * window_icon_size * window_icon_pixel_bytes
    );
    if (icon.pixels.size() !=
        std::size_t{window_icon_size} * window_icon_size * window_icon_pixel_bytes)
        return;
    const uint32_t last = window_icon_size - 1;
    CHECK(alpha_at(icon, 0, 0) == 0 && alpha_at(icon, last, 0) == 0);
    CHECK(alpha_at(icon, 0, last) == 0 && alpha_at(icon, last, last) == 0);
    CHECK(alpha_at(icon, window_icon_size / 2, window_icon_size / 2) == 0xff);
}

void test_refused_files() {
    WindowIcon icon;
    std::string error;
    const std::vector<uint8_t> not_png = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
    CHECK(
        !decode_window_icon(not_png, icon, error) && error == "not a PNG file" &&
        icon.pixels.empty()
    );

    const auto embedded = window_icon_png();
    const std::vector<uint8_t> cut(embedded.begin(), embedded.begin() + embedded.size() / 2);
    CHECK(!decode_window_icon(cut, icon, error) && !error.empty() && icon.pixels.empty());

    const auto rgb = encoded(oa::formats::png::ColorType::rgb, window_icon_size);
    CHECK(!decode_window_icon(rgb, icon, error) && error == "not an 8-bit RGBA image");

    const auto small = encoded(oa::formats::png::ColorType::rgb_alpha, 16);
    CHECK(!decode_window_icon(small, icon, error) && error == "not 256 by 256 pixels");

    const auto square = encoded(oa::formats::png::ColorType::rgb_alpha, window_icon_size);
    CHECK(decode_window_icon(square, icon, error) && error.empty());
    CHECK(
        icon.pixels.size() ==
        std::size_t{window_icon_size} * window_icon_size * window_icon_pixel_bytes
    );
}

} // namespace

/// Tells whether a row of an icon holds a pixel that is not wholly transparent.
bool row_visible(const WindowIcon& icon, uint32_t y) {
    for (uint32_t x = 0; x < icon.width; ++x)
        if (alpha_at(icon, x, y) != 0)
            return true;
    return false;
}

/// Tells whether a column of an icon holds a pixel that is not wholly transparent.
bool column_visible(const WindowIcon& icon, uint32_t x) {
    for (uint32_t y = 0; y < icon.height; ++y)
        if (alpha_at(icon, x, y) != 0)
            return true;
    return false;
}

void test_visible_part() {
    WindowIcon icon;
    std::string error;
    CHECK(decode_window_icon(window_icon_png(), icon, error));
    const WindowIcon part = visible_part(icon);
    // The corners are clear. A pixel on an edge may still be visible, so
    // the visible part can be the whole icon, and each of its four edges
    // touches something visible.
    CHECK(part.width > window_icon_size / 2 && part.width <= window_icon_size);
    CHECK(part.height > window_icon_size / 2 && part.height <= window_icon_size);
    CHECK(part.pixels.size() == std::size_t{part.width} * part.height * window_icon_pixel_bytes);
    if (part.width != 0 && part.height != 0) {
        CHECK(row_visible(part, 0) && row_visible(part, part.height - 1));
        CHECK(column_visible(part, 0) && column_visible(part, part.width - 1));
        CHECK(alpha_at(part, part.width / 2, part.height / 2) == 0xff);
    }

    // One faint pixel in a clear 4 by 3 icon is its visible part, as it was.
    WindowIcon faint;
    faint.width = 4;
    faint.height = 3;
    faint.pixels.assign(std::size_t{4} * 3 * window_icon_pixel_bytes, 0);
    const std::size_t at = (std::size_t{1} * 4 + 2) * window_icon_pixel_bytes;
    faint.pixels[at] = 10;
    faint.pixels[at + 1] = 20;
    faint.pixels[at + 2] = 30;
    faint.pixels[at + alpha_byte] = 1;
    const WindowIcon dot = visible_part(faint);
    CHECK(dot.width == 1 && dot.height == 1);
    CHECK((dot.pixels == std::vector<uint8_t>{10, 20, 30, 1}));
    // A second pixel, in the top left corner, widens it to the rectangle
    // that holds both.
    faint.pixels[alpha_byte] = 0xff;
    const WindowIcon both = visible_part(faint);
    CHECK(both.width == 3 && both.height == 2 && both.pixels.size() == 3 * 2 * 4);
    CHECK(alpha_at(both, 0, 0) == 0xff && alpha_at(both, 2, 1) == 1);

    // A clear icon, an empty one and one short of pixels have none.
    WindowIcon clear;
    clear.width = 2;
    clear.height = 2;
    clear.pixels.assign(std::size_t{2} * 2 * window_icon_pixel_bytes, 0x80);
    for (std::size_t pixel = 0; pixel < 4; ++pixel)
        clear.pixels[pixel * window_icon_pixel_bytes + alpha_byte] = 0;
    CHECK(visible_part(clear).pixels.empty() && visible_part(clear).width == 0);
    CHECK(visible_part(WindowIcon{}).pixels.empty());
    WindowIcon short_of_pixels = faint;
    short_of_pixels.pixels.resize(short_of_pixels.pixels.size() - 1);
    CHECK(visible_part(short_of_pixels).pixels.empty());
}

/// Checks that the settings icon's and the mods icon's visible parts are as
/// large as the app icon's, so the buttons beside each other show their
/// pictures at one size.
void test_settings_icon_part() {
    WindowIcon logo;
    WindowIcon gears;
    WindowIcon bays;
    std::string error;
    CHECK(decode_window_icon(window_icon_png(), logo, error));
    CHECK(decode_window_icon(settings_icon_png(), gears, error));
    CHECK(decode_window_icon(mods_icon_png(), bays, error));
    const WindowIcon logo_part = visible_part(logo);
    const WindowIcon gears_part = visible_part(gears);
    const WindowIcon bays_part = visible_part(bays);
    CHECK(gears_part.width != 0 && gears_part.height != 0);
    CHECK(bays_part.width != 0 && bays_part.height != 0);
    CHECK(gears_part.width == logo_part.width && gears_part.height == logo_part.height);
    CHECK(bays_part.width == logo_part.width && bays_part.height == logo_part.height);
}

/// Fills a square icon with one colour.
WindowIcon solid(uint32_t side, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    WindowIcon icon;
    icon.width = side;
    icon.height = side;
    icon.pixels.assign(std::size_t{side} * side * window_icon_pixel_bytes, 0);
    for (std::size_t pixel = 0; pixel < std::size_t{side} * side; ++pixel) {
        icon.pixels[pixel * window_icon_pixel_bytes] = red;
        icon.pixels[pixel * window_icon_pixel_bytes + 1] = green;
        icon.pixels[pixel * window_icon_pixel_bytes + 2] = blue;
        icon.pixels[pixel * window_icon_pixel_bytes + alpha_byte] = alpha;
    }
    return icon;
}

void put_pixel(
    WindowIcon& icon,
    uint32_t x,
    uint32_t y,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t alpha
) {
    const std::size_t at = (static_cast<std::size_t>(y) * icon.width + x) * window_icon_pixel_bytes;
    icon.pixels[at] = red;
    icon.pixels[at + 1] = green;
    icon.pixels[at + 2] = blue;
    icon.pixels[at + alpha_byte] = alpha;
}

/// An area-scaled copy averages each block, keeps a clear corner clear, and
/// a same-sized icon is a copy. A side of 0, or pixels that do not fill the
/// icon, is empty.
void test_area_scaled() {
    CHECK(area_scaled(solid(2, 1, 2, 3, 4), 0).pixels.empty());
    WindowIcon short_of_pixels = solid(2, 1, 2, 3, 4);
    short_of_pixels.pixels.pop_back();
    CHECK(area_scaled(short_of_pixels, 1).pixels.empty());

    const WindowIcon same = solid(2, 9, 8, 7, 6);
    const WindowIcon copied = area_scaled(same, 2);
    CHECK(copied.width == 2 && copied.height == 2 && copied.pixels == same.pixels);

    WindowIcon blocks = solid(4, 0, 0, 0, 0);
    for (uint32_t y = 0; y < 2; ++y)
        for (uint32_t x = 0; x < 2; ++x)
            put_pixel(blocks, x, y, 255, 0, 0, 255);
    for (uint32_t y = 2; y < 4; ++y)
        for (uint32_t x = 0; x < 2; ++x)
            put_pixel(blocks, x, y, 0, 255, 0, 255);
    for (uint32_t y = 2; y < 4; ++y)
        for (uint32_t x = 2; x < 4; ++x)
            put_pixel(blocks, x, y, 0, 0, 255, 128);
    const WindowIcon halved = area_scaled(blocks, 2);
    CHECK(halved.width == 2 && halved.height == 2);
    CHECK(alpha_at(halved, 1, 0) == 0);
    CHECK(halved.pixels[0] == 255 && halved.pixels[3] == 255);
    CHECK(alpha_at(halved, 0, 1) == 255 && halved.pixels[window_icon_pixel_bytes * 2 + 1] == 255);
    CHECK(alpha_at(halved, 1, 1) == 128 && halved.pixels[window_icon_pixel_bytes * 3 + 2] == 255);

    // A black and white checker reduced to one pixel is grey, not a corner.
    WindowIcon checker = solid(4, 0, 0, 0, 255);
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 4; ++x)
            if (((x + y) % 2) == 0)
                put_pixel(checker, x, y, 255, 255, 255, 255);
    const WindowIcon grey = area_scaled(checker, 1);
    CHECK(grey.width == 1 && grey.pixels.size() == window_icon_pixel_bytes);
    CHECK(grey.pixels[0] == 128 && grey.pixels[1] == 128 && grey.pixels[2] == 128);
    CHECK(grey.pixels[3] == 255);
}

int main() {
    test_embedded_icon(window_icon_png());
    test_embedded_icon(settings_icon_png());
    test_embedded_icon(mods_icon_png());
    test_refused_files();
    test_visible_part();
    test_settings_icon_part();
    test_area_scaled();
    if (failures != 0) {
        std::fprintf(stderr, "window icon: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf(
        "window icon: the embedded app, settings and mods icons decode, and other files are "
        "refused\n"
    );
    return 0;
}
