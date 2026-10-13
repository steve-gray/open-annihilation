// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Middle-click panning, apart from the match: the gesture's states and the
// speed the view takes from where the pointer sits relative to the anchor.
// The match stores the state, scrolls, and decides which presses count.
#pragma once

#include <cstdint>

namespace oa::app::middle_pan {

/// Points a middle press may move and still count as a click when released.
inline constexpr float kClickSlop = 4.0F;
/// Points from the anchor inside which the view stays still.
inline constexpr float kDeadZone = 12.0F;
/// Points past the dead zone at which the view reaches the scroll speed.
inline constexpr float kTopSpeedReach = 128.0F;

/// Where a middle-click pan stands.
enum class Phase : uint8_t {
    idle,     ///< no pan
    clicked,  ///< a click is down, or a click was released and the pan runs
    dragging, ///< the press moved past a click and the pan runs until release
};

/// One pan. The anchor is where the middle press landed, in screen points.
struct State {
    Phase phase{Phase::idle};
    bool held{false}; ///< the middle button is still down
    float anchor_x{0.0F};
    float anchor_y{0.0F};
    float press_x{0.0F};
    float press_y{0.0F};
};

/// What the pointer or the keyboard did.
enum class Gesture : uint8_t {
    middle_down, ///< the middle button went down
    middle_up,   ///< the middle button came up
    move,        ///< the pointer moved
    other,       ///< another button, the wheel, a key, or the window lost focus
};

/// The state after a gesture, and whether that gesture must not reach the game.
struct Result {
    State state{};
    bool swallow{false};
};

/// The share of the scroll speed the view takes, one axis each. Positive x is
/// screen right and positive y is screen down. The length is at most 1, so a
/// diagonal is no faster than an arrow key.
struct Speed {
    float x{0.0F};
    float y{0.0F};
};

/// Advances the pan by one gesture.
///
/// A middle press on a place that may start one (`can_start`) begins a click.
/// Releasing it without moving more than kClickSlop leaves the pan running.
/// Moving further while the button is held drags, and releasing then stops.
/// Any other button, the wheel, a key or a loss of focus stops a running pan
/// and is swallowed. With the setting off, nothing starts and nothing is
/// swallowed. A middle press while a pan is already running only stops it.
///
/// @param state the pan before the gesture
/// @param gesture what happened
/// @param x the pointer's screen point, for a pointer gesture
/// @param y the pointer's screen point, for a pointer gesture
/// @param enabled the Middle-click pans setting
/// @param can_start the press is on the battlefield, where a pan may begin
/// @return the pan after the gesture, and whether the gesture is consumed
[[nodiscard]] Result
apply(State state, Gesture gesture, float x, float y, bool enabled, bool can_start) noexcept;

/// Returns how fast the view scrolls from the anchor toward the pointer.
///
/// Inside the dead zone the speed is zero. It then rises in a straight line
/// with the distance, and reaches the full scroll speed kTopSpeedReach points
/// past the dead zone.
///
/// @param anchor_x where the pan was started
/// @param anchor_y where the pan was started
/// @param pointer_x where the pointer is
/// @param pointer_y where the pointer is
/// @return the share of the scroll speed on each screen axis
[[nodiscard]] Speed
scroll_share(float anchor_x, float anchor_y, float pointer_x, float pointer_y) noexcept;

} // namespace oa::app::middle_pan
