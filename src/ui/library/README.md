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
    entries. It then makes the base game entry (`base_game_id`): a mod, no
    registry, key `@base-game`. A package key is lower-case kebab-case, so
    it cannot hold `@`. That entry is not a package: it has no listing, no
    installed copy and no update, whatever a folder of the same key held;
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

  It keeps the tab, each tab's filter, the tag, search and selection. When
  the selected entry no longer shows, the first visible entry is selected
  and the details close.
- `set_tab`, `set_filter`, `set_tag`, `set_query`, `select`,
  `move_selection`, `open_details` and `close_details` browse; each
  recomputes `visible`, the entries shown in their order:
  - the tabs are Active, Mods, Maps, Languages, Updates and Registries,
    in that order. A Library opens on Active (`Library::tab`), which lists
    no entry and selects none: it shows the player's setup (`active_setup`,
    below). Registries lists the registries (`Inputs::registries`) and has
    no search, no filter and no package list;
  - Mods, Maps and Languages show their kind and Updates every kind, under
    the tab's own filter (`Library::filters`, `shown_filter`) and the tag:
    All; New keeps the tab's ten listings (`newest_count`) with the latest
    `Listing::published` day, ties by name, and never a listing without a
    day, so a package's release and revision play no part; Installed keeps
    INSTALLED, PLAYING and UPDATE; Updates keeps UPDATE. The base game
    entry shows on Mods under All and Installed only, including when a
    search matches its name, and on no other tab or filter. New never
    holds it. Each tab keeps its filter when another is shown: Updates
    starts on Updates and the others on All;
  - with no search: under New, the latest day first, then by name; under
    the others, updates, then the other installed entries, then GET
    entries that can be installed, then blocked ones; within each, by name
    folded to lower case, built-in registries first, then registry id;
  - with a search (`rank`, in `search.cpp`): every word must be found; a
    word's class is the best field it is in: the name, a tag that starts
    with it, the author or publisher, the summary. Entries order by the
    worst class among the words, then the sum of the classes, then names
    that start with the first word, then as with no search. The search is
    kept to 64 bytes, cut at a character boundary. Whenever the list keeps
    the base game entry, it is moved to the front after that order;
- `tags_in_tab` counts the tags of the entries a tab shows under its
  filter; `filter_count` counts the packages a filter keeps on the tab
  shown before the tag and the search, so no count moves with them, and
  leaves the base game entry out of every count, and
  `filter_caption` writes it as the filters show it, "Installed (4)"
  (`words::filter_with_count`); `update_count` counts the updates that can
  be installed, the number on the Updates tab and the OA button, and
  `updates_shown` the UPDATE entries the list shows, the Updates tab's
  "{n} updates".
- `active_setup` describes the Active tab from the inputs: the mod being
  played, the installed mod marked playing (its listing's name, badge and
  author when a registry lists it) or else `Inputs::playing_mod`, with its
  version ("Highland 0.4.0") and the release the tile states
  ("0.4.0 (rev 1)"), or the base game ("Base Game", "Total Annihilation
  3.1c", "No mod: the game as it was made"); the language in use, its name
  in itself and in English, its pack, version, revision and coverage
  (`Inputs::language`: "Polski 1.0", "Coverage 92%") or the language the
  game holds itself ("English · built in", "Built in"); and each installed
  map pack (`Inputs::map_packs`) by name, with its badge when an installed
  pack of that name has one, and its maps' names, players ("4 players")
  and sizes.
- `entry_actions`, `facts`, `status_line`, `byline` and `playing_note`
  describe an entry:
  - the buttons GET or UPDATE (CANCEL while queued, RETRY after a failure
    that can be retried), PLAY NOW, or SWITCH OFF on the mod being played,
    ROLL BACK, UNINSTALL, OPEN FOLDER and HOMEPAGE, each with whether it is
    enabled: PLAY NOW, SWITCH OFF and ROLL BACK are off during a game, and
    PLAY NOW and SWITCH OFF while `Inputs::mod_from_command_line` says
    `--mod-dir` or `--base-game` chose the mod;
  - the facts Installed, Latest, Author, Size, Base, Needs, Hacks, Rules,
    Maps, Coverage and From, in a long and a short form. Installed and
    Latest are the installed copy's and the listing's version and revision
    (`release_text`: "4.8 (rev 3)", "1.0" for a revision of 0, "rev 3"
    without a version), and Author is the listing's author, else its
    publisher, else the installed manifest's (`Installed::author`); each is
    left out when it has nothing to say. An update's Rules fact says in words whether
    it changes the game's rules and which saved games stop loading
    ("9c1f 4e0b → 51d2 07aa · changes the game's rules; saved games from
    revision 3 won't load under revision 4"), since a built-in registry's
    update asks no question that would say it. From says "Not reviewed by
    the OA team" for an added registry;
  - the byline names the author, or the publisher when there is no author.
    When the listing carries a download count it continues
    " · 12,400 downloads". With no author and no publisher the count is
    the whole line ("1 download"). A listing with no count shows nothing
    more. The count does not change an entry's state, order or search rank;
  - the note on updating the mod being played ("Finishes when you leave
    Ridge."), after "The command line chose this run's mod." on every
    installed mod while the command line chose it;
  - the status line says "No oamod.yaml present" for a mod folder without
    one (`Installed::without_manifest`), installed or played.
- `entry_summary` gives the words under the details' status line: the
  listing's summary, else the installed manifest's description
  (`Installed::description`), as for a mod of the player's own.
- `can_check_now` says whether the header's refresh icon can be pressed: a
  registry is on that it can ask (`Inputs::can_check`), none is being asked
  (`Inputs::checking`), and no game is being played. While one is being
  asked the icon turns and its text is "Refreshing". The check after a
  list was read does not disable it.
- `queue_lines` writes the footer: what the queue is doing now, what comes
  next, and a line for a small screen. After an update that changed the
  rules, it says so and that ROLL BACK restores the previous revision.
- `text.hpp` writes sizes in binary units (`size_text`: "41.0 MB",
  "188 MB"), download counts (`downloads_text`: "1 download",
  "12,400 downloads"), download progress, ages, rules hashes
  ("9c1f 4e0b"), versions ("4.8 · rev 3", "1.2 · 2 maps"), a release as
  the facts give it (`release_text`: "4.8 (rev 3)"), engine
  requirements, what an update changes from and to (`change_texts`:
  "revision 3" and "revision 4", or the versions, and "the old copy" or
  "the new copy" for a side no version or revision above 0 names), and the
  names of tabs, filters, states and buttons.
- No text says "Revision 0" or "rev 0". A catalogue that names no
  revision, as Core Prime's does for language packs, leaves it 0: the
  status line then says "Update available · 3.4 MB to download" for an
  update of the same version (`status_update_available`); "Revision
  {revision}" is written only for a revision above 0 of the version
  installed (`status_line`), and the update question's revision heading
  only when both revisions are above 0.

### The update question

`update_question.hpp` holds the question asked before an update that may
stop saved games loading, and the one place its rule lives. The Library's
UPDATE and UPDATE ALL go through it (the app's `Runtime::request_update`),
so the screens and the tests share its words.

- `asks_before_update(entry, update)` is true only for a mod
  (`Kind::mod`) from a registry the player added (`!Entry::built_in`, which
  `refresh` takes from `Registry::built_in`) whose update changes the rules
  (`UpdateNote::Rules::changes`) or whose rules cannot be known
  (`unknown`), since saved games load only under the same rules hash. It is
  false for an update that keeps the rules, for a language or map pack,
  whose rules are always the same, and for every built-in registry's
  package: downloads from the official registry never ask. Their rules
  change shows on the row and in the Rules fact instead, before UPDATE, and
  in the queue line after it.
- `update_question(entry, update, playing)` writes the question, or nothing
  when it is not asked: "Update {name}"; the heading "{name} 4.8 ·
  revision 3 → 4" for a new revision, or "{name} 4.8 → 4.9" for a new
  version; the body, "Revision 4 changes the game's rules. Saved games from
  revision 3 won't load under revision 4. Revision 3 is kept, and ROLL BACK
  brings it back.", or for an unknown rules hash "OA can't tell whether
  revision 4 changes the game's rules. …", with "It finishes when you leave
  {name}." for the mod being played; and the facts Rules ("9c1f 4e0b →
  51d2 07aa", or "unknown") and Download (`size_text`).
- `update_all_question(entries)` writes UPDATE ALL's one question over the
  entries whose updates ask, or nothing when none does: "Update all", and
  "N updates change the game's rules: …", naming up to three, then "and N
  more" ("1 update changes …" for one). The others start at once.
- `update_prompt(question)` makes the kit question that asks it: the
  heading, the body and each fact ("Rules: …") as its paragraphs, then NOT
  NOW (`not-now`) and UPDATE (`update`), NOT NOW marked at first and
  answering Escape, B and N, UPDATE with the primary look. Its word is the
  kit's default, so automation lists `oa.prompt.not-now`,
  `oa.prompt.update` and `oa.prompt.body`.

### The questions about the mod played

`mod_questions.hpp` holds what the app asks and tells over the Library
about the mod the game plays, as kit questions whose word is the kit's
default (`oa.prompt.<id>` to automation):

- `switch_off_question()`: SWITCH OFF's **SWITCH MOD**, "Switch to No Mod
  now? The game reloads its data for the new mod and returns to the main
  menu. Your other settings are kept.", with CANCEL (`cancel`), marked
  and answering Escape, B and N, and SWITCH (`switch`), primary, which Y
  answers;
- `switch_refused_note(reason)`: why PLAY NOW could not switch, under the
  same title, with OK (`ok`);
- `roll_back_question(title, from, to, playing)`: ROLL BACK's **ROLL BACK
  MOD**, "Roll back {title} to {to}? {from} is kept in its place, and ROLL
  BACK again brings it back.", with "The game reloads its data for the mod
  and returns to the main menu." for the mod the game plays, CANCEL
  (`cancel`) marked and ROLL BACK (`roll-back`) primary.

### Uninstalling

`uninstall.hpp` holds UNINSTALL's rules, its question and its notices. The
app removes the folder (`Runtime::library_uninstall`,
`package_install::uninstall_package`).

- `entry_actions` shows UNINSTALL on every installed entry, after ROLL BACK
  and before OPEN FOLDER, never as the main button. `uninstall_off_reason`
  turns it off, and says why, for a map pack while a game is played ("Map
  packs can't be uninstalled during a game.") and for any package with a
  queue item that has not failed or been installed ("It is in the download
  queue: CANCEL it before you uninstall it."). `details_note` adds that
  reason after `playing_note`'s, as the note under the buttons.
- `uninstall_step(library, entry)` says what pressing it leads to: nothing
  while it is off, a notice for the mod being played
  (`Installed::playing`) and for a language pack the game shows its words
  from (`Installed::in_use`), and the question for every other installed
  package.
- `uninstall_question(entry, folder)` makes the kit question: "Uninstall
  {name}", "Uninstall {name}? Its folder, {folder}, is removed." with ",
  and with it the copy ROLL BACK keeps." when a copy is kept, then "GET
  installs it again." for a package a registry lists, or "No registry
  lists it, so the Library cannot get it again."; CANCEL (`cancel`) marked
  and answering Escape, B and N, and UNINSTALL (`uninstall`) primary. Its
  word is the kit's default, so automation lists `oa.prompt.cancel` and
  `oa.prompt.uninstall`.
- `uninstall_notice(entry, step)` makes the notice for a package in use,
  and `uninstall_failed_notice(name, problem)` the one for an uninstall
  that removed nothing (busy, changed, a link, outside the player's
  folders, or not moved), each with one OK (`ok`).

### The screens

- `library_layout(library, state, frame, fonts)` returns the display list of
  the Library in a frame's window (`window_size`: 480 by 324, 720 by 486 or
  960 by 600 points, never larger than the frame), from its top left corner:
  - **The Active tab**, at every class, shows under the tabs, with no
    search and no filters, the headings MOD, LANGUAGE and MAPS over tiles.
    The mod tile has a large badge, the name, the version and revision
    and the author, with Playing as a pill on the title's line at the top
    right, or Base Game with the 3.1c look and "No mod: the game as it was
    made" and no pill. While a mod is played, UNLOAD MOD
    (`library.active.unload`, kind `button`, text "UNLOAD MOD") sits at the
    mod tile's bottom right. It is accent while it can act and plain while
    disabled, by the same rule as SWITCH OFF, and a press asks for
    `Action::switch_off` of the mod being played. The language tile has the
    pack's badge or a language mark, the name in itself and in English,
    the version and revision and the coverage, or Built in as a neutral
    pill on the title's line. Each map pack is a tile with its badge, its
    name and its maps (name, players, size). Badge, title and lines start
    at the top of the tile, with the same padding above them and below the
    last line or the button. Compact stacks the tiles; Regular and Large
    put the mod and the language side by side, the titles on one line and
    the tiles the same height; Large puts the map packs in two columns of
    equal height when more than one is installed. The mod label stops
    above UNLOAD MOD, the language label covers its tile, and each map's
    label covers its line. They scroll as the details do when they are
    taller than the window. The labels take no press; UNLOAD MOD does, and
    Tab and the arrows reach it while it is enabled. Compact's footer
    holds no DETAILS there;
  - **The Registries tab**, at every class, shows under the tabs, with no
    search and no filters, one row for each registry: its avatar (the
    initials, `registry_initials`, on the accent for a built-in registry
    and on `colour::switch_idle` for one the player added), its name with
    a Built in chip when it ships with the game, its address, when it was
    last read (`registry_refresh_line`: "refreshed 5 min ago", "never
    refreshed", or "couldn't reach · 2 h ago"), its subtitle
    (`registry_subtitle`: how many packages, "not reviewed" for one the
    player added, and how many mirrors) and an Off/On switch. A key icon
    shows the keys the catalogue is checked against when the pointer, the
    keys or the pad rest on it (each fingerprint and full public key, and
    "built in" or "added by you"); pressing it asks for nothing. REFRESH
    sits beside the switch and asks the host to refresh that registry
    (`Action::refresh_registry`); it is greyed while the registry is off
    or already being asked, and the refresh mark beside it turns while it
    is. When the ask reads a catalogue the mark shows a check
    (`ButtonLook::check_ms`) and then the arrow; a failed ask leaves the
    arrow. REMOVE and ADD MIRROR… sit under the subtitle for one the player
    added. A note under the list (`registry_note`) names the built-in
    registries, and a refusal (`Inputs::registries_notice`) stands over it
    in the lock colour. The rows and the note scroll as the details do.
    ADD REGISTRY… sits at the top right, above the list, and joins Tab's
    order after the rows; the footer holds CLOSE and no DETAILS. During a
    match the padlock and "Choose from the main menu." stand where ADD
    REGISTRY… was, and ADD MIRROR… gives way to the same lock; the
    switches, REFRESH and REMOVE stay. A switch, REFRESH, REMOVE, ADD
    MIRROR… and ADD REGISTRY… ask the host (`Action::set_registry`,
    `refresh_registry`, `remove_registry`, `add_mirror`, `add_registry`);
    the host applies a switch and a removal at once, and asks the content
    service to refresh that one registry;
  - **Compact** shows a list page: the header (the OA mark, "Library", this
    build's version, and a refresh icon at the top right; "offline" in the
    lock colour beside it when the last refresh could not reach a registry),
    the tabs with short names and their All counts ("Active", "Mods 11",
    "Maps 2", "Lang 2", "Upd 15", "Reg 2"; Updates' digits are amber while
    its count is above 0), the search field and the filter
    drop-down (All, New, Installed and Updates with their counts, "All
    (11)", then "Tag: balance (3)" for each tag of the tab), on the
    Updates tab "{n} updates" and UPDATE ALL on a line over the list, the
    list of 28-point rows (the name, then the version, size and an
    added registry's name, and the state's icon: a download arrow for GET,
    a circled up-arrow for UPDATE, a circled check for INSTALLED and
    PLAYING, dimmed when GET or UPDATE cannot act), and the footer (the queue's short line with a
    progress bar while downloading, DETAILS and CLOSE, CLOSE drawn as the
    main button). While
    `ScreenState::details_page` and the model's `details_open` are both set
    it shows the selected entry's details page instead
    (`details_page_layout`): its name and version in the header, the way
    back ("‹ Mods"), the status line, the byline, the summary in at most
    three lines, the short facts in label and value columns, the playing
    note, and in the footer the first three actions, MORE ▾ holding the
    rest, and CLOSE;
  - **Regular** shows "Open Annihilation Library" and "v0.8.0", the same
    refresh icon, the tabs with their All counts and a 240-point search, a row of chips (All, New,
    Installed and Updates with their counts, and the tags while they fit,
    TAGS ▾ holding the rest; on Updates, "{n} updates" and UPDATE ALL on a
    line over the list), a 420-point list of
    36-point rows (the name and byline, the summary, the version over the
    size, the state's icon), the details (a 40-point badge beside the
    name, its state's icon and the status line, the summary in at most six lines, the long facts, the
    actions three to a line, the playing note), and the footer (both queue
    lines and CLOSE);
  - **Large** shows Regular's header, tabs and search, a 180-point filter
    pane (a nav list of All, New, Installed and Updates with their counts,
    the heading TAGS and the tab's tags with their counts) in place of the
    chips, a 400-point list of rows of three lines (the tags as chips under
    the summary), on Updates under "{n} updates" and UPDATE ALL, and the
    details.

  A badge is the package's picture, or its letters (`badge_letters`: a
  word's first three, or the first of up to three words) in the kit's ink
  for the accent (`colour::on_accent`) on `colour::hint`, which reads at
  every class, at 20 points in Compact's rows, 28 in Regular's
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
    tabs, the search, the filters, UPDATE ALL and the list, the details'
    actions, the footer's buttons (on Active, the tabs and the footer's
    buttons alone; on Registries, the tabs, each registry's switch, REMOVE
    and ADD MIRROR…, and the footer's buttons); on Compact's details page, the way back, the actions,
    MORE and CLOSE. The arrows move by where controls sit (`kit::key`). On
    the list, Up and Down move the selection and keep the focus there, and
    past its first or last entry the focus leaves it, as it leaves a kit
    scroll area; Page Up and Page Down move by the rows shown, Home and End
    to the ends; on Active and on Registries, the page keys, Home and End
    scroll those lines.
    Enter on the list opens the details page at Compact and moves the
    focus to the first enabled action at Regular and Large.
    Escape clears a search that has the focus and holds words, else leaves
    the details page, else asks to close. On the search, typed text and the
    editing keys search as they are typed. An open drop-down takes Up,
    Down, Page Up, Page Down, Home, End, Enter, Space and Escape.
  - **Commands** (`ScreenCommand`), which a host maps: `find` (Ctrl+F, Cmd+F on
    macOS, `/` outside the search, the controller's Y), `next_tab` and
    `previous_tab` (Ctrl+Tab and Ctrl+Shift+Tab, RB and LB), `back` (B: the
    details page to the list, else close) and `first_action` (X: the
    selected entry's GET, UPDATE, CANCEL or RETRY when it is first and
    enabled). The controller's D-pad and left stick are the arrows, its A
    is Space, and its right stick (`library_stick`) scrolls the pane that
    holds the focus.
  - **Pointer and finger**: a press on a row selects it, and at Compact
    opens its details page; no press, double press or tap on a row gets,
    updates or installs anything, but for its icon: a row's download arrow
    is its GET and its circled up-arrow its UPDATE, a control of their own
    tried before the row, which select the row and ask for that action
    while the details' first button is that action and enabled, and take
    no press otherwise (blocked, queued or failed), the press reaching the
    row. A finger reaches the nearest control
    within its reach (`kit::finger_down`); a finger that moves farther than
    its reach in the list or the details scrolls that pane and presses
    nothing. A press outside an open drop-down only closes it. The wheel
    scrolls the pane under the pointer, or an open drop-down's items.
- Every control is named for automation (the OA layer lists each as `oa.`
  and its name):
  - `library.tab.active`, `library.tab.mods`, `library.tab.maps`,
    `library.tab.languages`, `library.tab.updates` (their text holds the
    count: "Updates 2"), `library.tab.registries`; `library.search`;
  - on the Registries tab `library.registry.<id>` (its halves `.off` and
    `.on`), `.key` (its text the fingerprints, one a line) and `.refresh`,
    and for a registry the player added `.remove` and `.add-mirror`; the
    labels `.address` and `.refreshed`; `library.add-registry`; the labels
    `library.registries.note` and `library.registries.notice`;
  - `library.filter` (Compact's drop-down, whose items, while it is open,
    are `library.filter.all`, `.new`, `.installed`, `.updates` and
    `.tag-<tag>`), or the chips or nav entries `library.filter.all`,
    `library.filter.new`, `library.filter.installed` and
    `library.filter.updates`, their text the filter and its count ("All
    (11)"); the tags' chips or nav entries `library.tag.<tag>`, and
    `library.tags` (TAGS ▾, its items `library.tags.<tag>` while it is
    open);
  - `library.list` (the list, kind `list`, no text), and one control for
    each row that shows, kind `list_item`, its text the entry's name:
    `library.row.<registry>.<key>`, or `library.row.<key>` for a package of
    the player's own, each word lower-cased with other characters as
    hyphens; a GET or UPDATE row's icon, after it, is
    `library.row.<registry>.<key>.get` or `.update`, kind `button`, its
    text "GET" or "UPDATE", listed disabled while it takes no press, and
    never in Tab's order;
  - `library.action.get`, `.update`, `.update-all`, `.cancel`, `.retry`,
    `.roll-back`, `.play-now`, `.switch-off`, `.open-folder` and
    `.homepage`; on Compact's
    details page MORE ▾ is `library.action`, whose open items are the
    actions after the first three under those same names;
  - `library.refresh` (the header's icon, kind `button`, its text "Refresh",
    or "Refreshing" while the registries are being asked; disabled while no
    registry can be asked, while one is being asked and during a game),
    `library.details`, `library.back`, `library.close`;
  - the labels `library.status`, `library.queue`, `library.note`,
    `library.from` and `library.rules`: controls of kind `area` that are
    neither enabled nor focusable, whose text is the words shown (the From
    fact's long value, and the Rules fact's value whole, short at Compact
    and long at Regular and Large);
  - on the Active tab the labels `library.active.mod` ("Highland 0.4.0",
    or "No mod: the game as it was made"), `library.active.language`
    ("Polski 1.0") and `library.active.map.<n>`, from 1 in the order shown
    ("Ridgeline · 4 players · 16x16"), and while a mod is played the button
    `library.active.unload` ("UNLOAD MOD"), disabled during a game and
    while the command line chose the mod. The mod label is the part of the
    tile above that button, the language label the part of its tile that
    shows, and a map's the part of its line that shows, empty while that
    part is scrolled out; at most 4,000 maps are named.
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
  but for the panes' widths the design gives (Compact's 128 points beside
  the search for the filter and its count, the 240-point search, the 420- and 400-point lists, the
  180-point filter pane and the details' 40-point badge). `draw.cpp` only
  paints the display list.
- The layout decides no rule of the model: which entries show, in which
  order, their states, what blocks them, their actions, the filters'
  counts and what the Active tab says are the model's.
- On the Active tab only UNLOAD MOD takes a press, a key or Tab, and only
  while a mod is played and SWITCH OFF would act. The labels do not.
- Every action Regular offers is reachable at Compact: on the list page, or
  on the details page, its first three as buttons and the rest in MORE ▾.
- No two controls meet, but the list and the rows it holds and each row
  and its icon; a row that does not show has no control. Every control that takes the focus is in Tab's
  declared order and reached by the arrows from the first tab.
- An update's Rules fact is shown whole at every class, never cut.
- A press on a row only selects it, and at Compact opens its details; only
  its download arrow or up-arrow asks for GET or UPDATE, by the same rule
  as the details' button.

## Tests

`ui-library-update-question` builds Libraries over a built-in and an added
registry: `asks_before_update` and `update_question` for an added
registry's mod whose rules change or are unknown (asked), one that keeps
them, the built-in registry's whose rules change and whose are unknown, and
a language and a map pack (never asked); the design's words exactly, with
the sample numbers; a new version and an unknown rules hash; the mod being
played; UPDATE ALL over a built-in and an added registry's rules-changing
mods naming only the added one, none to ask, and five named as three and
"2 more"; the kit question's paragraphs, NOT NOW marked and UPDATE primary,
its automation names and its text fitting at every class; the questions
about the mod played, their words, buttons and marks; an update that names
no revision, whose status line and question never say "Revision 0"; and a
lookup that translates.

`ui-library` builds Libraries over made-up registries and packages: the
state of a listing alone, a package installed alone, matched with and
without an update, played with and without one, a blocked update, and each
queue phase with its status line and button; every reason GET or UPDATE is
off and their order; installed entries never blocked; built-in and added
registries, two registries with one key, and a key installed from another
registry; the Library opening on Active; tabs, each tab's own filter, the
Updates tab's filters across every kind, and tags with their counts; the
filters' counts unmoved by the tag and the search; New's ten newest by
day, ties by name, never an undated listing, on Mods, Maps and Updates;
the Active tab's mod, language and map packs in each case; the order with
no search; the update count; the selection across refreshes and browsing;
the buttons of every state, SWITCH OFF on the mod played and PLAY NOW and
SWITCH OFF off while the command line chose the mod, its note, and when
the refresh icon can be pressed; the summary, the manifest's description standing
in for a listing's for a mod of the player's own, and "No oamod.yaml
present" for a folder without one; the facts with the sample numbers, Installed,
Latest and Author among them, a revision of 0 left out, a version left
out, a fact with nothing to say left out and the manifest's author
standing in; the Rules fact for an unchanged, changed and unknown rules
hash in both lengths; status lines, "Update available" for an update
that names no revision, bylines with and without a download count, and
the playing note; the queue lines, after an update that changed the
rules and one that kept them; `change_texts` for a new revision, a new
version and a side that names no revision; every text function,
`release_text` among them, and the Registries tab's initials, ages,
subtitles and note; and a lookup that translates.

`ui-library-uninstall` builds a Library over a built-in registry, the
player's own mod, the mod being played, a map pack and two language packs,
one of them in use: UNINSTALL on every installed package and on none
other, in its place among the buttons; off for a map pack during a game
and in each queue phase but failed and installed, with the note saying
why; the step for every kind and use; the question's words with and
without a kept copy, for a listed package and the player's own, CANCEL
marked and UNINSTALL primary; the notices' words for the mod being played,
the language in use and each problem, each with one OK; and a lookup that
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
(at Compact on its details page, MORE ▾ open), UPDATE ALL over the list
on the Updates tab beside the Mods tab's filters, the refresh icon and CLOSE;
that the Active tab is first, has no search, filter, list or DETAILS,
reads its mod, language and maps as labels, shows Playing and Built in
as pills, and while a mod is played puts UNLOAD MOD in Tab's order with
the tabs and the footer (absent on the base-game tile, and out of that
order while disabled); that the Registries tab lists each registry's
switch, key icon and REFRESH, its address and refresh time, REMOVE and
ADD MIRROR… only for one the player added, and ADD REGISTRY… at the top
right above the list, with no search or DETAILS; that resting on the key
icon or focusing it shows that registry's fingerprints and public keys,
and pressing it asks for nothing; that REFRESH asks for that registry and
is greyed, with the refresh mark turning beside it, while the registry is
off or already being asked; that a refresh which reads a list shows the
check and then the arrow, and one which fails shows the arrow; that a
switch's halves, Space, REMOVE, ADD
MIRROR… and ADD REGISTRY… ask for those actions, with ADD REGISTRY… and
ADD MIRROR… absent during a match; that
Tab follows the declared order and the
arrows reach every control that takes the focus from the first tab, and
no label; that the names are the screen's and unique; that every text
fits by the kit's measure, and that the Rules and From facts read their
whole values; and that a mod of the player's own shows its description in
the details. Regular shows more rows than Compact, and Large has the
filter pane. It then drives the screens: Enter at Compact and at Regular,
Escape's three cases, the tab commands both ways, the controller's X, Y
and B, the list's keys, Tab's walk, presses, double presses and taps on
rows (which never get anything), a row's up-arrow and download arrow at
every frame (named after the row, a button whose text is the word,
pressed as UPDATE and GET, and listed disabled and passing the press to
the row while blocked or queued), an action's button, the refresh icon greyed
until a registry can be asked and then asking, and saying "Refreshing" while
one is being asked, the labels taking no
press, the wheel and a finger's drag, typing and Backspace, the
filter drop-down with its counts, each tab keeping its filter, New's
chip, and a press outside the drop-down, the tag chips, MORE ▾, and the
Active tab with forty more maps: its lines taking no press, UNLOAD MOD
asking what SWITCH OFF asks and greyed during a game, and the tiles scrolling
by the wheel, the page keys, Home, End and the right stick.

`ui-library-draw-data` lays out and draws, in the installed game's fonts
and the modern fonts beside the test, Compact in the 640x480 window,
Regular in 1920x1080 and Large in 2560x1440 at 2x, Compact's details
page, the Active tab at Compact, Regular and Large, and the Updates tab at
Compact and Large: each twice, byte for byte the same, with nothing drawn outside the
window and every pixel of it drawn, every part inside it and every text
fitting. `oa-ui-library-draw-test --data --frames DIR` also writes
`library-compact.png`, `library-regular.png`, `library-large.png`,
`library-details-compact.png`, `library-active-compact.png`,
`library-active-regular.png`, `library-active-large.png`,
`library-updates-compact.png` and
`library-updates-large.png` for review.

## Limitations

- Folding is ASCII only: letters outside A to Z match only themselves.
- A tag is matched from its start, never inside it.
- An update that cannot be installed is described with the detail its
  update note carries, as the app wrote it; the model does not reread the
  catalogue for it.
- The OA layer's Library screen ([src/app](../../app/README.md#the-library))
  fills the inputs, places the window and maps the keyboard's own keys to
  `ScreenCommand`; the controller's Y, X, LB, RB and right stick do not
  reach `library_command` and `library_stick` yet.
- Only rows that show have controls: a journey scrolls or searches before
  it presses a row that does not show.
- Large shows as many tags in its filter pane as fit, and TAGS ▾ holds the
  rest; the filter pane does not scroll.
- The scroll bars show where the panes are scrolled but take no press: the
  wheel, the keys, the right stick and a finger scroll them.
