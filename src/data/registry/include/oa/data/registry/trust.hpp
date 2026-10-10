// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Which registries a player may add, and which registries are in effect.
// A descriptor is refused when it would pass itself off as a registry that
// ships with the game, by id, name, key or host. An unsigned registry is
// only for Developer mode, and only on 127.0.0.1.
#pragma once

#include "oa/data/registry/player_registries.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::registry {

/// One key the engine pins for a built-in registry.
struct PinnedKey {
    std::string_view registry{};   ///< the registry's id
    std::string_view key_id{};     ///< the key's id
    std::string_view public_key{}; ///< the key's ed25519 text
};

/// The keys the engine pins for built-in registries.
///
/// A built-in registry whose id is here is used only when its file names
/// exactly these keys. An id with no row is trusted as its file stands.
///
/// @return the pinned keys; empty until a built-in registry's key is pinned
[[nodiscard]] std::span<const PinnedKey> pinned_keys() noexcept;

/// Reads the built-in registry descriptors from a folder.
///
/// Every .yaml file in the folder, in name order, and not in folders inside
/// it. A file is skipped, with a problem, when it cannot be read, when it is
/// unsigned, when its id was already read, or when its id has pinned keys
/// and the file's keys are not those. A registry whose id has no pinned
/// keys is trusted as its file stands. A file that is not .yaml is ignored.
///
/// @param folder the folder of built-in descriptors
/// @param[out] problems one sentence for each skipped file; may be null
/// @param pinned the pinned keys; the compiled table when the caller passes none
/// @return the descriptors that were kept, in name order
[[nodiscard]] std::vector<Descriptor> read_builtin_registries(
    const std::filesystem::path& folder,
    std::vector<std::string>* problems,
    std::span<const PinnedKey> pinned = pinned_keys()
);

/// Why a registry, a mirror or a key change was refused. none when it was made.
enum class Refusal : uint8_t {
    none,                          ///< accepted
    unreadable,                    ///< the descriptor or the change is not valid
    built_in_id,                   ///< the id is a built-in registry's
    built_in_name,                 ///< the name is a built-in registry's
    built_in_key,                  ///< a key is a built-in registry's key
    built_in_host,                 ///< an address uses a host of a built-in registry
    already_added,                 ///< the player already added this id
    unsigned_needs_developer_mode, ///< an unsigned registry needs Developer mode
    unsigned_needs_loopback,       ///< an unsigned registry's hosts must be 127.0.0.1
    too_many_registries,           ///< the player already has max_added_registries
    not_added,                     ///< no added registry has this id
    built_in_mirror,               ///< a mirror cannot be added to a built-in registry
    bad_mirror,                    ///< the mirror is not an http address with no query
    mirror_listed,                 ///< the registry already lists this mirror
    no_keys,                       ///< the trusted keys would be empty
};

/// The sentence a log or the downloads screen shows for a refusal.
///
/// @param refusal the refusal
/// @return one English sentence; never null
[[nodiscard]] const char* refusal_text(Refusal refusal) noexcept;

/// What a new registry is checked against.
struct AddContext {
    std::span<const Descriptor> built_ins{}; ///< the built-in registries
    const PlayerRegistries* player =
        nullptr; ///< the player's registries; null when they are not part of the check
    bool developer_mode = false; ///< true when Developer mode is on
};

/// Reports whether a descriptor may be added.
///
/// Checked in order: a built-in id (ASCII case ignored), a built-in name
/// (case ignored, after trimming spaces), a built-in key (the same 32
/// bytes, whatever the id), a host of the catalogue, mirrors, API or
/// homepage that is or lies under a built-in registry's host, an id already
/// added, an unsigned registry while Developer mode is off, an unsigned
/// registry whose catalogue, mirror or API host is not exactly 127.0.0.1,
/// and a player who already has max_added_registries. A homepage is not
/// part of the loopback check. Rules that need the player's list apply
/// when player is set.
///
/// @param descriptor the descriptor to add
/// @param context the built-in registries, the player's list and Developer mode
/// @return none when it may be added, or the first rule it breaks
[[nodiscard]] Refusal check_new_registry(const Descriptor& descriptor, const AddContext& context);

/// Reports whether a player may add a mirror to a registry.
///
/// Refused for a built-in registry, for an id the player has not added,
/// when the address is not http or has a query, and when the registry
/// already lists it.
///
/// @param id the registry's id
/// @param url the mirror
/// @param context the built-in registries and the player's list
/// @return none when the mirror may be added, or why it may not
[[nodiscard]] Refusal
check_new_mirror(std::string_view id, const formats::url::Url& url, const AddContext& context);

/// Reports whether every catalogue, mirror and API host is exactly 127.0.0.1.
///
/// A homepage is only a link and is not checked. A registry with no API
/// checks its catalogue and its mirrors.
///
/// @param descriptor the descriptor
/// @return true when every one of those hosts is exactly 127.0.0.1
[[nodiscard]] bool loopback_only(const Descriptor& descriptor) noexcept;

/// Where a registry that is in effect comes from.
enum class Origin : uint8_t {
    built_in, ///< shipped with the game
    added,    ///< added by the player
};

/// One registry the game would use, or would show as conflicting.
struct RegistryInEffect {
    Descriptor descriptor{};          ///< the descriptor
    Origin origin = Origin::built_in; ///< built-in or added
    bool enabled = true;              ///< false when the player has turned it off
    std::vector<formats::url::Url>
        player_mirrors{};     ///< mirrors the player added; empty for a built-in registry
    bool conflicting = false; ///< an added registry whose id a built-in registry has taken
    std::string url{};        ///< where an added descriptor was read; empty for a built-in registry
};

/// The registries in effect: built-in ones first, in their order, then the
/// player's, in file order.
///
/// A built-in registry is off when its id is in the disabled list. An added
/// registry whose id a built-in registry has, ASCII case ignored, is marked
/// conflicting and is never used; the built-in registry wins.
///
/// @param built_ins the built-in registries, in their order
/// @param player the player's registries
/// @return the registries in effect
[[nodiscard]] std::vector<RegistryInEffect>
registries_in_effect(std::span<const Descriptor> built_ins, const PlayerRegistries& player);

/// Adds a registry to the player's list.
///
/// The day is YYYY-MM-DD. The url is empty when the descriptor came from a
/// file, and otherwise an http or https address. Refused, and the list left
/// unchanged, when the descriptor or the day cannot be stored or when
/// check_new_registry refuses it. The player's list checked is registries.
///
/// @param[in,out] registries the player's registries
/// @param descriptor the descriptor to add
/// @param url where the descriptor was read; empty when it came from a file
/// @param date the day it was added, YYYY-MM-DD
/// @param context the built-in registries and Developer mode
/// @return none when it was added, or why it was not
[[nodiscard]] Refusal add_registry(
    PlayerRegistries& registries,
    const Descriptor& descriptor,
    std::string url,
    std::string date,
    const AddContext& context
);

/// Removes an added registry.
///
/// @param[in,out] registries the player's registries
/// @param id the registry's id
/// @return none when it was removed, or not_added
[[nodiscard]] Refusal remove_registry(PlayerRegistries& registries, std::string_view id);

/// Turns a registry on or off.
///
/// A built-in id goes in or out of the disabled list. Any other id sets the
/// added registry's enabled flag. A conflicting added registry is not what
/// this changes when the id is a built-in one: the built-in registry wins.
///
/// @param[in,out] registries the player's registries
/// @param built_ins the built-in registries
/// @param id the registry's id
/// @param on true to turn it on, false to turn it off
/// @return none when the flag was set, or not_added
[[nodiscard]] Refusal set_enabled(
    PlayerRegistries& registries,
    std::span<const Descriptor> built_ins,
    std::string_view id,
    bool on
);

/// Adds a mirror the player chose for an added registry.
///
/// Refused, and the list left unchanged, when check_new_mirror refuses it.
/// The player's list checked is registries.
///
/// @param[in,out] registries the player's registries
/// @param id the registry's id
/// @param url the mirror
/// @param context the built-in registries
/// @return none when the mirror was added, or why it was not
[[nodiscard]] Refusal add_player_mirror(
    PlayerRegistries& registries,
    std::string_view id,
    const formats::url::Url& url,
    const AddContext& context
);

/// Replaces the trusted keys of an added registry.
///
/// Refused for an empty list, for an id that is not added, and for keys
/// that do not meet the descriptor's rules. The list is left unchanged
/// when refused.
///
/// @param[in,out] registries the player's registries
/// @param id the registry's id
/// @param keys the keys the player now trusts
/// @return none when the keys were replaced, or why they were not
[[nodiscard]] Refusal replace_trusted_keys(
    PlayerRegistries& registries, std::string_view id, std::vector<RegistryKey> keys
);

} // namespace oa::data::registry
