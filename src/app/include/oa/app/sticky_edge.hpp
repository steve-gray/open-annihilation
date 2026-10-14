// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Whether the pointer is held at a window's edges. The function reads only
// the sample and remembers what it needs in StickyEdge. It does not ask the
// window system for anything. The runtime puts the pointer back on the last
// pixel while the answer is hold, or confines it where a program cannot
// place the pointer, and leaves it free on let_go.
#pragma once

#include <array>
#include <cstdint>

namespace oa::app {

/// Whether the pointer is held at the window's edges or free to leave.
enum class StickyHold : uint8_t {
    hold,   ///< stays at the edge, so it can rest there and the view scrolls
    let_go, ///< free, until it has moved back in away from the edges
};

/// One report of where the pointer is and how it moved, in window points.
struct StickySample {
    float x{};          ///< place across the window, 0 at the left
    float y{};          ///< place down the window, 0 at the top
    float motion_x{};   ///< movement across since the previous sample
    float motion_y{};   ///< movement down since the previous sample
    uint64_t time_ms{}; ///< the sample's time, in milliseconds
    /// How far the desktop pointer is past the window, in the window's
    /// points. Positive is past the right, negative past the left. Zero is
    /// on the edge or inside. This is the push. Movement is not.
    float past_x{};
    /// How far the desktop pointer is past the window. Positive is past the
    /// bottom, negative past the top. Zero is on the edge or inside.
    float past_y{};
};

/// The push that lets the pointer go at Edge stickiness 1.0, in desktop
/// points past one edge within StickyLimits::push_window_ms. An ordinary
/// fast move that runs on past the edge stops short of it; a deliberate
/// shove carries on past it.
inline constexpr float sticky_push_points_per_level = 300.0F;

/// The arrival speed that lets the pointer go at Edge stickiness 1.0, in
/// points per second over StickyLimits::flick_window_ms: 625 points in the
/// last tenth of a second. An ordinary fast move toward the edge arrives
/// slower; a deliberate fling arrives faster.
inline constexpr float sticky_flick_points_per_s_per_level = 6250.0F;

/// How hard a push or a flick has to be before the pointer is let go, and
/// how far back in it must come before it is held again. The defaults are
/// Edge stickiness 1.0's.
struct StickyLimits {
    /// Desktop distance past one edge, added while the push continues, that
    /// lets the pointer go when it arrives within push_window_ms. Zero
    /// never holds.
    float push_points = sticky_push_points_per_level;
    /// How long a push is remembered. A later push starts again.
    uint64_t push_window_ms = 500;
    /// Arrival speed, in points per second, that lets the pointer go. Zero
    /// never holds.
    float flick_points_per_s = sticky_flick_points_per_s_per_level;
    /// How far back the approach toward an edge counts toward a flick.
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

    /// Per-edge push: desktop distance past that edge since the push started.
    struct Push {
        float points = 0.0F;     ///< distance past this edge in this push
        uint64_t started_ms = 0; ///< when the push's first distance arrived
        bool running = false;    ///< a push is open on this edge
    };

    /// Left, right, top, bottom.
    std::array<Push, 4> push{};
    /// Recent movement toward each edge, including the approach before the
    /// pointer reached it, so a flick can be summed. Oldest samples merge
    /// into the next when the list is full.
    static constexpr int kFlickSlots = 16;

    struct Flick {
        uint64_t time_ms = 0;           ///< when this movement arrived
        std::array<float, 4> outward{}; ///< points toward each edge
    };

    std::array<Flick, kFlickSlots> flick{};
    int flick_count = 0; ///< how many flick samples are kept
    int flick_next = 0;  ///< where the next flick sample is written
};

/// Returns the limits of a sticky edge as firm as an Edge stickiness level.
///
/// The push and the flick scale with the level, so 5.0 needs five times
/// the push and the flick of 1.0, and 0.0 never holds. How long a push and
/// a flick are remembered, and how far back in the pointer must come, stay
/// as they are.
///
/// @param level the Edge stickiness, 0.0 to 5.0; below 0.0 counts as 0.0
/// @return the limits
[[nodiscard]] StickyLimits sticky_limits(float level) noexcept;

/// Decides whether the pointer stays held at the window's edges.
///
/// Resting on an edge, with no movement and no distance past it, stays held
/// however long it lasts. The push is the distance past an edge, from
/// past_x and past_y, and from a place that is itself past the last pixel.
/// Those are one distance told two ways, so the greater one is counted
/// once, and only in that edge's direction. It lets go when the distance
/// adds up to the push within the push's time. The flick is the movement
/// toward an edge over the flick's time, including the samples from before
/// the pointer reached the edge, and it lets go when that sum reaches the
/// flick as the pointer arrives. Movement along an edge counts for neither.
/// A distance of zero never grows a push and never starts its time again.
/// After it lets go, it holds again only once the pointer is inside the
/// window and at least the rearm distance from every edge. A window too
/// small for that distance re-arms at its middle. Limits whose push or
/// flick is zero never hold.
///
/// @param state what earlier samples decided; updated
/// @param sample where the pointer is, how it moved, how far past the edge
///        the desktop pointer is, and when
/// @param width the window's width in points, the same units as sample
/// @param height the window's height in points
/// @param limits how hard a push or a flick lets go, and how far back in
///        the pointer must come
/// @return hold while the pointer should stay at the edges, let_go when it
///         is free
[[nodiscard]] StickyHold sticky_edge(
    StickyEdge& state, StickySample sample, float width, float height, StickyLimits limits = {}
);

/// Forgets a hold, so the pointer is free until it has moved back in.
///
/// A menu, the pause screen, a window without the focus and Window edge
/// Capture or Free all call this. The next samples hold again only from
/// inside.
///
/// @param state what earlier samples decided; left let go
void release_sticky_edge(StickyEdge& state);

} // namespace oa::app
