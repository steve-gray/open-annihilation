# Map browser

OA's map browser: one screen that shows every map the player could play,
the base game's, those of the map packs installed in the player's Maps
folder and those the catalogues offer, as one grid of cards with their
pictures, says whether each fits the game and the mod being played, and
gets one in a click. Library `oa-ui-map-browser`, headers
`include/oa/ui/map_browser/`, namespace `oa::ui::map_browser`.

The model (`model.hpp`) turns plain cards into what shows: it merges the
three sources, filters and orders them, and says what the primary button
does. It reads no file, clock or network and looks no word up. The screen
(`screen.hpp`) lays the model out at the three size classes, made only of
the UI kit's components ([src/ui/kit](../kit/README.md)), draws it through
the kit, and takes the pointer, a finger, the keys, typed text and the
wheel. Nothing here hosts the screen, fetches, installs or decodes a
picture: the app (`src/app/runtime_map_browser.cpp`) fills the cards from
the runtime's data, hosts the screen on the OA layer, asks its preview cache
([src/ui/map-previews](../map-previews/README.md)) for the pictures the
layout names, and acts on what the screen returns.

## Entry points

- `merge_cards(base, installed, online)`: one card per game name, the base
  maps first, then the installed pack maps, then those online. An online
  map whose name an installed pack already provides, from any registry,
  shows once, as installed (the Library offers its updates and other
  registries' copies), and two registries' copies of an online map show
  once, the first's.
- `apply_filters(browser)`: the cards that match the source (All, Base
  game, Installed packs, Online), any players chip that is on (2, 3 and 4
  a map whose most players is that number, 5–10 one from 5 to 10), any size
  chip that is on (S a longer side of at most 10, M 11 to 20, L above, in
  the size field's own units) and the search (the title, the name or the
  pack's name holds it, ignoring ASCII case). The base maps show first,
  then the installed, then those online, each by title. A selection that
  no longer shows is cleared.
- `primary_action(browser)`: Select for a base or installed map that fits;
  Download and select for an online map that fits or whose fit is not yet
  known; disabled, with its reason, for a map that does not fit; disabled
  "Downloading…" while a download runs.
- `card_word(name)`: a map's game name as a word of an automation name
  (`isle_of_ashes@archipelago` gives `isle-of-ashes-archipelago`).
- `browser_layout(browser, state, frame, fonts, previews)`: the window, the
  size class's dialog size (480×324 at Compact, 720×486 at Regular, 960×600
  at Large), with the kit's header ("Maps", and at its right "Skirmish ·
  playing <mod>", "Battle room · playing <mod>", or Total Annihilation 3.1c
  with no mod), the filter bar (one line at Regular and Large when the
  search keeps 120 points; at Compact two: the sources and the search, then
  PLAYERS and SIZE), a scrolled grid of cards in 3, 4 or 6 columns
  (`kit::card_cells`, each card with `kit::add_card`: its picture or
  "Loading preview", its source chip, its title cut to one line and
  "<players> · <W>×<H>"; a card that does not fit greyed with a "Doesn't
  fit" chip), and the footer: the selected map's title and details
  ("<pack> <version> · <registry> · <players> players · <W>×<H> ·
  <download>"), its fit chips ("Fits TA 3.1c ✓", "Doesn't fit <mod>", "Fit
  with <mod> is checked when it's installed"…) and, for a map that does not
  fit or a download that failed, the reason under them, a progress bar in
  their place while a download runs, and Cancel and the primary button at
  the right. At Compact the chips and the reason take a second line of the
  footer.
- `cards_in_view(...)`: the cards whose cells meet the grid's view and the
  row after them, which alone the layout asks pictures for
  (`PreviewHooks::ask`).
- `draw_browser(canvas, list, size)`: the kit's window face, the display
  list and the kit's window edge.
- `browser_pointer`, `browser_key`, `browser_text`, `browser_wheel`,
  `browser_find` (the controller's Y: the search takes the focus) and
  `focused_field`: the input. They return a `ScreenResult`: nothing, Cancel,
  or choose (the primary button on the selected card), and whether to draw
  again.

## Automation names

Every control is named for the automation endpoint, which lists it as `oa.`
and the name, its dialog `oa.map-browser`: `maps-filter-all`,
`maps-filter-base`, `maps-filter-installed`, `maps-filter-online`;
`maps-players-2`, `maps-players-3`, `maps-players-4`, `maps-players-5-10`;
`maps-size-s`, `maps-size-m`, `maps-size-l`; `maps-search`; one
`maps-card-<word>` per card that shows, its text the map's game name, its
word `card_word` of that name, with `-2`, `-3`… added in grid order to a
word already taken; `maps-cancel`; and `maps-select`, its text "Select",
"Download and select" or "Downloading…".

## State

The model (`Browser`) holds the cards, the filters, the cards that show,
the selection, the picker that opened it, the name of the mod being played
and a download's name, progress and failure. The screen's state
(`ScreenState`) holds the hover, press and focus, the grid's scroll and the
wheel's carry, the search field's text and caret, and a press in the grid
that may become a drag. The layout reads both and changes neither; the
input changes both. Pictures belong to the host's preview cache: a card's
picture refers to its pixels, valid until the host decodes again.

## Invariants

- The layout asks for the pictures of the cards in the grid's view and of
  the row after them, and for no other card's.
- Nothing is drawn outside the window, and nothing outside the kit: every
  part is a kit component or a generic part of the kit's display list,
  every colour a kit token.
- Two controls never share a name, and `kit::name_problem` finds nothing.
- The arrows move the focus by where the controls lie (`kit::focus_toward`),
  in the grid and between the bands; Tab follows the sources, the players,
  the sizes, the search, the grid (entered at the selected card, else the
  first), Cancel and the primary button.
- Every card that shows is a control, scrolled out of view or not, clipped
  to the grid's view, so the arrows reach every card and a card a press
  cannot reach is never pressed.
- A card that does not fit can be selected and never chosen: its primary
  button is disabled.
- Every player word is looked up in the interface catalogue
  (`oa::data::languages::interface_text`) as it is shown.

## Tests

`ui-map-browser` (`tests/model_test.cpp`, `tests/layout_test.cpp`, in a
synthetic font): merging, also when the pack is installed from another
registry; every filter and their combination; the search; the order; the
primary button's states; the cards' words. At each size class: 3, 4 and 6
columns; nothing outside the window, scrolled to the top and the end,
downloading and failed; the footer's buttons; every automation name, sound
and unique, and two maps whose names give one word told apart by `-2`; the
pictures asked for only for the cards in view and the next row, counted on
fake hooks; the focus moving right, left and down by geometry, and up out of
the grid; Tab in the declared order, into the grid at the selected card;
what presses, taps, the keys, typing and the wheel and a drag do.

## Limitations

- The grid's scroll bar shows where the grid is, and takes no press: the
  wheel, a drag, the keys and the focus scroll it.
- A map's size chip reads the first two numbers of its size text; a map
  whose size cannot be read matches no size chip.
- The window is the size class's dialog size whatever the window's room
  beyond it; a larger window shows the same window, centred.
