// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Helpers for the native checks of the OA layer's screens as the window
// shows them: the window frame expected while a screen shows over the front
// end, the pixels a presented frame differs in outside the software cursor's
// square, and pointer and finger events at a window pixel, so that a check
// presses a screen where the layer shows it at any window size.
#pragma once

#include "oa_layer.hpp"

#include "oa/ui/display_layout.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/layout.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oa::app {

/// A window size a check shows a screen of the OA layer at, and the size
/// class and scale the layer gives the screen there.
struct LayerWindow {
    int32_t width{};                     ///< the window's width, in pixels
    int32_t height{};                    ///< the window's height, in pixels
    oa::ui::kit::SizeClass size_class{}; ///< the class the screen is laid out at
    int32_t scale{};                     ///< window pixels a point
};

/// The windows the notice and prompt checks show their screens at beyond
/// 640 by 480: 1280 by 720, Large at 1×, and 1920 by 1080, Regular at 2×.
inline constexpr std::array<LayerWindow, 2> larger_layer_windows{{
    {1280, 720, oa::ui::kit::SizeClass::large, 1},
    {1920, 1080, oa::ui::kit::SizeClass::regular, 2},
}};

/// Returns a size class's name, as the checks print it.
///
/// @param size_class the class
/// @return "Compact", "Regular" or "Large"
[[nodiscard]] std::string_view size_class_name(oa::ui::kit::SizeClass size_class) noexcept;

/// How far round the pointer the software cursor may draw, in the front end's
/// picture's pixels.
inline constexpr int32_t layer_cursor_reach = 40;

/// The pointer as the window shows it: where it rests, and how far round it
/// the software cursor may draw.
struct LayerPointer {
    oa::ui::display_layout::Point at{}; ///< where it rests, in window pixels
    int32_t reach{};                    ///< the cursor's reach round it, in window pixels
};

/// Returns the pointer resting at a point of the front end's picture as the
/// window shows it: the point through the picture's rectangle, and
/// layer_cursor_reach picture pixels as the picture's letterbox scale makes
/// them, rounded up.
///
/// @param view what the layer's screens are placed on (OaLayer::view)
/// @param picture_point the pointer, in the picture's pixels
/// @return the pointer, in window pixels
[[nodiscard]] LayerPointer
window_pointer(const LayerView& view, oa::ui::display_layout::Point picture_point) noexcept;

/// Returns the window frame expected while a screen of the OA layer shows
/// over the front end: the frame presented with no OA screen open, every
/// pixel darkened as a modal screen's backdrop darkens it (the backdrop's
/// colour blended at the main menu's opacity, once for each such screen),
/// with the screen's own drawing copied at its placement.
///
/// The frames are compared at the display gamma the window shows; the
/// darkening is the frame's own, so the comparison holds where that gamma
/// leaves the picture's colours as they are, as the checks run.
///
/// @param closed the window's frame with no OA screen open
/// @param drawing the screen drawn at its size class and the view's scale,
///     as large as its placement
/// @param placement where the layer shows the screen, in window pixels
/// @param darkenings the modal screens with a backdrop that show
/// @return the expected frame, the size of the closed one
[[nodiscard]] oa::ui::frontend_renderer::Surface expected_layer_frame(
    const oa::ui::frontend_renderer::Surface& closed,
    const oa::ui::frontend_renderer::Surface& drawing,
    const LayerPlacement& placement,
    uint32_t darkenings = 1
);

/// Counts the pixels in which a presented window frame differs from the
/// expected one, every pixel of the window compared but those in the square
/// round the pointer where the software cursor draws.
///
/// @param presented the window's frame as presented
/// @param expected the frame expected (expected_layer_frame)
/// @param pointer where the pointer rests (window_pointer)
/// @return the pixels that differ; every pixel when the frames' sizes differ
[[nodiscard]] std::size_t layer_differences(
    const oa::ui::frontend_renderer::Surface& presented,
    const oa::ui::frontend_renderer::Surface& expected,
    const LayerPointer& pointer
) noexcept;

/// Returns the window pixel in the middle of one of a screen's points,
/// through the screen's placement: left + point × scale + scale / 2, and
/// the same down.
///
/// @param placement where the layer shows the screen
/// @param point the point, in the screen's points
/// @return the window pixel
[[nodiscard]] oa::ui::display_layout::Point
layer_window_pixel(const LayerPlacement& placement, oa::ui::kit::Point point) noexcept;

/// Returns a mouse event at the middle of a window pixel, in the window's
/// coordinates (its points), as SDL reports the pointer there.
///
/// @param window the window
/// @param sdl_renderer its renderer, whose output size gives the window's pixels
/// @param type SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN or SDL_EVENT_MOUSE_BUTTON_UP
/// @param pixel the window pixel
/// @param button the button pressed or released; 0 for a motion
/// @return the event, to dispatch
[[nodiscard]] SDL_Event window_pointer_event(
    SDL_Window* window,
    SDL_Renderer* sdl_renderer,
    SDL_EventType type,
    oa::ui::display_layout::Point pixel,
    uint8_t button
) noexcept;

/// Returns a finger's event at the middle of a window pixel, its place a
/// share of the window's size, as SDL reports a finger there.
///
/// @param window the window
/// @param sdl_renderer its renderer, whose output size gives the window's pixels
/// @param type SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_MOTION or SDL_EVENT_FINGER_UP
/// @param pixel the window pixel
/// @param touch the touch device the finger is on
/// @param finger the finger
/// @return the event, to dispatch
[[nodiscard]] SDL_Event window_finger_event(
    SDL_Window* window,
    SDL_Renderer* sdl_renderer,
    SDL_EventType type,
    oa::ui::display_layout::Point pixel,
    SDL_TouchID touch,
    SDL_FingerID finger
) noexcept;

} // namespace oa::app
