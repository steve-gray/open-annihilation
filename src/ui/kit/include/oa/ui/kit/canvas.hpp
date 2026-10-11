// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The canvas the kit's components draw on, and the few primitives every one
// of them draws through: a fill, a blend, an outline, a raised edge, a
// hairline rule, a line of text in a box and its width, a picture, a one-bit
// mark, and a clip. Places and sizes are in points. The crisp form draws
// them with the artless painter at a whole scale in the game's own fonts.
#pragma once

#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include <cstdint>
#include <string_view>

namespace oa::ui::kit {

/// Where a control is drawn: the crisp form, an artless surface at a whole
/// scale in the game's fonts.
struct Canvas {
    oa::ui::frontend_renderer::Surface* surface{};    ///< the picture; null draws nothing
    oa::ui::frontend_renderer::Placement placement{}; ///< where points land, and its clip
    const Fonts* fonts{};                             ///< the game's fonts; null draws no text
    oa::ui::frontend_renderer::RgbaPicture
        icon{}; ///< the Open Annihilation icon; empty draws the OA mark
};

/// Tells whether a canvas has anything to draw on.
///
/// @param canvas the canvas
/// @return true when it has a surface
[[nodiscard]] bool can_draw(const Canvas& canvas) noexcept;

/// Returns a canvas that draws only inside a rectangle, and inside the
/// canvas's own clip when it has one. Where the two do not meet, nothing draws.
///
/// @param canvas the canvas
/// @param rect the rectangle, in points
/// @return the canvas, clipped
[[nodiscard]] Canvas clipped(const Canvas& canvas, const Rect& rect);

/// Returns a canvas whose point (0, 0) is a point of another: what is drawn
/// on it lands that far right and down, and its clip stays where it was.
///
/// @param canvas the canvas
/// @param origin where the new canvas's origin lies, in the canvas's points
/// @return the canvas, moved
[[nodiscard]] Canvas placed_at(const Canvas& canvas, Point origin);

/// Fills a rectangle with a colour.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points
/// @param ink the colour
void fill_rect(const Canvas& canvas, const Rect& rect, Colour ink);

/// Blends a colour over a rectangle.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points
/// @param ink the colour
/// @param opacity its share of each pixel, in 256ths
void blend_rect(const Canvas& canvas, const Rect& rect, Colour ink, uint32_t opacity);

/// Draws a one-point outline round the inside of a rectangle.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points; its inside is left as it is
/// @param ink the outline's colour
void outline_rect(const Canvas& canvas, const Rect& rect, Colour ink);

/// Draws a one-point raised edge round the inside of a rectangle: light along
/// the top and left, dark along the bottom and right, the dark edges running
/// the whole width and height.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points; its inside is left as it is
/// @param light the top and left edges' colour
/// @param dark the bottom and right edges' colour
void bevel_rect(const Canvas& canvas, const Rect& rect, Colour light, Colour dark);

/// Draws a hairline rule: a rectangle one point high across, or one point
/// wide down.
///
/// @param canvas where it is drawn
/// @param rect the rule, in points
/// @param ink the colour
void rule_rect(const Canvas& canvas, const Rect& rect, Colour ink);

/// Draws a line of text in a box, its capitals centred on the box's height.
/// Nothing is drawn without fonts.
///
/// @param canvas where it is drawn
/// @param role which font
/// @param text the text, in UTF-8, as it is shown
/// @param box the box, in points
/// @param align where the text sits along the box's width
/// @param ink the text's colour
/// @param tracking extra points after each character but the last
void box_text(
    const Canvas& canvas,
    FontRole role,
    std::string_view text,
    const Rect& box,
    Align align,
    Colour ink,
    int32_t tracking = 0
);

/// Returns a line of text's width as the canvas draws it.
///
/// @param canvas the canvas
/// @param role which font
/// @param text the text, in UTF-8
/// @return the width, in points; 0 without fonts
[[nodiscard]] int32_t measure_text(const Canvas& canvas, FontRole role, std::string_view text);

/// Draws a picture scaled to fill a rectangle, such as the Open Annihilation
/// icon or a mod's badge.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points
/// @param picture the picture; an empty one draws nothing
void draw_picture(
    const Canvas& canvas, const Rect& rect, const oa::ui::frontend_renderer::RgbaPicture& picture
);

/// Draws a one-bit mark in one colour: only its set pixels, each a point.
///
/// @param canvas where it is drawn
/// @param bits the mark
/// @param at the mark's top left, in points
/// @param ink the colour
void draw_mark(
    const Canvas& canvas, const oa::ui::frontend_renderer::Mark& bits, Point at, Colour ink
);

} // namespace oa::ui::kit
