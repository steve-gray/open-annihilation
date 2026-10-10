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

The runtime owns one service from start to quit
(`start_content`, `tick_content`, `content_service` in
`src/app/runtime_content.cpp`). `tick_content` passes Developer mode on
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

## Invariants

A fetch never runs on the main thread's time, and `snapshot()` does not
wait for one. A headless check or an unattended run does not refresh at
start. The cache only ever holds bytes the catalogue check accepted. An
unreadable `Registries.yaml` is never written. A registry is added only
after its catalogue has verified with the keys being trusted, and a
mirror only after its catalogue has verified with the keys already
trusted for that registry.

## Tests

`app-content-refresh-rules` covers when a refresh is due, which registries
contribute packages, and how a reference resolves. `app-content` fetches
from a local fixture: the cache, mirrors, expiry, key rotation, Developer
mode, the player's setting, the worker and stopping mid-fetch.
`app-content-registries` adds, removes and turns off registries against
a local fixture, including a mirror and a `Registries.yaml` that cannot
be read.

## Limitations

No downloads. A refresh in flight when the game quits is dropped. A
check that has not finished is dropped with it.
