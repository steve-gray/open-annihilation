# Random bytes

The system's generator of random bytes, for a value the player must not be
able to guess. Target `oa-platform-random`, namespace
`oa::platform::random`. It has no SDL.

## Entry points

`oa/platform/random.hpp`:

- `fill_random` fills a caller-owned buffer from the system's generator.
  On Windows that is the generator shipped since Windows XP, and on
  Windows 95 and 98 the CryptoAPI generator those versions have. Elsewhere
  it reads `/dev/urandom` until the buffer is full. An empty buffer is
  already full. The call returns false when the generator cannot be read,
  and the buffer is then not a result: a short read fails rather than
  standing in for the missing bytes.

## State

The module reads the system's generator and writes only the buffer the
caller passes. It keeps nothing of its own.

## Invariants

A successful call has filled every byte. A failed call yields nothing a
caller may keep. The bytes come from the system's generator alone.

## Tests

`platform-random` fills 0, 1, 16 and 4096 bytes, checks that two 16-byte
fills differ, and checks that a 4096-byte fill is not all one value.

## Limitations

The caller cannot choose the bytes, and the module has no second generator.
