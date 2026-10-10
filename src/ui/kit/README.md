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
- `looks.hpp`: how a control looks. A look owns its texts.
- `components.hpp`: the controls, drawn on a crisp-backend canvas. One
  button, five looks. `paint` draws a display list.
- `chrome.hpp`: the window's face and edge, its header and footer band,
  the nav list, and a section's heading, row, rule and locked fade.
- `components_more.hpp`: the notice and the question (the settings
  dialog's notice and prompt): their models, placement, display lists,
  pointer, finger and key events and drawing, and the progress bar.

## State

None. `load_game_fonts` reads the game's font files. A screen passes the
pointer's place and the wheel's fraction in.

## Invariants

- Compact metrics are 0.7.3's exactly.
- Every token's value is pinned by `ui-kit-theme`.
- One wrap. Its rules are the only differences between its callers.
- The kit never looks text up in the interface catalogue. Callers pass what
  to show. A button's caption, a switch's OFF and ON, a strip's captions, a
  menu's items, a header's words, a nav entry and a row's label and hints
  arrive already looked up. A notice's title, lines and captions pass
  through the look-up its caller hands it, when one is handed: the notice
  wraps the words it was given and shows each line as the look-up returns
  it, as the settings dialog's notice always has. A question's texts are
  drawn as given.
- A notice and a question place their parts from the Compact metrics. A
  question's buttons are as wide as their captions at the estimated width,
  whatever the fonts, so a press lands where a button is drawn. Their
  events hit and reach through `hit` and `reach` over the same controls
  their display lists hold: a notice's OK before its open button, a
  question's buttons left to right.
- One button, five looks: accent, plain, quiet, inset, and plain while
  disabled. The kit chooses every colour. A caller passes the style and
  whether the pointer is over the button or holds it.
- `paint` draws a display list's items in the list's order. A control is
  added with its automation name.
- The canvas is the crisp backend: whole scales and the game's fonts. U24
  adds the smooth one.
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
wheel's fractions and the names. `ui-kit-components` checks each control's
pixels, that `paint` matches a direct draw, and the controls' geometry.
`ui-kit-chrome` checks the header, the footer band, the nav list, a row's
frame and the locked fade, and that `paint` matches those direct draws.
`ui-kit-notices` checks where six notices and six questions place their
parts against the settings dialog's own placement, their display lists'
names, kinds and Tab order, where a finger lands, every key, that `paint`
matches `draw_notice` and `draw_question`, and the progress bar's fill.

## Limitations

Regular and Large metrics are U10's, and so is choosing a screen's layout
from its size class. The canvas draws at whole scales; fractional sizes are
U24's. The arrows' rule is one function, its weight named, and is to be
tuned after controller playtests (D30). The screens' colour family converges
with U13 and U14.
