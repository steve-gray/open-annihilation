// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sticky window edge over a made-up pointer: resting on an edge stays
// held, a push or a flick lets go, movement along an edge does not, and
// coming back in near the edge is not held until the pointer has moved in.
#include "oa/app/sticky_edge.hpp"

#include "oa/test/check.hpp"

#include <cstdint>

namespace {

using oa::app::StickyEdge;
using oa::app::StickyHold;
using oa::app::StickyLimits;
using oa::app::StickySample;
using oa::app::release_sticky_edge;
using oa::app::sticky_edge;

constexpr float kWidth = 800.0F;
constexpr float kHeight = 600.0F;
constexpr float kRight = kWidth - 1.0F;
constexpr float kBottom = kHeight - 1.0F;

/// Feeds one sample and returns what the rule decided.
///
/// @param state what earlier samples decided
/// @param x place across the window
/// @param y place down the window
/// @param motion_x movement across
/// @param motion_y movement down
/// @param time_ms the sample's time
/// @param limits the thresholds; the ordinary ones when omitted
/// @return what sticky_edge returned
StickyHold feed(
    StickyEdge& state,
    float x,
    float y,
    float motion_x,
    float motion_y,
    uint64_t time_ms,
    StickyLimits limits = {}
) {
    return sticky_edge(
        state, StickySample{x, y, motion_x, motion_y, time_ms}, kWidth, kHeight, limits
    );
}

/// Holds from the middle, where the pointer is away from every edge.
void arm(StickyEdge& state) {
    OA_CHECK(feed(state, 400.0F, 300.0F, 0.0F, 0.0F, 0) == StickyHold::hold);
    OA_CHECK(state.holding);
}

void resting_on_an_edge_stays_held() {
    StickyEdge state;
    arm(state);
    // A few seconds on the right edge, the pointer still, as it is while
    // the view scrolls. Nothing lets go.
    for (uint64_t step = 0; step < 40; ++step) {
        const uint64_t time_ms = 1000 + step * 100;
        OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, time_ms) == StickyHold::hold);
    }
    OA_CHECK(state.holding);
}

void a_push_past_the_threshold_lets_go() {
    StickyEdge state;
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 1000) == StickyHold::hold);
    // 119 points of one push stays held. The last point reaches 120.
    OA_CHECK(feed(state, kRight, 300.0F, 119.0F, 0.0F, 1100) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 1.0F, 0.0F, 1200) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    // A push that arrives after the window has closed starts again, so
    // 119 and then 119 half a second later stays held.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 119.0F, 0.0F, 2000) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 119.0F, 0.0F, 2600) == StickyHold::hold);
    OA_CHECK(state.holding);
    // One movement of 120 on its own lets go.
    OA_CHECK(feed(state, kRight, 300.0F, 120.0F, 0.0F, 2700) == StickyHold::let_go);
}

void a_flick_lets_go() {
    StickyEdge state;
    arm(state);
    // Arriving at the bottom edge at 249 points in the last 100 ms stays
    // held. 250, the flick, lets go. A single slower step does not.
    OA_CHECK(feed(state, 400.0F, kBottom, 0.0F, 249.0F, 5000) == StickyHold::hold);
    release_sticky_edge(state);
    arm(state);
    OA_CHECK(feed(state, 400.0F, kBottom, 0.0F, 40.0F, 6000) == StickyHold::hold);
    OA_CHECK(feed(state, 400.0F, kBottom, 0.0F, 250.0F, 6050) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    // Four quick steps that add up across the last 100 ms are the same flick.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 7000) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 7010) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 7020) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 7030) == StickyHold::let_go);
}

void motion_along_an_edge_does_not_let_go() {
    StickyEdge state;
    arm(state);
    // On the bottom edge, a long slide sideways is not a push down, and
    // not a flick down. Movement back into the window counts neither.
    OA_CHECK(feed(state, 400.0F, kBottom, 400.0F, 0.0F, 1000) == StickyHold::hold);
    OA_CHECK(feed(state, 100.0F, kBottom, -400.0F, 0.0F, 1100) == StickyHold::hold);
    OA_CHECK(feed(state, 100.0F, kBottom, 0.0F, -80.0F, 1200) == StickyHold::hold);
    OA_CHECK(state.holding);
    // A corner counts each edge on its own axis. Sliding along the bottom
    // from the corner still does not push out of the left edge.
    OA_CHECK(feed(state, 0.0F, kBottom, 300.0F, 0.0F, 1300) == StickyHold::hold);
    OA_CHECK(feed(state, 0.0F, kBottom, -120.0F, 0.0F, 1400) == StickyHold::let_go);
}

void coming_back_near_the_edge_is_not_trapped() {
    StickyEdge state;
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 250.0F, 0.0F, 1000) == StickyHold::let_go);
    // Outside, then on the edge, then 23 points in: still free. 24 points
    // from every edge holds again.
    OA_CHECK(feed(state, kWidth + 10.0F, 300.0F, 10.0F, 0.0F, 1100) == StickyHold::let_go);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 1200) == StickyHold::let_go);
    OA_CHECK(feed(state, kRight - 23.0F, 300.0F, -23.0F, 0.0F, 1300) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    OA_CHECK(feed(state, kRight - 24.0F, 300.0F, -1.0F, 0.0F, 1400) == StickyHold::hold);
    OA_CHECK(state.holding);
    // Let go by the player leaving the match, then the same return.
    release_sticky_edge(state);
    OA_CHECK(!state.holding);
    OA_CHECK(feed(state, 10.0F, 10.0F, 0.0F, 0.0F, 2000) == StickyHold::let_go);
    OA_CHECK(feed(state, 24.0F, 24.0F, 14.0F, 14.0F, 2100) == StickyHold::hold);
}

void a_window_too_small_to_inset_still_rearms_in_the_middle() {
    StickyEdge state;
    StickyLimits limits;
    limits.rearm_points = 24.0F;
    // 30 points wide: the middle is the only place far enough from both edges.
    OA_CHECK(
        sticky_edge(state, StickySample{15.0F, 15.0F, 0.0F, 0.0F, 0}, 30.0F, 30.0F, limits) ==
        StickyHold::hold
    );
    OA_CHECK(
        sticky_edge(state, StickySample{0.0F, 15.0F, -15.0F, 0.0F, 10}, 30.0F, 30.0F, limits) ==
        StickyHold::let_go
    );
    OA_CHECK(
        sticky_edge(state, StickySample{2.0F, 15.0F, 2.0F, 0.0F, 20}, 30.0F, 30.0F, limits) ==
        StickyHold::let_go
    );
    OA_CHECK(
        sticky_edge(state, StickySample{15.0F, 15.0F, 13.0F, 0.0F, 30}, 30.0F, 30.0F, limits) ==
        StickyHold::hold
    );
}

} // namespace

int main() {
    resting_on_an_edge_stays_held();
    a_push_past_the_threshold_lets_go();
    a_flick_lets_go();
    motion_along_an_edge_does_not_let_go();
    coming_back_near_the_edge_is_not_trapped();
    a_window_too_small_to_inset_still_rearms_in_the_middle();
    return oa::test::check_exit_status();
}
