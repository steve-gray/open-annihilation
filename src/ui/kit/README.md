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
- `input.hpp`: a pointer and a finger's reach, the keys, Tab in a declared
  order, the arrows by where controls sit, and the wheel.

## State

None. `load_game_fonts` reads the game's font files. A screen passes the
pointer's place and the wheel's fraction in.

## Invariants

- Compact metrics are 0.7.3's exactly.
- Every token's value is pinned by `ui-kit-theme`.
- One wrap. Its rules are the only differences between its callers.
- The kit never looks text up in the interface catalogue. Callers pass what
  to show.
- The display list is the only record of what a screen drew and where its
  controls are. Hit testing reads `controls`. Drawing reads `items`.
- Every control of a kit screen carries a unique name: words of a-z, 0-9 and
  hyphens joined by dots, at most 100 bytes. The automation endpoint adds its
  own prefix; the name here has none.

The arrows move by where controls sit. A control's own scroll area is searched
first, including rows the area does not show. The focus leaves that area only
when nothing in it lies that way, and a move from outside never lands on a
row the area hides. Tab follows the declared order and wraps. Up and Down
never follow that order.

## Tests

`ui-kit-theme` checks every token against the value it replaces, and the
Compact metrics against 0.7.3's numbers. `ui-kit-text` checks the UTF-8
helpers, the estimated width, the stand-ins, and the wrap against copies of
the six wraps it replaces. `ui-kit-layout` checks the Auto scale, the design's
window sizes, arrangements, the scroll arithmetic and hit testing.
`ui-kit-input` checks reach, the pointer, Tab, the arrows, the keys, the
wheel's fractions and the names.

## Limitations

Regular and Large metrics are U10's, and so is choosing a screen's layout
from its size class. The arrows' rule is one function, its weight named, and
is to be tuned after controller playtests (D30). The screens' colour family
converges with U13 and U14.
