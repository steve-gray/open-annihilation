# The Library

The Library is where Open Annihilation finds, installs, updates, rolls
back and uninstalls mods, map packs and language packs without leaving the
game, and where you choose the mod you play. It lists
every package that Core Prime's registry, built into the game, and any
registry you added offer, together with what you have installed, and it
says for each whether you have it, whether an update waits, and what it
would change.

[Downloads and privacy](downloads.md) explains registries, the Downloads
settings and exactly what a download sends and records.
[Mod support](mods/README.md) covers mods themselves, and
[map packs](maps/README.md) and [languages](languages.md) theirs.

## Contents

- [Opening the Library](#opening-the-library)
- [The Active tab](#the-active-tab)
- [Tabs and states](#tabs-and-states)
- [Search and filters](#search-and-filters)
- [The details](#the-details)
- [Choosing a mod](#choosing-a-mod)
- [Keys and controllers](#keys-and-controllers)
- [Installing](#installing)
- [The download queue](#the-download-queue)
- [Updates](#updates)
- [The Registries tab](#the-registries-tab)
- [Which downloads ask first](#which-downloads-ask-first)
- [The update question](#the-update-question)
- [Rolling back](#rolling-back)
- [Uninstalling](#uninstalling)
- [Finishes when you leave …](#finishes-when-you-leave-)
- [Offline](#offline)
- [Check that you're a person](#check-that-youre-a-person)
- [Where packages go](#where-packages-go)

## Opening the Library

The Library opens on the main menu, from:

- **the OA button**, the plates over drive bays at the bottom right of
  the main menu, a 48-pixel square left of the **Settings** button and its
  gears (the same size, a quarter of a side apart, 12 pixels in from the
  frame). It opens on the [Active](#the-active-tab) tab. Its bottom-right
  corner shows how many mods the Mods tab lists under All (every mod the
  registries list, plus your own, without the Base Game entry): `7`, `123`,
  or `999+` above 999. While the catalogues are still being read and that
  number is not final, a small spinner turns there instead, and stops once
  the list is in. While updates you can install wait, the button also wears
  an amber dot in its top right corner, from the moment OA finds them: you
  need not open the Library to learn of them, and its **Updates** tab lists
  them;
- **MORE IN THE LIBRARY…** in Settings › Language, which opens it on
  Languages, over the settings, which stay open under it.

The
skirmish and battle room map lists have **MORE MAPS…** instead, which opens
the map browser, and the battle room has **GET MAP**, which downloads the
host's map without a detour ([map packs](maps/README.md)).

The first time the main menu is reached, a welcome points out these two
buttons, after a language and a choice about online features
([the welcome setup](first-run.md)). Settings › System can show that
welcome again.

The settings have a button of their own on the main menu, the gears beside
the OA button, which opens them with no Library under them
([settings](settings.md#opening-the-settings)). From the Library, **Cmd+,**
on macOS and **Ctrl+,** elsewhere opens them over the Library. The header's
top right holds a refresh icon. Pressing it, or F5, asks the registries
for their lists at once ([updates](#updates)). While they are being asked
the icon turns, one full turn every 1.6 seconds, and it cannot be pressed
again until they answer. When the refresh has read a list, the icon shows
a check for 0.8 seconds and then the arrow again. A refresh that fails goes
straight back to the arrow; the offline line says why. Pressing it during
the check asks again at once. **CLOSE**,
or Escape, goes back to the main menu.
The Library shows only on the main menu: it closes when the menu gives way
to another screen, and in a game the in-game menu's button, which shows the
gears too, opens the settings, not the Library.

The Library lays itself out for the room the window gives it, at the
Interface size in Settings › Graphics ([settings](settings.md#graphics)):
in the Compact layout, as on a 640x480 window, the list fills the window
and an entry's details are a page of their own; in the Regular and Large
layouts the details sit beside the list, and Large adds a column of
filters.

## The Active tab

**Active**, the first tab, shows what you are using now, as tiles:

- **MOD**: a tile for the mod you are playing. Its badge is large, and the
  tile names the mod, its version and revision ("4.8 (rev 3)") and who made
  it ("by the Ridge team"). **Playing** is a rounded pill at the top right,
  on the title's line. **UNLOAD MOD** sits at the bottom right. It asks the
  same **SWITCH MOD** question as **SWITCH OFF** and, on **SWITCH**, goes
  back to the game as 3.1c plays it
  ([choosing a mod](#choosing-a-mod)). It is greyed during a game and while
  the command line chose the run's mod; resting on it then shows "The
  command line chose this run's mod." When you are playing no mod, the
  tile is **Base Game**. Its badge is Total Annihilation's own icon, taken
  from the program in the game folder that describes itself as Total
  Annihilation, and drawn centred in the square. When that program is
  missing or its icon cannot be read, the letters "3.1c" stay. The tile reads "Total
  Annihilation 3.1c" and "No mod: the game as it was made", and it has no
  pill and no button.
- **LANGUAGE**: a tile for the language the game is showing. It carries the
  pack's badge, or a language mark, the language's name in itself and in
  English, the pack's version and revision, and how much of the game the
  pack covers ("Coverage 92%"). When the game is showing a language it
  holds itself, **Built in** is a rounded pill at the top right, on the
  title's line, in a muted ground so it does not look like an installed
  chip.
- **MAPS**: each map you have installed. On a small window a map is one
  line: its name, how many players it is for and its size ("Twin Rivers ·
  2 players · 12x12"). On a larger window it is a tile with that map's
  minimap, its title, its players and size ("4 players · 16x16"), and its
  description cut to one line. When the pack holds several maps, **From**
  and the pack's name is a link: at the right of the line on a small
  window, and under the lines on a tile. When the pack holds just that
  map, there is no **From** line: the title is the link, in the same accent,
  lighter while the pointer rests on it. Choosing the link, with the
  pointer, Enter or a controller's A, opens the Maps tab with that pack
  selected and its row in view. When you have no map pack, the pane says
  "No map packs are installed."

  The **MAPS** heading stays put, with a search field at its right,
  **Search maps**. Typing there leaves the maps whose name or pack name
  contains what you typed, ignoring capitals. While the field holds text
  the heading counts them, "2 of 5". When none match, the pane says "No
  installed map matches." Clearing the field shows every map again. The
  words stay while the Library is open, including on the other tabs. The
  search does not hide **MOD** or **LANGUAGE**.

The badge, the title and the lines start at the top of each tile, with the
same small padding above them and below the last line or, on the mod tile,
below **UNLOAD MOD**. Side by side, the mod and the language share a title
line and the same height, and so do maps in one row. Extra room stays
under the shorter tile's lines. A minimap keeps its shape: it is centred
in its square, and the rest of the square is the list's well colour.

On a small window the mod and the language stand in one column, and the
maps are one line each, several of them in the pane. On a larger window
the mod and the language stand side by side, and on the largest the maps
stand in two columns when you have more than one. The tab has no catalogue
search and no filters. The mod and language labels and each map's title line take
no press, except the title of a map whose pack holds only that map: that
title is the link to the pack. **UNLOAD MOD** does, and so does each map's
link to its pack while that link shows, and so does the maps search. Tab
and the arrows reach them. **MOD** and **LANGUAGE** stay where they are.
The maps pane runs from under the **MAPS** heading to the bottom of the
window, and when the maps are taller than that pane they scroll inside it
with the mouse wheel, a finger, the scroll bar, Page Up and Page Down,
Home and End, and a controller's right stick. The other tabs are where
packages are got, updated and played.

![The Active tab at Compact: one column, Highland's tile with a Playing pill and UNLOAD MOD, the language under it, and several maps on one line each. Atoll's title is the link and has no From line; the others keep From at the right of the line.](images/library/library-active-compact.png)

![The Active tab at Regular: the mod and the language side by side, and under them each installed map with its minimap and a cut description. Atoll, alone in its pack, has no From line; the others keep one.](images/library/library-active-regular.png)

![The Active tab at Large: the same tiles, with the maps in two columns of equal height.](images/library/library-active-large.png)

## Tabs and states

The tabs are **Active**, **Mods**, **Maps**, **Languages**, **Updates**
and **Registries** ("Lang", "Upd" and "Reg" in the Compact layout). Each
tab after Active shows how many it lists under All, before a search or a
tag, in brackets after its title and in that title's own font and colour:
"Mods (11)", "Maps (2)", "Languages (2)", "Updates (2)", "Registries (2)".
The short names keep the brackets ("Upd (2)"). Active has no count.
Updates counts the updates you can install, as the OA button's dot does.
Registries counts the registries listed. Registries is last, and it is
where registries are added, checked and turned off
([the Registries tab](#the-registries-tab)).

Each row shows the package's badge (its own picture, or up to three
letters), its name, its version ("4.8 · rev 4" from its second packaging
revision on, and a map pack's number of maps), its size and its state. A
map pack of one map shows that map's minimap in place of the badge, on
Maps and on Updates and in the details. A pack of several keeps its own
badge or its letters, except on the Maps tab, where a pack with no badge
shows the first map's minimap. The minimap keeps its shape in the square:
it is centred, and the rest of the square is the list's well colour. While the
picture has not arrived the square is an empty dashed outline. The
Regular and Large rows add who made it and its summary
("by the Ridge team"), and Large adds its tags. A Compact row names the registry
after the size when the package comes from one you added. The version
always shows whole, revision and all, at every layout.

The state is an icon at the row's right, and again beside the package's
name in the details:

| Icon | Means |
|---|---|
| a circled check, green | installed, and no newer release is offered; the mod being played shows it too |
| a circled up-arrow, amber | installed, and the registry it came from offers a newer release |
| a download arrow, grey | not installed |

The icons are drawn as shapes at the screen's own pixels, so they stay
sharp at every Interface size. The download arrow is the row's **GET** and
the circled up-arrow its **UPDATE**: pressing either does exactly what the
details' GET or UPDATE does, with the same questions, without opening the
details. Anywhere else on the row, a press only chooses it. To automation
they are `oa.library.row.<registry>.<key>.get` and `.update`
([automation](automation.md#the-library)).

Under that icon, the same size and centred on it, a package from a
built-in registry may show who made it. Open Annihilation's own packages
show a small copy of the app icon, the plates alone, filtered down from
a copy kept for that size. Cavedog's show a bone, off-white with a dark
outline, drawn as a shape. The name is the catalogue's `author`, or, when that listing
names none, the installed manifest's `author.name`: "OpenAnnihilation",
"Open Annihilation" or "OA team" for the badge, and "Cavedog
Entertainment" for the bone, ignoring case and space at either end.
The publisher is not read. A package that names no author shows no
mark, and another maker's package listed on Core Prime shows no mark.
A package you added yourself, or one from a registry you added, shows
no mark whatever its author says: an author name is free text. A row
with no state icon still puts the mark in that column. The rows do not
grow to make room; at every size class the mark fits under the icon.
When a mark shows, the row's read text gains " · Open Annihilation" or
" · Cavedog Entertainment". Its automation name does not change.

![Languages: the fixture's Portugues (Brasil), author OA team, installed from the built-in registry, with the OA badge under its check.](images/library/library-languages-compact.png)

A GET or UPDATE that cannot act has its icon dimmed and takes no press,
and the details say why:

- "Downloads from Core Prime are off (Settings › Downloads)": the
  registry's install ID is off ([the install ID](downloads.md#the-install-id));
- "Needs Open Annihilation 0.9.0 or later": the package needs a later
  Open Annihilation;
- "Needs the base game *base*": it is made for a base other than
  Total Annihilation 3.1c;
- "Needs hacks this OA lacks: *hacks*": a mod that uses standard hacks
  this build does not have, up to three of them named, then how many more.

A package you cannot get stays listed, after the others.

## Search and filters

The search field ("Search mods, authors, tags", and the like on each tab)
looks for every word you type in a package's name, the start of its tags,
its author or publisher, and its summary. A word in the name ranks first,
then a tag, then the author, then the summary. Without a search the list
shows updates first, then what is installed, then what you can get, then
what you cannot, each by name, Core Prime's first.

The filters are **All**, **New**, **Installed** (which keeps updates and
the mod being played) and **Updates**, each with how many packages it holds
on that tab in brackets, "Installed (4)", and each tag the packages carry,
with how many have it. A filter's count is of the whole tab: the tag and
the search you choose do not change it. **New** holds the ten packages the
registries published last, newest first, by the day each release came out;
a package whose registry gives no day is never among them. The filters are
chips over the list in the Regular layout, with a TAGS menu for the tags
that do not fit, a column of their own in Large, and one drop-down in
Compact. Choosing the tag already chosen clears it.

Each tab keeps its own filter: choosing Installed on Mods leaves Maps and
Updates as they were. The **Updates** tab holds every kind of package and
has the same filters, and it opens on Updates, every update together. Over
its list a line says how many updates it shows ("3 updates"), with **UPDATE
ALL**.

An empty list says "Nothing matches "*search*".", "No updates.", "Nothing
to show.", or, with no registry at all, "Nothing here yet. Turn a registry
on in Settings › Downloads."

## The details

Choosing a row shows its details: beside the list in the Regular and Large
layouts, or as a page of its own in Compact. On that page the footer's
right button is **BACK**. It returns to the same tab, filter, search,
scroll and row. The list's button stays **CLOSE**. Escape and the pad's
back button follow whichever of those is showing.

- **A status line**: "Not installed · 41.0 MB", "Installed", "Playing",
  "Revision 4 available · 3.2 MB to download" or "Version 4.9 available ·
  …", or "Update available · 3.4 MB to download" when the registry names
  no revision for an update of the same version, "No oamod.yaml present"
  for a mod folder without one, where the download stands ("Waiting to download", "Downloading ·
  18.2 of 64.0 MB", "Verifying", "Installing"), or why it cannot be got
  ([above](#tabs-and-states)).
- **The summary**: the registry's, or for a mod of your own its
  `oamod.yaml`'s description.
- **The map previews**, for a map pack: one large picture when the pack
  holds one map, and a grid of pictures when it holds several. Each
  picture is that map's minimap, fitted the same way as the row, and under
  it the map's name, how many players it is for, its size and its
  description. The picture is there before you download the pack and after.
  ![Maps, one map: the row's minimap, and a larger one in the details with the map's name, players, size and description.](images/library/library-maps-one-large.png)
  ![Maps, several maps and no pack badge: the first map's minimap on the row, and a grid of each map in the details.](images/library/library-maps-several-large.png)
- **The facts**:
  - **Installed**, the version you have and its packaging revision, "4.8
    (rev 3)", for a package you have installed;
  - **Latest**, the same for the release the registry lists, "4.8 (rev
    4)". A revision the registry or the package does not name is left
    out ("1.0"), and a package with no version shows its revision alone
    ("rev 3");
  - **Author**: the registry's author for it, else its publisher, else the
    author its own manifest names;
  - **Size**, and with an update how much there is to download;
  - **Base**: "Total Annihilation 3.1c";
  - **Needs**: the Open Annihilation it needs, "Open Annihilation 0.8.0 or
    later ✓", in amber when this one does not meet it;
  - **Hacks**, for a mod: "8 change the game · 4 view only", and any it
    needs that this build lacks;
  - **Rules**, for a mod: its rules hash, "9c1f 4e0b"; with an update,
    "9c1f 4e0b → 9c1f 4e0b same", or "9c1f 4e0b → 51d2 07aa · changes the
    game's rules; saved games from revision 3 won't load under revision 4",
    or "unknown · saved games from revision 3 may not load under revision
    4". Saved games load only under the rules they were saved with;
  - **Maps**, for a map pack: how many it holds;
  - **Coverage**, for a language pack: how much of the game it translates;
  - **From**: the registry, "Your own copy" for a package you installed from
    a file that no registry lists, or, in amber, "*registry* · Not reviewed
    by the OA team" for one from a registry you added
    ([not reviewed](downloads.md#not-reviewed-by-the-oa-team)).

  A fact with nothing to say is left out. These hold alike for mods, map
  packs and language packs.
- **The buttons**: **GET** or **UPDATE**, **CANCEL** while it is in the
  download queue, **RETRY** after a download that can be tried again;
  **PLAY NOW** for an installed mod you are not playing, which switches to
  it, or **SWITCH OFF** in its place for the mod you are playing, which
  goes back to the game as 3.1c plays it ([choosing a mod](#choosing-a-mod));
  **ROLL BACK** while an earlier copy is kept
  ([rolling back](#rolling-back)); **UNINSTALL** and **OPEN FOLDER** for an
  installed package ([uninstalling](#uninstalling)); and **HOMEPAGE**, which
  opens the package's web page in your browser.
- **A note** for mods while you play one: "Finishes when you leave Ridge."
  on the mod being played, or "You are playing Ridge. Updating Cinder
  doesn't restart the game." on another
  ([finishes when you leave …](#finishes-when-you-leave-)); on every
  installed mod, first, "The command line chose this run's mod." while
  `--mod-dir` or `--base-game` chose it; and, while UNINSTALL is off, why:
  "Map packs can't be uninstalled during a game." or "It is in the download
  queue: CANCEL it before you uninstall it."

The Regular and Large layouts show who made a package, and its download
count, on its row; Compact shows them under the status line.

## Choosing a mod

The Mods list starts with **Base Game (Disable Mods)**, above every mod,
under All and Installed, and whenever a search matches its name. Its
badge is the same icon as the Active tab's base-game tile, or the letters
"3.1c" when that icon cannot be read. Its details say "Total Annihilation
3.1c as it was made, with no mod". It is
not a mod: All, New, Installed and Updates leave it out of their counts,
New never lists it, and Active, Updates and Registries do not list it. It
has no update, and its details never offer UPDATE, ROLL BACK, UNINSTALL or
OPEN FOLDER.

While no mod is played it is marked **PLAYING** and its details offer no
action. While a mod is played its details offer **PLAY NOW**. That asks
the same **SWITCH MOD** question as **SWITCH OFF** on the mod you play
and, on **SWITCH**, reloads the game with no mod. It is greyed during a
game and while the command line chose the run's mod, and the details then
say "The command line chose this run's mod."

The Mods tab then lists every mod you can play: the ones you installed, from a
registry or a file, and every other folder the game can play as a mod,
each marked "Your own copy": the folders in the game folder's `mods`
folder and in your own Mods folder, `Documents/Open Annihilation/Mods`,
a folder without an `oamod.yaml`, which shows "N/A" for its version and
"No oamod.yaml present" in its details and plays with the game's own
rules, and a folder an earlier version's Pick Folder… chose, while it is
still the mod you play. A mod of your own shows its `oamod.yaml`'s
description in its details. The one you play is marked **PLAYING**. With
no mod, **Base Game (Disable Mods)** is the one marked **PLAYING**.

- **PLAY NOW**, on any other mod, plays it: the game reloads its data for
  that mod and returns to the main menu, without closing the window, and
  plays it at every later start too. A folder the game cannot play is
  refused, and a note over the Library says why under **SWITCH MOD**:
  "That folder cannot be played; the log says why.", with **OK**.
- **SWITCH OFF**, on the mod you play, goes back to the game as 3.1c plays
  it. It asks first, **SWITCH MOD**: "Switch to No Mod now? The game
  reloads its data for the new mod and returns to the main menu. Your
  other settings are kept.", with **CANCEL**, marked, and **SWITCH**.
  **SWITCH** (Y) reloads the game with no mod; **CANCEL** (Escape)
  leaves everything as it was.

**UNLOAD MOD**, on the Active tab while a mod is played, asks that same
question and follows that same path back to the base game. It is greyed in
the same places, and resting on it shows the same note.

Both change the mod only from the main menu: the Library shows only there,
and a game keeps the mod it started with. While `--mod-dir` or
`--base-game` chose the run's mod, both are greyed and the details say "The
command line chose this run's mod." **OPEN FOLDER** shows a mod's folder,
and **MODS** under Your files in Settings › System your Mods folder,
where a mod's folder goes to be listed
([settings](settings.md#system)).

## Keys and controllers

| Key | Does |
|---|---|
| Tab, Shift+Tab | the next and previous control |
| arrow keys | move by where controls are; on the list, Up and Down choose the row |
| Page Up, Page Down, Home, End | move through the list, or scroll the details while they have the focus, and on the Active tab scroll the maps pane |
| Enter, Space | on a row: open its details page in Compact, or go to its first button; on UNLOAD MOD: ask SWITCH MOD; on a map's From link: open Maps on that pack; on the maps search, Enter moves to the first map link |
| Cmd+F (Ctrl+F elsewhere), or `/` | the search; on the Active tab, the maps search |
| F5 | the header's refresh icon, when a registry can be asked |
| Ctrl+Tab, Ctrl+Shift+Tab | the next and previous tab |
| Escape | clears the search while it has the focus, else leaves the details page, else closes the Library |

With a controller, Y focuses the search (the maps search on the Active tab),
X presses the chosen entry's GET,
UPDATE, CANCEL or RETRY, L1 and R1 change tab, B goes back as Escape does,
and the right stick scrolls ([gamepad controls](controllers.md#menus)).
A finger or the mouse presses any control. A press, double click or tap on
a row chooses it: nothing installs until you press GET or UPDATE, in the
details or as the row's download arrow or up-arrow.

## Installing

**GET** installs a package, in the details or as the download arrow on its
row. It is the only way anything gets onto your machine from the Library:
OA never installs a package, or an update, that you did not press a button
for.

The download goes into the queue, and the package is checked before
anything is written: its SHA-256 against the signed catalogue's, then the
same checks as a file you open, its limits, the hacks a mod needs, and for
a map pack whether its maps fit 3.1c. Each failure keeps whatever was
installed before. Then:

- **a mod** goes into your Mods folder, and **PLAY NOW** switches to it;
- **a map pack** goes into your Maps folder, and its maps join the map
  browser at once. The ones that fit the mod you are playing join the
  skirmish list and the battle room's map list at once, including when
  you had either list open before the download and open it again, and
  when you go from the Library straight to Skirmish or a battle room.
  Removing the pack takes those maps off both lists at once. A mod the
  catalogue has no row for is decided by the map's files. One that does
  not fit is left out of those two lists
  ([map packs](maps/README.md));
- **a language pack** goes into your Languages folder, and Settings ›
  Language offers it at once ([languages](languages.md)).

Whether a download asks you anything first depends on where it comes from
and what you already have: [which downloads ask first](#which-downloads-ask-first).
While the Library is open, how an install went shows in its queue. An
install from the Library shows no notice when it is in place, whether or
not the Library is still open: the row and its pill already say so. A
file you open, from the command line or from outside the game, still says
so in a notice on the main menu.

## The download queue

One download runs at a time, at full speed; the others wait their turn.
The queue is at the foot of the Library, under the list:

- the first line says what runs now: "Downloading Cinder 3.1 · 18.2 of
  64.0 MB", "Waiting to download …", "Verifying Cinder", "Installing
  Cinder", "Installed Cinder", an update held for the mod being played
  ("Waiting · finishes when you leave Ridge"), or a failure;
- the second says what comes next: "Then Ridge 4.8 · waiting", and how many
  more;
- in Compact one line, "Cinder 28%" with a bar.

After an update that changes the rules the line says so: "Updated Ridge ·
revision 4 changes the game's rules · ROLL BACK restores revision 3", or
"… may change …" when OA cannot tell.

**CANCEL** in the details stops a download; what has come is kept, so a
later GET resumes it. A download cut short by quitting resumes at the next
start. A failure says why, as "The server could not be reached", "The
registry no longer offers this release", "There is not enough free space"
or "The download was not the file the catalogue promised", and **RETRY**
tries again, except when downloads from that registry are off or the
release is no longer offered: then GET or UPDATE comes back, and for a
release no longer offered OA fetches that registry's list again.

Nothing downloads during a game: a download that runs when a game starts
stops at its next step, and goes on, shown as waiting, once the game ends.

## Updates

OA looks for updates whenever it fetches the registries' lists, which
**Check for content updates** in Settings › Downloads decides
([when OA checks](downloads.md#when-oa-checks-for-content)), and **CHECK
NOW** at the foot of the Library asks every registry that is on at once,
whatever that setting says. It is greyed while no registry is on, and
while the registries are being asked.

A package is offered an update when the registry it was installed from
lists the same package at a higher release than the one installed: OA
records where each package came from when it installs it. A package you
installed from a file is matched to a registry's release by its SHA-256,
and offered that registry's updates from then on; one no registry lists is
your own copy and is never offered one. So is a mod folder from Open
Annihilation 0.7 or made by hand. A release of the same package in another
registry is not an update.

An update shows:

- as the circled up-arrow on the package's row, which is its **UPDATE**,
  and in its details with what changes;
- on the **Updates** tab, every kind together, with **UPDATE ALL** over the
  list, which starts every update the list shows that can be installed;
- as the dot on the main menu's **OA** button.

**UPDATE** and **UPDATE ALL** start updates; OA finds updates by itself, but
never installs one on its own. A mod that changes its rules is your call,
mid-campaign above all.

## The Registries tab

**Registries** is the last tab. It lists every registry the packages come
from, each on its own card, one under another with a gap between them,
with no search and no filters. Turning one off, or removing one, changes
the other tabs at once: its packages leave the lists as soon as the switch
or **REMOVE** is pressed.

Each card shows:

- an avatar, the short name's initials in a square: the first letters of
  its first three words, or of a name of one word its first two letters;
- the heading, which is the registry's title when it has one ("Core Prime
  - Official Open Annihilation Packages") and its short name when it does
  not;
- a key icon, whole, in the title band at the card's right. A built-in
  registry whose author is Open Annihilation or Cavedog Entertainment
  shows that maker's mark directly under the key, the same size, inside
  the band. A registry you added shows no mark. Resting on the key,
  or moving the keys or the pad onto it, shows a card headed **Signing key**,
  or **Signing keys**, with the keys its signatures are
  checked against: each key's fingerprint ("7001 · af50 · b971 · 5bb4")
  and its full public key (`ed25519:…`), and "built in" or "added by you",
  wide enough or wrapped so those words show whole. A registry that names
  no key says "No signing key". Pressing the icon asks for nothing;
- **REFRESH**, a standard button beside the switch. It asks that registry
  for its catalogue. It is greyed while the
  registry is off or already being asked, and while it is being asked the
  refresh icon beside it turns, one full turn every 1.6 seconds. When that
  ask reads a catalogue the icon shows a check for 0.8 seconds, then the
  arrow; an ask that finds nothing goes straight back to the arrow.
  Pressing **REFRESH** during the check asks again at once;
- an Off/On switch, the same switch Settings draws;
- the address it was added from, or its catalogue's address when it was
  not added from one;
- when it was last read: "refreshed 5 min ago", "never refreshed" before
  the first refresh, or "couldn't reach · 2 h ago" when the last refresh
  found nothing usable;
- the description, when the registry has one, as a paragraph under the
  address and the refresh line;
- a subtitle. A built-in registry's says how many packages it lists
  ("1 package"). One you added says how many packages and "not reviewed"
  ("31 packages · not reviewed"), then how many mirrors you added
  (" · 2 mirrors").

A registry that ships with the game has a **Built in** pill at the card's
right, under the key icon, **REFRESH** and the switch. It is the same
neutral pill the Active tab uses, so it does not look like an installed
chip.

A registry you added also has **REMOVE** and **ADD MIRROR…**, standard
buttons, under the subtitle. **REMOVE** forgets the registry at once and
leaves what you installed from it in place. Core Prime's registry can be
turned off and never removed, and it has no **ADD MIRROR…**: its mirrors
come only from its own catalogue. Turning a registry on fetches its list
at once.

Under the last card a note says "Content from registries other than Core
Prime isn't reviewed by the OA team. OA still checks every package before
using it.", naming every built-in registry by its short name ("Core Prime
and North", "Core Prime, North and West"), or, when none is built in,
"Content from these registries isn't reviewed by the OA team. …". When a
change is refused, the reason stands over the note ("OA could not change
the registries: …").

**ADD REGISTRY…** sits at the top right of the tab, above the cards. The
footer holds **CLOSE**. It opens the Add registry window
([adding a registry](downloads.md#adding-a-registry)). An address may be
`http` or `https`. Where this system has no TLS, `https` is read as
`http`, and Settings › Downloads says so. **CLOSE** leaves a switch or a
removal as it was stored. During a game the padlock and "Choose from the
main menu." stand where **ADD REGISTRY…** was, and **ADD MIRROR…** is
locked the same way; the switches, **REFRESH** and **REMOVE** stay.

The cards and the note scroll together when they are taller than the
window. Settings › Downloads, on the main menu, has **REGISTRIES IN THE
LIBRARY…**, which opens the Library on this tab.

![The Registries tab at Compact: Core Prime's card, with its title, description, key icon, REFRESH, switch and Built in pill.](images/library/library-registries-compact.png)

![The Registries tab at Regular: the same cards, one under another.](images/library/library-registries-regular.png)

![The Registries tab at Large: the same cards, with the note under the last.](images/library/library-registries-large.png)

## Which downloads ask first

**Downloads from Core Prime's registry install when you press the button,
with no question.** GET, UPDATE, UPDATE ALL, a language in Settings ›
Language or its notice, the map browser's Download and select, and GET MAP
install the package from Core Prime's registry as soon as it has
downloaded and passed its checks, mods and language packs always. The one
exception is a map pack that would replace a copy of it you did not get
from Core Prime ([map packs, below](#map-packs-that-replace-a-copy)). A mod
update that changes
the rules does not ask either: its row and its details say so before you
press UPDATE ("Revision 4 changes the rules", "Rules: 9c1f 4e0b → 51d2 07aa
· changes the game's rules; saved games from revision 3 won't load under
revision 4"), the queue says so after it, and ROLL BACK brings the earlier
revision back. Core Prime's mod replaces whatever copy of it your Mods
folder holds, keeping that copy to roll back to. It never goes beside a
folder of your own: when a folder of the mod's name holds something else,
it is not installed, and the Library's queue says "Mods/*folder* is a
folder of your own, not *name*. Move or rename it, then RETRY.", or, with
the Library closed, a **MOD NOT INSTALLED** notice says so.

**Registries you add yourself ask first.** A mod from a registry you added
asks before it changes anything you did not get from that same registry:

- **the update question**, before an update that changes the rules, or may
  change them ([below](#the-update-question));
- **REPLACE MOD**, when the mod is installed from another registry:
  "*Name* from *installed registry* is installed. Replace it with *name*
  from *new registry*? The one from *installed registry* is kept, and ROLL
  BACK brings it back.", with **CANCEL** and **REPLACE**;
- **FOLDER IN USE**, when your Mods folder has a folder of the mod's name
  that holds something else: "Your Mods folder already has a folder named
  *folder* that does not hold *title*. It is left as it is. Install *title*
  *version* alongside it, in a folder of its own?", with **CANCEL** and
  **INSTALL ALONGSIDE**;
- over a copy you installed from a file, the questions that file would
  ask: **UPDATE MOD**, **ANOTHER VERSION** or **ALREADY INSTALLED**
  ([installing a .oamod file](mods/README.md#installing-a-oamod-file)).

It installs with no question when the mod is new to you, and when it
replaces the copy that registry installed, or a folder with no record of
where it came from.

**Language packs never ask**, from any registry: one installs as it
arrives, on any screen but a game, and the language applies at once if it
is the one you chose ([languages](languages.md#installing-a-language-pack)).

<a id="map-packs-that-replace-a-copy"></a>**Map packs** install with no question, from any registry, when the pack is
new to you or replaces the copy that same registry installed, on any
screen but a game. When your Maps folder holds the same pack from a file,
made by hand or from another registry, OA asks before it replaces it, as it
does for a `.oamap` file you open (**REPLACE MAP PACK**, **UPDATE MAP
PACK** or **REINSTALL MAP PACK**), whichever registry the download comes
from, and the question waits for the main menu
([map packs](maps/README.md#installing-a-map-pack)).

While the Library is open its questions show over it, and what happened
shows in its queue.

## The update question

A mod from a registry you added asks before an update that changes the
game's rules, or may change them, because saved games load only under the
rules they were saved with:

> **Update Ridge**
>
> Ridge 4.8 · revision 3 → 4
>
> Revision 4 changes the game's rules. Saved games from revision 3 won't
> load under revision 4. Revision 3 is kept, and ROLL BACK brings it back.
>
> Rules: 9c1f 4e0b → 51d2 07aa
> Download: 3.2 MB
>
> **NOT NOW** · **UPDATE**

When OA cannot tell, it says "OA can't tell whether revision 4 changes the
game's rules. If it does, saved games from revision 3 won't load under it.
Revision 3 is kept, and ROLL BACK brings it back.", with "Rules: unknown".
For the mod being played it adds "It finishes when you leave Ridge.".
**NOT NOW**, or Escape, leaves the mod as it is; **UPDATE** starts the
download. An update whose rules stay the same does not ask.

**UPDATE ALL** asks once for all the updates that would ask: "1 update
changes the game's rules: Ridge. …" or "2 updates change the game's rules:
Ridge, Cinder. Saved games from their current versions won't load under
the new ones. Each is kept, and ROLL BACK brings it back." The updates that
do not ask start whatever the answer.

## Rolling back

Updating or replacing a mod keeps the copy it replaced, one copy back.
**ROLL BACK**, in the Library's details, brings it back. It shows on a mod
only while the copy kept is one the game can play. It asks first, **ROLL
BACK MOD**: "Roll back Ridge to 4.8 revision 3? 4.8 revision 4 is kept in
its place, and ROLL BACK again brings it back.", with **CANCEL** and **ROLL
BACK**. The two copies swap, so a second ROLL BACK undoes the first. The
record of where the mod came from goes back with it, so the update you
rolled back is offered again, and nothing installs it until you press
UPDATE. A roll back that cannot be made, as when the copy kept can no
longer be played, changes nothing, and **MOD NOT ROLLED BACK** over the
Library says why.

Rolling back the mod being played reloads the game's data at once and
returns to the main menu ("The game reloads its data for the mod and
returns to the main menu."). In 0.8 only mods roll back: the button also
shows on a map or language pack that keeps an earlier copy, but does
nothing there.

## Uninstalling

**UNINSTALL**, in an installed package's details after ROLL BACK, removes a
mod, map pack or language pack you no longer play. It asks first:

> **Uninstall Ridge**
>
> Uninstall Ridge? Its folder, Mods/ridge, is removed, and with it the copy
> ROLL BACK keeps.
>
> GET installs it again.
>
> **CANCEL** · **UNINSTALL**

The question names the package's folder in your own Open Annihilation
folder, `Mods/<id>`, `Maps/<id>` or `Languages/<tag>`, and says when the
copy ROLL BACK keeps goes with it. For a package you installed yourself
that no registry lists, it says "No registry lists it, so the Library
cannot get it again." **CANCEL**, or Escape, removes nothing. **UNINSTALL**
removes the folder and everything in it; the Library then shows the
package as not installed, with GET, or drops it when no registry lists it.

Some packages cannot go yet, and nothing is removed:

- **the mod you are playing**: a notice says "You are playing Ridge.
  Switch it off first with SWITCH OFF, then uninstall it.";
- **the language pack the game is shown in**, or one its words fall back
  to: a notice says "The game is shown in 简体中文. Choose another language
  in Settings › Language first, then uninstall it.";
- **a map pack during a game**: UNINSTALL is off, and the note under the
  buttons says "Map packs can't be uninstalled during a game.";
- **a package in the download queue**, while it downloads, is checked or
  installed, or waits for you to leave the mod being played: UNINSTALL is
  off until you press CANCEL.

Uninstalling stays inside your Mods, Maps and Languages folders. The
package's folder is moved aside in one step, under a hidden name the game
never reads as a package, and then deleted a little at a time; a folder
that is a link to somewhere else is refused, and a link inside the folder
is removed without touching what it leads to. If OA stops while it is
deleting, the next start finishes the job. When it cannot remove the
folder, because another program is using it, or the folder changed since
the Library listed it, a notice says so and the package stays as it was.

## Finishes when you leave …

The mod being played cannot change under the running game, so an update to
it, from the Library, downloads and is checked now, and is made when you
leave that mod, by switching to another, switching it off or quitting.
Until then the details say "Finishes when you leave Ridge.", and the queue
says "Waiting · finishes when you leave Ridge". Once it is made, the mod's
row shows the new revision. No notice is shown. A file you opened still
says so on the main menu.
**CANCEL** on the waiting update drops it, and leaves the mod as it was; it
is offered again.

Updating any other mod changes nothing you are playing, and the details
say so: "You are playing Ridge. Updating Cinder doesn't restart the game."
A `.oamod` file you open keeps its own behaviour: it replaces the mod being
played and reloads the game at once
([installing a .oamod file](mods/README.md#installing-a-oamod-file)).

## Offline

Nothing needs a registry to play: everything installed works with no
network, and the game starts and plays without reaching any server. The
Library keeps the last list each registry gave it. The header shows the
mods icon, the plates over drive bays, then the title and this build's
version. When a registry could not be reached, the
word "offline" stands in amber beside the refresh icon, and a line over
the list says, in amber, "Showing the list from 2 h ago. OA couldn't reach
the registries." The list stays usable however old it is.

## Check that you're a person

When many downloads come from one address in a short time, as at a LAN
party, Core Prime's registry may ask for a check before more downloads,
and OA shows it on the main menu, over the Library or the settings:

> **Downloads** · Check that you're a person
>
> Before more downloads, Core Prime needs to check this isn't an automated
> program. Open the check on this computer, or go to
> downloads.coreprime.net/verify on any device and enter:
>
> **HXQ7-4KP2**
>
> Waiting for the check. This updates by itself.
>
> **CANCEL** · **OPEN THE CHECK**

**OPEN THE CHECK** opens the check's page in your browser. Where OA has no
browser to open, as on a Steam Deck in Game Mode, the window says "Go to
downloads.coreprime.net/verify on another device and enter:", and you
enter the code on your phone or another computer; you can always do that
instead, from any device. Once the check is passed the window closes by
itself and the download goes on; your install ID is then trusted for a
day. A check left too long says "The check ran out. Try again for a new
code.", and **TRY AGAIN** asks for a new one. **CANCEL** cancels that
download and the others waiting for that registry. A check that comes
during a game waits for the main menu. What a check keeps is under
[privacy](downloads.md#what-core-prime-records).

## Where packages go

Packages install into your own Open Annihilation folder
([the installation guides](installation/macos.md#where-it-keeps-its-files)
say where it is), each in a folder named by its key: `Mods/<id>`,
`Maps/<id>` and `Languages/<tag>`. Beside a package's files OA keeps
`.oa-origin.yaml`, the record of where it came from, and `.backup`, the
copy an update replaced. **UNINSTALL** removes a package's folder with
both ([uninstalling](#uninstalling)), and **OPEN FOLDER** shows it.
