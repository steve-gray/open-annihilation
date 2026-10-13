// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The states of a middle-click pan and the speed curve, with no match and no
// window: the runtime asks what a gesture does and how fast to scroll.
#include "oa/app/middle_pan.hpp"

#include <algorithm>
#include <cmath>

namespace oa::app::middle_pan {
namespace {

/// Tells whether the pointer has left the click.
bool past_click(const State& state, float x, float y) noexcept {
    const float dx = x - state.press_x;
    const float dy = y - state.press_y;
    return dx * dx + dy * dy > kClickSlop * kClickSlop;
}

} // namespace

Result
apply(State state, Gesture gesture, float x, float y, bool enabled, bool can_start) noexcept {
    if (!enabled)
        return {};
    switch (gesture) {
    case Gesture::move:
        if (state.phase == Phase::clicked && state.held && past_click(state, x, y))
            state.phase = Phase::dragging;
        return {state, state.phase != Phase::idle};
    case Gesture::middle_down:
        // A press while a pan is running only ends it. It does not start another,
        // and the press is not passed on.
        if (state.phase != Phase::idle)
            return {{}, true};
        if (!can_start)
            return {state, false};
        state.phase = Phase::clicked;
        state.held = true;
        state.anchor_x = state.press_x = x;
        state.anchor_y = state.press_y = y;
        return {state, true};
    case Gesture::middle_up:
        if (state.phase == Phase::dragging)
            return {{}, true};
        if (state.phase == Phase::clicked && state.held) {
            if (past_click(state, x, y))
                return {{}, true};
            state.held = false;
            return {state, true};
        }
        return {state, false};
    case Gesture::other:
        if (state.phase == Phase::idle)
            return {state, false};
        return {{}, true};
    }
    return {state, false};
}

Speed scroll_share(float anchor_x, float anchor_y, float pointer_x, float pointer_y) noexcept {
    const float dx = pointer_x - anchor_x;
    const float dy = pointer_y - anchor_y;
    const float distance = std::hypot(dx, dy);
    if (!(distance > kDeadZone))
        return {};
    const float share = std::min((distance - kDeadZone) / kTopSpeedReach, 1.0F);
    const float unit = share / distance;
    return {unit * dx, unit * dy};
}

} // namespace oa::app::middle_pan
