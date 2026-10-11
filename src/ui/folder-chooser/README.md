# Folder chooser

The in-engine game folder chooser's model (`oa/ui/folder_chooser.hpp`,
`oa::ui::folder_chooser`): the screen that asks for the Total Annihilation
folder where the system's folder dialog may not show (Steam's Game Mode on a
Steam Deck), where several folders were found, or where a remembered folder
has gone. It is driven by touch, a gamepad, the keys or the pointer, and has
no SDL and no app header. The app (`folder_chooser_screen.cpp` in
[src/app](../../app/README.md)) fills a `Model`, lays it out with `lay_out`
into the UI kit's display list and paints its parts with the Game files
screen's painter, in that screen's look
([src/ui/game-files](../game-files/README.md)); presses and keys come back
as a `Command` for the app to carry out. Like the Game files screen, it is
laid out, hit tested and focused by the kit, so the two share one UI stack,
and the app shows both on the window through the same window-pixel code.

## Entry points

- `lay_out(model, state, viewport, measure)` lays out the view the model
  names, in the tablet form (the Steam Deck's 1280x800) or the compact form
  (a phone, or a window whose shorter side is under 600 points), inside the
  viewport's safe area, every control at least `min_button_points` (44 pt)
  high:
  - the list: the title, a line that says how many folders were found, the
    notice (a remembered folder that has gone and why, the system's dialog
    that did not open, or a folder that cannot be played), a row for each
    folder found with where it was found, the folder and what the check
    found there (a folder that cannot be played is shown with why, and takes
    no press), then Browse…, "The Total Annihilation demo (1997)", "Use the
    desktop's folder dialog" where the model offers it, and QUIT;
  - the browser: the folder's name and path, a hint (where the demo's
    installer lies), the notice, the places (Home, Downloads, each drive)
    and PARENT FOLDER, what the check found in the folder, its folders
    (game folders marked, "No folders here." for none), BACK and PLAY THIS
    FOLDER, which takes presses only when the folder can be played.

  The result is a `Layout`: the kit's display list of parts in canvas
  pixels, in painting order, inside a `game_files::Layout` for the painter
  (with the scrolling region), the focus order (the enabled targets, top to
  bottom) and how far the rows scroll. Each part that draws a target carries
  it as its kit control number, and is disabled when the target takes no
  press; the list's controls are those parts, the top one first, the ones in
  the scrolling region its scroll area 0, and its Tab order is the focus
  order. The focused and pressed targets of `UiState` show on their parts as
  the kit's states (`kit::mark_states`). A column that does not fit scrolls
  its rows between the fixed top and the buttons; when even that leaves the
  rows too little room, the whole column scrolls.
- `control_id` and `target_of` turn a `Target` (its kind and index) into the
  number its part and its kit control carry, and back.
- `hit_test` finds the target under a point with the kit's `reach` over the
  layout's controls, by the rule that a disabled control holding the point
  blocks it (`kit::DisabledControl::blocks`): the top control whose part,
  cut by its clip, holds the point, none when that one is disabled; else the
  nearest enabled one within reach. The Game files screen passes a press
  through a disabled control instead; each screen keeps its own rule.
- `press_down` and `press_up` take a press of a finger or the pointer: the
  command comes when the press comes up on the target it went down on, and
  the focus follows it. A press hides the focus the keys showed.
- `key` takes the keys a keyboard or a gamepad gives: the first shows the
  focus where it is; the arrows move it by the kit's two-dimensional focus
  (`kit::focus_toward`), judged with the rows laid out whole (the rows
  scrolled back, the buttons under them moved down past all of them), so up
  from BACK reaches the last folder; Tab and Shift+Tab move through the
  focus order, and a target the focus reaches is scrolled whole into view;
  Enter presses; Escape goes from the browser back to the list; the page
  keys scroll a page.
- `scroll` scrolls the rows, held within the layout's range.

Every word goes through `oa::data::languages::interface_text`.

## Built from the kit

The kit's text measures, fits and wraps every line, a word wider than its
line, such as a folder's path, breaking after its last separator that fits
(`kit::path_break`); its `text_part`, `line_part`, `button_row`, `icon_part`
and `plain_part` lay out the parts at the form's scale (`kit::Typesetter`,
`canvas_pixels`); its `scroll_column` places the column's three blocks and
says what scrolls; `list_part_controls` lists the controls and the Tab
order; and its `reach`, `focus_toward` and `mark_states` hit test, move the
focus and mark the parts.

What stays the chooser's: its metrics (`tablet_metrics` and
`compact_metrics`) and its forms rule; its compositions, built from the
kit's parts: the header, the title and the lines under it, banners, the
rows in their panel and the buttons; the model, the targets and their
commands; and what a key does (the first key showing the focus where it is,
Enter, Escape and the page keys). The app keeps the gamepads' handling, the
held direction's repeat and the right stick's scrolling, its own.

## Tests

`ui-folder-chooser` lays out the list and the browser at 1280x800 and at
852x393 with a phone's safe area: every target at least 44 points high and
inside the safe area, the focus order, the kit's controls (one for each part
that draws a target, the top one first, as the part lies, the scrolled ones
in scroll area 0) and the Tab order, the dialog's row only where offered,
the notice, the disabled rows and buttons; the keys (the first key, arrows
across the places and down into the folders, scrolling the focused folder
into view, up from BACK, Tab round the order, Enter, Escape, the page keys);
presses down and up, a press that slides off, a disabled target and a press
within reach; and the scroll's limits. Every layout of every case (every
model the checks build, one folder found, a long notice, long paths that
break at their separators, and the rows scrolled, focused and pressed) at
both sizes keeps the SHA-256 of its text form that `tests/layout-digests.txt`
records: the parts with every field the chooser sets, the targets, the focus
order and how far it scrolls. They were recorded from the chooser's own
layout code before it moved onto the kit, and the test fails on a missing,
extra or different digest. `oa-ui-folder-chooser-test --write-digests`
prints the file's content, and `--dump TEXT` the text form of the cases
whose names hold TEXT.
