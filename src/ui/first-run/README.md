# First-run setup

The welcome a player sees at the first main menu: a language, what online features do, and where the Library and Settings buttons are.

## Entry points

`decide` says whether the setup shows. `lay_out`, `key`, `pointer` and `draw` are one step. `place_coaches` keeps the two coach marks off the OA buttons. The preference key is `open-annihilation.first-run`, and `done` means the player finished it or skipped it.

## State

A `Session` holds the `Model` (the step, the language cards, the online-features choice and the button rectangles), the carousel's place and the focus. The host fills the model. Nothing here reads a preference, a clock or the network.

## Invariants

- `--first-run` shows the setup even when the preference is `done` and even in an automated run. Every other automated run leaves it hidden.
- Next is disabled while a language pack is downloading or being checked.
- Escape does nothing on the language step. On the other steps it is Back.
- The coach marks never cover either OA button. A short side flips the card.
- The picture well draws the Library's Active tab from a fixed Ridge sample. No picture file is shipped.
- Every word the player reads is in `interface_words.hpp`.

## Tests

`ui-first-run` checks the gate and the coach marks at Compact, Regular and Large. The native check `--check-first-run` walks the steps in the running game. `oa-ui-first-run-pictures` writes the manual's pictures; it is not a test.

## Limitations

The spotlight is drawn here, because the kit's spotlight has one cut-out and the main menu has two OA buttons. Show again lives on Settings › System, which this module does not draw.
