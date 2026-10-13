// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The player's skirmish rows survive a campaign mission's options save and a
// reload, a start that builds no match, and a skirmish played as slot 5.
#include "oa/ui/frontend_state/initialization.hpp"

#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace init = oa::ui::frontend_state::initialization;
namespace entry = oa::ui::frontend_state::game_entry;

namespace {
void require(bool held, const char* message) {
    if (!held)
        throw std::runtime_error(message);
}

struct Preferences final : init::PreferencesHost {
    std::map<std::string, uint32_t> numbers, written;
    std::map<std::string, std::string> strings;

    static std::string key(std::string_view section, std::string_view name) {
        return std::string(section) + ":" + std::string(name);
    }

    std::optional<uint32_t> read_number(std::string_view section, std::string_view name) override {
        const auto it = numbers.find(key(section, name));
        if (it == numbers.end())
            return {};
        return it->second;
    }

    void write_number(std::string_view section, std::string_view name, uint32_t value) override {
        written[key(section, name)] = value;
    }

    std::optional<std::string>
    read_string(std::string_view section, std::string_view name, std::size_t capacity) override {
        const auto it = strings.find(key(section, name));
        if (it == strings.end() || it->second.size() >= capacity)
            return {};
        return it->second;
    }

    void
    write_string(std::string_view section, std::string_view name, std::string_view value) override {
        strings[key(section, name)] = std::string(value);
    }

    void audio_mode(init::AudioMode) override {}

    void mixing_buffers(uint32_t) override {}

    void wave_volume(uint32_t) override {}

    void cd_volume(uint32_t) override {}

    uint32_t nickname_override_enabled() override { return 0; }

    std::string nickname_override() override { return {}; }

    std::string game_name_override() override { return {}; }

    std::optional<std::string> user_name() override { return {}; }

    std::string application_directory() override { return "root"; }

    std::string own_image_output_directory() override { return {}; }

    void select_map_list(int32_t) override {}

    void select_map_index(int32_t) override {}

    std::string selected_map_name() override { return "First Map"; }

    uint32_t mixing_buffer_count() override { return 8; }

    uint32_t wave_out_volume() override { return 0; }

    uint32_t cd_audio_volume() override { return 0; }

    uint8_t keep_stored_password() override { return 0; }

    void adopt_writes() {
        for (const auto& [stored_key, value] : written)
            numbers[stored_key] = value;
    }
};

/// Slot 1 Computer, slot 5 Player, ten rows. Slot numbers are the rows the
/// setup shows, starting at 1.
entry::SkirmishSettings player_rows() {
    entry::SkirmishSettings settings;
    settings.slot_count = 10;
    settings.map_name = "Painted Desert";
    for (int32_t row = 0; row < settings.slot_count; ++row) {
        auto& slot = settings.slots[static_cast<std::size_t>(row)];
        slot.controller = entry::controller::computer;
        slot.side = row % 2;
        slot.color = row;
        slot.alliance = entry::unassigned_alliance;
        slot.metal = 1000;
        slot.energy = 1000;
    }
    settings.slots[4].controller = entry::controller::human;
    return settings;
}

void store_count(Preferences& preferences, int32_t count) {
    preferences.numbers[Preferences::key(init::general_section, "NumSkirmishPlayers")] =
        static_cast<uint32_t>(count);
    preferences.strings[Preferences::key(init::general_section, "SkirmishMap")] = "Painted Desert";
}

int32_t players(const entry::SkirmishSettings& settings) {
    return entry::human_count(settings);
}

/// What a campaign mission writes into the live slots: the player on the
/// first row, the computer on the second, two rows in the match.
void seat_campaign(entry::SkirmishSettings& live) {
    live.slot_count = 2;
    for (auto& slot : live.slots)
        slot.controller = entry::controller::disabled;
    live.slots[0].controller = entry::controller::human;
    live.slots[0].side = 0;
    live.slots[0].alliance = 0;
    live.slots[0].color = 0;
    live.slots[1].controller = entry::controller::computer;
    live.slots[1].side = 1;
    live.slots[1].alliance = 1;
    live.slots[1].color = 1;
}

void campaign_options_save_and_reload() {
    const auto player = player_rows();
    auto live = player;
    std::optional<entry::SkirmishSettings> kept;
    init::remember_player_skirmish_settings(live, kept);
    seat_campaign(live);
    require(
        live.slot_count == 2 && live.slots[0].controller == entry::controller::human &&
            live.slots[1].controller == entry::controller::computer,
        "the campaign seats its own two players"
    );
    require(kept.has_value() && players(*kept) == 1, "the player's one Player is kept");

    oa::ui::frontend_state::State state;
    init::Preferences preferences;
    Preferences file;
    store_count(file, player.slot_count);
    // An options save during the mission writes the kept rows, not the
    // campaign's two players. NumSkirmishPlayers stays the player's count.
    init::save_preferences(state, kept ? *kept : live, preferences, file);
    require(
        !file.written.contains(Preferences::key(init::general_section, "NumSkirmishPlayers")),
        "the options save leaves the row count"
    );
    require(
        file.written.at(Preferences::key(init::skirmish_section, "Player0Controller")) ==
            static_cast<uint32_t>(entry::controller::computer),
        "slot 1 is saved as Computer"
    );
    require(
        file.written.at(Preferences::key(init::skirmish_section, "Player4Controller")) ==
            static_cast<uint32_t>(entry::controller::human),
        "slot 5 is saved as Player"
    );
    file.adopt_writes();

    // The mission ends: the kept rows come back, then the preferences load.
    init::restore_player_skirmish_settings(live, kept);
    require(!kept.has_value(), "the kept copy is put back");
    init::load_preferences(state, live, preferences, file);
    require(
        live.slot_count == 10 && live.slots[0].controller == entry::controller::computer &&
            live.slots[4].controller == entry::controller::human && players(live) == 1,
        "slot 5 Player and slot 1 Computer survive the mission and the reload"
    );
}

void failed_campaign_drops_the_copy() {
    auto live = player_rows();
    std::optional<entry::SkirmishSettings> kept;
    init::remember_player_skirmish_settings(live, kept);
    seat_campaign(live);
    // The start built no match.
    init::restore_player_skirmish_settings(live, kept);
    require(!kept.has_value(), "a start with no match drops the kept copy");
    require(
        live.slot_count == 10 && live.slots[0].controller == entry::controller::computer &&
            live.slots[4].controller == entry::controller::human && players(live) == 1,
        "the player's rows are current after the failed start"
    );
    init::remember_player_skirmish_settings(live, kept);
    require(players(*kept) == 1, "a later match remembers the restored rows");
}

void skirmish_as_slot_five_reloads() {
    const auto player = player_rows();
    oa::ui::frontend_state::State state;
    init::Preferences preferences;
    Preferences file;
    store_count(file, player.slot_count);
    init::save_preferences(state, player, preferences, file);
    file.adopt_writes();
    entry::SkirmishSettings loaded;
    init::load_preferences(state, loaded, preferences, file);
    require(
        loaded.slot_count == 10 && loaded.slots[0].controller == entry::controller::computer &&
            loaded.slots[4].controller == entry::controller::human && players(loaded) == 1,
        "a skirmish as slot 5 reloads as one Player"
    );
}
} // namespace

int main() {
    try {
        campaign_options_save_and_reload();
        failed_campaign_drops_the_copy();
        skirmish_as_slot_five_reloads();
        std::cout << "campaign skirmish slots passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
