# OA UI kit

The OA UI kit (`oa/ui/kit`, `oa::ui::kit`): one look, one scale and one input
model for Open Annihilation's own screens. Provisional until the kit is
declared stable (D31). Its headers change whenever a screen needs them to,
until then.

## Entry points

- `theme.hpp`: the colour tokens (the settings dialog, the Game files screen
  and the folder chooser, the touch controls) and the metrics of the three
  size classes: `compact_metrics`, 0.7.3's, and `regular_metrics` and
  `large_metrics`, which give the settings dialog, its notices and its
  prompts more room (their sizes, the padding, the nav list's width and the
  characters a host's line under a row holds) and keep every other size.
- `text.hpp`: the game's two text faces, their width, drawing a line and a
  boxed line, and the one wrap.
- `layout.hpp`: points, the Auto scale and the three size classes,
  `layer_viewport` (the scale an Interface size gives a canvas, held to
  what fits Compact's dialog), `metrics_of` (a class's metrics, which a screen takes its window's sizes
  from), rows, columns, grids, splits and scroll areas, and the display
  list.
- `input.hpp`: a pointer and a finger's reach, the keys, Tab in a declared
  order, the arrows by where controls sit, the wheel, and a text field's
  editing: `insert_text` for typed text and `edit_text` for Backspace,
  Delete, Left, Right, Home and End. For automation, `automation_entries`
  lists the named controls, and `automation_parts` lists each control
  followed by the parts of it that take a press of their own (an
  `AutomationEntry` with `part` set): a switch's halves `<name>.off` and
  `<name>.on`, a strip's levels where `level_at` finds them, a row of
  buttons' buttons, and an open drop-down's items at `choice_item`, each
  named by its control's `Control::parts` word, else its place from 1. A
  control's rectangle there is the part of it a press reaches. The
  endpoint lists them after `oa.` (docs/automation.md).
- `looks.hpp`: how a control looks. A look owns its texts. A text field's
  state, `TextField`, is its text and its caret. A picture item draws its
  `PictureLook`'s picture, such as a mod's badge, or else the canvas's icon.
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
- `components_more.hpp`: the notice and the question (the settings
  dialog's notice and prompt): their models, placement at their size class
  (`Notice::size_class`, `Question::size_class`), display lists, pointer,
  finger and key events and drawing, and the progress bar. A
  notice's and a question's `word` (`notice` and `prompt` unless the code
  that raises it gives its own, as the missing-language question's
  `language-notice`) starts its controls' names: a notice's `<word>.ok` and
  `<word>.open`, a question's buttons `<word>.<id>` by their
  `QuestionButton::id`, or `<word>.button-<n>` from 1 without one, and
  both's text `<word>.body`, a control of kind `area` that is neither
  enabled nor focusable, so that no press, finger or key reaches it, whose
  text is the title, the paragraphs and the failure, one a line.
- `rows.hpp`: declared rows. A settings page is a table of `RowSpec`s,
  each made by a factory that takes the row's id first: `toggle`, `choice`,
  `slider`, `levels`, `value_and_button`, `buttons`, `text_field`, `link`
  and `text`, bound to a model's fields or to its get and set functions:

  ```cpp
  constexpr std::array<kit::RowSpec<Downloads>, 3> kDownloadsRows{
      kit::choice("check-updates", "Check for content updates", &Downloads::check, kCheckCaptions),
      kit::toggle("install-id-on", "Install ID", &Downloads::install_id_on, {"A random number."}),
      kit::value_and_button("install-id", "", install_id_text, "RESET", "reset", kResetId),
  };
  ```

  `view_of` reads a row's `RowView` from its spec and the model through the
  caller's text function; `place_rows` places a column of views,
  `rows_scroll` and `scroll` scroll it, and `add_rows` lists what the rows
  draw and their controls, named `<prefix>.<id>`. A drop-down's items and a
  strip's levels have ids too, beside their captions: an index field's
  `choice_ids` (the `choice` factory that takes them) and a stepper's `id`
  function (`row_choice_id` reads either). `add_rows` gives a control its
  levels' or items' ids, or a row of buttons' `button_ids`, as its parts'
  words, which automation names its parts by. `step`, `activate`,
  `press`, `drag`, `choose`, `type` and `edit` change the model as a row's
  control is used.

## State

None. `load_game_fonts` reads the game's font files. A screen passes the
pointer's place and the wheel's fraction in, and keeps its fields' text and
its hover card's timer in its own state.

## Invariants

- Compact metrics are 0.7.3's exactly, for the controls 0.7.3 has. Regular
  and Large metrics differ from them only in the sizes the design's table
  gives the classes; rows, text and controls keep Compact's sizes, so a
  larger class shows more rows instead of growing them.
- Every token's value is pinned by `ui-kit-theme`.
- One wrap. Its rules are the only differences between its callers.
- The kit never looks text up in the interface catalogue. Callers pass what
  to show. A button's caption, a switch's OFF and ON, a strip's captions, a
  menu's items, a header's words, a nav entry, a row's label and hints, a
  chip's, a tab's, a card's, a hover card's and a link's words arrive
  already looked up. A text field holds the player's own text. A notice's
  title, lines and captions pass through the look-up its caller hands it,
  when one is handed: the notice wraps the words it was given and shows
  each line as the look-up returns it, as the settings dialog's notice
  always has. A question's texts are drawn as given.
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
- A notice and a question place their parts from the Compact metrics, but
  for their width and greatest height, which are their size class's: their
  text wraps at the wider width, and their buttons keep their sizes. A
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
  own prefix; the name here has none. A part of a control is named after it
  with one word more, from the code's own words (a row's, a choice's or a
  button's id), never from a text as shown.
- A column of rows is placed exactly as the settings dialog places a
  section's rows: `ui-kit-rows` holds rows shaped as Controls', Graphics' and
  Developer's to the dialog's own placement, and U08 proves it on every page.
- A row's kind is its spec's `kind`, and nothing else decides it. A spec
  has an id, one word of a-z, 0-9 and hyphens; a table built at compile time
  with a malformed id does not compile.
- A `RowView` and the display list own their texts: they keep nothing of
  the spec or the model.

The arrows move by where controls sit. A control's own scroll area is searched
first, including rows the area does not show. The focus leaves that area only
when nothing in it lies that way, and a move from outside never lands on a
row the area hides. A control in no scroll area, such as a footer button or
an entry of a section list, has no area of its own: every control that shows
is searched at once. Tab follows the declared order and wraps. Up and Down
never follow that order. Left and Right go to a focused text field, which
moves its caret, as do Home, End, Backspace and Delete. Tabs and cards do not
take Left and Right: the arrows move the focus between them. Enter goes to a
focused button, link, list row or tab.

## Tests

`ui-kit-theme` checks every token against the value it replaces, the
Compact metrics against 0.7.3's numbers, Regular's and Large's against the
design's table of sizes and that they differ from Compact's in nothing
else, and that each class's dialog and notices fit the least window of their
class. `ui-kit-text` checks the UTF-8
helpers, the estimated width, the stand-ins, and the wrap against copies of
the six wraps it replaces. `ui-kit-layout` checks the Auto scale, the design's
window sizes, the Interface size's examples and Auto at each window, that `metrics_of` gives each class its metrics, arrangements,
the scroll arithmetic and hit testing.
`ui-kit-input` checks reach, the pointer, Tab, the arrows, the keys and
their values, the wheel's fractions, the names, the parts automation lists
(a switch's halves splitting it, a strip's levels where `level_at` finds
them, an open drop-down's items at `choice_item` and a screen's own item
control listed once, a row of buttons' buttons and a clipped control's
press area), and a text field's typing and editing over characters of one
to four bytes. `ui-kit-components` checks each control's
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
`ui-kit-notices` checks where six notices and six questions place their
parts against the settings dialog's own placement, their display lists'
names, kinds and Tab order, a word of their own (`language-notice`) and
their text's control, which changes what no press or finger finds at any
point of the box, where a finger lands, every key (the editing
keys, which do nothing, among them), that `paint` matches `draw_notice` and
`draw_question`, and the progress bar's fill.
`ui-kit-rows` lays out, draws, focuses and changes a page of every row
kind from a table: the placement of rows shaped as the settings dialog's
Controls, Graphics and Developer sections, with the game's fonts and the
modern fonts' taller hints, locked and not, against values computed with
the dialog's own placement, and the scroll against the dialog's; the names,
kinds, Tab order and drawing order of the display list; the ids of a
drop-down's items and a strip's levels, and the parts they name; rows of two models
in one column; every event on every kind; and the page painted.

## Limitations

The metrics hold the settings dialog's, its notices' and its prompts' sizes
for each class (`regular_metrics`, `large_metrics`, `metrics_of`); the
Library's and the map browser's are theirs to add. A drop-down's open menu
keeps above the footer line it is given (`choice_menu`), the Compact
settings dialog's unless a larger dialog gives its own. The canvas draws at
whole scales; fractional sizes are U24's. The arrows' rule is one function, its weight named, and is to be
tuned after controller playtests (D30). The screens' colour family converges
with U13 and U14. The look of the list rows, chips, fields, tabs, cards,
hover cards and links follows the design's components mockup in the kit's
tokens; it is reviewed in the gallery (U12) and pinned by golden frames
(U17). A field takes typed text and the editing keys; it has no selection,
no clipboard and no press that places the caret.
A row of buttons takes up to three, with Your files' widths. A link is as
wide as its line unless its spec gives a width. The rows do not draw a
drop-down's open menu, a scroll bar or the rule a scrolled view keeps at
its top: the screen draws them, as the settings dialog does.
