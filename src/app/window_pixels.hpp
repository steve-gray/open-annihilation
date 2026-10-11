// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The window-pixel path the Game files screen and the folder chooser share,
// beside the renderer's state they put back (render_state.hpp): the canvas
// read from the window (its pixels, the pixels a window point holds and the
// safe area), a painted frame shown on the window in its own pixels, and
// fingers, the pointer and the wheel read as one press in canvas pixels.
// What a press does is each screen's own.
#pragma once

#include "render_state.hpp"

#include "oa/ui/game_files.hpp"
#include "oa/ui/paint/painter.hpp"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <string_view>

namespace oa::app {

// Named once, whichever app header that draws with the painter is included first.
#ifndef OA_APP_UI_PAINT
#define OA_APP_UI_PAINT
namespace paint = oa::ui::paint;
#endif

class RendererHost;

/// How far a press may move over the rows before it scrolls them, in points.
inline constexpr float drag_threshold_points = 10.0F;
/// How far one notch of the wheel scrolls the rows, in points.
inline constexpr float wheel_notch_points = 40.0F;

/// Reads a window's canvas into a viewport: the renderer's output in pixels, the canvas pixels
/// a window point holds, and the window's safe area in canvas pixels, rounded to the nearest.
///
/// @param window the window
/// @param renderer its renderer
/// @param[in,out] viewport the canvas last read; left as it was when the window or the
///     renderer has no size
/// @return true when the viewport changed
bool read_window_viewport(
    SDL_Window* window, SDL_Renderer* renderer, oa::ui::game_files::Viewport& viewport
);

/// A painted frame shown on a window in its own pixels, through a streaming texture of the
/// canvas's size that is kept between frames and made again when the size changes.
class WindowFrame {
  public:

    /// Makes the frame for a renderer.
    ///
    /// @param renderer the renderer it draws with
    /// @param owner the screen it shows, as a message names it: "the folder chooser"
    WindowFrame(SDL_Renderer* renderer, std::string_view owner) noexcept;

    /// Lets go of the texture.
    ~WindowFrame();

    WindowFrame(const WindowFrame&) = delete;
    WindowFrame& operator=(const WindowFrame&) = delete;

    /// Lets go of the texture, which a renderer whose device was reset has lost; the next
    /// frame makes another.
    void drop_texture() noexcept;

    /// Shows a canvas on the window: copies it into the texture, draws to the window in its
    /// own pixels (RenderState::use_window_pixels), clears it to the screen's background
    /// colour, draws the texture over the whole window without blending or smoothing,
    /// presents, and tells the host a frame was presented.
    ///
    /// @param canvas the painted frame, of the renderer's output size
    /// @param state the renderer's state the screen keeps to put back
    /// @param host what made the renderer, told of the frame; may be null
    /// @return false for an empty canvas or when the texture cannot be made, which is reported
    bool show(const paint::Canvas& canvas, const RenderState& state, RendererHost* host);

  private:

    SDL_Renderer* renderer_{}; ///< the renderer
    std::string_view owner_{}; ///< the screen it shows, for messages
    SDL_Texture* texture_{};   ///< the frame's texture, null until the first frame
    int width_{};              ///< the texture's width
    int height_{};             ///< the texture's height
};

/// A press of a finger or the pointer's left button under way on a screen drawn in the
/// window's pixels.
struct WindowPress {
    bool active{};       ///< a press is under way
    bool finger{};       ///< it is a finger's
    SDL_TouchID touch{}; ///< the finger's device
    SDL_FingerID id{};   ///< the finger
    float start_x{};     ///< where it went down, canvas pixels
    float start_y{};     ///< where it went down, canvas pixels
    float last_y{};      ///< where it was last taken as a scroll, canvas pixels
    bool scrolling{};    ///< it scrolls the rows
};

/// What an event does to the press on a screen drawn in the window's pixels.
enum class PointerStep : uint8_t {
    none,   ///< nothing
    down,   ///< a finger or the left button went down
    move,   ///< the press under way moved
    up,     ///< the press under way came up
    cancel, ///< the system cancelled the finger of the press under way
    wheel,  ///< the wheel turned
};

/// One event as the pointer on a screen drawn in the window's pixels.
struct PointerEvent {
    PointerStep step{PointerStep::none}; ///< what it does
    float x{};                           ///< where, canvas pixels
    float y{};                           ///< where, canvas pixels
    bool finger{};                       ///< it is a finger's
    SDL_TouchID touch{};                 ///< the finger's device
    SDL_FingerID id{};                   ///< the finger
    float wheel_points{};                ///< how far the wheel scrolls, points; positive is down
};

/// Reads an event as the pointer on a screen drawn in the window's pixels. A finger's place
/// is its share of the canvas, the pointer's its window points times the canvas pixels a
/// point holds. A finger going down is a press from any device (the screen says which
/// devices it takes); its moves, its lift and its cancelling count only for the finger of
/// the press under way. The left button going down is a press, and the pointer's moves and
/// the button coming up count only while a press of the pointer is under way. The mouse's
/// events SDL makes from a finger are left out, the finger itself being taken. A notch of the
/// wheel away from the player scrolls up by wheel_notch_points, a flipped wheel the other
/// way.
///
/// @param event the event
/// @param viewport the canvas
/// @param press the press under way
/// @return what the event does; PointerStep::none for any other event
[[nodiscard]] PointerEvent read_pointer(
    const SDL_Event& event, const oa::ui::game_files::Viewport& viewport, const WindowPress& press
) noexcept;

/// Tells whether a touch device's fingers reach a screen drawn in the window's pixels: a
/// touch screen's, never the pointer's or a pen's made into fingers.
///
/// @param device the device
/// @return true for a direct touch device
[[nodiscard]] bool touch_screen(SDL_TouchID device) noexcept;

/// Tells whether a press under way has become a scroll of the rows at a point: it went down
/// over the rows and has moved farther up or down than drag_threshold_points.
///
/// @param press the press under way
/// @param rows the scrolling region, canvas pixels; empty when nothing scrolls
/// @param y where the press is, canvas pixels
/// @param px_per_point canvas pixels per point
/// @return true when it scrolls the rows from here on
[[nodiscard]] bool press_scrolls(
    const WindowPress& press, const oa::ui::kit::Rect& rows, float y, float px_per_point
) noexcept;

/// Returns a place on the canvas as the whole canvas pixel nearest it.
///
/// @param x across, canvas pixels
/// @param y down, canvas pixels
/// @return the pixel
[[nodiscard]] oa::ui::kit::Point canvas_point(float x, float y) noexcept;

} // namespace oa::app
