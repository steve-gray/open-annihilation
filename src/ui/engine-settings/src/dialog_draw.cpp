// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How the settings dialog and the OA button are drawn, and the fonts they
// draw their texts in: a dark gunmetal panel with one-pixel raised edges,
// hairline rules, light text with muted hints, and one green accent for
// what is selected. The header and the button show the Open Annihilation
// icon, or the green OA mark when the host has no icon to give.

#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"
#include "oa/data/mod_profile/overrides.hpp"
#include "oa/ui/engine_settings/notice.hpp"
#include "oa/ui/engine_settings/prompt.hpp"

#include "geometry.hpp"
#include "notice_geometry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

namespace renderer = oa::ui::frontend_renderer;
namespace layout = geometry;
namespace kit = oa::ui::kit;

namespace {

using renderer::Rgb;
using renderer::SourceRect;

/// A canvas on the dialog's surface, for the kit's crisp controls.
kit::Canvas drawn(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const DialogFonts& fonts,
    const renderer::RgbaPicture& icon = {}
) {
    return {&target, placement, &fonts, icon};
}

/// The placement clipped to a rectangle, as the kit clips a canvas.
renderer::Placement clipped_view(const renderer::Placement& placement, const SourceRect& rect) {
    return kit::clipped({nullptr, placement, nullptr, {}}, rect).placement;
}

/// An Off/On switch's look, its captions already looked up.
kit::SwitchLook switch_look(bool on, bool hovered, bool locked) {
    return {
        on,
        hovered,
        locked,
        std::string(layout::shown_text(layout::off_text)),
        std::string(layout::shown_text(layout::on_text)),
    };
}

/// A level strip's look, its captions already looked up. offered is the
/// count a player may choose, never the strip's zero sentinel.
kit::LevelsLook strip_look(Setting setting, std::size_t level, bool hovered, bool locked) {
    const layout::Strip strip = layout::strip_of(setting);
    kit::LevelsLook look;
    look.level_width = strip.level_width;
    look.chosen = level;
    look.offered = layout::offered_levels(strip);
    look.hovered = hovered;
    look.locked = locked;
    look.captions.reserve(strip.levels);
    for (std::size_t index = 0; index < strip.levels; ++index)
        look.captions.emplace_back(layout::shown_text(layout::strip_caption(setting, index)));
    return look;
}

/// Draws the header: the icon, the title, the shared game's note and the version.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
/// @param icon the Open Annihilation icon; empty draws the OA mark
void draw_header(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts,
    const renderer::RgbaPicture& icon
) {
    const int32_t inner = dialog_width - 2 * layout::edge;
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::header_top, inner, layout::header_height},
        kit::rgb(kit::colour::band)
    );
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::header_rule_row, inner, 1},
        kit::rgb(kit::colour::rule)
    );
    const SourceRect mark = layout::header_mark;
    kit::draw_header_mark(drawn(target, placement, fonts, icon), mark);
    const int32_t title_left = mark.x + mark.width + layout::header_gap;
    const SourceRect title{
        title_left, layout::header_top, layout::title_width, layout::header_height
    };
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::regular,
        layout::shown_text(layout::title_text),
        title,
        kit::Align::left,
        kit::rgb(kit::colour::text),
        layout::heading_tracking
    );
    // The modern fonts draw the title wider than the game's font: the suffix follows it.
    const bool modern_only = !std::any_of(
        fonts.regular.font.glyphs.begin(), fonts.regular.font.glyphs.end(), [](const auto& glyph) {
            return glyph.has_value();
        }
    );
    const int32_t title_right = modern_only
                                    ? std::max(
                                          title.x + title.width,
                                          title.x + kit::modern_tracked_width(
                                                        fonts,
                                                        kit::FontRole::regular,
                                                        layout::shown_text(layout::title_text),
                                                        layout::heading_tracking,
                                                        placement.scale
                                                    )
                                      )
                                    : title.x + title.width;
    const SourceRect suffix{
        title_right + layout::header_gap,
        layout::header_top,
        layout::title_suffix_width,
        layout::header_height,
    };
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::regular,
        layout::shown_text(layout::title_suffix_text),
        suffix,
        kit::Align::left,
        kit::rgb(kit::colour::hint),
        layout::heading_tracking
    );
    const SourceRect version{
        layout::content_right - layout::version_width,
        layout::header_top,
        layout::version_width,
        layout::header_height,
    };
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(dialog.version),
        version,
        kit::Align::right,
        kit::rgb(kit::colour::quiet)
    );
    if (dialog.locks.shared_game) {
        const int32_t version_left = version.x + version.width -
                                     kit::text_width(fonts, kit::FontRole::small, dialog.version);
        const int32_t shared_left = suffix.x + suffix.width + layout::header_gap;
        const SourceRect shared{
            shared_left,
            layout::header_top,
            version_left - layout::version_gap - shared_left,
            layout::header_height,
        };
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            kit::FontRole::small,
            layout::shown_text(layout::shared_game_text),
            shared,
            kit::Align::right,
            kit::rgb(kit::colour::lock)
        );
    }
}

/// Draws the section list.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_list(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    const int32_t body_height = layout::footer_rule_row - layout::body_top;
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::body_top, layout::list_width, body_height},
        kit::rgb(kit::colour::list)
    );
    renderer::fill_source_rect(
        target,
        placement,
        {layout::list_rule_column, layout::body_top, 1, body_height},
        kit::rgb(kit::colour::rule)
    );
    if (dialog.kind == DialogKind::engine)
        renderer::fill_source_rect(
            target, placement, layout::list_divider(), kit::rgb(kit::colour::rule)
        );
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        const SourceRect item = layout::dialog_list_item(dialog, page);
        const int32_t control = page_control(page);
        const bool selected = page == dialog.page;
        const bool hovered = dialog.hovered == control || dialog.pressed == control;
        Rgb text = kit::rgb(kit::colour::list_text);
        if (selected) {
            renderer::fill_source_rect(
                target, placement, item, kit::rgb(kit::colour::list_selected)
            );
            renderer::fill_source_rect(
                target,
                placement,
                {item.x + layout::list_marker_offset,
                 item.y + (item.height - layout::list_marker_height) / 2,
                 layout::list_marker_width,
                 layout::list_marker_height},
                kit::rgb(kit::colour::accent)
            );
            text = kit::rgb(kit::colour::text);
        } else if (hovered) {
            renderer::fill_source_rect(target, placement, item, kit::rgb(kit::colour::hover));
            text = kit::rgb(kit::colour::text);
        }
        const SourceRect caption{
            item.x + layout::list_text_offset,
            item.y,
            item.width - layout::list_text_offset - layout::list_text_margin,
            item.height,
        };
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            kit::FontRole::regular,
            layout::shown_text(layout::page_name(page)),
            caption,
            kit::Align::left,
            text
        );
        if (dialog.focused == control)
            kit::draw_focus_ring(drawn(target, placement, fonts), item, false);
    }
}

/// Returns how wide a text is in the regular font, as the dialog draws it.
///
/// @param fonts the fonts
/// @return the measure, in source pixels
std::function<int32_t(std::string_view)> regular_width(const DialogFonts& fonts) {
    return [&fonts](std::string_view text) {
        return dialog_text_width(fonts, DialogFont::regular, text);
    };
}

/// Draws an open drop-down's menu over the dialog. The items are already
/// looked up.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param field the drop-down's field
/// @param setting the drop-down's setting
/// @param fonts the fonts
void paint_open_menu(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const SourceRect& field,
    Setting setting,
    const DialogFonts& fonts
) {
    const std::size_t choices = layout::choice_count(dialog, setting);
    const SourceRect menu = layout::choice_list(field, choices);
    const int32_t shown = layout::shown_choices(choices);
    kit::ChoiceMenuLook look;
    look.first = dialog.list_first;
    look.total = static_cast<int32_t>(choices);
    look.chosen = static_cast<int32_t>(layout::choice_index(dialog, setting));
    look.marked = dialog.list_marked;
    look.focus_shown = dialog.focused != no_control;
    for (int32_t place = 0; place < shown; ++place) {
        const int32_t item = dialog.list_first + place;
        if (item >= static_cast<int32_t>(choices))
            break;
        look.shown.emplace_back(
            layout::shown_text(
                layout::shown_choice_text(
                    dialog,
                    setting,
                    static_cast<std::size_t>(item),
                    layout::choice_item_text_room,
                    regular_width(fonts)
                )
            )
        );
    }
    kit::draw_choice_menu(drawn(target, placement, fonts), menu, look);
}

/// Draws one row of Developer's list: an area's or a hack's header, a line
/// under a hack, or a parameter's control.
///
/// @param[in,out] target the surface
/// @param in_view where the dialog lands, clipped to the list's view
/// @param dialog the dialog
/// @param row the row
/// @param first the row is the list's first, which the line over the list tops
/// @param fonts the fonts
void draw_list_row(
    renderer::Surface& target,
    const renderer::Placement& in_view,
    const Dialog& dialog,
    const layout::ListRow& row,
    bool first,
    const DialogFonts& fonts
) {
    const bool header =
        row.kind == layout::ListRowKind::area || row.kind == layout::ListRowKind::hack;
    const bool takes = header || !row.locked;
    const bool hovered = takes && row.control != no_control &&
                         (dialog.hovered == row.control || dialog.pressed == row.control);
    const bool focused = takes && row.control != no_control && dialog.focused == row.control;
    if (row.kind == layout::ListRowKind::area && !first)
        renderer::fill_source_rect(
            target,
            in_view,
            {layout::content_left, row.top, layout::content_width, 1},
            kit::rgb(kit::colour::rule)
        );
    if (header && hovered)
        renderer::fill_source_rect(
            target,
            in_view,
            {row.control_area.x,
             row.control_area.y + 1,
             row.control_area.width,
             row.control_area.height - 1},
            kit::rgb(kit::colour::hover)
        );
    switch (row.kind) {
    case layout::ListRowKind::area:
        kit::draw_mark(
            drawn(target, in_view, fonts),
            row.open ? kit::Mark::arrow_open : kit::Mark::arrow_closed,
            {row.arrow.x, row.arrow.y},
            hovered ? kit::colour::text : kit::colour::hint
        );
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::regular,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::text)
        );
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.shown),
            row.value,
            kit::Align::right,
            kit::rgb(kit::colour::hint)
        );
        break;
    case layout::ListRowKind::hack:
        kit::draw_mark(
            drawn(target, in_view, fonts),
            row.open ? kit::Mark::arrow_open : kit::Mark::arrow_closed,
            {row.arrow.x, row.arrow.y},
            hovered ? kit::colour::text : kit::colour::hint
        );
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            row.on ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::hint)
        );
        kit::draw_switch(
            drawn(target, in_view, fonts),
            row.toggle,
            switch_look(row.on, hovered && !row.locked, row.locked)
        );
        break;
    case layout::ListRowKind::id:
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::quiet)
        );
        break;
    case layout::ListRowKind::text:
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::hint)
        );
        break;
    case layout::ListRowKind::scope:
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::lock)
        );
        break;
    case layout::ListRowKind::heading:
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::text)
        );
        break;
    case layout::ListRowKind::toggle:
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::text)
        );
        kit::draw_switch(
            drawn(target, in_view, fonts),
            row.control_area,
            switch_look(row.on, hovered, row.locked)
        );
        break;
    case layout::ListRowKind::slider:
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.text),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::text)
        );
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(row.shown),
            row.value,
            kit::Align::right,
            kit::rgb(kit::colour::text)
        );
        kit::draw_slider(
            drawn(target, in_view, fonts),
            row.control_area,
            {row.stop, row.stops, row.locked, hovered}
        );
        break;
    }
    // A control that takes no change, while Developer Mode is off, fades
    // as a locked row's does; its text keeps its strength.
    if (row.locked &&
        (row.kind == layout::ListRowKind::hack || row.kind == layout::ListRowKind::toggle ||
         row.kind == layout::ListRowKind::slider))
        renderer::blend_source_rect(
            target,
            in_view,
            row.kind == layout::ListRowKind::hack ? row.toggle : row.control_area,
            kit::rgb(kit::colour::panel),
            kit::locked_fade
        );
    if (!focused)
        return;
    kit::draw_focus_ring(drawn(target, in_view, fonts), row.control_area, !header);
}

/// Draws what lies under Developer's rows: its list clipped to the list's
/// view with the list's scroll bar while it scrolls, and the list's footer
/// with Show Active Only and Restore profile values.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param open Developer's rows and list (layout::open_rows)
/// @param fonts the fonts
void draw_developer(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const layout::ScrolledRows& open,
    const DialogFonts& fonts
) {
    const auto rule = [&](int32_t row) {
        renderer::fill_source_rect(
            target,
            placement,
            {layout::content_left, row, layout::content_width, 1},
            kit::rgb(kit::colour::rule)
        );
    };
    const auto hot = [&](int32_t control) {
        return dialog.hovered == control || dialog.pressed == control;
    };
    const renderer::Placement in_view = clipped_view(placement, layout::developer_view_clip);
    for (std::size_t index = 0; index < open.list.rows.size(); ++index)
        draw_list_row(target, in_view, dialog, open.list.rows[index], index == 0, fonts);
    // Scrolled from its top, the view's first row keeps a line.
    if (open.scroll > 0)
        renderer::fill_source_rect(
            target,
            in_view,
            {layout::content_left, layout::developer_view.y, layout::content_width, 1},
            kit::rgb(kit::colour::rule)
        );
    if (open.limit > 0) {
        const bool bar_hot =
            dialog.hovered == scroll_bar_control || dialog.pressed == scroll_bar_control;
        kit::draw_scroll_bar(
            drawn(target, placement, fonts),
            open.area.well,
            layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height),
            bar_hot
        );
    }

    rule(layout::developer_footer_rule);
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::regular,
        layout::shown_text(
            layout::active_only_text(
                active_hack_count(dialog), oa::data::mod_profile::standard_hacks().size()
            )
        ),
        layout::active_only_label,
        kit::Align::left,
        kit::rgb(kit::colour::text)
    );
    kit::draw_switch(
        drawn(target, placement, fonts),
        layout::active_only_switch,
        switch_look(dialog.developer.active_only, hot(active_only_control), false)
    );
    if (dialog.focused == active_only_control)
        kit::draw_focus_ring(drawn(target, placement, fonts), layout::active_only_switch, true);
    // Restore profile values takes a press only while Developer Mode is on.
    // It lights while the pointer is over it or a press on it is held.
    const SourceRect button = layout::restore_profile_button;
    const bool enabled = dialog.chosen.developer_mode;
    const bool over = hot(restore_profile_control);
    kit::draw_button(
        drawn(target, placement, fonts),
        button,
        {std::string(layout::shown_text(layout::restore_profile_text)),
         kit::ButtonStyle::plain,
         enabled && over,
         enabled && dialog.pressed == restore_profile_control,
         enabled}
    );
    if (enabled && dialog.focused == restore_profile_control)
        kit::draw_focus_ring(drawn(target, placement, fonts), button, true);
}

/// Draws a mod's badge: its picture, the OA mark for No Mod, or a blank
/// dashed square when it has none.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param rect the badge's square
/// @param shown the mod's row's texts and details; null details for No Mod
/// @param no_mod the badge is No Mod's
void draw_badge(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& rect,
    const layout::ModRowText& shown,
    bool no_mod
) {
    if (no_mod) {
        renderer::draw_outline(target, placement, rect, kit::rgb(kit::colour::accent));
        // The small OA mark is 9 by 7. It is centred in the badge, one row
        // down when the spare rows are odd, as the header's square centres it.
        constexpr int32_t mark_width = 9;
        constexpr int32_t mark_height = 7;
        kit::draw_mark(
            {&target, placement, nullptr, {}},
            kit::Mark::oa_small,
            {rect.x + (rect.width - mark_width) / 2, rect.y + (rect.height - mark_height + 1) / 2},
            kit::colour::accent
        );
        return;
    }
    if (shown.details != nullptr && shown.details->badge_width > 0) {
        const renderer::RgbaPicture picture{
            shown.details->badge_width, shown.details->badge_height, shown.details->badge_pixels
        };
        if (renderer::picture_drawable(picture)) {
            renderer::draw_picture(target, placement, rect, picture);
            renderer::draw_outline(target, placement, rect, kit::rgb(kit::colour::control_border));
            return;
        }
    }
    // A blank badge: a dashed square.
    for (int32_t along = 0; along < rect.width; along += 3) {
        const int32_t dash = std::min(2, rect.width - along);
        renderer::fill_source_rect(
            target,
            placement,
            {rect.x + along, rect.y, dash, 1},
            kit::rgb(kit::colour::control_border)
        );
        renderer::fill_source_rect(
            target,
            placement,
            {rect.x + along, rect.y + rect.height - 1, dash, 1},
            kit::rgb(kit::colour::control_border)
        );
    }
    for (int32_t along = 0; along < rect.height; along += 3) {
        const int32_t dash = std::min(2, rect.height - along);
        renderer::fill_source_rect(
            target,
            placement,
            {rect.x, rect.y + along, 1, dash},
            kit::rgb(kit::colour::control_border)
        );
        renderer::fill_source_rect(
            target,
            placement,
            {rect.x + rect.width - 1, rect.y + along, 1, dash},
            kit::rgb(kit::colour::control_border)
        );
    }
}

/// Draws Mods: the lock line while it is locked, the list of rows in its
/// view with its scroll bar, OPEN MODS FOLDER and the lines under it.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param open Mods' rows
/// @param fonts the fonts
void draw_mods(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const layout::ScrolledRows& open,
    const DialogFonts& fonts
) {
    const bool locked = dialog.locks.mod != Lock::none;
    const auto small_width = [&fonts](std::string_view text) {
        return dialog_text_width(fonts, DialogFont::small, text);
    };
    if (locked) {
        kit::draw_mark(
            drawn(target, placement, fonts),
            kit::Mark::padlock,
            {layout::mods_lock_line.x,
             layout::mods_lock_text.y +
                 (layout::mods_lock_text.height - layout::padlock_height) / 2},
            kit::colour::lock
        );
        kit::WrapRules rules;
        rules.shorten_word = [&](std::string_view word) {
            return layout::cut_text(word, layout::mods_lock_text.width, small_width);
        };
        const auto lines = kit::wrap(
            layout::shown_text(
                dialog.locks.mod == Lock::command_line ? layout::mod_from_command_line_text
                                                       : layout::mod_in_game_text
            ),
            layout::mods_lock_text.width,
            small_width,
            rules
        );
        for (std::size_t line = 0; line < lines.size() && line < 2; ++line) {
            SourceRect rect = layout::mods_lock_text;
            rect.y += static_cast<int32_t>(line) * rect.height;
            kit::draw_boxed_text(
                target,
                placement,
                fonts,
                kit::FontRole::small,
                layout::shown_text(lines[line]),
                rect,
                kit::Align::left,
                kit::rgb(kit::colour::lock)
            );
        }
    }
    const SourceRect& view = open.area.view;
    const renderer::Placement in_view = clipped_view(
        placement,
        {view.x - layout::focus_inset, view.y, view.width + 2 * layout::focus_inset, view.height}
    );
    const auto rows = mod_rows(dialog);
    for (std::size_t index = 0; index < open.rows.rows.size() && index < rows.size(); ++index) {
        const layout::Row& row = open.rows.rows[index];
        const layout::ModRowText shown = layout::mod_row_text(dialog, rows[index]);
        const bool playing = rows[index].playing;
        const bool hovered =
            !locked && (dialog.hovered == row.control || dialog.pressed == row.control);
        const SourceRect& box = row.control_area;
        renderer::fill_source_rect(
            target,
            in_view,
            box,
            playing ? kit::rgb(kit::colour::list_selected)
                    : (hovered ? kit::rgb(kit::colour::hover) : kit::rgb(kit::colour::list))
        );
        renderer::draw_outline(
            target,
            in_view,
            box,
            playing ? kit::rgb(kit::colour::accent)
                    : (hovered ? kit::rgb(kit::colour::control_hover)
                               : kit::rgb(kit::colour::control_border))
        );
        const SourceRect badge{
            box.x + layout::mod_row_inset,
            box.y + (box.height - layout::mod_badge_side) / 2,
            layout::mod_badge_side,
            layout::mod_badge_side
        };
        draw_badge(target, in_view, badge, shown, rows[index].offered == no_mod_row);
        // The version at the top right; the title, and PLAYING after it,
        // in what is left of the line.
        const int32_t version_width = std::min(small_width(shown.version), row.value.width / 3);
        const SourceRect version{
            row.value.x + row.value.width - version_width,
            row.value.y,
            version_width,
            row.value.height
        };
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(layout::cut_text(shown.version, version.width, small_width)),
            version,
            kit::Align::right,
            kit::rgb(kit::colour::hint)
        );
        const std::string_view tag = layout::shown_text(layout::playing_text);
        const int32_t tag_width = playing ? small_width(tag) + 6 : 0;
        const int32_t title_room = version.x - 6 - row.label.x - tag_width;
        const std::string title = layout::cut_text(shown.title, title_room, regular_width(fonts));
        const int32_t title_width = kit::text_width(fonts, kit::FontRole::regular, title);
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::regular,
            layout::shown_text(title),
            {row.label.x, row.label.y, title_room, row.label.height},
            kit::Align::left,
            kit::rgb(kit::colour::text)
        );
        if (playing)
            kit::draw_boxed_text(
                target,
                in_view,
                fonts,
                kit::FontRole::small,
                layout::shown_text(tag),
                {row.label.x + title_width + 6, row.value.y, tag_width, row.value.height},
                kit::Align::left,
                kit::rgb(kit::colour::accent)
            );
        // A row whose folder keeps an earlier version shows ROLL BACK at
        // the right of its description, which is cut shorter for it.
        const bool roll_back = layout::offers_roll_back(dialog, rows[index]);
        const SourceRect roll_back_box = layout::roll_back_button(row);
        SourceRect description = row.hints[0];
        if (roll_back)
            description.width = roll_back_box.x - layout::mod_roll_back_gap - description.x;
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::small,
            layout::shown_text(layout::cut_text(shown.description, description.width, small_width)),
            description,
            kit::Align::left,
            shown.has_profile ? kit::rgb(kit::colour::hint) : kit::rgb(kit::colour::lock)
        );
        if (roll_back) {
            const int32_t control = layout::roll_back_control(open.rows, index);
            const bool over = !locked && (dialog.hovered == control || dialog.pressed == control);
            const bool held = !locked && dialog.pressed == control;
            kit::draw_button(
                drawn(target, in_view, fonts),
                roll_back_box,
                {std::string(layout::shown_text(layout::roll_back_text)),
                 kit::ButtonStyle::inset,
                 over,
                 held,
                 true}
            );
            if (locked)
                renderer::blend_source_rect(
                    target, in_view, roll_back_box, kit::rgb(kit::colour::panel), kit::locked_fade
                );
            if (dialog.focused == control && !locked)
                kit::draw_focus_ring(drawn(target, in_view, fonts), roll_back_box, true);
        }
        if (locked && !playing)
            renderer::blend_source_rect(
                target, in_view, box, kit::rgb(kit::colour::panel), kit::locked_fade
            );
        if (dialog.focused == row.control && !locked)
            kit::draw_focus_ring(drawn(target, in_view, fonts), box, true);
    }
    if (open.limit > 0) {
        const bool bar_hot =
            dialog.hovered == scroll_bar_control || dialog.pressed == scroll_bar_control;
        kit::draw_scroll_bar(
            drawn(target, placement, fonts),
            open.area.well,
            layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height),
            bar_hot
        );
    }
    const int32_t control = layout::mods_folder_control(open.rows);
    const SourceRect& button = layout::mods_folder_button;
    const bool over = !locked && (dialog.hovered == control || dialog.pressed == control);
    const bool held = !locked && dialog.pressed == control;
    kit::draw_button(
        drawn(target, placement, fonts),
        button,
        {std::string(layout::shown_text(layout::shown_text(layout::open_mods_folder_text))),
         kit::ButtonStyle::quiet,
         over,
         held,
         true}
    );
    if (locked)
        renderer::blend_source_rect(
            target, placement, button, kit::rgb(kit::colour::panel), kit::locked_fade
        );
    if (dialog.focused == control && !locked)
        kit::draw_focus_ring(drawn(target, placement, fonts), button, true);
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(layout::shown_text(layout::mods_folders_text[0])),
        layout::mods_note_first,
        kit::Align::left,
        kit::rgb(kit::colour::quiet)
    );
    const bool notice = !dialog.folder_notice.empty();
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(
            notice ? std::string_view(dialog.folder_notice)
                   : layout::shown_text(layout::mods_folders_text[1])
        ),
        layout::mods_note_second,
        kit::Align::left,
        notice ? kit::rgb(kit::colour::lock) : kit::rgb(kit::colour::quiet)
    );
}

/// Draws the open section: its heading, its rows clipped to the view they
/// scroll in, and its scroll bar while they scroll; on Developer, its list
/// and the list's footer under its rows.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_section(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(layout::page_heading(dialog.page)),
        layout::heading,
        kit::Align::left,
        kit::rgb(kit::colour::quiet),
        layout::heading_tracking
    );
    const layout::ScrolledRows open = layout::open_rows(dialog);
    if (layout::mods_page(dialog)) {
        draw_mods(target, placement, dialog, open, fonts);
        return;
    }
    const layout::Rows& rows = open.rows;
    // A row the view cuts shows the part inside it, its text and its focus
    // outline included.
    const renderer::Placement in_view = clipped_view(placement, layout::view_clip);
    for (const layout::Row& row : rows.rows) {
        const bool locked = row.lock != Lock::none;
        const bool hovered =
            !locked && (dialog.hovered == row.control || dialog.pressed == row.control);
        renderer::fill_source_rect(
            target,
            in_view,
            {layout::content_left, row.top, layout::content_width, 1},
            kit::rgb(kit::colour::rule)
        );
        kit::draw_boxed_text(
            target,
            in_view,
            fonts,
            kit::FontRole::regular,
            layout::shown_text(layout::row_label(row.setting)),
            row.label,
            kit::Align::left,
            kit::rgb(kit::colour::text)
        );
        // The host's texts under a Game files row keep to their line's
        // columns.
        const bool host_text = layout::is_button(row.setting) || layout::is_text(row.setting);
        for (std::size_t line = 0; line < row.hint_lines; ++line) {
            // A notice is drawn as a lock's text is.
            const layout::HintLine hint = layout::row_hint(dialog, row.setting, line);
            const SourceRect& box = row.hints[line];
            kit::draw_boxed_text(
                target,
                host_text
                    ? clipped_view(in_view, {box.x, layout::view.y, box.width, layout::view.height})
                    : in_view,
                fonts,
                kit::FontRole::small,
                layout::shown_text(
                    layout::shown_hint_text(
                        dialog,
                        row.setting,
                        line,
                        row.hints[line].width,
                        [&fonts](std::string_view text) {
                            return dialog_text_width(fonts, DialogFont::small, text);
                        }
                    )
                ),
                row.hints[line],
                kit::Align::left,
                hint.notice ? kit::rgb(kit::colour::lock) : kit::rgb(kit::colour::hint)
            );
        }
        // Where the files are shows text alone: it has no control to draw.
        if (layout::is_button(row.setting)) {
            kit::draw_button(
                drawn(target, in_view, fonts),
                row.control_area,
                {std::string(layout::shown_text(layout::manage_text)),
                 kit::ButtonStyle::accent,
                 hovered,
                 dialog.pressed == row.control && dialog.hovered == row.control,
                 true}
            );
        } else if (layout::is_buttons(row.setting)) {
            const bool over_row = dialog.hovered == row.control;
            for (std::size_t index = 0; index < folder_button_count; ++index) {
                const SourceRect button = layout::folder_button(row.control_area, index);
                const bool button_hovered = over_row && dialog.folder_hovered == index;
                const bool button_held = button_hovered && dialog.pressed == row.control;
                kit::draw_button(
                    drawn(target, in_view, fonts),
                    button,
                    {std::string(layout::shown_text(layout::folder_button_text(index))),
                     kit::ButtonStyle::quiet,
                     button_hovered,
                     button_held,
                     true}
                );
            }
        } else if (layout::is_strip(row.setting)) {
            if (row.control_area.width > 0)
                kit::draw_levels(
                    drawn(target, in_view, fonts),
                    row.control_area,
                    strip_look(
                        row.setting,
                        layout::strip_level(dialog.chosen, row.setting),
                        hovered,
                        locked
                    )
                );
        } else if (layout::is_slider(row.setting)) {
            kit::draw_slider(
                drawn(target, in_view, fonts),
                row.control_area,
                {layout::stop_of(
                     layout::slider_settings(dialog),
                     row.setting,
                     dialog.highest_offered_unit,
                     dialog.offered_screen_sizes
                 ),
                 layout::stops_of(
                     dialog.chosen,
                     row.setting,
                     dialog.highest_offered_unit,
                     dialog.offered_screen_sizes
                 ),
                 locked,
                 hovered}
            );
            kit::draw_boxed_text(
                target,
                in_view,
                fonts,
                kit::FontRole::regular,
                layout::shown_text(layout::value_text(row.setting, dialog)),
                row.value,
                kit::Align::right,
                kit::rgb(kit::colour::text)
            );
        } else if (layout::is_choice(row.setting)) {
            kit::draw_choice(
                drawn(target, in_view, fonts),
                row.control_area,
                {std::string(
                     layout::shown_text(
                         layout::field_text(
                             dialog, row, layout::choice_field_text_room, regular_width(fonts)
                         )
                     )
                 ),
                 hovered,
                 dialog.open_list == row.control}
            );
        } else if (row.control_area.width > 0) {
            kit::draw_switch(
                drawn(target, in_view, fonts),
                row.control_area,
                switch_look(
                    layout::switch_on(dialog.chosen, row.setting) ||
                        row.lock == Lock::set_by_language,
                    hovered,
                    locked
                )
            );
        }
        if (locked) {
            // A status is the player's explanation and keeps its strength:
            // such a row fades only its label line, down to its status.
            const int32_t faded =
                row.hint_is_status
                    ? layout::row_padding + layout::label_line_height + layout::hint_gap
                    : row.height - 1;
            renderer::blend_source_rect(
                target,
                in_view,
                {layout::content_left, row.top + 1, layout::content_width, faded},
                kit::rgb(kit::colour::panel),
                kit::locked_fade
            );
            const std::string once(layout::shown_text(layout::lock_text(row.lock)));
            kit::draw_lock(
                drawn(target, in_view, fonts),
                row.lock_area,
                {std::string(layout::shown_text(once))}
            );
        }
        // Your files rings the button the keys mark.
        const SourceRect focus_area =
            layout::is_buttons(row.setting)
                ? layout::folder_button(
                      row.control_area, static_cast<std::size_t>(dialog.folder_marked)
                  )
                : row.control_area;
        if (dialog.focused == row.control && !locked)
            kit::draw_focus_ring(drawn(target, in_view, fonts), focus_area, true);
    }
    renderer::fill_source_rect(
        target,
        in_view,
        {layout::content_left, rows.bottom, layout::content_width, 1},
        kit::rgb(kit::colour::rule)
    );
    // Developer's rows stay at its top, the line under them over its list.
    if (layout::developer_page(dialog)) {
        draw_developer(target, placement, dialog, open, fonts);
        return;
    }
    if (open.limit == 0)
        return;
    // Scrolled from its top, the view's first row keeps a line, so that the
    // cut there is the same hairline as a row's own.
    if (open.scroll > 0)
        renderer::fill_source_rect(
            target,
            in_view,
            {layout::content_left, layout::view.y, layout::content_width, 1},
            kit::rgb(kit::colour::rule)
        );
    const bool bar_hot =
        dialog.hovered == scroll_bar_control || dialog.pressed == scroll_bar_control;
    kit::draw_scroll_bar(
        drawn(target, placement, fonts),
        open.area.well,
        layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height),
        bar_hot
    );
}

/// Draws the footer: Restore defaults, Cancel and OK.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_footer(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    const int32_t inner = dialog_width - 2 * layout::edge;
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::footer_rule_row, inner, 1},
        kit::rgb(kit::colour::rule)
    );
    renderer::fill_source_rect(
        target,
        placement,
        {layout::edge, layout::footer_top, inner, layout::footer_height},
        kit::rgb(kit::colour::band)
    );
    const std::array<std::pair<int32_t, std::string_view>, 3> buttons{{
        {restore_control, layout::restore_text},
        {cancel_control, layout::cancel_text},
        {ok_control, layout::ok_text},
    }};
    for (const auto& [control, caption] : buttons) {
        const SourceRect button = layout::footer_button(control);
        const bool held = dialog.pressed == control && dialog.hovered == control;
        const bool hovered = dialog.hovered == control;
        kit::draw_button(
            drawn(target, placement, fonts),
            button,
            {std::string(layout::shown_text(caption)),
             control == ok_control ? kit::ButtonStyle::accent : kit::ButtonStyle::plain,
             hovered,
             held,
             true}
        );
        if (dialog.focused == control)
            kit::draw_focus_ring(drawn(target, placement, fonts), button, true);
    }
}

/// Draws the question a folder without an oamod.yaml raises, over the
/// dialog: a raised box in the header's face with the folder's path, the
/// question in amber and white, and No and Yes as Cancel and OK look, the
/// marked one ringed in green.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param fonts the fonts
void draw_question(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    renderer::fill_source_rect(
        target, placement, layout::question_box, kit::rgb(kit::colour::band)
    );
    renderer::draw_outline(target, placement, layout::question_box, kit::rgb(kit::colour::accent));
    const bool roll_back = dialog.mod_question == ModQuestion::roll_back;
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(
            layout::shown_text(
                roll_back ? layout::roll_back_heading_text : layout::switch_heading_text
            )
        ),
        layout::question_heading,
        kit::Align::left,
        kit::rgb(kit::colour::quiet),
        layout::heading_tracking
    );
    const layout::ModRowText offered =
        layout::mod_row_text(dialog, ModRow{dialog.switch_question, false});
    draw_badge(
        target, placement, layout::question_badge, offered, dialog.switch_question == no_mod_row
    );
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::regular,
        layout::shown_text(
            layout::cut_text(offered.title, layout::question_title.width, regular_width(fonts))
        ),
        layout::question_title,
        kit::Align::left,
        kit::rgb(kit::colour::text)
    );
    const auto small_width = [&fonts](std::string_view text) {
        return dialog_text_width(fonts, DialogFont::small, text);
    };
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(
            layout::cut_text(
                layout::question_version_text(dialog, offered),
                layout::question_version.width,
                small_width
            )
        ),
        layout::question_version,
        kit::Align::left,
        kit::rgb(kit::colour::hint)
    );
    const auto lines = layout::question_text_lines(dialog, small_width);
    // The note's lines, the last ones, are drawn in the lock's colour.
    kit::WrapRules note_rules;
    note_rules.shorten_word = [&](std::string_view word) {
        return layout::cut_text(word, layout::question_first_line.width, small_width);
    };
    const std::size_t note_lines = offered.has_profile || roll_back
                                       ? 0
                                       : kit::wrap(
                                             layout::switch_no_profile_text,
                                             layout::question_first_line.width,
                                             small_width,
                                             note_rules
                                         )
                                             .size();
    for (std::size_t line = 0; line < lines.size(); ++line) {
        SourceRect rect = layout::question_first_line;
        rect.y += static_cast<int32_t>(line) * rect.height;
        const bool note = line + note_lines >= lines.size();
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            kit::FontRole::small,
            layout::shown_text(lines[line]),
            rect,
            kit::Align::left,
            note ? kit::rgb(kit::colour::lock) : kit::rgb(kit::colour::text)
        );
    }
    const std::array<std::pair<int32_t, std::string_view>, 2> buttons{{
        {question_no_control, layout::no_text},
        {question_yes_control, roll_back ? layout::roll_back_text : layout::yes_text},
    }};
    for (const auto& [control, caption] : buttons) {
        const bool yes = control == question_yes_control;
        const SourceRect button =
            yes ? layout::question_yes_rect(dialog) : layout::question_no_rect(dialog);
        const bool held = dialog.pressed == control && dialog.hovered == control;
        const bool hovered = dialog.hovered == control;
        kit::draw_button(
            drawn(target, placement, fonts),
            button,
            {std::string(layout::shown_text(caption)),
             yes ? kit::ButtonStyle::accent : kit::ButtonStyle::plain,
             hovered,
             held,
             true}
        );
        if (yes != dialog.question_marks_no)
            kit::draw_focus_ring(drawn(target, placement, fonts), button, true);
    }
}

} // namespace

DialogFonts load_dialog_fonts(oa::AssetStore& assets) {
    return kit::load_game_fonts(assets);
}

int32_t dialog_text_width(const DialogFonts& fonts, DialogFont font, std::string_view text) {
    return kit::text_width(fonts, font, text);
}

void draw_dialog(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts,
    const renderer::RgbaPicture& icon
) {
    const SourceRect whole{0, 0, dialog_width, dialog_height};
    renderer::fill_source_rect(target, placement, whole, kit::rgb(kit::colour::panel));
    draw_header(target, placement, dialog, fonts, icon);
    draw_list(target, placement, dialog, fonts);
    draw_section(target, placement, dialog, fonts);
    draw_footer(target, placement, dialog, fonts);
    renderer::draw_bevel(
        target,
        placement,
        whole,
        kit::rgb(kit::colour::edge_light),
        kit::rgb(kit::colour::edge_dark)
    );
    // An open drop-down list lies over everything else.
    if (dialog.open_list != no_control) {
        const layout::ScrolledRows open = layout::open_rows(dialog);
        for (const layout::Row& row : open.rows.rows)
            if (row.control == dialog.open_list && layout::is_choice(row.setting) &&
                row.lock == Lock::none)
                paint_open_menu(target, placement, dialog, row.control_area, row.setting, fonts);
    }
    // The question lies over all of it.
    if (dialog.switch_question != no_question)
        draw_question(target, placement, dialog, fonts);
}

namespace {

/// Draws a notice's or a prompt's box, header, text and footer band: the
/// panel, the header with the icon (or the OA mark) and the title, the text
/// with paths in the regular font and the failure in amber, and the footer.
///
/// @param[in,out] target the surface
/// @param placement where the box's top left corner lands
/// @param height the box's height
/// @param footer_rule the row of the line over the footer
/// @param title the title
/// @param title_rect the title's place
/// @param lines the text's lines that fit
/// @param fonts the fonts
/// @param icon the Open Annihilation icon; an empty picture draws the OA mark
/// @param look_up look the title and text up in the interface catalogue
void draw_notice_box(
    renderer::Surface& target,
    const renderer::Placement& placement,
    int32_t height,
    int32_t footer_rule,
    std::string_view title,
    const SourceRect& title_rect,
    const std::vector<notice_geometry::Line>& lines,
    const DialogFonts& fonts,
    const renderer::RgbaPicture& icon,
    bool look_up
) {
    namespace place = notice_geometry;
    const SourceRect whole{0, 0, notice_width, height};
    const int32_t inner = notice_width - 2 * place::edge;
    renderer::fill_source_rect(target, placement, whole, kit::rgb(kit::colour::panel));
    // The header: the icon, or the OA mark, and the title.
    renderer::fill_source_rect(
        target,
        placement,
        {place::edge, place::edge, inner, place::header_height},
        kit::rgb(kit::colour::band)
    );
    renderer::fill_source_rect(
        target,
        placement,
        {place::edge, place::header_rule_row, inner, 1},
        kit::rgb(kit::colour::rule)
    );
    kit::draw_header_mark(drawn(target, placement, fonts, icon), place::icon);
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::regular,
        (look_up ? layout::shown_text(title) : title),
        title_rect,
        kit::Align::left,
        kit::rgb(kit::colour::text),
        place::title_tracking
    );
    // The text: paths in the regular font, the rest in the small one, the
    // failure in amber.
    for (const auto& line : lines)
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            (line.path ? kit::FontRole::regular : kit::FontRole::small),
            (look_up ? layout::shown_text(line.text) : line.text),
            line.rect,
            kit::Align::left,
            line.failure ? kit::rgb(kit::colour::lock) : kit::rgb(kit::colour::text)
        );
    renderer::fill_source_rect(
        target, placement, {place::edge, footer_rule, inner, 1}, kit::rgb(kit::colour::rule)
    );
    renderer::fill_source_rect(
        target,
        placement,
        {place::edge, footer_rule + 1, inner, place::footer_height},
        kit::rgb(kit::colour::band)
    );
}

} // namespace

void draw_notice(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Notice& notice,
    const DialogFonts& fonts,
    const renderer::RgbaPicture& icon
) {
    const auto placed = notice_geometry::place_notice(
        notice, regular_width(fonts), [&fonts](std::string_view text) {
            return dialog_text_width(fonts, DialogFont::small, text);
        }
    );
    draw_notice_box(
        target,
        placement,
        placed.height,
        placed.footer_rule,
        notice.title,
        placed.title,
        placed.lines,
        fonts,
        icon,
        true
    );
    // The footer: the open button as Cancel looks and OK, the marked one
    // ringed in green.
    const std::array<std::pair<int32_t, std::string_view>, 2> buttons{{
        {notice_open_control, notice.open_caption},
        {notice_ok_control, layout::ok_text},
    }};
    for (const auto& [control, caption] : buttons) {
        const bool ok = control == notice_ok_control;
        const SourceRect button = ok ? placed.ok_button : placed.open_button;
        const bool hovered = notice.hovered == control;
        const bool held = notice.pressed == control && notice.hovered == control;
        kit::draw_button(
            drawn(target, placement, fonts),
            button,
            {std::string(layout::shown_text(caption)),
             ok ? kit::ButtonStyle::accent : kit::ButtonStyle::plain,
             hovered,
             held,
             true}
        );
        if (notice.marked == control)
            kit::draw_focus_ring(drawn(target, placement, fonts), button, true);
    }
    renderer::draw_bevel(
        target,
        placement,
        {0, 0, notice_width, placed.height},
        kit::rgb(kit::colour::edge_light),
        kit::rgb(kit::colour::edge_dark)
    );
}

void draw_prompt(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Prompt& prompt,
    const DialogFonts& fonts,
    const renderer::RgbaPicture& icon
) {
    const auto placed = notice_geometry::place_prompt(
        prompt, regular_width(fonts), [&fonts](std::string_view text) {
            return dialog_text_width(fonts, DialogFont::small, text);
        }
    );
    draw_notice_box(
        target,
        placement,
        placed.height,
        placed.footer_rule,
        prompt.title,
        placed.title,
        placed.lines,
        fonts,
        icon,
        false
    );
    // The progress bar: a well, filled in the accent colour as far as it came.
    if (placed.bar.width > 0) {
        renderer::fill_source_rect(target, placement, placed.bar, kit::rgb(kit::colour::well));
        renderer::draw_outline(
            target, placement, placed.bar, kit::rgb(kit::colour::control_border)
        );
        const int32_t inner = placed.bar.width - 2;
        const int32_t filled = static_cast<int32_t>(
            int64_t{inner} * std::clamp(prompt.progress, 0, prompt_progress_whole) /
            prompt_progress_whole
        );
        if (filled > 0)
            renderer::fill_source_rect(
                target,
                placement,
                {placed.bar.x + 1, placed.bar.y + 1, filled, placed.bar.height - 2},
                kit::rgb(kit::colour::accent)
            );
    }
    for (std::size_t index = 0; index < placed.buttons.size(); ++index) {
        const auto control = static_cast<int32_t>(index);
        const bool hovered = prompt.hovered == control;
        const bool held = prompt.pressed == control && prompt.hovered == control;
        kit::draw_button(
            drawn(target, placement, fonts),
            placed.buttons[index],
            {std::string(prompt.buttons[index].caption),
             prompt.buttons[index].accent ? kit::ButtonStyle::accent : kit::ButtonStyle::plain,
             hovered,
             held,
             true}
        );
        if (prompt.marked == control)
            kit::draw_focus_ring(drawn(target, placement, fonts), placed.buttons[index], true);
    }
    renderer::draw_bevel(
        target,
        placement,
        {0, 0, notice_width, placed.height},
        kit::rgb(kit::colour::edge_light),
        kit::rgb(kit::colour::edge_dark)
    );
}

void draw_oa_button(
    renderer::Surface& target,
    const renderer::Placement& placement,
    int32_t side,
    ButtonLook look,
    const DialogFonts& fonts,
    const renderer::RgbaPicture& icon
) {
    kit::draw_oa_button(
        {&target, placement, &fonts, icon}, side, static_cast<kit::OaButtonState>(look)
    );
}

void draw_oa_mark(
    renderer::Surface& target,
    const renderer::Placement& placement,
    int32_t side,
    const renderer::RgbaPicture& icon
) {
    kit::draw_oa_mark({&target, placement, nullptr, icon}, side);
}

} // namespace oa::ui::engine_settings
