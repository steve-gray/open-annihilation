// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What this machine is playing, for the presence it tells other machines:
// Developer Mode, the simulation hash, whether the rules differ from 3.1c,
// the mod, and which standard hacks are on. The header includes no other
// header of the engine's.
//
// Limitations: an override Developer Mode holds still counts, and still
// decides whether its hack is on, when the resolver refused it and the
// profile in play does not carry it.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oa::app {

class Runtime;

/// The mod in play, as its profile names it.
struct PresenceMod {
    std::string id{};             ///< the mod's stable id
    std::string name{};           ///< the mod's display name
    std::string version{};        ///< the mod's version
    int64_t packaging_revision{}; ///< how many times the package has been updated

    /// Compares two mods: every field.
    ///
    /// @param left one mod
    /// @param right the other
    /// @return true when every field is equal
    friend bool operator==(const PresenceMod& left, const PresenceMod& right) = default;
};

/// One Developer Mode override: the hack and whether the override turns it on.
struct PresenceOverride {
    std::string hack{}; ///< the hack's id
    bool on{};          ///< the override turns the hack on

    /// Compares two overrides: the hack and whether it is on.
    ///
    /// @param left one override
    /// @param right the other
    /// @return true when both fields are equal
    friend bool operator==(const PresenceOverride& left, const PresenceOverride& right) = default;
};

/// What this machine is playing.
struct PresenceFacts {
    /// Developer Mode is on (Runtime::developer_mode).
    bool developer_mode{};
    /// The simulation hash the game plays, as simulation_hash chooses it:
    /// the played profile's, or the plain baseline's when the game plays 3.1c.
    std::array<uint8_t, 32> sim_hash{};
    /// The played profile's simulation hash differs from the plain baseline's.
    /// With the settings not yet layered, any played profile counts as
    /// differing. False when the game plays no profile.
    bool rules_differ_from_base{};
    /// The mod in play: its id, name, version and packaging revision
    /// (ModProfile::id, name, version, packaging.revision). None when the
    /// game plays no profile.
    std::optional<PresenceMod> mod;
    /// Standard hacks of simulation scope that are on, in registry order.
    /// A hack is on when the played profile's shipped state has it on, or,
    /// while Developer Mode is on and an override names it, when that
    /// override is on.
    std::vector<std::string> game_hacks;
    /// Standard hacks of view scope that are on, in registry order, by the
    /// same rule as game_hacks.
    std::vector<std::string> view_hacks;
    /// Developer Mode's overrides, each hack's id and whether it is on
    /// (EngineSettings::hack_overrides). None while Developer Mode is off.
    std::vector<PresenceOverride> overrides;

    /// Compares two readings: every field.
    ///
    /// @param left one reading
    /// @param right the other
    /// @return true when every field is equal
    friend bool operator==(const PresenceFacts& left, const PresenceFacts& right) = default;
};

/// Reads what this machine is playing.
///
/// @param runtime the running app
/// @return the facts
/// @throws std::runtime_error when the plain baseline does not resolve and
///         the facts need its simulation hash
[[nodiscard]] PresenceFacts presence_facts(const Runtime& runtime);

} // namespace oa::app
