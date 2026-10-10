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
`Registries.yaml` only to store a key rotation. Without a data folder the
catalogues stay in memory.

The install ID and the update setting live in the preference values the
caller passes (`open-annihilation.install-id.<registry id>`,
`open-annihilation.content-updates`). Downloaded files are
`<data>/content/downloads/`.

## Invariants

A fetch never runs on the main thread's time, and `snapshot()` does not
wait for one. A headless check or an unattended run does not refresh at
start. The cache only ever holds bytes the catalogue check accepted. An
unreadable `Registries.yaml` is never written.

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

## Limitations

No downloads, and no adding or removing registries. A refresh in flight
when the game quits is dropped. Turning an install ID off does not, by
itself, refuse a download: the download asks for the ID and stops when
there is none.
