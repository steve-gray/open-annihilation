// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The window's chrome, drawn for the crisp backend. The arithmetic is the
// settings dialog's, moved here unchanged.

#include "oa/ui/kit/chrome.hpp"

#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/text.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace oa::ui::kit {

namespace renderer = oa::ui::frontend_renderer;

namespace {

/// The columns between the header's note and its version.
constexpr int32_t kVersionGap = 8;

/// The selected nav entry's marker, within the entry.
constexpr int32_t kMarkerOffset = 5;
constexpr int32_t kMarkerWidth = 2;
constexpr int32_t kMarkerHeight = 8;
/// An entry's text column within the entry, and the columns kept clear of its right.
constexpr int32_t kTextOffset = 12;
constexpr int32_t kTextMargin = 4;

bool drawable(const Canvas& canvas) noexcept {
    return canvas.surface != nullptr;
}

/// The title's right column: the columns kept for it, or, when the modern
/// fonts stand in for the game's font wholly, the wider of that and the
/// width they draw.
int32_t title_right(const Canvas& canvas, const Rect& title, const HeaderLook& look) {
    const bool modern_only =
        canvas.fonts != nullptr && !std::any_of(
                                       canvas.fonts->regular.font.glyphs.begin(),
                                       canvas.fonts->regular.font.glyphs.end(),
                                       [](const auto& glyph) { return glyph.has_value(); }
                                   );
    if (!modern_only)
        return title.x + title.width;
    return std::max(
        title.x + title.width,
        title.x +
            modern_tracked_width(
                *canvas.fonts, FontRole::regular, look.title, look.tracking, canvas.placement.scale
            )
    );
}

void draw_line(
    const Canvas& canvas,
    FontRole role,
    std::string_view text,
    const Rect& box,
    Align align,
    Colour ink,
    int32_t tracking
) {
    if (!drawable(canvas) || canvas.fonts == nullptr || text.empty())
        return;
    draw_boxed_text(
        *canvas.surface, canvas.placement, *canvas.fonts, role, text, box, align, rgb(ink), tracking
    );
}

} // namespace

void draw_window_face(const Canvas& canvas, const Rect& whole) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, whole, rgb(colour::panel));
}

void draw_window_edge(const Canvas& canvas, const Rect& whole) {
    if (!drawable(canvas))
        return;
    renderer::draw_bevel(
        *canvas.surface, canvas.placement, whole, rgb(colour::edge_light), rgb(colour::edge_dark)
    );
}

void draw_header(const Canvas& canvas, const HeaderLook& look) {
    if (!drawable(canvas))
        return;
    const int32_t edge = compact_metrics.edge;
    const int32_t header_top = edge;
    const int32_t header_height = compact_metrics.header_height;
    const int32_t inner = look.width - 2 * edge;
    renderer::fill_source_rect(
        *canvas.surface,
        canvas.placement,
        {edge, header_top, inner, header_height},
        rgb(colour::band)
    );
    renderer::fill_source_rect(
        *canvas.surface,
        canvas.placement,
        {edge, header_top + header_height, inner, 1},
        rgb(colour::rule)
    );
    const Rect mark{
        compact_metrics.padding,
        compact_metrics.mark_top,
        compact_metrics.mark_side,
        compact_metrics.mark_side,
    };
    draw_header_mark(canvas, mark);
    const Rect title{
        mark.x + mark.width + compact_metrics.header_gap,
        header_top,
        look.title_width,
        header_height,
    };
    draw_line(
        canvas, FontRole::regular, look.title, title, Align::left, colour::text, look.tracking
    );
    const int32_t drawn_right = title_right(canvas, title, look);
    const Rect second{
        drawn_right + compact_metrics.header_gap,
        header_top,
        look.second_width,
        header_height,
    };
    draw_line(
        canvas, FontRole::regular, look.second, second, Align::left, colour::hint, look.tracking
    );
    const int32_t content_right = look.width - edge - compact_metrics.padding;
    const Rect version{
        content_right - look.version_width, header_top, look.version_width, header_height
    };
    draw_line(canvas, FontRole::small, look.version, version, Align::right, colour::quiet, 0);
    if (look.note.empty() || canvas.fonts == nullptr)
        return;
    const int32_t version_left =
        version.x + version.width - text_width(*canvas.fonts, FontRole::small, look.version);
    const int32_t note_left = second.x + second.width + compact_metrics.header_gap;
    const Rect note{
        note_left,
        header_top,
        version_left - kVersionGap - note_left,
        header_height,
    };
    draw_line(canvas, FontRole::small, look.note, note, Align::right, colour::lock, 0);
}

void draw_footer_band(const Canvas& canvas, int32_t width, int32_t rule_row) {
    if (!drawable(canvas))
        return;
    const int32_t inner = width - 2 * compact_metrics.edge;
    renderer::fill_source_rect(
        *canvas.surface,
        canvas.placement,
        {compact_metrics.edge, rule_row, inner, 1},
        rgb(colour::rule)
    );
    renderer::fill_source_rect(
        *canvas.surface,
        canvas.placement,
        {compact_metrics.edge, rule_row + 1, inner, compact_metrics.footer_height},
        rgb(colour::band)
    );
}

void draw_nav(const Canvas& canvas, const NavLook& look) {
    if (!drawable(canvas))
        return;
    if (look.area.width > 0 && look.area.height > 0) {
        renderer::fill_source_rect(*canvas.surface, canvas.placement, look.area, rgb(colour::list));
        renderer::fill_source_rect(
            *canvas.surface,
            canvas.placement,
            {look.rule_column, look.area.y, 1, look.area.height},
            rgb(colour::rule)
        );
    }
    if (look.divider.has_value())
        renderer::fill_source_rect(
            *canvas.surface, canvas.placement, *look.divider, rgb(colour::rule)
        );
    for (const NavEntry& entry : look.entries) {
        Colour ink = colour::list_text;
        if (entry.selected) {
            renderer::fill_source_rect(
                *canvas.surface, canvas.placement, entry.rect, rgb(colour::list_selected)
            );
            renderer::fill_source_rect(
                *canvas.surface,
                canvas.placement,
                {entry.rect.x + kMarkerOffset,
                 entry.rect.y + (entry.rect.height - kMarkerHeight) / 2,
                 kMarkerWidth,
                 kMarkerHeight},
                rgb(colour::accent)
            );
            ink = colour::text;
        } else if (entry.hovered) {
            renderer::fill_source_rect(
                *canvas.surface, canvas.placement, entry.rect, rgb(colour::hover)
            );
            ink = colour::text;
        }
        const Rect caption{
            entry.rect.x + kTextOffset,
            entry.rect.y,
            entry.rect.width - kTextOffset - kTextMargin,
            entry.rect.height,
        };
        draw_line(canvas, FontRole::regular, entry.caption, caption, Align::left, ink, 0);
        if (entry.focused)
            draw_focus_ring(canvas, entry.rect, false);
    }
}

void add_nav(
    DisplayList& list,
    const NavLook& look,
    std::span<const ControlId> ids,
    std::span<const std::string> names
) {
    for (std::size_t index = 0; index < look.entries.size(); ++index) {
        const NavEntry& entry = look.entries[index];
        Item item;
        item.role = Role::nav;
        item.rect = entry.rect;
        item.text = entry.caption;
        item.state.hovered = entry.hovered;
        item.state.focused = entry.focused;
        item.state.selected = entry.selected;
        if (index == 0) {
            item.look = look;
        } else {
            NavLook one;
            one.entries.push_back(entry);
            item.look = std::move(one);
        }
        list.items.push_back(std::move(item));

        Control control;
        control.id = index < ids.size() ? ids[index] : no_control;
        control.rect = entry.rect;
        control.enabled = true;
        control.name = index < names.size() ? names[index] : std::string{};
        control.kind = ControlKind::tab;
        control.steps = false;
        control.checked = entry.selected;
        control.text = entry.caption;
        list.controls.push_back(std::move(control));
    }
}

void draw_heading(const Canvas& canvas, const Rect& rect, const std::string& text) {
    draw_line(
        canvas,
        FontRole::small,
        text,
        rect,
        Align::left,
        colour::quiet,
        compact_metrics.heading_tracking
    );
}

void draw_row_frame(const Canvas& canvas, const RowFrame& row) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(
        *canvas.surface, canvas.placement, {row.left, row.top, row.width, 1}, rgb(colour::rule)
    );
    draw_line(canvas, FontRole::regular, row.label_text, row.label, Align::left, colour::text, 0);
    const std::size_t lines = std::min(row.hints.size(), row.hint_texts.size());
    for (std::size_t line = 0; line < lines; ++line) {
        const bool notice = line < row.hint_notices.size() && row.hint_notices[line];
        Canvas target = canvas;
        if (line < row.hint_clips.size() && row.hint_clips[line].width > 0 &&
            row.hint_clips[line].height > 0)
            target = clipped(canvas, row.hint_clips[line]);
        draw_line(
            target,
            FontRole::small,
            row.hint_texts[line],
            row.hints[line],
            Align::left,
            notice ? colour::lock : colour::hint,
            0
        );
    }
}

void draw_locked_fade(const Canvas& canvas, const Rect& rect) {
    if (!drawable(canvas))
        return;
    renderer::blend_source_rect(
        *canvas.surface, canvas.placement, rect, rgb(colour::panel), locked_fade
    );
}

void draw_rule(const Canvas& canvas, int32_t left, int32_t row, int32_t width) {
    if (!drawable(canvas))
        return;
    renderer::fill_source_rect(
        *canvas.surface, canvas.placement, {left, row, width, 1}, rgb(colour::rule)
    );
}

} // namespace oa::ui::kit
