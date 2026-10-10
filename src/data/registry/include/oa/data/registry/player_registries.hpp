// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The registries a player has added, and the built-in registries that player
// has turned off, kept in Documents/Open Annihilation/Registries.yaml. An
// added registry keeps a copy of its descriptor, so one added from a file
// and every one while offline still work.
#pragma once

#include "oa/data/registry/descriptor.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::registry {

/// The name of the player's registry file, inside the player's Open
/// Annihilation folder.
inline constexpr std::string_view player_registries_file = "Registries.yaml";

/// The most registries one player may add.
inline constexpr std::size_t max_added_registries = 64;

/// One registry the player added. descriptor.keys are the keys the player trusts.
struct AddedRegistry {
    Descriptor descriptor{}; ///< the descriptor, with the trusted keys
    std::string url{};       ///< where the descriptor was read; empty when it came from a file
    std::vector<formats::url::Url> player_mirrors{}; ///< mirrors this player added
    std::string added{};                             ///< the day it was added, YYYY-MM-DD
    bool enabled = true;                             ///< false when the player has turned it off
};

/// The player's registry file: the registries they added, and the built-in
/// ids they turned off.
struct PlayerRegistries {
    std::vector<AddedRegistry> registries{}; ///< the added registries, in file order
    std::vector<std::string> disabled{};     ///< built-in registry ids the player turned off
};

/// Reads a player's registry file from its bytes.
///
/// registries and disabled are optional, and an absent one reads as empty.
/// Nothing else is allowed at the top. Each added registry requires id,
/// name, catalogue, trusted-keys, downloads, added and enabled. url,
/// homepage, mirrors and player-mirrors are optional and read as empty when
/// absent. trusted-keys may be empty only for an unsigned registry. Every
/// value follows the descriptor's rule for it. Plain, single-quoted and
/// double-quoted scalars read alike. A key this build does not know is
/// refused.
///
/// @param bytes the file's bytes
/// @param[out] out the registries; left unchanged when the bytes are refused
/// @param[out] error why the bytes were refused; may be null
/// @return true when the file was read
[[nodiscard]] bool
read_player_registries(std::span<const uint8_t> bytes, PlayerRegistries& out, std::string* error);

/// Writes a player's registry file.
///
/// Every string is double-quoted. Reading the text back yields the same
/// values.
///
/// @param registries the registries
/// @return the file's text
[[nodiscard]] std::string player_registries_text(const PlayerRegistries& registries);

/// What a load of the player's registry file did.
enum class LoadResult : uint8_t {
    missing,    ///< there is no file; the registries are empty
    read,       ///< the file was read
    unreadable, ///< the file is there and was refused
};

/// Loads the player's registry file.
///
/// A missing file reads as empty. A file that cannot be read leaves out
/// unchanged.
///
/// @param file the file's path
/// @param[out] out the registries; empty when the file is missing, unchanged when it is refused
/// @param[out] error why the file was refused; may be null
/// @return missing, read or unreadable
[[nodiscard]] LoadResult load_player_registries(
    const std::filesystem::path& file, PlayerRegistries& out, std::string* error
);

/// Replaces the player's registry file with these registries.
///
/// A file that cannot be read is left as it was and nothing is written.
/// The write goes through the platform's file replace, so a failure leaves
/// the previous file in place.
///
/// @param file the file's path
/// @param registries the registries to store
/// @param[out] error why the file was not replaced; may be null
/// @return true when the file holds these registries
[[nodiscard]] bool save_player_registries(
    const std::filesystem::path& file, const PlayerRegistries& registries, std::string* error
);

} // namespace oa::data::registry
