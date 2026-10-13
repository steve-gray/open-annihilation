// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A wreck's picture follows its feature record: over deep water it is drawn
// lower each frame until it rests, and on land it stays where it was placed.
#include "oa/app/match_model_draws.hpp"
#include "oa/sim/feature_runtime.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using oa::app::follow_feature_draw;
using oa::app::MatchFeatureDraw;
using oa::sim::feature_runtime::feature_record;
using oa::sim::feature_runtime::init_feature_pool;
using oa::sim::feature_runtime::no_feature;
using oa::sim::feature_runtime::place_feature;
using oa::sim::feature_runtime::PlacedFeature;
using oa::sim::feature_runtime::slot_capacity;
using oa::sim::feature_runtime::tick_features;
using oa::sim::feature_runtime::wreck_sink_speed;

constexpr int32_t fixed_one = 0x10000;

// One map, one 3DO wreck type and a feature pool.
struct Map {
    oa::World world{};
    std::vector<oa::MapPlot> plots;
    std::vector<oa::FeatureDef> defs;
    std::vector<PlacedFeature> records;

    explicit Map(uint8_t sea, uint8_t ground) : plots(8U * 8U), defs(1), records(slot_capacity) {
        world.game.map_width = 8;
        world.game.map_height = 8;
        world.game.sea_level = sea;
        world.game.gravity = 0x4000;
        world.plots = plots.data();
        for (auto& plot : plots) {
            plot.feature = no_feature;
            plot.height = ground;
            plot.high_height = ground;
            plot.low_height = ground;
        }
        world.feature_defs = defs.data();
        world.feature_def_count = 1;
        world.game.feature_def_count = 1;
        world.placed_features = reinterpret_cast<uint8_t*>(records.data());
        world.placed_feature_count = static_cast<uint32_t>(records.size());
        defs[0].footprint_x = 1;
        defs[0].footprint_z = 1;
        defs[0].dead_feature = no_feature;
        CHECK(init_feature_pool(world));
    }

    std::size_t at(int32_t cell_x, int32_t cell_z) const {
        return static_cast<std::size_t>(cell_z * world.game.map_width + cell_x);
    }
};

// The draw a placed wreck starts from: the record's position, with a picture
// already cached, as a frame before the wreck moves would have left it.
MatchFeatureDraw draw_of(const PlacedFeature& record, int32_t cell_x, int32_t cell_z) {
    MatchFeatureDraw draw;
    draw.position = {record.model.position.x, record.model.position.y, record.model.position.z};
    draw.cell_x = cell_x;
    draw.cell_z = cell_z;
    draw.feature_index = record.def_index;
    draw.state.cache.draws = 4;
    draw.state.cache.has_image = true;
    return draw;
}

oa::FixedVec3 over_cell(int32_t cell_x, int32_t cell_z, int32_t height) {
    return {
        static_cast<oa::oa_fixed>((cell_x * 16 + 8) * fixed_one),
        static_cast<oa::oa_fixed>(height * fixed_one),
        static_cast<oa::oa_fixed>((cell_z * 16 + 8) * fixed_one)
    };
}

// A corpse over deep water is drawn lower on each later frame until it rests
// on the seabed, and the cached picture is dropped as it moves.
void test_wreck_over_deep_water_sinks_on_the_frame() {
    constexpr uint8_t sea = 12;
    constexpr uint8_t seabed = 4;
    constexpr int32_t cell_x = 3;
    constexpr int32_t cell_z = 3;
    Map map(sea, seabed);
    oa::sim::feature_runtime::FeatureHost host{};
    const oa::FixedVec3 start = over_cell(cell_x, cell_z, sea);
    auto* wreck = place_feature(map.world, host, map.at(cell_x, cell_z), 0, &start, nullptr, 10);
    CHECK(wreck != nullptr);
    if (wreck == nullptr)
        return;
    wreck->model.velocity.x = 0;
    wreck->model.velocity.z = 0;
    wreck->model.velocity.y = wreck_sink_speed;
    MatchFeatureDraw draw = draw_of(*wreck, cell_x, cell_z);
    const int32_t spawned = draw.position.y;
    int32_t previous = spawned;
    int frames_lower = 0;
    for (int frame = 0; frame < 80 && wreck->model.velocity.y != 0; ++frame) {
        tick_features(map.world, host);
        follow_feature_draw(draw, map.world);
        CHECK(draw.position.x == wreck->model.position.x);
        CHECK(draw.position.y == wreck->model.position.y);
        CHECK(draw.position.z == wreck->model.position.z);
        CHECK(draw.position.y < previous);
        CHECK(draw.state.cache.draws == 0 && !draw.state.cache.has_image);
        ++frames_lower;
        previous = draw.position.y;
        draw.state.cache.draws = 4;
        draw.state.cache.has_image = true;
    }
    CHECK(frames_lower > 1);
    CHECK(draw.position.y == seabed * fixed_one);
    CHECK(draw.position.y < spawned);
    CHECK(
        wreck->model.velocity.x == 0 && wreck->model.velocity.y == 0 && wreck->model.velocity.z == 0
    );
    for (int frame = 0; frame < 3; ++frame) {
        tick_features(map.world, host);
        follow_feature_draw(draw, map.world);
        CHECK(draw.position.y == seabed * fixed_one);
        CHECK(draw.state.cache.draws == 4 && draw.state.cache.has_image);
    }
}

// A corpse on land is drawn where it was placed, and stays there.
void test_wreck_on_land_stays_where_it_was_placed() {
    constexpr uint8_t ground = 20;
    constexpr int32_t cell_x = 2;
    constexpr int32_t cell_z = 5;
    Map map(0, ground);
    oa::sim::feature_runtime::FeatureHost host{};
    const oa::FixedVec3 start = over_cell(cell_x, cell_z, ground);
    auto* wreck = place_feature(map.world, host, map.at(cell_x, cell_z), 0, &start, nullptr, 10);
    CHECK(wreck != nullptr);
    if (wreck == nullptr)
        return;
    CHECK(
        wreck->model.velocity.x == 0 && wreck->model.velocity.y == 0 && wreck->model.velocity.z == 0
    );
    MatchFeatureDraw draw = draw_of(*wreck, cell_x, cell_z);
    for (int frame = 0; frame < 4; ++frame) {
        tick_features(map.world, host);
        follow_feature_draw(draw, map.world);
        CHECK(draw.position.x == start.x);
        CHECK(draw.position.y == start.y);
        CHECK(draw.position.z == start.z);
        CHECK(draw.state.cache.draws == 4 && draw.state.cache.has_image);
    }
    const auto* rested =
        feature_record(map.world, map.plots[map.at(cell_x, cell_z)].feature_record);
    CHECK(rested != nullptr && rested->model.position.y == start.y);
}

} // namespace

int main() {
    test_wreck_over_deep_water_sinks_on_the_frame();
    test_wreck_on_land_stays_where_it_was_placed();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("match wreck draw passed");
    return EXIT_SUCCESS;
}
