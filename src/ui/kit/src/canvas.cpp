// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The canvas's primitives: the crisp form through the artless painter at a
// whole scale in the game's fonts.

#include "oa/ui/kit/canvas.hpp"

#include "oa/ui/frontend_renderer/artless.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace oa::ui::kit {

namespace renderer = oa::ui::frontend_renderer;

bool can_draw(const Canvas& canvas) noexcept {
    return canvas.surface != nullptr;
}

Canvas clipped(const Canvas& canvas, const Rect& rect) {
    Canvas out = canvas;
    const Rect& outer = canvas.placement.clip;
    if (outer.width <= 0 || outer.height <= 0) {
        out.placement.clip = rect;
        return out;
    }
    const int32_t left = std::max(rect.x, outer.x);
    const int32_t top = std::max(rect.y, outer.y);
    const int32_t right = std::min(rect.x + rect.width, outer.x + outer.width);
    const int32_t bottom = std::min(rect.y + rect.height, outer.y + outer.height);
    out.placement.clip = {left, top, std::max(right - left, 0), std::max(bottom - top, 0)};
    if (out.placement.clip.width == 0 || out.placement.clip.height == 0)
        out.placement.scale = 0;
    return out;
}

Canvas placed_at(const Canvas& canvas, Point origin) {
    Canvas shifted = canvas;
    const int32_t scale = canvas.placement.scale;
    shifted.placement.x += origin.x * scale;
    shifted.placement.y += origin.y * scale;
    if (shifted.placement.clip.width > 0 && shifted.placement.clip.height > 0) {
        shifted.placement.clip.x -= origin.x;
        shifted.placement.clip.y -= origin.y;
    }
    return shifted;
}

void fill_rect(const Canvas& canvas, const Rect& rect, Colour ink) {
    if (canvas.surface != nullptr)
        renderer::fill_source_rect(*canvas.surface, canvas.placement, rect, rgb(ink));
}

void blend_rect(const Canvas& canvas, const Rect& rect, Colour ink, uint32_t opacity) {
    if (canvas.surface != nullptr)
        renderer::blend_source_rect(*canvas.surface, canvas.placement, rect, rgb(ink), opacity);
}

void outline_rect(const Canvas& canvas, const Rect& rect, Colour ink) {
    if (canvas.surface != nullptr)
        renderer::draw_outline(*canvas.surface, canvas.placement, rect, rgb(ink));
}

void bevel_rect(const Canvas& canvas, const Rect& rect, Colour light, Colour dark) {
    if (canvas.surface != nullptr)
        renderer::draw_bevel(*canvas.surface, canvas.placement, rect, rgb(light), rgb(dark));
}

void rule_rect(const Canvas& canvas, const Rect& rect, Colour ink) {
    // A rule is a fill one pixel high or wide at a whole scale.
    fill_rect(canvas, rect, ink);
}

void box_text(
    const Canvas& canvas,
    FontRole role,
    std::string_view text,
    const Rect& box,
    Align align,
    Colour ink,
    int32_t tracking
) {
    if (canvas.surface == nullptr || canvas.fonts == nullptr)
        return;
    draw_boxed_text(
        *canvas.surface, canvas.placement, *canvas.fonts, role, text, box, align, rgb(ink), tracking
    );
}

int32_t measure_text(const Canvas& canvas, FontRole role, std::string_view text) {
    if (canvas.fonts == nullptr)
        return 0;
    return text_width(*canvas.fonts, role, text);
}

void draw_picture(const Canvas& canvas, const Rect& rect, const renderer::RgbaPicture& picture) {
    if (canvas.surface != nullptr)
        renderer::draw_picture(*canvas.surface, canvas.placement, rect, picture);
}

void draw_mark(const Canvas& canvas, const renderer::Mark& bits, Point at, Colour ink) {
    if (canvas.surface != nullptr)
        renderer::draw_mark(*canvas.surface, canvas.placement, bits, at.x, at.y, rgb(ink));
}

} // namespace oa::ui::kit
