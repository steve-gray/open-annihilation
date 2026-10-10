# Content

The content service keeps every registry in effect, built-in and added, and
the catalogue cached for each one. A worker thread fetches a catalogue and
its signature, checks them, and tries the next mirror when a source cannot
be used. The main thread reads an immutable snapshot. There is no UI.

## Entry points

`oa/app/content/service.hpp`, namespace `oa::app::content`:

- `Service` holds the registries, the cache and the worker. `start` reads
  `Registries.yaml`, the built-in folder and the cache. `refresh` queues a
  fetch. `snapshot` and `generation` are what a frame reads;
  `generation()` moves on whenever the snapshot changes.
- `make_snapshot` merges registry views into that snapshot without a network.
- `reference_urls` resolves a catalogue reference against the host that
  served the catalogue, then the registry's other sources.
- `run_on_worker` queues a job on the worker, behind the work already
  queued, with the worker's HTTP client. `on_refreshed` adds a listener
  called on the worker after a refresh that found a usable catalogue.
- `oa/app/content/updates.hpp` lists installed packages and the catalogue
  releases that update them. `list_installed`, `match_installed`,
  `find_updates` and `waiting_updates` only read. `adopt_match` is the one
  write. `this_engine_rules` is this build's release and the hacks it
  carries out.

The runtime owns one service from start to quit
(`start_content`, `tick_content`, `content_service` in
`src/app/runtime_content.cpp`). `tick_content` passes Developer mode on
when it changes.

## State

Reads `Registries.yaml` in the player's folder and the built-in descriptors
in `registries/` beside the program. Reads and writes
`<data>/content/catalogues/<registry id>/` (`catalogue.json`,
`catalogue.json.sig` when a signature was served, and `state.yaml`). Writes
`Registries.yaml` only to store a key rotation. Without a data folder the
catalogues stay in memory.

## Updates

An update is a catalogue release of the same kind and key, from the same
registry, with a higher release number than the one recorded for the
installed package. The version text is kept to show the player and is never
what decides it. Nothing here downloads or installs a release.

Provenance is decided from the origin record and the catalogues in effect:

- A catalogue record is that registry and key at the release it names, even
  when the registry is now off or gone. With no listing, there is no update.
- A file record matches an enabled entry of the same kind whose SHA-256 is
  the file's, at that entry's release. When two registries list the same
  bytes, a built-in registry wins, and otherwise the one listed first.
  `adopt_match` rewrites the record as a catalogue origin, keeping the
  SHA-256 and dating it today, so the next release is offered as an update.
- Anything else is the player's own and is never offered an update: a file
  whose SHA-256 no entry lists, or a folder with no record.

Names, ids and versions are not how a match is decided.

For a mod, the rules hash is its profile resolved with no player settings
and no Developer Mode overrides, the same way a catalogue's `sim_hash` is
produced. Both hashes present and equal means the rules are unchanged; both
present and different means they change; otherwise it is unknown. A language
pack or a map pack has no rules hash, so an update of one is unchanged.

An update this build cannot install is still listed. An engine requirement
this build does not meet is reported first, then a base other than
`ta-3.1c`, then a hack the registry does not carry out. Up to three missing
hack ids are named, and the rest as "N more". `waiting_updates` does not
count a blocked update. Results are ordered by kind (mods, map packs,
language packs) and then by name, ignoring letter case.

The rules hash of a mod is remembered for its folder by the manifest's size
and modification time, under a lock, so a refresh does not resolve an
unchanged mod again.

## Invariants

A fetch never runs on the main thread's time, and `snapshot()` does not
wait for one. A headless check or an unattended run does not refresh at
start. The cache only ever holds bytes the catalogue check accepted. An
unreadable `Registries.yaml` is never written.

`list_installed` and `find_updates` only read. `adopt_match` is the one
write, and it writes only through the installer's origin record, which
holds the kind's root lock and replaces the file whole. While an install
holds that lock, it writes nothing.

## Tests

`app-content-refresh-rules` covers when a refresh is due, which registries
contribute packages, and how a reference resolves. `app-content` fetches
from a local fixture: the cache, mirrors, expiry, key rotation, Developer
mode, the player's setting, the worker and stopping mid-fetch.

`app-content-updates` covers ten cases on temporary folders and in-memory
catalogues, with no network: a higher release, an equal or lower release,
the same key in another registry, a file matched by SHA-256 and then
adopted, an unknown SHA-256, a folder with no record, the rules hash, a
release this build cannot install, a roll back that offers the update
again, and a listing that changes no file.

## Limitations

No downloads, and no adding or removing registries. A refresh in flight
when the game quits is dropped.

A file install of an older release cannot be matched, because a catalogue
lists only each key's latest release. A mod whose manifest changes without
its size or modification time changing keeps the remembered rules hash
until one of those does.
