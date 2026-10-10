# Descriptor

A descriptor says where a catalogue lives, which keys sign it, and how its
packages are downloaded. The same text is a `registry.yaml` on the server
and an `.oareg` file a player opens. OA reads at most 64 KiB. The text is
UTF-8 YAML without a byte-order mark. A key named twice is refused, and a
key this version does not know is refused. Strings may be plain, single
quoted or double quoted; OA's own writer double-quotes every string.

[examples/registry.yaml](examples/registry.yaml) is a signed direct
registry. [examples/example.oareg](examples/example.oareg) is the same kind
of file for a registry that uses the download API. The schema is
[schemas/descriptor.schema.json](schemas/descriptor.schema.json).

## Keys

| Key | Required | Rule |
|---|---|---|
| `registry` | yes | The whole number `1`. |
| `id` | yes | Kebab-case, 1 to 32 bytes: words of `a-z` and `0-9` joined by single hyphens, not starting or ending with a hyphen. |
| `name` | yes | 1 to 64 bytes of UTF-8, with no control character (C0, DEL, or U+007F to U+009F) and no byte that is not UTF-8. |
| `homepage` | no | An `http` or `https` address, opened only in a browser. |
| `catalogue` | yes | An `http` address with no query. The catalogue file. |
| `mirrors` | no | At most 8 other copies of the catalogue. Each is `http` with no query. Absent reads as none. |
| `keys` | no | At most 8 signing keys. Absent or empty means the registry is unsigned. |
| `downloads` | yes | How packages are fetched. See below. |

`downloads` is a mapping. `mode` is required and is `direct` or `tokens`.

- `direct` refuses `api`. It refuses `install-id: required`. When
  `install-id` is absent it is `none`. A direct registry is fetched from
  the catalogue's own file address and checked by SHA-256.
- `tokens` requires `api`, an `http` address with no query. When
  `install-id` is absent it is `required`. Packages are fetched through
  the [download API](download-api.md).

`install-id` is `required` or `none`.

## Addresses

An address OA reads is 1 to 2048 bytes of printable ASCII. The scheme is
`http` or `https` in any case, followed by `://`. No user name or password,
and no IPv6 literal. The host is lower-cased and loses one trailing dot. A
fragment is dropped. `.` and `..` path segments are removed.

A catalogue, a mirror and a download API MUST be `http` and MUST NOT have a
query. OA fetches them over plain HTTP and checks what it gets. `https` is
refused for those. A homepage, the page where a descriptor was added, and a
challenge page MAY be `http` or `https`, and MAY have a query.

The address a player is shown for a registry is the catalogue's host, and
`:port` when the port is not 80.

## Signing keys

Each entry of `keys` has `id` and `public`, and nothing else.

`id` is 1 to 64 bytes. The first byte is `a-z` or `0-9`. Each byte after it
is `a-z`, `0-9`, `.` or `-`.

`public` is `ed25519:` and 44 characters of strict base64, 52 characters in
all. That is one Ed25519 public key.

Within one descriptor, key ids are unique and the 32 public-key bytes are
unique. The same key MAY appear in two different registries.

The fingerprint a player compares is the first 8 bytes of the SHA-256 of
those 32 bytes, as four groups of four lower-case hex digits separated by a
space, a middle dot and a space. For the example key `rfc8032-test-1` it is
`21fe · 31df · a154 · a261`. That key is a published test vector. It MUST
NOT sign a real catalogue. See [signatures.md](signatures.md).

## Adding an `.oareg`

OA checks a descriptor a player adds, in this order, and stops at the first
refusal. Built-in registries are the ones that ship with the game.

1. The id is a built-in id, ignoring ASCII case.
2. The name is a built-in name, after trimming spaces and ignoring case.
3. A key is a built-in key: the same 32 bytes, whatever id it uses.
4. The host of the catalogue, a mirror, the API or the homepage is a
   built-in host, or a name under one. A name is under a host when they are
   equal, ignoring case and one trailing dot, or the name ends with `.` and
   that host. An IPv4 address matches only that same address.
5. The player has already added this id. This applies when OA is looking at
   the player's list.
6. The registry is unsigned and Developer mode is off.
7. The registry is unsigned and the host of the catalogue, a mirror or the
   API is not exactly `127.0.0.1`. The homepage is not part of this check.
8. The player already has 64 added registries.

A player MUST NOT add a mirror to a built-in registry. Only that registry's
signed catalogue can name one. A mirror a player adds to a registry they
added MUST be `http` with no query, and MUST NOT already be listed.

An unsigned registry is only for Developer mode, and only when every
catalogue, mirror and API host is exactly `127.0.0.1`.

## `Registries.yaml`

OA keeps registries a player has added, and built-in ids they have turned
off, in `Registries.yaml` in the player's Open Annihilation folder. An
added registry keeps a copy of its descriptor, so one added from a file
still works while offline. The file is at most 256 KiB. `registries` and
`disabled` are optional, and an absent one reads as empty. Nothing else is
allowed at the top. An unknown key is refused. The reader does not apply
the limit of 64 again; OA refuses the add that would pass it.

Each added registry requires `id`, `name`, `catalogue`, `trusted-keys`,
`downloads`, `added` and `enabled`. Optional, and empty when absent: `url`,
`homepage`, `mirrors`, `player-mirrors`. Every value follows the descriptor
rule for it. `trusted-keys` replaces the descriptor's `keys` and may be
empty only for an unsigned registry. `url` is where the descriptor was
read, `http` or `https`, and is absent when it came from a file.
`player-mirrors` are mirrors this player added. `added` is the day it was
added, `YYYY-MM-DD`: month `01` to `12`, day `01` to `31`. OA does not check
that the day occurs in that month. `enabled` is `true` or `false`. Ids in
one file are unique. Each id in `disabled` is a registry id.

[examples/Registries.yaml](examples/Registries.yaml) is one added registry.
Its schema is
[schemas/registries-yaml.schema.json](schemas/registries-yaml.schema.json).
