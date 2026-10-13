// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The 3DO draws the match renderer keeps from frame to frame: each placed 3D
// feature with its own draw state, and the draw state of every unit slot.
#pragma once

#include "oa/sim/feature_runtime.hpp"
#include "oa/present/model/model_draw.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace oa::app {

// A placed 3D feature the match draws. Its draw state lives here so that it
// moves and dies with the feature: the list drops the features the map
// replaces or removes and grows with wrecks, which moves the others in
// memory, and a feature of the same model must not take over their state.
struct MatchFeatureDraw {
    MatchFeatureDraw() = default;
    // A copy's cached image and silhouette would point into the source's
    // pixels, which go with the source: features only ever move.
    MatchFeatureDraw(const MatchFeatureDraw&) = delete;
    MatchFeatureDraw& operator=(const MatchFeatureDraw&) = delete;
    MatchFeatureDraw(MatchFeatureDraw&&) noexcept = default;
    MatchFeatureDraw& operator=(MatchFeatureDraw&&) noexcept = default;

    oa::sim::model_runtime::Instance instance{};
    oa::formats::objects3d::FixedVector3 position{};
    oa::sim::model_runtime::RotationWords rotation{}; // the placed record's orientation
    int32_t cell_x{};
    int32_t cell_z{};
    uint16_t feature_index = oa::sim::feature_runtime::no_feature; // MapPlot.feature at load
    oa::present::model::ModelState state{};
};

// A growing list moves its features instead of copying them: a copied
// state's cached image would still point into the source's pixels.
static_assert(std::is_nothrow_move_constructible_v<MatchFeatureDraw>);

// The draw state of one unit slot and the instance it was begun for.
struct UnitDrawState {
    uint32_t instance_generation{}; // the slot's SlotRuntime::instance_generation
    oa::present::model::ModelState state{};
};

// The slot list grows by moving its states, for the same reason.
static_assert(std::is_nothrow_move_constructible_v<UnitDrawState>);

/// Returns a unit slot's draw state, begun afresh when the slot's instance is of another generation.
///
/// A unit made after a death can get the freed instance's memory, so the
/// state follows the slot's instance generation rather than the instance's
/// address, and a new unit never inherits the dead one's cached image,
/// silhouette, draw count or piece snapshots.
///
/// @param[in,out] units draw states by unit slot; grown to hold the slot
/// @param slot unit slot
/// @param instance_generation the slot's SlotRuntime::instance_generation
/// @return the slot's draw state
inline oa::present::model::ModelState&
unit_draw_state(std::vector<UnitDrawState>& units, uint16_t slot, uint32_t instance_generation) {
    if (units.size() <= slot)
        units.resize(static_cast<std::size_t>(slot) + 1);
    auto& tracked = units[slot];
    if (tracked.instance_generation != instance_generation)
        tracked = {instance_generation, {}};
    return tracked.state;
}

/// Copies a 3DO feature's record position into its draw.
///
/// While the record sinks, each frame's picture is drawn lower; once the
/// record rests, the picture stays at that height. A change drops the cached
/// picture, so the next frame builds it where the wreck is and the underwater
/// tint follows the hull.
///
/// @param[in,out] feature the feature's draw
/// @param world world holding the plot and the feature record
inline void follow_feature_draw(MatchFeatureDraw& feature, oa::World& world) {
    const oa::MapPlot* plot = oa::world_plot(&world, feature.cell_x, feature.cell_z);
    if (plot == nullptr || (plot->flags & OA_PLOT_FLAG_ANIMATING_FEATURE) == 0)
        return;
    const oa::sim::feature_runtime::PlacedFeature* record =
        oa::sim::feature_runtime::feature_record(world, plot->feature_record);
    if (record == nullptr)
        return;
    const oa::formats::objects3d::FixedVector3 record_position{
        record->model.position.x, record->model.position.y, record->model.position.z
    };
    if (feature.position.x == record_position.x && feature.position.y == record_position.y &&
        feature.position.z == record_position.z)
        return;
    feature.position = record_position;
    feature.state.image = {};
    feature.state.shadow = {};
    feature.state.cache = {};
    feature.state.finer.reset();
}

} // namespace oa::app
