# Paint

Anti-aliased painting into an RGBA canvas (`oa/ui/paint/painter.hpp`,
`oa::ui::paint`), used by the touch controls, the Game files screen and the
folder chooser. The gamepad's glyphs (`oa/ui/paint/pad_glyphs.hpp`,
`oa::ui::paint::pad_glyphs`) are painted with it: the project's own shapes
and letters.

## Entry points

- `painter.hpp`: `Canvas` and `Painter`; the shapes (rectangles, rounded
  panels, circles, wedges); the icon marks; and the text functions
  (`draw_line`, `text_width`, `line_metrics`, `fit_text`, `paint_line`).
- `pad_glyphs.hpp`: the glyphs (`glyph_width`, `draw_glyph`, `spec_width`,
  `draw_spec`, `chord_width`, `draw_chord`).

## State

None. Fonts are passed in.

## Invariants

- Straight alpha.
- Every shape is clipped to the painter's clip and the canvas.
- Edges are smoothed by coverage.
- It includes no window-system header, no present header and no app header.
- Gamma is the presenter's.

## Tests

`ui-paint` checks rectangles clipped to the canvas, the icon marks, text
from the bundled fonts and a gamepad glyph of every shape inside its box.
The text checks read the bundled fonts beside the test.

## Limitations

The window icon is decoded in the app (`game_files_icon`). Text is wrapped by
the OA UI kit; this painter shortens one line (`fit_text`) and does not wrap.
