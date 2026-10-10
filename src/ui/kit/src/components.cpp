// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's controls, drawn for the crisp backend. The arithmetic
// is the dialog's, moved here unchanged.

#include "oa/ui/kit/components.hpp"

#include "oa/ui/kit/chrome.hpp"

#include "oa/ui/frontend_renderer/artless.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace oa::ui::kit {

namespace renderer = oa::ui::frontend_renderer;

namespace {

/// The OA letters, 9 by 7, row by row.
constexpr std::array<uint8_t, 9 * 7> kSmallMarkBits{
    0, 1, 1, 0, 0, 0, 1, 1, 0, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 1, 1, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    0, 1, 1, 0, 0, 1, 0, 0, 1, //
};
/// The OA letters, 12 by 9, row by row.
constexpr std::array<uint8_t, 12 * 9> kLargeMarkBits{
    0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, //
    0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, //
};
/// The padlock, 5 by 7, row by row.
constexpr std::array<uint8_t, 5 * 7> kPadlockBits{
    0, 1, 1, 1, 0, //
    1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, //
    1, 1, 1, 1, 1, //
    1, 1, 0, 1, 1, //
    1, 1, 0, 1, 1, //
    1, 1, 1, 1, 1, //
};
/// An arrow pointing right, 5 by 5, row by row.
constexpr std::array<uint8_t, 5 * 5> kClosedArrowBits{
    0, 1, 0, 0, 0, //
    0, 1, 1, 0, 0, //
    0, 1, 1, 1, 0, //
    0, 1, 1, 0, 0, //
    0, 1, 0, 0, 0, //
};
/// An arrow pointing down, 5 by 5, row by row.
constexpr std::array<uint8_t, 5 * 5> kOpenArrowBits{
    0, 0, 0, 0, 0, //
    1, 1, 1, 1, 1, //
    0, 1, 1, 1, 0, //
    0, 0, 1, 0, 0, //
    0, 0, 0, 0, 0, //
};
/// A drop-down's arrow, 7 by 4, row by row.
constexpr std::array<uint8_t, 7 * 4> kChoiceArrowBits{
    1, 1, 1, 1, 1, 1, 1, //
    0, 1, 1, 1, 1, 1, 0, //
    0, 0, 1, 1, 1, 0, 0, //
    0, 0, 0, 1, 0, 0, 0, //
};

constexpr renderer::Mark kSmallMark{9, 7, kSmallMarkBits};
constexpr renderer::Mark kLargeMark{12, 9, kLargeMarkBits};
constexpr renderer::Mark kPadlock{5, 7, kPadlockBits};
constexpr renderer::Mark kClosedArrow{5, 5, kClosedArrowBits};
constexpr renderer::Mark kOpenArrow{5, 5, kOpenArrowBits};
constexpr renderer::Mark kChoiceArrow{7, 4, kChoiceArrowBits};

/// The chosen menu item's marker, the same mark the section list uses.
constexpr int32_t kChosenMarkerOffset = 5;
constexpr int32_t kChosenMarkerWidth = 2;
constexpr int32_t kChosenMarkerHeight = 8;
/// The columns a menu item keeps clear at its right.
constexpr int32_t kChoiceTextMargin = 4;
/// The settings dialog's height. A menu that would pass the line over its
/// footer opens above its field, and never above the body's first row.
constexpr int32_t kSettingsDialogHeight = 324;

const renderer::Mark& bits_of(Mark mark) noexcept {
    switch (mark) {
    case Mark::oa_small:
        return kSmallMark;
    case Mark::oa_large:
        return kLargeMark;
    case Mark::padlock:
        return kPadlock;
    case Mark::arrow_closed:
        return kClosedArrow;
    case Mark::arrow_open:
        return kOpenArrow;
    case Mark::choice_arrow:
        return kChoiceArrow;
    }
    return kSmallMark;
}

bool drawable(const Canvas& canvas) noexcept {
    return canvas.surface != nullptr;
}

void paint_text(
    const Canvas& canvas,
    FontRole role,
    std::string_view text,
    const Rect& box,
    Align align,
    Colour ink
) {
    if (!drawable(canvas) || canvas.fonts == nullptr)
        return;
    draw_boxed_text(
        *canvas.surface, canvas.placement, *canvas.fonts, role, text, box, align, rgb(ink)
    );
}

/// A button's face, when it has one, its border and its caption.
struct ButtonColours {
    std::optional<Colour> face{};
    Colour border{};
    Colour caption{};
};

ButtonColours button_colours(const ButtonLook& look) noexcept {
    if (look.style == ButtonStyle::accent) {
        Colour face = colour::accent;
        if (look.held)
            face = colour::accent_held;
        else if (look.hovered)
            face = colour::accent_light;
        return {face, colour::accent_light, colour::on_accent};
    }
    const bool hovered = look.enabled && look.hovered;
    const bool held = look.enabled && look.held;
    if (look.style == ButtonStyle::plain && !look.enabled)
        return {std::nullopt, colour::control_border, colour::switch_idle};
    const Colour border = hovered ? colour::control_hover : colour::control_border;
    const Colour caption = hovered ? colour::text : colour::button_text;
    if (look.style == ButtonStyle::inset) {
        Colour face = colour::list;
        if (held)
            face = colour::band;
        else if (hovered)
            face = colour::hover;
        return {face, border, caption};
    }
    std::optional<Colour> face;
    if (look.style == ButtonStyle::quiet) {
        if (held)
            face = colour::band;
        else if (hovered)
            face = colour::hover;
    } else if (held || hovered) {
        face = colour::hover;
    }
    return {face, border, caption};
}

void draw_mark_square(const Canvas& canvas, int32_t side, Colour ink) {
    if (!drawable(canvas))
        return;
    const int32_t square_side =
        side * compact_metrics.button_square_numerator / compact_metrics.button_square_denominator;
    const int32_t square_offset = (side - square_side) / 2;
    const Rect square{square_offset, square_offset, square_side, square_side};
    renderer::draw_outline(*canvas.surface, canvas.placement, square, rgb(ink));
    const renderer::Mark& mark =
        square_side - 2 >= kLargeMark.width + 2 * compact_metrics.large_mark_margin ? kLargeMark
                                                                                    : kSmallMark;
    renderer::draw_mark(
        *canvas.surface,
        canvas.placement,
        mark,
        square_offset + (square_side - mark.width) / 2,
        square_offset + (square_side - mark.height + 1) / 2,
        rgb(ink)
    );
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

void add_control(
    DisplayList& list,
    const Rect& rect,
    ControlId id,
    std::string name,
    ControlKind kind,
    bool steps,
    bool checked,
    std::string text,
    bool enabled
) {
    Control control;
    control.id = id;
    control.rect = rect;
    control.enabled = enabled;
    control.name = std::move(name);
    control.kind = kind;
    control.steps = steps;
    control.checked = checked;
    control.text = std::move(text);
    list.controls.push_back(std::move(control));
}

void paint_item(const Canvas& canvas, const Item& item) {
    Canvas drawn = canvas;
    if (item.clip.width > 0 && item.clip.height > 0)
        drawn = clipped(canvas, item.clip);
    if (!drawable(drawn))
        return;
    renderer::Surface& surface = *drawn.surface;
    const renderer::Placement& placement = drawn.placement;
    switch (item.role) {
    case Role::fill:
        renderer::fill_source_rect(surface, placement, item.rect, rgb(item.colour));
        break;
    case Role::blend:
        renderer::blend_source_rect(surface, placement, item.rect, rgb(item.colour), item.opacity);
        break;
    case Role::outline:
        renderer::draw_outline(surface, placement, item.rect, rgb(item.colour));
        break;
    case Role::bevel:
        renderer::draw_bevel(
            surface, placement, item.rect, rgb(item.colour), rgb(colour::edge_dark)
        );
        break;
    case Role::rule:
        renderer::fill_source_rect(surface, placement, item.rect, rgb(item.colour));
        break;
    case Role::text:
        paint_text(drawn, item.font, item.text, item.rect, item.align, item.colour);
        break;
    case Role::picture:
        renderer::draw_picture(surface, placement, item.rect, drawn.icon);
        break;
    case Role::mark:
        if (const auto* look = std::get_if<MarkLook>(&item.look))
            draw_mark(drawn, look->mark, {item.rect.x, item.rect.y}, look->colour);
        break;
    case Role::button:
        if (const auto* look = std::get_if<ButtonLook>(&item.look))
            draw_button(drawn, item.rect, *look);
        break;
    case Role::toggle:
        if (const auto* look = std::get_if<SwitchLook>(&item.look))
            draw_switch(drawn, item.rect, *look);
        break;
    case Role::levels:
        if (const auto* look = std::get_if<LevelsLook>(&item.look))
            draw_levels(drawn, item.rect, *look);
        break;
    case Role::slider:
        if (const auto* look = std::get_if<SliderLook>(&item.look))
            draw_slider(drawn, item.rect, *look);
        break;
    case Role::choice:
        if (const auto* look = std::get_if<ChoiceLook>(&item.look))
            draw_choice(drawn, item.rect, *look);
        break;
    case Role::choice_menu:
        if (const auto* look = std::get_if<ChoiceMenuLook>(&item.look))
            draw_choice_menu(drawn, item.rect, *look);
        break;
    case Role::lock:
        if (const auto* look = std::get_if<LockLook>(&item.look))
            draw_lock(drawn, item.rect, *look);
        break;
    case Role::scroll_bar:
        if (const auto* look = std::get_if<ScrollBarLook>(&item.look))
            draw_scroll_bar(drawn, item.rect, look->thumb, look->hot);
        break;
    case Role::focus_ring:
        if (const auto* look = std::get_if<FocusRingLook>(&item.look))
            draw_focus_ring(drawn, item.rect, look->around);
        break;
    case Role::oa_button:
        if (const auto* look = std::get_if<OaButtonLook>(&item.look))
            draw_oa_button(placed_at(drawn, {item.rect.x, item.rect.y}), look->side, look->state);
        break;
    case Role::oa_mark:
        draw_oa_mark(placed_at(drawn, {item.rect.x, item.rect.y}), item.rect.width);
        break;
    case Role::header:
        if (const auto* look = std::get_if<HeaderLook>(&item.look))
            draw_header(drawn, *look);
        break;
    case Role::footer_band:
        if (const auto* look = std::get_if<FooterBandLook>(&item.look))
            draw_footer_band(drawn, look->width, look->rule_row);
        break;
    case Role::nav:
        if (const auto* look = std::get_if<NavLook>(&item.look))
            draw_nav(drawn, *look);
        break;
    case Role::heading:
        if (const auto* look = std::get_if<HeadingLook>(&item.look))
            draw_heading(drawn, item.rect, look->text);
        break;
    case Role::row_frame:
        if (const auto* look = std::get_if<RowFrame>(&item.look))
            draw_row_frame(drawn, *look);
        break;
    case Role::locked_fade:
        draw_locked_fade(drawn, item.rect);
        break;
    }
}

} // namespace

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

void draw_mark(const Canvas& canvas, Mark mark, Point at, Colour ink) {
    if (!drawable(canvas))
        return;
    renderer::draw_mark(*canvas.surface, canvas.placement, bits_of(mark), at.x, at.y, rgb(ink));
}

void draw_focus_ring(const Canvas& canvas, const Rect& control, bool around) {
    if (!drawable(canvas))
        return;
    const Rect ring = around ? grown(control, compact_metrics.focus_inset) : control;
    renderer::draw_outline(*canvas.surface, canvas.placement, ring, rgb(colour::accent));
}

void draw_button(const Canvas& canvas, const Rect& area, const ButtonLook& look) {
    if (!drawable(canvas))
        return;
    const ButtonColours colours = button_colours(look);
    if (colours.face)
        renderer::fill_source_rect(*canvas.surface, canvas.placement, area, rgb(*colours.face));
    renderer::draw_outline(*canvas.surface, canvas.placement, area, rgb(colours.border));
    paint_text(canvas, FontRole::small, look.caption, area, Align::centre, colours.caption);
}

void draw_switch(const Canvas& canvas, const Rect& area, const SwitchLook& look) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, area, rgb(colour::well));
    renderer::draw_outline(
        *canvas.surface,
        canvas.placement,
        area,
        rgb(look.hovered ? colour::control_hover : colour::control_border)
    );
    const int32_t half = (area.width - 2) / 2;
    const Rect off{area.x + 1, area.y + 1, half, area.height - 2};
    const Rect on_half{area.x + 1 + half, area.y + 1, half, area.height - 2};
    if (look.on)
        renderer::fill_source_rect(
            *canvas.surface,
            canvas.placement,
            on_half,
            rgb(look.locked ? colour::control_hover : colour::accent)
        );
    else
        renderer::fill_source_rect(
            *canvas.surface, canvas.placement, off, rgb(colour::off_selected)
        );
    paint_text(
        canvas,
        FontRole::small,
        look.off_caption,
        off,
        Align::centre,
        look.on ? colour::switch_idle : colour::text
    );
    paint_text(
        canvas,
        FontRole::small,
        look.on_caption,
        on_half,
        Align::centre,
        look.on ? colour::on_accent : colour::switch_idle
    );
}

void draw_levels(const Canvas& canvas, const Rect& area, const LevelsLook& look) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, area, rgb(colour::well));
    renderer::draw_outline(
        *canvas.surface,
        canvas.placement,
        area,
        rgb(look.hovered ? colour::control_hover : colour::control_border)
    );
    for (std::size_t index = 0; index < look.captions.size(); ++index) {
        const Rect segment{
            area.x + 1 + static_cast<int32_t>(index) * look.level_width,
            area.y + 1,
            look.level_width,
            area.height - 2,
        };
        const bool selected = index == look.chosen;
        if (selected)
            renderer::fill_source_rect(
                *canvas.surface,
                canvas.placement,
                segment,
                rgb(look.locked ? colour::control_hover : colour::accent)
            );
        paint_text(
            canvas,
            FontRole::small,
            look.captions[index],
            segment,
            Align::centre,
            selected ? colour::on_accent : colour::button_text
        );
        if (index >= look.offered)
            renderer::blend_source_rect(
                *canvas.surface, canvas.placement, segment, rgb(colour::panel), locked_fade
            );
    }
}

std::size_t
level_at(const Rect& area, std::size_t levels, int32_t level_width, int32_t column) noexcept {
    if (levels == 0 || level_width <= 0)
        return 0;
    const int32_t along = std::max(column - area.x - 1, int32_t{0}) / level_width;
    return std::min(static_cast<std::size_t>(along), levels - 1);
}

void draw_slider(const Canvas& canvas, const Rect& area, const SliderLook& look) {
    if (!drawable(canvas))
        return;
    const Rect track{
        area.x, area.y + compact_metrics.track_offset, area.width, compact_metrics.track_height
    };
    renderer::fill_source_rect(*canvas.surface, canvas.placement, track, rgb(colour::well));
    renderer::draw_outline(*canvas.surface, canvas.placement, track, rgb(colour::control_border));
    const int32_t knob = knob_column(area, look.stop, look.stops);
    renderer::fill_source_rect(
        *canvas.surface,
        canvas.placement,
        {track.x + 1, track.y + 1, knob - track.x - 1, track.height - 2},
        rgb(look.locked ? colour::control_border : colour::accent)
    );
    const int32_t travel = area.width - compact_metrics.knob_width;
    if (look.stops > 1 && travel / (look.stops - 1) >= compact_metrics.least_stop_spacing) {
        for (int32_t index = 0; index < look.stops; ++index) {
            renderer::fill_source_rect(
                *canvas.surface,
                canvas.placement,
                {knob_column(area, index, look.stops),
                 area.y + compact_metrics.stop_offset,
                 1,
                 compact_metrics.stop_height},
                rgb(colour::control_border)
            );
        }
    }
    const Rect knob_rect{
        knob - compact_metrics.knob_width / 2,
        area.y,
        compact_metrics.knob_width,
        compact_metrics.knob_height
    };
    Colour face = colour::accent;
    if (look.locked)
        face = colour::control_hover;
    else if (look.hovered)
        face = colour::accent_light;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, knob_rect, rgb(face));
    renderer::draw_outline(*canvas.surface, canvas.placement, knob_rect, rgb(colour::on_accent));
}

int32_t knob_column(const Rect& track, int32_t stop, int32_t stops) noexcept {
    const int32_t first = track.x + compact_metrics.knob_width / 2;
    const int32_t travel = track.width - compact_metrics.knob_width;
    if (stops < 2)
        return first;
    return first +
           (travel * std::clamp(stop, int32_t{0}, stops - 1) + (stops - 1) / 2) / (stops - 1);
}

int32_t stop_at(const Rect& track, int32_t column, int32_t stops) noexcept {
    const int32_t first = track.x + compact_metrics.knob_width / 2;
    const int32_t travel = track.width - compact_metrics.knob_width;
    if (stops < 2 || travel <= 0)
        return 0;
    const int32_t along = std::clamp(column - first, int32_t{0}, travel);
    return (along * (stops - 1) + travel / 2) / travel;
}

void draw_choice(const Canvas& canvas, const Rect& area, const ChoiceLook& look) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, area, rgb(colour::well));
    Colour border = colour::control_border;
    if (look.open)
        border = colour::accent;
    else if (look.hovered)
        border = colour::control_hover;
    renderer::draw_outline(*canvas.surface, canvas.placement, area, rgb(border));
    const Rect text_box{
        area.x + compact_metrics.choice_text_inset,
        area.y,
        area.width - compact_metrics.choice_text_inset - compact_metrics.choice_arrow_room,
        area.height,
    };
    paint_text(
        clipped(canvas, text_box), FontRole::regular, look.text, text_box, Align::left, colour::text
    );
    renderer::draw_mark(
        *canvas.surface,
        canvas.placement,
        kChoiceArrow,
        area.x + area.width - compact_metrics.choice_arrow_room +
            (compact_metrics.choice_arrow_room - compact_metrics.choice_arrow_width) / 2,
        area.y + (area.height - compact_metrics.choice_arrow_height) / 2,
        rgb(look.open ? colour::accent : colour::button_text)
    );
}

void draw_choice_menu(const Canvas& canvas, const Rect& menu, const ChoiceMenuLook& look) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, menu, rgb(colour::list));
    renderer::draw_outline(*canvas.surface, canvas.placement, menu, rgb(colour::control_hover));
    const int32_t count = static_cast<int32_t>(look.shown.size());
    for (int32_t place = 0; place < count; ++place) {
        const int32_t item = look.first + place;
        if (look.total >= 0 && item >= look.total)
            break;
        const Rect box = choice_item(menu, place);
        Colour text = colour::list_text;
        if (item == look.chosen) {
            renderer::fill_source_rect(
                *canvas.surface, canvas.placement, box, rgb(colour::list_selected)
            );
            renderer::fill_source_rect(
                *canvas.surface,
                canvas.placement,
                {box.x + kChosenMarkerOffset,
                 box.y + (box.height - kChosenMarkerHeight) / 2,
                 kChosenMarkerWidth,
                 kChosenMarkerHeight},
                rgb(colour::accent)
            );
            text = colour::text;
        }
        if (item == look.marked) {
            if (item != look.chosen)
                renderer::fill_source_rect(
                    *canvas.surface, canvas.placement, box, rgb(colour::hover)
                );
            text = colour::text;
            if (look.focus_shown)
                renderer::draw_outline(*canvas.surface, canvas.placement, box, rgb(colour::accent));
        }
        const Rect caption{
            box.x + compact_metrics.choice_item_text_inset,
            box.y,
            box.width - compact_metrics.choice_item_text_inset - kChoiceTextMargin,
            box.height,
        };
        paint_text(
            clipped(canvas, caption),
            FontRole::regular,
            look.shown[static_cast<std::size_t>(place)],
            caption,
            Align::left,
            text
        );
    }
    const int32_t window =
        shown_choices(static_cast<std::size_t>(std::max(look.total, int32_t{0})));
    if (look.total > window) {
        const int32_t travel = menu.height - 2;
        const int32_t height = std::max(travel * window / look.total, 4);
        const int32_t top =
            menu.y + 1 + (travel - height) * look.first / std::max(look.total - window, 1);
        renderer::fill_source_rect(
            *canvas.surface,
            canvas.placement,
            {menu.x + menu.width - 3, top, 2, height},
            rgb(colour::control_hover)
        );
    }
}

int32_t shown_choices(std::size_t choices) noexcept {
    const auto most = static_cast<std::size_t>(compact_metrics.most_shown_choices);
    return static_cast<int32_t>(std::min(choices, most));
}

Rect choice_menu(const Rect& field, std::size_t choices) noexcept {
    const int32_t height = shown_choices(choices) * compact_metrics.choice_item_height + 2;
    int32_t top = field.y + field.height;
    const int32_t footer_line =
        kSettingsDialogHeight - compact_metrics.edge - compact_metrics.footer_height - 1;
    const int32_t body_top = compact_metrics.edge + compact_metrics.header_height + 1;
    if (top + height > footer_line)
        top = std::max(field.y - height, body_top);
    return {field.x, top, field.width, height};
}

Rect choice_item(const Rect& menu, int32_t place) noexcept {
    return {
        menu.x + 1,
        menu.y + 1 + place * compact_metrics.choice_item_height,
        menu.width - 2,
        compact_metrics.choice_item_height
    };
}

void draw_lock(const Canvas& canvas, const Rect& area, const LockLook& look) {
    if (!drawable(canvas))
        return;
    int32_t width = 0;
    if (canvas.fonts != nullptr)
        width = text_width(*canvas.fonts, FontRole::small, look.text);
    const int32_t left =
        area.x + area.width - width - compact_metrics.padlock_gap - compact_metrics.padlock_width;
    renderer::draw_mark(
        *canvas.surface,
        canvas.placement,
        kPadlock,
        left,
        area.y + (area.height - compact_metrics.padlock_height) / 2,
        rgb(colour::lock)
    );
    const Rect text_area{
        left + compact_metrics.padlock_width + compact_metrics.padlock_gap,
        area.y,
        width,
        area.height
    };
    paint_text(canvas, FontRole::small, look.text, text_area, Align::left, colour::lock);
}

void draw_scroll_bar(const Canvas& canvas, const Rect& well, const Rect& thumb, bool hot) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, well, rgb(colour::well));
    renderer::draw_outline(
        *canvas.surface,
        canvas.placement,
        well,
        rgb(hot ? colour::control_hover : colour::control_border)
    );
    renderer::fill_source_rect(
        *canvas.surface,
        canvas.placement,
        thumb,
        rgb(hot ? colour::switch_idle : colour::control_hover)
    );
}

void draw_oa_button(const Canvas& canvas, int32_t side, OaButtonState state) {
    if (!drawable(canvas))
        return;
    const Rect whole{0, 0, side, side};
    Colour face = colour::panel;
    if (state == OaButtonState::hovered)
        face = colour::hover;
    else if (state == OaButtonState::pressed)
        face = colour::band;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, whole, rgb(face));
    if (state == OaButtonState::pressed)
        renderer::draw_bevel(
            *canvas.surface,
            canvas.placement,
            whole,
            rgb(colour::edge_dark),
            rgb(colour::edge_light)
        );
    else
        renderer::draw_bevel(
            *canvas.surface,
            canvas.placement,
            whole,
            rgb(colour::edge_light),
            rgb(colour::edge_dark)
        );
    if (renderer::picture_drawable(canvas.icon)) {
        if (state == OaButtonState::hovered)
            renderer::draw_outline(
                *canvas.surface, canvas.placement, grown(whole, -1), rgb(colour::accent)
            );
        const int32_t pressed_in =
            state == OaButtonState::pressed ? compact_metrics.button_icon_press : 0;
        const int32_t icon_side = side - 2 * compact_metrics.button_icon_inset;
        renderer::draw_picture(
            *canvas.surface,
            canvas.placement,
            {compact_metrics.button_icon_inset + pressed_in,
             compact_metrics.button_icon_inset + pressed_in,
             icon_side,
             icon_side},
            canvas.icon
        );
        return;
    }
    draw_mark_square(
        canvas, side, state == OaButtonState::idle ? colour::accent : colour::accent_light
    );
}

void draw_oa_mark(const Canvas& canvas, int32_t side) {
    if (!drawable(canvas) || side <= 0)
        return;
    if (renderer::picture_drawable(canvas.icon)) {
        renderer::draw_picture(*canvas.surface, canvas.placement, {0, 0, side, side}, canvas.icon);
        return;
    }
    draw_mark_square(canvas, side, colour::accent);
}

void draw_header_mark(const Canvas& canvas, const Rect& place) {
    if (!drawable(canvas))
        return;
    if (renderer::picture_drawable(canvas.icon)) {
        renderer::draw_picture(*canvas.surface, canvas.placement, place, canvas.icon);
        return;
    }
    const int32_t side = compact_metrics.mark_square;
    const Rect square{
        place.x + (place.width - side) / 2,
        place.y + (place.height - side) / 2,
        side,
        side,
    };
    renderer::draw_outline(*canvas.surface, canvas.placement, square, rgb(colour::accent));
    renderer::draw_mark(
        *canvas.surface,
        canvas.placement,
        kSmallMark,
        square.x + (square.width - kSmallMark.width) / 2,
        square.y + (square.height - kSmallMark.height + 1) / 2,
        rgb(colour::accent)
    );
}

void add_button(
    DisplayList& list, const Rect& rect, const ButtonLook& look, ControlId id, std::string name
) {
    Item item;
    item.role = Role::button;
    item.rect = rect;
    item.control = id;
    item.text = look.caption;
    item.state.hovered = look.hovered;
    item.state.pressed = look.held;
    item.state.disabled = !look.enabled;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list,
        rect,
        id,
        std::move(name),
        ControlKind::button,
        false,
        false,
        look.caption,
        look.enabled
    );
}

void add_switch(
    DisplayList& list, const Rect& rect, const SwitchLook& look, ControlId id, std::string name
) {
    const std::string text = look.on ? look.on_caption : look.off_caption;
    Item item;
    item.role = Role::toggle;
    item.rect = rect;
    item.control = id;
    item.text = text;
    item.state.hovered = look.hovered;
    item.state.on = look.on;
    item.state.locked = look.locked;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list, rect, id, std::move(name), ControlKind::toggle, true, look.on, text, !look.locked
    );
}

void add_levels(
    DisplayList& list, const Rect& rect, const LevelsLook& look, ControlId id, std::string name
) {
    const std::string text =
        look.chosen < look.captions.size() ? look.captions[look.chosen] : std::string{};
    Item item;
    item.role = Role::levels;
    item.rect = rect;
    item.control = id;
    item.text = text;
    item.state.hovered = look.hovered;
    item.state.locked = look.locked;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list, rect, id, std::move(name), ControlKind::levels, true, false, text, !look.locked
    );
}

void add_slider(
    DisplayList& list, const Rect& rect, const SliderLook& look, ControlId id, std::string name
) {
    const std::string text = std::to_string(look.stop);
    Item item;
    item.role = Role::slider;
    item.rect = rect;
    item.control = id;
    item.text = text;
    item.state.hovered = look.hovered;
    item.state.locked = look.locked;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list, rect, id, std::move(name), ControlKind::slider, true, false, text, !look.locked
    );
}

void add_choice(
    DisplayList& list, const Rect& rect, const ChoiceLook& look, ControlId id, std::string name
) {
    Item item;
    item.role = Role::choice;
    item.rect = rect;
    item.control = id;
    item.text = look.text;
    item.state.hovered = look.hovered;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(list, rect, id, std::move(name), ControlKind::choice, true, false, look.text, true);
}

void add_focus_ring(DisplayList& list, const Rect& control, bool around) {
    Item item;
    item.role = Role::focus_ring;
    item.rect = control;
    item.look = FocusRingLook{around};
    list.items.push_back(std::move(item));
}

void add_lock(DisplayList& list, const Rect& area, const LockLook& look) {
    Item item;
    item.role = Role::lock;
    item.rect = area;
    item.text = look.text;
    item.look = look;
    list.items.push_back(std::move(item));
}

void add_mark(DisplayList& list, Point at, Mark mark, Colour ink) {
    Item item;
    item.role = Role::mark;
    item.rect = {at.x, at.y, 0, 0};
    item.colour = ink;
    item.look = MarkLook{mark, ink};
    list.items.push_back(std::move(item));
}

void paint(const Canvas& canvas, const DisplayList& list) {
    for (const Item& item : list.items)
        paint_item(canvas, item);
}

} // namespace oa::ui::kit
