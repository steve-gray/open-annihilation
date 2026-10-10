# Catalogue

A catalogue lists the packages a registry offers. OA reads the file whole,
at most 16 MiB, as UTF-8 JSON without a byte-order mark (RFC 8259), nested
at most 64 objects and arrays deep. `catalogue` MUST be `1`. A member this
version does not know is ignored. A member named twice in one object,
anywhere in the tree, refuses the whole catalogue. One package that breaks
a rule is left out, the reason is kept for the first 100 such packages, and
the rest stay. Two packages with the same key, ignoring case, are both left
out.

[examples/catalogue.json](examples/catalogue.json) has one entry of each
kind. Its schema is
[schemas/catalogue.schema.json](schemas/catalogue.schema.json). The bytes of
that file are what [examples/catalogue.json.sig](examples/catalogue.json.sig)
signs.

## Top level

| Member | Required | Rule |
|---|---|---|
| `catalogue` | yes | The whole number `1`. |
| `registry` | yes | The registry id, as in the [descriptor](descriptor.md). |
| `sequence` | yes | A whole number, at least 0. It never goes backwards. |
| `generated` | yes | UTC time. `expires` MUST be later. |
| `expires` | yes | UTC time, after `generated`. |
| `downloads` | no | An `http` address. A query is allowed here. Informational. |
| `keys` | no | At most 8 current signing keys. See [signatures.md](signatures.md). |
| `mirrors` | no | At most 8 `http` addresses with no query. |
| `packages` | yes | The entries. At most 20,000. An empty array is a catalogue. |

A UTC time is `YYYY-MM-DDTHH:MM:SSZ`. A fraction of a second may follow the
seconds, a dot and one or more digits; it is accepted and does not change
the second. The date is a real Gregorian day, 29 February included in a
leap year. The hour is 0 to 23 and the minute and second are 0 to 59.

A whole number is a JSON number with no fraction and no exponent, inside
64-bit signed range. `1` is a whole number. `1.0` is not, except for
`coverage`, which is read from the spelling of the number.

`keys` uses the descriptor's key rules: `id`, `public` as `ed25519:` and 44
characters of base64, ids unique, public keys unique, at most 8. A member of
a key object other than `id` and `public` is ignored. An empty list is not
a rotation. See [signatures.md](signatures.md).

`downloads` on a catalogue is any `http` address the URL reader accepts,
including one with a query. The descriptor's `catalogue`, `mirrors` and
`api` still forbid a query.

## Every package

| Member | Required | Rule |
|---|---|---|
| `kind` | yes | `oamod`, `oamap` or `oalang`. |
| `id` | yes | The package key. No key contains `@`. |
| `name` | yes | 1 to 64 bytes. |
| `release` | yes | A whole number from 1 to 2147483647. |
| `size` | yes | A whole number of bytes from 1 to 4294967296. |
| `sha256` | yes | 64 lower-case hex digits. |
| `file` | yes | A catalogue reference whose last segment is `<sha256>.<kind>`. |

A mod or map-pack key is lower-case kebab-case of 1 to 64 bytes. A language
key is a language tag of 2 to 35 bytes: a first part of letters (`A-Z` or
`a-z`), then hyphen-joined parts of letters and digits.

A reference is absolute from the host root (`/v1/p/<name>`) or relative to
the catalogue's folder. It is at most 512 bytes. It has no scheme, no `//`,
no `.` or `..` segment, no empty segment, and no `\`, `%`, `?`, `#`, `:`,
space, control character or byte above 0x7E. OA resolves it against the host
that served the catalogue. The last segment of `file` MUST be the lower-case
hex digest, a dot, and the kind. A registry SHOULD put the file at
`/v1/p/<sha256>.<kind>`.

| Member | Required | Rule |
|---|---|---|
| `version` | mods only | 1 to 32 bytes. Optional for a map pack or a language pack. |
| `revision` | mods only | A whole number from 1 to 65535. If any other kind names it, the same rule applies. |
| `publisher`, `author` | no | 1 to 64 bytes when present. |
| `summary` | no | 0 to 200 bytes. |
| `notes` | no | 0 to 2000 bytes. |
| `homepage` | no | 1 to 256 bytes, `http://` or `https://` in either case, at least one byte after the scheme, and no ASCII control, delete or space. |
| `tags` | no | At most 16. Each is lower-case kebab-case of 1 to 32 bytes. |
| `badge` | no | A reference ending `.png`. |
| `requires` | no | An object. `engine` is a range, below. `base` is any string and is copied as written. Other members are ignored. |
| `sim_hash` | no | 64 hex digits, either case. |
| `hacks` | no | `sim` and `view` are whole numbers from 0 to 2147483647. `ids` is at most 256 hack ids, each 1 to 64 bytes of `a-z`, `0-9`, `.` and `-`. Other members are ignored. |

`requires.engine` is 1 to 4 comparisons in at most 64 bytes, separated by
commas that spaces may surround. Each comparison is `>=`, `>`, `<=`, `<` or
`=`, optional spaces, then `MAJOR.MINOR.PATCH`. Each part is 0 to 65535
with no leading zero; a lone `0` is a part. In YAML a value that starts
with `>` is a block scalar unless it is quoted. In the catalogue, which is
JSON, quote it as a string anyway.

A picture reference, `badge` or a map `preview`, MUST be a valid reference
ending `.png`. OA does not require the file name to be a hash. A registry
SHOULD still serve pictures at `/v1/i/<sha256>.png`.

## Mods

A mod (`oamod`) MUST name `version` and `revision`. The manifest inside the
package is described by the
[OAMOD standard](../mods/oamod-standard.md).

## Map packs

A map pack (`oamap`) MUST name `maps`, a non-empty array. Each map is an
object:

| Member | Required | Rule |
|---|---|---|
| `map` | yes | `<stem>@<package id>` with exactly one `@`, a non-empty stem, and a suffix that is exactly this package's id. |
| `name` | yes | 1 to 64 bytes. |
| `players` | yes | A whole number from 1 to 10. |
| `size` | yes | Width `x` height. Each side is 1 to 256, with no leading zero. |
| `preview` | no | A picture reference ending `.png`. |
| `compatible` | no | An object of names to booleans, in the order written. Each name is 1 to 256 bytes. |

`size` is the text `16x16`, not an object. A name in `compatible` is a free
name such as `ta-3.1c`, the base the map was published against, not a
package key. `true` means that target fits. The manifest inside the package
is described by the [OAMAP standard](../maps/oamap-standard.md).

## Language packs

Only a language pack is read for these members. On any other kind they are
ignored.

| Member | Required | Rule |
|---|---|---|
| `coverage` | no | A JSON number from 0 to 1, at most 18 significant digits. `1` and `1.0` are both accepted. |
| `english_name` | no | 1 to 64 bytes. |
| `word` | no | 1 to 32 bytes. |
| `locales` | no | At most 16 language tags. |
| `fallbacks` | no | At most 8 language tags. |
| `needs` | no | `game-fonts` or `modern-fonts`. |

The manifest inside the package is described by
[languages.md](../languages.md).

## Sequence, expiry and how OA keeps a catalogue

For a registry that names a key, OA verifies the detached signature over the
exact bytes before it parses them. An unsigned registry skips that. The
version, the registry id and the sequence come next.

A lower sequence than the one OA already kept is refused. The same sequence
and the same bytes are unchanged. The same sequence and different bytes are
refused. A higher sequence replaces the catalogue.

A catalogue whose `expires` is at or before the clock OA checks with is
still used. OA marks it expired. It does not refuse some other expiry, and
it does not require a 30-day lifetime. `oa-tool registry init` signs an
empty catalogue for 30 days; that is a publisher's habit, and a registry
SHOULD set `expires` about 30 days after `generated`.

OA reads catalogues again when the one it holds is older than 6 hours at
start, and not more often than every 60 seconds from the Library. That is
the client's habit, not a field of the catalogue.

An added registry whose `keys` are present, not empty, and different from
the keys the player trusts reports the new set, so the player can accept a
rotation. Comparison is the id and the 32 bytes, in any order. A built-in
registry's keys in the catalogue are ignored. An empty key list is never a
rotation.
