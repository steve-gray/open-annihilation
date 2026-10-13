// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sticky window edge over a made-up pointer: resting on an edge stays
// held, a push past the threshold within its time lets go and one spread
// past that time does not, a quick approach lets go and a slow one holds,
// movement along an edge does not, and coming back in near the edge is not
// held until the pointer has moved in.
#include "oa/app/sticky_edge.hpp"

#include "oa/test/check.hpp"

#include <algorithm>
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
/// @param past_x desktop distance past the left (negative) or the right
/// @param past_y desktop distance past the top (negative) or the bottom
/// @param limits the thresholds; the ordinary ones when omitted
/// @return what sticky_edge returned
StickyHold feed(
    StickyEdge& state,
    float x,
    float y,
    float motion_x,
    float motion_y,
    uint64_t time_ms,
    float past_x = 0.0F,
    float past_y = 0.0F,
    StickyLimits limits = {}
) {
    return sticky_edge(
        state,
        StickySample{x, y, motion_x, motion_y, time_ms, past_x, past_y},
        kWidth,
        kHeight,
        limits
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
    // the view scrolls. No distance past the edge, so nothing lets go.
    for (uint64_t step = 0; step < 40; ++step) {
        const uint64_t time_ms = 1000 + step * 100;
        OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, time_ms) == StickyHold::hold);
    }
    OA_CHECK(state.holding);
}

void a_push_past_the_threshold_lets_go() {
    StickyEdge state;
    arm(state);
    // Movement on the edge is not a push. 200 points is under the flick.
    OA_CHECK(feed(state, kRight, 300.0F, 200.0F, 0.0F, 1000) == StickyHold::hold);
    // 119 points past the edge stays held. The last point, within the
    // push's time, reaches 120.
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 2000, 119.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 2100, 1.0F) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    // A sample at exactly half a second still belongs to the same push.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 3000, 60.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 3500, 60.0F) == StickyHold::let_go);
    // A rest grows nothing and does not start the time again, so the next
    // distance within the window still belongs to the push.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 4000, 60.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 4400) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 4490, 60.0F) == StickyHold::let_go);
    // A push spread past the window starts again, so 60 and then 60 does
    // not let go.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 5000, 60.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 5600, 60.0F) == StickyHold::hold);
    OA_CHECK(state.holding);
    // One distance of 120 on its own lets go. So does the same distance
    // told by a place past the last pixel, added up while the push goes on.
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 5700, 120.0F) == StickyHold::let_go);
    arm(state);
    OA_CHECK(feed(state, kWidth + 40.0F, 300.0F, 0.0F, 0.0F, 6000) == StickyHold::hold);
    OA_CHECK(feed(state, kWidth + 40.0F, 300.0F, 0.0F, 0.0F, 6100) == StickyHold::hold);
    OA_CHECK(feed(state, kWidth + 40.0F, 300.0F, 0.0F, 0.0F, 6200) == StickyHold::let_go);
}

void a_quick_approach_lets_go_and_a_slow_one_holds() {
    StickyEdge state;
    arm(state);
    // Arriving at 249 points in the last 100 ms stays held. 250 lets go.
    OA_CHECK(feed(state, 400.0F, kBottom, 0.0F, 249.0F, 5000) == StickyHold::hold);
    release_sticky_edge(state);
    arm(state);
    OA_CHECK(feed(state, 400.0F, kBottom, 0.0F, 250.0F, 5100) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    // The approach starts inside the window. The samples before the edge
    // count, and the one that arrives lets go.
    arm(state);
    OA_CHECK(feed(state, 100.0F, 300.0F, 100.0F, 0.0F, 6000) == StickyHold::hold);
    OA_CHECK(feed(state, 300.0F, 300.0F, 100.0F, 0.0F, 6030) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 60.0F, 0.0F, 6060) == StickyHold::let_go);
    // A slow walk to the edge stays under the flick in any 100 ms, and
    // there is no distance past the edge, so it stays held.
    arm(state);
    float place = 30.0F;
    uint64_t time_ms = 7000;
    while (place < kRight) {
        const float step = std::min(30.0F, kRight - place);
        place += step;
        OA_CHECK(feed(state, place, 300.0F, step, 0.0F, time_ms) == StickyHold::hold);
        time_ms += 90;
    }
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, time_ms) == StickyHold::hold);
    OA_CHECK(state.holding);
    // Four quick steps on the edge are the same flick: the push is not
    // movement, so the first three stay held and the fourth lets go.
    release_sticky_edge(state);
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 9000) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 9010) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 9020) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 80.0F, 0.0F, 9030) == StickyHold::let_go);
}

void motion_along_an_edge_does_not_let_go() {
    StickyEdge state;
    arm(state);
    // On the bottom edge, a long slide sideways is not a push down, and
    // not a flick down. Movement back into the window counts for neither.
    OA_CHECK(feed(state, 400.0F, kBottom, 400.0F, 0.0F, 1000) == StickyHold::hold);
    OA_CHECK(feed(state, 100.0F, kBottom, -400.0F, 0.0F, 1100) == StickyHold::hold);
    OA_CHECK(feed(state, 100.0F, kBottom, 0.0F, -80.0F, 1200) == StickyHold::hold);
    OA_CHECK(state.holding);
    // A corner counts each edge on its own axis. Sliding, and a short
    // movement out of the left edge, do not let go. The desktop distance
    // past the left edge does.
    OA_CHECK(feed(state, 0.0F, kBottom, 300.0F, 0.0F, 2000) == StickyHold::hold);
    OA_CHECK(feed(state, 0.0F, kBottom, -120.0F, 0.0F, 2100) == StickyHold::hold);
    OA_CHECK(feed(state, 0.0F, kBottom, 0.0F, 0.0F, 2200, -120.0F) == StickyHold::let_go);
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
    // Resting on the edge stays held. A short movement out of it does too.
    OA_CHECK(
        sticky_edge(state, StickySample{0.0F, 15.0F, 0.0F, 0.0F, 10}, 30.0F, 30.0F, limits) ==
        StickyHold::hold
    );
    OA_CHECK(
        sticky_edge(state, StickySample{0.0F, 15.0F, -15.0F, 0.0F, 20}, 30.0F, 30.0F, limits) ==
        StickyHold::hold
    );
    release_sticky_edge(state);
    // After it was let go, a place near the edge is not held. The middle is.
    OA_CHECK(
        sticky_edge(state, StickySample{2.0F, 15.0F, 2.0F, 0.0F, 30}, 30.0F, 30.0F, limits) ==
        StickyHold::let_go
    );
    OA_CHECK(
        sticky_edge(state, StickySample{15.0F, 15.0F, 13.0F, 0.0F, 40}, 30.0F, 30.0F, limits) ==
        StickyHold::hold
    );
}

} // namespace

int main() {
    resting_on_an_edge_stays_held();
    a_push_past_the_threshold_lets_go();
    a_quick_approach_lets_go_and_a_slow_one_holds();
    motion_along_an_edge_does_not_let_go();
    coming_back_near_the_edge_is_not_trapped();
    a_window_too_small_to_inset_still_rearms_in_the_middle();
    return oa::test::check_exit_status();
}
