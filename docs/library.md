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
- [Offline, and the list's age](#offline-and-the-lists-age)
- [Check that you're a person](#check-that-youre-a-person)
- [Where packages go](#where-packages-go)

## Opening the Library

The Library opens on the main menu, from:

- **the OA button**, the Open Annihilation logo at the bottom right of the
  main menu, left of the **Settings** button and its gears. It opens on the
  [Active](#the-active-tab) tab. While updates you can install wait, the
  button wears an amber dot in its top right corner, from the moment OA
  finds them: you need not open the Library to learn of them, and its
  **Updates** tab lists them;
- **MORE IN THE LIBRARY…** in Settings › Language, which opens it on
  Languages, over the settings, which stay open under it.

The
skirmish and battle room map lists have **MORE MAPS…** instead, which opens
the map browser, and the battle room has **GET MAP**, which downloads the
host's map without a detour ([map packs](maps/README.md)).

The settings have a button of their own on the main menu, the gears beside
the OA button, which opens them with no Library under them
([settings](settings.md#opening-the-settings)). From the Library, **Cmd+,**
on macOS and **Ctrl+,** elsewhere opens them over the Library. The header's
top right holds a refresh icon. Pressing it, or F5, asks the registries
for their lists at once ([updates](#updates)). While they are being asked
the icon turns, and it cannot be pressed again until they answer. **CLOSE**,
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

**Active**, the first tab, shows what you are using now, top to bottom:

- **MOD**: the mod you are playing, with its badge, name and version
  ("Ridge 4.8 · rev 3"), or "No mod is played";
- **LANGUAGE**: the language pack the game's words come from, with its
  version ("Deutsch 1.2"), or the language the game holds itself
  ("English · built in");
- **MAPS**: each map pack you have installed, by name, with its maps under
  it: each map's name, how many players it is for and its size ("Ridgeline
  · 4 players · 16x16"), or "No map packs are installed."

It is a list to read: nothing on it can be chosen or pressed, and it has no
search and no filters. When it is longer than the window it scrolls with
the mouse wheel, a finger, Page Up and Page Down, Home and End, and a
controller's right stick. The other tabs are where packages are got,
updated and played.

## Tabs and states

The tabs are **Active**, **Mods**, **Maps**, **Languages**, **Updates**
and **Registries** ("Lang", "Upd" and "Reg" in the Compact layout). Each
tab after Active shows how many it lists under All, before a search or a
tag: "Mods 11", "Maps 2", "Languages 2", "Updates 2", "Registries 2", and
the short names keep the count ("Upd 2"). Updates counts the updates you
can install, as the OA button's dot does, and its digits stay amber while
the count is above 0.
Registries counts the registries listed. Registries is last, and it is
where registries are added, checked and turned off
([the Registries tab](#the-registries-tab)).

Each row shows the package's badge (its own picture, or up to three
letters), its name, its version ("4.8 · rev 4" from its second packaging
revision on, and a map pack's number of maps), its size and its state. The
Regular and Large rows add who made it and its summary, with its download
count where the registry keeps one ("by the Ridge team · 12,400
downloads"), and Large adds its tags. A Compact row names the registry
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
layouts, or as a page of its own in Compact, with "‹ Mods" (or the tab's
name) back to the list.

- **A status line**: "Not installed · 41.0 MB", "Installed", "Playing",
  "Revision 4 available · 3.2 MB to download" or "Version 4.9 available ·
  …", or "Update available · 3.4 MB to download" when the registry names
  no revision for an update of the same version, "No oamod.yaml present"
  for a mod folder without one, where the download stands ("Waiting to download", "Downloading ·
  18.2 of 64.0 MB", "Verifying", "Installing"), or why it cannot be got
  ([above](#tabs-and-states)).
- **The summary**: the registry's, or for a mod of your own its
  `oamod.yaml`'s description.
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

The Mods tab lists every mod you can play: the ones you installed, from a
registry or a file, and every other folder the game can play as a mod,
each marked "Your own copy": the folders in the game folder's `mods`
folder and in your own Mods folder, `Documents/Open Annihilation/Mods`,
a folder without an `oamod.yaml`, which shows "N/A" for its version and
"No oamod.yaml present" in its details and plays with the game's own
rules, and a folder an earlier version's Pick Folder… chose, while it is
still the mod you play. A mod of your own shows its `oamod.yaml`'s
description in its details. The one you play is marked **PLAYING**; with
no mod none is.

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

Both change the mod only from the main menu: the Library shows only there,
and a game keeps the mod it started with. While `--mod-dir` or
`--base-game` chose the run's mod, both are greyed and the details say "The
command line chose this run's mod." **OPEN FOLDER** shows a mod's folder,
and **MODS** under Your files in Settings › Common Tweaks your Mods folder,
where a mod's folder goes to be listed
([settings](settings.md#common-tweaks)).

## Keys and controllers

| Key | Does |
|---|---|
| Tab, Shift+Tab | the next and previous control |
| arrow keys | move by where controls are; on the list, Up and Down choose the row |
| Page Up, Page Down, Home, End | move through the list, or scroll the details while they have the focus, and the Active tab |
| Enter, Space | on a row: open its details page in Compact, or go to its first button |
| Cmd+F (Ctrl+F elsewhere), or `/` | the search |
| F5 | the header's refresh icon, when a registry can be asked |
| Ctrl+Tab, Ctrl+Shift+Tab | the next and previous tab |
| Escape | clears the search while it has the focus, else leaves the details page, else closes the Library |

With a controller, Y focuses the search, X presses the chosen entry's GET,
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
  browser and the battle room's map list at once, and the skirmish list at
  the next start, or as soon as you choose one in the map browser
  ([map packs](maps/README.md));
- **a language pack** goes into your Languages folder, and Settings ›
  Language offers it at once ([languages](languages.md)).

Whether a download asks you anything first depends on where it comes from
and what you already have: [which downloads ask first](#which-downloads-ask-first).
While the Library is open, how an install went shows in its queue. A mod
whose install finishes after you close the Library says so in a notice on
the main menu, as a `.oamod` file you open does.

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
from, with no search and no filters. Turning one off, or removing one,
changes the other tabs at once: its packages leave the lists as soon as
the switch or **REMOVE** is pressed.

Each row shows:

- an avatar, the name's initials in a square: the first letters of its
  first three words, or of a name of one word its first two letters;
- the name, and a **Built in** chip when the registry ships with the game;
- a subtitle. A built-in registry's says how many packages it lists and
  when its list was last checked ("1 package · checked 4 min ago", or
  "not checked yet" before the first check). One you added says its
  address, how many packages and "not reviewed"
  ("maps.example.org · 31 packages · not reviewed"), then how many mirrors
  you added (" · 2 mirrors"). When the last check found nothing usable it
  says "couldn't reach", and "list from *age*" when a list was kept;
- an Off/On switch.

A registry you added also has **REMOVE** and **ADD MIRROR…** under the
subtitle. **REMOVE** forgets the registry at once and leaves what you
installed from it in place. Core Prime's registry can be turned off and
never removed, and it has no **ADD MIRROR…**: its mirrors come only from
its own catalogue. Turning a registry on fetches its list at once.

Under the list a note says "Content from registries other than Core Prime
isn't reviewed by the OA team. OA still checks every package before using
it.", naming every built-in registry ("Core Prime and North", "Core Prime,
North and West"), or, when none is built in, "Content from these
registries isn't reviewed by the OA team. …". When a change is refused,
the reason stands over the note ("OA could not change the registries:
…").

**ADD REGISTRY…** sits in the footer, beside **CLOSE**. It opens the same
Add registry window as opening an `.oareg` file: the address or "From
*file*", the fingerprints of the keys, and
"Not reviewed by the OA team"
([adding a registry](downloads.md#adding-a-registry)). **CLOSE** leaves a
switch or a removal as it was stored. During a game **ADD REGISTRY…** and
**ADD MIRROR…** are locked ("Choose from the main menu."); the switches
and **REMOVE** stay.

The rows and the note scroll together when they are taller than the
window. Settings › Downloads, on the main menu, has **REGISTRIES IN THE
LIBRARY…**, which opens the Library on this tab.

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
says "Waiting · finishes when you leave Ridge". Once it is made, a **MOD
UPDATED** notice says so on the main menu.
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
title and this build's version. When a registry could not be reached, the
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
