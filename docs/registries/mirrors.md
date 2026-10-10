# Mirrors

A mirror is another copy of a registry's descriptor, catalogue, signature,
pictures and packages. Players use it when the origin is hard to reach. A
mirror needs no keys of its own. OA checks the catalogue against the keys it
already trusts for that registry, and each package against the SHA-256 the
catalogue names.

## Copying one

`oa-tool registry mirror URL FOLDER` copies a registry into `FOLDER`, laid
out as on the host that served it. `URL` is the registry descriptor. The
catalogue's signature is checked, and a sequence lower than the one `FOLDER`
already holds stops the copy. Each package and picture is checked by
SHA-256. A file already there with the right SHA-256 is skipped, and a
`.part` is resumed with a Range request. A file that fails its check is
deleted. The catalogue and its signature are written last, byte for byte.

A direct origin is fetched from the catalogue's own addresses. A tokens
origin is copied through its download API: the tool asks for a key, prints a
check's code and address, and carries on once the check is passed. The
folder is served as a direct registry either way. The origin's download API
stays the origin's policy; the mirror does not have to run one.

Serve `FOLDER` over plain HTTP, as [running.md](running.md) serves a
registry made with `registry init`. The descriptor in the folder names the
origin's catalogue. To publish the mirror, the origin adds the mirror's
catalogue URL to its own descriptor, or the player adds it themselves.

## How a mirror is named

The descriptor's `mirrors` list, at most 8, holds other copies of the
catalogue. Each is an `http` address with no query. The catalogue may repeat
that list in its own `mirrors`. A player who added the registry can add
further mirrors, kept as `player-mirrors` in
[Registries.yaml](descriptor.md). Those follow the same address rule, and a
mirror already listed is refused.

A player MUST NOT add a mirror to a built-in registry. Only that registry's
signed catalogue can name one.

## When OA uses one

A direct download tries the catalogue's file address on the origin and on
the mirrors. A tokens download uses the key's URL. When the download API
cannot be reached, OA fetches those file addresses instead. Neither a 4xx
nor a 428 falls through to a mirror. A 404 or 409 on the key request means the
release is no longer offered; OA refreshes the catalogue rather than fetching
a mirror's copy of the old file. A 429 waits and asks again.
