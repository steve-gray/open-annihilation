// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A unit whose type is a feature stops being a unit when its build finishes:
// it is removed and the feature named by its corpse takes the same footprint,
// at that feature's own health, in the builder's colour. The builder's order
// still ends with "Building complete", and the selection and the unit count
// are as they were before the wall existed. Run with --data, the installed
// commander's dragon's teeth do the same.
#include "installed_units.hpp"
#include "match_tick_access.hpp"

#include "oa/core/feature_def.h"
#include "oa/core/map_plot.h"
#include "oa/core/unit.h"
#include "oa/sim/feature_runtime.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/test/match_services.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x);   \
    } while (false)

namespace {
constexpr int32_t map_cells = 64;
constexpr uint8_t sea_level = 20;
constexpr uint8_t land_height = 30;
constexpr uint16_t builder_type = 1;
constexpr uint16_t wall_type = 2;
constexpr int16_t wall_health = 50;
constexpr int16_t wall_feature_health = 80;
constexpr float wall_feature_metal = 7.0F;
constexpr uint16_t no_further_feature = 0xffff;

struct Services : oa::test::QuietServices {
    std::vector<uint32_t> speech;
    std::vector<std::string> captions;

    void command_sound(sim::unit_spawn::Slot&, uint32_t category) override {
        speech.push_back(category);
        captions.emplace_back();
    }

    static void
    speak(void* context, sim::unit_spawn::Slot&, uint32_t category, const char* caption) {
        auto& services = *static_cast<Services*>(context);
        services.speech.push_back(category);
        services.captions.emplace_back(caption == nullptr ? "" : caption);
    }

    void clear() {
        speech.clear();
        captions.clear();
    }
};

using Scenario = oa::test::EmptyScenario;

uint32_t step(
    sim::match_runtime::Match& match,
    sim::unit_spawn::Slot& unit,
    sim::simulation_state::Order& order
) {
    sim::match_runtime::MatchTickAccess host(match);
    order.wait_events = 0;
    const auto result = host.dispatch_mission(match.state(), unit.record, order, 0);
    if (result == 0)
        order.phase = 0;
    else if (result == 1)
        ++order.phase;
    return result;
}

void fund(sim::unit_spawn::Slot& unit) {
    auto& economy = unit.record.economy;
    economy.energy.requested = economy.energy.accepted = economy.energy.gate = 0.0F;
    economy.metal.requested = economy.metal.accepted = economy.metal.gate = 0.0F;
}

void set_feature_name(oa::FeatureDef& def, const char* text) {
    std::size_t at = 0;
    for (; text[at] != '\0' && at + 1 < sizeof def.name; ++at)
        def.name[at] = text[at];
    def.name[at] = '\0';
}

bool same_name(const char* stored, std::size_t capacity, std::string_view name) {
    std::size_t length = 0;
    while (length < capacity && stored[length] != '\0')
        ++length;
    if (length != name.size())
        return false;
    for (std::size_t at = 0; at < length; ++at)
        if (stored[at] != name[at])
            return false;
    return true;
}

uint32_t world_of(int32_t cell) {
    return static_cast<uint32_t>(cell * 16 + 8) << 16;
}

void prepare_players(sim::match_runtime::Match& match) {
    match.configure_strategic_environment({0, 0.5F, 0});
    for (uint8_t player = 0; player < 2; ++player) {
        match.simulation().players[player].present = true;
        match.simulation().players[player].status = player == 0 ? 1 : 2;
        std::array<uint8_t, 10> allies{};
        allies[player] = 1;
        match.configure_player_alliances(player, allies);
        auto& resources = match.state().game.players[player];
        resources.energy = resources.metal = 1000.0F;
        resources.energy_storage = resources.metal_storage = 1000.0F;
    }
    std::array<uint8_t, 10> local_allies{};
    local_allies[0] = 1;
    match.configure_outcomes(0, local_allies, false);
}

// QuietServices is not on Match. The fixtures keep the services object and
// pass its address to the speech hook themselves.
void bind_speech(sim::match_runtime::Match& match, Services& services) {
    match.set_speech_hooks({&services, &Services::speak});
}

struct FlatMap {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain;
    std::vector<sim::spatial_state::Plot> plots;
    std::array<sim::visibility_state::SightMask, 1> masks{};

    FlatMap() {
        map.attribute_width = map.attribute_height = map_cells;
        map.sea_level = sea_level;
        const auto cells =
            static_cast<std::size_t>(map_cells) * static_cast<std::size_t>(map_cells);
        map.attributes.resize(cells);
        terrain.resize(cells);
        plots.resize(cells);
        for (auto& cell : map.attributes)
            cell.height = land_height;
        for (auto& plot : plots) {
            plot.low_height = land_height;
            plot.high_height = land_height;
        }
        masks[0].width = masks[0].height = 15;
        masks[0].offset_x = masks[0].offset_z = 7;
        masks[0].pixels.assign(15 * 15, 1);
    }
};

// The wall's corpse, and the rubble one further wreck level would leave.
oa::FeatureDef make_feature(const char* name, int16_t health, float metal, uint16_t next) {
    oa::FeatureDef def{};
    set_feature_name(def, name);
    def.footprint_x = 2;
    def.footprint_z = 2;
    def.damage = health;
    def.metal = metal;
    def.dead_feature = next;
    def.flags = static_cast<uint16_t>(OA_FEATURE_FLAG_BLOCKING | OA_FEATURE_FLAG_RECLAIMABLE);
    return def;
}

struct Synthetic {
    FlatMap ground;
    std::shared_ptr<formats::objects3d::Model> model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> script = std::make_shared<formats::cob::CobProgram>();
    std::array<sim::unit_spawn::LoadedType, 3> loaded{};
    std::array<sim::unit_spawn::Type, 3> types{};
    std::array<data::unit_definitions::UnitDefinition, 3> defs{};
    std::array<data::unit_definitions::RuntimeDefinitionMetadata, 3> metadata{};
    data::unit_definitions::UnitTargetCategoryMasks target_masks{};
    std::array<sim::match_runtime::RuntimeTypeFields, 3> fields{};
    std::vector<uint8_t> yard = std::vector<uint8_t>(4, 4);
    sim::combat_state::WeaponRegistry weapons;
    std::array<oa::FeatureDef, 2> features{
        make_feature("Wall", wall_feature_health, wall_feature_metal, 1),
        make_feature("Rubble", 20, 1.0F, no_further_feature)
    };
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    Synthetic() {
        model->objects.resize(1);
        model->objects[0].name = "root";
        model->objects[0].vertices = {{0, 0, 0}, {0, 20 << 16, 0}};
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        for (std::size_t type = 1; type < types.size(); ++type) {
            loaded[type].model = model;
            loaded[type].script = script;
            types[type].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
            types[type].simulation.maximum_health = 100;
            types[type].footprint_x = types[type].footprint_z = 2;
            types[type].model = reinterpret_cast<uintptr_t>(model.get());
            types[type].cob = reinterpret_cast<uintptr_t>(script.get());
            defs[type].sight_distance = 200;
            defs[type].build_time = 60;
            defs[type].build_cost_energy = 200;
            defs[type].build_cost_metal = 10;
            fields[type].definition = &defs[type];
            fields[type].yard_mask = yard;
            fields[type].runtime_metadata = &metadata[type];
            fields[type].target_masks = &target_masks;
            loaded[type].type = types[type];
        }
        loaded[builder_type].unit_name = "BUILDER";
        loaded[wall_type].unit_name = "WALL";
        types[builder_type].bm_code = 1;
        types[builder_type].simulation.flags |= OA_UNIT_DEF_FLAG_BUILDER;
        types[builder_type].simulation.abilities = OA_UNIT_DEF_ABILITY_CAN_MOVE;
        defs[builder_type].builder = true;
        defs[builder_type].can_move = true;
        defs[builder_type].worker_time = 300;
        defs[builder_type].build_distance = 200;
        defs[builder_type].acceleration_fixed = 65536;
        defs[builder_type].brake_rate_fixed = 65536;
        defs[builder_type].max_velocity_fixed = 2 * 65536;
        defs[builder_type].turn_rate = 1024;
        fields[builder_type].movement_class = 0;
        types[wall_type].bm_code = 0;
        types[wall_type].simulation.maximum_health = static_cast<uint32_t>(wall_health);
        types[wall_type].simulation.flags |= OA_UNIT_DEF_FLAG_IS_FEATURE;
        defs[wall_type].is_feature = true;
        fields[wall_type].corpse_feature = 0;
        // The type record is copied into the loaded type before the match
        // reads it; the wall's feature flag is set after that copy above.
        loaded[builder_type].type = types[builder_type];
        loaded[wall_type].type = types[wall_type];

        sim::match_runtime::OfflineInputs input{ground.map,
                                                loaded,
                                                types,
                                                fields,
                                                weapons,
                                                ground.terrain,
                                                ground.masks,
                                                32,
                                                32,
                                                40,
                                                2,
                                                0,
                                                30,
                                                1,
                                                &scenario,
                                                [] { return 1000u; },
                                                ground.plots,
                                                {},
                                                0,
                                                0,
                                                0.0F,
                                                features};
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        bind_speech(*match, services);
        prepare_players(*match);
        match->state().game.tick = 100;
    }

    sim::unit_spawn::Slot& spawn(int32_t cell_x, int32_t cell_z) {
        auto* slot =
            match->create({0, builder_type, {world_of(cell_x), 0, world_of(cell_z)}, true, 1, 0});
        CHECK(slot && slot->unit);
        return *slot;
    }
};

struct Installed {
    test::InstalledUnits& units;
    FlatMap ground;
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    explicit Installed(test::InstalledUnits& loaded) : units(loaded) {
        sim::match_runtime::OfflineInputs input{
            ground.map,
            units.loaded,
            units.types,
            units.fields,
            units.weapons,
            ground.terrain,
            ground.masks,
            32,
            32,
            40,
            2,
            0,
            30,
            1,
            &scenario,
            [] { return 1000u; },
            ground.plots,
            {},
            0,
            0,
            0.0F,
            units.features.defs
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        bind_speech(*match, services);
        prepare_players(*match);
        match->state().game.tick = 100;
    }

    sim::unit_spawn::Slot& spawn(std::string_view name, int32_t cell_x, int32_t cell_z) {
        auto* slot = match->create(
            {0, units.type(name), {world_of(cell_x), 0, world_of(cell_z)}, true, 1, 0}
        );
        CHECK(slot && slot->unit);
        return *slot;
    }
};

uint16_t live_unit_count(const sim::match_runtime::Match& match) {
    return match.state().game.players[0].unit_count;
}

int selected_units(const sim::match_runtime::Match& match) {
    int selected = 0;
    for (const auto& slot : match.world().slots)
        if (slot.unit && slot.record.type_index != 0 && (slot.record.flags & OA_UNIT_FLAG_SELECTED))
            ++selected;
    return selected;
}

// Drives MobileBuild through placing the frame and nanolathing it, without
// waiting for the builder to walk. The builder's script sets the build stance;
// a type with no such script is stood in as the build starts.
sim::unit_spawn::Slot&
finish_wall(sim::match_runtime::Match& match, sim::unit_spawn::Slot& builder, uint16_t wall) {
    // A site several footprints clear of the builder, on dry flat land.
    constexpr int32_t site = 584;
    auto& order =
        match.issue_mobile_build(builder.unit_index, wall, {site << 16, 0, site << 16}, false);
    CHECK(step(match, builder, order) == 1);
    const auto placed = step(match, builder, order);
    if (placed != 1)
        throw std::runtime_error(
            "the nanoframe was not placed (result " + std::to_string(placed) + ")"
        );
    sim::unit_spawn::Slot* frame = nullptr;
    for (auto& slot : match.world().slots)
        if (slot.unit && slot.record.type_index == wall)
            frame = &slot;
    CHECK(frame != nullptr && frame->record.build_remaining != 0.0F);
    for (int waited = 0;
         waited < 1200 && (builder.record.build_flags & OA_UNIT_BUILD_IN_BUILD_STANCE) == 0;
         ++waited)
        match.tick_scripts(1);
    if ((builder.record.build_flags & OA_UNIT_BUILD_IN_BUILD_STANCE) == 0)
        throw std::runtime_error("the builder never took its build stance");
    CHECK(step(match, builder, order) == 1);
    for (int worked = 0; worked < 400 && order.phase == 3; ++worked) {
        fund(builder);
        (void)step(match, builder, order);
    }
    CHECK(order.phase == 4);
    CHECK(frame->record.build_remaining == 0.0F);
    CHECK((frame->record.flags & OA_UNIT_FLAG_DEATH_PENDING) != 0);
    CHECK(
        frame->record.damage_kind == static_cast<uint8_t>(sim::match_runtime::DeathKind::dismissed)
    );
    return *frame;
}

void settle_wall(sim::match_runtime::Match& match, sim::unit_spawn::Slot& frame) {
    for (int tick = 0; tick < 5 && frame.record.type_index != 0; ++tick) {
        ++match.state().game.tick;
        match.tick();
    }
    CHECK(frame.record.type_index == 0);
}

const sim::match_runtime::Match::Wreck& newest_wreck(const sim::match_runtime::Match& match) {
    CHECK(!match.wrecks().empty());
    return match.wrecks().back();
}

void expect_wall_feature(
    sim::match_runtime::Match& match,
    const sim::match_runtime::Match::Wreck& wreck,
    std::string_view name,
    int16_t health,
    float metal,
    uint8_t owner
) {
    auto& world = match.state();
    CHECK(wreck.feature < world.feature_def_count);
    const auto& feature = world.feature_defs[wreck.feature];
    CHECK(same_name(feature.name, sizeof feature.name, name));
    CHECK(feature.damage == health);
    CHECK(feature.metal == metal);
    CHECK((feature.flags & OA_FEATURE_FLAG_BLOCKING) != 0);
    CHECK((feature.flags & OA_FEATURE_FLAG_RECLAIMABLE) != 0);
    const auto plot =
        static_cast<std::size_t>(wreck.cell_z) * static_cast<std::size_t>(world.game.map_width) +
        static_cast<std::size_t>(wreck.cell_x);
    const auto plots = static_cast<std::size_t>(world.game.map_width) *
                       static_cast<std::size_t>(world.game.map_height);
    CHECK(plot < plots);
    CHECK(world.plots[plot].feature == wreck.feature);
    const auto colour = static_cast<uint8_t>(
        (world.plots[plot].flags & OA_PLOT_FLAG_PLAYER_FEATURE_MASK) >>
        sim::feature_runtime::plot_player_shift
    );
    CHECK(colour == owner);
    const auto* placed =
        sim::feature_runtime::feature_record(world, world.plots[plot].feature_record);
    CHECK(placed != nullptr);
    CHECK(placed->damage == 0);
    CHECK(placed->def_index == wreck.feature);
    CHECK(placed->model.velocity.y == 0);
}

bool spoke(const Services& services, std::string_view caption) {
    for (const auto& said : services.captions)
        if (said == caption)
            return true;
    return false;
}

// A made-up wall: its own health is 50 and its feature's is 80, so a copy of
// the unit's health would show. One further wreck level would be the rubble.
void finished_wall_becomes_a_feature() {
    Synthetic fixture;
    auto& builder = fixture.spawn(16, 16);
    builder.record.flags |= OA_UNIT_FLAG_SELECTED;
    const auto units_before = live_unit_count(*fixture.match);
    const auto listed_before = fixture.match->state().game.hot_unit_count;
    CHECK(selected_units(*fixture.match) == 1);
    auto& frame = finish_wall(*fixture.match, builder, wall_type);
    CHECK(live_unit_count(*fixture.match) == static_cast<uint16_t>(units_before + 1));
    fixture.services.clear();
    const auto wrecks_before = fixture.match->wrecks().size();
    settle_wall(*fixture.match, frame);
    CHECK(fixture.match->wrecks().size() == wrecks_before + 1);
    const auto& wreck = newest_wreck(*fixture.match);
    expect_wall_feature(*fixture.match, wreck, "Wall", wall_feature_health, wall_feature_metal, 0);
    CHECK(wreck.feature == 0);
    CHECK(fixture.match->state().feature_defs[wreck.feature].dead_feature == 1);
    CHECK(
        fixture.match->state().unit_defs[wall_type].max_damage == static_cast<uint32_t>(wall_health)
    );
    CHECK(fixture.match->state().feature_defs[0].damage != wall_health);
    CHECK(live_unit_count(*fixture.match) == units_before);
    CHECK(selected_units(*fixture.match) == 1);
    CHECK((builder.record.flags & OA_UNIT_FLAG_SELECTED) != 0);
    CHECK(fixture.match->state().game.hot_unit_count == listed_before);
    CHECK(spoke(fixture.services, "Building complete"));
    CHECK(!spoke(fixture.services, "Construction terminated"));
}

// The installed dragon's teeth: the feature is DragonsTeeth at 4000 health,
// not the unit's 3500 and not the rubble one further wreck level on.
void installed_dragons_teeth(const AssetStore& store) {
    test::InstalledUnits units(store, {"ARMCOM", "ARMDRAG"});
    Installed fixture(units);
    const auto wall = units.type("ARMDRAG");
    CHECK(units.definitions[wall].is_feature);
    CHECK(units.definitions[wall].max_damage == 3500);
    auto& commander = fixture.spawn("ARMCOM", 16, 16);
    commander.record.flags |= OA_UNIT_FLAG_SELECTED;
    const auto units_before = live_unit_count(*fixture.match);
    const auto listed_before = fixture.match->state().game.hot_unit_count;
    CHECK(selected_units(*fixture.match) == 1);
    auto& frame = finish_wall(*fixture.match, commander, wall);
    CHECK(live_unit_count(*fixture.match) == static_cast<uint16_t>(units_before + 1));
    fixture.services.clear();
    const auto wrecks_before = fixture.match->wrecks().size();
    settle_wall(*fixture.match, frame);
    CHECK(fixture.match->wrecks().size() == wrecks_before + 1);
    const auto& wreck = newest_wreck(*fixture.match);
    expect_wall_feature(*fixture.match, wreck, "DragonsTeeth", 4000, 8.0F, 0);
    const auto& feature = fixture.match->state().feature_defs[wreck.feature];
    CHECK(feature.damage != 3500);
    CHECK(feature.dead_feature < fixture.match->state().feature_def_count);
    CHECK(same_name(
        fixture.match->state().feature_defs[feature.dead_feature].name,
        sizeof feature.name,
        "RockTeeth"
    ));
    CHECK(wreck.feature != feature.dead_feature);
    CHECK(wreck.feature == static_cast<uint16_t>(units.fields[wall].corpse_feature));
    CHECK(live_unit_count(*fixture.match) == units_before);
    CHECK(selected_units(*fixture.match) == 1);
    CHECK((commander.record.flags & OA_UNIT_FLAG_SELECTED) != 0);
    CHECK(fixture.match->state().game.hot_unit_count == listed_before);
    CHECK(spoke(fixture.services, "Building complete"));
    CHECK(!spoke(fixture.services, "Construction terminated"));
    for (const auto& slot : fixture.match->world().slots)
        CHECK(!(slot.unit && slot.record.type_index == wall));
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (test::game_data_requested(argc, argv)) {
            const auto assets = test::require_game_assets("the installed dragon's teeth");
            installed_dragons_teeth(assets);
            std::cout << "installed dragon's teeth passed\n";
            return 0;
        }
        finished_wall_becomes_a_feature();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "feature walls passed\n";
    return 0;
}
