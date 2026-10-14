// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sticky window edge over a made-up pointer: resting on an edge stays
// held, a push past the threshold within its time lets go and one spread
// past that time does not, a quick approach lets go and a slow one holds,
// movement along an edge does not, and coming back in near the edge is not
// held until the pointer has moved in. Edge stickiness scales the push and
// the flick: 0.0 never holds, 1.0 holds what the first sticky edge let go,
// and 5.0 needs five times 1.0's push and flick.
#include "oa/app/sticky_edge.hpp"

#include "oa/test/check.hpp"

#include <algorithm>
#include <cstdint>
#include <tuple>

namespace {

using oa::app::StickyEdge;
using oa::app::StickyHold;
using oa::app::StickyLimits;
using oa::app::StickySample;
using oa::app::release_sticky_edge;
using oa::app::sticky_edge;
using oa::app::sticky_limits;

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
/// @param limits the thresholds; Edge stickiness 1.0's when omitted
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
    // Movement on the edge is not a push. 500 points is under the flick.
    OA_CHECK(feed(state, kRight, 300.0F, 500.0F, 0.0F, 1000) == StickyHold::hold);
    // 299 points past the edge stays held. The last point, within the
    // push's time, reaches 300.
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 2000, 299.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 2100, 1.0F) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    // A sample at exactly half a second still belongs to the same push.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 3000, 150.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 3500, 150.0F) == StickyHold::let_go);
    // A rest grows nothing and does not start the time again, so the next
    // distance within the window still belongs to the push.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 4000, 150.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 4400) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 4490, 150.0F) == StickyHold::let_go);
    // A push spread past the window starts again, so 150 and then 150 does
    // not let go.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 5000, 150.0F) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 5600, 150.0F) == StickyHold::hold);
    OA_CHECK(state.holding);
    // One distance of 300 on its own lets go. So does the same distance
    // told by a place past the last pixel, added up while the push goes on.
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 5700, 300.0F) == StickyHold::let_go);
    arm(state);
    OA_CHECK(feed(state, kWidth + 100.0F, 300.0F, 0.0F, 0.0F, 6000) == StickyHold::hold);
    OA_CHECK(feed(state, kWidth + 100.0F, 300.0F, 0.0F, 0.0F, 6100) == StickyHold::hold);
    OA_CHECK(feed(state, kWidth + 100.0F, 300.0F, 0.0F, 0.0F, 6200) == StickyHold::let_go);
}

void a_quick_approach_lets_go_and_a_slow_one_holds() {
    StickyEdge state;
    arm(state);
    // Arriving at 624 points in the last 100 ms stays held. 625 lets go.
    OA_CHECK(feed(state, 400.0F, kBottom, 0.0F, 624.0F, 5000) == StickyHold::hold);
    release_sticky_edge(state);
    arm(state);
    OA_CHECK(feed(state, 400.0F, kBottom, 0.0F, 625.0F, 5100) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    // The approach starts inside the window. The samples before the edge
    // count, and the one that arrives lets go.
    arm(state);
    OA_CHECK(feed(state, 250.0F, 300.0F, 200.0F, 0.0F, 6000) == StickyHold::hold);
    OA_CHECK(feed(state, 550.0F, 300.0F, 300.0F, 0.0F, 6030) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 249.0F, 0.0F, 6060) == StickyHold::let_go);
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
    OA_CHECK(feed(state, kRight, 300.0F, 160.0F, 0.0F, 9000) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 160.0F, 0.0F, 9010) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 160.0F, 0.0F, 9020) == StickyHold::hold);
    OA_CHECK(feed(state, kRight, 300.0F, 160.0F, 0.0F, 9030) == StickyHold::let_go);
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
    OA_CHECK(feed(state, 0.0F, kBottom, 0.0F, 0.0F, 2200, -300.0F) == StickyHold::let_go);
}

void coming_back_near_the_edge_is_not_trapped() {
    StickyEdge state;
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 625.0F, 0.0F, 1000) == StickyHold::let_go);
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

/// The first sticky edge's limits, before Edge stickiness: a 120-point push
/// within half a second, or arriving at 2500 points a second.
constexpr StickyLimits kEarlierLimits{120.0F, 500, 2500.0F, 100, 24.0F};

void the_level_scales_the_push_and_the_flick() {
    // 1.0 is the defaults; 5.0 is five times its push and flick; the times
    // and the rearm distance stay. Below 0.0 counts as 0.0.
    const StickyLimits one = sticky_limits(1.0F);
    const StickyLimits defaults{};
    OA_CHECK(one.push_points == defaults.push_points);
    OA_CHECK(one.flick_points_per_s == defaults.flick_points_per_s);
    OA_CHECK(one.push_window_ms == defaults.push_window_ms);
    OA_CHECK(one.flick_window_ms == defaults.flick_window_ms);
    OA_CHECK(one.rearm_points == defaults.rearm_points);
    const StickyLimits five = sticky_limits(5.0F);
    OA_CHECK(five.push_points == 5.0F * one.push_points);
    OA_CHECK(five.flick_points_per_s == 5.0F * one.flick_points_per_s);
    OA_CHECK(five.push_window_ms == one.push_window_ms);
    OA_CHECK(five.flick_window_ms == one.flick_window_ms);
    OA_CHECK(five.rearm_points == one.rearm_points);
    OA_CHECK(sticky_limits(2.5F).push_points == 2.5F * one.push_points);
    OA_CHECK(sticky_limits(-1.0F).push_points == 0.0F);
    OA_CHECK(sticky_limits(-1.0F).flick_points_per_s == 0.0F);
    // 1.0 is about two and a half times as firm as the first sticky edge.
    OA_CHECK(one.push_points == 2.5F * kEarlierLimits.push_points);
    OA_CHECK(one.flick_points_per_s == 2.5F * kEarlierLimits.flick_points_per_s);
}

void zero_never_holds() {
    const StickyLimits zero = sticky_limits(0.0F);
    StickyEdge state;
    // Not in the middle, not resting on an edge, not after a slow walk.
    OA_CHECK(feed(state, 400.0F, 300.0F, 0.0F, 0.0F, 0, 0.0F, 0.0F, zero) == StickyHold::let_go);
    OA_CHECK(!state.holding);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 100, 0.0F, 0.0F, zero) == StickyHold::let_go);
    for (uint64_t step = 0; step < 10; ++step)
        OA_CHECK(
            feed(
                state,
                400.0F + 10.0F * static_cast<float>(step),
                300.0F,
                10.0F,
                0.0F,
                200 + step * 100,
                0.0F,
                0.0F,
                zero
            ) == StickyHold::let_go
        );
    OA_CHECK(!state.holding);
    // A pointer held at 1.0 is let go at once when the level drops to 0.0.
    arm(state);
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 2000, 0.0F, 0.0F, zero) == StickyHold::let_go);
    OA_CHECK(!state.holding);
}

void one_holds_what_the_first_sticky_edge_let_go() {
    // A 200-point push past the edge: the first sticky edge let it go, 1.0
    // holds it.
    StickyEdge earlier;
    StickyEdge state;
    OA_CHECK(
        feed(earlier, 400.0F, 300.0F, 0.0F, 0.0F, 0, 0.0F, 0.0F, kEarlierLimits) == StickyHold::hold
    );
    arm(state);
    OA_CHECK(
        feed(earlier, kRight, 300.0F, 0.0F, 0.0F, 1000, 200.0F, 0.0F, kEarlierLimits) ==
        StickyHold::let_go
    );
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 1000, 200.0F) == StickyHold::hold);
    // An ordinary fast move to the edge, 450 points in its last tenth of a
    // second: the first sticky edge let it go, 1.0 holds it.
    release_sticky_edge(earlier);
    release_sticky_edge(state);
    OA_CHECK(
        feed(earlier, 400.0F, 300.0F, 0.0F, 0.0F, 2000, 0.0F, 0.0F, kEarlierLimits) ==
        StickyHold::hold
    );
    arm(state);
    for (StickyEdge* edge : {&earlier, &state}) {
        const StickyLimits limits = edge == &earlier ? kEarlierLimits : StickyLimits{};
        std::ignore = feed(*edge, 500.0F, 300.0F, 150.0F, 0.0F, 3000, 0.0F, 0.0F, limits);
        std::ignore = feed(*edge, 650.0F, 300.0F, 150.0F, 0.0F, 3030, 0.0F, 0.0F, limits);
    }
    OA_CHECK(
        feed(earlier, kRight, 300.0F, 149.0F, 0.0F, 3060, 0.0F, 0.0F, kEarlierLimits) ==
        StickyHold::let_go
    );
    OA_CHECK(feed(state, kRight, 300.0F, 149.0F, 0.0F, 3060) == StickyHold::hold);
    OA_CHECK(state.holding);
}

void five_needs_five_times_one() {
    const StickyLimits five = sticky_limits(5.0F);
    StickyEdge state;
    const auto arm_five = [&] {
        OA_CHECK(feed(state, 400.0F, 300.0F, 0.0F, 0.0F, 0, 0.0F, 0.0F, five) == StickyHold::hold);
    };
    // The push: 1499 points past the edge hold, 1500 let go.
    arm_five();
    OA_CHECK(
        feed(state, kRight, 300.0F, 0.0F, 0.0F, 1000, 1499.0F, 0.0F, five) == StickyHold::hold
    );
    OA_CHECK(feed(state, kRight, 300.0F, 0.0F, 0.0F, 1100, 1.0F, 0.0F, five) == StickyHold::let_go);
    // The flick: 3124 points in the last tenth of a second hold, 3125 let go.
    release_sticky_edge(state);
    arm_five();
    OA_CHECK(
        feed(state, kRight, 300.0F, 3124.0F, 0.0F, 2000, 0.0F, 0.0F, five) == StickyHold::hold
    );
    release_sticky_edge(state);
    arm_five();
    OA_CHECK(
        feed(state, kRight, 300.0F, 3125.0F, 0.0F, 3000, 0.0F, 0.0F, five) == StickyHold::let_go
    );
    // What lets 1.0 go, 5.0 holds.
    release_sticky_edge(state);
    arm_five();
    OA_CHECK(
        feed(state, kRight, 300.0F, 625.0F, 0.0F, 4000, 300.0F, 0.0F, five) == StickyHold::hold
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
    the_level_scales_the_push_and_the_flick();
    zero_never_holds();
    one_holds_what_the_first_sticky_edge_let_go();
    five_needs_five_times_one();
    return oa::test::check_exit_status();
}
