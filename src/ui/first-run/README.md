# First-run setup

The welcome a player sees at the first main menu: a language, what online features do, and where the Library and Settings buttons are.

## Entry points

`decide` says whether the setup shows. `lay_out`, `key`, `pointer` and `draw` are one step. `place_coaches` keeps the two coach marks off the OA buttons. `setup_scale_percent` and `setup_frame` give the scale and the room the hosts lay a window out in: the room holds at most `setup_room`, Regular's 960 by 540, and a larger window draws it larger. The preference key is `open-annihilation.first-run`, and `done` means the player finished it or pressed Skip.

## State

A `Session` holds the `Model` (the step, the language cards, the online-features choice, the button rectangles and whether the game runs in a window), the carousel's place and the focus. The host fills the model. Nothing here reads a preference, a clock or the network.

## Invariants

- `--first-run` shows the setup even when the preference is `done` and even in an automated run. Every other automated run leaves it hidden.
- Next is disabled while a language pack is downloading or being checked.
- Escape does nothing on the language step. On the other steps it is Back.
- The coach marks never cover either OA button. A short side flips the card.
- The picture well draws the Library's Active tab from a fixed Ridge sample. No picture file is shipped.
- Every word the player reads is in `interface_words.hpp`.
- Only the chosen language card is marked: the focus follows it as the carousel turns, the carousel rings it, and it alone is drawn Active while the step speaks its language. The language in use is not marked while another card is chosen.
- In a window the header leaves out the mark and "Open Annihilation". Full screen keeps them.
- The setup's words, buttons and cards grow together with a window larger than 960 by 540 points; below that the room is the window's own points.
- The left footer button says **Quit** or **Skip** for the whole walk, whichever the host set on the model. It does not change from step to step. Copying does not relabel it Cancel. Escape on that step still returns to the game step.
- On the language step, a built-in or installed language in the middle shows that step's words in itself. A pack that is not installed yet leaves them in the language in use, and nothing is downloaded to preview it. The other steps stay in the language in use until the player chooses.

## Tests

`ui-first-run` checks the gate and the coach marks at Compact, Regular and Large, the chosen card's mark as the carousel turns, the header in a window, the Locate step and the setup's scale. `ui-first-run-sizes` lays every step with a header out on 640x480, 1280x720, 1920x1080 and 1920x1117 at 2x windows, draws it in the bundled fonts, and checks the footer's buttons and the body text against their stated sizes, that nothing overlaps in English, German and Chinese, the header's mark and name, and that Locate another… holds its words in every language. `zh-Hans-setup.tdf` is the Chinese pack's words for it. The native check `--check-first-run` walks the steps in the running game. `oa-ui-first-run-pictures` writes the manual's pictures; it is not a test.

## Limitations

The spotlight is drawn here, because the kit's spotlight has one cut-out and the main menu has two OA buttons. Show again lives on Settings › System, which this module does not draw.
