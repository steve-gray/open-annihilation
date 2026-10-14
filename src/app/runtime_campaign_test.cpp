// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The player's skirmish rows survive a campaign mission's options save and a
// reload, a start that builds no match, and closing and starting again after
// a skirmish played as slot 5.
#include "oa/ui/frontend_state/initialization.hpp"
#include "oa/ui/frontend_state/skirmish_ui.hpp"

#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace init = oa::ui::frontend_state::initialization;
namespace entry = oa::ui::frontend_state::game_entry;
namespace skirmish_ui = oa::ui::frontend_state::skirmish_ui;

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
    // campaign's two players, and writes the player's row count with them.
    init::save_preferences(state, kept ? *kept : live, preferences, file);
    require(
        file.written.at(Preferences::key(init::general_section, "NumSkirmishPlayers")) ==
            static_cast<uint32_t>(player.slot_count),
        "the options save writes the player's row count"
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

/// Slot 1 Computer with a side, colour and stores of its own, slot 5 Player,
/// ten rows. Slot numbers are the rows the setup shows, starting at 1.
void seed_slot_five(Preferences& file) {
    store_count(file, 10);
    const auto put = [&](int32_t row,
                         uint32_t controller,
                         uint32_t side,
                         uint32_t color,
                         uint32_t metal,
                         uint32_t energy) {
        const auto player = "Player" + std::to_string(row);
        const auto field = [&](std::string_view suffix, uint32_t value) {
            file.numbers[Preferences::key(init::skirmish_section, player + std::string(suffix))] =
                value;
        };
        field("Controller", controller);
        field("Side", side);
        field("Color", color);
        field("AllyGroup", static_cast<uint32_t>(entry::unassigned_alliance));
        field("Metal", metal);
        field("Energy", energy);
    };
    for (int32_t row = 0; row < 10; ++row)
        put(row,
            static_cast<uint32_t>(entry::controller::computer),
            static_cast<uint32_t>(row % 2),
            static_cast<uint32_t>(row),
            1000,
            1000);
    put(0, static_cast<uint32_t>(entry::controller::computer), 1, 6, 2500, 4500);
    put(4, static_cast<uint32_t>(entry::controller::human), 0, 4, 1000, 1000);
}

uint32_t stored(const Preferences& file, std::string_view section, std::string_view name) {
    const auto key = Preferences::key(section, name);
    require(file.numbers.contains(key), "a stored value is missing");
    return file.numbers.at(key);
}

uint32_t saved(const Preferences& file, std::string_view section, std::string_view name) {
    const auto key = Preferences::key(section, name);
    require(file.written.contains(key), "a save left a value unwritten");
    return file.written.at(key);
}

void expect_slot_five(const entry::SkirmishSettings& settings) {
    require(settings.slot_count == 10 && players(settings) == 1, "ten rows and one Player");
    const auto& first = settings.slots[0];
    require(
        first.controller == entry::controller::computer && first.side == 1 && first.color == 6 &&
            first.metal == 2500 && first.energy == 4500,
        "slot 1 is Computer with its side, colour and resources"
    );
    require(settings.slots[4].controller == entry::controller::human, "slot 5 is Player");
}

void expect_file_slot_five(const Preferences& file) {
    require(
        stored(file, init::general_section, "NumSkirmishPlayers") == 10,
        "the file's row count is the player's"
    );
    for (int32_t row = 0; row < 10; ++row) {
        const auto controller =
            stored(file, init::skirmish_section, "Player" + std::to_string(row) + "Controller");
        const auto expected = row == 4 ? entry::controller::human : entry::controller::computer;
        require(
            controller == static_cast<uint32_t>(expected), "the file has Player only at slot 5"
        );
    }
    require(
        stored(file, init::skirmish_section, "Player0Side") == 1 &&
            stored(file, init::skirmish_section, "Player0Color") == 6 &&
            stored(file, init::skirmish_section, "Player0Metal") == 2500 &&
            stored(file, init::skirmish_section, "Player0Energy") == 4500,
        "slot 1 keeps its side, colour and resources in the file"
    );
}

/// What Runtime saves: the kept rows when a match has replaced them, and the
/// live rows otherwise. Quit flushes those writes and does not save again.
const entry::SkirmishSettings& rows_to_save(
    const entry::SkirmishSettings& live, const std::optional<entry::SkirmishSettings>& kept
) {
    return kept ? *kept : live;
}

/// The intro gate saves before a start has loaded the rows. That save must
/// not write slot 1 as Player, and must not write a row count of zero over
/// the count the player left.
void intro_save_before_load_leaves_the_rows() {
    Preferences file;
    seed_slot_five(file);
    oa::ui::frontend_state::State state;
    state.play_intro_movie = 0;
    init::Preferences preferences;
    const entry::SkirmishSettings unloaded;
    init::save_preferences(state, unloaded, preferences, file);
    require(
        !file.written.contains(Preferences::key(init::skirmish_section, "Player0Controller")),
        "a save before the rows are loaded does not write slot 1"
    );
    require(
        !file.written.contains(Preferences::key(init::general_section, "NumSkirmishPlayers")),
        "a save before the rows are loaded does not write a zero row count"
    );
    expect_file_slot_five(file);
}

/// Start, reach the main menu, play and leave a skirmish as slot 5, quit,
/// and start again. The saves are the ones on that path: the start of the
/// skirmish, and a save while the match is up. The main menu does not save.
/// Opening the setup loads the rows and does not fill slot 1, because a row
/// is already taken. Quitting flushes the writes already made.
void slot_five_survives_closing() {
    Preferences file;
    seed_slot_five(file);
    oa::ui::frontend_state::State state;
    init::Preferences preferences;
    entry::SkirmishSettings live;

    init::load_preferences(state, live, preferences, file);
    expect_slot_five(live);
    require(!skirmish_ui::all_slots_disabled(live), "opening the setup leaves the loaded rows");

    file.written.clear();
    init::save_preferences(state, live, preferences, file);
    for (int32_t row = 0; row < live.slot_count; ++row) {
        const auto controller =
            saved(file, init::skirmish_section, "Player" + std::to_string(row) + "Controller");
        const auto expected = row == 4 ? entry::controller::human : entry::controller::computer;
        require(
            controller == static_cast<uint32_t>(expected),
            "starting the skirmish writes Player only at slot 5"
        );
    }
    require(
        saved(file, init::skirmish_section, "Player0Side") == 1 &&
            saved(file, init::skirmish_section, "Player0Color") == 6 &&
            saved(file, init::skirmish_section, "Player0Metal") == 2500 &&
            saved(file, init::skirmish_section, "Player0Energy") == 4500,
        "starting the skirmish keeps slot 1's side, colour and resources"
    );
    require(
        saved(file, init::general_section, "NumSkirmishPlayers") == 10,
        "starting the skirmish writes the row count"
    );
    file.adopt_writes();

    // A save while the match is up writes the rows the player set. A match
    // that seats its own first row does not replace those rows in the save.
    std::optional<entry::SkirmishSettings> kept;
    init::remember_player_skirmish_settings(live, kept);
    live.slot_count = 2;
    live.slots[0].controller = entry::controller::human;
    live.slots[0].side = 0;
    live.slots[0].color = 0;
    live.slots[0].metal = 1000;
    live.slots[0].energy = 1000;
    file.written.clear();
    init::save_preferences(state, rows_to_save(live, kept), preferences, file);
    require(
        saved(file, init::skirmish_section, "Player0Controller") ==
            static_cast<uint32_t>(entry::controller::computer),
        "a save during the skirmish does not write slot 1 as Player"
    );
    require(
        saved(file, init::general_section, "NumSkirmishPlayers") == 10,
        "a save during the skirmish writes the player's row count"
    );
    file.adopt_writes();
    init::restore_player_skirmish_settings(live, kept);

    entry::SkirmishSettings again;
    init::load_preferences(state, again, preferences, file);
    expect_slot_five(again);
    expect_file_slot_five(file);
    require(
        !skirmish_ui::all_slots_disabled(again),
        "the setup after the next start shows the same rows"
    );
}

/// The row count a save writes is the row count the next load reads, including
/// when the file held a different count.
void slot_count_round_trip() {
    oa::ui::frontend_state::State state;
    init::Preferences preferences;
    Preferences file;
    store_count(file, 4);
    entry::SkirmishSettings settings = player_rows();
    settings.slot_count = 6;
    init::save_preferences(state, settings, preferences, file);
    require(
        saved(file, init::general_section, "NumSkirmishPlayers") == 6,
        "the save writes the setup's row count"
    );
    file.adopt_writes();
    entry::SkirmishSettings loaded;
    init::load_preferences(state, loaded, preferences, file);
    require(loaded.slot_count == 6, "the row count read back is the row count written");
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
        intro_save_before_load_leaves_the_rows();
        slot_five_survives_closing();
        slot_count_round_trip();
        skirmish_as_slot_five_reloads();
        std::cout << "campaign skirmish slots passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
