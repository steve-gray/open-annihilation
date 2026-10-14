// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The window's size in points, which every Open Annihilation screen lays out
// from, and the drawing surface in pixels, which it draws at. A 2× display
// shows the same arrangement as the same window at one pixel per point, with
// sharper edges. SDL 3.4.16's dummy driver has no display scale, so a check
// names one with the OA_PIXEL_DENSITY hint.
#pragma once

#include <SDL3/SDL.h>

namespace oa::app {

/// The hint a check sets, to a whole number from 2 to 8, before the window
/// opens. Unset, or 1, leaves the window at the density SDL reports.
inline constexpr const char* pixel_density_hint = "OA_PIXEL_DENSITY";

/// The window as a layout measures it and as it is drawn.
struct WindowSurface {
    int points_width{};  ///< the layout width, in points
    int points_height{}; ///< the layout height, in points
    int pixel_width{};   ///< the drawing surface, in pixels
    int pixel_height{};  ///< the drawing surface, in pixels
    int sdl_width{};     ///< SDL_GetWindowSize's width, for distances it reports
    int sdl_height{};    ///< SDL_GetWindowSize's height
    float density{1.0F}; ///< canvas pixels per point; at least 1
};

/// The positive whole density OA_PIXEL_DENSITY names, from 1 to 8.
///
/// A missing hint, or one that is not a whole number in that range, is 1.
/// The hint is read with SDL_GetHint, which also sees the environment variable
/// of that name. 8 is the greatest a check may ask, so a mistyped value cannot
/// grow the window without limit.
///
/// @return the density, at least 1
[[nodiscard]] int asked_pixel_density() noexcept;

/// Grows a window SDL draws at one pixel per point, so its drawing surface
/// holds the density OA_PIXEL_DENSITY names.
///
/// The size the window has when this is called is remembered as its size in
/// points, and the window is resized to that many pixels per point. A display
/// that already reports more pixels than points is left as it is: its points
/// are already the window's. Called again for the same window and density, it
/// does not grow the window a second time. A null window, or a density of 1,
/// forgets a size remembered for that window.
///
/// @param window the window, after its size has been chosen; may be null
void apply_asked_pixel_density(SDL_Window* window);

/// The window's points and its drawing surface.
///
/// The drawing surface is the larger of SDL_GetWindowSizeInPixels and
/// SDL_GetRenderOutputSize, so a drawable that has already grown past the
/// window's reported pixels is the one the layout uses. The points are
/// SDL_GetWindowSize, unless apply_asked_pixel_density grew this window, in
/// which case they are that remembered size, following a later resize. The
/// density is the surface's pixels per point, and at least 1. A window with
/// no size leaves every field 0 and the density 1.
///
/// @param window the window; may be null
/// @param renderer its renderer; may be null, and then only the window's pixels count
/// @return the surface; empty when the window has no size
[[nodiscard]] WindowSurface window_surface(SDL_Window* window, SDL_Renderer* renderer);

/// A distance SDL_GetWindowSize's coordinates measure, in canvas pixels.
///
/// When that size is the window's points, the distance is multiplied by the
/// density. When a headless density grew the window, those coordinates are
/// already canvas pixels, and the distance is scaled into them.
///
/// @param surface the window
/// @param sdl_distance the distance, in SDL_GetWindowSize's coordinates
/// @param across true for a horizontal distance, false for a vertical one
/// @return the distance, in canvas pixels
[[nodiscard]] int
surface_canvas_pixels(const WindowSurface& surface, int sdl_distance, bool across) noexcept;

/// Where a frame is drawn in a drawing surface, as SDL's letterbox or whole
/// steps place it (SDL_SetRenderLogicalPresentation).
///
/// Whole steps use the greatest whole scale that fits, at least 1, and centre
/// the frame. A letterbox fills the surface's width or its height, whichever
/// keeps the frame's shape, and centres the other side. A surface of the
/// frame's own shape fills the surface. An empty size draws nothing.
///
/// @param output_width the drawing surface's width, in pixels
/// @param output_height its height, in pixels
/// @param frame_width the frame's width, in its own pixels
/// @param frame_height its height, in its own pixels
/// @param whole_steps true for whole steps, false for a letterbox
/// @return the frame's place, in drawing-surface pixels; empty when a size is empty
[[nodiscard]] SDL_FRect frame_letterbox(
    int output_width, int output_height, int frame_width, int frame_height, bool whole_steps
) noexcept;

/// A frame placed on a window's drawing surface.
struct PlacedFrame {
    WindowSurface surface{}; ///< the window
    SDL_FRect drawn{};       ///< where the frame is drawn, in canvas pixels
    /// True when SDL's logical presentation already draws the frame there, on
    /// this surface, so the frame can go through it unchanged.
    bool sdl_matches{false};
};

/// Places a frame on the window's drawing surface.
///
/// The place is frame_letterbox of the surface. When SDL's logical
/// presentation is that place, of this frame, on a surface of the same size,
/// the returned rectangle is SDL's own, so a window at one pixel per point
/// keeps the rectangle it had.
///
/// @param window the window; may be null
/// @param renderer its renderer; may be null
/// @param frame_width the frame's width, in its own pixels
/// @param frame_height its height, in its own pixels
/// @param whole_steps true for whole steps, false for a letterbox
/// @return the place; drawn empty when the surface or the frame has no size
[[nodiscard]] PlacedFrame place_frame(
    SDL_Window* window, SDL_Renderer* renderer, int frame_width, int frame_height, bool whole_steps
) noexcept;

} // namespace oa::app
