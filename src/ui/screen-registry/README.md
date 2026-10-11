# Screen registry

The screen registry (`oa/ui/screen_registry`, `oa::app`) holds the tables the
application fills for frontend screens, overlays, dispatcher steps and
dispatcher queries. It also holds the descriptors of settings pages, kit
screens, notices and prompts declared by code outside the engine's own
screens. The engine hosts those pages, screens, notices and prompts. The
tables this module already kept are unchanged.

## Entry points

- `oa/ui/screen_registry.hpp`: the screen, overlay, step and query tables.
  `screen_register` takes a screen, and refuses `kScreenAny`, a duplicate id
  or a full table. `overlay_register` keeps overlays sorted by z, a later
  overlay of equal z after an earlier one. `step_register` and
  `query_register` bind one handler to a dispatcher step or query, and refuse
  a null handler, a step or query already bound, or a full table. `screen_find`,
  `step_find` and `query_find` look a binding up. A query no handler takes is
  answered with 0. Built-in screens use the values of `app::Screen`; other
  screens take ids from `kFirstPackageScreen`.
- `oa/ui/screen_registry/kit.hpp`: the descriptors the engine hosts.
  `settings_page_register` takes a `SettingsPageDesc` (an id, a title, an
  owner, preference rows, and `applied`). `kit_screen_register` takes a
  `KitScreenDesc` (a name, `lay_out`, `act` and `closed`). `NoticeRequest` and
  `PromptRequest` describe a notice and a prompt. The OA layer reads the two
  registries and shows the requests (`src/app/README.md`).

## State

A `ScreenRegistry` holds up to `kMaxScreens` screens, `kMaxOverlays`
overlays, `kMaxSteps` steps and `kMaxQueries` queries, and the name of the
last registration it refused. A `SettingsRegistry` holds up to
`kMaxSettingsPages` pages, in registration order, with copies of each page's
id, title and owner. A `KitScreenRegistry` holds up to `kMaxKitScreens`
screens, in registration order, with a copy of each screen's name. The
caller keeps a page's rows, and a screen's model, alive for the runtime's
life. The registries record nothing of their own between runs: the
application owns them.

## Invariants

- A refused registration leaves the tables as they were. The first one a
  registry refuses sets its `rejected`, and a later registration, taken or
  refused, leaves it as it is, so that the application can stop the start
  on any refusal once the registrations are in.
- A page id and a screen name are dotted words of a to z, 0 to 9 and
  hyphens, at most 48 bytes. A page id is none of the engine's section
  words, including `registered`.
- Every row of a taken page is bound to a preference key. A key is dotted
  words, at most 64 bytes, and is not in the engine's `open-annihilation.`
  space and not in 3.1c's `Section|Key` form. A choice's ids are words, one
  for each caption. A slider's step is positive and its most is at least its
  least.
- A taken screen has `lay_out` and `act`. Its automation dialog is
  `oa.<name>`, and its controls' kit names start with that name.
- A prompt has from 1 to `most_prompt_buttons` buttons. Its answer is
  delivered once.

## Tests

`screen-registry` checks duplicate screen ids, overlay z order, step and
query binding, and the null, duplicate and full-table refusals. It also
checks that a settings page and a kit screen are taken, that a duplicate id,
an engine section word, an engine key, a `Section|Key` key, a row with no
binding and a full table are refused, and that `rejected` names the reason.

## Limitations

The module stores the descriptors and refuses the ones that do not fit. The
settings dialog lays a registered page out, and the OA layer opens a kit
screen and queues a notice or a prompt. Those hosts are documented with the
dialog and the OA layer. The screen, overlay, step and query tables are the
ones the application already fills; this module does not decide which
screens the game shows.
