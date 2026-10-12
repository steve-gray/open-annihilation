// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game's three icons, each at 256 by 256 pixels, made by
// tools/make_icons.py and embedded as a PNG file. The app icon, the plates
// alone, is the window icon start-up gives the window, and the badge of
// every OA dialog and notice but Settings and the Library. The settings
// icon, the plates over two gears, is what the Settings button, the in-game
// menu's OA button and the Settings dialog's header draw. The mods icon,
// the plates over drive bays, is what the main menu's Library button and
// the Library's header draw. Each is cut to its visible part where it is
// drawn.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace oa::app {

/// Width and height of the window icon, in pixels.
inline constexpr uint32_t window_icon_size = 256;

/// Bytes of one window icon pixel: red, green, blue and alpha.
inline constexpr uint32_t window_icon_pixel_bytes = 4;

/// A decoded window icon.
struct WindowIcon {
    uint32_t width{};              ///< pixels
    uint32_t height{};             ///< pixels
    std::vector<uint8_t> pixels{}; ///< rows of window_icon_pixel_bytes per pixel, top row first
};

/// Returns the window icon the game embeds.
///
/// @return a PNG file of window_icon_size by window_icon_size 8-bit RGBA
///         pixels, which lasts as long as the program
std::span<const uint8_t> window_icon_png();

/// Returns the settings icon the game embeds, which the buttons that open
/// Settings draw, and the Settings dialog's header.
///
/// @return a PNG file of window_icon_size by window_icon_size 8-bit RGBA
///         pixels, which lasts as long as the program
std::span<const uint8_t> settings_icon_png();

/// Returns the mods icon the game embeds, which the Library button and the
/// Library's header draw.
///
/// @return a PNG file of window_icon_size by window_icon_size 8-bit RGBA
///         pixels, which lasts as long as the program
std::span<const uint8_t> mods_icon_png();

/// The side, in pixels, of the app icon kept ready for the small maker's
/// mark. A mark at most this large is drawn from that copy, filtered down,
/// rather than from the full icon.
inline constexpr uint32_t maker_mark_prepared_side = 64;

/// Decodes an 8-bit RGBA PNG file for one of the three icons, which share
/// this form.
///
/// @param png the file
/// @param[out] icon the pixels; left empty on failure
/// @param[out] error what was wrong, on failure
/// @return true when the file held one 8-bit RGBA image of window_icon_size
///         by window_icon_size pixels
[[nodiscard]] bool
decode_window_icon(std::span<const uint8_t> png, WindowIcon& icon, std::string& error);

/// Returns the smallest rectangle of an icon that holds every pixel that is
/// not wholly transparent, so that a small copy of the icon fills its place
/// without the clear margin round it.
///
/// @param icon the icon, its pixels filling its size
/// @return that rectangle's pixels; empty when every pixel is transparent or
///         the pixels do not fill the icon's size
[[nodiscard]] WindowIcon visible_part(const WindowIcon& icon);

/// Scales an icon to a square with an area filter: each pixel of the result
/// is the mean of the source pixels that land on it, weighted by alpha, so
/// a clear corner stays clear. A side of 0, or pixels that do not fill the
/// icon, returns an empty icon. The same side returns a copy.
///
/// @param icon the icon
/// @param side the result's width and height, in pixels
/// @return the scaled icon
[[nodiscard]] WindowIcon area_scaled(const WindowIcon& icon, uint32_t side);

} // namespace oa::app
