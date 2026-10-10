// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The built-in registries whose keys the engine pins. A row is the
// registry's id, the key's id and the key's ed25519 text. An id with no
// row is trusted as its file stands.
//
// Core Prime's catalogue key, fingerprint 7001 · af50 · b971 · 5bb4, was
// made by the maintainer offline.

inline constexpr std::array<PinnedKey, 1> pinned_key_table{{
    {"coreprime", "steve-2026", "ed25519:dHXqUJM9LbifF+boPakeKsZoSD29qhTEAxkyRj+s5yU="},
}};
