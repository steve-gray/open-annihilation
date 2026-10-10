# The OA Registry Specification

A registry is a catalogue of packages Open Annihilation can install, served
over plain HTTP, with a detached signature so the catalogue can be trusted
without TLS. This specification, `registry: 1`, is everything a registry
serves. Anyone can run one that OA trusts without reading the engine.

The key words MUST, MUST NOT, SHOULD and SHOULD NOT in these documents are
to be interpreted as described in RFC 2119.

## Version

`registry` in the descriptor, and `catalogue` in the catalogue, are `1`.
A later version adds members or documents. It does not change the meaning of
a member this version already defines. OA refuses a descriptor whose
`registry` is not `1`, and a catalogue whose `catalogue` is not `1`.

## Documents

- [descriptor.md](descriptor.md): the descriptor, the `.oareg` file, and
  `Registries.yaml`.
- [catalogue.md](catalogue.md): the catalogue OA reads.
- [signatures.md](signatures.md): the detached signature, keys and rotation.
- [packages.md](packages.md): the three package kinds, how they are addressed,
  and what OA checks before it installs one.
- [download-api.md](download-api.md): direct downloads and the optional
  download API.
- [running.md](running.md): putting a registry on a static HTTP server.
- [mirrors.md](mirrors.md): copies of a registry.

The schemas under [schemas/](schemas) are JSON Schema draft 2020-12, one for
each document. A schema describes the data a file holds, not the YAML or JSON
syntax around it. Where a reader ignores a member it does not know, the text
says so: the schema lists the members of version 1, and a catalogue or an API
body may carry a further member that this version does not read. A descriptor
and `Registries.yaml` are different: an unknown key is refused.

The files under [examples/](examples) are accepted by the readers this
version ships with. The catalogue's signature is the worked example in
[signatures.md](signatures.md).

## Conformance

A registry conforms to version 1 when its descriptor, its catalogue, its
signature and, if `downloads.mode` is `tokens`, its download API follow
these documents. A package file conforms when it follows
[packages.md](packages.md) and the standard for its kind.

OA fetches catalogues and packages over plain HTTP. It checks a signed
catalogue against the keys it already trusts, and a package against the
SHA-256 the catalogue names. HTTPS is for a page a browser opens, not for a
fetch.
