# Packages

This module installs packages into a folder of the player's own folder. One
table of kinds says what differs; today it holds the mod kind, `oamod`. A
mod package is a zip archive of a mod's folder, the one that holds its
`oamod.yaml`; it installs into `Mods/<id>`, by the id its profile gives,
whatever the file is called. The module has no SDL: the game's runtime
shows what it asks and answers (`src/app/runtime_mod_install.cpp`, and
"Mod packages" in [src/app](../README.md#mod-packages-oamod)).

## Entry points

`oa/app/package_install.hpp`, namespace `oa::app::package_install`:

- `open_package` reads a package and checks it before anything is written:
  its directory through the streamed zip reader
  (`oa/formats/zip/stream.hpp`), where its `oamod.yaml` lies, every name it
  unpacks, its sizes, and its profile, resolved twice as a load of the mod
  resolves it (the second time with its packaged INI file, or the game
  folder's, and the registry values).
- `plan_install`, the oamod kind's plan, decides, from what each folder of
  Mods holds (`FolderHooks`), where a package goes and what it asks.
- `Unpacking` unpacks a package's files into a staging folder inside Mods,
  a budget of bytes a step: its folders, then its files, then a sync of
  each folder and, on macOS, one flush of the drive's cache to storage.
  Each folder or file made and each folder synced costs
  `unpack_entry_cost` (16 KiB) of the budget, and a step ends after about
  4 ms, so that a package of many small files never holds a frame long.
- `commit_change` puts a change in place with renames on one volume.
- `recover_changes` settles what a stop left, and `Discarder` deletes the
  folders a change drops a little at a time.
- `read_installed_mod` and `read_backup`, the oamod kind's, read what a
  folder and its `.backup` hold, for the plan and the Mods page's ROLL BACK.

`oa/app/package_install/inbox.hpp` keeps what outlives each run of the game in
one process: the packages waiting for the main menu (`post_package_file`,
`take_package_file`), the change that waits for the run playing its target to
end (`set_pending_change`, `finish_pending_change`) and its outcome, and the
discard folders the start's recovery found.

`oa/app/package_install/prompts.hpp` builds every prompt the install shows
(`oa::ui::engine_settings::Prompt`): its text through the interface
catalogue, its buttons and what each answers.

`oa/app/package_install/handoff.hpp` has the instance lock and the hand-off
folder a second start writes its packages into for the copy already
running.

## Kinds

`kKinds` in `src/kinds.cpp` is the table. Today it holds one entry, `oamod`:
the extension `.oamod`, the manifest `oamod.yaml` (at most 256 KiB), the
root folder `Mods` and the prefix `.oamod-` on the installer's own folders
and on the lock. `package_kinds`, `kind_for_file` and `find_kind` read the
table; `mod_kind` is that entry. A file whose extension is none of them is
refused as `unknown_kind`, and nothing is read.

Reading the container, unpacking, the renames, recovery, the lock,
discarding, the inbox and the hand-off are the same for every kind. They
take the kind and build the seven folder names from its prefix
(`folder_names`). A kind supplies those names and the hooks: `read_manifest`
fills what the package installs and the kind's own fields, `read_installed`
and `read_backup` say what a folder holds, `plan` decides where it goes,
`check_staged` checks the unpacked files before they are put in place (null
checks nothing) and `prompts` words every prompt. The oamod hooks are
`src/plan_oamod.cpp` and `src/prompts_oamod.cpp`
(`oa/app/package_install/oamod.hpp`): the profile resolved twice, where a
mod goes, and the words a mod install shows. `Package::profile` is that
resolved profile, and null for every other kind.

### Adding a kind

A new kind is wired in five places, and nowhere in the generic files:

- `src/plan_<kind>.cpp`, implementing the hooks (`read_manifest`,
  `read_installed`, `read_backup`, `plan`, and `check_staged` when the
  staged files need a check of their own).
- Its line in `add_library` in this module's `CMakeLists.txt`.
- Its entry in `kKinds` in `src/kinds.cpp`.
- Its branch in each function of `src/app/runtime_package_kinds.cpp`.
  `package_options` is where the runtime hands the hooks their `context`,
  and `package_changed` is where the app takes in what a change put in place.
- Its test `app-package-install-<kind>`.

A kind's own refusals go at the end of `Refusal`, worded by its
`KindPrompts::refusal_text`.

## What a package holds

A zip archive (`oa/formats/zip/stream.hpp` says which) whose top holds
`oamod.yaml`, matched without case, or which holds one folder at its top
that holds it, as the Finder's Compress makes: that folder is stripped.
`__MACOSX` folders, `.DS_Store` files and AppleDouble files (a last part
starting `._`) are ignored, and so is a `.backup` folder of its own, which
is ours. It is refused, before anything is written, for:

- a file that cannot be read, or is not a zip archive; a damaged directory
  or entry;
- no `oamod.yaml` where it should be, one larger than 256 KiB, or one whose
  profile does not resolve (its diagnostics are kept);
- a name that cannot be unpacked on every system (`portable_name_problem`:
  each part 1 to 255 bytes of UTF-8 without control characters or `< > : "
  | ? * \`, not ending in a dot or a space, and not a name Windows keeps for
  a device), two names that differ only in the case of their ASCII
  letters, as the game matches names, or a file and a folder of one name;
- more than 16,384 folders (`max_package_folders`), those its paths pass
  through included;
- a link or a special file, an encrypted entry, or one packed otherwise
  than stored or deflated;
- more than 4 GiB unpacked, or more than 64 MiB unpacked and more than 200
  times its own size;
- an id that names a device on Windows.

The names are kept as a tree of their parts, each part once under its
folder, so reading a package takes memory that grows with the parts it
names, never with the length of its paths.

Unpacking also refuses too little room on the disk (the files and 64 MiB
more), an unpacked path longer than the system opens less 16 characters,
and a file or folder that exists already when it is made, as a name the
file system folds to another's does: every file and folder is made anew,
never over another.

## Where a package goes

The plan looks at `Mods/<id>` (A), then at the folders Install alongside
makes, `Mods/<id>-<version>` and `-2` to `-99` (B), the version made
folder-safe (`version_folder_part`). The first row that applies wins:

| A | B | Plan |
|---|---|---|
| this mod, the same version, another revision | any | UPDATE MOD on A |
| this mod, the same version and revision | any | ALREADY INSTALLED on A |
| not as above | one holds this mod at the same version, another revision | UPDATE MOD on it |
| not as above | one holds this mod at the same version and revision | ALREADY INSTALLED on it |
| missing | | installed into A with no question |
| this mod, another version | | ANOTHER VERSION: replace A, or install alongside in the first free B |
| anything else | | FOLDER IN USE: install alongside in the first free B; A is never replaced |
| either of the last two with no free B | | refused |

A revision that cannot be read counts as another.

## Changes

Every change is a sequence of renames on the volume Mods is on, each
refusing a name that exists (`rename_exclusively`) and tried again for up to
ten seconds while another program, such as a file sync or a virus scanner,
holds the files. The folders it uses lie in Mods, named after the target
`T`: `.oamod-staging-T` (S), `.oamod-old-T` (O), `.oamod-replaced-T` (X),
`.oamod-restore-T` (R) and `.oamod-discard-T-n` (D).

| Change | Renames |
|---|---|
| install | S to T |
| replace | T to O; S to T; O's own `.backup` to D; O to `T/.backup` |
| reinstall | T to X; S to T; X's `.backup` to `T/.backup`; X to D |
| roll back | `T/.backup` to R; T to O; R to T; O to `T/.backup` |

So three installs keep one version back: after revisions 1, 2 and 3 the
folder holds 3, its `.backup` 2, and 1 is deleted. A roll back swaps the
two, so a second undoes the first. A failure before the new files are in
place undoes what was done; after it, the rest is tried once more, and what
it could not settle is renamed to a visible `<T>-left-over` folder.

Before a change, the target must still hold what the plan found, or it is
refused as changed; a roll back is refused as changed when `T/.backup` is
a link or junction. Once the files are in place, T and Mods are synced, and
on macOS the drive's cache is flushed to storage, before the player is
told. A copy of the game holds the lock on `Mods/.oamod-lock`
while it unpacks or changes Mods, so that another copy's recovery never
touches its folders; that copy's installs are refused as busy meanwhile.

Recovery runs once at every start before the mod folder is resolved, and
again after a change that waited for its run. For each target it finds
folders of: X with T missing, X goes back; X with T, X's `.backup` moves
over and X goes. O with T missing, O goes back; O with T, O's own `.backup`
goes and O becomes `T/.backup`. R with T and no `T/.backup`, R becomes it;
R with T missing, R becomes T. S goes. Every crash point of every change
settles so to the state before it or after it. What it cannot settle
becomes `<T>-left-over`. Every D it finds is deleted, whatever it is.

The `Discarder` removes at most 64 entries, or works 4 ms, a step. Links and
junctions, a D that is one included, are removed, never entered, so that a
`.backup` or staging folder someone made a link to a folder outside Mods
drops only the link; read-only files are made writable first.

A link (`is_link_or_junction`) is a symbolic link, or on Windows a reparse
point whose tag is a name surrogate (`reparse_tag_is_link`): a junction,
mount point or symbolic link, which names another place. A folder or file
with any other tag is itself, such as one a file sync keeps in the cloud,
as the sync of Documents does on many Windows machines: the plan reads it
as a mod, recovery settles it, the `Discarder` enters it and ROLL BACK
takes it. A reparse point whose tag cannot be read counts as a link.

## Tests

- `app-mod-install-package`: a Finder-made package, a package with its
  profile at its top and a `.backup` of its own, a packaged INI file the
  second resolution reads, the folders a package makes, each refusal of a
  package once (more folders than a mod may take as paths of 254 folders
  each), the name rule and the folder-safe version.
- `app-mod-install-plan`: each row of the table over a Mods folder kept in a
  map, and each prompt's title, buttons, first mark and sentences, each
  fitting its height with a long path.
- `app-mod-install-change`: an install, three replaces, a reinstall, two roll
  backs and an install alongside in a scratch Mods folder; a rename refused
  at each step of each change; every crash point settled; the folders
  recovery cannot settle; the retries and waits; a rename never replacing;
  links in a discard folder; a `.backup` linked to a folder outside Mods,
  dropped by a replace and refused by a roll back, and a discard folder
  that is such a link, each leaving that folder whole; the Windows reparse
  tags taken as links, as values, so that it runs everywhere; files made
  anew only; a target that changed; the lock; the room and path checks;
  the files a step makes held to its budget.
- `app-mod-install-handoff`: the instance lock and the hand-off folder.
- `app-package-install-kinds`: the kind table holds exactly `oamod`, a file
  is a package only by that extension, the seven `.oamod-` names, and a
  made-up kind built in the test installs, replaces, rolls back, recovers
  and discards through the generic code, including a staged check that
  refuses.

The native check `native-mod-install` (`docs/development/testing.md`)
installs packages in the game itself.

## Limitations

A package's names are checked by their ASCII letters for case, as the game's
own lookups match names (`AssetStore` in `oa/formats/hpi.hpp`); two names
that only a file system takes for one, by the case of other letters or by
another Unicode form, are refused where it does, when the second cannot be
made anew, and both unpack where it does not. Archives that span several
disks or carry data before their first entry are not read.
