// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How the settings dialog and the OA button are drawn, and the fonts they
// draw their texts in: a dark gunmetal panel with one-pixel raised edges,
// hairline rules, light text with muted hints, and one green accent for
// what is selected. The header and the button show the Open Annihilation
// icon, or the green OA mark when the host has no icon to give.

#include "oa/ui/engine_settings/dialog.hpp"
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

/// The columns between a control and its keyboard focus outline.
constexpr int32_t kFocusInset = layout::focus_inset;

/// Returns a rectangle grown on every side.
///
/// @param rect the rectangle
/// @param by the columns and rows added on each side
/// @return the grown rectangle
SourceRect grown(const SourceRect& rect, int32_t by) noexcept {
    return {rect.x - by, rect.y - by, rect.width + 2 * by, rect.height + 2 * by};
}

/// The OA mark's letters, 4 by 7 each with a column between: for the
/// header and the in-game button.
constexpr int32_t kSmallMarkWidth = 9;
/// The small mark's height.
constexpr int32_t kSmallMarkHeight = 7;
/// The small mark, row by row.
constexpr std::array<uint8_t, kSmallMarkWidth * kSmallMarkHeight> kSmallMarkBits{
    0, 1, 1, 0, 0, 0, 1, 1, 0, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 1, 1, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    1, 0, 0, 1, 0, 1, 0, 0, 1, //
    0, 1, 1, 0, 0, 1, 0, 0, 1, //
};
/// The OA mark's letters, 5 by 9 each with two columns between: for the
/// main menu's button.
constexpr int32_t kLargeMarkWidth = 12;
/// The large mark's height.
constexpr int32_t kLargeMarkHeight = 9;
/// The large mark, row by row.
constexpr std::array<uint8_t, kLargeMarkWidth * kLargeMarkHeight> kLargeMarkBits{
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
/// The padlock, row by row.
constexpr std::array<uint8_t, layout::padlock_width * layout::padlock_height> kPadlockBits{
    0, 1, 1, 1, 0, //
    1, 0, 0, 0, 1, //
    1, 0, 0, 0, 1, //
    1, 1, 1, 1, 1, //
    1, 1, 0, 1, 1, //
    1, 1, 0, 1, 1, //
    1, 1, 1, 1, 1, //
};

/// The columns and rows between the OA button's sides and its icon: the
/// bevel, one for the outline under the pointer, and one clear.
constexpr int32_t kButtonIconInset = 3;
/// How far a held OA button's icon moves right and down, as if pressed in.
constexpr int32_t kButtonIconPress = 1;

/// The OA button's outlined square, as a share of its side: 20 of 32.
constexpr int32_t kButtonSquareNumerator = 20;
/// The OA button's outlined square's share's denominator.
constexpr int32_t kButtonSquareDenominator = 32;
/// The least columns between the large mark and its square's outline.
constexpr int32_t kLargeMarkMargin = 2;

/// A closed area's or hack's arrow, pointing right, row by row.
constexpr std::array<uint8_t, layout::arrow_side * layout::arrow_side> kClosedArrowBits{
    0, 1, 0, 0, 0, //
    0, 1, 1, 0, 0, //
    0, 1, 1, 1, 0, //
    0, 1, 1, 0, 0, //
    0, 1, 0, 0, 0, //
};
/// An open area's or hack's arrow, pointing down, row by row.
constexpr std::array<uint8_t, layout::arrow_side * layout::arrow_side> kOpenArrowBits{
    0, 0, 0, 0, 0, //
    1, 1, 1, 1, 1, //
    0, 1, 1, 1, 0, //
    0, 0, 1, 0, 0, //
    0, 0, 0, 0, 0, //
};

/// The small OA mark.
constexpr renderer::Mark kSmallMark{kSmallMarkWidth, kSmallMarkHeight, kSmallMarkBits};
/// A closed area's or hack's arrow.
constexpr renderer::Mark kClosedArrow{layout::arrow_side, layout::arrow_side, kClosedArrowBits};
/// An open area's or hack's arrow.
constexpr renderer::Mark kOpenArrow{layout::arrow_side, layout::arrow_side, kOpenArrowBits};
/// The large OA mark.
constexpr renderer::Mark kLargeMark{kLargeMarkWidth, kLargeMarkHeight, kLargeMarkBits};
/// The padlock.
constexpr renderer::Mark kPadlock{layout::padlock_width, layout::padlock_height, kPadlockBits};
/// A drop-down's arrow, pointing down, row by row.
constexpr std::array<uint8_t, layout::choice_arrow_width * layout::choice_arrow_height>
    kChoiceArrowBits{
        1, 1, 1, 1, 1, 1, 1, //
        0, 1, 1, 1, 1, 1, 0, //
        0, 0, 1, 1, 1, 0, 0, //
        0, 0, 0, 1, 0, 0, 0, //
};
/// A drop-down's arrow.
constexpr renderer::Mark kChoiceArrow{
    layout::choice_arrow_width, layout::choice_arrow_height, kChoiceArrowBits
};

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
    if (renderer::picture_drawable(icon)) {
        renderer::draw_picture(target, placement, mark, icon);
    } else {
        const SourceRect square{
            mark.x + (mark.width - layout::header_mark_square) / 2,
            mark.y + (mark.height - layout::header_mark_square) / 2,
            layout::header_mark_square,
            layout::header_mark_square,
        };
        renderer::draw_outline(target, placement, square, kit::rgb(kit::colour::accent));
        renderer::draw_mark(
            target,
            placement,
            kSmallMark,
            square.x + (square.width - kSmallMarkWidth) / 2,
            square.y + (square.height - kSmallMarkHeight + 1) / 2,
            kit::rgb(kit::colour::accent)
        );
    }
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
            renderer::draw_outline(target, placement, item, kit::rgb(kit::colour::accent));
    }
}

/// Draws an Off/On switch.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the switch
/// @param on the switch is On
/// @param hovered the pointer is over it
/// @param locked the switch cannot be changed now: On shows without the accent
/// @param fonts the fonts
void draw_switch(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    bool on,
    bool hovered,
    bool locked,
    const DialogFonts& fonts
) {
    renderer::fill_source_rect(target, placement, area, kit::rgb(kit::colour::well));
    renderer::draw_outline(
        target,
        placement,
        area,
        hovered ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
    );
    const int32_t half = (area.width - 2) / 2;
    const SourceRect off{area.x + 1, area.y + 1, half, area.height - 2};
    const SourceRect on_half{area.x + 1 + half, area.y + 1, half, area.height - 2};
    if (on)
        renderer::fill_source_rect(
            target,
            placement,
            on_half,
            locked ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::accent)
        );
    else
        renderer::fill_source_rect(target, placement, off, kit::rgb(kit::colour::off_selected));
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(layout::off_text),
        off,
        kit::Align::centre,
        on ? kit::rgb(kit::colour::switch_idle) : kit::rgb(kit::colour::text)
    );
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(layout::on_text),
        on_half,
        kit::Align::centre,
        on ? kit::rgb(kit::colour::on_accent) : kit::rgb(kit::colour::switch_idle)
    );
}

/// Draws a strip of levels, such as Enhanced anti-aliasing's or Hardware
/// acceleration's. The levels the strip shows but does not offer are faded
/// into the panel, as a locked row is.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the strip
/// @param setting the strip's setting
/// @param level the index of the level chosen
/// @param hovered the pointer is over it
/// @param locked the strip cannot be changed now: the level chosen shows
///     without the accent
/// @param fonts the fonts
void draw_levels(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    Setting setting,
    std::size_t level,
    bool hovered,
    bool locked,
    const DialogFonts& fonts
) {
    renderer::fill_source_rect(target, placement, area, kit::rgb(kit::colour::well));
    renderer::draw_outline(
        target,
        placement,
        area,
        hovered ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
    );
    const layout::Strip strip = layout::strip_of(setting);
    for (std::size_t index = 0; index < strip.levels; ++index) {
        const SourceRect segment{
            area.x + 1 + static_cast<int32_t>(index) * strip.level_width,
            area.y + 1,
            strip.level_width,
            area.height - 2,
        };
        const bool selected = index == level;
        if (selected)
            renderer::fill_source_rect(
                target,
                placement,
                segment,
                locked ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::accent)
            );
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            kit::FontRole::small,
            layout::shown_text(layout::strip_caption(setting, index)),
            segment,
            kit::Align::centre,
            selected ? kit::rgb(kit::colour::on_accent) : kit::rgb(kit::colour::button_text)
        );
        if (index >= layout::offered_levels(strip))
            renderer::blend_source_rect(
                target, placement, segment, kit::rgb(kit::colour::panel), kit::locked_fade
            );
    }
}

/// Draws a slider: its track filled up to the knob, its stops and its knob.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the slider's track area
/// @param stop the stop the knob is on
/// @param stops the slider's stops
/// @param locked the slider cannot be moved now
/// @param hovered the pointer is over it, or holds it
void draw_slider(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    int32_t stop,
    int32_t stops,
    bool locked,
    bool hovered
) {
    const SourceRect track{area.x, area.y + layout::track_offset, area.width, layout::track_height};
    renderer::fill_source_rect(target, placement, track, kit::rgb(kit::colour::well));
    renderer::draw_outline(target, placement, track, kit::rgb(kit::colour::control_border));
    const int32_t knob = layout::knob_column(area, stop, stops);
    renderer::fill_source_rect(
        target,
        placement,
        {track.x + 1, track.y + 1, knob - track.x - 1, track.height - 2},
        locked ? kit::rgb(kit::colour::control_border) : kit::rgb(kit::colour::accent)
    );
    const int32_t travel = area.width - layout::knob_width;
    if (stops > 1 && travel / (stops - 1) >= layout::least_stop_spacing) {
        for (int32_t index = 0; index < stops; ++index) {
            renderer::fill_source_rect(
                target,
                placement,
                {layout::knob_column(area, index, stops),
                 area.y + layout::stop_offset,
                 1,
                 layout::stop_height},
                kit::rgb(kit::colour::control_border)
            );
        }
    }
    const SourceRect knob_rect{
        knob - layout::knob_width / 2, area.y, layout::knob_width, layout::knob_height
    };
    Rgb face = kit::rgb(kit::colour::accent);
    if (locked)
        face = kit::rgb(kit::colour::control_hover);
    else if (hovered)
        face = kit::rgb(kit::colour::accent_light);
    renderer::fill_source_rect(target, placement, knob_rect, face);
    renderer::draw_outline(target, placement, knob_rect, kit::rgb(kit::colour::on_accent));
}

/// Draws a button that asks the host to act, as OK is drawn: the accent's
/// face, lighter under the pointer and darker while held, with its caption
/// in the small font.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the button
/// @param caption its caption
/// @param hovered the pointer is over it
/// @param held a press on it is held
/// @param fonts the fonts
void draw_action_button(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    std::string_view caption,
    bool hovered,
    bool held,
    const DialogFonts& fonts
) {
    Rgb face = kit::rgb(kit::colour::accent);
    if (held)
        face = kit::rgb(kit::colour::accent_held);
    else if (hovered)
        face = kit::rgb(kit::colour::accent_light);
    renderer::fill_source_rect(target, placement, area, face);
    renderer::draw_outline(target, placement, area, kit::rgb(kit::colour::accent_light));
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(caption),
        area,
        kit::Align::centre,
        kit::rgb(kit::colour::on_accent)
    );
}

/// Returns a placement that draws only inside a rectangle, and inside the
/// placement's own clip when it has one.
///
/// @param placement the placement
/// @param rect the rectangle, in source pixels
/// @return the placement, clipped
renderer::Placement clipped_to(const renderer::Placement& placement, const SourceRect& rect) {
    renderer::Placement clipped = placement;
    const SourceRect& outer = placement.clip;
    if (outer.width <= 0 || outer.height <= 0) {
        clipped.clip = rect;
        return clipped;
    }
    const int32_t left = std::max(rect.x, outer.x);
    const int32_t top = std::max(rect.y, outer.y);
    const int32_t right = std::min(rect.x + rect.width, outer.x + outer.width);
    const int32_t bottom = std::min(rect.y + rect.height, outer.y + outer.height);
    clipped.clip = {left, top, std::max(right - left, 0), std::max(bottom - top, 0)};
    // Where the rectangles do not meet nothing is drawn: an empty clip would
    // clip to the surface alone, and a scale of 0 draws nothing.
    if (clipped.clip.width == 0 || clipped.clip.height == 0)
        clipped.scale = 0;
    return clipped;
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

/// Draws a drop-down's field: a well like a switch's with the choice at
/// its left and the arrow at its right.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the field
/// @param text the choice, in UTF-8
/// @param hovered the pointer is over it, or holds it
/// @param open its list is open
/// @param fonts the fonts
void draw_choice(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    std::string_view text,
    bool hovered,
    bool open,
    const DialogFonts& fonts
) {
    renderer::fill_source_rect(target, placement, area, kit::rgb(kit::colour::well));
    Rgb border = kit::rgb(kit::colour::control_border);
    if (open)
        border = kit::rgb(kit::colour::accent);
    else if (hovered)
        border = kit::rgb(kit::colour::control_hover);
    renderer::draw_outline(target, placement, area, border);
    const SourceRect text_box{
        area.x + layout::choice_text_inset,
        area.y,
        area.width - layout::choice_text_inset - layout::choice_arrow_room,
        area.height,
    };
    // A choice wider than its room is cut at the room's edge.
    kit::draw_boxed_text(
        target,
        clipped_to(placement, text_box),
        fonts,
        kit::FontRole::regular,
        layout::shown_text(text),
        text_box,
        kit::Align::left,
        kit::rgb(kit::colour::text)
    );
    renderer::draw_mark(
        target,
        placement,
        kChoiceArrow,
        area.x + area.width - layout::choice_arrow_room +
            (layout::choice_arrow_room - layout::choice_arrow_width) / 2,
        area.y + (area.height - layout::choice_arrow_height) / 2,
        open ? kit::rgb(kit::colour::accent) : kit::rgb(kit::colour::button_text)
    );
}

/// Draws an open drop-down list over the dialog: its items, the chosen one
/// marked as the section list marks its open section, the item the pointer
/// or the keys mark lit, and a thumb at its right edge when it holds more
/// items than it shows.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param field the drop-down's field
/// @param setting the drop-down's setting
/// @param fonts the fonts
void draw_choice_list(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const SourceRect& field,
    Setting setting,
    const DialogFonts& fonts
) {
    const std::size_t choices = layout::choice_count(dialog, setting);
    const SourceRect list = layout::choice_list(field, choices);
    const int32_t shown = layout::shown_choices(choices);
    const auto chosen = static_cast<int32_t>(layout::choice_index(dialog, setting));
    renderer::fill_source_rect(target, placement, list, kit::rgb(kit::colour::list));
    renderer::draw_outline(target, placement, list, kit::rgb(kit::colour::control_hover));
    for (int32_t place = 0; place < shown; ++place) {
        const int32_t item = dialog.list_first + place;
        if (item >= static_cast<int32_t>(choices))
            break;
        const SourceRect box = layout::choice_item(list, place);
        Rgb text = kit::rgb(kit::colour::list_text);
        if (item == chosen) {
            renderer::fill_source_rect(
                target, placement, box, kit::rgb(kit::colour::list_selected)
            );
            renderer::fill_source_rect(
                target,
                placement,
                {box.x + layout::list_marker_offset,
                 box.y + (box.height - layout::list_marker_height) / 2,
                 layout::list_marker_width,
                 layout::list_marker_height},
                kit::rgb(kit::colour::accent)
            );
            text = kit::rgb(kit::colour::text);
        }
        if (item == dialog.list_marked) {
            if (item != chosen)
                renderer::fill_source_rect(target, placement, box, kit::rgb(kit::colour::hover));
            text = kit::rgb(kit::colour::text);
            // The keyboard focus shows round the marked item once a key has
            // shown it.
            if (dialog.focused != no_control)
                renderer::draw_outline(target, placement, box, kit::rgb(kit::colour::accent));
        }
        const SourceRect caption{
            box.x + layout::choice_item_text_inset,
            box.y,
            box.width - layout::choice_item_text_inset - layout::list_text_margin,
            box.height,
        };
        kit::draw_boxed_text(
            target,
            clipped_to(placement, caption),
            fonts,
            kit::FontRole::regular,
            layout::shown_text(
                layout::shown_choice_text(
                    dialog,
                    setting,
                    static_cast<std::size_t>(item),
                    layout::choice_item_text_room,
                    regular_width(fonts)
                )
            ),
            caption,
            kit::Align::left,
            text
        );
    }
    if (static_cast<int32_t>(choices) > shown) {
        const int32_t travel = list.height - 2;
        const int32_t height = std::max(travel * shown / static_cast<int32_t>(choices), 4);
        const int32_t top = list.y + 1 +
                            (travel - height) * dialog.list_first /
                                std::max(static_cast<int32_t>(choices) - shown, 1);
        renderer::fill_source_rect(
            target,
            placement,
            {list.x + list.width - 3, top, 2, height},
            kit::rgb(kit::colour::control_hover)
        );
    }
}

/// Draws the padlock and a lock's text, ending at the lock area's right.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param area the lock area
/// @param lock the lock
/// @param fonts the fonts
void draw_lock(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& area,
    Lock lock,
    const DialogFonts& fonts
) {
    const std::string_view text = layout::shown_text(layout::lock_text(lock));
    const int32_t text_width = kit::text_width(fonts, kit::FontRole::small, text);
    const int32_t left =
        area.x + area.width - text_width - layout::padlock_gap - layout::padlock_width;
    renderer::draw_mark(
        target,
        placement,
        kPadlock,
        left,
        area.y + (area.height - layout::padlock_height) / 2,
        kit::rgb(kit::colour::lock)
    );
    const SourceRect text_area{
        left + layout::padlock_width + layout::padlock_gap, area.y, text_width, area.height
    };
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(text),
        text_area,
        kit::Align::left,
        kit::rgb(kit::colour::lock)
    );
}

/// Draws the open section's scroll bar: a well like a switch's, its thumb
/// in its inner columns, both lighter under the pointer or while held.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands
/// @param dialog the dialog
/// @param open the open section's rows
void draw_scroll_bar(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const Dialog& dialog,
    const layout::ScrolledRows& open
) {
    const bool hot = dialog.hovered == scroll_bar_control || dialog.pressed == scroll_bar_control;
    renderer::fill_source_rect(target, placement, open.area.well, kit::rgb(kit::colour::well));
    renderer::draw_outline(
        target,
        placement,
        open.area.well,
        hot ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
    );
    renderer::fill_source_rect(
        target,
        placement,
        layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height),
        hot ? kit::rgb(kit::colour::switch_idle) : kit::rgb(kit::colour::control_hover)
    );
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
        renderer::draw_mark(
            target,
            in_view,
            row.open ? kOpenArrow : kClosedArrow,
            row.arrow.x,
            row.arrow.y,
            hovered ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::hint)
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
        renderer::draw_mark(
            target,
            in_view,
            row.open ? kOpenArrow : kClosedArrow,
            row.arrow.x,
            row.arrow.y,
            hovered ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::hint)
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
        draw_switch(target, in_view, row.toggle, row.on, hovered && !row.locked, row.locked, fonts);
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
        draw_switch(target, in_view, row.control_area, row.on, hovered, row.locked, fonts);
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
        draw_slider(target, in_view, row.control_area, row.stop, row.stops, row.locked, hovered);
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
    if (header)
        renderer::draw_outline(target, in_view, row.control_area, kit::rgb(kit::colour::accent));
    else
        renderer::draw_outline(
            target, in_view, grown(row.control_area, kFocusInset), kit::rgb(kit::colour::accent)
        );
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
    const renderer::Placement in_view = clipped_to(placement, layout::developer_view_clip);
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
    if (open.limit > 0)
        draw_scroll_bar(target, placement, dialog, open);

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
    draw_switch(
        target,
        placement,
        layout::active_only_switch,
        dialog.developer.active_only,
        hot(active_only_control),
        false,
        fonts
    );
    if (dialog.focused == active_only_control)
        renderer::draw_outline(
            target,
            placement,
            grown(layout::active_only_switch, kFocusInset),
            kit::rgb(kit::colour::accent)
        );
    // Restore profile values takes a press only while Developer Mode is on.
    const SourceRect button = layout::restore_profile_button;
    const bool enabled = dialog.chosen.developer_mode;
    const bool button_hot = enabled && hot(restore_profile_control);
    if (button_hot)
        renderer::fill_source_rect(target, placement, button, kit::rgb(kit::colour::hover));
    renderer::draw_outline(
        target,
        placement,
        button,
        button_hot ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
    );
    Rgb caption = kit::rgb(kit::colour::switch_idle);
    if (enabled)
        caption = button_hot ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::button_text);
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(layout::restore_profile_text),
        button,
        kit::Align::centre,
        caption
    );
    if (enabled && dialog.focused == restore_profile_control)
        renderer::draw_outline(
            target, placement, grown(button, kFocusInset), kit::rgb(kit::colour::accent)
        );
}

/// Draws Your files' buttons as Cancel looks: the button under the pointer
/// lit, and held while a press on it is held.
///
/// @param[in,out] target the surface
/// @param placement where the dialog lands, clipped to the view
/// @param row the row
/// @param dialog the dialog
/// @param fonts the fonts
void draw_folder_buttons(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const layout::Row& row,
    const Dialog& dialog,
    const DialogFonts& fonts
) {
    const bool over_row = dialog.hovered == row.control;
    for (std::size_t index = 0; index < folder_button_count; ++index) {
        const SourceRect button = layout::folder_button(row.control_area, index);
        const bool hovered = over_row && dialog.folder_hovered == index;
        const bool held = hovered && dialog.pressed == row.control;
        if (held || hovered)
            renderer::fill_source_rect(
                target,
                placement,
                button,
                held ? kit::rgb(kit::colour::band) : kit::rgb(kit::colour::hover)
            );
        renderer::draw_outline(
            target,
            placement,
            button,
            hovered ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
        );
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            kit::FontRole::small,
            layout::shown_text(layout::folder_button_text(index)),
            button,
            kit::Align::centre,
            hovered ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::button_text)
        );
    }
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
        renderer::draw_mark(
            target,
            placement,
            kSmallMark,
            rect.x + (rect.width - kSmallMarkWidth) / 2,
            rect.y + (rect.height - kSmallMarkHeight + 1) / 2,
            kit::rgb(kit::colour::accent)
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
        renderer::draw_mark(
            target,
            placement,
            kPadlock,
            layout::mods_lock_line.x,
            layout::mods_lock_text.y + (layout::mods_lock_text.height - layout::padlock_height) / 2,
            kit::rgb(kit::colour::lock)
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
    const renderer::Placement in_view = clipped_to(
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
            const bool lit = !locked && (dialog.hovered == control || dialog.pressed == control);
            renderer::fill_source_rect(
                target,
                in_view,
                roll_back_box,
                lit ? (dialog.pressed == control ? kit::rgb(kit::colour::band)
                                                 : kit::rgb(kit::colour::hover))
                    : kit::rgb(kit::colour::list)
            );
            renderer::draw_outline(
                target,
                in_view,
                roll_back_box,
                lit ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
            );
            kit::draw_boxed_text(
                target,
                in_view,
                fonts,
                kit::FontRole::small,
                layout::shown_text(layout::roll_back_text),
                roll_back_box,
                kit::Align::centre,
                lit ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::button_text)
            );
            if (locked)
                renderer::blend_source_rect(
                    target, in_view, roll_back_box, kit::rgb(kit::colour::panel), kit::locked_fade
                );
            if (dialog.focused == control && !locked)
                renderer::draw_outline(
                    target,
                    in_view,
                    grown(roll_back_box, kFocusInset),
                    kit::rgb(kit::colour::accent)
                );
        }
        if (locked && !playing)
            renderer::blend_source_rect(
                target, in_view, box, kit::rgb(kit::colour::panel), kit::locked_fade
            );
        if (dialog.focused == row.control && !locked)
            renderer::draw_outline(
                target, in_view, grown(box, kFocusInset), kit::rgb(kit::colour::accent)
            );
    }
    if (open.limit > 0)
        draw_scroll_bar(target, placement, dialog, open);
    const int32_t control = layout::mods_folder_control(open.rows);
    const SourceRect& button = layout::mods_folder_button;
    const bool hovered = !locked && (dialog.hovered == control || dialog.pressed == control);
    if (hovered)
        renderer::fill_source_rect(
            target,
            placement,
            button,
            dialog.pressed == control ? kit::rgb(kit::colour::band) : kit::rgb(kit::colour::hover)
        );
    renderer::draw_outline(
        target,
        placement,
        button,
        hovered ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
    );
    kit::draw_boxed_text(
        target,
        placement,
        fonts,
        kit::FontRole::small,
        layout::shown_text(layout::shown_text(layout::open_mods_folder_text)),
        button,
        kit::Align::centre,
        hovered ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::button_text)
    );
    if (locked)
        renderer::blend_source_rect(
            target, placement, button, kit::rgb(kit::colour::panel), kit::locked_fade
        );
    if (dialog.focused == control && !locked)
        renderer::draw_outline(
            target, placement, grown(button, kFocusInset), kit::rgb(kit::colour::accent)
        );
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
    const renderer::Placement in_view = clipped_to(placement, layout::view_clip);
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
                    ? clipped_to(in_view, {box.x, layout::view.y, box.width, layout::view.height})
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
            draw_action_button(
                target,
                in_view,
                row.control_area,
                layout::manage_text,
                hovered,
                dialog.pressed == row.control && dialog.hovered == row.control,
                fonts
            );
        } else if (layout::is_buttons(row.setting)) {
            draw_folder_buttons(target, in_view, row, dialog, fonts);
        } else if (layout::is_strip(row.setting)) {
            if (row.control_area.width > 0)
                draw_levels(
                    target,
                    in_view,
                    row.control_area,
                    row.setting,
                    layout::strip_level(dialog.chosen, row.setting),
                    hovered,
                    locked,
                    fonts
                );
        } else if (layout::is_slider(row.setting)) {
            draw_slider(
                target,
                in_view,
                row.control_area,
                layout::stop_of(
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
                hovered
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
            draw_choice(
                target,
                in_view,
                row.control_area,
                layout::field_text(
                    dialog, row, layout::choice_field_text_room, regular_width(fonts)
                ),
                hovered,
                dialog.open_list == row.control,
                fonts
            );
        } else if (row.control_area.width > 0) {
            draw_switch(
                target,
                in_view,
                row.control_area,
                layout::switch_on(dialog.chosen, row.setting) || row.lock == Lock::set_by_language,
                hovered,
                locked,
                fonts
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
            draw_lock(target, in_view, row.lock_area, row.lock, fonts);
        }
        // Your files rings the button the keys mark.
        const SourceRect focus_area =
            layout::is_buttons(row.setting)
                ? layout::folder_button(
                      row.control_area, static_cast<std::size_t>(dialog.folder_marked)
                  )
                : row.control_area;
        if (dialog.focused == row.control && !locked)
            renderer::draw_outline(
                target, in_view, grown(focus_area, kFocusInset), kit::rgb(kit::colour::accent)
            );
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
    draw_scroll_bar(target, placement, dialog, open);
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
        if (control == ok_control) {
            Rgb face = kit::rgb(kit::colour::accent);
            if (held)
                face = kit::rgb(kit::colour::accent_held);
            else if (hovered)
                face = kit::rgb(kit::colour::accent_light);
            renderer::fill_source_rect(target, placement, button, face);
            renderer::draw_outline(target, placement, button, kit::rgb(kit::colour::accent_light));
            kit::draw_boxed_text(
                target,
                placement,
                fonts,
                kit::FontRole::small,
                layout::shown_text(caption),
                button,
                kit::Align::centre,
                kit::rgb(kit::colour::on_accent)
            );
        } else {
            if (held || hovered)
                renderer::fill_source_rect(target, placement, button, kit::rgb(kit::colour::hover));
            renderer::draw_outline(
                target,
                placement,
                button,
                hovered ? kit::rgb(kit::colour::control_hover)
                        : kit::rgb(kit::colour::control_border)
            );
            kit::draw_boxed_text(
                target,
                placement,
                fonts,
                kit::FontRole::small,
                layout::shown_text(caption),
                button,
                kit::Align::centre,
                hovered ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::button_text)
            );
        }
        if (dialog.focused == control)
            renderer::draw_outline(
                target, placement, grown(button, kFocusInset), kit::rgb(kit::colour::accent)
            );
    }
}

/// Draws the OA mark in a square whose top left corner is source pixel
/// (0, 0): letters in an outlined square of 20/32 of its side, in its
/// middle, the large letters where they fit with room round them.
///
/// @param[in,out] target the surface
/// @param placement where the square lands, and its scale
/// @param side the square's side, in source pixels
/// @param accent the outline's and the letters' colour
void draw_mark_square(
    renderer::Surface& target, const renderer::Placement& placement, int32_t side, Rgb accent
) {
    const int32_t square_side = side * kButtonSquareNumerator / kButtonSquareDenominator;
    const int32_t square_offset = (side - square_side) / 2;
    renderer::draw_outline(
        target, placement, {square_offset, square_offset, square_side, square_side}, accent
    );
    const renderer::Mark& mark =
        square_side - 2 >= kLargeMarkWidth + 2 * kLargeMarkMargin ? kLargeMark : kSmallMark;
    renderer::draw_mark(
        target,
        placement,
        mark,
        square_offset + (square_side - mark.width) / 2,
        square_offset + (square_side - mark.height + 1) / 2,
        accent
    );
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
        if (yes) {
            Rgb face = kit::rgb(kit::colour::accent);
            if (held)
                face = kit::rgb(kit::colour::accent_held);
            else if (hovered)
                face = kit::rgb(kit::colour::accent_light);
            renderer::fill_source_rect(target, placement, button, face);
            renderer::draw_outline(target, placement, button, kit::rgb(kit::colour::accent_light));
            kit::draw_boxed_text(
                target,
                placement,
                fonts,
                kit::FontRole::small,
                layout::shown_text(caption),
                button,
                kit::Align::centre,
                kit::rgb(kit::colour::on_accent)
            );
        } else {
            if (held || hovered)
                renderer::fill_source_rect(target, placement, button, kit::rgb(kit::colour::hover));
            renderer::draw_outline(
                target,
                placement,
                button,
                hovered ? kit::rgb(kit::colour::control_hover)
                        : kit::rgb(kit::colour::control_border)
            );
            kit::draw_boxed_text(
                target,
                placement,
                fonts,
                kit::FontRole::small,
                layout::shown_text(caption),
                button,
                kit::Align::centre,
                hovered ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::button_text)
            );
        }
        if (yes != dialog.question_marks_no)
            renderer::draw_outline(
                target, placement, grown(button, kFocusInset), kit::rgb(kit::colour::accent)
            );
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
                draw_choice_list(target, placement, dialog, row.control_area, row.setting, fonts);
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
    if (renderer::picture_drawable(icon)) {
        renderer::draw_picture(target, placement, place::icon, icon);
    } else {
        const SourceRect square{
            place::icon.x + (place::icon.width - layout::header_mark_square) / 2,
            place::icon.y + (place::icon.height - layout::header_mark_square) / 2,
            layout::header_mark_square,
            layout::header_mark_square,
        };
        renderer::draw_outline(target, placement, square, kit::rgb(kit::colour::accent));
        renderer::draw_mark(
            target,
            placement,
            kSmallMark,
            square.x + (square.width - kSmallMarkWidth) / 2,
            square.y + (square.height - kSmallMarkHeight + 1) / 2,
            kit::rgb(kit::colour::accent)
        );
    }
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

/// Draws a button of a notice or a prompt: an accent one as OK looks, the
/// others as Cancel, ringed in green when marked.
///
/// @param[in,out] target the surface
/// @param placement where the box's top left corner lands
/// @param button the button's place
/// @param caption its caption
/// @param accent drawn as OK is
/// @param hovered the pointer is over it
/// @param held a press on it is held
/// @param marked the keys mark it
/// @param fonts the fonts
/// @param look_up look the caption up in the interface catalogue
void draw_notice_button(
    renderer::Surface& target,
    const renderer::Placement& placement,
    const SourceRect& button,
    std::string_view caption,
    bool accent,
    bool hovered,
    bool held,
    bool marked,
    const DialogFonts& fonts,
    bool look_up
) {
    if (accent) {
        Rgb face = kit::rgb(kit::colour::accent);
        if (held)
            face = kit::rgb(kit::colour::accent_held);
        else if (hovered)
            face = kit::rgb(kit::colour::accent_light);
        renderer::fill_source_rect(target, placement, button, face);
        renderer::draw_outline(target, placement, button, kit::rgb(kit::colour::accent_light));
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            kit::FontRole::small,
            (look_up ? layout::shown_text(caption) : caption),
            button,
            kit::Align::centre,
            kit::rgb(kit::colour::on_accent)
        );
    } else {
        if (held || hovered)
            renderer::fill_source_rect(target, placement, button, kit::rgb(kit::colour::hover));
        renderer::draw_outline(
            target,
            placement,
            button,
            hovered ? kit::rgb(kit::colour::control_hover) : kit::rgb(kit::colour::control_border)
        );
        kit::draw_boxed_text(
            target,
            placement,
            fonts,
            kit::FontRole::small,
            (look_up ? layout::shown_text(caption) : caption),
            button,
            kit::Align::centre,
            hovered ? kit::rgb(kit::colour::text) : kit::rgb(kit::colour::button_text)
        );
    }
    if (marked)
        renderer::draw_outline(
            target, placement, grown(button, kFocusInset), kit::rgb(kit::colour::accent)
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
        draw_notice_button(
            target,
            placement,
            ok ? placed.ok_button : placed.open_button,
            caption,
            ok,
            notice.hovered == control,
            notice.pressed == control && notice.hovered == control,
            notice.marked == control,
            fonts,
            true
        );
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
        draw_notice_button(
            target,
            placement,
            placed.buttons[index],
            prompt.buttons[index].caption,
            prompt.buttons[index].accent,
            prompt.hovered == control,
            prompt.pressed == control && prompt.hovered == control,
            prompt.marked == control,
            fonts,
            false
        );
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
    const DialogFonts&,
    const renderer::RgbaPicture& icon
) {
    const SourceRect whole{0, 0, side, side};
    Rgb face = kit::rgb(kit::colour::panel);
    if (look == ButtonLook::hovered)
        face = kit::rgb(kit::colour::hover);
    else if (look == ButtonLook::pressed)
        face = kit::rgb(kit::colour::band);
    renderer::fill_source_rect(target, placement, whole, face);
    if (look == ButtonLook::pressed)
        renderer::draw_bevel(
            target,
            placement,
            whole,
            kit::rgb(kit::colour::edge_dark),
            kit::rgb(kit::colour::edge_light)
        );
    else
        renderer::draw_bevel(
            target,
            placement,
            whole,
            kit::rgb(kit::colour::edge_light),
            kit::rgb(kit::colour::edge_dark)
        );
    if (renderer::picture_drawable(icon)) {
        if (look == ButtonLook::hovered)
            renderer::draw_outline(
                target, placement, grown(whole, -1), kit::rgb(kit::colour::accent)
            );
        const int32_t pressed_in = look == ButtonLook::pressed ? kButtonIconPress : 0;
        const int32_t icon_side = side - 2 * kButtonIconInset;
        renderer::draw_picture(
            target,
            placement,
            {kButtonIconInset + pressed_in, kButtonIconInset + pressed_in, icon_side, icon_side},
            icon
        );
        return;
    }
    draw_mark_square(
        target,
        placement,
        side,
        look == ButtonLook::idle ? kit::rgb(kit::colour::accent)
                                 : kit::rgb(kit::colour::accent_light)
    );
}

void draw_oa_mark(
    renderer::Surface& target,
    const renderer::Placement& placement,
    int32_t side,
    const renderer::RgbaPicture& icon
) {
    if (side <= 0)
        return;
    if (renderer::picture_drawable(icon)) {
        renderer::draw_picture(target, placement, {0, 0, side, side}, icon);
        return;
    }
    draw_mark_square(target, placement, side, kit::rgb(kit::colour::accent));
}

} // namespace oa::ui::engine_settings
