# OA UI kit

The OA UI kit (`oa/ui/kit`, `oa::ui::kit`): one look, one scale and one input
model for Open Annihilation's own screens. Provisional until the kit is
declared stable (D31). Its headers change whenever a screen needs them to,
until then.

## Entry points

- `theme.hpp`: the colour tokens (the settings dialog, the Game files screen
  and the folder chooser, the touch controls) and the Compact metrics.
- `text.hpp`: the game's two text faces, their width, drawing a line and a
  boxed line, and the one wrap.
- `layout.hpp`: points, the Auto scale and the three size classes, rows,
  columns, grids, splits and scroll areas, and the display list.

## State

None. `load_game_fonts` reads the game's font files.

## Invariants

- Compact metrics are 0.7.3's exactly.
- Every token's value is pinned by `ui-kit-theme`.
- One wrap. Its rules are the only differences between its callers.
- The kit never looks text up in the interface catalogue. Callers pass what
  to show.
- The display list is the only record of what a screen drew and where its
  controls are. Hit testing reads `controls`. Drawing reads `items`.

## Tests

`ui-kit-theme` checks every token against the value it replaces, and the
Compact metrics against 0.7.3's numbers. `ui-kit-text` checks the UTF-8
helpers, the estimated width, the stand-ins, and the wrap against copies of
the six wraps it replaces. `ui-kit-layout` checks the Auto scale, the design's
window sizes, arrangements, the scroll arithmetic and hit testing.

## Limitations

Regular and Large metrics are U10's, and so is choosing a screen's layout
from its size class. Input over the display list is U04's. The screens'
colour family converges with U13 and U14.
