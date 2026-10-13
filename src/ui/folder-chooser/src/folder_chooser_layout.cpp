// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The locator's layout in the Game files screen's look (folder_chooser.hpp).
// A header, then a sidebar of places, the folder's rows (what each one is,
// and its size) and the selection's panel, then the buttons or, on a Steam
// Deck, the pad hints with the buttons. The rows scroll. Every control is at
// least 44 points high. Parts are drawn in the focus order: places, up, the
// filter, the rows, the system picker when it is offered, Cancel, then the
// accent button.
#include "folder_chooser_internal.hpp"

#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/folder_chooser.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::folder_chooser {

namespace {

namespace gf = oa::ui::game_files;
namespace kit = oa::ui::kit;
using gf::Colour;
using kit::Glyph;
using kit::Lettering;
using kit::Role;
using kit::TextFit;
using detail::rows_area_group;

/// A window whose shorter side is under this many points hides the side panel.
constexpr float compact_short_side_points = 600.0f;
/// The least width, in points, the rows keep before the side panel is dropped.
constexpr float least_rows_points = 280.0f;
/// The side panel's width, in points, when it is shown.
constexpr float detail_points = 280.0f;
/// The sidebar's width, in points.
constexpr float sidebar_points = 196.0f;
/// A row's least height. A Steam Deck uses the larger one.
constexpr float row_points = 44.0f;
/// A Steam Deck row's height, in points.
constexpr float deck_row_points = 52.0f;
/// Space between the columns, in points.
constexpr float gap_points = 12.0f;
/// The header bar's height, in points.
constexpr float header_points = 56.0f;

/// Returns one of the locator's words in the installed language.
///
/// @param english the words as the source writes them
/// @return their translation, or the words themselves
std::string tr(std::string_view english) {
    return std::string(oa::data::languages::interface_text(english));
}

/// Returns a length in canvas pixels.
///
/// @param type the scale
/// @param points the length
/// @return canvas pixels
int32_t px(const kit::Typesetter& type, float points) {
    return std::max<int32_t>(0, kit::canvas_pixels(type, points));
}

/// The heading a band of places is shown under. Empty for a band with no heading.
///
/// @param group the band
/// @param deck a Steam Deck
/// @return the heading
std::string group_heading(PlaceGroup group, bool deck) {
    switch (group) {
    case PlaceGroup::places:
        return tr("PLACES");
    case PlaceGroup::found:
        return tr("FOUND");
    case PlaceGroup::volumes:
        return tr("VOLUMES");
    case PlaceGroup::deck_games:
        return deck ? tr("GAMES ON THIS DECK") : tr("GAMES");
    }
    return {};
}

/// Adds a text part and returns how tall it is.
///
/// @param parts the parts
/// @param type the scale
/// @param at the text's top left
/// @param room the width it may take
/// @param words the text
/// @param look its look
/// @param most_lines the most lines
/// @return the height, in canvas pixels
int32_t add_text(
    std::vector<kit::Item>& parts,
    const kit::Typesetter& type,
    kit::Point at,
    int32_t room,
    std::string_view words,
    const Lettering& look,
    std::size_t most_lines = 1
) {
    return kit::text_part(
        parts, type, Role::text, at, room, words, look, TextFit::column, most_lines
    );
}

/// Adds one row that a press can reach.
///
/// @param parts the parts
/// @param box where it lies
/// @param target what a press reaches
/// @param marked the row is the one the panel describes, or the place that is open
/// @param glyph the mark
void add_row(
    std::vector<kit::Item>& parts, const kit::Rect& box, Target target, bool marked, Glyph glyph
) {
    kit::Item row{};
    row.role = marked ? Role::banner : Role::row;
    row.rect = box;
    row.control = control_id(target);
    row.colour = marked ? gf::green_colour : gf::text_colour;
    row.glyph = glyph;
    parts.push_back(std::move(row));
}

/// Lays the header bar out.
///
/// @param parts the parts
/// @param type the scale
/// @param safe the safe area
/// @param version the version's words
/// @return the first row under the bar
int32_t add_header(
    std::vector<kit::Item>& parts,
    const kit::Typesetter& type,
    const kit::Rect& safe,
    std::string_view version
) {
    const int32_t height = std::max(px(type, header_points), px(type, row_points));
    const kit::Rect bar{safe.x, safe.y, safe.width, height};
    kit::plain_part(parts, Role::header_bar, bar, gf::line_colour);
    const int32_t pad = px(type, 16.0f);
    const int32_t badge = px(type, 26.0f);
    const int32_t mid = bar.y + height / 2;
    kit::icon_part(
        parts, {bar.x + pad, mid - badge / 2, badge, badge}, Glyph::oa, gf::text_colour, false
    );
    const Lettering title{15.0f, true, gf::text_colour, kit::TextStyle::header};
    const int32_t title_x = bar.x + pad + badge + px(type, 10.0f);
    add_text(
        parts,
        type,
        {title_x, bar.y + px(type, 8.0f)},
        bar.width / 2,
        tr("OPEN ANNIHILATION"),
        title
    );
    const Lettering sub{12.0f, false, gf::dim_colour, kit::TextStyle::small};
    add_text(
        parts, type, {title_x, bar.y + px(type, 28.0f)}, bar.width / 2, tr("Locate the game"), sub
    );
    if (!version.empty()) {
        const Lettering ver{12.0f, false, gf::dim_colour, kit::TextStyle::small};
        kit::line_part(
            parts,
            type,
            bar.x + bar.width - pad,
            mid,
            bar.width / 3,
            version,
            ver,
            TextFit::tight_right,
            Role::version
        );
    }
    return bar.y + height;
}

/// Lays the sidebar out, in the order the focus moves.
///
/// @param parts the parts
/// @param type the scale
/// @param area the sidebar
/// @param model the locator
/// @param row_height a place's height
void add_places(
    std::vector<kit::Item>& parts,
    const kit::Typesetter& type,
    const kit::Rect& area,
    const Model& model,
    int32_t row_height
) {
    int32_t y = area.y;
    const Lettering heading{11.0f, true, gf::dim_colour, kit::TextStyle::small};
    PlaceGroup previous = PlaceGroup::places;
    bool any = false;
    for (std::size_t index = 0; index < model.places.size(); ++index) {
        const Place& place = model.places[index];
        if (!any || place.group != previous) {
            y += add_text(
                parts,
                type,
                {area.x, y},
                area.width,
                group_heading(place.group, model.deck),
                heading
            );
            y += px(type, 4.0f);
            previous = place.group;
            any = true;
        }
        if (y + row_height > area.y + area.height)
            break;
        const bool open = model.place == static_cast<int32_t>(index);
        add_row(
            parts,
            {area.x, y, area.width, row_height},
            {TargetKind::place, static_cast<uint16_t>(index)},
            open,
            Glyph::folder
        );
        const Lettering label{13.0f, false, gf::text_colour, kit::TextStyle::row_title};
        add_text(
            parts,
            type,
            {area.x + px(type, 8.0f), y + row_height / 4},
            area.width - px(type, 16.0f),
            place.label,
            label
        );
        y += row_height;
    }
}

/// Where the name, the description and the size sit in a wide row.
struct RowColumns {
    int32_t name_x = 0;
    int32_t name_room = 0;
    int32_t what_x = 0;
    int32_t what_room = 0;
    int32_t size_right = 0;
    int32_t size_room = 0;
};

/// Splits a wide row into name, description and size. The name may start
/// after an icon; it still ends where the description column begins.
///
/// @param type the scale
/// @param left the row's left
/// @param width the row's width
/// @param inset how far the name starts after `left`
/// @return the three columns
RowColumns row_columns(const kit::Typesetter& type, int32_t left, int32_t width, int32_t inset) {
    const int32_t gap = px(type, 12.0f);
    const int32_t pad = px(type, 10.0f);
    const int32_t size_room = px(type, 72.0f);
    const int32_t name_span = std::max<int32_t>(px(type, 96.0f), (width - size_room) * 42 / 100);
    RowColumns columns;
    columns.name_x = left + inset;
    columns.name_room = std::max<int32_t>(0, left + name_span - columns.name_x);
    columns.what_x = left + name_span + gap;
    columns.size_room = size_room;
    columns.size_right = left + width - pad;
    columns.what_room = std::max<int32_t>(0, columns.size_right - size_room - gap - columns.what_x);
    return columns;
}

/// Lays one folder row out: its name, what it is and its size.
///
/// @param parts the parts
/// @param type the scale
/// @param box the row, relative to the rows' top
/// @param entry the row
/// @param index which row
/// @param marked the side panel describes it
/// @param wide the what and size columns show
void add_entry(
    std::vector<kit::Item>& parts,
    const kit::Typesetter& type,
    const kit::Rect& box,
    const Entry& entry,
    uint16_t index,
    bool marked,
    bool wide
) {
    add_row(
        parts,
        box,
        {TargetKind::entry, index},
        marked,
        entry.directory ? Glyph::folder : Glyph::file
    );
    const int32_t pad = px(type, 10.0f);
    const int32_t icon = px(type, 18.0f);
    const int32_t text_x = box.x + pad + icon;
    const Lettering name{14.0f, true, gf::text_colour, kit::TextStyle::row_title};
    const Lettering what{
        12.0f,
        false,
        entry.recognised ? gf::green_colour : gf::dim_colour,
        kit::TextStyle::row_detail
    };
    const Lettering size{12.0f, false, gf::dim_colour, kit::TextStyle::small};
    if (!wide) {
        add_text(
            parts,
            type,
            {text_x, box.y + px(type, 4.0f)},
            box.width - text_x - pad,
            entry.name,
            name
        );
        add_text(
            parts,
            type,
            {text_x, box.y + box.height / 2},
            box.width - text_x - pad,
            entry.what,
            what
        );
        return;
    }
    const RowColumns columns = row_columns(type, box.x, box.width, text_x - box.x);
    const int32_t mid = box.y + box.height / 2;
    kit::line_part(
        parts, type, columns.name_x, mid, columns.name_room, entry.name, name, TextFit::tight_left
    );
    kit::line_part(
        parts, type, columns.what_x, mid, columns.what_room, entry.what, what, TextFit::tight_left
    );
    if (!entry.size_label.empty()) {
        kit::line_part(
            parts,
            type,
            columns.size_right,
            mid,
            columns.size_room,
            entry.size_label,
            size,
            TextFit::tight_right
        );
    }
}

/// Lays the side panel out.
///
/// @param parts the parts
/// @param type the scale
/// @param area the panel
/// @param model the locator
void add_detail(
    std::vector<kit::Item>& parts,
    const kit::Typesetter& type,
    const kit::Rect& area,
    const Model& model
) {
    kit::plain_part(parts, Role::panel, area, gf::line_colour);
    const int32_t pad = px(type, 12.0f);
    int32_t y = area.y + pad;
    const int32_t room = std::max<int32_t>(0, area.width - 2 * pad);
    const Lettering kicker{11.0f, true, gf::dim_colour, kit::TextStyle::small};
    y += add_text(parts, type, {area.x + pad, y}, room, tr("SELECTED"), kicker);
    y += px(type, 6.0f);
    const Entry* entry = nullptr;
    if (model.selected >= 0 && static_cast<std::size_t>(model.selected) < model.entries.size())
        entry = &model.entries[static_cast<std::size_t>(model.selected)];
    const Lettering title{16.0f, true, gf::text_colour, kit::TextStyle::title};
    const std::string heading =
        entry != nullptr ? entry->title : tr("Select a folder or an installer.");
    y += add_text(parts, type, {area.x + pad, y}, room, heading, title, 2);
    y += px(type, 8.0f);
    if (entry != nullptr && !entry->account.empty()) {
        const Lettering body{12.5f, false, gf::text_colour, kit::TextStyle::body};
        y += add_text(parts, type, {area.x + pad, y}, room, entry->account, body, 6);
        y += px(type, 8.0f);
    }
    if (entry != nullptr) {
        int32_t x = area.x + pad;
        const int32_t pill_h = px(type, 22.0f);
        const Lettering pill{11.0f, false, gf::text_colour, kit::TextStyle::small};
        for (const std::string& bundle : entry->bundles) {
            const int32_t text_size = kit::canvas_font(type, pill.points);
            const int32_t measured =
                kit::measured_width(type.measure, bundle, text_size, pill.bold);
            const int32_t width = std::min(room, measured + px(type, 16.0f));
            if (x + width > area.x + area.width - pad) {
                x = area.x + pad;
                y += pill_h + px(type, 4.0f);
            }
            if (y + pill_h > area.y + area.height - pad)
                break;
            kit::plain_part(parts, Role::panel, {x, y, width, pill_h}, gf::line_colour);
            add_text(
                parts,
                type,
                {x + px(type, 6.0f), y + px(type, 3.0f)},
                width - px(type, 8.0f),
                bundle,
                pill
            );
            x += width + px(type, 4.0f);
        }
        y += pill_h + px(type, 10.0f);
    }
    if (model.permission_note && y < area.y + area.height - pad) {
        const Lettering note{11.0f, false, gf::dim_colour, kit::TextStyle::small};
        add_text(
            parts,
            type,
            {area.x + pad, y},
            room,
            tr("macOS asks once before an app reads Desktop, Documents or Downloads."),
            note,
            4
        );
    }
}

/// Lays the buttons, and on a Steam Deck the pad hints, out.
///
/// @param parts the parts
/// @param type the scale
/// @param area the footer
/// @param model the locator
/// @param button_height a button's height
/// @return the height used
int32_t add_footer(
    std::vector<kit::Item>& parts,
    const kit::Typesetter& type,
    const kit::Rect& area,
    const Model& model,
    int32_t button_height
) {
    int32_t y = area.y;
    if (model.deck) {
        const Lettering hint{12.0f, false, gf::dim_colour, kit::TextStyle::small};
        const std::string line =
            tr("A") + "  " + tr("Open") + "    " + tr("X") + "  " +
            (model.choose_label.empty() ? tr("Use this folder") : model.choose_label) + "    " +
            tr("Y") + "  " + tr("Filter") + "    " + tr("B") + "  " + tr("Back");
        y += add_text(parts, type, {area.x, y}, area.width, line, hint);
        y += px(type, 6.0f);
    }
    std::vector<kit::ButtonSpec> leading;
    if (model.offers_dialog && !model.deck) {
        kit::ButtonSpec dialog{};
        dialog.label = tr("Use the system picker…");
        dialog.control = control_id({TargetKind::dialog, 0});
        dialog.enabled = true;
        leading.push_back(std::move(dialog));
    }
    kit::ButtonSpec cancel{};
    cancel.label = tr("Cancel");
    cancel.control = control_id({TargetKind::cancel, 0});
    leading.push_back(std::move(cancel));
    kit::ButtonSpec choose{};
    choose.label = model.choose_label.empty() ? tr("Use this folder") : model.choose_label;
    choose.role = Role::button_main;
    choose.glyph = Glyph::play;
    choose.control = control_id({TargetKind::choose, 0});
    choose.enabled = model.can_choose;
    const std::array<kit::ButtonSpec, 1> trailing{choose};
    kit::ButtonRowSizes sizes{};
    sizes.text = 12.5f;
    sizes.pad = 14.0f;
    sizes.least_main = 150.0f;
    sizes.gap = 8.0f;
    sizes.height = model.deck ? deck_row_points : row_points;
    const int32_t height = kit::button_row(
        parts,
        type,
        {area.x, y},
        area.width,
        leading,
        std::span<const kit::ButtonSpec>(trailing),
        sizes
    );
    static_cast<void>(button_height);
    return (y - area.y) + height;
}

} // namespace

Layout lay_out(
    const Model& model,
    const UiState& state,
    const Viewport& viewport,
    const TextMeasureHooks& measure
) {
    Layout layout;
    layout.paint.device = gf::device_class(viewport);
    const float scale = viewport.px_per_point > 0.0f ? viewport.px_per_point : 1.0f;
    layout.paint.px_per_point = scale;
    const kit::Typesetter type{measure, scale};
    const kit::Rect safe{
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom,
    };
    if (safe.width <= 0 || safe.height <= 0)
        return layout;
    const bool short_side = static_cast<float>(std::min(viewport.width, viewport.height)) / scale <
                            compact_short_side_points;
    std::vector<kit::Item>& parts = layout.paint.list.items;
    const int32_t under_header = add_header(parts, type, safe, model.version);
    int32_t top = under_header + px(type, 10.0f);
    if (!model.notice.empty()) {
        const Lettering notice{12.0f, false, gf::text_colour, kit::TextStyle::body};
        const int32_t height = add_text(
            parts,
            type,
            {safe.x + px(type, 16.0f), top},
            safe.width - px(type, 32.0f),
            model.notice,
            notice,
            3
        );
        top += height + px(type, 8.0f);
    }
    const int32_t margin = px(type, 16.0f);
    const int32_t bottom_limit = safe.y + safe.height - margin;
    const int32_t button_height = px(type, model.deck ? deck_row_points : row_points);
    const int32_t footer_height = button_height + (model.deck ? px(type, 22.0f) : 0);
    const int32_t content_bottom =
        std::max(top + px(type, row_points), bottom_limit - footer_height - px(type, 8.0f));
    const int32_t content_height = std::max<int32_t>(px(type, row_points), content_bottom - top);
    const int32_t gap = px(type, gap_points);
    int32_t sidebar = std::min(px(type, sidebar_points), safe.width / 4);
    sidebar = std::max(sidebar, px(type, row_points));
    const int32_t rows_left = safe.x + margin + sidebar + gap;
    int32_t rows_width = safe.x + safe.width - margin - rows_left;
    int32_t detail = 0;
    if (!short_side && rows_width > px(type, least_rows_points) + gap + px(type, detail_points)) {
        detail = px(type, detail_points);
        rows_width -= detail + gap;
    }
    const kit::Rect side{safe.x + margin, top, sidebar, content_height};
    const int32_t row_height = px(type, model.deck ? deck_row_points : row_points);
    add_places(parts, type, side, model, std::max(row_height, px(type, row_points)));

    const int32_t tool = std::max(row_height, px(type, row_points));
    kit::ButtonSpec up{};
    up.label = tr("Up");
    up.control = control_id({TargetKind::up, 0});
    up.enabled = model.has_parent;
    const int32_t up_width = std::min(px(type, 88.0f), rows_width / 4);
    kit::button_part(parts, type, {rows_left, top, up_width, tool}, up, 12.5f);
    const std::string filter_label = model.filter.empty() ? tr("Filter") : model.filter;
    kit::ButtonSpec filter{};
    filter.label = filter_label;
    filter.control = control_id({TargetKind::filter, 0});
    const int32_t filter_width =
        std::min(px(type, 160.0f), std::max<int32_t>(px(type, 88.0f), rows_width / 3));
    kit::button_part(
        parts, type, {rows_left + rows_width - filter_width, top, filter_width, tool}, filter, 12.5f
    );
    const int32_t path_x = rows_left + up_width + px(type, 8.0f);
    const int32_t path_room =
        std::max<int32_t>(0, rows_width - up_width - filter_width - px(type, 16.0f));
    const Lettering path_look{12.0f, false, gf::dim_colour, kit::TextStyle::small};
    add_text(parts, type, {path_x, top + px(type, 14.0f)}, path_room, model.path, path_look);

    const bool wide = rows_width >= px(type, 420.0f);
    int32_t rows_top = top + tool + px(type, 6.0f);
    if (wide) {
        const Lettering column{11.0f, true, gf::dim_colour, kit::TextStyle::small};
        const RowColumns columns = row_columns(type, rows_left, rows_width, 0);
        add_text(parts, type, {columns.name_x, rows_top}, columns.name_room, tr("NAME"), column);
        add_text(
            parts, type, {columns.what_x, rows_top}, columns.what_room, tr("WHAT IT IS"), column
        );
        kit::line_part(
            parts,
            type,
            columns.size_right,
            rows_top + px(type, 8.0f),
            columns.size_room,
            tr("SIZE"),
            column,
            TextFit::tight_right
        );
        rows_top += px(type, 18.0f);
    }
    const int32_t rows_height = std::max<int32_t>(row_height, content_bottom - rows_top);
    const kit::Rect rows_area{rows_left, rows_top, rows_width, rows_height};
    std::vector<kit::Item> rows;
    if (model.entries.empty()) {
        const Lettering empty{13.0f, false, gf::dim_colour, kit::TextStyle::body};
        add_text(
            rows, type, {rows_left, px(type, 8.0f)}, rows_width, tr("No folders here."), empty
        );
    }
    const int32_t entry_height = std::max(row_height, px(type, row_points));
    for (std::size_t index = 0; index < model.entries.size(); ++index) {
        const bool marked = model.selected == static_cast<int32_t>(index);
        add_entry(
            rows,
            type,
            {rows_left, static_cast<int32_t>(index) * entry_height, rows_width, entry_height},
            model.entries[index],
            static_cast<uint16_t>(index),
            marked,
            wide
        );
    }
    const int32_t body = model.entries.empty()
                             ? px(type, 28.0f)
                             : static_cast<int32_t>(model.entries.size()) * entry_height;
    const kit::ScrollColumn placed = kit::scroll_column(
        rows_area, 0, body, 0, 0, px(type, 48.0f), px(type, static_cast<float>(state.scroll_points))
    );
    kit::move_parts(parts, rows, placed.body, rows_area);
    layout.paint.rows = rows_area;
    const int32_t limit = std::max(0, body - rows_height);
    layout.scroll_max_points =
        limit > 0 ? static_cast<int32_t>(std::ceil(static_cast<float>(limit) / scale)) : 0;
    layout.paint.scroll_max_points = layout.scroll_max_points;
    static_cast<void>(placed);

    if (detail > 0) {
        const kit::Rect panel{rows_left + rows_width + gap, top, detail, content_height};
        add_detail(parts, type, panel, model);
    }
    const kit::Rect footer{
        safe.x + margin, content_bottom + px(type, 8.0f), safe.width - 2 * margin, footer_height
    };
    add_footer(parts, type, footer, model, button_height);

    kit::list_part_controls(layout.paint.list);
    for (kit::Control& control : layout.paint.list.controls) {
        if (target_of(control.id).kind == TargetKind::entry)
            control.group = rows_area_group;
    }
    for (const kit::ControlId id : layout.paint.list.tab_order)
        layout.focus_order.push_back(target_of(id));
    kit::Interaction shown{};
    shown.focus_shown = state.focus_shown;
    if (state.focus >= 0 && static_cast<std::size_t>(state.focus) < layout.focus_order.size())
        shown.focused = control_id(layout.focus_order[static_cast<std::size_t>(state.focus)]);
    if (const kit::Item* held = detail::target_part(layout.paint.list, state.pressed);
        held != nullptr && !held->state.disabled)
        shown.pressed = held->control;
    kit::mark_states(layout.paint.list, shown);
    return layout;
}

} // namespace oa::ui::folder_chooser
