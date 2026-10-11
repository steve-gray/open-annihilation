// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game's window icon: the Open Annihilation icon at 256 by 256 pixels,
// made by tools/make_icons.py and embedded in the game as a PNG file, which
// start-up decodes and gives the window. The settings dialog's header and
// the main menu's Library button draw the same icon, cut to its visible
// part. The settings icon, the logo over a row of gears, is embedded beside
// it at the same size; the main menu's Settings button and the in-game
// menu's OA button draw it, cut the same way.
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
/// Settings draw.
///
/// @return a PNG file of window_icon_size by window_icon_size 8-bit RGBA
///         pixels, which lasts as long as the program
std::span<const uint8_t> settings_icon_png();

/// Decodes an 8-bit RGBA PNG file for a window icon, or for the settings
/// icon, which has the same form.
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

} // namespace oa::app
