// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Whether the pointer is held at a window's edges. The function reads only
// the pointer's place, its motion and the time, and remembers what it needs
// in StickyEdge. It does not ask the window system for anything: the runtime
// confines the pointer while the answer is hold, and lets it go on let_go.
#pragma once

#include <array>
#include <cstdint>

namespace oa::app {

/// Whether the pointer is held inside the window or free to leave it.
enum class StickyHold : uint8_t {
    hold,   ///< confined, so it can rest on an edge and the view scrolls
    let_go, ///< free, until it has moved back in away from the edges
};

/// One report of where the pointer is and how it moved, in window points.
struct StickySample {
    float x{};          ///< place across the window, 0 at the left
    float y{};          ///< place down the window, 0 at the top
    float motion_x{};   ///< movement across since the previous sample
    float motion_y{};   ///< movement down since the previous sample
    uint64_t time_ms{}; ///< the sample's time, in milliseconds
};

/// How hard a push or a flick has to be before the pointer is let go, and
/// how far back in it must come before it is held again.
struct StickyLimits {
    /// Outward movement, in one edge's direction, that lets the pointer go
    /// when it arrives within push_window_ms.
    float push_points = 120.0F;
    /// How long a push is remembered. Movement after this starts a new push.
    uint64_t push_window_ms = 500;
    /// Arrival speed, in points per second, that lets the pointer go.
    float flick_points_per_s = 2500.0F;
    /// How far back an arrival's movement counts toward a flick.
    uint64_t flick_window_ms = 100;
    /// How far from every edge the pointer must be, after it was let go,
    /// before it is held again.
    float rearm_points = 24.0F;
};

/// What sticky_edge remembers between samples. Starts let go, so the first
/// samples hold only once the pointer is inside and away from the edges.
struct StickyEdge {
    /// The pointer is held. False after let go, until it has moved back in.
    bool holding = false;

    /// Per-edge push: outward points since the push started, and when.
    struct Push {
        float points = 0.0F;     ///< outward points in this push
        uint64_t started_ms = 0; ///< when the push's first point arrived
        bool running = false;    ///< a push is open on this edge
    };

    /// Left, right, top, bottom.
    std::array<Push, 4> push{};
    /// Recent outward movement while the pointer was on an edge, so a flick
    /// can be summed. Oldest samples merge into the next when the list is
    /// full, so a fast stream of small movements is not forgotten.
    static constexpr int kFlickSlots = 16;

    struct Flick {
        uint64_t time_ms = 0;           ///< when this movement arrived
        std::array<float, 4> outward{}; ///< outward points on each edge
    };

    std::array<Flick, kFlickSlots> flick{};
    int flick_count = 0; ///< how many flick samples are kept
    int flick_next = 0;  ///< where the next flick sample is written
};

/// Decides whether the pointer stays held at the window's edges.
///
/// Resting on an edge, with no movement, stays held however long it lasts.
/// Movement past an edge counts only in that edge's direction: along an
/// edge it counts nothing. It lets go when that movement adds up to the
/// push, within the push's time, or when the pointer arrives at the edge
/// at the flick's speed over the flick's time. After it lets go, it holds
/// again only once the pointer is inside the window and at least the rearm
/// distance from every edge. A window too small for that distance re-arms
/// at its middle.
///
/// @param state what earlier samples decided; updated
/// @param sample where the pointer is, how it moved and when
/// @param width the window's width in points, the same units as sample
/// @param height the window's height in points
/// @param limits how hard a push or a flick lets go, and how far back in
///        the pointer must come
/// @return hold while the pointer should be confined, let_go when it is free
[[nodiscard]] StickyHold sticky_edge(
    StickyEdge& state, StickySample sample, float width, float height, StickyLimits limits = {}
);

/// Forgets a hold, so the pointer is free until it has moved back in.
///
/// A menu, the pause screen, a window without the focus and the Free
/// setting all call this. The next samples hold again only from inside.
///
/// @param state what earlier samples decided; left let go
void release_sticky_edge(StickyEdge& state);

} // namespace oa::app
