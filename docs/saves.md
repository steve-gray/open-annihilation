# Saves, recordings and films

A saved game is still the same file an older build reads. Beside it, and
beside a recording, Open Annihilation writes a short note named with
`.oa-content` on the end of the file's name. A film's note is `oa-content.txt`
in the film's folder. The note names the base game, the expansions that
change the rules, and the mod's name. Music is not in the note.

A file from before this note has no note. It loads as it does today.

Loading a save whose note names a base game, or a rule-changing expansion,
that is off shows **This save needs other content**. The question compares
the save with the game as it is now. **Turn on and load** turns on what the
save needs, leaves every other expansion as it is, restarts the game's files
and then loads the save. Choosing another base game clears the mod, as
choosing a base game does. **Cancel** leaves the save unopened.

A recording opened with `--play-demo` asks the same question and, on **Turn
on and load**, plays that recording after the restart. A recording whose
note does not match, opened where the question is not shown, is refused with
the same sentence. A film keeps the note and does not ask on its own: nothing
loads a film as a game.

Saved games stay in `Saves/<mod>/`, and recordings and films stay in their
own folders for that mod.
