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

The words it shows are in `oa/ui/library/text.hpp`, English with `{place}`
holes, each a `constexpr std::string_view` there and nowhere else, so a
translator finds them together.

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

## State

None beyond the `Library` value the caller holds: its inputs, entries,
browsing state and selection. The model keeps nothing else between calls.

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

## Limitations

- Folding is ASCII only: letters outside A to Z match only themselves.
- A tag is matched from its start, never inside it.
- An update that cannot be installed is described with the detail its
  update note carries, as the app wrote it; the model does not reread the
  catalogue for it.
