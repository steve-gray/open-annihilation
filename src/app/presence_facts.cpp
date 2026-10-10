// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What this machine is playing, read for the presence it tells other machines.
#include "oa/app/presence_facts.hpp"

#include "oa/app/runtime.hpp"

#include "engine_settings_state.hpp"

#include "oa/data/mod_profile.hpp"
#include "oa/data/mod_profile/overrides.hpp"

#include <span>
#include <stdexcept>
#include <string>

namespace oa::app {

namespace {

namespace profiles = oa::data::mod_profile;

/// Resolves the plain baseline and returns its sim hash.
///
/// @return the hash
/// @throws std::runtime_error when the baseline does not resolve
oa::base::sha256::Digest baseline_digest() {
    const std::string text = profiles::base_game_profile_text();
    const auto resolved = profiles::resolve_profile(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
        profiles::base_game_id
    );
    if (!resolved.resolution)
        throw std::runtime_error("the plain baseline does not resolve");
    return resolved.resolution->profile.sim_hash;
}

} // namespace

PresenceFacts presence_facts(const Runtime& runtime) {
    PresenceFacts facts{};
    facts.developer_mode = runtime.developer_mode();
    const auto* profile = runtime.mod_profile();
    if (profile != nullptr)
        facts.sim_hash = profile->sim_hash;
    else if (runtime.engine_settings_ && runtime.engine_settings_->layered)
        facts.sim_hash = runtime.engine_settings_->base_sim_hash;
    else if (runtime.options_.mod_profile)
        facts.sim_hash = runtime.options_.mod_profile->sim_hash;
    else
        facts.sim_hash = baseline_digest();

    if (profile == nullptr) {
        facts.rules_differ_from_base = false;
    } else if (runtime.engine_settings_ == nullptr || !runtime.engine_settings_->layered) {
        facts.rules_differ_from_base = true;
    } else {
        facts.rules_differ_from_base = profile->sim_hash != runtime.engine_settings_->base_sim_hash;
    }

    if (profile != nullptr) {
        facts.mod =
            PresenceMod{profile->id, profile->name, profile->version, profile->packaging.revision};
    }

    const auto* state = runtime.engine_settings_ ? runtime.engine_settings_.get() : nullptr;
    if (state != nullptr) {
        const auto hacks = profiles::standard_hacks();
        const auto count =
            hacks.size() < state->profile_hacks.size() ? hacks.size() : state->profile_hacks.size();
        const auto overrides =
            std::span<const profiles::HackOverride>{state->current.hack_overrides};
        for (std::size_t index = 0; index < count; ++index) {
            const auto* hack = hacks[index];
            bool on = state->profile_hacks[index].on;
            if (facts.developer_mode) {
                if (const auto* found = profiles::find_override(overrides, hack->id))
                    on = found->on;
            }
            if (!on)
                continue;
            if (hack->scope == profiles::registry::Scope::sim)
                facts.game_hacks.emplace_back(hack->id);
            else if (hack->scope == profiles::registry::Scope::view)
                facts.view_hacks.emplace_back(hack->id);
        }
        if (facts.developer_mode) {
            facts.overrides.reserve(state->current.hack_overrides.size());
            for (const auto& override : state->current.hack_overrides)
                facts.overrides.push_back(PresenceOverride{override.hack, override.on});
        }
    }
    return facts;
}

} // namespace oa::app
