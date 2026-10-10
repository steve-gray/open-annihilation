# Content

The content service keeps every registry in effect, built-in and added, and
the catalogue cached for each one. A worker thread fetches a catalogue and
its signature, checks them, and tries the next mirror when a source cannot
be used. The main thread reads an immutable snapshot. There is no UI.

The same module stores the player's download settings: one install ID per
registry, when to check for catalogue updates, and the size of the
downloaded files.

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
- `check_registry_url` and `check_registry_file` check a registry before
  it is trusted. `check_mirror` does the same for a mirror of a registry
  the player already added. Each returns a request id. `registry_check`
  reads the outcome once the worker has finished, and nothing until then.
  A check fetches on the worker: the descriptor (an address is fetched,
  a file is the bytes the caller already has), every refusal rule, then
  the catalogue and its signature. The catalogue has to verify with the
  keys about to be trusted. A file has no address.
- `add_checked_registry` stores a check that was accepted, with the
  service clock's date, and writes that catalogue into the cache.
  `remove_registry` removes an added registry and that registry's cache
  folder. A registry that ships with the game can be turned off, not
  removed. `set_registry_enabled` turns a registry on or off and, when
  it is turned on, queues a refresh. `add_checked_mirror` stores a mirror
  a check has accepted. These edits run on the calling thread. They write
  only `Registries.yaml` and the cache, and they write nothing while
  `Registries.yaml` could not be read. Nothing is trusted without its
  own catalogue verifying.
- `oa/app/content/updates.hpp` lists installed packages and the catalogue
  releases that update them. `list_installed`, `match_installed`,
  `find_updates` and `waiting_updates` only read. `adopt_match` is the one
  write. `this_engine_rules` is this build's release and the hacks it
  carries out.

`oa/app/content/settings.hpp`, same namespace:

- `read_install_id`, `ensure_install_id`, `reset_install_id`,
  `turn_install_id_off` and `turn_install_id_on` keep one install ID per
  registry in the preference values. `make_install_id_text` makes one from
  the system's random bytes. `install_id_shown` is the form Settings shows.
- `read_check_for_updates` and `write_check_for_updates` are the player's
  Check for content updates setting (`automatically`, `library`, `never`).
- `downloads_folder`, `downloaded_bytes` and `empty_downloads` are the
  downloaded files under the data folder.

The runtime owns one service from start to quit
(`start_content`, `tick_content`, `content_service` in
`src/app/runtime_content.cpp`). `start_content` reads the update setting.
`content_install_id` is how a download gets a registry's ID, making it
then when none is stored. `tick_content` passes Developer mode on
when it changes.

## State

Reads `Registries.yaml` in the player's folder and the built-in descriptors
in `registries/` beside the program. Reads and writes
`<data>/content/catalogues/<registry id>/` (`catalogue.json`,
`catalogue.json.sig` when a signature was served, and `state.yaml`). Writes
`Registries.yaml` to store a key rotation, a registry the player added
or removed, a registry turned on or off, or a mirror the player added.
An edit replaces that file through the platform's file replace, so a
failure leaves the previous file in place. Without a data folder the
catalogues stay in memory.

The install ID and the update setting live in the preference values the
caller passes (`open-annihilation.install-id.<registry id>`,
`open-annihilation.content-updates`). Downloaded files are
`<data>/content/downloads/`.
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
unreadable `Registries.yaml` is never written. A registry is added only
after its catalogue has verified with the keys being trusted, and a
mirror only after its catalogue has verified with the keys already
trusted for that registry.

`list_installed` and `find_updates` only read. `adopt_match` is the one
write, and it writes only through the installer's origin record, which
holds the kind's root lock and replaces the file whole. While an install
holds that lock, it writes nothing.

An install ID is made only when a download needs one, or when the player
resets it. It is not made at start, not shared between registries, and not
made while the stored value is `off`. `empty_downloads` removes only
package files and their partial downloads in that folder, never a folder,
never `queue.yaml` and never a file a download is using.

## Tests

`app-content-refresh-rules` covers when a refresh is due, which registries
contribute packages, and how a reference resolves. `app-content` fetches
from a local fixture: the cache, mirrors, expiry, key rotation, Developer
mode, the player's setting, the worker and stopping mid-fetch.
`app-content-settings` covers the install ID, the update setting and
emptying the downloads folder.
`app-content-registries` adds, removes and turns off registries against
a local fixture, including a mirror and a `Registries.yaml` that cannot
be read.

`app-content-updates` covers ten cases on temporary folders and in-memory
catalogues, with no network: a higher release, an equal or lower release,
the same key in another registry, a file matched by SHA-256 and then
adopted, an unknown SHA-256, a folder with no record, the rules hash, a
release this build cannot install, a roll back that offers the update
again, and a listing that changes no file.

## Limitations

No downloads. A refresh in flight when the game quits is dropped. A
check that has not finished is dropped with it.
No downloads, and no adding or removing registries. A refresh in flight
when the game quits is dropped. Turning an install ID off does not, by
itself, refuse a download: the download asks for the ID and stops when
there is none.
when the game quits is dropped.

A file install of an older release cannot be matched, because a catalogue
lists only each key's latest release. A mod whose manifest changes without
its size or modification time changing keeps the remembered rules hash
until one of those does.
