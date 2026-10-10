# Catalogue signatures

Ed25519, as RFC 8032 defines it, so the engine can check that a catalogue
it downloads is the one the maintainers signed. A player who adds a
registry is shown a key's fingerprint before trusting that registry.
The same library seals a publisher's Ed25519 seed into a key file: the
seed is encrypted, and the key id and the public key stay in the clear
where a catalogue signature can name them. Target `oa-base-signing`,
headers `oa/base/signing/ed25519.hpp` and `oa/base/signing/sealed_key.hpp`,
namespace `oa::base::signing`.

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
- `seal_key` writes a sealed key file from a seed, a passphrase, a salt, a
  nonce and `SealParameters`. The caller's seed is left as it was. The
  output is unchanged, and its size is zero, when the status is not ok.
- `read_sealed_key_info` reads the id, the public key and the parameters.
  It does not take a passphrase and it does not check the tag.
- `unseal_key` opens the seed and checks that it is the recorded public key.
- `seal_status_text` is the status as a short sentence. An unknown status
  is an empty view.

`SealParameters` defaults to 262,144 KiB (256 MiB), 3 passes and 1 lane.
`SealedKeyInfo::id` holds the id, and `id_size` says how many of its bytes
are the id. A shorter id is followed by zeros. An id of 64 bytes fills the
array and has no terminating zero.

## State

None. A call keeps nothing for the next one. Argon2id's work area is
allocated for that call, aligned, wiped and released before the call
returns. A derived key, and every copy of a seed or a secret key the
call made, is wiped the same way.

## Invariants

The signatures are standard Ed25519 with SHA-512. A seed this library is
handed a copy of is wiped before the function that copied it returns.
`verify` returns false when a signature does not match, including for an
empty message and for a message of one byte, and it does not throw.

A sealed key file is version 1. Integers are little-endian. The file is
exactly 144 + *n* bytes, where *n* is the key id's length, 1 to 64.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `4F 41 53 4B 45 59 00 1A` |
| 8 | 1 | version, 1 |
| 9 | 1 | key id length *n* |
| 10 | 2 | zero |
| 12 | 4 | Argon2id memory in KiB |
| 16 | 4 | Argon2id passes |
| 20 | 4 | Argon2id lanes |
| 24 | 16 | salt |
| 40 | 24 | nonce |
| 64 | 32 | Ed25519 public key |
| 96 | *n* | key id |
| 96 + *n* | 32 | the seed, encrypted |
| 128 + *n* | 16 | Poly1305 tag |

The encryption key is Argon2id of the passphrase and the salt, to 32
bytes, with those three parameters and with no secret or associated data
of Argon2's own. The AEAD is XChaCha20-Poly1305. Its associated data is
the bytes before the ciphertext, so the id, the public key and the
parameters cannot be changed without the passphrase. After the tag
matches, the seed must produce the public key the file records. A failed
tag and a seed that does not are both `wrong_passphrase`.

A file is read only when its memory is 8 to 1,048,576 KiB, its passes are
1 to 10 and its lanes are 1 to 4. Memory must also be at least eight times
the lane count: fewer blocks than that is not a parameter Argon2id can
run, and the file is `bad_parameters` before any work area is allocated.
Lanes are checked before that product, so the product cannot wrap.

The bytes are read in this order. A short file is `truncated` until the
field that distinguishes it is present. A bad magic, or a file longer
than 144 + *n*, is `not_sealed_key`. A version other than 1 is
`unknown_version`. A reserved field that is not zero, or parameters
outside the range above, is `bad_parameters`. An id length of 0 or more
than 64, or an id the registry's key rule refuses, is `bad_id`.
`read_sealed_key_info` stops there. `unseal_key` then derives the key.
A work area that cannot be allocated is `out_of_memory`.

The key id rule is `oa::data::registry::valid_key_id`. This library
declares that function and does not link `oa-data-registry`: the registry
already links signing, and a base library does not link data. A program
that seals or opens a key, including the sealed-key test, links the
registry after this library so the call resolves.

No function in this library throws.

## Tests

`base-signing` checks the RFC 8032 section 7.1 vectors TEST 1, TEST 2,
TEST 3 and TEST 1024, a flipped bit in the message, the signature and the
key, the text forms, the RFC 4648 base64 vectors, and a fingerprint
computed independently.

`base-signing-sealed-key` checks Argon2id against RFC 9106 section 5.3
and XChaCha20-Poly1305 against draft-irtf-cfrg-xchacha-03 Appendix A.3.1,
seals and opens a key, and pins the sealed bytes of one fixed test
pattern so a change of layout or of Argon2id fails the test. That pin is
a regression pin, not a key. A wrong passphrase, a changed header byte,
a changed ciphertext or tag, a truncated or overlong file, a bad magic,
version 2, parameters outside the range, and an id the key rule refuses
are each refused with their status. The info is read with no passphrase.

## Limitations

One key is stored in one file. This library does not rotate a key, and
it does not write a second signature. The file format is version 1.
The library does not open a file and does not ask for a passphrase: the
caller hands it bytes, and wipes the passphrase it still holds.
