// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The built-in registries whose keys the engine pins. F21's key goes there.
// A row is the registry's id, the key's id and the key's ed25519 text. An
// id with no row is trusted as its file stands.

inline constexpr std::array<PinnedKey, 0> pinned_key_table{};
