# Catalogue signatures

Ed25519, as RFC 8032 defines it, so the engine can check that a catalogue
it downloads is the one the maintainers signed. A player who adds a
registry is shown a key's fingerprint before trusting that registry.
Target `oa-base-signing`, header `oa/base/signing/ed25519.hpp`, namespace
`oa::base::signing`.

## Entry points

- `key_pair_from_seed` turns a 32-byte seed into a secret key and its public
  key.
- `sign` signs the bytes of a message with a secret key.
- `verify` reports whether a signature matches a message and a public key.
- `public_key_text` and `parse_public_key` write and read `ed25519:` followed
  by the key in base64.
- `signature_text` and `parse_signature` write and read a signature in base64.
- `fingerprint` writes the first eight bytes of the public key's SHA-256 as
  four groups of four lower-case hexadecimal digits, separated by a space, a
  middle dot and a space.

## State

None. A call keeps nothing for the next one.

## Invariants

The signatures are standard Ed25519 with SHA-512. A seed this library is
handed a copy of is wiped before the function that copied it returns.
`verify` returns false when a signature does not match, including for an
empty message and for a message of one byte, and it does not throw.

## Tests

`base-signing` checks the RFC 8032 section 7.1 vectors TEST 1, TEST 2,
TEST 3 and TEST 1024, a flipped bit in the message, the signature and the
key, the text forms, the RFC 4648 base64 vectors, and a fingerprint
computed independently.

## Limitations

The library does not store a key and does not ask for a passphrase. Sealed
key files are part of the publisher's tools, not of this library.
