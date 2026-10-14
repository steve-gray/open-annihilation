// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The window's points and its drawing surface (window_surface.hpp). The
// remembered point size lives in this file alone: a copy in each caller would
// disagree about whether the hint had already grown the window.
#include "oa/app/window_surface.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace oa::app {
namespace {

/// The greatest density a check may name. Above it the hint is read as this.
constexpr int kGreatestAskedDensity = 8;

/// How close two places must be, in pixels, to be the one SDL already uses.
constexpr float kPlaceEpsilon = 0.05F;

/// The point size apply_asked_pixel_density remembered for one window.
struct AskedWindow {
    SDL_WindowID id{};   ///< the window; 0 is none
    int density{1};      ///< the density it was grown by; 1 was not grown
    int points_width{};  ///< the points it had before it grew
    int points_height{}; ///< the points it had before it grew
};

/// The one remembered window. The game has one window, and a second call for
/// it must see the first.
///
/// @return the remembered window
AskedWindow& asked_window() noexcept {
    static AskedWindow stored;
    return stored;
}

/// Tells whether two places are the same rectangle, within kPlaceEpsilon.
///
/// @param got the place SDL reports
/// @param want the place the surface asks for
/// @return true when each edge is within kPlaceEpsilon
bool same_place(const SDL_FRect& got, const SDL_FRect& want) noexcept {
    return std::fabs(got.x - want.x) <= kPlaceEpsilon &&
           std::fabs(got.y - want.y) <= kPlaceEpsilon &&
           std::fabs(got.w - want.w) <= kPlaceEpsilon && std::fabs(got.h - want.h) <= kPlaceEpsilon;
}

} // namespace

int asked_pixel_density() noexcept {
    const char* hint = SDL_GetHint(pixel_density_hint);
    if (hint == nullptr || hint[0] == '\0')
        return 1;
    int value = 0;
    for (const char* at = hint; *at != '\0'; ++at) {
        if (*at < '0' || *at > '9')
            return 1;
        value = value * 10 + (*at - '0');
        if (value > kGreatestAskedDensity)
            return kGreatestAskedDensity;
    }
    return value < 1 ? 1 : value;
}

void apply_asked_pixel_density(SDL_Window* window) {
    AskedWindow& stored = asked_window();
    if (window == nullptr) {
        stored = {};
        return;
    }
    const SDL_WindowID id = SDL_GetWindowID(window);
    const int asked = asked_pixel_density();
    // Already grown, for this window, by the density the hint still names.
    if (stored.id == id && stored.density == asked && asked > 1)
        return;
    stored = {};
    stored.id = id;
    if (asked <= 1)
        return;
    int points_w = 0;
    int points_h = 0;
    int pixels_w = 0;
    int pixels_h = 0;
    if (!SDL_GetWindowSize(window, &points_w, &points_h) || points_w <= 0 || points_h <= 0)
        return;
    if (!SDL_GetWindowSizeInPixels(window, &pixels_w, &pixels_h) || pixels_w <= 0 || pixels_h <= 0)
        return;
    // The display's own density. Its points are the window's, and growing it
    // would make a 2× display 4×.
    if (pixels_w > points_w || pixels_h > points_h)
        return;
    // A window this large cannot be grown by the density without passing what
    // a drawing surface holds.
    if (points_w > 100000 / asked || points_h > 100000 / asked)
        return;
    const int grown_w = points_w * asked;
    const int grown_h = points_h * asked;
    if (!SDL_SetWindowSize(window, grown_w, grown_h))
        return;
    std::ignore = SDL_SyncWindow(window);
    stored.density = asked;
    stored.points_width = points_w;
    stored.points_height = points_h;
}

WindowSurface window_surface(SDL_Window* window, SDL_Renderer* renderer) {
    WindowSurface surface;
    if (window == nullptr)
        return surface;
    int sdl_w = 0;
    int sdl_h = 0;
    if (!SDL_GetWindowSize(window, &sdl_w, &sdl_h) || sdl_w <= 0 || sdl_h <= 0)
        return surface;
    surface.sdl_width = sdl_w;
    surface.sdl_height = sdl_h;
    int pixel_w = sdl_w;
    int pixel_h = sdl_h;
    int in_pixels_w = 0;
    int in_pixels_h = 0;
    if (SDL_GetWindowSizeInPixels(window, &in_pixels_w, &in_pixels_h) && in_pixels_w > 0 &&
        in_pixels_h > 0) {
        pixel_w = in_pixels_w;
        pixel_h = in_pixels_h;
    }
    int output_w = 0;
    int output_h = 0;
    if (renderer != nullptr && SDL_GetRenderOutputSize(renderer, &output_w, &output_h) &&
        output_w > 0 && output_h > 0) {
        pixel_w = std::max(pixel_w, output_w);
        pixel_h = std::max(pixel_h, output_h);
    }
    const AskedWindow& stored = asked_window();
    const bool hinted = stored.density > 1 && stored.id == SDL_GetWindowID(window);
    if (hinted) {
        const int expected_w = stored.points_width * stored.density;
        const int expected_h = stored.points_height * stored.density;
        if (pixel_w == expected_w && pixel_h == expected_h) {
            surface.points_width = stored.points_width;
            surface.points_height = stored.points_height;
        } else {
            // A resize since the hint grew the window, in the same pixels per point.
            surface.points_width = std::max(1, (pixel_w + stored.density / 2) / stored.density);
            surface.points_height = std::max(1, (pixel_h + stored.density / 2) / stored.density);
        }
    } else {
        surface.points_width = sdl_w;
        surface.points_height = sdl_h;
    }
    if (surface.points_width > pixel_w)
        surface.points_width = pixel_w;
    if (surface.points_height > pixel_h)
        surface.points_height = pixel_h;
    surface.pixel_width = pixel_w;
    surface.pixel_height = pixel_h;
    surface.density =
        static_cast<float>(pixel_w) / static_cast<float>(std::max(surface.points_width, 1));
    if (!(surface.density >= 1.0F))
        surface.density = 1.0F;
    return surface;
}

int surface_canvas_pixels(const WindowSurface& surface, int sdl_distance, bool across) noexcept {
    if (sdl_distance == 0)
        return 0;
    const int sdl_span = across ? surface.sdl_width : surface.sdl_height;
    const int pixels = across ? surface.pixel_width : surface.pixel_height;
    if (sdl_span <= 0 || pixels <= 0)
        return 0;
    // The size SDL reports is the point size, so the density is the scale.
    if (surface.points_width == surface.sdl_width && surface.points_height == surface.sdl_height)
        return static_cast<int>(std::lround(static_cast<float>(sdl_distance) * surface.density));
    return static_cast<int>(std::lround(
        static_cast<double>(sdl_distance) * static_cast<double>(pixels) /
        static_cast<double>(sdl_span)
    ));
}

SDL_FRect frame_letterbox(
    int output_width, int output_height, int frame_width, int frame_height, bool whole_steps
) noexcept {
    SDL_FRect placed{0.0F, 0.0F, 0.0F, 0.0F};
    if (output_width <= 0 || output_height <= 0 || frame_width <= 0 || frame_height <= 0)
        return placed;
    const float output_w = static_cast<float>(output_width);
    const float output_h = static_cast<float>(output_height);
    const float logical_w = static_cast<float>(frame_width);
    const float logical_h = static_cast<float>(frame_height);
    const float want_aspect = logical_w / logical_h;
    const float real_aspect = output_w / output_h;
    if (whole_steps) {
        // The same integer division SDL uses for INTEGER_SCALE.
        float scale = want_aspect > real_aspect ? static_cast<float>(output_width / frame_width)
                                                : static_cast<float>(output_height / frame_height);
        if (scale < 1.0F)
            scale = 1.0F;
        placed.w = std::floor(logical_w * scale);
        placed.h = std::floor(logical_h * scale);
        placed.x = (output_w - placed.w) / 2.0F;
        placed.y = (output_h - placed.h) / 2.0F;
        return placed;
    }
    if (std::fabs(want_aspect - real_aspect) < 0.0001F) {
        placed.w = output_w;
        placed.h = output_h;
        return placed;
    }
    if (want_aspect > real_aspect) {
        const float scale = output_w / logical_w;
        placed.w = output_w;
        placed.h = std::floor(logical_h * scale);
        placed.y = (output_h - placed.h) / 2.0F;
        return placed;
    }
    const float scale = output_h / logical_h;
    placed.h = output_h;
    placed.w = std::floor(logical_w * scale);
    placed.x = (output_w - placed.w) / 2.0F;
    return placed;
}

PlacedFrame place_frame(
    SDL_Window* window, SDL_Renderer* renderer, int frame_width, int frame_height, bool whole_steps
) noexcept {
    PlacedFrame placed;
    placed.surface = window_surface(window, renderer);
    const int out_w = placed.surface.pixel_width;
    const int out_h = placed.surface.pixel_height;
    if (out_w <= 0 || out_h <= 0 || frame_width <= 0 || frame_height <= 0)
        return placed;
    placed.drawn = frame_letterbox(out_w, out_h, frame_width, frame_height, whole_steps);
    int output_w = 0;
    int output_h = 0;
    int logical_w = 0;
    int logical_h = 0;
    SDL_FRect reckoned{};
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    const bool presented =
        renderer != nullptr && SDL_GetRenderOutputSize(renderer, &output_w, &output_h) &&
        output_w == out_w && output_h == out_h &&
        SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &mode) &&
        mode != SDL_LOGICAL_PRESENTATION_DISABLED && logical_w == frame_width &&
        logical_h == frame_height && SDL_GetRenderLogicalPresentationRect(renderer, &reckoned) &&
        reckoned.w > 0.0F && reckoned.h > 0.0F && same_place(reckoned, placed.drawn);
    if (presented) {
        placed.drawn = reckoned;
        placed.sdl_matches = true;
    }
    return placed;
}

} // namespace oa::app
