// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Helpers for the native checks of the OA layer's screens as the window
// shows them (oa_layer_check.hpp).

#include "oa_layer_check.hpp"

#include "oa/ui/kit/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oa::app {

namespace {

namespace artless = oa::ui::frontend_renderer;

/// Returns the window's pixels per point of its own: its renderer's output
/// size over its size.
///
/// @param window the window
/// @param sdl_renderer its renderer
/// @return the density; 1 when either size is unknown
float pixel_density(SDL_Window* window, SDL_Renderer* sdl_renderer) noexcept {
    int window_width = 0;
    int window_height = 0;
    int output_width = 0;
    int output_height = 0;
    if (window == nullptr || sdl_renderer == nullptr ||
        !SDL_GetWindowSize(window, &window_width, &window_height) ||
        !SDL_GetRenderOutputSize(sdl_renderer, &output_width, &output_height) ||
        window_width <= 0 || output_width <= 0)
        return 1.0F;
    return static_cast<float>(output_width) / static_cast<float>(window_width);
}

} // namespace

std::string_view size_class_name(oa::ui::kit::SizeClass size_class) noexcept {
    switch (size_class) {
    case oa::ui::kit::SizeClass::regular:
        return "Regular";
    case oa::ui::kit::SizeClass::large:
        return "Large";
    case oa::ui::kit::SizeClass::compact:
        break;
    }
    return "Compact";
}

LayerPointer
window_pointer(const LayerView& view, oa::ui::display_layout::Point picture_point) noexcept {
    if (view.picture_size.x <= 0 || view.picture_size.y <= 0)
        return {picture_point, layer_cursor_reach};
    const double across =
        static_cast<double>(view.picture.width) / static_cast<double>(view.picture_size.x);
    const double down =
        static_cast<double>(view.picture.height) / static_cast<double>(view.picture_size.y);
    return {
        {view.picture.x + static_cast<int32_t>(std::floor((picture_point.x + 0.5) * across)),
         view.picture.y + static_cast<int32_t>(std::floor((picture_point.y + 0.5) * down))},
        static_cast<int32_t>(std::ceil(layer_cursor_reach * std::max(across, down)))
    };
}

artless::Surface expected_layer_frame(
    const artless::Surface& closed,
    const artless::Surface& drawing,
    const LayerPlacement& placement,
    uint32_t darkenings
) {
    artless::Surface expected = closed;
    // The backdrop over the whole window, as it darkens the picture.
    for (uint32_t pass = 0; pass < darkenings; ++pass)
        artless::blend_source_rect(
            expected,
            {0, 0, 1},
            {0, 0, static_cast<int32_t>(expected.width), static_cast<int32_t>(expected.height)},
            oa::ui::kit::rgb(oa::ui::kit::colour::backdrop),
            oa::ui::kit::menu_backdrop_opacity
        );
    // The drawing at its place, pixel for pixel.
    const auto& shown = placement.shown;
    const auto drawn_width = static_cast<int32_t>(drawing.width);
    const auto drawn_height = static_cast<int32_t>(drawing.height);
    const int32_t left = std::max(shown.x, 0);
    const int32_t top = std::max(shown.y, 0);
    const int32_t right = std::min(
        {shown.x + shown.width, shown.x + drawn_width, static_cast<int32_t>(closed.width)}
    );
    const int32_t bottom = std::min(
        {shown.y + shown.height, shown.y + drawn_height, static_cast<int32_t>(closed.height)}
    );
    for (int32_t row = top; row < bottom; ++row)
        for (int32_t column = left; column < right; ++column) {
            const auto* from =
                drawing.rgb.data() + (static_cast<std::size_t>(row - shown.y) * drawing.width +
                                      static_cast<std::size_t>(column - shown.x)) *
                                         3U;
            auto* to = expected.rgb.data() + (static_cast<std::size_t>(row) * expected.width +
                                              static_cast<std::size_t>(column)) *
                                                 3U;
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
    return expected;
}

std::size_t layer_differences(
    const artless::Surface& presented, const artless::Surface& expected, const LayerPointer& pointer
) noexcept {
    if (presented.width != expected.width || presented.height != expected.height ||
        presented.rgb.size() != expected.rgb.size())
        return static_cast<std::size_t>(expected.width) * expected.height;
    std::size_t differing = 0;
    for (uint32_t y = 0; y < presented.height; ++y)
        for (uint32_t x = 0; x < presented.width; ++x) {
            const auto column = static_cast<int32_t>(x);
            const auto row = static_cast<int32_t>(y);
            if (column >= pointer.at.x - pointer.reach && column < pointer.at.x + pointer.reach &&
                row >= pointer.at.y - pointer.reach && row < pointer.at.y + pointer.reach)
                continue;
            const std::size_t at = (static_cast<std::size_t>(y) * presented.width + x) * 3U;
            if (presented.rgb[at] != expected.rgb[at] ||
                presented.rgb[at + 1] != expected.rgb[at + 1] ||
                presented.rgb[at + 2] != expected.rgb[at + 2])
                ++differing;
        }
    return differing;
}

oa::ui::display_layout::Point
layer_window_pixel(const LayerPlacement& placement, oa::ui::kit::Point point) noexcept {
    const auto& shown = placement.shown;
    if (placement.points_width <= 0 || placement.points_height <= 0)
        return {shown.x, shown.y};
    // A point's window pixels, and the one in their middle.
    const auto middle = [](int32_t start, int32_t length, int32_t points, int32_t at) {
        const int64_t first = int64_t{at} * length / points;
        const int64_t past = (int64_t{at} + 1) * length / points;
        return start + static_cast<int32_t>((first + past) / 2);
    };
    return {
        middle(shown.x, shown.width, placement.points_width, point.x),
        middle(shown.y, shown.height, placement.points_height, point.y)
    };
}

SDL_Event window_pointer_event(
    SDL_Window* window,
    SDL_Renderer* sdl_renderer,
    SDL_EventType type,
    oa::ui::display_layout::Point pixel,
    uint8_t button
) noexcept {
    const float density = pixel_density(window, sdl_renderer);
    const float x = (static_cast<float>(pixel.x) + 0.5F) / density;
    const float y = (static_cast<float>(pixel.y) + 0.5F) / density;
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = window != nullptr ? SDL_GetWindowID(window) : 0;
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.windowID = window != nullptr ? SDL_GetWindowID(window) : 0;
        event.button.button = button;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = 1;
        event.button.x = x;
        event.button.y = y;
    }
    return event;
}

SDL_Event window_finger_event(
    SDL_Window* window,
    SDL_Renderer* sdl_renderer,
    SDL_EventType type,
    oa::ui::display_layout::Point pixel,
    SDL_TouchID touch,
    SDL_FingerID finger
) noexcept {
    const float density = pixel_density(window, sdl_renderer);
    int window_width = 1;
    int window_height = 1;
    if (window == nullptr || !SDL_GetWindowSize(window, &window_width, &window_height) ||
        window_width <= 0 || window_height <= 0) {
        window_width = 1;
        window_height = 1;
    }
    SDL_Event event{};
    event.type = type;
    event.tfinger.touchID = touch;
    event.tfinger.fingerID = finger;
    event.tfinger.x =
        (static_cast<float>(pixel.x) + 0.5F) / density / static_cast<float>(window_width);
    event.tfinger.y =
        (static_cast<float>(pixel.y) + 0.5F) / density / static_cast<float>(window_height);
    event.tfinger.pressure = type == SDL_EVENT_FINGER_UP ? 0.0F : 1.0F;
    event.tfinger.windowID = window != nullptr ? SDL_GetWindowID(window) : 0;
    return event;
}

} // namespace oa::app
