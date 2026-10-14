// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sticky window edge: held while the pointer rests on an edge, let go
// by a firm push past it or by a quick approach, and held again only from
// inside the window.
#include "oa/app/sticky_edge.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace oa::app {
namespace {

/// The four edges, in the order StickyEdge stores them.
enum class Side : std::size_t { left = 0, right = 1, top = 2, bottom = 3 };

/// Returns how far from the edges a pointer must be before it is held again.
///
/// A window too small for the asked distance uses a band around its middle,
/// so the middle can still re-arm.
///
/// @param span the window's width or height, in points
/// @param asked the distance asked for, in points
/// @return the distance that fits this span
float rearm_inset(float span, float asked) {
    const float last = span - 1.0F;
    if (!(last > asked * 2.0F))
        return std::max(0.0F, last * 0.5F - 1.0F);
    return asked;
}

/// Forgets the push and the flick, and leaves the pointer free.
///
/// @param state what earlier samples decided
void disarm(StickyEdge& state) {
    state.holding = false;
    state.push = {};
    state.flick_count = 0;
    state.flick_next = 0;
}

/// Remembers one sample's movement toward the edges, merging the oldest
/// two when the list is full so their sum is kept.
///
/// @param state what earlier samples decided
/// @param time_ms when the movement arrived
/// @param outward points toward each edge; an edge with none stays 0
void remember_flick(StickyEdge& state, uint64_t time_ms, const std::array<float, 4>& outward) {
    constexpr int slots = StickyEdge::kFlickSlots;
    if (state.flick_count == slots) {
        const int oldest = (state.flick_next - state.flick_count + slots * 2) % slots;
        const int newer = (oldest + 1) % slots;
        for (std::size_t side = 0; side < outward.size(); ++side)
            state.flick[static_cast<std::size_t>(newer)].outward[side] +=
                state.flick[static_cast<std::size_t>(oldest)].outward[side];
        --state.flick_count;
    }
    StickyEdge::Flick& written = state.flick[static_cast<std::size_t>(state.flick_next)];
    written.time_ms = time_ms;
    written.outward = outward;
    state.flick_next = (state.flick_next + 1) % slots;
    ++state.flick_count;
}

/// Sums the movement toward the edges still inside the flick's time.
///
/// @param state what earlier samples decided
/// @param time_ms the time now
/// @param window_ms how far back movement counts
/// @return points toward each edge within the time
std::array<float, 4> flick_sum(const StickyEdge& state, uint64_t time_ms, uint64_t window_ms) {
    std::array<float, 4> sum{};
    constexpr int slots = StickyEdge::kFlickSlots;
    const int oldest = (state.flick_next - state.flick_count + slots * 2) % slots;
    for (int step = 0; step < state.flick_count; ++step) {
        const StickyEdge::Flick& sample =
            state.flick[static_cast<std::size_t>((oldest + step) % slots)];
        if (sample.time_ms > time_ms || time_ms - sample.time_ms > window_ms)
            continue;
        for (std::size_t side = 0; side < sum.size(); ++side)
            sum[side] += sample.outward[side];
    }
    return sum;
}

/// The distance past one end of a span.
///
/// The place's own overshoot and the desktop distance are the same push
/// told two ways, so the greater one counts once.
///
/// @param overshoot how far the place is past this end, or 0
/// @param field how far the desktop pointer is past this end, or 0
/// @return the push past this end
float push_past(float overshoot, float field) {
    return std::max(overshoot, field);
}

} // namespace

StickyLimits sticky_limits(float level) noexcept {
    const float held = level > 0.0F ? level : 0.0F;
    StickyLimits limits;
    limits.push_points = sticky_push_points_per_level * held;
    limits.flick_points_per_s = sticky_flick_points_per_s_per_level * held;
    return limits;
}

void release_sticky_edge(StickyEdge& state) {
    disarm(state);
}

StickyHold sticky_edge(
    StickyEdge& state, StickySample sample, float width, float height, StickyLimits limits
) {
    // A window too small to have an inside, and an edge with no firmness,
    // never hold.
    if (!(width >= 2.0F) || !(height >= 2.0F) || !(limits.push_points > 0.0F) ||
        !(limits.flick_points_per_s > 0.0F)) {
        disarm(state);
        return StickyHold::let_go;
    }
    const bool outside =
        sample.x < 0.0F || sample.y < 0.0F || sample.x >= width || sample.y >= height;
    const float inset_x = rearm_inset(width, limits.rearm_points);
    const float inset_y = rearm_inset(height, limits.rearm_points);
    const bool inside_far = !outside && sample.x >= inset_x && sample.x <= width - 1.0F - inset_x &&
                            sample.y >= inset_y && sample.y <= height - 1.0F - inset_y;
    if (!state.holding) {
        if (!inside_far)
            return StickyHold::let_go;
        disarm(state);
        state.holding = true;
        return StickyHold::hold;
    }
    // Toward an edge, wherever the pointer is, so the approach before the
    // edge is part of the flick. Along an edge, and back into the window,
    // this edge's own direction stays zero.
    std::array<float, 4> toward{};
    if (sample.motion_x < 0.0F)
        toward[static_cast<std::size_t>(Side::left)] = -sample.motion_x;
    if (sample.motion_x > 0.0F)
        toward[static_cast<std::size_t>(Side::right)] = sample.motion_x;
    if (sample.motion_y < 0.0F)
        toward[static_cast<std::size_t>(Side::top)] = -sample.motion_y;
    if (sample.motion_y > 0.0F)
        toward[static_cast<std::size_t>(Side::bottom)] = sample.motion_y;
    const bool any_toward =
        toward[0] > 0.0F || toward[1] > 0.0F || toward[2] > 0.0F || toward[3] > 0.0F;
    if (any_toward)
        remember_flick(state, sample.time_ms, toward);
    const float flick_points =
        limits.flick_points_per_s * static_cast<float>(limits.flick_window_ms) / 1000.0F;
    const std::array<float, 4> flicked = flick_sum(state, sample.time_ms, limits.flick_window_ms);
    const float last_x = width - 1.0F;
    const float last_y = height - 1.0F;
    const std::array<float, 4> past{
        push_past(sample.x < 0.0F ? -sample.x : 0.0F, sample.past_x < 0.0F ? -sample.past_x : 0.0F),
        push_past(
            sample.x > last_x ? sample.x - last_x : 0.0F,
            sample.past_x > 0.0F ? sample.past_x : 0.0F
        ),
        push_past(sample.y < 0.0F ? -sample.y : 0.0F, sample.past_y < 0.0F ? -sample.past_y : 0.0F),
        push_past(
            sample.y > last_y ? sample.y - last_y : 0.0F,
            sample.past_y > 0.0F ? sample.past_y : 0.0F
        ),
    };
    const std::array<bool, 4> on_side{
        sample.x <= 0.0F,
        sample.x >= last_x,
        sample.y <= 0.0F,
        sample.y >= last_y,
    };
    for (std::size_t side = 0; side < on_side.size(); ++side) {
        // On the edge, or already past it. Inside, away from this edge, the
        // push ends. The approach stays, so it still counts on arrival.
        const bool at_edge = on_side[side] || past[side] > 0.0F;
        if (!at_edge) {
            state.push[side] = {};
            continue;
        }
        // A distance of zero adds nothing and does not start the time again.
        if (past[side] > 0.0F) {
            StickyEdge::Push& push = state.push[side];
            if (!push.running || sample.time_ms < push.started_ms ||
                sample.time_ms - push.started_ms > limits.push_window_ms) {
                push.points = 0.0F;
                push.started_ms = sample.time_ms;
                push.running = true;
            }
            push.points += past[side];
        }
        if (state.push[side].points >= limits.push_points || flicked[side] >= flick_points) {
            disarm(state);
            return StickyHold::let_go;
        }
    }
    return StickyHold::hold;
}

} // namespace oa::app
