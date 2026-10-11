# Paint

Anti-aliased painting into an RGBA canvas (`oa/ui/paint/painter.hpp`,
`oa::ui::paint`), used by the touch controls, the Game files screen, the
folder chooser and the OA UI kit's smooth canvas (`src/ui/kit`). The gamepad's glyphs (`oa/ui/paint/pad_glyphs.hpp`,
`oa::ui::paint::pad_glyphs`) are painted with it: the project's own shapes
and letters.

## Entry points

- `painter.hpp`: `Canvas` and `Painter`; the shapes (rectangles, rounded
  panels, circles, wedges); RGB pictures by the nearest pixel (`draw_rgb`)
  and RGBA pictures scaled and filtered (`draw_rgba`: shrunk, each pixel
  the picture's pixels its span covers, weighted by how much; grown,
  bilinear), which the OA UI kit's smooth canvas draws the Open Annihilation
  icon with; the icon marks; and the text functions (`draw_line`,
  `text_width`, `line_metrics`, `fit_text`, `paint_line`).
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
from the bundled fonts, RGBA pictures scaled and filtered (copied at their
own size, shading smoothly when grown, averaged when shrunk, mixed by their
alpha and kept to the clip) and a gamepad glyph of every shape inside its
box.
The text checks read the bundled fonts beside the test.

## Limitations

The window icon is decoded in the app (`game_files_icon`). Text is wrapped by
the OA UI kit; this painter shortens one line (`fit_text`) and does not wrap.
