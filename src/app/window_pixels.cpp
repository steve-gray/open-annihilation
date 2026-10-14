// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The window-pixel path the Game files screen and the folder chooser share
// (window_pixels.hpp).
#include "window_pixels.hpp"

#include "oa/app/window_surface.hpp"
#include "render_host.hpp"

#include <cmath>
#include <iostream>

namespace oa::app {

bool read_window_viewport(
    SDL_Window* window, SDL_Renderer* renderer, oa::ui::game_files::Viewport& viewport
) {
    int output_width = 0;
    int output_height = 0;
    // A renderer with no size yet leaves the viewport as it was. The surface
    // then takes the larger of that output and the window's pixels, and the
    // points the layout uses, which a headless density may have grown past.
    if (window == nullptr || renderer == nullptr ||
        !SDL_GetRenderOutputSize(renderer, &output_width, &output_height) || output_width <= 0 ||
        output_height <= 0)
        return false;
    const WindowSurface surface = window_surface(window, renderer);
    if (surface.pixel_width <= 0 || surface.pixel_height <= 0 || surface.points_width <= 0)
        return false;
    oa::ui::game_files::Viewport next;
    next.width = surface.pixel_width;
    next.height = surface.pixel_height;
    next.px_per_point = surface.density;
    SDL_Rect safe{};
    if (SDL_GetWindowSafeArea(window, &safe) && safe.w > 0 && safe.h > 0) {
        next.safe.left = surface_canvas_pixels(surface, safe.x, true);
        next.safe.top = surface_canvas_pixels(surface, safe.y, false);
        next.safe.right = surface_canvas_pixels(surface, surface.sdl_width - safe.x - safe.w, true);
        next.safe.bottom =
            surface_canvas_pixels(surface, surface.sdl_height - safe.y - safe.h, false);
    }
    const bool same = next.width == viewport.width && next.height == viewport.height &&
                      next.px_per_point == viewport.px_per_point &&
                      next.safe.left == viewport.safe.left && next.safe.top == viewport.safe.top &&
                      next.safe.right == viewport.safe.right &&
                      next.safe.bottom == viewport.safe.bottom;
    if (same)
        return false;
    viewport = next;
    return true;
}

WindowFrame::WindowFrame(SDL_Renderer* renderer, std::string_view owner) noexcept
    : renderer_(renderer), owner_(owner) {
}

WindowFrame::~WindowFrame() {
    drop_texture();
}

void WindowFrame::drop_texture() noexcept {
    if (texture_ != nullptr)
        SDL_DestroyTexture(texture_);
    texture_ = nullptr;
}

bool WindowFrame::show(const paint::Canvas& canvas, const RenderState& state, RendererHost* host) {
    if (canvas.width <= 0 || canvas.height <= 0)
        return false;
    if (texture_ == nullptr || width_ != canvas.width || height_ != canvas.height) {
        drop_texture();
        texture_ = SDL_CreateTexture(
            renderer_,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            canvas.width,
            canvas.height
        );
        if (texture_ == nullptr) {
            std::cerr << "open-annihilation: " << owner_
                      << " cannot make its texture: " << SDL_GetError() << '\n';
            return false;
        }
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        width_ = canvas.width;
        height_ = canvas.height;
    }
    SDL_UpdateTexture(texture_, nullptr, canvas.rgba.data(), canvas.width * 4);
    state.use_window_pixels();
    const oa::ui::game_files::Colour back = oa::ui::game_files::background_colour;
    SDL_SetRenderDrawColor(renderer_, back.r, back.g, back.b, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    if (SDL_RenderPresent(renderer_) && host != nullptr)
        host->note_presented_frame(0);
    return true;
}

PointerEvent read_pointer(
    const SDL_Event& event, const oa::ui::game_files::Viewport& viewport, const WindowPress& press
) noexcept {
    const auto finger_at = [&viewport, &event](PointerStep step) {
        PointerEvent taken;
        taken.step = step;
        taken.x = event.tfinger.x * static_cast<float>(viewport.width);
        taken.y = event.tfinger.y * static_cast<float>(viewport.height);
        taken.finger = true;
        taken.touch = event.tfinger.touchID;
        taken.id = event.tfinger.fingerID;
        return taken;
    };
    const auto pointer_at = [&viewport](PointerStep step, float x, float y) {
        PointerEvent taken;
        taken.step = step;
        taken.x = x * viewport.px_per_point;
        taken.y = y * viewport.px_per_point;
        return taken;
    };
    // Read only for a finger's event: the finger of the press under way.
    const auto pressing_finger = [&press, &event]() {
        return press.active && press.finger && event.tfinger.touchID == press.touch &&
               event.tfinger.fingerID == press.id;
    };
    const bool pressing_pointer = press.active && !press.finger;
    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN:
        return finger_at(PointerStep::down);
    case SDL_EVENT_FINGER_MOTION:
        return pressing_finger() ? finger_at(PointerStep::move) : PointerEvent{};
    case SDL_EVENT_FINGER_UP:
        return pressing_finger() ? finger_at(PointerStep::up) : PointerEvent{};
    case SDL_EVENT_FINGER_CANCELED:
        return pressing_finger() ? finger_at(PointerStep::cancel) : PointerEvent{};
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        // SDL's mouse events made from a finger are left out: the finger itself is taken.
        if (event.button.which != SDL_TOUCH_MOUSEID && event.button.button == SDL_BUTTON_LEFT)
            return pointer_at(PointerStep::down, event.button.x, event.button.y);
        return {};
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.which != SDL_TOUCH_MOUSEID && pressing_pointer)
            return pointer_at(PointerStep::move, event.motion.x, event.motion.y);
        return {};
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.which != SDL_TOUCH_MOUSEID && event.button.button == SDL_BUTTON_LEFT &&
            pressing_pointer)
            return pointer_at(PointerStep::up, event.button.x, event.button.y);
        return {};
    case SDL_EVENT_MOUSE_WHEEL: {
        if (event.wheel.which == SDL_TOUCH_MOUSEID)
            return {};
        float notches = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            notches = -notches;
        PointerEvent taken;
        taken.step = PointerStep::wheel;
        taken.wheel_points = -notches * wheel_notch_points;
        return taken;
    }
    default:
        return {};
    }
}

bool touch_screen(SDL_TouchID device) noexcept {
    if (device == SDL_MOUSE_TOUCHID || device == SDL_PEN_TOUCHID)
        return false;
    return SDL_GetTouchDeviceType(device) == SDL_TOUCH_DEVICE_DIRECT;
}

bool press_scrolls(
    const WindowPress& press, const oa::ui::kit::Rect& rows, float y, float px_per_point
) noexcept {
    const bool over_rows = rows.width > 0 && rows.height > 0 &&
                           press.start_x >= static_cast<float>(rows.x) &&
                           press.start_x < static_cast<float>(rows.x + rows.width) &&
                           press.start_y >= static_cast<float>(rows.y) &&
                           press.start_y < static_cast<float>(rows.y + rows.height);
    return over_rows && std::fabs(y - press.start_y) > drag_threshold_points * px_per_point;
}

oa::ui::kit::Point canvas_point(float x, float y) noexcept {
    return {static_cast<int32_t>(std::lround(x)), static_cast<int32_t>(std::lround(y))};
}

} // namespace oa::app
