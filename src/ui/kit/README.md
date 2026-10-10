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
  order, the arrows by where controls sit, the wheel, and a text field's
  editing: `insert_text` for typed text and `edit_text` for Backspace,
  Delete, Left, Right, Home and End.
- `looks.hpp`: how a control looks. A look owns its texts. A text field's
  state, `TextField`, is its text and its caret.
- `components.hpp`: the controls, drawn on a crisp-backend canvas. One
  button, five looks. `paint` draws a display list.
- `chrome.hpp`: the window's face and edge, its header and footer band,
  the nav list, and a section's heading, row, rule and locked fade.
- `controls.hpp`: the controls of the Library, the map browser and the
  battle room's card:
  - a list row, in its full form (`list_row_height`, a 28-point badge) and
    its short form (`short_list_row_height`, the Mods row's), with a badge
    picture, letters or a blank badge, a title and its byline, lines cut
    with "...", an aside, a chip and tags;
  - chips in seven states: get, installed, update, playing, online, a
    problem and a filter, each selectable, hovered and disabled, and
    `chip_width`;
  - a text field, and with the magnifier the search field, its text moved
    so its caret shows;
  - tabs with a count, `tab_rects`;
  - cards with a square preview or a placeholder, chips on the preview, a
    title and a subtitle, greyed or not, `card_height` and `card_cells`, a
    grid of as many columns as fit or of a fixed number;
  - a hover card with the OA mark, a title and an aside, label and value
    rows, a quiet block and an arrow from any edge, `hover_card_size`,
    `hover_card_rect` and `hover_card_due`, which says when the pointer has
    rested on one control for `hover_card_delay_ms` (400);
  - a link, `link_width`.

  Each has a draw function and an `add_*` that puts it in a display list
  with its name, kind and Tab place; `paint` draws them.

## State

None. `load_game_fonts` reads the game's font files. A screen passes the
pointer's place and the wheel's fraction in, and keeps its fields' text and
its hover card's timer in its own state.

## Invariants

- Compact metrics are 0.7.3's exactly, for the controls 0.7.3 has.
- Every token's value is pinned by `ui-kit-theme`.
- One wrap. Its rules are the only differences between its callers.
- The kit never looks text up in the interface catalogue. Callers pass what
  to show. A button's caption, a switch's OFF and ON, a strip's captions, a
  menu's items, a header's words, a nav entry, a row's label and hints, a
  chip's, a tab's, a card's, a hover card's and a link's words arrive
  already looked up. A text field holds the player's own text.
- The new controls use only kit tokens, and their sizes are
  `compact_metrics` members. The design's components mockup is drawn in web
  colours; the kit draws them so:

  | Mockup | Kit token |
  |---|---|
  | lime | `colour::accent` |
  | lime ink | `colour::on_accent` |
  | amber | `colour::lock` |
  | red | `colour::danger` (e06c5c, the Game files screen's red) |
  | blue | `colour::online` (6ea8d6) |
  | dim | `colour::hint` |
  | line | `colour::control_border` |
  | line2 | `colour::control_hover` |
  | field | `colour::well` |
  | panel2 | `colour::list_selected` |
  | the hover card's ground | `colour::band` |
  | a selected row's tint | `colour::accent` blended at `selected_tint` (15/256) over the face |

- A text field's text is well-formed UTF-8 without control characters, and
  its caret stands on a character boundary: `insert_text` refuses anything
  else whole, and `edit_text` deletes and steps over whole characters.
- `kit::Key` keeps the settings dialog's keys' values up to No; the editing
  keys, Backspace and Delete, follow it.
- A hover card is due only once the pointer has rested on one control for
  `hover_card_delay_ms`. The timer is the screen's: the kit keeps none.
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
never follow that order. Left and Right go to a focused text field, which
moves its caret, as do Home, End, Backspace and Delete. Tabs and cards do not
take Left and Right: the arrows move the focus between them. Enter goes to a
focused button, link, list row or tab.

## Tests

`ui-kit-theme` checks every token against the value it replaces, and the
Compact metrics against 0.7.3's numbers. `ui-kit-text` checks the UTF-8
helpers, the estimated width, the stand-ins, and the wrap against copies of
the six wraps it replaces. `ui-kit-layout` checks the Auto scale, the design's
window sizes, arrangements, the scroll arithmetic and hit testing.
`ui-kit-input` checks reach, the pointer, Tab, the arrows, the keys and
their values, the wheel's fractions, the names, and a text field's typing
and editing over characters of one to four bytes. `ui-kit-components` checks each control's
pixels, that `paint` matches a direct draw, and the controls' geometry.
`ui-kit-chrome` checks the header, the footer band, the nav list, a row's
frame and the locked fade, and that `paint` matches those direct draws.
`ui-kit-controls` checks each new control's pixels in the tokens above with
a synthetic font: list rows in both forms, every chip state, the field's
placeholder, caret and scrolling, tabs with and without a count, cards and
their grid at three widths and at 3, 4 and 6 columns, the arrows through a
grid of cards, the hover card with an arrow on each side, its place in a
frame and its delay, and links; and that every `add_*` names its controls
and `paint` matches the direct draws.

## Limitations

Regular and Large metrics are U10's, and so is choosing a screen's layout
from its size class. The canvas draws at whole scales; fractional sizes are
U24's. The arrows' rule is one function, its weight named, and is to be
tuned after controller playtests (D30). The screens' colour family converges
with U13 and U14. The look of the list rows, chips, fields, tabs, cards,
hover cards and links follows the design's components mockup in the kit's
tokens; it is reviewed in the gallery (U12) and pinned by golden frames
(U17). A field takes typed text and the editing keys; it has no selection,
no clipboard and no press that places the caret.
