// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's controls, drawn through the canvas's primitives. The
// arithmetic is the dialog's, moved here unchanged. paint draws every role,
// the controls of controls.hpp among them.

#include "oa/ui/kit/components.hpp"

#include "oa/ui/kit/chrome.hpp"
#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/controls.hpp"

#include "oa/ui/frontend_renderer/artless.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

/// A search field's magnifier, 7 by 7, row by row.
constexpr std::array<uint8_t, 7 * 7> kMagnifierBits{
    0, 1, 1, 1, 0, 0, 0, //
    1, 0, 0, 0, 1, 0, 0, //
    1, 0, 0, 0, 1, 0, 0, //
    1, 0, 0, 0, 1, 0, 0, //
    0, 1, 1, 1, 1, 0, 0, //
    0, 0, 0, 0, 0, 1, 0, //
    0, 0, 0, 0, 0, 0, 1, //
};

/// The OA button's attention dot, 7 by 7, row by row: the whole disc, which
/// its ring's colour fills.
constexpr std::array<uint8_t, 7 * 7> kDotBits{
    0, 0, 1, 1, 1, 0, 0, //
    0, 1, 1, 1, 1, 1, 0, //
    1, 1, 1, 1, 1, 1, 1, //
    1, 1, 1, 1, 1, 1, 1, //
    1, 1, 1, 1, 1, 1, 1, //
    0, 1, 1, 1, 1, 1, 0, //
    0, 0, 1, 1, 1, 0, 0, //
};
/// The dot's face inside its one-point ring, 5 by 5, row by row, drawn one
/// point in from the disc's top left.
constexpr std::array<uint8_t, 5 * 5> kDotFaceBits{
    0, 1, 1, 1, 0, //
    1, 1, 1, 1, 1, //
    1, 1, 1, 1, 1, //
    1, 1, 1, 1, 1, //
    0, 1, 1, 1, 0, //
};

constexpr renderer::Mark kSmallMark{9, 7, kSmallMarkBits};
constexpr renderer::Mark kDot{7, 7, kDotBits};
constexpr renderer::Mark kDotFace{5, 5, kDotFaceBits};
/// The dot's ring, in points: its face starts this far in from the disc's edge.
constexpr int32_t kDotRing = 1;
static_assert(
    kDot.width == oa_button_dot_side && kDot.height == oa_button_dot_side &&
        kDotFace.width == oa_button_dot_side - 2 * kDotRing &&
        kDotFace.height == oa_button_dot_side - 2 * kDotRing,
    "the dot is oa_button_dot_side square, its face one ring in"
);
constexpr renderer::Mark kLargeMark{12, 9, kLargeMarkBits};
constexpr renderer::Mark kPadlock{5, 7, kPadlockBits};
constexpr renderer::Mark kClosedArrow{5, 5, kClosedArrowBits};
constexpr renderer::Mark kOpenArrow{5, 5, kOpenArrowBits};
constexpr renderer::Mark kChoiceArrow{7, 4, kChoiceArrowBits};
constexpr renderer::Mark kMagnifier{7, 7, kMagnifierBits};
static_assert(
    kMagnifier.width == compact_metrics.magnifier_side &&
        kMagnifier.height == compact_metrics.magnifier_side,
    "the magnifier is magnifier_side square"
);

/// The chosen menu item's marker, the same mark the section list uses.
constexpr int32_t kChosenMarkerOffset = 5;
constexpr int32_t kChosenMarkerWidth = 2;
constexpr int32_t kChosenMarkerHeight = 8;
/// The columns a menu item keeps clear at its right.
constexpr int32_t kChoiceTextMargin = 4;

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
    case Mark::magnifier:
        return kMagnifier;
    }
    return kSmallMark;
}

bool drawable(const Canvas& canvas) noexcept {
    return can_draw(canvas);
}

void paint_text(
    const Canvas& canvas,
    FontRole role,
    std::string_view text,
    const Rect& box,
    Align align,
    Colour ink
) {
    box_text(canvas, role, text, box, align, ink);
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
    outline_rect(canvas, square, ink);
    const renderer::Mark& mark =
        square_side - 2 >= kLargeMark.width + 2 * compact_metrics.large_mark_margin ? kLargeMark
                                                                                    : kSmallMark;
    draw_mark(
        canvas,
        mark,
        {square_offset + (square_side - mark.width) / 2,
         square_offset + (square_side - mark.height + 1) / 2},
        ink
    );
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
    switch (item.role) {
    case Role::fill:
        fill_rect(drawn, item.rect, item.colour);
        break;
    case Role::blend:
        blend_rect(drawn, item.rect, item.colour, item.opacity);
        break;
    case Role::outline:
        outline_rect(drawn, item.rect, item.colour);
        break;
    case Role::bevel:
        bevel_rect(drawn, item.rect, item.colour, colour::edge_dark);
        break;
    case Role::rule:
        rule_rect(drawn, item.rect, item.colour);
        break;
    case Role::text:
        paint_text(drawn, item.font, item.text, item.rect, item.align, item.colour);
        break;
    case Role::picture:
        if (const auto* look = std::get_if<PictureLook>(&item.look))
            draw_picture(drawn, item.rect, look->picture);
        else
            draw_picture(drawn, item.rect, drawn.icon);
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
            draw_oa_button(
                placed_at(drawn, {item.rect.x, item.rect.y}), look->side, look->state, look->dot
            );
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
    case Role::list_row:
        if (const auto* look = std::get_if<ListRowLook>(&item.look))
            draw_list_row(drawn, item.rect, *look);
        break;
    case Role::chip:
        if (const auto* look = std::get_if<ChipLook>(&item.look))
            draw_chip(drawn, item.rect, *look);
        break;
    case Role::field:
        if (const auto* look = std::get_if<SearchLook>(&item.look))
            draw_search(drawn, item.rect, *look);
        break;
    case Role::tabs:
        if (const auto* look = std::get_if<TabsLook>(&item.look))
            draw_tabs(drawn, item.rect, *look);
        break;
    case Role::card:
        if (const auto* look = std::get_if<CardLook>(&item.look))
            draw_card(drawn, item.rect, *look);
        break;
    case Role::hover_card:
        if (const auto* look = std::get_if<HoverCardLook>(&item.look))
            draw_hover_card(drawn, item.rect, *look);
        break;
    case Role::link:
        if (const auto* look = std::get_if<LinkLook>(&item.look))
            draw_link(drawn, item.rect, *look);
        break;
    case Role::progress:
        if (const auto* look = std::get_if<ProgressLook>(&item.look))
            draw_progress(drawn, item.rect, look->done, look->whole);
        break;
    case Role::header_bar:
    case Role::badge:
    case Role::header_text:
    case Role::version:
    case Role::title:
    case Role::panel:
    case Role::icon:
    case Role::banner:
    case Role::row:
    case Role::divider:
    case Role::button_main:
    case Role::button_danger:
    case Role::progress_track:
    case Role::progress_fill:
    case Role::progress_busy:
    case Role::backdrop:
    case Role::sheet:
        // A screen drawn in the modern fonts draws these parts with its own
        // painter; the kit's canvas, in either form, draws nothing for them.
        break;
    }
}

// ---- State icons ----
//
// A state icon is drawn from rings and strokes worked out at the canvas's own
// pixels, so that it keeps its edges at every scale. Its design is a square
// of 24 units, each written here in quarters; the icon fills the shorter
// side of the rectangle it is drawn in, centred along the longer.

/// A state icon's square, in quarter units: 24 units.
constexpr int64_t kIconGrid = 96;
/// The samples across, and the samples down, each pixel a state icon's
/// coverage is counted from: 16 a pixel, so its edges take 17 shades.
constexpr int64_t kIconSamples = 4;
/// The most pixels across or down a state icon is drawn at; a larger
/// rectangle draws nothing.
constexpr int32_t kMostIconPixels = 1024;

/// A point of a state icon's design, in quarter units from its top left.
struct IconPoint {
    int64_t x{}; ///< across
    int64_t y{}; ///< down
};

/// A stroke of a state icon's design: a straight line with round ends.
struct IconStroke {
    IconPoint from{}; ///< one end
    IconPoint to{};   ///< the other end
};

/// The ring of the circled icons: its centre, and the radii of its outer and
/// inner edges, in quarter units: 2 units wide, its outer edge a unit in.
constexpr IconPoint kRingCentre{48, 48};
constexpr int64_t kRingOuter = 44;
constexpr int64_t kRingInner = 36;
/// Half a stroke's width, in quarter units: every stroke is 2.5 units wide.
constexpr int64_t kStrokeHalf = 5;

/// The check: down to the right, then up to the right.
constexpr std::array<IconStroke, 2> kCheckStrokes{{
    {{26, 50}, {42, 66}},
    {{42, 66}, {70, 34}},
}};
/// The up-arrow: its shaft, then its head's two sides.
constexpr std::array<IconStroke, 3> kUpArrowStrokes{{
    {{48, 70}, {48, 28}},
    {{30, 46}, {48, 28}},
    {{48, 28}, {66, 46}},
}};
/// The download arrow: its shaft and its head's two sides, then the tray
/// under it, its two sides and its bottom.
constexpr std::array<IconStroke, 6> kDownloadStrokes{{
    {{48, 12}, {48, 62}},
    {{26, 40}, {48, 62}},
    {{48, 62}, {70, 40}},
    {{14, 62}, {14, 82}},
    {{14, 82}, {82, 82}},
    {{82, 82}, {82, 62}},
}};

/// Returns a state icon's strokes.
///
/// @param icon the icon
/// @return its strokes, in quarter units
std::span<const IconStroke> strokes_of(StateIcon icon) noexcept {
    switch (icon) {
    case StateIcon::circled_check:
        return kCheckStrokes;
    case StateIcon::circled_up_arrow:
        return kUpArrowStrokes;
    case StateIcon::download_arrow:
        return kDownloadStrokes;
    }
    return {};
}

/// Tells whether a state icon has the ring round its strokes.
///
/// @param icon the icon
/// @return true for the circled icons
bool ringed(StateIcon icon) noexcept {
    return icon == StateIcon::circled_check || icon == StateIcon::circled_up_arrow;
}

/// Tells whether a point lies within a distance of a stroke.
///
/// @param point the point
/// @param stroke the stroke
/// @param half_width the distance, in the same units
/// @return true when the point is that near the stroke or its round ends
bool near_stroke(IconPoint point, const IconStroke& stroke, int64_t half_width) noexcept {
    const int64_t along_x = stroke.to.x - stroke.from.x;
    const int64_t along_y = stroke.to.y - stroke.from.y;
    const int64_t from_x = point.x - stroke.from.x;
    const int64_t from_y = point.y - stroke.from.y;
    const int64_t reach_squared = half_width * half_width;
    const int64_t length_squared = along_x * along_x + along_y * along_y;
    const int64_t projected = from_x * along_x + from_y * along_y;
    if (length_squared == 0 || projected <= 0)
        return from_x * from_x + from_y * from_y <= reach_squared;
    if (projected >= length_squared) {
        const int64_t to_x = point.x - stroke.to.x;
        const int64_t to_y = point.y - stroke.to.y;
        return to_x * to_x + to_y * to_y <= reach_squared;
    }
    const int64_t across = from_x * along_y - from_y * along_x;
    return across * across <= reach_squared * length_squared;
}

/// Returns how much of each pixel of a rectangle a state icon covers.
///
/// The icon fills a square of the rectangle's shorter side, centred along
/// the longer. Its design is scaled to that square, each place rounded to
/// an eighth of a pixel, and each pixel counts which of its 16 samples, at
/// the middles of a 4 by 4 grid, lie in the ring or within half a stroke's
/// width of a stroke. The arithmetic is in integers, so every machine draws
/// the same pixels.
///
/// @param icon the icon
/// @param columns the rectangle's pixels across, at most kMostIconPixels
/// @param rows the rectangle's pixels down, at most kMostIconPixels
/// @return columns times rows bytes, rows top to bottom: 0 uncovered to 255 covered
std::vector<uint8_t> state_icon_coverage(StateIcon icon, int32_t columns, int32_t rows) {
    std::vector<uint8_t> coverage(
        static_cast<std::size_t>(std::max(columns, 0)) * static_cast<std::size_t>(std::max(rows, 0))
    );
    const int32_t side = std::min(columns, rows);
    if (side <= 0)
        return coverage;
    const int32_t left = (columns - side) / 2;
    const int32_t top = (rows - side) / 2;
    // Places in eighths of a pixel of the square: a sample lies at an odd
    // eighth, the middle of its sixteenth of the pixel.
    const int64_t square = 2 * kIconSamples * side;
    const auto scaled = [square](int64_t quarters) {
        return (quarters * square + kIconGrid / 2) / kIconGrid;
    };
    const IconPoint centre{scaled(kRingCentre.x), scaled(kRingCentre.y)};
    const int64_t outer = scaled(kRingOuter);
    const int64_t inner = scaled(kRingInner);
    const int64_t half_width = scaled(kStrokeHalf);
    std::vector<IconStroke> strokes;
    for (const IconStroke& stroke : strokes_of(icon))
        strokes.push_back(
            {{scaled(stroke.from.x), scaled(stroke.from.y)},
             {scaled(stroke.to.x), scaled(stroke.to.y)}}
        );
    const bool ring = ringed(icon);
    const auto covers = [&](IconPoint point) {
        if (ring) {
            const int64_t across = point.x - centre.x;
            const int64_t down = point.y - centre.y;
            const int64_t distance_squared = across * across + down * down;
            if (distance_squared <= outer * outer && distance_squared >= inner * inner)
                return true;
        }
        for (const IconStroke& stroke : strokes)
            if (near_stroke(point, stroke, half_width))
                return true;
        return false;
    };
    constexpr int64_t samples = kIconSamples * kIconSamples;
    for (int32_t row = 0; row < side; ++row)
        for (int32_t column = 0; column < side; ++column) {
            int64_t inside = 0;
            for (int64_t down = 0; down < kIconSamples; ++down)
                for (int64_t across = 0; across < kIconSamples; ++across)
                    if (covers(
                            {2 * kIconSamples * column + 2 * across + 1,
                             2 * kIconSamples * row + 2 * down + 1}
                        ))
                        ++inside;
            const std::size_t at =
                static_cast<std::size_t>(top + row) * static_cast<std::size_t>(columns) +
                static_cast<std::size_t>(left + column);
            coverage[at] = static_cast<uint8_t>((inside * 255 + samples / 2) / samples);
        }
    return coverage;
}

} // namespace

Point mark_size(Mark mark) noexcept {
    const renderer::Mark& bits = bits_of(mark);
    return {bits.width, bits.height};
}

void draw_mark(const Canvas& canvas, Mark mark, Point at, Colour ink) {
    if (!drawable(canvas))
        return;
    draw_mark(canvas, bits_of(mark), at, ink);
}

void draw_state_icon(const Canvas& canvas, const Rect& rect, StateIcon icon, Colour ink) {
    if (!drawable(canvas))
        return;
    const Point pixels = covered_pixels(canvas, rect);
    if (pixels.x <= 0 || pixels.y <= 0 || pixels.x > kMostIconPixels || pixels.y > kMostIconPixels)
        return;
    const std::vector<uint8_t> coverage = state_icon_coverage(icon, pixels.x, pixels.y);
    blend_coverage(canvas, rect, coverage, ink);
}

void draw_focus_ring(const Canvas& canvas, const Rect& control, bool around) {
    if (!drawable(canvas))
        return;
    const Rect ring = around ? grown(control, compact_metrics.focus_inset) : control;
    outline_rect(canvas, ring, colour::accent);
}

void draw_button(const Canvas& canvas, const Rect& area, const ButtonLook& look) {
    if (!drawable(canvas))
        return;
    const ButtonColours colours = button_colours(look);
    if (colours.face)
        fill_rect(canvas, area, *colours.face);
    outline_rect(canvas, area, colours.border);
    paint_text(canvas, FontRole::small, look.caption, area, Align::centre, colours.caption);
}

void draw_switch(const Canvas& canvas, const Rect& area, const SwitchLook& look) {
    if (!drawable(canvas))
        return;
    fill_rect(canvas, area, colour::well);
    outline_rect(canvas, area, look.hovered ? colour::control_hover : colour::control_border);
    const int32_t half = (area.width - 2) / 2;
    const Rect off{area.x + 1, area.y + 1, half, area.height - 2};
    const Rect on_half{area.x + 1 + half, area.y + 1, half, area.height - 2};
    if (look.on)
        fill_rect(canvas, on_half, look.locked ? colour::control_hover : colour::accent);
    else
        fill_rect(canvas, off, colour::off_selected);
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
    fill_rect(canvas, area, colour::well);
    outline_rect(canvas, area, look.hovered ? colour::control_hover : colour::control_border);
    for (std::size_t index = 0; index < look.captions.size(); ++index) {
        const Rect segment{
            area.x + 1 + static_cast<int32_t>(index) * look.level_width,
            area.y + 1,
            look.level_width,
            area.height - 2,
        };
        const bool selected = index == look.chosen;
        if (selected)
            fill_rect(canvas, segment, look.locked ? colour::control_hover : colour::accent);
        paint_text(
            canvas,
            FontRole::small,
            look.captions[index],
            segment,
            Align::centre,
            selected ? colour::on_accent : colour::button_text
        );
        if (index >= look.offered)
            blend_rect(canvas, segment, colour::panel, locked_fade);
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
    fill_rect(canvas, track, colour::well);
    outline_rect(canvas, track, colour::control_border);
    const int32_t knob = knob_column(area, look.stop, look.stops);
    fill_rect(
        canvas,
        {track.x + 1, track.y + 1, knob - track.x - 1, track.height - 2},
        look.locked ? colour::control_border : colour::accent
    );
    const int32_t travel = area.width - compact_metrics.knob_width;
    if (look.stops > 1 && travel / (look.stops - 1) >= compact_metrics.least_stop_spacing) {
        for (int32_t index = 0; index < look.stops; ++index) {
            fill_rect(
                canvas,
                {knob_column(area, index, look.stops),
                 area.y + compact_metrics.stop_offset,
                 1,
                 compact_metrics.stop_height},
                colour::control_border
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
    fill_rect(canvas, knob_rect, face);
    outline_rect(canvas, knob_rect, colour::on_accent);
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
    fill_rect(canvas, area, colour::well);
    Colour border = colour::control_border;
    if (look.open)
        border = colour::accent;
    else if (look.hovered)
        border = colour::control_hover;
    outline_rect(canvas, area, border);
    const Rect text_box{
        area.x + compact_metrics.choice_text_inset,
        area.y,
        area.width - compact_metrics.choice_text_inset - compact_metrics.choice_arrow_room,
        area.height,
    };
    paint_text(
        clipped(canvas, text_box), FontRole::regular, look.text, text_box, Align::left, colour::text
    );
    draw_mark(
        canvas,
        kChoiceArrow,
        {area.x + area.width - compact_metrics.choice_arrow_room +
             (compact_metrics.choice_arrow_room - compact_metrics.choice_arrow_width) / 2,
         area.y + (area.height - compact_metrics.choice_arrow_height) / 2},
        look.open ? colour::accent : colour::button_text
    );
}

namespace {

/// Returns the row of one choice, when the menu names its rows.
///
/// @param look the menu
/// @param item the choice, from 0
/// @return the row; null for an ordinary menu, or an item past the rows
const ChoiceMenuRow* menu_row(const ChoiceMenuLook& look, int32_t item) noexcept {
    if (item < 0 || static_cast<std::size_t>(item) >= look.items.size())
        return nullptr;
    return &look.items[static_cast<std::size_t>(item)];
}

/// Draws one menu row's label and, to its right, its aside, with a gap
/// between them. An aside that would meet the label is clipped to the room
/// left of that gap.
///
/// @param canvas where it is drawn
/// @param box the row
/// @param label the row's text
/// @param aside the right-aligned text; empty for none
/// @param ink the text's colour
void draw_menu_text(
    const Canvas& canvas,
    const Rect& box,
    std::string_view label,
    std::string_view aside,
    Colour ink
) {
    const int32_t inner_left = box.x + compact_metrics.choice_item_text_inset;
    const int32_t inner_right = box.x + box.width - kChoiceTextMargin;
    int32_t label_right = inner_right;
    if (!aside.empty()) {
        // Eight points between the label and the aside, and eight kept for
        // the label, so a long aside is clipped before it meets the label.
        constexpr int32_t kGap = 8;
        constexpr int32_t kMinLabel = 8;
        int32_t aside_width = 0;
        if (canvas.fonts != nullptr)
            aside_width = text_width(*canvas.fonts, FontRole::regular, aside);
        int32_t room = inner_right - inner_left - kMinLabel - kGap;
        if (room < 1)
            room = 1;
        if (aside_width > room)
            aside_width = room;
        const int32_t aside_left = inner_right - aside_width;
        label_right = aside_left - kGap;
        const Rect aside_box{aside_left, box.y, aside_width, box.height};
        paint_text(
            clipped(canvas, aside_box), FontRole::regular, aside, aside_box, Align::right, ink
        );
    }
    const Rect caption{
        inner_left, box.y, std::max(label_right - inner_left, int32_t{0}), box.height
    };
    paint_text(clipped(canvas, caption), FontRole::regular, label, caption, Align::left, ink);
}

} // namespace

void draw_choice_menu(const Canvas& canvas, const Rect& menu, const ChoiceMenuLook& look) {
    if (!drawable(canvas))
        return;
    fill_rect(canvas, menu, colour::list);
    outline_rect(canvas, menu, colour::control_hover);
    const int32_t count = static_cast<int32_t>(look.shown.size());
    for (int32_t place = 0; place < count; ++place) {
        const int32_t item = look.first + place;
        if (look.total >= 0 && item >= look.total)
            break;
        const Rect box = choice_item(menu, place);
        const ChoiceMenuRow* row = menu_row(look, item);
        // A divider is a section heading: no selection, no marker, no focus.
        if (row != nullptr && row->divider) {
            const Rect heading{
                box.x + compact_metrics.choice_item_text_inset,
                box.y,
                std::max(box.width - compact_metrics.choice_item_text_inset - kChoiceTextMargin, 0),
                box.height,
            };
            draw_heading(canvas, heading, look.shown[static_cast<std::size_t>(place)]);
            continue;
        }
        const bool choosable = row == nullptr || row->choosable;
        Colour text = choosable ? colour::list_text : colour::quiet;
        if (choosable && item == look.chosen) {
            fill_rect(canvas, box, colour::list_selected);
            fill_rect(
                canvas,
                {box.x + kChosenMarkerOffset,
                 box.y + (box.height - kChosenMarkerHeight) / 2,
                 kChosenMarkerWidth,
                 kChosenMarkerHeight},
                colour::accent
            );
            text = colour::text;
        }
        if (item == look.marked) {
            if (item != look.chosen || !choosable)
                fill_rect(canvas, box, colour::hover);
            if (choosable)
                text = colour::text;
            if (look.focus_shown)
                outline_rect(canvas, box, colour::accent);
        }
        const std::string_view aside =
            row != nullptr ? std::string_view{row->aside} : std::string_view{};
        if (aside.empty()) {
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
        } else {
            draw_menu_text(canvas, box, look.shown[static_cast<std::size_t>(place)], aside, text);
        }
    }
    const int32_t window =
        shown_choices(static_cast<std::size_t>(std::max(look.total, int32_t{0})));
    if (look.total > window) {
        const int32_t travel = menu.height - 2;
        const int32_t height = std::max(travel * window / look.total, 4);
        const int32_t top =
            menu.y + 1 + (travel - height) * look.first / std::max(look.total - window, 1);
        fill_rect(canvas, {menu.x + menu.width - 3, top, 2, height}, colour::control_hover);
    }
}

int32_t shown_choices(std::size_t choices) noexcept {
    const auto most = static_cast<std::size_t>(compact_metrics.most_shown_choices);
    return static_cast<int32_t>(std::min(choices, most));
}

Rect choice_menu(const Rect& field, std::size_t choices, int32_t footer_line) noexcept {
    const int32_t height = shown_choices(choices) * compact_metrics.choice_item_height + 2;
    int32_t top = field.y + field.height;
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

int32_t choice_item_at(const Rect& menu, int32_t x, int32_t y) noexcept {
    if (x < menu.x || y < menu.y || x >= menu.x + menu.width || y >= menu.y + menu.height)
        return -1;
    const int32_t local = y - (menu.y + 1);
    if (local < 0)
        return -1;
    const int32_t place = local / compact_metrics.choice_item_height;
    const Rect box = choice_item(menu, place);
    if (x < box.x || y < box.y || x >= box.x + box.width || y >= box.y + box.height)
        return -1;
    return place;
}

int32_t choice_mark_toward(const ChoiceMenuLook& look, int32_t marked, int32_t step) noexcept {
    const int32_t last = std::max(look.total, int32_t{0}) - 1;
    if (last < 0)
        return 0;
    const auto divider = [&look](int32_t index) noexcept {
        return index >= 0 && static_cast<std::size_t>(index) < look.items.size() &&
               look.items[static_cast<std::size_t>(index)].divider;
    };
    if (step == 0)
        return std::clamp(marked, int32_t{0}, last);
    const int32_t direction = step > 0 ? 1 : -1;
    int32_t target = std::clamp(marked + step, int32_t{0}, last);
    if (!divider(target))
        return target;
    const int32_t skipped = target + direction;
    if (skipped < 0 || skipped > last || divider(skipped))
        return std::clamp(marked, int32_t{0}, last);
    return skipped;
}

void draw_lock(const Canvas& canvas, const Rect& area, const LockLook& look) {
    if (!drawable(canvas))
        return;
    const int32_t width = measure_text(canvas, FontRole::small, look.text);
    const int32_t left =
        area.x + area.width - width - compact_metrics.padlock_gap - compact_metrics.padlock_width;
    draw_mark(
        canvas,
        kPadlock,
        {left, area.y + (area.height - compact_metrics.padlock_height) / 2},
        colour::lock
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
    fill_rect(canvas, well, colour::well);
    outline_rect(canvas, well, hot ? colour::control_hover : colour::control_border);
    fill_rect(canvas, thumb, hot ? colour::switch_idle : colour::control_hover);
}

namespace {

/// Draws the OA button's face at the canvas's origin: the bevelled square,
/// and the icon inset or the OA mark.
///
/// @param canvas where it is drawn; its icon is the button's
/// @param side the square's side, in points
/// @param state how it looks
void draw_oa_button_face(const Canvas& canvas, int32_t side, OaButtonState state) {
    const Rect whole{0, 0, side, side};
    Colour face = colour::panel;
    if (state == OaButtonState::hovered)
        face = colour::hover;
    else if (state == OaButtonState::pressed)
        face = colour::band;
    fill_rect(canvas, whole, face);
    if (state == OaButtonState::pressed)
        bevel_rect(canvas, whole, colour::edge_dark, colour::edge_light);
    else
        bevel_rect(canvas, whole, colour::edge_light, colour::edge_dark);
    if (renderer::picture_drawable(canvas.icon)) {
        if (state == OaButtonState::hovered)
            outline_rect(canvas, grown(whole, -1), colour::accent);
        const int32_t pressed_in =
            state == OaButtonState::pressed ? compact_metrics.button_icon_press : 0;
        const int32_t icon_side = side - 2 * compact_metrics.button_icon_inset;
        draw_picture(
            canvas,
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

/// Draws the attention dot over the OA button's top right corner
/// (oa_button_dot): the disc in the outline token, then its face one ring
/// in, in the warning token.
///
/// @param canvas where the button is drawn
/// @param side the button's side, in points
void draw_attention_dot(const Canvas& canvas, int32_t side) {
    const Rect dot = oa_button_dot(side);
    draw_mark(canvas, kDot, {dot.x, dot.y}, colour::control_border);
    draw_mark(canvas, kDotFace, {dot.x + kDotRing, dot.y + kDotRing}, colour::lock);
}

} // namespace

void draw_oa_button(const Canvas& canvas, int32_t side, OaButtonState state, bool dot) {
    if (!drawable(canvas))
        return;
    draw_oa_button_face(canvas, side, state);
    if (dot)
        draw_attention_dot(canvas, side);
}

void draw_oa_mark(const Canvas& canvas, int32_t side) {
    if (!drawable(canvas) || side <= 0)
        return;
    if (renderer::picture_drawable(canvas.icon)) {
        draw_picture(canvas, {0, 0, side, side}, canvas.icon);
        return;
    }
    draw_mark_square(canvas, side, colour::accent);
}

void draw_header_mark(const Canvas& canvas, const Rect& place) {
    if (!drawable(canvas))
        return;
    if (renderer::picture_drawable(canvas.icon)) {
        draw_picture(canvas, place, canvas.icon);
        return;
    }
    const int32_t side = compact_metrics.mark_square;
    const Rect square{
        place.x + (place.width - side) / 2,
        place.y + (place.height - side) / 2,
        side,
        side,
    };
    outline_rect(canvas, square, colour::accent);
    draw_mark(
        canvas,
        kSmallMark,
        {square.x + (square.width - kSmallMark.width) / 2,
         square.y + (square.height - kSmallMark.height + 1) / 2},
        colour::accent
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
