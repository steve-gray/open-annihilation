// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A middle click starts a pan and a later press ends it without being passed
// on; a drag pans while the button is held and stops when it comes up; the
// view stays still inside the dead zone and reaches the scroll speed past it;
// with Middle-click pans off, a middle click does nothing and is not swallowed.
#include "oa/app/middle_pan.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using oa::app::middle_pan::Gesture;
using oa::app::middle_pan::Phase;
using oa::app::middle_pan::State;
using oa::app::middle_pan::apply;
using oa::app::middle_pan::kClickSlop;
using oa::app::middle_pan::kDeadZone;
using oa::app::middle_pan::kTopSpeedReach;
using oa::app::middle_pan::scroll_share;

constexpr float kAnchorX = 100.0F;
constexpr float kAnchorY = 80.0F;

bool near(float value, float wanted) {
    return std::fabs(value - wanted) <= 1.0e-5F;
}

/// A middle press and release that stay within a click, on the battlefield.
State click_pan() {
    const auto down = apply({}, Gesture::middle_down, kAnchorX, kAnchorY, true, true);
    CHECK(down.swallow);
    CHECK(down.state.phase == Phase::clicked);
    CHECK(down.state.held);
    const auto up = apply(down.state, Gesture::middle_up, kAnchorX, kAnchorY, true, true);
    CHECK(up.swallow);
    CHECK(up.state.phase == Phase::clicked);
    CHECK(!up.state.held);
    CHECK(up.state.anchor_x == kAnchorX && up.state.anchor_y == kAnchorY);
    return up.state;
}

void click_starts_and_a_later_press_ends_it() {
    const State panning = click_pan();
    const auto ended = apply(panning, Gesture::other, 0.0F, 0.0F, true, true);
    CHECK(ended.swallow);
    CHECK(ended.state.phase == Phase::idle);
    // The press that ended it does not start another pan.
    const auto again = apply(ended.state, Gesture::middle_down, kAnchorX, kAnchorY, true, true);
    CHECK(again.state.phase == Phase::clicked);
    CHECK(again.swallow);
    const auto stopped = apply(panning, Gesture::middle_down, 40.0F, 40.0F, true, true);
    CHECK(stopped.swallow);
    CHECK(stopped.state.phase == Phase::idle);
}

void drag_pans_while_held() {
    auto down = apply({}, Gesture::middle_down, kAnchorX, kAnchorY, true, true);
    const float within = kAnchorX + kClickSlop;
    const auto still = apply(down.state, Gesture::move, within, kAnchorY, true, true);
    CHECK(still.state.phase == Phase::clicked);
    CHECK(still.state.held);
    const float dragged = kAnchorX + kDeadZone + kTopSpeedReach;
    const auto drag = apply(still.state, Gesture::move, dragged, kAnchorY, true, true);
    CHECK(drag.swallow);
    CHECK(drag.state.phase == Phase::dragging);
    CHECK(drag.state.held);
    CHECK(drag.state.anchor_x == kAnchorX);
    const auto speed = scroll_share(drag.state.anchor_x, drag.state.anchor_y, dragged, kAnchorY);
    CHECK(near(speed.x, 1.0F));
    CHECK(near(speed.y, 0.0F));
    const auto released = apply(drag.state, Gesture::middle_up, dragged, kAnchorY, true, true);
    CHECK(released.swallow);
    CHECK(released.state.phase == Phase::idle);
}

void dead_zone_and_top_speed() {
    const auto still = scroll_share(0.0F, 0.0F, kDeadZone, 0.0F);
    CHECK(still.x == 0.0F && still.y == 0.0F);
    const float half = kDeadZone + kTopSpeedReach / 2.0F;
    const auto midway = scroll_share(0.0F, 0.0F, half, 0.0F);
    CHECK(near(midway.x, 0.5F));
    CHECK(near(midway.y, 0.0F));
    const float full = kDeadZone + kTopSpeedReach;
    const auto topped = scroll_share(0.0F, 0.0F, full, 0.0F);
    CHECK(near(topped.x, 1.0F));
    CHECK(near(topped.y, 0.0F));
    const float along = full / std::sqrt(2.0F);
    const auto diagonal = scroll_share(0.0F, 0.0F, along, along);
    CHECK(near(diagonal.x, diagonal.y));
    CHECK(near(std::hypot(diagonal.x, diagonal.y), 1.0F));
    const auto beyond = scroll_share(0.0F, 0.0F, full + 40.0F, 0.0F);
    CHECK(near(beyond.x, 1.0F));
    CHECK(near(beyond.y, 0.0F));
}

void off_does_nothing() {
    const auto down = apply({}, Gesture::middle_down, kAnchorX, kAnchorY, false, true);
    CHECK(!down.swallow);
    CHECK(down.state.phase == Phase::idle);
    const State running = click_pan();
    const auto cleared = apply(running, Gesture::other, 0.0F, 0.0F, false, true);
    CHECK(!cleared.swallow);
    CHECK(cleared.state.phase == Phase::idle);
    const auto missed = apply({}, Gesture::middle_down, 0.0F, 0.0F, true, false);
    CHECK(!missed.swallow);
    CHECK(missed.state.phase == Phase::idle);
}

} // namespace

int main() {
    click_starts_and_a_later_press_ends_it();
    drag_pans_while_held();
    dead_zone_and_top_speed();
    off_does_nothing();
    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    return 0;
}
