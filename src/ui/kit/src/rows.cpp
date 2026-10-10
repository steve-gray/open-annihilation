// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Declared rows: the placement of a column of rows, as the settings dialog
// places a section's, their scroll, and what they draw and where their
// controls are. A row's kind is its view's, read from its spec.

#include "oa/ui/kit/rows.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::kit {

namespace {

constexpr const Metrics& kMetrics = compact_metrics;

/// Tells whether a rectangle has an area.
///
/// @param rect the rectangle
/// @return true when it is wider and taller than nothing
[[nodiscard]] bool has_area(const Rect& rect) noexcept {
    return rect.width > 0 && rect.height > 0;
}

/// Returns the width of the control a row keeps at its label line's right.
///
/// @param view the row
/// @return the control's width, in points; 0 for a row whose control, if it
///     has one, stands elsewhere
[[nodiscard]] int32_t label_line_control_width(const RowView& view) noexcept {
    switch (view.kind) {
    case RowKind::toggle:
        return kMetrics.switch_width;
    case RowKind::levels:
        return view.count * view.control_width + 2;
    case RowKind::value_and_button:
        return view.control_width > 0 ? view.control_width : kMetrics.button_row_width;
    case RowKind::buttons: {
        int32_t width = 0;
        const std::size_t count =
            std::min(view.buttons.size(), kMetrics.folder_button_widths.size());
        for (std::size_t at = 0; at < count; ++at)
            width += kMetrics.folder_button_widths[at] + (at > 0 ? kMetrics.folder_button_gap : 0);
        return width;
    }
    case RowKind::choice:
    case RowKind::slider:
    case RowKind::text_field:
    case RowKind::link:
    case RowKind::text:
        break;
    }
    return 0;
}

/// Returns the control kind a row's control is listed as.
///
/// @param kind the row's kind
/// @return the control's kind; a value_and_button row's is a button
[[nodiscard]] ControlKind control_kind(RowKind kind) noexcept {
    switch (kind) {
    case RowKind::toggle:
        return ControlKind::toggle;
    case RowKind::choice:
        return ControlKind::choice;
    case RowKind::slider:
        return ControlKind::slider;
    case RowKind::levels:
        return ControlKind::levels;
    case RowKind::value_and_button:
        return ControlKind::button;
    case RowKind::buttons:
        return ControlKind::buttons;
    case RowKind::text_field:
        return ControlKind::text_field;
    case RowKind::link:
        return ControlKind::link;
    case RowKind::text:
        break;
    }
    return ControlKind::area;
}

/// Tells whether Left and Right act on a row's control.
///
/// @param kind the row's kind
/// @return true for every control but a button and a link
[[nodiscard]] bool takes_steps(RowKind kind) noexcept {
    switch (kind) {
    case RowKind::toggle:
    case RowKind::choice:
    case RowKind::slider:
    case RowKind::levels:
    case RowKind::buttons:
    case RowKind::text_field:
        return true;
    case RowKind::value_and_button:
    case RowKind::link:
    case RowKind::text:
        break;
    }
    return false;
}

/// Tells whether a row's hint lines are text from its host, kept to their
/// own columns, as the Game files rows' are.
///
/// @param kind the row's kind
/// @return true for a value_and_button and a text row
[[nodiscard]] bool host_text(RowKind kind) noexcept {
    switch (kind) {
    case RowKind::value_and_button:
    case RowKind::text:
        return true;
    case RowKind::toggle:
    case RowKind::choice:
    case RowKind::slider:
    case RowKind::levels:
    case RowKind::buttons:
    case RowKind::text_field:
    case RowKind::link:
        break;
    }
    return false;
}

/// Returns a row's control's name.
///
/// @param state the names' prefix
/// @param view the row
/// @return <prefix>.<id>, a value_and_button's with its button's word; the id alone without a prefix
[[nodiscard]] std::string control_name(const RowsState& state, const RowView& view) {
    std::string name = state.name_prefix;
    if (!name.empty())
        name += '.';
    name += view.id;
    if (view.kind == RowKind::value_and_button && !view.button_ids.empty()) {
        name += '.';
        name += view.button_ids.front();
    }
    return name;
}

/// Returns what automation reads as a row's control's text.
///
/// @param view the row
/// @return a switch's caption, a strip's chosen level, a slider's, a
///     drop-down's or a value_and_button's value, a text field's text, a link's
///     caption; empty for a row of buttons
[[nodiscard]] std::string control_text(const RowView& view) {
    switch (view.kind) {
    case RowKind::toggle:
        return view.captions.size() == 2 ? view.captions[view.on ? 1 : 0] : std::string();
    case RowKind::levels:
        return view.index >= 0 && static_cast<std::size_t>(view.index) < view.captions.size()
                   ? view.captions[static_cast<std::size_t>(view.index)]
                   : std::string();
    case RowKind::choice:
    case RowKind::slider:
        return view.value;
    case RowKind::value_and_button:
    case RowKind::link:
        return view.buttons.empty() ? std::string() : view.buttons.front();
    case RowKind::text_field:
        return view.text;
    case RowKind::buttons:
    case RowKind::text:
        break;
    }
    return {};
}

/// Appends one item, in the rows' clip.
///
/// @param[in,out] list the display list
/// @param role what it draws
/// @param rect where
/// @param control the control it draws, or none
/// @param text its words
/// @param state how its control looks
/// @param look its component's look
/// @param clip the rows' clip
void push_item(
    DisplayList& list,
    Role role,
    const Rect& rect,
    ControlId control,
    std::string text,
    State state,
    Look look,
    const Rect& clip
) {
    Item item;
    item.role = role;
    item.rect = rect;
    item.clip = clip;
    item.text = std::move(text);
    item.control = control;
    item.state = state;
    item.look = std::move(look);
    list.items.push_back(std::move(item));
}

/// Appends a value's text, in the regular font.
///
/// @param[in,out] list the display list
/// @param rect its box
/// @param text the value, as shown
/// @param align where it sits in its box
/// @param ink its colour
/// @param clip the rows' clip
void push_value(
    DisplayList& list,
    const Rect& rect,
    const std::string& text,
    Align align,
    Colour ink,
    const Rect& clip
) {
    if (text.empty() || !has_area(rect))
        return;
    Item item;
    item.role = Role::text;
    item.rect = rect;
    item.clip = clip;
    item.text = text;
    item.font = FontRole::regular;
    item.align = align;
    item.colour = ink;
    list.items.push_back(std::move(item));
}

/// Appends a row's rule, label and hint lines.
///
/// @param[in,out] list the display list
/// @param placed the rows
/// @param row the row
/// @param clip the rows' clip
void push_frame(
    DisplayList& list, const PlacedRows& placed, const PlacedRow& row, const Rect& clip
) {
    RowFrame frame;
    frame.top = row.top;
    frame.left = placed.left;
    frame.width = placed.right - placed.left;
    frame.label = row.label;
    frame.label_text = row.view.label;
    const bool own_columns = host_text(row.view.kind);
    for (std::size_t line = 0; line < row.hints.size(); ++line) {
        const Rect& box = row.hints[line];
        frame.hints.push_back(box);
        frame.hint_texts.push_back(
            line < row.view.hints.size() ? row.view.hints[line] : std::string()
        );
        frame.hint_notices.push_back(line < row.view.notices.size() && row.view.notices[line]);
        if (!own_columns)
            frame.hint_clips.emplace_back();
        else if (has_area(clip))
            frame.hint_clips.push_back({box.x, clip.y, box.width, clip.height});
        else
            frame.hint_clips.push_back({box.x, row.top, box.width, row.height});
    }
    push_item(
        list,
        Role::row_frame,
        {placed.left, row.top, frame.width, row.height},
        no_control,
        row.view.label,
        {},
        std::move(frame),
        clip
    );
}

/// Appends a row's control as it looks, and a slider's or a row's value.
///
/// @param[in,out] list the display list
/// @param row the row
/// @param control its control, or none while it is locked
/// @param state the pointer and the focus
void push_control(
    DisplayList& list, const PlacedRow& row, ControlId control, const RowsState& state
) {
    const RowView& view = row.view;
    const Rect& area = row.control_area;
    const bool locked = !view.lock.empty();
    const bool live = control != no_control && view.enabled;
    const bool hovered = live && (state.hovered == control || state.pressed == control);
    const bool held = live && state.pressed == control && state.hovered == control;
    const Rect& clip = state.clip;
    State look_state;
    look_state.hovered = hovered;
    look_state.locked = locked;
    look_state.disabled = !view.enabled;
    switch (view.kind) {
    case RowKind::toggle: {
        if (!has_area(area))
            break;
        SwitchLook look;
        look.on = view.on;
        look.hovered = hovered;
        look.locked = locked;
        if (view.captions.size() == 2) {
            look.off_caption = view.captions[0];
            look.on_caption = view.captions[1];
        }
        look_state.on = view.on;
        push_item(list, Role::toggle, area, control, control_text(view), look_state, look, clip);
        break;
    }
    case RowKind::levels: {
        if (!has_area(area))
            break;
        LevelsLook look;
        look.captions = view.captions;
        look.level_width = view.control_width;
        look.chosen = static_cast<std::size_t>(std::max(view.index, int32_t{0}));
        look.offered = static_cast<std::size_t>(std::max(view.offered, int32_t{0}));
        look.hovered = hovered;
        look.locked = locked;
        push_item(list, Role::levels, area, control, control_text(view), look_state, look, clip);
        break;
    }
    case RowKind::slider: {
        if (has_area(area)) {
            const SliderLook look{view.index, view.count, locked, hovered};
            push_item(list, Role::slider, area, control, view.value, look_state, look, clip);
        }
        push_value(list, row.value, view.value, Align::right, colour::text, clip);
        break;
    }
    case RowKind::choice: {
        if (!has_area(area))
            break;
        const ChoiceLook look{
            view.value, hovered, control != no_control && state.open_menu == control
        };
        push_item(list, Role::choice, area, control, view.value, look_state, look, clip);
        break;
    }
    case RowKind::value_and_button: {
        push_value(
            list,
            row.value,
            view.value,
            view.label.empty() ? Align::left : Align::right,
            colour::hint,
            clip
        );
        if (!has_area(area) || view.buttons.empty())
            break;
        ButtonLook look;
        look.caption = view.buttons.front();
        look.style = view.enabled ? ButtonStyle::accent : ButtonStyle::plain;
        look.hovered = hovered;
        look.held = held;
        look.enabled = view.enabled;
        look_state.pressed = held;
        push_item(list, Role::button, area, control, look.caption, look_state, look, clip);
        break;
    }
    case RowKind::buttons: {
        if (!has_area(area))
            break;
        for (std::size_t at = 0; at < view.buttons.size(); ++at) {
            const bool button_hovered =
                live && state.hovered == control && state.hovered_button == at;
            const bool button_held = button_hovered && state.pressed == control;
            ButtonLook look;
            look.caption = view.buttons[at];
            look.style = view.enabled ? ButtonStyle::quiet : ButtonStyle::plain;
            look.hovered = button_hovered;
            look.held = button_held;
            look.enabled = view.enabled;
            State button_state = look_state;
            button_state.hovered = button_hovered;
            button_state.pressed = button_held;
            push_item(
                list,
                Role::button,
                row_button(area, at),
                control,
                look.caption,
                button_state,
                look,
                clip
            );
        }
        break;
    }
    case RowKind::text_field: {
        if (!has_area(area))
            break;
        const bool focused = control != no_control && state.focused == control;
        SearchLook look;
        look.field.text = view.text;
        look.field.caret = focused ? std::min(state.caret, view.text.size()) : view.text.size();
        look.focused = focused;
        look.hovered = hovered;
        look.magnifier = false;
        look_state.focused = focused;
        push_item(list, Role::field, area, control, view.text, look_state, look, clip);
        break;
    }
    case RowKind::link: {
        if (!has_area(area) || view.buttons.empty())
            break;
        const LinkLook look{view.buttons.front(), hovered, view.enabled};
        push_item(list, Role::link, area, control, look.caption, look_state, look, clip);
        break;
    }
    case RowKind::text:
        push_value(
            list,
            row.value,
            view.value,
            view.label.empty() ? Align::left : Align::right,
            colour::hint,
            clip
        );
        break;
    }
}

} // namespace

std::string shown_copy(const ShownText& shown, std::string_view english) {
    return std::string(shown ? shown(english) : english);
}

void row_id_is_not_one_word() noexcept {
}

PlacedRows place_rows(std::span<const RowView> views, const RowPlacement& placement) {
    PlacedRows placed;
    placed.left = placement.left;
    placed.right = placement.right;
    const int32_t width = placement.right - placement.left;
    const int32_t line_height = kMetrics.regular_line;
    int32_t top = placement.first_top;
    placed.rows.reserve(views.size());
    for (std::size_t at = 0; at < views.size(); ++at) {
        PlacedRow& row = placed.rows.emplace_back();
        row.view = views[at];
        const RowView& view = row.view;
        row.control = placement.first_control + static_cast<int32_t>(at);
        row.top = top;
        const int32_t label_top = top + 1 + placement.row_padding;
        const bool locked = !view.lock.empty();
        // The lock, right-aligned on the label line; the label ends short of it.
        const Rect right_lock{
            placement.right - kMetrics.lock_width, label_top, kMetrics.lock_width, line_height
        };
        int32_t label_right = placement.right;
        const int32_t control_width = label_line_control_width(view);
        if (control_width == 0 || (locked && view.hint_is_status)) {
            // A control of a line of its own leaves the lock the label line's
            // right; a row whose hint is its status shows its lock where its
            // control would be.
            if (locked) {
                row.lock_area = right_lock;
                label_right = row.lock_area.x - kMetrics.label_gap;
            }
        } else {
            row.control_area = {
                placement.right - control_width, label_top, control_width, line_height
            };
            label_right = row.control_area.x - kMetrics.label_gap;
            // Any other locked control keeps its place, so that its value
            // shows, with its lock left of it.
            if (locked) {
                row.lock_area = {
                    row.control_area.x - kMetrics.label_gap - kMetrics.lock_width,
                    label_top,
                    kMetrics.lock_width,
                    line_height
                };
                label_right = row.lock_area.x - kMetrics.label_gap;
            }
        }
        if (view.kind == RowKind::link) {
            // A link stands alone where the label would, short of its lock.
            const int32_t room = label_right - placement.left;
            const int32_t link_columns =
                view.control_width > 0 ? std::min(view.control_width, room) : room;
            row.control_area = {placement.left, label_top, link_columns, line_height};
        } else {
            row.label = {placement.left, label_top, label_right - placement.left, line_height};
        }
        int32_t bottom = label_top + line_height + kMetrics.hint_gap;
        for (std::size_t line = 0; line < view.hints.size(); ++line) {
            if (line > 0 && placement.tall)
                bottom += kMetrics.tall_hint_line_gap;
            row.hints.push_back({placement.left, bottom, width, kMetrics.small_line});
            bottom += kMetrics.small_line;
        }
        switch (view.kind) {
        case RowKind::slider: {
            bottom += kMetrics.slider_gap;
            const int32_t value_left = placement.right - kMetrics.slider_value_width;
            row.control_area = {
                placement.left,
                bottom,
                value_left - kMetrics.slider_value_gap - placement.left,
                kMetrics.slider_line_height
            };
            row.value = {
                value_left, bottom, kMetrics.slider_value_width, kMetrics.slider_line_height
            };
            bottom += kMetrics.slider_line_height;
            break;
        }
        case RowKind::choice:
        case RowKind::text_field: {
            // A drop-down's field and a text field stand on their own line,
            // as a slider's track does.
            bottom += kMetrics.slider_gap;
            const int32_t field_height =
                view.kind == RowKind::choice ? line_height : kMetrics.field_height;
            row.control_area = {
                placement.left,
                bottom,
                view.control_width > 0 ? view.control_width : kMetrics.choice_width,
                field_height
            };
            bottom += field_height;
            break;
        }
        case RowKind::value_and_button:
        case RowKind::text:
            // The value shares the label's box on the label line.
            row.value = row.label;
            break;
        case RowKind::toggle:
        case RowKind::levels:
        case RowKind::buttons:
        case RowKind::link:
            break;
        }
        bottom += placement.row_padding;
        row.height = bottom - top;
        top = bottom;
    }
    placed.bottom = top;
    return placed;
}

void scroll(PlacedRows& placed, int32_t by) noexcept {
    const auto lift = [by](Rect& rect) {
        if (has_area(rect))
            rect.y -= by;
    };
    for (PlacedRow& row : placed.rows) {
        row.top -= by;
        lift(row.label);
        lift(row.lock_area);
        for (Rect& hint : row.hints)
            lift(hint);
        lift(row.control_area);
        lift(row.value);
    }
    placed.bottom -= by;
}

ScrollArea rows_scroll(
    const PlacedRows& placed,
    const Rect& view,
    const Rect& well,
    const Rect& hit,
    int32_t page_step,
    int32_t end_gap,
    bool tall
) {
    ScrollArea area;
    area.view = view;
    area.well = well;
    area.hit = hit;
    area.page_step = page_step;
    area.content_height = placed.bottom + 1 + end_gap - view.y;
    area.limit = ScrollArea::limit_for(area.content_height, view.height);
    if (area.limit > 0 && tall) {
        // While the words are drawn in the modern fonts, whose ideographs
        // fill a hint line from its top row, the view's top edge cuts no hint
        // line at the end of the scroll: the rows scroll on until the line has
        // passed the edge.
        for (bool moved = true; moved;) {
            moved = false;
            for (const PlacedRow& row : placed.rows)
                for (const Rect& box : row.hints) {
                    const int32_t box_top = box.y - area.limit;
                    if (box_top <= view.y && box_top + box.height > view.y) {
                        area.limit += box_top + box.height - view.y;
                        moved = true;
                    }
                }
        }
        area.content_height = area.limit + view.height;
    }
    return area;
}

Rect row_button(const Rect& area, std::size_t at) noexcept {
    const std::size_t count = kMetrics.folder_button_widths.size();
    if (at >= count)
        return {area.x, area.y, 0, 0};
    int32_t left = area.x;
    for (std::size_t before = 0; before < at; ++before)
        left += kMetrics.folder_button_widths[before] + kMetrics.folder_button_gap;
    return {left, area.y, kMetrics.folder_button_widths[at], area.height};
}

std::size_t row_button_at(const Rect& area, std::size_t count, Point point) noexcept {
    const std::size_t buttons = std::min(count, kMetrics.folder_button_widths.size());
    for (std::size_t at = 0; at < buttons; ++at)
        if (contains(row_button(area, at), point))
            return at;
    return count;
}

void add_rows(DisplayList& list, const PlacedRows& placed, const RowsState& state) {
    const Rect& clip = state.clip;
    for (const PlacedRow& row : placed.rows) {
        const RowView& view = row.view;
        const bool locked = !view.lock.empty();
        const bool has_control = has_area(row.control_area) && !locked;
        const ControlId control = has_control ? row.control : no_control;
        push_frame(list, placed, row, clip);
        push_control(list, row, control, state);
        if (locked) {
            // A status is the player's explanation and keeps its strength:
            // such a row fades only its label line, down to its status.
            const int32_t faded = view.hint_is_status ? row.lock_area.y + kMetrics.regular_line +
                                                            kMetrics.hint_gap - (row.top + 1)
                                                      : row.height - 1;
            push_item(
                list,
                Role::locked_fade,
                {placed.left, row.top + 1, placed.right - placed.left, faded},
                no_control,
                {},
                {},
                {},
                clip
            );
            push_item(
                list,
                Role::lock,
                row.lock_area,
                no_control,
                view.lock,
                {},
                LockLook{view.lock},
                clip
            );
        }
        if (has_control && state.focused == control) {
            // A row of buttons rings the button the keys mark.
            const Rect ring = view.kind == RowKind::buttons
                                  ? row_button(row.control_area, state.marked_button)
                                  : row.control_area;
            push_item(list, Role::focus_ring, ring, no_control, {}, {}, FocusRingLook{true}, clip);
        }
        if (!has_control)
            continue;
        Control entry;
        entry.id = control;
        entry.rect = row.control_area;
        entry.clip = clip;
        entry.enabled = view.enabled;
        entry.name = control_name(state, view);
        entry.kind = control_kind(view.kind);
        entry.steps = takes_steps(view.kind);
        entry.group = state.group;
        entry.checked = view.kind == RowKind::toggle && view.on;
        entry.text = control_text(view);
        if (view.kind == RowKind::buttons)
            entry.parts = view.button_ids;
        else if (view.kind == RowKind::levels || view.kind == RowKind::choice)
            entry.parts = view.choice_ids;
        list.controls.push_back(std::move(entry));
        if (view.enabled)
            list.tab_order.push_back(control);
    }
    Item rule;
    rule.role = Role::rule;
    rule.rect = {placed.left, placed.bottom, placed.right - placed.left, 1};
    rule.clip = clip;
    rule.colour = colour::rule;
    list.items.push_back(std::move(rule));
}

} // namespace oa::ui::kit
