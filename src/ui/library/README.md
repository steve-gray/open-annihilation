# Library

The Library's model (`oa/ui/library/library.hpp`, `oa::ui::library`): one
list of every package the player can get or has installed, mods, map packs
and language packs together, searchable on the player's own machine and
honest about what each package does to the game. It turns plain inputs into
the Library's state: the entries and their states, which of them show, the
selection, the facts and buttons of the selected entry, and the footer's
download queue lines. It has no drawing, no input handling, no file, clock
or network access, and no app header: the app copies what it knows into
`Inputs`, and the Library screens lay out and draw what the model says.

The Library's screens (`oa/ui/library/screen.hpp`) lay that state out at
the three size classes, made only of the UI kit's components
([src/ui/kit](../kit/README.md)), draw it through the kit's painter, and
take the pointer, a finger, the keys, typed text, the wheel and a
controller. They read the model's visible entries, actions, facts, status
lines, bylines, playing note and queue lines, and decide none of its rules.
Nothing here hosts the screens, fetches or installs: a host lays the screen
out for its frame, draws it, passes its input in and acts on the actions it
returns.

The words both show are in `oa/ui/library/text.hpp`, English with `{place}`
holes, each a `constexpr std::string_view` there and nowhere else, so a
translator finds them together. Every word passes through `Library::text`
(`TextHooks`) before it is shown.

## Entry points

- `refresh(library, inputs)` rebuilds the entries from new inputs:
  - one entry per listing (registry, kind and key), joined with the package
    installed from the same registry, kind and key, and one per installed
    package no listing names (the player's own, or one whose registry is off
    or no longer lists it). Two registries that list one key are two
    entries;
  - its state: UPDATE when an update note names it (blocked or not), else
    PLAYING for the mod being played, else INSTALLED, else GET;
  - whether GET or UPDATE is allowed, and why not, the first reason that
    holds: a game is being played ("Not during a game"); the registry's
    downloads are off; for GET, an engine requirement this build does not
    meet, a base game other than `ta-3.1c`, or hacks the registry does not
    have or this build does not carry out (up to three named, then
    "· N more"); for UPDATE, what its update note says. An INSTALLED or
    PLAYING entry is never blocked, so ROLL BACK and OPEN FOLDER stay. A
    blocked GET is listed with its reason, never hidden;
  - `built_in`, taken only from `Registry::built_in` (its GET and UPDATE
    never ask the player first), `not_reviewed` for every other registry,
    the registry's name, and, on a GET entry, the name of another registry
    the same kind and key is installed from (`replaces_registry_name`).

  It keeps the tab, filter, tag, search and selection. When the selected
  entry no longer shows, the first visible entry is selected and the
  details close.
- `set_tab`, `set_filter`, `set_tag`, `set_query`, `select`,
  `move_selection`, `open_details` and `close_details` browse; each
  recomputes `visible`, the entries shown in their order:
  - Mods, Maps and Languages show their kind, under the filter (All;
    Installed keeps INSTALLED, PLAYING and UPDATE; Updates keeps UPDATE)
    and the tag. The Updates tab shows every UPDATE entry of every kind and
    ignores the filter and tag;
  - with no search: updates, then the other installed entries, then GET
    entries that can be installed, then blocked ones; within each, by name
    folded to lower case, built-in registries first, then registry id;
  - with a search (`rank`, in `search.cpp`): every word must be found; a
    word's class is the best field it is in: the name, a tag that starts
    with it, the author or publisher, the summary. Entries order by the
    worst class among the words, then the sum of the classes, then names
    that start with the first word, then as with no search. The search is
    kept to 64 bytes, cut at a character boundary.
- `tags_in_tab` counts the tags of the entries a tab shows under its
  filter; `update_count` counts the updates that can be installed, the
  number on the Updates tab and the OA button.
- `entry_actions`, `facts`, `status_line`, `byline` and `playing_note`
  describe an entry:
  - the buttons GET or UPDATE (CANCEL while queued, RETRY after a failure
    that can be retried), PLAY NOW, ROLL BACK, OPEN FOLDER and HOMEPAGE,
    each with whether it is enabled;
  - the facts Size, Base, Needs, Hacks, Rules, Maps, Coverage and From, in
    a long and a short form. An update's Rules fact says in words whether
    it changes the game's rules and which saved games stop loading
    ("9c1f 4e0b → 51d2 07aa · changes the game's rules; saved games from
    revision 3 won't load under revision 4"), since a built-in registry's
    update asks no question that would say it. From says "Not reviewed by
    the OA team" for an added registry;
  - the note on updating the mod being played ("Finishes when you leave
    Ridge.").
- `queue_lines` writes the footer: what the queue is doing now, what comes
  next, and a line for a small screen. After an update that changed the
  rules, it says so and that ROLL BACK restores the previous revision.
- `text.hpp` writes sizes in binary units (`size_text`: "41.0 MB",
  "188 MB"), download progress, ages, the header's list age, rules hashes
  ("9c1f 4e0b"), versions ("4.8 · rev 3", "1.2 · 2 maps"), engine
  requirements, what an update changes from and to (`change_texts`:
  "revision 3" and "revision 4", or the versions), and the names of tabs,
  filters, states and buttons.

### The screens

- `library_layout(library, state, frame, fonts)` returns the display list of
  the Library in a frame's window (`window_size`: 480 by 324, 720 by 486 or
  960 by 600 points, never larger than the frame), from its top left corner:
  - **Compact** shows a list page: the header (the OA mark, "Library" and
    the list's age), the tabs with short names ("Mods", "Maps", "Lang",
    "Upd" with the update count), the search field and the filter
    drop-down (All, Installed, Updates, then "Tag: balance (3)" for each
    tag of the tab) or, on the Updates tab, "{n} updates" and UPDATE ALL,
    the list of 28-point rows (the name, then the version, size and an
    added registry's name, and the state's chip, disabled when GET or
    UPDATE cannot act), and the footer (the queue's short line with a
    progress bar while downloading, SETTINGS…, DETAILS and CLOSE). While
    `ScreenState::details_page` and the model's `details_open` are both set
    it shows the selected entry's details page instead
    (`details_page_layout`): its name and version in the header, the way
    back ("‹ Mods"), the status line, the byline, the summary in at most
    three lines, the short facts in label and value columns, the playing
    note, and in the footer the first three actions, MORE ▾ holding the
    rest, and CLOSE;
  - **Regular** shows "Open Annihilation Library" and "v0.8.0 · list from
    4 min ago", the tabs and a 240-point search, a row of chips (All,
    Installed, Updates and the tags while they fit, TAGS ▾ holding the
    rest; on Updates, "{n} updates" and UPDATE ALL), a 420-point list of
    36-point rows (the name and byline, the summary, the version over the
    size, the chip), the details (a 40-point badge beside the name, chip
    and status line, the summary in at most six lines, the long facts, the
    actions three to a line, the playing note), and the footer (both queue
    lines, SETTINGS… and CLOSE);
  - **Large** shows Regular's header, tabs and search, a 180-point filter
    pane (a nav list of All, Installed and Updates, the heading TAGS and the
    tab's tags with their counts, or UPDATE ALL on Updates) in place of the
    chips, a 400-point list of rows of three lines (the tags as chips under
    the summary), and the details.

  A badge is the package's picture, or its letters (`badge_letters`: a
  word's first three, or the first of up to three words) on
  `colour::list_selected`, at 20 points in Compact's rows, 28 in Regular's
  and Large's, and 40 in the details. The Rules and From facts are shown
  whole at every class; the other facts take at most two lines at Compact
  and three in the details pane. The list and the details scroll, each with
  the kit's scroll bar while it holds more than it shows.
- `draw_library(canvas, list)` draws a display list with `kit::paint`.
- `library_pointer`, `library_key`, `library_text`, `library_wheel`,
  `library_command` and `library_stick` take input in the form the OA
  layer's screens take it (`ScreenResult`: the action for the host, the
  entry it is for, and whether to draw again):
  - **Keys** (`kit::Key`): Tab and Shift+Tab follow the declared order: the
    tabs, the search, the filters, the list, the details' actions, the
    footer's buttons; on Compact's details page, the way back, the actions,
    MORE and CLOSE. The arrows move by where controls sit (`kit::key`). On
    the list, Up and Down move the selection and keep the focus there, and
    past its first or last entry the focus leaves it, as it leaves a kit
    scroll area; Page Up and Page Down move by the rows shown, Home and End
    to the ends. Enter on the list opens the details page at Compact and
    moves the focus to the first enabled action at Regular and Large.
    Escape clears a search that has the focus and holds words, else leaves
    the details page, else asks to close. On the search, typed text and the
    editing keys search as they are typed. An open drop-down takes Up,
    Down, Page Up, Page Down, Home, End, Enter, Space and Escape.
  - **Commands** (`Command`), which a host maps: `find` (Ctrl+F, Cmd+F on
    macOS, `/` outside the search, the controller's Y), `next_tab` and
    `previous_tab` (Ctrl+Tab and Ctrl+Shift+Tab, RB and LB), `back` (B: the
    details page to the list, else close) and `first_action` (X: the
    selected entry's GET, UPDATE, CANCEL or RETRY when it is first and
    enabled). The controller's D-pad and left stick are the arrows, its A
    is Space, and its right stick (`library_stick`) scrolls the pane that
    holds the focus.
  - **Pointer and finger**: a press on a row selects it, and at Compact
    opens its details page; no press, double press or tap on a row gets,
    updates or installs anything. A finger reaches the nearest control
    within its reach (`kit::finger_down`); a finger that moves farther than
    its reach in the list or the details scrolls that pane and presses
    nothing. A press outside an open drop-down only closes it. The wheel
    scrolls the pane under the pointer, or an open drop-down's items.
- Every control is named for automation (the OA layer lists each as `oa.`
  and its name):
  - `library.tab.mods`, `library.tab.maps`, `library.tab.languages`,
    `library.tab.updates` (their text holds the count: "Updates 2");
    `library.search`;
  - `library.filter` (Compact's drop-down, whose items, while it is open,
    are `library.filter.all`, `.installed`, `.updates` and `.tag-<tag>`),
    or the chips or nav entries `library.filter.all`,
    `library.filter.installed` and `library.filter.updates`; the tags'
    chips or nav entries `library.tag.<tag>`, and `library.tags` (TAGS ▾,
    its items `library.tags.<tag>` while it is open);
  - `library.list` (the list, kind `list`, no text), and one control for
    each row that shows, kind `list_item`, its text the entry's name:
    `library.row.<registry>.<key>`, or `library.row.<key>` for a package of
    the player's own, each word lower-cased with other characters as
    hyphens;
  - `library.action.get`, `.update`, `.update-all`, `.cancel`, `.retry`,
    `.roll-back`, `.play-now`, `.open-folder` and `.homepage`; on Compact's
    details page MORE ▾ is `library.action`, whose open items are the
    actions after the first three under those same names;
  - `library.details`, `library.back`, `library.settings`, `library.close`;
  - the labels `library.status`, `library.queue`, `library.note`,
    `library.from` and `library.rules`: controls of kind `area` that are
    neither enabled nor focusable, whose text is the words shown (the From
    fact's long value, and the Rules fact's value whole, short at Compact
    and long at Regular and Large).
- `focused_field(state, list)` gives the search field while it has the
  focus, over which a host keeps the system's text input started.

## State

None beyond the `Library` value the caller holds: its inputs, entries,
browsing state and selection. The model keeps nothing else between calls.
The screens keep their hover, press and focus, the two panes' scroll and the
wheel's carry, the search field, the open drop-down and the Compact details
page in the caller's `ScreenState`. The input reads where the screen's
controls and panes are from the display list it is given, the one the
screen shows. A display list's badges point into the model's inputs, and
stay valid until the model's next refresh.

## Invariants

- Pure: no file, clock, network or random access; the same inputs and calls
  give the same state on every machine.
- `Entry` pointers point into the owning `Library::inputs` and stay valid
  until the next `refresh`. A copied `Library` still points into the
  original's inputs: refresh the copy before reading its entries.
- The selection is always one of the visible entries, or none when none
  shows.
- Every text comes from a pattern in `text.hpp`, looked up whole through
  `Library::text` (`TextHooks`) before its places are filled; with no
  lookup it is English.
- The screens are made only of the kit's components and its display list's
  generic parts; every colour is a kit token and every size a kit metric,
  but for the panes' widths the design gives (Compact's 104 points beside
  the search, the 240-point search, the 420- and 400-point lists, the
  180-point filter pane and the details' 40-point badge). `draw.cpp` only
  paints the display list.
- The layout decides no rule of the model: which entries show, in which
  order, their states, what blocks them and their actions are the model's.
- Every action Regular offers is reachable at Compact: on the list page, or
  on the details page, its first three as buttons and the rest in MORE ▾.
- No two controls meet, but the list and the rows it holds; a row that does
  not show has no control. Every control that takes the focus is in Tab's
  declared order and reached by the arrows from the first tab.
- An update's Rules fact is shown whole at every class, never cut.
- A press on a row only selects it, and at Compact opens its details.

## Tests

`ui-library` builds Libraries over made-up registries and packages: the
state of a listing alone, a package installed alone, matched with and
without an update, played with and without one, a blocked update, and each
queue phase with its status line and button; every reason GET or UPDATE is
off and their order; installed entries never blocked; built-in and added
registries, two registries with one key, and a key installed from another
registry; tabs, filters and tags with their counts; the order with no
search; the update count; the selection across refreshes and browsing; the
buttons of every state; the facts with the sample numbers, the Rules fact
for an unchanged, changed and unknown rules hash in both lengths; status
lines, bylines and the playing note; the queue lines, after an update that
changed the rules and one that kept them; `change_texts` for a new
revision and a new version; every text function; and a lookup that
translates.

`ui-library-search` checks that a name beats a tag, a tag an author or
publisher and those a summary; several words; a word in no field hiding
the entry; ASCII folding; tag prefixes; the 64-byte cap at a character
boundary; and ties ordered by name and then registry.

`ui-library-layout` lays the screens out over a fixture of fifteen
packages of the three kinds from a built-in and an added registry (an
update of the built-in Ridge that changes the rules, a blocked update, the
mod being played, a GET blocked by three missing hacks, and four queue
items: downloading, waiting, failed, and an installed update that changed
the rules), in a synthetic font, at Compact, Regular and Large and in the
windows of 640x480 at 1x, 1920x1080 at 2x and 2560x1440 at 2x. At each it
checks that every part lies inside the window; that no two controls or
press areas meet; that every action of the selected entry is a control
(at Compact on its details page, MORE ▾ open), UPDATE ALL on the Updates
tab, and SETTINGS… and CLOSE; that Tab follows the declared order and the
arrows reach every control that takes the focus from the first tab, and
no label; that the names are the screen's and unique; that every text
fits by the kit's measure, and that the Rules and From facts read their
whole values. Regular shows more rows than Compact, and Large has the
filter pane. It then drives the screens: Enter at Compact and at Regular,
Escape's three cases, the tab commands both ways, the controller's X, Y
and B, the list's keys, Tab's walk, presses, double presses and taps on
rows (which never get anything), an action's button, the labels taking no
press, the wheel and a finger's drag, typing and Backspace, the filter
drop-down and a press outside it, the tag chips, and MORE ▾.

`ui-library-draw-data` lays out and draws, in the installed game's fonts
and the modern fonts beside the test, Compact in the 640x480 window,
Regular in 1920x1080 and Large in 2560x1440 at 2x, and Compact's details
page: each twice, byte for byte the same, with nothing drawn outside the
window and every pixel of it drawn, every part inside it and every text
fitting. `oa-ui-library-draw-test --data --frames DIR` also writes
`library-compact.png`, `library-regular.png`, `library-large.png` and
`library-details-compact.png` for review.

## Limitations

- Folding is ASCII only: letters outside A to Z match only themselves.
- A tag is matched from its start, never inside it.
- An update that cannot be installed is described with the detail its
  update note carries, as the app wrote it; the model does not reread the
  catalogue for it.
- The screens are not hosted yet: the OA layer's Library screen fills the
  inputs, places the window and maps the platform's keys and the
  controller to `kit::Key` and `Command`.
- Only rows that show have controls: a journey scrolls or searches before
  it presses a row that does not show.
- A badge's letters are drawn in the kit's ink for the accent
  (`colour::on_accent`) on `colour::list_selected`, which shows them only
  faintly; the row's look is the kit's.
- Large shows as many tags in its filter pane as fit, and TAGS ▾ holds the
  rest; the filter pane does not scroll.
- The scroll bars show where the panes are scrolled but take no press: the
  wheel, the keys, the right stick and a finger scroll them.
