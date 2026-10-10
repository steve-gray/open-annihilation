// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How the settings dialog and the OA button are drawn, and the fonts they
// draw their texts in: a dark gunmetal panel with one-pixel raised edges,
// hairline rules, light text with muted hints, and one green accent for
// what is selected. The header and the button show the Open Annihilation
// icon, or the green OA mark when the host has no icon to give. The dialog
// is one display list, its items in the order they are drawn and its
// controls named; draw_dialog paints it.

#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/chrome.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/rows.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"
#include "oa/data/mod_profile/overrides.hpp"
#include "oa/data/mod_profile/registry.hpp"

#include "developer.hpp"
#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings {

namespace renderer = oa::ui::frontend_renderer;
namespace layout = geometry;
namespace kit = oa::ui::kit;

namespace {

using renderer::SourceRect;

/// The word each of the dialog's controls' names starts with.
constexpr std::string_view kNamePrefix = "settings";

/// Returns a control's name: the dialog's word, then the words given.
///
/// @param words the words after it, joined by dots
/// @return the name
std::string named(std::string_view words) {
    return std::string(kNamePrefix) + "." + std::string(words);
}

/// Returns a text in name form: lower case, every character outside a to z,
/// 0 to 9 and hyphens a hyphen, and a dot kept or made a hyphen.
///
/// @param text the text, UTF-8
/// @param keep_dots dots stay, as between a hack's words
/// @return the word or words
std::string word_form(std::string_view text, bool keep_dots) {
    std::string word;
    for (std::size_t at = 0; at < text.size();) {
        const auto byte = static_cast<unsigned char>(text[at]);
        const std::size_t bytes = std::max<std::size_t>(kit::character_bytes(text.substr(at)), 1);
        at += bytes;
        if (byte >= 'A' && byte <= 'Z')
            word += static_cast<char>(byte - 'A' + 'a');
        else if (
            (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' ||
            (keep_dots && byte == '.')
        )
            word += static_cast<char>(byte);
        else
            word += '-';
    }
    return word;
}

/// The pieces of the list the dialog's parts go into: the items in the
/// order they are drawn, and the controls grouped by the order a press
/// tries them.
struct Building {
    kit::DisplayList list;                ///< the items, in drawing order
    std::vector<kit::Control> question;   ///< the question's buttons
    std::vector<kit::Control> menu;       ///< an open drop-down list's items
    std::vector<kit::Control> developer;  ///< Developer's list's rows
    std::vector<kit::Control> footer_own; ///< Show Active Only and Restore profile values
    std::vector<kit::Control> roll_backs; ///< Mods' rows' ROLL BACK buttons
    std::vector<kit::Control> rows;       ///< the open section's rows, or Mods' list
    std::vector<kit::Control> folder;     ///< OPEN MODS FOLDER
    std::vector<kit::Control> scroll;     ///< the scroll bar
    std::vector<kit::Control> footer;     ///< Restore defaults, Cancel and OK
    std::vector<kit::Control> nav;        ///< the sections' entries
    kit::Canvas measure{};                ///< what texts are measured in
    const DialogFonts* fonts{};           ///< the fonts; null to estimate widths
};

/// Returns how wide a text is in one of the dialog's fonts, or at the
/// estimated width without them.
///
/// @param fonts the fonts; null to estimate
/// @param font which of them
/// @return the measure, in source pixels
std::function<int32_t(std::string_view)> width_in(const DialogFonts* fonts, DialogFont font) {
    return [fonts, font](std::string_view text) {
        if (fonts != nullptr)
            return dialog_text_width(*fonts, font, text);
        return static_cast<int32_t>(kit::character_count(text)) * estimated_character_width;
    };
}

/// Appends an item.
///
/// @param[in,out] list the display list
/// @param role what it draws
/// @param rect where
/// @param clip what it is drawn clipped to; empty for the whole dialog
/// @return the item, to fill in
kit::Item&
item(kit::DisplayList& list, kit::Role role, const SourceRect& rect, const SourceRect& clip) {
    kit::Item& added = list.items.emplace_back();
    added.role = role;
    added.rect = rect;
    added.clip = clip;
    return added;
}

/// Appends a flat rectangle.
void fill(
    kit::DisplayList& list, const SourceRect& rect, kit::Colour colour, const SourceRect& clip
) {
    item(list, kit::Role::fill, rect, clip).colour = colour;
}

/// Appends a one-pixel outline.
void outline(
    kit::DisplayList& list, const SourceRect& rect, kit::Colour colour, const SourceRect& clip
) {
    item(list, kit::Role::outline, rect, clip).colour = colour;
}

/// Appends a text in a box.
void text(
    kit::DisplayList& list,
    DialogFont font,
    std::string_view words,
    const SourceRect& box,
    kit::Align align,
    kit::Colour colour,
    const SourceRect& clip
) {
    kit::Item& added = item(list, kit::Role::text, box, clip);
    added.text = std::string(words);
    added.font = font;
    added.align = align;
    added.colour = colour;
}

/// Appends a one-bit mark at a point.
void mark(
    kit::DisplayList& list,
    kit::Mark which,
    kit::Point at,
    kit::Colour colour,
    const SourceRect& clip
) {
    kit::Item& added = item(list, kit::Role::mark, {at.x, at.y, 0, 0}, clip);
    added.colour = colour;
    added.look = kit::MarkLook{which, colour};
}

/// Appends a button.
void button(
    kit::DisplayList& list,
    const SourceRect& rect,
    kit::ButtonLook look,
    int32_t control,
    const SourceRect& clip
) {
    kit::Item& added = item(list, kit::Role::button, rect, clip);
    added.text = look.caption;
    added.control = control;
    added.state.hovered = look.hovered;
    added.state.pressed = look.held;
    added.state.disabled = !look.enabled;
    added.look = std::move(look);
}

/// Appends an Off/On switch.
void toggle(
    kit::DisplayList& list,
    const SourceRect& rect,
    kit::SwitchLook look,
    int32_t control,
    const SourceRect& clip
) {
    kit::Item& added = item(list, kit::Role::toggle, rect, clip);
    added.text = look.on ? look.on_caption : look.off_caption;
    added.control = control;
    added.state.hovered = look.hovered;
    added.state.on = look.on;
    added.state.locked = look.locked;
    added.look = std::move(look);
}

/// Appends the keyboard focus outline.
void focus_ring(
    kit::DisplayList& list, const SourceRect& rect, bool around, const SourceRect& clip
) {
    item(list, kit::Role::focus_ring, rect, clip).look = kit::FocusRingLook{around};
}

/// Appends a locked row's fade over a rectangle.
void fade(kit::DisplayList& list, const SourceRect& rect, const SourceRect& clip) {
    item(list, kit::Role::locked_fade, rect, clip);
}

/// Appends a scroll bar.
void scroll_bar(kit::DisplayList& list, const SourceRect& well, const SourceRect& thumb, bool hot) {
    item(list, kit::Role::scroll_bar, well, {}).look = kit::ScrollBarLook{thumb, hot};
}

/// Returns a control.
///
/// @param id its number
/// @param rect where it lies
/// @param name its automation name
/// @param kind what it is
/// @return the control, to fill in further
kit::Control
control_of(int32_t id, const SourceRect& rect, std::string name, kit::ControlKind kind) {
    kit::Control control;
    control.id = id;
    control.rect = rect;
    control.name = std::move(name);
    control.kind = kind;
    return control;
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

/// The header the dialog shows: its title, the second word, the version and,
/// while a shared game keeps running, the note.
///
/// @param dialog the dialog
/// @return the header's look
kit::HeaderLook dialog_header(const Dialog& dialog) {
    kit::HeaderLook header;
    header.width = dialog_width;
    header.title = std::string(layout::shown_text(layout::title_text));
    header.title_width = layout::title_width;
    header.tracking = layout::heading_tracking;
    header.second = std::string(layout::shown_text(layout::title_suffix_text));
    header.second_width = layout::title_suffix_width;
    header.version = std::string(layout::shown_text(dialog.version));
    header.version_width = layout::version_width;
    if (dialog.locks.shared_game)
        header.note = std::string(layout::shown_text(layout::shared_game_text));
    return header;
}

/// Adds the section list: one item that draws it, and a control for each
/// entry, named for its section.
///
/// @param[in,out] building the list
/// @param dialog the dialog
void add_nav(Building& building, const Dialog& dialog) {
    kit::NavLook nav;
    const int32_t body_height = layout::footer_rule_row - layout::body_top;
    nav.area = {layout::edge, layout::body_top, layout::list_width, body_height};
    nav.rule_column = layout::list_rule_column;
    if (dialog.kind == DialogKind::engine)
        nav.divider = layout::list_divider();
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        const int32_t control = page_control(page);
        kit::NavEntry entry;
        entry.rect = layout::dialog_list_item(dialog, page);
        entry.caption = std::string(layout::shown_text(layout::page_name(page)));
        entry.selected = page == dialog.page;
        entry.hovered = dialog.hovered == control || dialog.pressed == control;
        entry.focused = dialog.focused == control;
        kit::Control added = control_of(
            control,
            entry.rect,
            named("nav." + std::string(layout::page_word(page))),
            kit::ControlKind::tab
        );
        added.checked = entry.selected;
        added.text = entry.caption;
        building.nav.push_back(std::move(added));
        nav.entries.push_back(std::move(entry));
    }
    item(building.list, kit::Role::nav, nav.area, {}).look = std::move(nav);
}

/// Adds a mod's badge: its picture, the OA mark for No Mod, or a blank
/// dashed square when it has none.
///
/// @param[in,out] list the display list
/// @param rect the badge's square
/// @param shown the mod's row's texts and details; null details for No Mod
/// @param no_mod the badge is No Mod's
/// @param clip what it is drawn clipped to
void add_badge(
    kit::DisplayList& list,
    const SourceRect& rect,
    const layout::ModRowText& shown,
    bool no_mod,
    const SourceRect& clip
) {
    if (no_mod) {
        outline(list, rect, kit::colour::accent, clip);
        // The small OA mark is 9 by 7. It is centred in the badge, one row
        // down when the spare rows are odd, as the header's square centres it.
        constexpr int32_t mark_width = 9;
        constexpr int32_t mark_height = 7;
        mark(
            list,
            kit::Mark::oa_small,
            {rect.x + (rect.width - mark_width) / 2, rect.y + (rect.height - mark_height + 1) / 2},
            kit::colour::accent,
            clip
        );
        return;
    }
    if (shown.details != nullptr && shown.details->badge_width > 0) {
        const renderer::RgbaPicture picture{
            shown.details->badge_width, shown.details->badge_height, shown.details->badge_pixels
        };
        if (renderer::picture_drawable(picture)) {
            item(list, kit::Role::picture, rect, clip).look = kit::PictureLook{picture};
            outline(list, rect, kit::colour::control_border, clip);
            return;
        }
    }
    // A blank badge: a dashed square.
    for (int32_t along = 0; along < rect.width; along += 3) {
        const int32_t dash = std::min(2, rect.width - along);
        fill(list, {rect.x + along, rect.y, dash, 1}, kit::colour::control_border, clip);
        fill(
            list,
            {rect.x + along, rect.y + rect.height - 1, dash, 1},
            kit::colour::control_border,
            clip
        );
    }
    for (int32_t along = 0; along < rect.height; along += 3) {
        const int32_t dash = std::min(2, rect.height - along);
        fill(list, {rect.x, rect.y + along, 1, dash}, kit::colour::control_border, clip);
        fill(
            list,
            {rect.x + rect.width - 1, rect.y + along, 1, dash},
            kit::colour::control_border,
            clip
        );
    }
}

/// Returns the words Mods' rows are named by, in their order: no-mod for No
/// Mod, else the folder's last component in name form, a second of the
/// same word with -2 after it and a third with -3.
///
/// @param dialog the dialog
/// @param rows Mods' rows
/// @return one word for each row
std::vector<std::string> mod_words(const Dialog& dialog, const std::vector<ModRow>& rows) {
    std::vector<std::string> words;
    words.reserve(rows.size());
    for (const ModRow& row : rows) {
        std::string word = "no-mod";
        if (row.offered >= 0 && static_cast<std::size_t>(row.offered) < dialog.mod_folders.size()) {
            std::string_view folder = dialog.mod_folders[static_cast<std::size_t>(row.offered)];
            while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\'))
                folder.remove_suffix(1);
            const auto last = folder.find_last_of("/\\");
            word =
                word_form(last == std::string_view::npos ? folder : folder.substr(last + 1), false);
            if (word.empty())
                word = "mod";
        }
        std::string unique = word;
        for (int32_t count = 2; std::find(words.begin(), words.end(), unique) != words.end();
             ++count)
            unique = word + "-" + std::to_string(count);
        words.push_back(std::move(unique));
    }
    return words;
}

/// Adds Mods: the lock line while it is locked, the list of rows in its
/// view with its scroll bar, OPEN MODS FOLDER and the lines under it.
///
/// @param[in,out] building the list
/// @param dialog the dialog
/// @param open Mods' rows
void add_mods(Building& building, const Dialog& dialog, const layout::ScrolledRows& open) {
    kit::DisplayList& list = building.list;
    const bool locked = dialog.locks.mod != Lock::none;
    const auto small_width = width_in(building.fonts, DialogFont::small);
    const auto regular_width = width_in(building.fonts, DialogFont::regular);
    if (locked) {
        mark(
            list,
            kit::Mark::padlock,
            {layout::mods_lock_line.x,
             layout::mods_lock_text.y +
                 (layout::mods_lock_text.height - layout::padlock_height) / 2},
            kit::colour::lock,
            {}
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
            text(
                list,
                DialogFont::small,
                layout::shown_text(lines[line]),
                rect,
                kit::Align::left,
                kit::colour::lock,
                {}
            );
        }
    }
    const SourceRect& view = open.area.view;
    const SourceRect in_view{
        view.x - layout::focus_inset, view.y, view.width + 2 * layout::focus_inset, view.height
    };
    const auto rows = mod_rows(dialog);
    const auto words = mod_words(dialog, rows);
    for (std::size_t index = 0; index < open.rows.rows.size() && index < rows.size(); ++index) {
        const layout::Row& row = open.rows.rows[index];
        const layout::ModRowText shown = layout::mod_row_text(dialog, rows[index]);
        const bool playing = rows[index].playing;
        const bool hovered =
            !locked && (dialog.hovered == row.control || dialog.pressed == row.control);
        const SourceRect& box = row.control_area;
        fill(
            list,
            box,
            playing ? kit::colour::list_selected
                    : (hovered ? kit::colour::hover : kit::colour::list),
            in_view
        );
        outline(
            list,
            box,
            playing ? kit::colour::accent
                    : (hovered ? kit::colour::control_hover : kit::colour::control_border),
            in_view
        );
        const SourceRect badge{
            box.x + layout::mod_row_inset,
            box.y + (box.height - layout::mod_badge_side) / 2,
            layout::mod_badge_side,
            layout::mod_badge_side
        };
        add_badge(list, badge, shown, rows[index].offered == no_mod_row, in_view);
        // The version at the top right; the title, and PLAYING after it,
        // in what is left of the line.
        const int32_t version_width = std::min(small_width(shown.version), row.value.width / 3);
        const SourceRect version{
            row.value.x + row.value.width - version_width,
            row.value.y,
            version_width,
            row.value.height
        };
        text(
            list,
            DialogFont::small,
            layout::shown_text(layout::cut_text(shown.version, version.width, small_width)),
            version,
            kit::Align::right,
            kit::colour::hint,
            in_view
        );
        const std::string_view tag = layout::shown_text(layout::playing_text);
        const int32_t tag_width = playing ? small_width(tag) + 6 : 0;
        const int32_t title_room = version.x - 6 - row.label.x - tag_width;
        const std::string title = layout::cut_text(shown.title, title_room, regular_width);
        const int32_t title_width = regular_width(title);
        text(
            list,
            DialogFont::regular,
            layout::shown_text(title),
            {row.label.x, row.label.y, title_room, row.label.height},
            kit::Align::left,
            kit::colour::text,
            in_view
        );
        if (playing)
            text(
                list,
                DialogFont::small,
                layout::shown_text(tag),
                {row.label.x + title_width + 6, row.value.y, tag_width, row.value.height},
                kit::Align::left,
                kit::colour::accent,
                in_view
            );
        // A row whose folder keeps an earlier version shows ROLL BACK at
        // the right of its description, which is cut shorter for it.
        const bool roll_back = layout::offers_roll_back(dialog, rows[index]);
        const SourceRect roll_back_box = layout::roll_back_button(row);
        SourceRect description = row.hints[0];
        if (roll_back)
            description.width = roll_back_box.x - layout::mod_roll_back_gap - description.x;
        text(
            list,
            DialogFont::small,
            layout::shown_text(layout::cut_text(shown.description, description.width, small_width)),
            description,
            kit::Align::left,
            shown.has_profile ? kit::colour::hint : kit::colour::lock,
            in_view
        );
        if (roll_back) {
            const int32_t control = layout::roll_back_control(open.rows, index);
            const bool over = !locked && (dialog.hovered == control || dialog.pressed == control);
            const bool held = !locked && dialog.pressed == control;
            const std::string caption(layout::shown_text(layout::roll_back_text));
            button(
                list,
                roll_back_box,
                {caption, kit::ButtonStyle::inset, over, held, true},
                locked ? no_control : control,
                in_view
            );
            if (locked)
                fade(list, roll_back_box, in_view);
            if (dialog.focused == control && !locked)
                focus_ring(list, roll_back_box, true, in_view);
            if (!locked) {
                kit::Control added = control_of(
                    control,
                    roll_back_box,
                    named("mod." + words[index] + ".roll-back"),
                    kit::ControlKind::button
                );
                added.clip = view;
                added.group = layout::mods_list_group;
                added.text = caption;
                building.roll_backs.push_back(std::move(added));
            }
        }
        if (locked && !playing)
            fade(list, box, in_view);
        if (dialog.focused == row.control && !locked)
            focus_ring(list, box, true, in_view);
        if (!locked) {
            kit::Control added = control_of(
                row.control,
                box,
                named("mod." + words[index] + ".switch"),
                kit::ControlKind::list_item
            );
            added.clip = view;
            added.group = layout::mods_list_group;
            added.checked = playing;
            added.text = shown.title;
            building.rows.push_back(std::move(added));
        }
    }
    if (open.limit > 0) {
        const bool bar_hot =
            dialog.hovered == scroll_bar_control || dialog.pressed == scroll_bar_control;
        scroll_bar(
            list,
            open.area.well,
            layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height),
            bar_hot
        );
    }
    const int32_t control = layout::mods_folder_control(open.rows);
    const SourceRect& folder_button = layout::mods_folder_button;
    const bool over = !locked && (dialog.hovered == control || dialog.pressed == control);
    const bool held = !locked && dialog.pressed == control;
    const std::string caption(
        layout::shown_text(layout::shown_text(layout::open_mods_folder_text))
    );
    button(
        list,
        folder_button,
        {caption, kit::ButtonStyle::quiet, over, held, true},
        locked ? no_control : control,
        {}
    );
    if (locked)
        fade(list, folder_button, {});
    if (dialog.focused == control && !locked)
        focus_ring(list, folder_button, true, {});
    if (!locked) {
        kit::Control added =
            control_of(control, folder_button, named("open-mods-folder"), kit::ControlKind::button);
        added.text = caption;
        building.folder.push_back(std::move(added));
    }
    text(
        list,
        DialogFont::small,
        layout::shown_text(layout::shown_text(layout::mods_folders_text[0])),
        layout::mods_note_first,
        kit::Align::left,
        kit::colour::quiet,
        {}
    );
    const bool notice = !dialog.folder_notice.empty();
    text(
        list,
        DialogFont::small,
        layout::shown_text(
            notice ? std::string_view(dialog.folder_notice)
                   : layout::shown_text(layout::mods_folders_text[1])
        ),
        layout::mods_note_second,
        kit::Align::left,
        notice ? kit::colour::lock : kit::colour::quiet,
        {}
    );
}

/// Returns a row of Developer's list's name: its area's, its hack's, or its
/// hack's and its parameter's, with a set's value or a list's item.
///
/// @param row the row
/// @return the name
std::string list_row_name(const layout::ListRow& row) {
    namespace registry = oa::data::mod_profile::registry;
    if (row.kind == layout::ListRowKind::area) {
        const auto areas = developer_areas();
        const std::string_view area = row.area < areas.size() ? areas[row.area].name : "area";
        return named("hack-area." + word_form(area, true));
    }
    const auto hacks = oa::data::mod_profile::standard_hacks();
    if (row.hack >= hacks.size())
        return named("hack");
    const registry::Entry& entry = *hacks[row.hack];
    std::string name = named("hack." + word_form(entry.id, true));
    if (row.kind == layout::ListRowKind::hack)
        return name;
    const auto parameters = registry::parameters_of(entry);
    if (row.parameter < 0 || static_cast<std::size_t>(row.parameter) >= parameters.size())
        return name;
    const registry::Parameter& parameter = parameters[static_cast<std::size_t>(row.parameter)];
    name += "." + word_form(parameter.name, false);
    if (row.item == layout::list_length)
        return name + ".length";
    if (row.item == layout::whole_parameter)
        return name;
    // A set's value is named by its word; a list's item by its number from 1.
    if (row.kind == layout::ListRowKind::toggle) {
        const auto words = registry::enum_values_of(parameter.value);
        const auto at = static_cast<std::size_t>(row.item);
        return name + "." +
               (at < words.size() ? word_form(words[at], false) : std::to_string(at + 1));
    }
    return name + "." + std::to_string(row.item + 1);
}

/// Adds one row of Developer's list: an area's or a hack's header, a line
/// under a hack, or a parameter's control.
///
/// @param[in,out] building the list
/// @param dialog the dialog
/// @param row the row
/// @param first the row is the list's first, which the line over the list tops
void add_list_row(
    Building& building, const Dialog& dialog, const layout::ListRow& row, bool first
) {
    kit::DisplayList& list = building.list;
    const SourceRect& clip = layout::developer_view_clip;
    const bool header =
        row.kind == layout::ListRowKind::area || row.kind == layout::ListRowKind::hack;
    const bool takes = header || !row.locked;
    const bool hovered = takes && row.control != no_control &&
                         (dialog.hovered == row.control || dialog.pressed == row.control);
    const bool focused = takes && row.control != no_control && dialog.focused == row.control;
    if (row.kind == layout::ListRowKind::area && !first)
        fill(
            list, {layout::content_left, row.top, layout::content_width, 1}, kit::colour::rule, clip
        );
    if (header && hovered)
        fill(
            list,
            {row.control_area.x,
             row.control_area.y + 1,
             row.control_area.width,
             row.control_area.height - 1},
            kit::colour::hover,
            clip
        );
    const auto line = [&](DialogFont font,
                          std::string_view words,
                          const SourceRect& box,
                          kit::Align align,
                          kit::Colour colour) {
        text(list, font, layout::shown_text(words), box, align, colour, clip);
    };
    switch (row.kind) {
    case layout::ListRowKind::area:
        mark(
            list,
            row.open ? kit::Mark::arrow_open : kit::Mark::arrow_closed,
            {row.arrow.x, row.arrow.y},
            hovered ? kit::colour::text : kit::colour::hint,
            clip
        );
        line(DialogFont::regular, row.text, row.label, kit::Align::left, kit::colour::text);
        line(DialogFont::small, row.shown, row.value, kit::Align::right, kit::colour::hint);
        break;
    case layout::ListRowKind::hack:
        mark(
            list,
            row.open ? kit::Mark::arrow_open : kit::Mark::arrow_closed,
            {row.arrow.x, row.arrow.y},
            hovered ? kit::colour::text : kit::colour::hint,
            clip
        );
        line(
            DialogFont::small,
            row.text,
            row.label,
            kit::Align::left,
            row.on ? kit::colour::text : kit::colour::hint
        );
        toggle(
            list,
            row.toggle,
            switch_look(row.on, hovered && !row.locked, row.locked),
            row.locked ? no_control : row.control,
            clip
        );
        break;
    case layout::ListRowKind::id:
        line(DialogFont::small, row.text, row.label, kit::Align::left, kit::colour::quiet);
        break;
    case layout::ListRowKind::text:
        line(DialogFont::small, row.text, row.label, kit::Align::left, kit::colour::hint);
        break;
    case layout::ListRowKind::scope:
        line(DialogFont::small, row.text, row.label, kit::Align::left, kit::colour::lock);
        break;
    case layout::ListRowKind::heading:
        line(DialogFont::small, row.text, row.label, kit::Align::left, kit::colour::text);
        break;
    case layout::ListRowKind::toggle:
        line(DialogFont::small, row.text, row.label, kit::Align::left, kit::colour::text);
        toggle(
            list,
            row.control_area,
            switch_look(row.on, hovered, row.locked),
            takes ? row.control : no_control,
            clip
        );
        break;
    case layout::ListRowKind::slider: {
        line(DialogFont::small, row.text, row.label, kit::Align::left, kit::colour::text);
        line(DialogFont::small, row.shown, row.value, kit::Align::right, kit::colour::text);
        kit::Item& slider = item(list, kit::Role::slider, row.control_area, clip);
        slider.control = takes ? row.control : no_control;
        slider.text = row.shown;
        slider.state.hovered = hovered;
        slider.state.locked = row.locked;
        slider.look = kit::SliderLook{row.stop, row.stops, row.locked, hovered};
        break;
    }
    }
    // A control that takes no change, while Developer Mode is off, fades
    // as a locked row's does; its text keeps its strength.
    if (row.locked &&
        (row.kind == layout::ListRowKind::hack || row.kind == layout::ListRowKind::toggle ||
         row.kind == layout::ListRowKind::slider))
        fade(list, row.kind == layout::ListRowKind::hack ? row.toggle : row.control_area, clip);
    if (focused)
        focus_ring(list, row.control_area, !header, clip);
    if (!layout::list_row_takes_input(row))
        return;
    kit::ControlKind kind = kit::ControlKind::list_item;
    if (row.kind == layout::ListRowKind::toggle)
        kind = kit::ControlKind::toggle;
    else if (row.kind == layout::ListRowKind::slider)
        kind = kit::ControlKind::slider;
    kit::Control added = control_of(row.control, row.control_area, list_row_name(row), kind);
    added.clip = layout::developer_view;
    added.steps = true;
    added.group = layout::developer_list_group;
    added.checked = row.on;
    added.text = row.kind == layout::ListRowKind::slider ? row.shown : row.text;
    building.developer.push_back(std::move(added));
}

/// Adds what lies under Developer's rows: its list clipped to the list's
/// view with the list's scroll bar while it scrolls, and the list's footer
/// with Show Active Only and Restore profile values.
///
/// @param[in,out] building the list
/// @param dialog the dialog
/// @param open Developer's rows and list (layout::open_rows)
void add_developer(Building& building, const Dialog& dialog, const layout::ScrolledRows& open) {
    kit::DisplayList& list = building.list;
    const auto hot = [&](int32_t control) {
        return dialog.hovered == control || dialog.pressed == control;
    };
    for (std::size_t index = 0; index < open.list.rows.size(); ++index)
        add_list_row(building, dialog, open.list.rows[index], index == 0);
    // Scrolled from its top, the view's first row keeps a line.
    if (open.scroll > 0)
        fill(
            list,
            {layout::content_left, layout::developer_view.y, layout::content_width, 1},
            kit::colour::rule,
            layout::developer_view_clip
        );
    if (open.limit > 0)
        scroll_bar(
            list,
            open.area.well,
            layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height),
            hot(scroll_bar_control)
        );
    fill(
        list,
        {layout::content_left, layout::developer_footer_rule, layout::content_width, 1},
        kit::colour::rule,
        {}
    );
    const std::string label(
        layout::shown_text(
            layout::active_only_text(
                active_hack_count(dialog), oa::data::mod_profile::standard_hacks().size()
            )
        )
    );
    text(
        list,
        DialogFont::regular,
        label,
        layout::active_only_label,
        kit::Align::left,
        kit::colour::text,
        {}
    );
    const kit::SwitchLook active =
        switch_look(dialog.developer.active_only, hot(active_only_control), false);
    toggle(list, layout::active_only_switch, active, active_only_control, {});
    if (dialog.focused == active_only_control)
        focus_ring(list, layout::active_only_switch, true, {});
    kit::Control active_only = control_of(
        active_only_control,
        layout::active_only_switch,
        named("active-only"),
        kit::ControlKind::toggle
    );
    active_only.steps = true;
    active_only.checked = dialog.developer.active_only;
    active_only.text = dialog.developer.active_only ? active.on_caption : active.off_caption;
    building.footer_own.push_back(std::move(active_only));
    // Restore profile values takes a press only while Developer Mode is on.
    // It lights while the pointer is over it or a press on it is held.
    const SourceRect& restore = layout::restore_profile_button;
    const bool enabled = developer::restore_profile_enabled(dialog);
    const std::string caption(layout::shown_text(layout::restore_profile_text));
    button(
        list,
        restore,
        {caption,
         kit::ButtonStyle::plain,
         enabled && hot(restore_profile_control),
         enabled && dialog.pressed == restore_profile_control,
         enabled},
        restore_profile_control,
        {}
    );
    if (enabled && dialog.focused == restore_profile_control)
        focus_ring(list, restore, true, {});
    kit::Control restore_profile = control_of(
        restore_profile_control, restore, named("restore-profile-values"), kit::ControlKind::button
    );
    restore_profile.enabled = enabled;
    restore_profile.text = caption;
    building.footer_own.push_back(std::move(restore_profile));
}

/// Adds the open section: its heading, its rows clipped to the view they
/// scroll in, and its scroll bar while they scroll; on Developer, its list
/// and the list's footer under its rows; on Mods, its list.
///
/// @param[in,out] building the list
/// @param dialog the dialog
/// @param open the open section's rows (layout::open_rows)
void add_section(Building& building, const Dialog& dialog, const layout::ScrolledRows& open) {
    kit::DisplayList& list = building.list;
    item(list, kit::Role::heading, layout::heading, {}).look =
        kit::HeadingLook{std::string(layout::shown_text(layout::page_heading(dialog.page)))};
    if (layout::mods_page(dialog)) {
        add_mods(building, dialog, open);
        return;
    }
    // The rows as their specs show them, through the kit; a row the view
    // cuts shows the part inside it, its text and its focus outline
    // included. Developer's rows stay at its top while its list scrolls.
    kit::PlacedRows placed;
    static_cast<void>(layout::place_section(
        dialog,
        dialog.page,
        layout::shown_locks(dialog),
        dialog.section_hooks,
        width_in(building.fonts, DialogFont::small),
        &placed
    ));
    kit::scroll(placed, layout::developer_page(dialog) ? 0 : open.scroll);
    kit::RowsState state;
    state.name_prefix = std::string(kNamePrefix);
    state.hovered = dialog.hovered;
    state.pressed = dialog.pressed;
    state.focused = dialog.focused;
    state.open_menu = dialog.open_list;
    state.marked_button = static_cast<std::size_t>(dialog.folder_marked);
    state.hovered_button = dialog.folder_hovered;
    state.group = layout::section_group;
    state.clip = layout::view_clip;
    kit::DisplayList rows;
    kit::add_rows(rows, placed, state);
    for (kit::Item& added : rows.items)
        list.items.push_back(std::move(added));
    // A press reaches a row's control where the view shows it.
    for (kit::Control& added : rows.controls) {
        added.clip = layout::view;
        building.rows.push_back(std::move(added));
    }
    if (layout::developer_page(dialog)) {
        add_developer(building, dialog, open);
        return;
    }
    if (open.limit == 0)
        return;
    // Scrolled from its top, the view's first row keeps a line, so that the
    // cut there is the same hairline as a row's own.
    if (open.scroll > 0) {
        kit::Item& rule = item(
            list,
            kit::Role::rule,
            {layout::content_left, layout::view.y, layout::content_width, 1},
            layout::view_clip
        );
        rule.colour = kit::colour::rule;
    }
    scroll_bar(
        list,
        open.area.well,
        layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height),
        dialog.hovered == scroll_bar_control || dialog.pressed == scroll_bar_control
    );
}

/// Adds Restore defaults, Cancel and OK.
///
/// @param[in,out] building the list
/// @param dialog the dialog
void add_footer(Building& building, const Dialog& dialog) {
    struct FooterButton {
        int32_t control{};
        std::string_view caption{};
        std::string_view name{};
    };

    const std::array<FooterButton, 3> buttons{{
        {restore_control, layout::restore_text, "restore-defaults"},
        {cancel_control, layout::cancel_text, "cancel"},
        {ok_control, layout::ok_text, "ok"},
    }};
    for (const FooterButton& footer : buttons) {
        const SourceRect rect = layout::footer_button(footer.control);
        const bool held = dialog.pressed == footer.control && dialog.hovered == footer.control;
        const bool hovered = dialog.hovered == footer.control;
        const std::string caption(layout::shown_text(footer.caption));
        button(
            building.list,
            rect,
            {caption,
             footer.control == ok_control ? kit::ButtonStyle::accent : kit::ButtonStyle::plain,
             hovered,
             held,
             true},
            footer.control,
            {}
        );
        if (dialog.focused == footer.control)
            focus_ring(building.list, rect, true, {});
        kit::Control added =
            control_of(footer.control, rect, named(footer.name), kit::ControlKind::button);
        added.text = caption;
        building.footer.push_back(std::move(added));
    }
}

/// Adds an open drop-down's menu over the dialog, and a control for each
/// item it shows.
///
/// @param[in,out] building the list
/// @param dialog the dialog
/// @param open the open section's rows
void add_open_menu(Building& building, const Dialog& dialog, const layout::ScrolledRows& open) {
    if (dialog.open_list == no_control)
        return;
    for (const layout::Row& row : open.rows.rows) {
        if (row.control != dialog.open_list || row.lock != Lock::none ||
            layout::kind_of(row.setting) != kit::RowKind::choice)
            continue;
        const auto& spec = layout::row_spec(row.setting);
        const layout::SettingsModel model = layout::reading(dialog.chosen, dialog);
        const int32_t choices = kit::row_count(spec, model);
        const SourceRect menu =
            layout::choice_list(row.control_area, static_cast<std::size_t>(choices));
        const int32_t shown = layout::shown_choices(static_cast<std::size_t>(choices));
        kit::ChoiceMenuLook look;
        look.first = dialog.list_first;
        look.total = choices;
        look.chosen = kit::row_index(spec, model);
        look.marked = dialog.list_marked;
        look.focus_shown = dialog.focused != no_control;
        for (int32_t place = 0; place < shown; ++place) {
            const int32_t choice = dialog.list_first + place;
            if (choice >= choices)
                break;
            look.shown.emplace_back(layout::shown_text(kit::row_caption(spec, model, choice)));
            kit::Control added = control_of(
                layout::menu_item_control(choice),
                layout::choice_item(menu, place),
                named(
                    std::string(layout::setting_name(row.setting)) + ".item-" +
                    std::to_string(choice + 1)
                ),
                kit::ControlKind::list_item
            );
            added.checked = choice == look.chosen;
            added.text = look.shown.back();
            building.menu.push_back(std::move(added));
        }
        item(building.list, kit::Role::choice_menu, menu, {}).look = std::move(look);
        return;
    }
}

/// Adds the question a mod row raises, over the dialog: a raised box in the
/// header's face with the mod's badge, title and version, the question in
/// amber and white, and the two buttons, the marked one ringed in green.
///
/// @param[in,out] building the list
/// @param dialog the dialog
void add_question(Building& building, const Dialog& dialog) {
    kit::DisplayList& list = building.list;
    fill(list, layout::question_box, kit::colour::band, {});
    outline(list, layout::question_box, kit::colour::accent, {});
    const bool roll_back = dialog.mod_question == ModQuestion::roll_back;
    item(list, kit::Role::heading, layout::question_heading, {}).look =
        kit::HeadingLook{std::string(
            layout::shown_text(
                layout::shown_text(
                    roll_back ? layout::roll_back_heading_text : layout::switch_heading_text
                )
            )
        )};
    const layout::ModRowText offered =
        layout::mod_row_text(dialog, ModRow{dialog.switch_question, false});
    add_badge(list, layout::question_badge, offered, dialog.switch_question == no_mod_row, {});
    const auto small_width = width_in(building.fonts, DialogFont::small);
    const auto regular_width = width_in(building.fonts, DialogFont::regular);
    text(
        list,
        DialogFont::regular,
        layout::shown_text(
            layout::cut_text(offered.title, layout::question_title.width, regular_width)
        ),
        layout::question_title,
        kit::Align::left,
        kit::colour::text,
        {}
    );
    text(
        list,
        DialogFont::small,
        layout::shown_text(
            layout::cut_text(
                layout::question_version_text(dialog, offered),
                layout::question_version.width,
                small_width
            )
        ),
        layout::question_version,
        kit::Align::left,
        kit::colour::hint,
        {}
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
        text(
            list,
            DialogFont::small,
            layout::shown_text(lines[line]),
            rect,
            kit::Align::left,
            note ? kit::colour::lock : kit::colour::text,
            {}
        );
    }

    struct QuestionButton {
        int32_t control{};
        std::string_view caption{};
        std::string_view name{};
    };

    const std::array<QuestionButton, 2> buttons{{
        {question_no_control, layout::no_text, "question.no"},
        {question_yes_control,
         roll_back ? layout::roll_back_text : layout::yes_text,
         "question.yes"},
    }};
    std::vector<kit::Control> answers;
    for (const QuestionButton& answer : buttons) {
        const bool yes = answer.control == question_yes_control;
        const SourceRect rect =
            yes ? layout::question_yes_rect(dialog) : layout::question_no_rect(dialog);
        const bool held = dialog.pressed == answer.control && dialog.hovered == answer.control;
        const bool hovered = dialog.hovered == answer.control;
        const std::string caption(layout::shown_text(answer.caption));
        button(
            list,
            rect,
            {caption,
             yes ? kit::ButtonStyle::accent : kit::ButtonStyle::plain,
             hovered,
             held,
             true},
            answer.control,
            {}
        );
        if (yes != dialog.question_marks_no)
            focus_ring(list, rect, true, {});
        kit::Control added =
            control_of(answer.control, rect, named(answer.name), kit::ControlKind::button);
        added.focusable = false;
        added.text = caption;
        answers.push_back(std::move(added));
    }
    // A press tries the answering button first.
    building.question.push_back(std::move(answers[1]));
    building.question.push_back(std::move(answers[0]));
}

} // namespace

namespace geometry {

kit::DisplayList dialog_list(const Dialog& dialog, const DialogFonts* fonts) {
    Building building;
    building.fonts = fonts;
    const SourceRect whole{0, 0, dialog_width, dialog_height};
    fill(building.list, whole, kit::colour::panel, {});
    item(building.list, kit::Role::header, {0, 0, dialog_width, header_rule_row + 1}, {}).look =
        dialog_header(dialog);
    add_nav(building, dialog);
    const ScrolledRows open = open_rows(dialog);
    add_section(building, dialog, open);
    item(
        building.list,
        kit::Role::footer_band,
        {0, footer_rule_row, dialog_width, footer_height + 1},
        {}
    )
        .look = kit::FooterBandLook{dialog_width, footer_rule_row};
    add_footer(building, dialog);
    if (open.limit > 0) {
        kit::Control bar = control_of(
            scroll_bar_control, open.area.hit, named("scroll-bar"), kit::ControlKind::scroll_bar
        );
        bar.focusable = false;
        building.scroll.push_back(std::move(bar));
    }
    item(building.list, kit::Role::bevel, whole, {}).colour = kit::colour::edge_light;
    // An open drop-down list lies over everything else, and the question
    // over all of it.
    add_open_menu(building, dialog, open);
    if (dialog.switch_question != no_question)
        add_question(building, dialog);
    kit::DisplayList list = std::move(building.list);
    for (auto* group :
         {&building.question,
          &building.menu,
          &building.developer,
          &building.footer_own,
          &building.roll_backs,
          &building.rows,
          &building.folder,
          &building.scroll,
          &building.footer,
          &building.nav})
        for (kit::Control& control : *group)
            list.controls.push_back(std::move(control));
    list.tab_order = focus_order(dialog, open);
    return list;
}

} // namespace geometry

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
    kit::paint({&target, placement, &fonts, icon}, layout::dialog_list(dialog, &fonts));
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
