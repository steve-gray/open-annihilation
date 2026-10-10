# Running a registry

A direct registry is a folder on any static HTTP server. OA fetches it over
plain HTTP and checks the signature, so the server does not need TLS.

## Make the folder

1. Make a sealed publisher key, and keep the file private.
   `oa-tool catalogue keygen --id ID --out FILE` prints the public key and
   its fingerprint. Put only the public key in the descriptor.
2. `oa-tool registry init FOLDER --id ID --name NAME --base-url URL --key KEY_FILE`
   makes a direct registry. `FOLDER` must be missing or empty.
   `--base-url` is the `http` address the registry is served at, with no
   trailing slash. `--homepage` is optional. The command writes
   `registry.yaml`, an empty catalogue signed for 30 days, its `.sig`, and
   the `v1/p` and `v1/i` folders. It reads the descriptor and the signed
   catalogue back before it reports success, and it prints the descriptor's
   path and the key's fingerprint.
3. Pack each package with `oa-tool pack`, and check it with
   `oa-tool check --json`, as [packages.md](packages.md) describes. Copy the
   zip to `v1/p/<sha256>.<kind>` and any picture to `v1/i/<sha256>.png`.
4. Add one catalogue entry per package, as [catalogue.md](catalogue.md)
   describes. Use the `sha256`, `size` and kind facts from `check`. Advance
   `sequence` by at least one when the bytes change. Set `generated` and
   `expires`. An expiry about 30 days ahead matches what `registry init`
   writes; OA still accepts any expiry later than `generated`, and it still
   uses a catalogue that has passed `expires`.
5. `oa-tool catalogue sign --key KEY_FILE catalogue.json` writes the
   detached signature over those exact bytes. Do not reformat the catalogue
   after signing.
6. Serve the folder at `--base-url` over plain HTTP. The catalogue URL in
   the descriptor MUST be the URL of `catalogue.json` on that host.

The publish script in the open-annihilation/content-library repository is
one way to automate steps 3 to 5. A registry does not have to use it.

A registry that wants keys instead of direct fetches sets `downloads.mode`
to `tokens`, names `api`, and serves the [download API](download-api.md).
`registry init` itself makes a direct registry. The same catalogue, signature
and package paths apply.

## Caching

A registry SHOULD cache the catalogue for a short time and SHOULD treat
packages and pictures as immutable, because their names are the SHA-256 of
their bytes. Core Prime caches its catalogue for 60 seconds. OA does not
read those cache headers as a rule: it refreshes a catalogue older than 6
hours when it starts, and not more often than every 60 seconds from the
Library, and it still uses a catalogue past `expires`.

A package URL handed out by the download API is valid until the key's
`expires`, and SHOULD NOT be cached past that.

## What a registry must not do

A registry MUST NOT serve the original game's data. OA does not check for
it. A registry that uses the download API MUST NOT record the network
address a request came from.
