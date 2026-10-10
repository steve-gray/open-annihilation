// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// List rows, chips, text and search fields, tabs, cards and their grid,
// hover cards and links, drawn for the crisp backend. Every colour is a kit
// token and every size a member of compact_metrics.

#include "oa/ui/kit/controls.hpp"

#include "oa/ui/kit/chrome.hpp"

#include "oa/ui/frontend_renderer/artless.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::kit {

namespace renderer = oa::ui::frontend_renderer;

namespace {

/// The sizes every control here is drawn at.
constexpr const Metrics& kMetrics = compact_metrics;

/// What a cut text ends with.
constexpr std::string_view kEllipsis = "...";

/// Tells whether a canvas has a surface to draw on.
///
/// @param canvas the canvas
/// @return true when it has one
bool drawable(const Canvas& canvas) noexcept {
    return canvas.surface != nullptr;
}

/// Returns a text's width on a canvas, 0 when it has no fonts.
///
/// @param canvas the canvas
/// @param role the font
/// @param text the text, in UTF-8
/// @return the width, in points
int32_t measured(const Canvas& canvas, FontRole role, std::string_view text) {
    if (canvas.fonts == nullptr)
        return 0;
    return text_width(*canvas.fonts, role, text);
}

/// Fills a rectangle with a token.
///
/// @param canvas where it is drawn
/// @param rect the rectangle
/// @param ink the colour
void fill(const Canvas& canvas, const Rect& rect, Colour ink) {
    renderer::fill_source_rect(*canvas.surface, canvas.placement, rect, rgb(ink));
}

/// Outlines a rectangle in a token.
///
/// @param canvas where it is drawn
/// @param rect the rectangle
/// @param ink the colour
void outline(const Canvas& canvas, const Rect& rect, Colour ink) {
    renderer::draw_outline(*canvas.surface, canvas.placement, rect, rgb(ink));
}

/// Blends a token over a rectangle.
///
/// @param canvas where it is drawn
/// @param rect the rectangle
/// @param ink the colour
/// @param opacity its share of each pixel, in 256ths
void blend(const Canvas& canvas, const Rect& rect, Colour ink, uint32_t opacity) {
    renderer::blend_source_rect(*canvas.surface, canvas.placement, rect, rgb(ink), opacity);
}

/// Draws a text in a box, when the canvas has a surface and fonts.
///
/// @param canvas where it is drawn
/// @param role the font
/// @param text the text, in UTF-8, as it is drawn
/// @param box the box
/// @param align where it sits along the box
/// @param ink the colour
/// @param tracking extra columns after each glyph but the last
void line(
    const Canvas& canvas,
    FontRole role,
    std::string_view text,
    const Rect& box,
    Align align,
    Colour ink,
    int32_t tracking = 0
) {
    if (!drawable(canvas) || canvas.fonts == nullptr || text.empty())
        return;
    draw_boxed_text(
        *canvas.surface, canvas.placement, *canvas.fonts, role, text, box, align, rgb(ink), tracking
    );
}

/// Draws a text cut with "..." to its box's width.
///
/// @param canvas where it is drawn
/// @param role the font
/// @param text the text, in UTF-8
/// @param box the box; the text is cut to its width
/// @param align where it sits along the box
/// @param ink the colour
void cut_line(
    const Canvas& canvas,
    FontRole role,
    std::string_view text,
    const Rect& box,
    Align align,
    Colour ink
) {
    if (!drawable(canvas) || canvas.fonts == nullptr || text.empty() || box.width <= 0)
        return;
    line(canvas, role, cut_to_width(*canvas.fonts, role, text, box.width), box, align, ink);
}

/// Appends a control.
///
/// @param[in,out] list the display list
/// @param rect where it lies
/// @param id its number
/// @param name its automation name
/// @param kind what it is
/// @param steps Left and Right act on it
/// @param checked a chosen one, for automation
/// @param text its caption or value, for automation
/// @param enabled a press reaches it
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

// ---- Chips ----

/// A chip's face, when it has one, its border and its text.
struct ChipColours {
    std::optional<Colour> face{}; ///< the face; none leaves what is under the chip
    Colour border{};              ///< the outline
    Colour text{};                ///< the words
};

/// Returns a chip's colours.
///
/// @param look the chip
/// @return its face, border and text
ChipColours chip_colours(const ChipLook& look) noexcept {
    if (look.disabled)
        return {std::nullopt, colour::switch_idle, colour::switch_idle};
    ChipColours colours;
    switch (look.state) {
    case ChipState::get:
        colours = {std::nullopt, colour::control_hover, colour::hint};
        break;
    case ChipState::installed:
        colours = {std::nullopt, colour::accent, colour::accent};
        break;
    case ChipState::update:
        colours = {std::nullopt, colour::lock, colour::lock};
        break;
    case ChipState::playing:
        colours = {colour::accent, colour::accent, colour::on_accent};
        break;
    case ChipState::online:
        colours = {std::nullopt, colour::online, colour::online};
        break;
    case ChipState::problem:
        colours = {std::nullopt, colour::danger, colour::danger};
        break;
    case ChipState::filter:
        if (look.selected)
            colours = {colour::list_selected, colour::accent, colour::text};
        else
            colours = {
                std::nullopt,
                look.hovered ? colour::control_hover : colour::control_border,
                colour::hint
            };
        break;
    }
    if (look.hovered && !colours.face.has_value())
        colours.face = colour::hover;
    return colours;
}

/// Returns a chip's width on a canvas: its text, measured when the canvas
/// has fonts, and chip_padding either side.
///
/// @param canvas the canvas
/// @param look the chip
/// @return the width, in points
int32_t chip_width_on(const Canvas& canvas, const ChipLook& look) {
    return measured(canvas, FontRole::small, look.text) + 2 * kMetrics.chip_padding;
}

/// Returns where a row's chip of a width lies: at the right inset, centred down the row.
///
/// @param row the row
/// @param width the chip's width
/// @return the chip's rectangle
Rect chip_in_row(const Rect& row, int32_t width) noexcept {
    return {
        row.x + row.width - kMetrics.list_row_inset - width,
        row.y + (row.height - kMetrics.chip_height) / 2,
        width,
        kMetrics.chip_height,
    };
}

/// Draws chips left to right from a point, chip_gap apart, as many as end
/// at or before a column.
///
/// @param canvas where they are drawn
/// @param chips the chips
/// @param at the first chip's top left
/// @param right the column no chip may pass
void draw_chip_line(
    const Canvas& canvas, const std::vector<ChipLook>& chips, Point at, int32_t right
) {
    int32_t left = at.x;
    for (const ChipLook& chip : chips) {
        const int32_t width = chip_width_on(canvas, chip);
        if (left + width > right)
            break;
        draw_chip(canvas, {left, at.y, width, kMetrics.chip_height}, chip);
        left += width + kMetrics.chip_gap;
    }
}

// ---- List rows ----

/// Draws a list row's badge: the picture, the letters on their colour, or a
/// blank dashed square.
///
/// @param canvas where it is drawn
/// @param badge the badge's square
/// @param look the row
void draw_row_badge(const Canvas& canvas, const Rect& badge, const ListRowLook& look) {
    if (badge.width <= 0 || badge.height <= 0)
        return;
    if (renderer::picture_drawable(look.badge)) {
        renderer::draw_picture(*canvas.surface, canvas.placement, badge, look.badge);
        outline(canvas, badge, colour::control_border);
        return;
    }
    if (!look.badge_text.empty()) {
        fill(canvas, badge, look.badge_colour);
        line(
            clipped(canvas, badge),
            FontRole::small,
            look.badge_text,
            badge,
            Align::centre,
            colour::on_accent
        );
        return;
    }
    const int32_t hairline = kMetrics.hairline;
    const int32_t period = kMetrics.badge_dash + kMetrics.badge_dash_gap;
    for (int32_t along = 0; along < badge.width; along += period) {
        const int32_t dash = std::min(kMetrics.badge_dash, badge.width - along);
        fill(canvas, {badge.x + along, badge.y, dash, hairline}, colour::control_border);
        fill(
            canvas,
            {badge.x + along, badge.y + badge.height - hairline, dash, hairline},
            colour::control_border
        );
    }
    for (int32_t along = 0; along < badge.height; along += period) {
        const int32_t dash = std::min(kMetrics.badge_dash, badge.height - along);
        fill(canvas, {badge.x, badge.y + along, hairline, dash}, colour::control_border);
        fill(
            canvas,
            {badge.x + badge.width - hairline, badge.y + along, hairline, dash},
            colour::control_border
        );
    }
}

// ---- Text and search fields ----

/// Returns the width of a field's text before its caret, in the regular
/// font. A caret past the text counts as at its end, and one inside a
/// character as at that character's start.
///
/// @param fonts the fonts
/// @param field the text and its caret
/// @return the width, in points
int32_t before_caret(const Fonts& fonts, const TextField& field) {
    const std::string_view text = field.text;
    std::size_t caret = std::min(field.caret, text.size());
    while (caret > 0 && caret < text.size() && continuation_byte(text[caret]))
        --caret;
    return text_width(fonts, FontRole::regular, text.substr(0, caret));
}

// ---- Tabs ----

/// Returns a tab's count, 0 when the look gives none.
///
/// @param look the tabs
/// @param index the tab
/// @return the count
int32_t count_of(const TabsLook& look, std::size_t index) noexcept {
    return index < look.counts.size() ? look.counts[index] : 0;
}

/// Returns the width of a tab's count: its digits and tab_count_padding
/// either side, and never narrower than it is high.
///
/// @param fonts the fonts, or null to count the digits as no columns
/// @param count the count
/// @return the width, in points
int32_t count_width(const Fonts* fonts, int32_t count) {
    const int32_t digits =
        fonts == nullptr ? 0 : text_width(*fonts, FontRole::small, std::to_string(count));
    return std::max(kMetrics.tab_count_height, digits + 2 * kMetrics.tab_count_padding);
}

/// Returns a tab caption's width in the small font with tab_tracking.
///
/// @param fonts the fonts, or null for no columns
/// @param caption the caption
/// @return the width, in points
int32_t caption_width(const Fonts* fonts, std::string_view caption) {
    if (fonts == nullptr || caption.empty())
        return 0;
    return tracked_width(*fonts, FontRole::small, caption, kMetrics.tab_tracking);
}

// ---- Cards ----

/// Returns a card's preview: the largest square that leaves room for the
/// card's two lines and three insets, centred across the card, card_inset
/// under its top.
///
/// @param rect the card
/// @return the preview's square
Rect card_preview(const Rect& rect) noexcept {
    const int32_t lines = kMetrics.regular_line + kMetrics.small_line + 3 * kMetrics.card_inset;
    const int32_t side =
        std::max(std::min(rect.width - 2 * kMetrics.card_inset, rect.height - lines), int32_t{0});
    return {rect.x + (rect.width - side) / 2, rect.y + kMetrics.card_inset, side, side};
}

// ---- Hover cards ----

/// Tells whether a hover card has a header line.
///
/// @param look the card
/// @return true when it shows the mark, a title or an aside
bool has_header(const HoverCardLook& look) noexcept {
    return look.mark || !look.title.empty() || !look.aside.empty();
}

/// Returns the column a hover card's values start at, from its text's left:
/// the widest label and hover_card_padding, or 0 when no row has a label.
///
/// @param fonts the fonts, or null to measure nothing
/// @param look the card
/// @return the column, in points
int32_t label_column(const Fonts* fonts, const HoverCardLook& look) {
    bool labelled = false;
    int32_t widest = 0;
    for (const HoverCardRow& row : look.rows) {
        if (row.label.empty())
            continue;
        labelled = true;
        if (fonts != nullptr)
            widest = std::max(widest, text_width(*fonts, FontRole::small, row.label));
    }
    return labelled ? widest + kMetrics.hover_card_padding : 0;
}

/// Draws a hover card's arrow: a triangle reaching hover_card_arrow out of
/// one edge, its point arrow_at along that edge, drawn as nested bars.
///
/// @param canvas where it is drawn
/// @param rect the card
/// @param look the card's arrow
void draw_arrow(const Canvas& canvas, const Rect& rect, const HoverCardLook& look) {
    const int32_t depth = kMetrics.hover_card_arrow;
    for (int32_t half = 0; half < depth; ++half) {
        const int32_t reach = depth - half;
        const int32_t across = 2 * half + 1;
        Rect bar{};
        switch (look.arrow) {
        case Side::none:
            return;
        case Side::top:
            bar = {rect.x + look.arrow_at - half, rect.y - reach, across, reach};
            break;
        case Side::bottom:
            bar = {rect.x + look.arrow_at - half, rect.y + rect.height, across, reach};
            break;
        case Side::left:
            bar = {rect.x - reach, rect.y + look.arrow_at - half, reach, across};
            break;
        case Side::right:
            bar = {rect.x + rect.width, rect.y + look.arrow_at - half, reach, across};
            break;
        }
        fill(canvas, bar, colour::lock);
    }
}

} // namespace

std::string cut_to_width(const Fonts& fonts, FontRole role, std::string_view text, int32_t width) {
    if (text_width(fonts, role, text) <= width)
        return std::string(text);
    std::string best(kEllipsis);
    for (std::size_t end = 0; end < text.size();) {
        end += character_bytes(text.substr(end));
        std::string shown(text.substr(0, end));
        shown += kEllipsis;
        if (text_width(fonts, role, shown) > width)
            break;
        best = std::move(shown);
    }
    return best;
}

// ---- List rows ----

Rect list_row_chip_rect(const Fonts& fonts, const Rect& row, const ChipLook& chip) {
    return chip_in_row(row, chip_width(fonts, chip));
}

void draw_list_row(const Canvas& canvas, const Rect& rect, const ListRowLook& look) {
    if (!drawable(canvas))
        return;
    if (look.hovered)
        fill(canvas, rect, colour::hover);
    if (look.selected) {
        blend(canvas, rect, colour::accent, selected_tint);
        outline(canvas, rect, colour::accent);
    }

    const int32_t inset = kMetrics.list_row_inset;
    const int32_t side =
        std::max(std::min(rect.height - 2 * inset, kMetrics.list_row_badge), int32_t{0});
    const Rect badge{rect.x + inset, rect.y + (rect.height - side) / 2, side, side};
    draw_row_badge(canvas, badge, look);

    int32_t right = rect.x + rect.width - inset;
    if (look.chip.has_value()) {
        const Rect chip = chip_in_row(rect, chip_width_on(canvas, *look.chip));
        draw_chip(canvas, chip, *look.chip);
        right = chip.x - kMetrics.list_row_gap;
    }

    // The text: the title's line, then each line, then the tags' line,
    // centred down the row. A short row's text fills it from its top.
    const int32_t text_left = badge.x + side + inset;
    const auto line_count = static_cast<int32_t>(look.lines.size()) + (look.tags.empty() ? 0 : 1);
    const int32_t text_height = std::max(
                                    kMetrics.list_row_title_top + kMetrics.regular_line,
                                    kMetrics.list_row_line_top + line_count * kMetrics.small_line
                                ) +
                                kMetrics.list_row_title_top;
    const int32_t top = rect.y + std::max((rect.height - text_height) / 2, int32_t{0});
    const int32_t title_row = top + kMetrics.list_row_title_top;
    // The aside's lines are the small font's, the first centred on the title's line.
    const int32_t aside_row = title_row + (kMetrics.regular_line - kMetrics.small_line) / 2;

    int32_t text_right = right;
    if (!look.aside.empty()) {
        int32_t aside_width = 0;
        for (const std::string& entry : look.aside)
            aside_width = std::max(aside_width, measured(canvas, FontRole::small, entry));
        aside_width = std::min(aside_width, std::max(right - text_left, int32_t{0}));
        const int32_t aside_left = right - aside_width;
        for (std::size_t index = 0; index < look.aside.size(); ++index) {
            const Rect box{
                aside_left,
                aside_row + static_cast<int32_t>(index) * kMetrics.small_line,
                aside_width,
                kMetrics.small_line,
            };
            cut_line(canvas, FontRole::small, look.aside[index], box, Align::right, colour::hint);
        }
        text_right = aside_left - kMetrics.list_row_gap;
    }

    const int32_t room = std::max(text_right - text_left, int32_t{0});
    const Rect title_box{text_left, title_row, room, kMetrics.regular_line};
    if (canvas.fonts != nullptr && !look.title.empty()) {
        const std::string title = cut_to_width(*canvas.fonts, FontRole::regular, look.title, room);
        line(canvas, FontRole::regular, title, title_box, Align::left, colour::text);
        const int32_t by_left =
            text_left + text_width(*canvas.fonts, FontRole::regular, title) + kMetrics.list_row_gap;
        if (!look.by.empty() && by_left < text_right) {
            const Rect by_box{by_left, title_row, text_right - by_left, kMetrics.regular_line};
            cut_line(canvas, FontRole::small, look.by, by_box, Align::left, colour::hint);
        }
    } else if (canvas.fonts != nullptr && !look.by.empty()) {
        cut_line(canvas, FontRole::small, look.by, title_box, Align::left, colour::hint);
    }

    int32_t line_row = top + kMetrics.list_row_line_top;
    for (const std::string& text : look.lines) {
        cut_line(
            canvas,
            FontRole::small,
            text,
            {text_left, line_row, room, kMetrics.small_line},
            Align::left,
            colour::list_text
        );
        line_row += kMetrics.small_line;
    }
    draw_chip_line(canvas, look.tags, {text_left, line_row}, text_right);
}

void add_list_row(
    DisplayList& list, const Rect& rect, const ListRowLook& look, ControlId id, std::string name
) {
    Item item;
    item.role = Role::list_row;
    item.rect = rect;
    item.control = id;
    item.text = look.title;
    item.state.hovered = look.hovered;
    item.state.selected = look.selected;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list,
        rect,
        id,
        std::move(name),
        ControlKind::list_item,
        false,
        look.selected,
        look.title,
        true
    );
}

// ---- Chips ----

int32_t chip_width(const Fonts& fonts, const ChipLook& look) {
    return text_width(fonts, FontRole::small, look.text) + 2 * kMetrics.chip_padding;
}

void draw_chip(const Canvas& canvas, const Rect& rect, const ChipLook& look) {
    if (!drawable(canvas))
        return;
    const ChipColours colours = chip_colours(look);
    if (colours.face.has_value())
        fill(canvas, rect, *colours.face);
    outline(canvas, rect, colours.border);
    line(clipped(canvas, rect), FontRole::small, look.text, rect, Align::centre, colours.text);
}

void add_chip(
    DisplayList& list, const Rect& rect, const ChipLook& look, ControlId id, std::string name
) {
    Item item;
    item.role = Role::chip;
    item.rect = rect;
    item.control = id;
    item.text = look.text;
    item.state.hovered = look.hovered;
    item.state.selected = look.selected;
    item.state.disabled = look.disabled;
    item.look = look;
    list.items.push_back(std::move(item));
    if (id == no_control)
        return;
    add_control(
        list,
        rect,
        id,
        std::move(name),
        ControlKind::button,
        false,
        look.selected,
        look.text,
        !look.disabled
    );
}

// ---- Text and search fields ----

Rect field_text_rect(const Rect& rect, const SearchLook& look) noexcept {
    int32_t left = rect.x + kMetrics.field_inset;
    if (look.magnifier)
        left += kMetrics.magnifier_side + kMetrics.field_inset;
    const int32_t right = rect.x + rect.width - kMetrics.field_inset;
    return {left, rect.y, std::max(right - left, int32_t{0}), rect.height};
}

int32_t field_scroll(const Fonts& fonts, const Rect& rect, const SearchLook& look) {
    const int32_t room = field_text_rect(rect, look).width - kMetrics.caret_width;
    return std::max(before_caret(fonts, look.field) - room, int32_t{0});
}

void draw_search(const Canvas& canvas, const Rect& rect, const SearchLook& look) {
    if (!drawable(canvas))
        return;
    fill(canvas, rect, colour::well);
    Colour border = colour::control_border;
    if (look.focused)
        border = colour::accent;
    else if (look.hovered)
        border = colour::control_hover;
    outline(canvas, rect, border);
    if (look.magnifier)
        draw_mark(
            canvas,
            Mark::magnifier,
            {rect.x + kMetrics.field_inset, rect.y + (rect.height - kMetrics.magnifier_side) / 2},
            colour::hint
        );

    const Rect box = field_text_rect(rect, look);
    const Canvas inside = clipped(canvas, box);
    if (look.field.text.empty()) {
        cut_line(inside, FontRole::regular, look.placeholder, box, Align::left, colour::hint);
    } else if (canvas.fonts != nullptr) {
        const int32_t scroll = field_scroll(*canvas.fonts, rect, look);
        line(
            inside,
            FontRole::regular,
            look.field.text,
            {box.x - scroll, box.y, box.width + scroll, box.height},
            Align::left,
            colour::text
        );
    }
    if (!look.focused || canvas.fonts == nullptr)
        return;
    const int32_t before = before_caret(*canvas.fonts, look.field);
    const int32_t scroll = field_scroll(*canvas.fonts, rect, look);
    const int32_t rows = std::min(capitals(*canvas.fonts, FontRole::regular), box.height);
    fill(
        inside,
        {box.x + before - scroll, box.y + (box.height - rows) / 2, kMetrics.caret_width, rows},
        colour::text
    );
}

void add_search(
    DisplayList& list, const Rect& rect, const SearchLook& look, ControlId id, std::string name
) {
    Item item;
    item.role = Role::field;
    item.rect = rect;
    item.control = id;
    item.text = look.field.text;
    item.state.hovered = look.hovered;
    item.state.focused = look.focused;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list, rect, id, std::move(name), ControlKind::text_field, true, false, look.field.text, true
    );
}

// ---- Tabs ----

std::vector<Rect> tab_rects(const Fonts& fonts, const Rect& strip, const TabsLook& look) {
    std::vector<Rect> rects;
    rects.reserve(look.captions.size());
    int32_t left = strip.x;
    for (std::size_t index = 0; index < look.captions.size(); ++index) {
        int32_t width = 2 * kMetrics.tab_padding + caption_width(&fonts, look.captions[index]);
        const int32_t count = count_of(look, index);
        if (count > 0)
            width += kMetrics.tab_count_gap + count_width(&fonts, count);
        rects.push_back({left, strip.y, width, strip.height});
        left += width;
    }
    return rects;
}

void draw_tabs(const Canvas& canvas, const Rect& strip, const TabsLook& look) {
    if (!drawable(canvas))
        return;
    draw_rule(canvas, strip.x, strip.y + strip.height - kMetrics.hairline, strip.width);
    if (canvas.fonts == nullptr)
        return;
    const std::vector<Rect> rects = tab_rects(*canvas.fonts, strip, look);
    for (std::size_t index = 0; index < rects.size(); ++index) {
        const Rect& tab = rects[index];
        const bool selected = index == look.selected;
        const bool hovered = look.hovered.has_value() && *look.hovered == index;
        const std::string& caption = look.captions[index];
        const int32_t width = caption_width(canvas.fonts, caption);
        const int32_t above_rule = tab.height - kMetrics.tab_rule;
        line(
            canvas,
            FontRole::small,
            caption,
            {tab.x + kMetrics.tab_padding, tab.y, width, above_rule},
            Align::left,
            selected || hovered ? colour::text : colour::hint,
            kMetrics.tab_tracking
        );
        const int32_t count = count_of(look, index);
        if (count > 0) {
            const Rect pill{
                tab.x + kMetrics.tab_padding + width + kMetrics.tab_count_gap,
                tab.y + (above_rule - kMetrics.tab_count_height) / 2,
                count_width(canvas.fonts, count),
                kMetrics.tab_count_height,
            };
            fill(canvas, pill, colour::lock);
            line(
                canvas,
                FontRole::small,
                std::to_string(count),
                pill,
                Align::centre,
                colour::on_accent
            );
        }
        if (selected)
            fill(canvas, {tab.x, tab.y + above_rule, tab.width, kMetrics.tab_rule}, colour::accent);
    }
}

void add_tabs(
    DisplayList& list,
    const Fonts& fonts,
    const Rect& strip,
    const TabsLook& look,
    std::span<const ControlId> ids,
    std::span<const std::string> names
) {
    Item item;
    item.role = Role::tabs;
    item.rect = strip;
    if (look.selected < look.captions.size())
        item.text = look.captions[look.selected];
    item.look = look;
    list.items.push_back(std::move(item));

    const std::vector<Rect> rects = tab_rects(fonts, strip, look);
    for (std::size_t index = 0; index < rects.size(); ++index) {
        std::string text = look.captions[index];
        const int32_t count = count_of(look, index);
        if (count > 0)
            text += " " + std::to_string(count);
        add_control(
            list,
            rects[index],
            index < ids.size() ? ids[index] : no_control,
            index < names.size() ? names[index] : std::string{},
            ControlKind::tab,
            false,
            index == look.selected,
            std::move(text),
            true
        );
    }
}

// ---- Cards ----

int32_t card_height(int32_t width) noexcept {
    const int32_t side = std::max(width - 2 * kMetrics.card_inset, int32_t{0});
    return side + kMetrics.regular_line + kMetrics.small_line + 3 * kMetrics.card_inset;
}

std::vector<Rect> card_cells(const Rect& area, std::size_t count, std::size_t columns) {
    std::vector<Rect> cells;
    if (count == 0)
        return cells;
    const int64_t gap = kMetrics.card_gap;
    // No more columns than the area has points, so a column is never empty.
    const int64_t most = std::max<int64_t>(area.width, 1);
    int64_t wanted =
        columns > static_cast<std::size_t>(most) ? most : static_cast<int64_t>(columns);
    if (wanted == 0)
        wanted = std::max<int64_t>((area.width + gap) / (kMetrics.card_least_width + gap), 1);
    // The least width at which exactly the wanted columns fit the area.
    const auto least = static_cast<int32_t>((area.width + gap) / wanted - gap);
    const std::vector<Rect> first_row =
        grid(area, least, 0, kMetrics.card_gap, static_cast<std::size_t>(wanted));
    if (first_row.empty())
        return cells;
    // The left columns are the widest, so the first cell's height spaces the rows.
    const int32_t row_step = card_height(first_row.front().width) + kMetrics.card_gap;
    cells.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        Rect cell = first_row[index % first_row.size()];
        cell.y = area.y + static_cast<int32_t>(index / first_row.size()) * row_step;
        cell.height = card_height(cell.width);
        cells.push_back(cell);
    }
    return cells;
}

void draw_card(const Canvas& canvas, const Rect& rect, const CardLook& look) {
    if (!drawable(canvas))
        return;
    fill(canvas, rect, colour::list_selected);
    Colour border = colour::control_border;
    if (look.selected)
        border = colour::accent;
    else if (look.hovered)
        border = colour::control_hover;
    outline(canvas, rect, border);

    const Rect preview = card_preview(rect);
    if (renderer::picture_drawable(look.picture)) {
        renderer::draw_picture(*canvas.surface, canvas.placement, preview, look.picture);
    } else if (preview.width > 0) {
        fill(canvas, preview, colour::well);
        cut_line(
            clipped(canvas, preview),
            FontRole::small,
            look.placeholder,
            preview,
            Align::centre,
            colour::hint
        );
    }
    draw_chip_line(
        canvas,
        look.chips,
        {preview.x + kMetrics.card_inset,
         preview.y + preview.height - kMetrics.card_inset - kMetrics.chip_height},
        preview.x + preview.width - kMetrics.card_inset
    );

    const int32_t text_width_room = std::max(rect.width - 2 * kMetrics.card_inset, int32_t{0});
    const Rect title{
        rect.x + kMetrics.card_inset,
        preview.y + preview.height + kMetrics.card_inset,
        text_width_room,
        kMetrics.regular_line,
    };
    cut_line(canvas, FontRole::regular, look.title, title, Align::left, colour::text);
    const Rect subtitle{title.x, title.y + title.height, text_width_room, kMetrics.small_line};
    cut_line(canvas, FontRole::small, look.subtitle, subtitle, Align::left, colour::hint);

    if (look.greyed)
        blend(canvas, rect, colour::panel, locked_fade);
}

void add_card(
    DisplayList& list, const Rect& rect, const CardLook& look, ControlId id, std::string name
) {
    Item item;
    item.role = Role::card;
    item.rect = rect;
    item.control = id;
    item.text = look.title;
    item.state.hovered = look.hovered;
    item.state.selected = look.selected;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list,
        rect,
        id,
        std::move(name),
        ControlKind::list_item,
        false,
        look.selected,
        look.title,
        true
    );
}

// ---- Hover cards ----

Point hover_card_size(const Fonts& fonts, const HoverCardLook& look) {
    const int32_t padding = kMetrics.hover_card_padding;
    int32_t widest = 0;
    int32_t lines = 0;
    if (has_header(look)) {
        int32_t header = 0;
        const auto join = [&](int32_t part) { header += (header > 0 ? padding : 0) + part; };
        if (look.mark)
            join(mark_size(Mark::oa_small).x);
        if (!look.title.empty())
            join(text_width(fonts, FontRole::small, look.title));
        if (!look.aside.empty())
            join(text_width(fonts, FontRole::small, look.aside));
        widest = header;
        ++lines;
    }
    const int32_t values = label_column(&fonts, look);
    for (const HoverCardRow& row : look.rows) {
        const int32_t from = row.label.empty() ? 0 : values;
        widest = std::max(widest, from + text_width(fonts, FontRole::small, row.value));
        ++lines;
    }
    for (const std::string& text : look.block) {
        widest = std::max(widest, text_width(fonts, FontRole::small, text));
        ++lines;
    }
    const int32_t width = look.width > 0 ? look.width : widest + 2 * padding;
    return {width, lines * kMetrics.small_line + 2 * padding};
}

Rect hover_card_rect(const Rect& anchor, const Rect& frame, Point size) noexcept {
    const int32_t frame_right = frame.x + frame.width;
    const int32_t frame_bottom = frame.y + frame.height;
    const int32_t x = std::max(std::min(anchor.x, frame_right - size.x), frame.x);
    const int32_t below = anchor.y + anchor.height + kMetrics.hover_card_arrow;
    const int32_t above = anchor.y - kMetrics.hover_card_arrow - size.y;
    int32_t y = below;
    if (below + size.y > frame_bottom)
        y = above >= frame.y ? above : frame_bottom - size.y;
    y = std::max(std::min(y, frame_bottom - size.y), frame.y);
    return {x, y, size.x, size.y};
}

void draw_hover_card(const Canvas& canvas, const Rect& rect, const HoverCardLook& look) {
    if (!drawable(canvas))
        return;
    fill(canvas, rect, colour::band);
    outline(canvas, rect, colour::lock);
    draw_arrow(canvas, rect, look);

    const int32_t padding = kMetrics.hover_card_padding;
    const int32_t row_height = kMetrics.small_line;
    const int32_t left = rect.x + padding;
    const int32_t right = rect.x + rect.width - padding;
    const int32_t room = std::max(right - left, int32_t{0});
    int32_t row = rect.y + padding;

    if (has_header(look)) {
        int32_t pen = left;
        if (look.mark) {
            const Point mark = mark_size(Mark::oa_small);
            draw_mark(
                canvas, Mark::oa_small, {pen, row + (row_height - mark.y) / 2}, colour::accent
            );
            pen += mark.x + padding;
        }
        int32_t aside_width = 0;
        if (!look.aside.empty()) {
            aside_width = std::min(
                measured(canvas, FontRole::small, look.aside), std::max(right - pen, int32_t{0})
            );
            cut_line(
                canvas,
                FontRole::small,
                look.aside,
                {right - aside_width, row, aside_width, row_height},
                Align::right,
                colour::hint
            );
        }
        const int32_t title_right = aside_width > 0 ? right - aside_width - padding : right;
        cut_line(
            canvas,
            FontRole::small,
            look.title,
            {pen, row, std::max(title_right - pen, int32_t{0}), row_height},
            Align::left,
            colour::text
        );
        row += row_height;
    }

    const int32_t values = label_column(canvas.fonts, look);
    for (const HoverCardRow& entry : look.rows) {
        int32_t value_left = left;
        if (!entry.label.empty()) {
            cut_line(
                canvas,
                FontRole::small,
                entry.label,
                {left, row, std::max(values - padding, int32_t{0}), row_height},
                Align::left,
                colour::hint
            );
            value_left = left + values;
        }
        cut_line(
            canvas,
            FontRole::small,
            entry.value,
            {value_left, row, std::max(right - value_left, int32_t{0}), row_height},
            Align::left,
            entry.value_colour
        );
        row += row_height;
    }

    for (const std::string& text : look.block) {
        cut_line(
            canvas, FontRole::small, text, {left, row, room, row_height}, Align::left, colour::quiet
        );
        row += row_height;
    }
}

void add_hover_card(DisplayList& list, const Rect& rect, const HoverCardLook& look) {
    Item item;
    item.role = Role::hover_card;
    item.rect = rect;
    item.text = look.title;
    item.look = look;
    list.items.push_back(std::move(item));
}

bool hover_card_due(HoverTimer& timer, ControlId hovered, uint64_t now_ms) noexcept {
    if (hovered == no_control) {
        timer = {};
        return false;
    }
    if (hovered != timer.over) {
        timer.over = hovered;
        timer.since_ms = now_ms;
        return false;
    }
    return now_ms >= timer.since_ms && now_ms - timer.since_ms >= hover_card_delay_ms;
}

// ---- Links ----

int32_t link_width(const Fonts& fonts, const LinkLook& look) {
    return text_width(fonts, FontRole::small, look.caption);
}

void draw_link(const Canvas& canvas, const Rect& rect, const LinkLook& look) {
    if (!drawable(canvas))
        return;
    Colour ink = colour::accent;
    if (!look.enabled)
        ink = colour::switch_idle;
    else if (look.hovered)
        ink = colour::accent_light;
    line(canvas, FontRole::small, look.caption, rect, Align::left, ink);
}

void add_link(
    DisplayList& list, const Rect& rect, const LinkLook& look, ControlId id, std::string name
) {
    Item item;
    item.role = Role::link;
    item.rect = rect;
    item.control = id;
    item.text = look.caption;
    item.state.hovered = look.hovered;
    item.state.disabled = !look.enabled;
    item.look = look;
    list.items.push_back(std::move(item));
    add_control(
        list, rect, id, std::move(name), ControlKind::link, false, false, look.caption, look.enabled
    );
}

} // namespace oa::ui::kit
