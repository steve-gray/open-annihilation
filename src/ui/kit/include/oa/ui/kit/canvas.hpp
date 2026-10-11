// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Provisional: this header changes whenever OA's screens need it to, until the kit is declared stable (src/ui/kit/README.md).

// The canvas the kit's components draw on, and the few primitives every one
// of them draws through: a fill, a blend, an outline, a raised edge, a
// hairline rule, a line of text in a box and its width, a picture, a one-bit
// mark, and a clip. Places and sizes are in points. A canvas has two forms.
// The crisp form draws with the artless painter at a whole scale in the
// game's own fonts. The smooth form draws with the painter of
// oa/ui/paint/painter.hpp at any scale, a fractional one among them, in the
// modern fonts: every edge of a rectangle falls on a whole pixel, each
// rounded on its own, and text is drawn at its own size in pixels.
#pragma once

#include "oa/platform/text_font.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"
#include "oa/ui/paint/painter.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace oa::ui::kit {

/// The smooth form of a canvas: the painter's canvas at a scale in percent,
/// such as 150, its text in the bundled modern fonts. A point at (x, y) is
/// the pixel edge lround((origin + x) * scale), and the same down.
struct SmoothCanvas {
    /// Paints the canvas's pixels; null for a canvas of the crisp form.
    oa::ui::paint::Painter* painter{};
    /// The bundled modern fonts the text is drawn in; null draws no text.
    oa::platform::text_font::FontStack* fonts{};
    int32_t scale_percent{100}; ///< canvas pixels a point, in percent; 150 is one and a half
    /// The point of the painter's canvas this canvas's point (0, 0) lies at
    /// (placed_at).
    Point origin{};
    /// The canvas pixels drawing may touch: none clips to the painter's
    /// canvas alone, and an empty box draws nothing.
    std::optional<oa::ui::paint::Box> clip{};
};

/// Where a control is drawn: the crisp form, an artless surface at a whole
/// scale in the game's fonts, or, while its smooth form has a painter, the
/// smooth form. Either way the game's fonts lay the screens out.
struct Canvas {
    /// The crisp form's picture; null draws nothing there.
    oa::ui::frontend_renderer::Surface* surface{};
    oa::ui::frontend_renderer::Placement placement{}; ///< where points land, and its clip
    const Fonts* fonts{};                             ///< the game's fonts; null draws no text
    oa::ui::frontend_renderer::RgbaPicture
        icon{}; ///< the Open Annihilation icon; empty draws the OA mark
    SmoothCanvas
        smooth{}; ///< the smooth form; the crisp form's surface is not read while it paints
};

/// The size the smooth form draws a font's text at, in points: its game
/// font's height, 12 for the regular font and 10 for the small one. At a
/// scale the text is lround(points * scale) pixels per em.
///
/// @param role which font
/// @return the size, in points
[[nodiscard]] constexpr int32_t smooth_text_points(FontRole role) noexcept {
    return role == FontRole::regular ? 12 : 10;
}

/// Tells whether the smooth form draws a font's text bold: the regular
/// font, the game's heavy font of the title, the labels and the values, is;
/// the small one is not.
///
/// @param role which font
/// @return true for bold
[[nodiscard]] constexpr bool smooth_text_bold(FontRole role) noexcept {
    return role == FontRole::regular;
}

/// Tells whether a canvas has anything to draw on.
///
/// @param canvas the canvas
/// @return true when it has a surface, or its smooth form a painter
[[nodiscard]] bool can_draw(const Canvas& canvas) noexcept;

/// Returns a canvas that draws only inside a rectangle, and inside the
/// canvas's own clip when it has one. Where the two do not meet, nothing
/// draws. On the smooth form the clip is the rectangle's pixels.
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

/// Draws a one-point outline round the inside of a rectangle. On the smooth
/// form it is max(1, lround(scale)) whole pixels thick.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points; its inside is left as it is
/// @param ink the outline's colour
void outline_rect(const Canvas& canvas, const Rect& rect, Colour ink);

/// Draws a one-point raised edge round the inside of a rectangle: light along
/// the top and left, dark along the bottom and right, the dark edges running
/// the whole width and height. On the smooth form each edge is
/// max(1, lround(scale)) whole pixels thick.
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points; its inside is left as it is
/// @param light the top and left edges' colour
/// @param dark the bottom and right edges' colour
void bevel_rect(const Canvas& canvas, const Rect& rect, Colour light, Colour dark);

/// Draws a hairline rule: a rectangle one point high across, or one point
/// wide down. On the smooth form its thickness is max(1, lround(points *
/// scale)) whole pixels from its first edge, so every rule is as thick as
/// every other; its length's edges are rounded as a fill's are.
///
/// @param canvas where it is drawn
/// @param rect the rule, in points
/// @param ink the colour
void rule_rect(const Canvas& canvas, const Rect& rect, Colour ink);

/// Draws a line of text in a box, its capitals centred on the box's height.
/// Nothing is drawn without fonts.
///
/// The smooth form draws it in the modern fonts, antialiased, at
/// lround(smooth_text_points * scale) pixels per em, bold as
/// smooth_text_bold says, its tracking lround(tracking * scale) pixels.
/// Text wider than the box at that size steps down a pixel at a time until it
/// fits, to three quarters of its size at the least, as the game font's text
/// keeps to the boxes the layout measured it into.
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
/// @return the width, in points, on the smooth form its pixels at its
///     size over the scale, rounded up; 0 without fonts
[[nodiscard]] int32_t measure_text(const Canvas& canvas, FontRole role, std::string_view text);

/// Draws a picture scaled to fill a rectangle, such as the Open Annihilation
/// icon or a mod's badge. The smooth form draws it filtered
/// (oa::ui::paint::Painter::draw_rgba).
///
/// @param canvas where it is drawn
/// @param rect the rectangle, in points
/// @param picture the picture; an empty one draws nothing
void draw_picture(
    const Canvas& canvas, const Rect& rect, const oa::ui::frontend_renderer::RgbaPicture& picture
);

/// Draws a one-bit mark in one colour: only its set pixels, each a point, on
/// the smooth form a fill of that point's pixels.
///
/// @param canvas where it is drawn
/// @param bits the mark
/// @param at the mark's top left, in points
/// @param ink the colour
void draw_mark(
    const Canvas& canvas, const oa::ui::frontend_renderer::Mark& bits, Point at, Colour ink
);

} // namespace oa::ui::kit
