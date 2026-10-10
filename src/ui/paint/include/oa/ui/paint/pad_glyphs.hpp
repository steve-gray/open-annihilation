// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The gamepad's button glyphs, painted with the anti-aliased painter into an
// RGBA canvas: the project's own shapes and letters for the Steam Deck, Xbox,
// PlayStation and Nintendo styles, never console or Valve art
// (docs/controllers.md). The touch controls, the Game files screen and the
// folder chooser draw with the same painter.
#pragma once

#include "oa/platform/text_font.hpp"
#include "oa/ui/pad_controls.hpp"
#include "oa/ui/paint/painter.hpp"

namespace oa::ui::paint::pad_glyphs {

/// Returns the width a glyph takes at a height (letters and pills widen).
///
/// @param button the physical button
/// @param style the glyph set
/// @param height the glyph's height, pixels
/// @return the width, pixels
[[nodiscard]] float glyph_width(
    oa::ui::pad_controls::PadButton button, oa::ui::pad_controls::GlyphStyle style, float height
) noexcept;

/// Paints a button's glyph, the project's own shapes and letters, centred in a box.
///
/// @param painter the painter
/// @param fonts the fonts the letters are drawn in; null draws the shapes alone
/// @param button the physical button
/// @param style the glyph set
/// @param box where the glyph goes, pixels
/// @param ink the colour of the marks and letters
/// @param fill the colour of the shape's body
void draw_glyph(
    paint::Painter& painter,
    oa::platform::text_font::FontStack* fonts,
    oa::ui::pad_controls::PadButton button,
    oa::ui::pad_controls::GlyphStyle style,
    paint::Area box,
    paint::Rgba ink,
    paint::Rgba fill
);

/// Returns the width a glyph drawn by a spec takes at a height (pills widen with their text).
///
/// @param spec how the glyph is drawn
/// @param height the glyph's height, pixels
/// @return the width, pixels
[[nodiscard]] float spec_width(const oa::ui::pad_controls::GlyphSpec& spec, float height) noexcept;

/// Paints a glyph drawn by a spec, centred in a box at the box's height: discs with letters,
/// pills with labels, PlayStation's marks as strokes, a D-pad with its arm lit, trackpad and
/// stick outlines, and the View and Menu marks.
///
/// @param painter the painter
/// @param fonts the fonts the letters are drawn in; null draws the shapes alone
/// @param spec how the glyph is drawn
/// @param box where the glyph goes, pixels
/// @param ink the colour of the marks and letters
/// @param fill the colour of the shape's body
void draw_spec(
    paint::Painter& painter,
    oa::platform::text_font::FontStack* fonts,
    const oa::ui::pad_controls::GlyphSpec& spec,
    paint::Area box,
    paint::Rgba ink,
    paint::Rgba fill
);

/// Returns the width a chord takes ("View" + "X" with a gap and a plus).
///
/// @param chord the buttons
/// @param style the glyph set
/// @param height the glyphs' height, pixels
/// @return the width, pixels
[[nodiscard]] float chord_width(
    const oa::ui::pad_controls::Chord& chord, oa::ui::pad_controls::GlyphStyle style, float height
) noexcept;

/// Paints a chord's glyphs left to right from a box's left edge, vertically centred, the plus
/// between them in the shapes' colour.
///
/// @param painter the painter
/// @param fonts the fonts the letters are drawn in; null draws the shapes alone
/// @param chord the buttons
/// @param style the glyph set
/// @param box where the glyphs go, pixels; the glyphs are its height
/// @param ink the colour of the marks and letters
/// @param fill the colour of the shapes' bodies
void draw_chord(
    paint::Painter& painter,
    oa::platform::text_font::FontStack* fonts,
    const oa::ui::pad_controls::Chord& chord,
    oa::ui::pad_controls::GlyphStyle style,
    paint::Area box,
    paint::Rgba ink,
    paint::Rgba fill
);

} // namespace oa::ui::paint::pad_glyphs
