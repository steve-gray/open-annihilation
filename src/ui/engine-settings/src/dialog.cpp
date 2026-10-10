// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's sections, rows and controls, where they lie, and
// what pointer and key events do to them.

#include "oa/ui/engine_settings/dialog.hpp"

#include "developer.hpp"
#include "geometry.hpp"

#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/mod_profile/overrides.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings {

namespace geometry {

namespace {

/// The sections a dialog of the engine's settings lists, in the list's order.
struct EnginePageList {
    std::array<Page, most_listed_pages> pages{}; ///< the sections; the first `count` count
    std::size_t count{};                         ///< how many it lists
};

/// Returns the engine's sections a dialog lists, in the list's order: the
/// five that every dialog lists, then Touch, Controller and Game files where
/// each is listed, then Developer.
///
/// @param touch the dialog lists Touch
/// @param controller the dialog lists Controller
/// @param game_files the dialog lists Game files
/// @return the sections
constexpr EnginePageList engine_page_list(bool touch, bool controller, bool game_files) noexcept {
    EnginePageList list{};
    const auto add = [&list](Page page) { list.pages[list.count++] = page; };
    for (const Page page :
         {Page::mods, Page::controls, Page::common_tweaks, Page::language, Page::graphics})
        add(page);
    if (touch)
        add(Page::touch);
    if (controller)
        add(Page::controller);
    if (game_files)
        add(Page::game_files);
    add(Page::developer);
    return list;
}

/// Returns a list's place among kEnginePageLists.
///
/// @param touch the dialog lists Touch
/// @param controller the dialog lists Controller
/// @param game_files the dialog lists Game files
/// @return 0 to 7: Touch adds 1, Controller 2 and Game files 4
constexpr std::size_t
engine_page_list_index(bool touch, bool controller, bool game_files) noexcept {
    return (touch ? 1U : 0U) + (controller ? 2U : 0U) + (game_files ? 4U : 0U);
}

/// Every list of the engine's sections, by engine_page_list_index.
constexpr std::array<EnginePageList, 8> kEnginePageLists{
    engine_page_list(false, false, false),
    engine_page_list(true, false, false),
    engine_page_list(false, true, false),
    engine_page_list(true, true, false),
    engine_page_list(false, false, true),
    engine_page_list(true, false, true),
    engine_page_list(false, true, true),
    engine_page_list(true, true, true),
};
static_assert(kEnginePageLists.front().count == 6, "six sections without the optional three");
static_assert(
    kEnginePageLists.back().count == most_listed_pages,
    "the longest list holds the most sections a dialog lists"
);
/// The one section a Language dialog lists.
constexpr std::array<Page, 1> kLanguageTextPages{Page::language};

/// Tells whether every section's entry number is its place in the list of
/// the engine's settings with Touch and Controller, a list without either
/// only leaving its number out.
///
/// @return true when page_control follows the list with Touch and Controller
constexpr bool entries_follow_the_list() noexcept {
    const EnginePageList& list = kEnginePageLists[engine_page_list_index(true, true, false)];
    for (std::size_t index = 0; index < list.count; ++index)
        if (page_control(list.pages[index]) != static_cast<int32_t>(index))
            return false;
    return true;
}

static_assert(
    entries_follow_the_list(), "each entry's number is its place with Touch and Controller listed"
);
static_assert(
    page_control(Page::game_files) == page_control(Page::developer) + 1,
    "Game files' entry number follows Developer's, so Touch, Controller and Developer keep theirs"
);
/// The mod options' sections, in the list's order.
constexpr std::array<Page, 5> kModPages{
    Page::mod_keys, Page::mod_patrol, Page::mod_guard, Page::mod_tools, Page::mod_chat
};

/// Returns a text's width at estimated_character_width a character, as the
/// rows are placed where no font measures their words.
///
/// @param text the text, in UTF-8
/// @return the width, in source pixels
int32_t estimated_width(std::string_view text) {
    return static_cast<int32_t>(oa::ui::kit::character_count(text)) * estimated_character_width;
}

/// Returns a section's place among Dialog::scroll.
///
/// @param page the section
/// @return its index, below page_count
std::size_t scroll_index(Page page) noexcept {
    return std::min(static_cast<std::size_t>(page), page_count - 1);
}

} // namespace

SourceRect folder_button(const SourceRect& area, std::size_t index) noexcept {
    int32_t left = area.x;
    for (std::size_t before = 0; before < index && before < folder_button_count; ++before)
        left += folder_button_widths[before] + folder_button_gap;
    const int32_t width = index < folder_button_count ? folder_button_widths[index] : 0;
    return {left, area.y, width, area.height};
}

std::size_t folder_button_at(const SourceRect& area, int32_t x, int32_t y) noexcept {
    for (std::size_t index = 0; index < folder_button_count; ++index) {
        const SourceRect button = folder_button(area, index);
        if (x >= button.x && y >= button.y && x < button.x + button.width &&
            y < button.y + button.height)
            return index;
    }
    return folder_button_count;
}

std::string_view shown_text(std::string_view english) {
    return oa::data::languages::interface_text(english);
}

RowContext row_context(const Dialog& dialog) noexcept {
    return RowContext{dialog.steam_input, dialog.steam_deck_panel_hz};
}

kit::RowView row_view(
    const Dialog& dialog, Setting setting, Lock lock, bool status, const TextWidth& small_width
) {
    kit::RowView shown =
        kit::view_of(row_spec(setting), reading(dialog.chosen, dialog), &shown_text);
    // The lock the dialog puts on the row, looked up as the dialog has
    // always drawn it, and a switch a language sets showing On.
    shown.lock =
        lock == Lock::none ? std::string() : std::string(shown_text(shown_text(lock_text(lock))));
    shown.hint_is_status = status;
    if (lock == Lock::set_by_language)
        shown.on = true;
    for (std::size_t line = 0; line < shown.hints.size(); ++line) {
        if (line == 0 && hint_is_path(setting)) {
            const std::string path = row_hint(dialog, setting, 0).text;
            shown.hints[line] =
                path.empty() ? std::string()
                             : std::string(shown_text(path_tail(path, content_width, small_width)));
            continue;
        }
        shown.hints[line] = std::string(shown_text(shown.hints[line]));
    }
    return shown;
}

Rows place_section(
    const Dialog& dialog,
    Page page,
    const Locks& locks,
    const SectionHooks* section,
    const TextWidth& small_width,
    kit::PlacedRows* placed
) {
    const auto settings = section_settings(page, section, row_context(dialog));
    std::vector<kit::RowView> views;
    std::vector<Lock> row_locks;
    std::vector<bool> statuses;
    views.reserve(settings.size());
    for (const Setting setting : settings) {
        row_locks.push_back(section_lock(locks, setting, section));
        statuses.push_back(section_hint_is_status(setting, section));
        views.push_back(row_view(dialog, setting, row_locks.back(), statuses.back(), small_width));
    }
    // Developer's own rows lie closer, over its list; while the dialog's
    // words are drawn in the modern fonts, a hint's lines lie further apart.
    const bool own_section = section != nullptr && section->settings != nullptr;
    kit::RowPlacement placement;
    placement.left = content_left;
    placement.right = content_right;
    placement.first_top = first_row_top;
    placement.row_padding =
        page == Page::developer && !own_section ? developer_row_padding : row_padding;
    placement.tall = oa::data::languages::interface_language().needs !=
                     oa::data::languages::TextNeeds::game_fonts;
    placement.first_control = first_row_control;
    kit::PlacedRows kit_rows = kit::place_rows(views, placement);
    Rows rows{};
    rows.bottom = kit_rows.bottom;
    rows.rows.reserve(kit_rows.rows.size());
    for (std::size_t index = 0; index < kit_rows.rows.size(); ++index) {
        const kit::PlacedRow& placed_row = kit_rows.rows[index];
        Row& row = rows.rows.emplace_back();
        row.setting = settings[index];
        row.control = placed_row.control;
        row.lock = row_locks[index];
        row.hint_is_status = statuses[index];
        row.top = placed_row.top;
        row.height = placed_row.height;
        row.label = placed_row.label;
        row.lock_area = placed_row.lock_area;
        row.hint_lines = std::min(placed_row.hints.size(), most_row_lines);
        for (std::size_t line = 0; line < row.hint_lines; ++line)
            row.hints[line] = placed_row.hints[line];
        row.control_area = placed_row.control_area;
        // Only a slider shows a value of its own beside its control.
        if (placed_row.view.kind == kit::RowKind::slider)
            row.value = placed_row.value;
    }
    if (placed != nullptr)
        *placed = std::move(kit_rows);
    return rows;
}

Rows place_rows(
    Page page,
    const Locks& locks,
    int32_t scroll,
    const SectionHooks* section,
    const RowContext& context
) {
    // The rows' places hang on what the dialog shows beyond each setting's
    // rows: the Steam Input notice and a Steam Deck's rate.
    Dialog shown;
    shown.steam_input = context.steam_input;
    shown.steam_deck_panel_hz = context.steam_deck_panel_hz;
    Rows placed = place_section(shown, page, locks, section, &estimated_width);
    scroll_rows(placed, scroll);
    return placed;
}

void scroll_rows(Rows& rows, int32_t by) noexcept {
    const auto lift = [by](SourceRect& rect) {
        if (rect.width > 0 && rect.height > 0)
            rect.y -= by;
    };
    for (Row& row : rows.rows) {
        row.top -= by;
        lift(row.label);
        lift(row.lock_area);
        for (SourceRect& hint : row.hints)
            lift(hint);
        lift(row.control_area);
        lift(row.value);
    }
    rows.bottom -= by;
}

int32_t content_height(const Rows& rows, int32_t scroll) noexcept {
    return rows.bottom + scroll + 1 + end_gap - first_row_top;
}

int32_t scroll_limit(int32_t content_height) noexcept {
    return std::max(content_height - view.height, int32_t{0});
}

Locks shown_locks(const Dialog& dialog) noexcept {
    Locks locks = dialog.locks;
    // The language chosen, System default's included, may need the modern
    // fonts and chat in UTF-8.
    namespace languages = oa::data::languages;
    const languages::Language& system =
        dialog.system_language != nullptr ? *dialog.system_language : languages::english();
    const languages::Language& chosen = languages::chosen_language(dialog.chosen.language, system);
    if (locks.modern_fonts == Lock::none && chosen.needs == languages::TextNeeds::modern_fonts)
        locks.modern_fonts = Lock::set_by_language;
    if (locks.unicode_chat == Lock::none &&
        std::find(
            dialog.unicode_chat_languages.begin(), dialog.unicode_chat_languages.end(), chosen.tag
        ) != dialog.unicode_chat_languages.end())
        locks.unicode_chat = Lock::set_by_language;
    const bool modern_fonts =
        dialog.chosen.modern_fonts || locks.modern_fonts == Lock::set_by_language;
    if (locks.text_size == Lock::none && !modern_fonts)
        locks.text_size = Lock::needs_modern_fonts;
    // After zoom says where units turn to dots; drawn whole, they never do.
    if (locks.zoomed_out_after == Lock::none &&
        dialog.chosen.zoomed_out_units != ZoomedOutUnits::dots)
        locks.zoomed_out_after = Lock::needs_dots;
    return locks;
}

ScrolledRows open_rows(const Dialog& dialog) {
    ScrolledRows open{};
    if (mods_page(dialog)) {
        // Mods' list scrolls in a view of its own, over OPEN MODS FOLDER.
        open.area = mods_scroll(dialog.locks.mod != Lock::none);
        open.rows = place_mod_rows(dialog, 0);
        open.content_height = open.rows.bottom - open.area.view.y;
        open.limit = std::max(open.content_height - open.area.view.height, int32_t{0});
        open.scroll = std::clamp(dialog.scroll[scroll_index(dialog.page)], int32_t{0}, open.limit);
        scroll_rows(open.rows, open.scroll);
        return open;
    }
    open.rows = place_section(
        dialog, dialog.page, shown_locks(dialog), dialog.section_hooks, &estimated_width
    );
    if (developer_page(dialog)) {
        // Developer's rows stay at its top; its list scrolls under them in a
        // view of its own, with the end gap under its last row.
        open.area = developer_scroll;
        open.list = place_list(dialog, 0);
        open.content_height = open.list.bottom + end_gap - developer_view.y;
        open.limit = std::max(open.content_height - developer_view.height, int32_t{0});
        open.scroll = std::clamp(dialog.scroll[scroll_index(dialog.page)], int32_t{0}, open.limit);
        scroll_list(open.list, open.scroll);
        return open;
    }
    open.content_height = content_height(open.rows, 0);
    open.limit = scroll_limit(open.content_height);
    // While the dialog's words are drawn in the modern fonts, whose
    // ideographs fill a hint line from its top row, the view's top edge
    // cuts no hint line at the end of the scroll, of which a sliver would
    // show under it: the section scrolls on until the line has passed the
    // edge, its end gap that much taller.
    const bool tall = oa::data::languages::interface_language().needs !=
                      oa::data::languages::TextNeeds::game_fonts;
    if (open.limit > 0 && tall) {
        const int32_t view_top = open.area.view.y;
        for (bool moved = true; moved;) {
            moved = false;
            for (const Row& row : open.rows.rows)
                for (std::size_t line = 0; line < row.hint_lines; ++line) {
                    const SourceRect& box = row.hints[line];
                    const int32_t top = box.y - open.limit;
                    if (top <= view_top && top + box.height > view_top) {
                        open.limit += top + box.height - view_top;
                        moved = true;
                    }
                }
        }
        open.content_height = open.limit + view.height;
    }
    open.scroll = std::clamp(dialog.scroll[scroll_index(dialog.page)], int32_t{0}, open.limit);
    scroll_rows(open.rows, open.scroll);
    return open;
}

int32_t scroll_showing(const ScrolledRows& open, std::size_t index) noexcept {
    if (index >= open.rows.rows.size())
        return open.scroll;
    const Row& row = open.rows.rows[index];
    // The row's line, at the section's top, may come up to the view's first
    // row; the line under it, or the end gap under the last row, down to its
    // last. A row taller than the view would show its top.
    const SourceRect& seen = open.area.view;
    const int32_t line = row.top + open.scroll;
    const int32_t highest = line - seen.y;
    const int32_t lowest = index + 1 == open.rows.rows.size()
                               ? open.limit
                               : line + row.height - (seen.y + seen.height - 1);
    const int32_t scroll = std::min(std::max(open.scroll, lowest), highest);
    return std::clamp(scroll, int32_t{0}, open.limit);
}

SourceRect scroll_thumb(int32_t scroll, int32_t limit, int32_t content_height) noexcept {
    return scroll_thumb(section_scroll, scroll, limit, content_height);
}

SourceRect scroll_thumb(
    const ScrollArea& area, int32_t scroll, int32_t limit, int32_t content_height
) noexcept {
    const SourceRect inside{
        area.well.x + 1, area.well.y + 1, area.well.width - 2, area.well.height - 2
    };
    int32_t height = inside.height;
    if (content_height > area.view.height)
        height = std::max(least_thumb_height, inside.height * area.view.height / content_height);
    const int32_t travel = inside.height - height;
    int32_t top = inside.y;
    if (limit > 0)
        top += (travel * std::clamp(scroll, int32_t{0}, limit) + limit / 2) / limit;
    return {inside.x, top, inside.width, height};
}

int32_t scroll_at(int32_t thumb_top, int32_t limit, int32_t content_height) noexcept {
    return scroll_at(section_scroll, thumb_top, limit, content_height);
}

int32_t scroll_at(
    const ScrollArea& area, int32_t thumb_top, int32_t limit, int32_t content_height
) noexcept {
    const SourceRect thumb = scroll_thumb(area, 0, limit, content_height);
    const int32_t travel = area.well.height - 2 - thumb.height;
    if (travel <= 0 || limit <= 0)
        return 0;
    const int32_t along = std::clamp(thumb_top - thumb.y, int32_t{0}, travel);
    return (limit * along + travel / 2) / travel;
}

SourceRect list_item(Page page, bool touch, bool game_files, bool controller) noexcept {
    // Each entry at its place in the list its dialog shows; Touch and
    // Controller, which only a dialog that lists them asks for, keep their
    // own places.
    const auto kind = static_cast<int32_t>(page) >= static_cast<int32_t>(Page::mod_keys)
                          ? DialogKind::mod_options
                          : DialogKind::engine;
    const auto listed = dialog_pages(kind, touch, game_files, controller);
    const auto found = std::find(listed.begin(), listed.end(), page);
    const auto index = found != listed.end() ? static_cast<int32_t>(found - listed.begin())
                                             : page_control(page) - first_page_control;
    int32_t top = list_first_top + index * (list_item_height + list_item_gap);
    if (page == Page::developer)
        top = list_divider().y + 1 + list_divider_margin;
    return {list_item_left, top, list_item_width, list_item_height};
}

SourceRect list_divider() noexcept {
    // Developer stands at the foot of the list, as far under the divider as
    // the first section stands under the list's top.
    const int32_t row =
        footer_rule_row - (list_first_top - body_top) - list_item_height - list_divider_margin - 1;
    return {
        list_item_left + list_divider_inset,
        row,
        list_item_width - 2 * list_divider_inset,
        1,
    };
}

SourceRect footer_button(int32_t control) noexcept {
    if (control == restore_control)
        return restore_button;
    if (control == cancel_control)
        return cancel_button;
    return ok_button;
}

EngineSettings slider_settings(const Dialog& dialog) {
    EngineSettings shown = dialog.chosen;
    if (dialog.window_screen_size)
        shown.screen_size = *dialog.window_screen_size;
    return shown;
}

std::vector<std::string>
break_lines(std::string_view text, std::size_t characters, std::size_t most_lines) {
    // The mark between a location's folders: "Open Annihilation › Total
    // Annihilation".
    constexpr std::string_view location_mark = "›";
    std::vector<std::string> lines;
    characters = std::max<std::size_t>(characters, 1);
    // Where each character starts, so that lines count characters and never
    // cut one.
    const auto next = [&text](std::size_t at) {
        const auto lead = static_cast<unsigned char>(text[at]);
        std::size_t bytes = 1;
        if (lead >= 0xf0)
            bytes = 4;
        else if (lead >= 0xe0)
            bytes = 3;
        else if (lead >= 0xc0)
            bytes = 2;
        return std::min(at + bytes, text.size());
    };
    std::size_t start = 0;
    while (start < text.size() && lines.size() < most_lines) {
        while (start < text.size() && text[start] == ' ')
            ++start;
        if (start >= text.size())
            break;
        std::size_t end = start;
        std::size_t count = 0;
        std::size_t last_space = std::string_view::npos;
        // A space after a location's separator, which keeps the folders'
        // names whole.
        std::size_t last_separator = std::string_view::npos;
        while (end < text.size() && count < characters) {
            if (text[end] == ' ') {
                last_space = end;
                if (end >= start + location_mark.size() &&
                    text.substr(end - location_mark.size(), location_mark.size()) == location_mark)
                    last_separator = end;
            }
            end = next(end);
            ++count;
        }
        // The rest fits, or the line breaks after its last separator in its
        // second half, else where a word ends, at its last space, or a word
        // longer than a line is cut.
        if (end < text.size()) {
            if (last_separator != std::string_view::npos &&
                last_separator - start >= (end - start) / 2)
                end = last_separator;
            else if (text[end] != ' ' && last_space != std::string_view::npos && last_space > start)
                end = last_space;
        }
        std::string_view line = text.substr(start, end - start);
        while (!line.empty() && line.back() == ' ')
            line.remove_suffix(1);
        lines.emplace_back(line);
        start = end;
    }
    return lines;
}

SourceRect dialog_list_item(const Dialog& dialog, Page page) noexcept {
    if (dialog.kind != DialogKind::language_text)
        return list_item(page, dialog.touch, dialog.game_files, dialog.controller);
    // A Language dialog lists its one section at the top.
    return {list_item_left, list_first_top, list_item_width, list_item_height};
}

std::string_view lock_text(Lock lock) noexcept {
    switch (lock) {
    case Lock::none:
        return {};
    case Lock::in_game:
        return "Locked during a game";
    case Lock::set_by_host:
        return "Set by the host";
    case Lock::command_line:
        return "Set on the command line";
    case Lock::unavailable:
        return "Not available here";
    case Lock::set_by_mod:
        return "Set by the mod";
    case Lock::needs_modern_fonts:
        return "Needs modern fonts";
    case Lock::always_on:
        return "Always on here";
    case Lock::set_by_language:
        return "Set by the language";
    case Lock::needs_dots:
        return "Needs Dots";
    }
    return {};
}

} // namespace geometry

namespace layout = geometry;

bool layout::list_row_takes_input(const layout::ListRow& row) noexcept {
    if (row.control == no_control)
        return false;
    return row.kind == layout::ListRowKind::area || row.kind == layout::ListRowKind::hack ||
           !row.locked;
}

std::vector<bool> layout::roll_back_rows(const Dialog& dialog, const layout::ScrolledRows& open) {
    std::vector<bool> shown(open.rows.rows.size(), false);
    if (!layout::mods_page(dialog))
        return shown;
    const auto rows = mod_rows(dialog);
    for (std::size_t index = 0; index < shown.size() && index < rows.size(); ++index)
        shown[index] = layout::offers_roll_back(dialog, rows[index]);
    return shown;
}

std::vector<int32_t> layout::focus_order(const Dialog& dialog, const layout::ScrolledRows& open) {
    std::vector<int32_t> order;
    // A row that only shows text takes no focus; a row of Mods is followed
    // by its ROLL BACK.
    const std::vector<bool> roll_backs = roll_back_rows(dialog, open);
    for (std::size_t index = 0; index < open.rows.rows.size(); ++index) {
        const layout::Row& row = open.rows.rows[index];
        if (row.lock == Lock::none && row.control_area.width > 0)
            order.push_back(row.control);
        if (row.lock == Lock::none && roll_backs[index])
            order.push_back(layout::roll_back_control(open.rows, index));
    }
    if (layout::developer_page(dialog)) {
        for (const layout::ListRow& row : open.list.rows) {
            if (list_row_takes_input(row))
                order.push_back(row.control);
        }
        order.push_back(active_only_control);
        if (developer::restore_profile_enabled(dialog))
            order.push_back(restore_profile_control);
    }
    if (layout::mods_page(dialog) && dialog.locks.mod == Lock::none)
        order.push_back(layout::mods_folder_control(open.rows));
    order.push_back(restore_control);
    order.push_back(cancel_control);
    order.push_back(ok_control);
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller))
        order.push_back(page_control(page));
    return order;
}

namespace {

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the point's column
/// @param y the point's row
/// @return true inside it
bool contains(const layout::SourceRect& rect, int32_t x, int32_t y) noexcept {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/// Tells whether a rectangle lies wholly in another.
///
/// @param rect the rectangle
/// @param outer the other
/// @return true when no part of it lies outside
bool wholly_in(const layout::SourceRect& rect, const layout::SourceRect& outer) noexcept {
    return rect.x >= outer.x && rect.y >= outer.y && rect.x + rect.width <= outer.x + outer.width &&
           rect.y + rect.height <= outer.y + outer.height;
}

/// Returns the row a control is, when it is one of the open section's.
///
/// @param rows the open section's rows
/// @param control the control
/// @return the row; nullptr for a control that is not a row's
const layout::Row* row_of(const layout::Rows& rows, int32_t control) noexcept {
    const int32_t index = control - first_row_control;
    if (index < 0 || static_cast<std::size_t>(index) >= rows.rows.size())
        return nullptr;
    return &rows.rows[static_cast<std::size_t>(index)];
}

/// Returns the control under a point that a press can act on.
///
/// A row's control answers only on the part the view shows, and a locked
/// row's takes no press. The scroll bar answers while the section scrolls.
/// On Developer, its list's rows, Show Active Only and, while Developer Mode
/// is on, Restore profile values answer too.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @param x the point's column
/// @param y the point's row
/// @return the control; no_control when none is there
int32_t
control_at(const Dialog& dialog, const layout::ScrolledRows& open, int32_t x, int32_t y) noexcept {
    if (layout::developer_page(dialog)) {
        if (contains(layout::developer_view, x, y)) {
            for (const layout::ListRow& row : open.list.rows) {
                if (list_row_takes_input(row) && contains(row.control_area, x, y))
                    return row.control;
            }
        }
        if (contains(layout::active_only_switch, x, y))
            return active_only_control;
        if (developer::restore_profile_enabled(dialog) &&
            contains(layout::restore_profile_button, x, y))
            return restore_profile_control;
    }
    // Mods' rows answer in its list's own view; every other section's rows,
    // Developer's above its list among them, in the view under the heading.
    if (contains(layout::mods_page(dialog) ? open.area.view : layout::view, x, y)) {
        // A row's ROLL BACK lies inside the row and answers first.
        const std::vector<bool> roll_backs = roll_back_rows(dialog, open);
        for (std::size_t index = 0; index < roll_backs.size(); ++index) {
            const layout::Row& row = open.rows.rows[index];
            if (roll_backs[index] && row.lock == Lock::none &&
                contains(layout::roll_back_button(row), x, y))
                return layout::roll_back_control(open.rows, index);
        }
        for (const layout::Row& row : open.rows.rows) {
            if (row.lock == Lock::none && contains(row.control_area, x, y))
                return row.control;
        }
    }
    if (layout::mods_page(dialog) && dialog.locks.mod == Lock::none &&
        contains(layout::mods_folder_button, x, y))
        return layout::mods_folder_control(open.rows);
    if (open.limit > 0 && contains(open.area.hit, x, y))
        return scroll_bar_control;
    for (const int32_t control : {restore_control, cancel_control, ok_control}) {
        if (contains(layout::footer_button(control), x, y))
            return control;
    }
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        if (contains(layout::dialog_list_item(dialog, page), x, y))
            return page_control(page);
    }
    return no_control;
}

/// Notes where the pointer is, so that the hover can follow the rows a
/// scroll moves under it.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column
/// @param y the pointer's row
void note_pointer(Dialog& dialog, int32_t x, int32_t y) noexcept {
    dialog.pointer_known = true;
    dialog.pointer_x = x;
    dialog.pointer_y = y;
}

/// Finds the control under the last pointer point again, after a scroll
/// moved the rows under it; a held press keeps its hover.
///
/// @param[in,out] dialog the dialog
/// @param open the open section's rows, at the offset the scroll left
void hover_again(Dialog& dialog, const layout::ScrolledRows& open) noexcept {
    if (!dialog.pointer_known || dialog.pressed != no_control)
        return;
    dialog.hovered = control_at(dialog, open, dialog.pointer_x, dialog.pointer_y);
}

/// Scrolls the open section to an offset, moving its placed rows with it;
/// on Developer, its list alone. No scroll changes a setting or moves the
/// focus.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows (layout::open_rows), left at
///     the new offset
/// @param offset the offset, clamped to the section's limit
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction scroll_to(Dialog& dialog, layout::ScrolledRows& open, int32_t offset) noexcept {
    const int32_t next = std::clamp(offset, int32_t{0}, open.limit);
    dialog.scroll[layout::scroll_index(dialog.page)] = next;
    if (next == open.scroll)
        return DialogAction::none;
    if (!layout::developer_page(dialog))
        layout::scroll_rows(open.rows, next - open.scroll);
    layout::scroll_list(open.list, next - open.scroll);
    open.scroll = next;
    hover_again(dialog, open);
    return DialogAction::redraw;
}

/// Scrolls the least that shows a row whole, when a control is one of the
/// open section's rows; nothing moves while a press is held.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the offset shown
/// @param control the control
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction show_row(Dialog& dialog, layout::ScrolledRows& open, int32_t control) noexcept {
    if (control < first_row_control || dialog.pressed != no_control)
        return DialogAction::none;
    // Developer's rows and its footer's switch and button stay where they
    // are; its list's rows scroll.
    if (layout::developer_page(dialog))
        return control < first_hack_list_control
                   ? DialogAction::none
                   : scroll_to(dialog, open, layout::list_scroll_showing(open, control));
    if (layout::mods_page(dialog) && control >= layout::mods_folder_control(open.rows)) {
        // A row's ROLL BACK brings its row into view.
        const int32_t row = layout::roll_back_row(open.rows, control);
        if (row < 0)
            return DialogAction::none;
        return scroll_to(dialog, open, layout::scroll_showing(open, static_cast<std::size_t>(row)));
    }
    return scroll_to(
        dialog,
        open,
        layout::scroll_showing(open, static_cast<std::size_t>(control - first_row_control))
    );
}

/// Moves the focus to the next or previous control, and scrolls its row
/// into view when it is a row.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the offset shown
/// @param forward true for the next control, false for the previous
/// @return DialogAction::redraw
DialogAction move_focus(Dialog& dialog, layout::ScrolledRows& open, bool forward) {
    const auto order = focus_order(dialog, open);
    const auto found = std::find(order.begin(), order.end(), dialog.focused);
    if (found == order.end()) {
        dialog.focused = forward ? order.front() : order.back();
    } else {
        const auto count = static_cast<std::ptrdiff_t>(order.size());
        const std::ptrdiff_t at = found - order.begin();
        const std::ptrdiff_t next = (at + (forward ? 1 : count - 1)) % count;
        dialog.focused = order[static_cast<std::size_t>(next)];
    }
    static_cast<void>(show_row(dialog, open, dialog.focused));
    return DialogAction::redraw;
}

/// Scrolls the open section for Page Up, Page Down, Home or End, whatever
/// has the focus; nothing moves while a press is held.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the new offset
/// @param key the key
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction scroll_key(Dialog& dialog, layout::ScrolledRows& open, DialogKey key) noexcept {
    if (dialog.pressed != no_control)
        return DialogAction::none;
    int32_t next = open.scroll;
    if (key == DialogKey::page_up)
        next -= open.area.page_step;
    else if (key == DialogKey::page_down)
        next += open.area.page_step;
    else if (key == DialogKey::home)
        next = 0;
    else
        next = open.limit;
    return scroll_to(dialog, open, next);
}

/// Reports a change of the chosen settings, or only a look's. Hardware
/// acceleration passing to a higher level, from Off to Basic or Full or
/// from Basic to Full, asks for the graphics card to be tried afresh.
///
/// @param[in,out] dialog the dialog, its chosen settings after the event
/// @param before the chosen settings before the event
/// @return DialogAction::changed when they differ, else DialogAction::redraw
DialogAction changed_or_redraw(Dialog& dialog, const EngineSettings& before) noexcept {
    if (dialog.chosen.hardware_acceleration > before.hardware_acceleration)
        ++dialog.forget_renderer_failures;
    return before == dialog.chosen ? DialogAction::redraw : DialogAction::changed;
}

/// Returns the model of the settings the dialog shows, which its rows change.
///
/// @param[in,out] dialog the dialog
/// @return the model of its chosen settings
layout::SettingsModel chosen_model(Dialog& dialog) noexcept {
    return {&dialog.chosen, &dialog};
}

/// Returns a row as the kit's events read it: its control and the width of
/// its strip's levels.
///
/// @param row the row, placed
/// @return the kit's placed row
kit::PlacedRow placed_row(const layout::Row& row) {
    kit::PlacedRow placed;
    placed.control = row.control;
    placed.control_area = row.control_area;
    placed.view.kind = layout::kind_of(row.setting);
    placed.view.control_width = layout::row_spec(row.setting).control_width;
    return placed;
}

/// Notes that a slider's knob moved: Screen size shows the size chosen from
/// then on, no longer the window's own.
///
/// @param[in,out] dialog the dialog
/// @param setting the slider's setting
void knob_moved(Dialog& dialog, Setting setting) noexcept {
    if (setting == Setting::screen_size)
        dialog.window_screen_size.reset();
}

/// Moves a row's control one step down or up through its row spec: a
/// switch to Off or On, a level strip one level, a drop-down one choice, as
/// the kit steps them; a slider to the stop next to the one its value is
/// nearest, held to its ends, so that a value between two stops at an end
/// moves onto the end stop.
///
/// @param[in,out] dialog the dialog, whose chosen settings change
/// @param setting the row's setting
/// @param up true for a step up
void step(Dialog& dialog, Setting setting, bool up) {
    const auto& spec = layout::row_spec(setting);
    layout::SettingsModel model = chosen_model(dialog);
    if (spec.kind != kit::RowKind::slider) {
        static_cast<void>(kit::step(spec, model, up));
        return;
    }
    kit::set_row_index(spec, model, kit::row_index(spec, model) + (up ? 1 : -1));
    knob_moved(dialog, setting);
}

/// An open drop-down list, as it lies now.
struct OpenList {
    const layout::Row* row{};  ///< the drop-down's row
    layout::SourceRect rect{}; ///< the list, its border included
    std::size_t choices{};     ///< the items it offers
    int32_t shown{};           ///< the items it shows at once
};

/// Returns the open list, placed by its row.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @return the list; nothing while none is open or its row is not shown
std::optional<OpenList> open_list(const Dialog& dialog, const layout::ScrolledRows& open) {
    if (dialog.open_list == no_control)
        return std::nullopt;
    const layout::Row* row = row_of(open.rows, dialog.open_list);
    if (row == nullptr || layout::kind_of(row->setting) != kit::RowKind::choice ||
        row->lock != Lock::none)
        return std::nullopt;
    const auto choices = static_cast<std::size_t>(
        kit::row_count(layout::row_spec(row->setting), layout::reading(dialog.chosen, dialog))
    );
    return OpenList{
        row,
        layout::choice_list(row->control_area, choices),
        choices,
        layout::shown_choices(choices)
    };
}

/// Returns the item of an open list under a point.
///
/// @param dialog the dialog, whose first shown item counts
/// @param list the list
/// @param x the point's column
/// @param y the point's row
/// @return the item, from 0; -1 for a point on no item
int32_t list_item_at(const Dialog& dialog, const OpenList& list, int32_t x, int32_t y) noexcept {
    for (int32_t shown = 0; shown < list.shown; ++shown)
        if (contains(layout::choice_item(list.rect, shown), x, y)) {
            const int32_t item = dialog.list_first + shown;
            return item < static_cast<int32_t>(list.choices) ? item : -1;
        }
    return -1;
}

/// Closes the open list, choosing nothing.
///
/// @param[in,out] dialog the dialog
void close_list(Dialog& dialog) noexcept {
    dialog.open_list = no_control;
    dialog.list_pressed = -1;
}

/// Scrolls an open list the least that shows an item.
///
/// @param[in,out] dialog the dialog
/// @param list the list
/// @param item the item, from 0
void show_list_item(Dialog& dialog, const OpenList& list, int32_t item) noexcept {
    if (item < dialog.list_first)
        dialog.list_first = item;
    else if (item >= dialog.list_first + list.shown)
        dialog.list_first = item - list.shown + 1;
    dialog.list_first = std::clamp(
        dialog.list_first, int32_t{0}, std::max(static_cast<int32_t>(list.choices) - list.shown, 0)
    );
}

/// Opens a drop-down's list, marking its chosen item and showing it.
///
/// @param[in,out] dialog the dialog
/// @param row the drop-down's row
/// @return DialogAction::redraw
DialogAction open_choices(Dialog& dialog, const layout::Row& row) {
    const auto& spec = layout::row_spec(row.setting);
    const layout::SettingsModel model = layout::reading(dialog.chosen, dialog);
    const auto choices = static_cast<std::size_t>(kit::row_count(spec, model));
    const OpenList list{
        &row,
        layout::choice_list(row.control_area, choices),
        choices,
        layout::shown_choices(choices)
    };
    dialog.open_list = row.control;
    dialog.list_pressed = -1;
    dialog.list_first = 0;
    dialog.list_marked = kit::row_index(spec, model);
    show_list_item(dialog, list, dialog.list_marked);
    return DialogAction::redraw;
}

/// Chooses an open list's item and closes the list.
///
/// @param[in,out] dialog the dialog
/// @param list the list
/// @param item the item, from 0
/// @return DialogAction::changed when the choice moved, else DialogAction::redraw
DialogAction choose(Dialog& dialog, const OpenList& list, int32_t item) {
    const Setting setting = list.row->setting;
    close_list(dialog);
    const EngineSettings before = dialog.chosen;
    layout::SettingsModel model = chosen_model(dialog);
    static_cast<void>(kit::choose(layout::row_spec(setting), model, item));
    return changed_or_redraw(dialog, before);
}

/// Takes a key while a drop-down list is open: Up and Down mark the item
/// above or below, Page Up and Page Down a list's height of items away, Home
/// and End the first and the last; Enter and Space choose the marked item;
/// Escape closes the list; Tab and Shift+Tab close it and move the focus.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows
/// @param list the open list
/// @param key the key
/// @return what the key asks of the host
DialogAction
list_key(Dialog& dialog, layout::ScrolledRows& open, const OpenList& list, DialogKey key) {
    const int32_t last = static_cast<int32_t>(list.choices) - 1;
    int32_t marked = dialog.list_marked;
    switch (key) {
    case DialogKey::up:
        --marked;
        break;
    case DialogKey::down:
        ++marked;
        break;
    case DialogKey::page_up:
        marked -= list.shown;
        break;
    case DialogKey::page_down:
        marked += list.shown;
        break;
    case DialogKey::home:
        marked = 0;
        break;
    case DialogKey::end:
        marked = last;
        break;
    case DialogKey::enter:
    case DialogKey::space:
        return choose(dialog, list, std::clamp(marked, int32_t{0}, last));
    case DialogKey::escape:
        close_list(dialog);
        return DialogAction::redraw;
    case DialogKey::tab:
    case DialogKey::back_tab:
        close_list(dialog);
        return move_focus(dialog, open, key == DialogKey::tab);
    default:
        return DialogAction::none;
    }
    marked = std::clamp(marked, int32_t{0}, last);
    if (marked == dialog.list_marked)
        return DialogAction::none;
    dialog.list_marked = marked;
    show_list_item(dialog, list, marked);
    return DialogAction::redraw;
}

/// Returns the question's button under a point.
///
/// @param dialog the dialog, its question showing
/// @param x the point's column
/// @param y the point's row
/// @return question_yes_control, question_no_control, or no_control for neither
int32_t question_button_at(const Dialog& dialog, int32_t x, int32_t y) noexcept {
    if (contains(layout::question_yes_rect(dialog), x, y))
        return question_yes_control;
    if (contains(layout::question_no_rect(dialog), x, y))
        return question_no_control;
    return no_control;
}

/// Answers the question over Mods and puts it away: SWITCH makes the mod
/// offered the Mod setting and asks the host to switch to it; ROLL BACK asks
/// the host to roll the row's folder back; CANCEL leaves everything as it
/// was.
///
/// @param[in,out] dialog the dialog
/// @param yes the answer: SWITCH
/// @return DialogAction::switch_mod for SWITCH, roll_back_mod for ROLL BACK,
///         else DialogAction::redraw
DialogAction answer_question(Dialog& dialog, bool yes) {
    const int32_t offered = dialog.switch_question;
    const ModQuestion asked = dialog.mod_question;
    dialog.switch_question = no_question;
    dialog.mod_question = ModQuestion::switch_mod;
    dialog.question_marks_no = false;
    dialog.hovered = no_control;
    dialog.pressed = no_control;
    if (!yes)
        return DialogAction::redraw;
    if (asked == ModQuestion::roll_back) {
        if (offered < 0 || static_cast<std::size_t>(offered) >= dialog.mod_folders.size())
            return DialogAction::redraw;
        dialog.roll_back_folder = dialog.mod_folders[static_cast<std::size_t>(offered)];
        return DialogAction::roll_back_mod;
    }
    if (offered >= 0 && static_cast<std::size_t>(offered) < dialog.mod_folders.size())
        dialog.chosen.mod_folder = dialog.mod_folders[static_cast<std::size_t>(offered)];
    else
        dialog.chosen.mod_folder.clear();
    return DialogAction::switch_mod;
}

/// Asks the Switch Mod question for a row of Mods; the row of the mod
/// played, and every row while the page is locked, asks nothing.
///
/// @param[in,out] dialog the dialog
/// @param open Mods' rows
/// @param control the row's control
/// @return DialogAction::redraw when the question shows, else DialogAction::none
DialogAction ask_to_switch(Dialog& dialog, const layout::ScrolledRows& open, int32_t control) {
    const layout::Row* row = row_of(open.rows, control);
    if (row == nullptr || row->lock != Lock::none)
        return DialogAction::none;
    const auto rows = mod_rows(dialog);
    const auto index = static_cast<std::size_t>(control - first_row_control);
    if (index >= rows.size() || rows[index].playing)
        return DialogAction::none;
    dialog.switch_question = rows[index].offered;
    dialog.mod_question = ModQuestion::switch_mod;
    dialog.question_marks_no = false;
    dialog.hovered = no_control;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::redraw;
}

/// Asks the Roll Back Mod question for a row of Mods whose folder keeps an
/// earlier version; every row while the page is locked asks nothing.
///
/// @param[in,out] dialog the dialog
/// @param open Mods' rows
/// @param index the row's place
/// @return DialogAction::redraw when the question shows, else DialogAction::none
DialogAction ask_to_roll_back(Dialog& dialog, const layout::ScrolledRows& open, std::size_t index) {
    if (index >= open.rows.rows.size() || open.rows.rows[index].lock != Lock::none)
        return DialogAction::none;
    const auto rows = mod_rows(dialog);
    if (index >= rows.size() || !layout::offers_roll_back(dialog, rows[index]))
        return DialogAction::none;
    dialog.switch_question = rows[index].offered;
    dialog.mod_question = ModQuestion::roll_back;
    dialog.question_marks_no = false;
    dialog.hovered = no_control;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::redraw;
}

/// Takes a key while the question shows: Y answers Yes; N and Escape answer
/// No; Enter and Space answer the marked button; Left marks No, Right marks
/// Yes, and Tab and Shift+Tab move the mark to the other button.
///
/// @param[in,out] dialog the dialog
/// @param key the key
/// @return what the key asks of the host
DialogAction question_key(Dialog& dialog, DialogKey key) {
    bool marks_no = dialog.question_marks_no;
    switch (key) {
    case DialogKey::yes:
        return answer_question(dialog, true);
    case DialogKey::no:
    case DialogKey::escape:
        return answer_question(dialog, false);
    case DialogKey::enter:
    case DialogKey::space:
        return answer_question(dialog, !dialog.question_marks_no);
    case DialogKey::left:
        marks_no = true;
        break;
    case DialogKey::right:
        marks_no = false;
        break;
    case DialogKey::tab:
    case DialogKey::back_tab:
        marks_no = !marks_no;
        break;
    default:
        return DialogAction::none;
    }
    if (marks_no == dialog.question_marks_no)
        return DialogAction::none;
    dialog.question_marks_no = marks_no;
    return DialogAction::redraw;
}

/// Resets every setting the dialog can change to its default. Each locked
/// setting, found through its row's lock on every section the dialog lists,
/// keeps its value. A dialog of the engine's settings keeps the mod
/// options, and each press there also asks for the graphics card to be
/// tried afresh; a dialog of a mod's options resets the mod options alone,
/// up to the mod's most snap radii. Either reports a change even when no
/// setting moved.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::changed
DialogAction restore_defaults(Dialog& dialog) {
    const EngineSettings before = dialog.chosen;
    if (dialog.kind == DialogKind::mod_options) {
        ModOptions options = dialog.defaults.mod_options;
        options.mex_snap_most = before.mod_options.mex_snap_most;
        options.wreck_snap_most = before.mod_options.wreck_snap_most;
        options.mex_snap_radius = std::min(options.mex_snap_radius, options.mex_snap_most);
        options.wreck_snap_radius = std::min(options.wreck_snap_radius, options.wreck_snap_most);
        if (dialog.locks.mex_snap != Lock::none)
            options.mex_snap_radius = before.mod_options.mex_snap_radius;
        if (dialog.locks.wreck_snap != Lock::none)
            options.wreck_snap_radius = before.mod_options.wreck_snap_radius;
        dialog.chosen.mod_options = options;
        dialog.restored = true;
        return DialogAction::changed;
    }
    if (dialog.kind == DialogKind::language_text) {
        // Language alone: only its settings go back to their defaults, each
        // locked one kept.
        const auto language = layout::section_settings(Page::language, dialog.section_hooks);
        for (const Setting setting : language)
            if (layout::section_lock(dialog.locks, setting, dialog.section_hooks) == Lock::none)
                layout::copy_row(
                    setting, chosen_model(dialog), layout::reading(dialog.defaults, dialog)
                );
        dialog.restored = true;
        return DialogAction::changed;
    }
    EngineSettings restored = dialog.defaults;
    restored.mod_options = before.mod_options;
    // The overrides stay: Restore profile values clears them. The mod
    // changes only through the Switch Mod question.
    restored.hack_overrides = before.hack_overrides;
    restored.picked_mod_folder = before.picked_mod_folder;
    restored.mod_folder = before.mod_folder;
    // The backups switch changes only where the dialog lists it.
    if (!dialog.game_files)
        restored.game_files_backed_up = before.game_files_backed_up;
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        const auto settings = layout::section_settings(page, dialog.section_hooks);
        for (const Setting setting : settings) {
            if (layout::section_lock(dialog.locks, setting, dialog.section_hooks) != Lock::none)
                layout::copy_row(setting, {&restored, &dialog}, layout::reading(before, dialog));
        }
    }
    dialog.chosen = restored;
    dialog.restored = true;
    ++dialog.forget_renderer_failures;
    return DialogAction::changed;
}

/// Closes the dialog keeping what it shows.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::accepted
DialogAction accept(Dialog& dialog) noexcept {
    dialog.open_list = no_control;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::accepted;
}

/// Closes the dialog putting back what it opened with.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::cancelled
DialogAction cancel(Dialog& dialog) noexcept {
    dialog.open_list = no_control;
    dialog.chosen = dialog.opened;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::cancelled;
}

/// Shows a section.
///
/// @param[in,out] dialog the dialog
/// @param page the section
/// @return DialogAction::redraw
DialogAction show_page(Dialog& dialog, Page page) noexcept {
    dialog.open_list = no_control;
    dialog.list_pressed = -1;
    dialog.page = page;
    dialog.wheel_rows = 0.0F;
    if (dialog.focused >= first_row_control)
        dialog.focused = page_control(page);
    return DialogAction::redraw;
}

/// Asks the host to open one of Your files' folders.
///
/// @param[in,out] dialog the dialog
/// @param button the button pressed, which the keys then mark
/// @return DialogAction::open_folder
DialogAction open_folder(Dialog& dialog, FolderButton button) noexcept {
    dialog.folder_marked = button;
    dialog.folder_to_open = button;
    return DialogAction::open_folder;
}

/// Takes what a row's button asks for: MANAGE… the Game files screen, a
/// button of Your files its folder.
///
/// @param[in,out] dialog the dialog
/// @param action the button's action (manage_game_files_action, or
///     first_folder_action and its place)
/// @return what it asks of the host
DialogAction row_action(Dialog& dialog, kit::ActionId action) noexcept {
    if (action == layout::manage_game_files_action)
        return DialogAction::manage_game_files;
    const kit::ActionId button = action - layout::first_folder_action;
    if (button < 0 || button >= static_cast<kit::ActionId>(folder_button_count))
        return DialogAction::none;
    return open_folder(dialog, static_cast<FolderButton>(button));
}

/// Presses a button, flips a switch or opens a drop-down's list, as Space
/// or a click does; in Developer's list, opens or closes an area or a hack.
///
/// @param[in,out] dialog the dialog
/// @param open the open section's rows
/// @param control the control
/// @return what it asks of the host
DialogAction activate(Dialog& dialog, const layout::ScrolledRows& open, int32_t control) {
    if (control == restore_control)
        return restore_defaults(dialog);
    if (control == cancel_control)
        return cancel(dialog);
    if (control == ok_control)
        return accept(dialog);
    // A section's entry, by its number: a dialog without Touch has no
    // entry numbered as Touch.
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller))
        if (control == page_control(page))
            return show_page(dialog, page);
    if (layout::developer_page(dialog)) {
        if (control == active_only_control)
            return developer::set_active_only(dialog, !dialog.developer.active_only);
        if (control == restore_profile_control)
            return developer::restore_profile_values(dialog);
        if (const layout::ListRow* row = layout::list_row(open.list, control))
            return developer::activate(dialog, *row);
    }
    if (layout::mods_page(dialog)) {
        if (control == layout::mods_folder_control(open.rows))
            return dialog.locks.mod == Lock::none ? open_folder(dialog, FolderButton::mods)
                                                  : DialogAction::none;
        if (const int32_t row = layout::roll_back_row(open.rows, control); row >= 0)
            return ask_to_roll_back(dialog, open, static_cast<std::size_t>(row));
        return ask_to_switch(dialog, open, control);
    }
    // A row takes Space as its row spec says: a switch flips, a drop-down
    // opens its list, MANAGE… and the marked button of Your files ask the
    // host to act; a slider, a strip and a row of text take nothing.
    const layout::Row* row = row_of(open.rows, control);
    if (row == nullptr || row->lock != Lock::none)
        return DialogAction::none;
    const auto& spec = layout::row_spec(row->setting);
    const EngineSettings before = dialog.chosen;
    layout::SettingsModel model = chosen_model(dialog);
    const kit::RowResult result =
        kit::activate(spec, model, static_cast<std::size_t>(dialog.folder_marked));
    if (result.event == kit::RowEvent::open_menu)
        return open_choices(dialog, *row);
    if (result.event == kit::RowEvent::action)
        return row_action(dialog, result.action);
    if (spec.kind != kit::RowKind::toggle)
        return DialogAction::none;
    return changed_or_redraw(dialog, before);
}

/// Sets a slider to the stop under a column.
///
/// @param[in,out] dialog the dialog
/// @param row the slider's row
/// @param column the column
/// @return what it asks of the host
DialogAction drag_to(Dialog& dialog, const layout::Row& row, int32_t column) {
    const EngineSettings before = dialog.chosen;
    layout::SettingsModel model = chosen_model(dialog);
    static_cast<void>(kit::drag(layout::row_spec(row.setting), model, placed_row(row), column));
    knob_moved(dialog, row.setting);
    return changed_or_redraw(dialog, before);
}

/// A part of the dialog where a press acts on a control.
struct PressArea {
    int32_t control{no_control}; ///< the control
    layout::SourceRect rect{};   ///< where it answers a press
};

/// Returns the part two rectangles share.
///
/// @param a a rectangle
/// @param b another
/// @return the rectangle in both; zero wide or high when they do not meet
layout::SourceRect common_part(const layout::SourceRect& a, const layout::SourceRect& b) noexcept {
    const int32_t left = std::max(a.x, b.x);
    const int32_t top = std::max(a.y, b.y);
    const int32_t right = std::min(a.x + a.width, b.x + b.width);
    const int32_t bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(right - left, int32_t{0}), std::max(bottom - top, int32_t{0})};
}

/// Returns the parts where a press acts on a control now, as control_at
/// tries them: on Developer its list's rows that take input, Show Active
/// Only and Restore profile values while it is enabled; the open section's
/// unlocked rows where the view shows them; the scroll bar while the
/// section scrolls; the footer's buttons; the sections' entries.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @return the parts; a row the view hides has none
std::vector<PressArea> press_areas(const Dialog& dialog, const layout::ScrolledRows& open) {
    std::vector<PressArea> areas;
    const auto add = [&areas](int32_t control, const layout::SourceRect& rect) {
        if (rect.width > 0 && rect.height > 0)
            areas.push_back(PressArea{control, rect});
    };
    if (layout::developer_page(dialog)) {
        for (const layout::ListRow& row : open.list.rows)
            if (list_row_takes_input(row))
                add(row.control, common_part(row.control_area, layout::developer_view));
        add(active_only_control, layout::active_only_switch);
        if (developer::restore_profile_enabled(dialog))
            add(restore_profile_control, layout::restore_profile_button);
    }
    // Mods' rows answer in its list's own view, under which OPEN MODS
    // FOLDER stands; every other section's rows in the view under the
    // heading.
    const layout::SourceRect& rows_view = layout::mods_page(dialog) ? open.area.view : layout::view;
    const std::vector<bool> roll_backs = roll_back_rows(dialog, open);
    for (std::size_t index = 0; index < roll_backs.size(); ++index)
        if (roll_backs[index] && open.rows.rows[index].lock == Lock::none)
            add(layout::roll_back_control(open.rows, index),
                common_part(layout::roll_back_button(open.rows.rows[index]), rows_view));
    for (const layout::Row& row : open.rows.rows)
        if (row.lock == Lock::none)
            add(row.control, common_part(row.control_area, rows_view));
    if (layout::mods_page(dialog) && dialog.locks.mod == Lock::none)
        add(layout::mods_folder_control(open.rows), layout::mods_folder_button);
    if (open.limit > 0)
        add(scroll_bar_control, open.area.hit);
    for (const int32_t control : {restore_control, cancel_control, ok_control})
        add(control, layout::footer_button(control));
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller))
        add(page_control(page), layout::dialog_list_item(dialog, page));
    return areas;
}

/// A point of the dialog, in source pixels.
struct SourcePoint {
    int32_t x{}; ///< column
    int32_t y{}; ///< row
};

/// Returns a rectangle's pixel nearest a point.
///
/// @param rect the rectangle, not empty
/// @param x the point's column
/// @param y the point's row
/// @return the point itself inside the rectangle, else the nearest pixel on its edge
SourcePoint nearest_pixel(const layout::SourceRect& rect, int32_t x, int32_t y) noexcept {
    return {
        std::clamp(x, rect.x, rect.x + rect.width - 1),
        std::clamp(y, rect.y, rect.y + rect.height - 1),
    };
}

/// Returns the square of the distance between two points.
///
/// @param a a point
/// @param b another
/// @return the distance squared, in source pixels squared
int64_t distance_squared(SourcePoint a, SourcePoint b) noexcept {
    const int64_t across = int64_t{a.x} - b.x;
    const int64_t down = int64_t{a.y} - b.y;
    return across * across + down * down;
}

/// Returns where a finger's press lands: the finger's own point over a
/// control, else the nearest point of the nearest control within reach;
/// while a drop-down list is open, the finger's point over one of its
/// items, else the nearest point of the nearest item within reach; and
/// while the Switch Mod question shows, the same of its two buttons.
///
/// @param dialog the dialog
/// @param open the open section's rows
/// @param x the finger's column
/// @param y the finger's row
/// @param reach how far a control may lie from the finger, in source pixels
/// @return the point the press takes; the finger's own with nothing within reach
SourcePoint finger_target(
    const Dialog& dialog, const layout::ScrolledRows& open, int32_t x, int32_t y, int32_t reach
) {
    const SourcePoint finger{x, y};
    if (reach <= 0)
        return finger;
    const int64_t within = int64_t{reach} * reach;
    std::optional<SourcePoint> best;
    int64_t best_distance = 0;
    const auto consider = [&](SourcePoint candidate) {
        const int64_t distance = distance_squared(candidate, finger);
        if (distance <= within && (!best || distance < best_distance)) {
            best = candidate;
            best_distance = distance;
        }
    };
    // The Switch Mod question takes every press: the finger's point over one
    // of its buttons, else the nearest point of the nearer within reach.
    if (dialog.switch_question != no_question) {
        if (question_button_at(dialog, x, y) != no_control)
            return finger;
        consider(nearest_pixel(layout::question_yes_rect(dialog), x, y));
        consider(nearest_pixel(layout::question_no_rect(dialog), x, y));
        return best.value_or(finger);
    }
    if (const auto list = open_list(dialog, open)) {
        if (list_item_at(dialog, *list, x, y) >= 0)
            return finger;
        for (int32_t shown = 0; shown < list->shown; ++shown)
            if (dialog.list_first + shown < static_cast<int32_t>(list->choices))
                consider(nearest_pixel(layout::choice_item(list->rect, shown), x, y));
        return best.value_or(finger);
    }
    if (control_at(dialog, open, x, y) != no_control)
        return finger;
    // A part another control covers at its nearest pixel is passed over.
    for (const PressArea& area : press_areas(dialog, open)) {
        const SourcePoint candidate = nearest_pixel(area.rect, x, y);
        if (control_at(dialog, open, candidate.x, candidate.y) == area.control)
            consider(candidate);
    }
    return best.value_or(finger);
}

/// Tells whether a press is held: on a control, or on an open list's item.
///
/// @param dialog the dialog
/// @return true while a press is held
bool press_held(const Dialog& dialog) noexcept {
    return dialog.pressed != no_control || dialog.list_pressed >= 0;
}

} // namespace

std::span<const Page>
dialog_pages(DialogKind kind, bool touch, bool game_files, bool controller) noexcept {
    switch (kind) {
    case DialogKind::engine:
        break;
    case DialogKind::mod_options:
        return layout::kModPages;
    case DialogKind::language_text:
        return layout::kLanguageTextPages;
    }
    const layout::EnginePageList& list =
        layout::kEnginePageLists[layout::engine_page_list_index(touch, controller, game_files)];
    return std::span<const Page>(list.pages.data(), list.count);
}

void open_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page,
    const AccelerationStatus& acceleration,
    uint16_t highest_offered_unit,
    const ModOffer& mods,
    std::span<const oa::data::mod_profile::HackState> profile_hacks,
    const oa::data::languages::Language* system_language,
    bool touch,
    bool game_files,
    bool controller
) {
    dialog = Dialog{};
    dialog.touch = touch;
    dialog.game_files = game_files;
    dialog.controller = controller;
    dialog.system_language = system_language;
    dialog.highest_offered_unit = highest_offered_unit;
    dialog.mod_names.assign(mods.names.begin(), mods.names.end());
    dialog.mod_folders.assign(mods.folders.begin(), mods.folders.end());
    dialog.mod_details.assign(mods.details.begin(), mods.details.end());
    dialog.playing_mod_folder = std::string(mods.playing);
    dialog.opened = current;
    dialog.chosen = current;
    dialog.defaults = defaults;
    dialog.locks = locks;
    dialog.acceleration = acceleration;
    dialog.version = std::string(version);
    // Touch, Controller and Game files show only while they are listed.
    dialog.page = (page == Page::touch && !touch) || (page == Page::game_files && !game_files) ||
                          (page == Page::controller && !controller)
                      ? dialog_pages(DialogKind::engine).front()
                      : page;
    if (profile_hacks.empty())
        dialog.developer.profile = oa::data::mod_profile::base_hack_states();
    else
        dialog.developer.profile.assign(profile_hacks.begin(), profile_hacks.end());
    dialog.developer.areas_open.assign(developer_areas().size(), 0);
    dialog.developer.hacks_open.assign(oa::data::mod_profile::standard_hacks().size(), 0);
}

void open_mod_options_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page
) {
    open_dialog(dialog, current, defaults, locks, version, page);
    dialog.kind = DialogKind::mod_options;
    const auto pages = dialog_pages(DialogKind::mod_options);
    if (std::find(pages.begin(), pages.end(), page) == pages.end())
        dialog.page = pages.front();
}

void open_language_text_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    const oa::data::languages::Language* system_language
) {
    open_dialog(
        dialog,
        current,
        defaults,
        locks,
        version,
        Page::language,
        {},
        highest_unit_limit,
        {},
        {},
        system_language
    );
    dialog.kind = DialogKind::language_text;
}

DialogAction
set_acceleration_status(Dialog& dialog, const AccelerationStatus& acceleration) noexcept {
    if (dialog.acceleration == acceleration)
        return DialogAction::none;
    dialog.acceleration = acceleration;
    return DialogAction::redraw;
}

DialogAction set_touch_controls(Dialog& dialog, bool touch) noexcept {
    if (dialog.touch == touch)
        return DialogAction::none;
    dialog.touch = touch;
    if (dialog.kind != DialogKind::engine || touch)
        return DialogAction::redraw;
    // Touch's entry and rows leave the dialog: what pointed at them points
    // at nothing, and Touch's section gives way to the first.
    const int32_t entry = page_control(Page::touch);
    for (int32_t* control : {&dialog.hovered, &dialog.pressed, &dialog.focused})
        if (*control == entry || (dialog.page == Page::touch && *control >= first_row_control))
            *control = no_control;
    if (dialog.page == Page::touch) {
        static_cast<void>(show_page(dialog, dialog_pages(DialogKind::engine).front()));
        dialog.dragging = false;
    }
    return DialogAction::redraw;
}

DialogAction set_controller_section(Dialog& dialog, bool controller, bool steam_input) noexcept {
    if (dialog.controller == controller && dialog.steam_input == steam_input)
        return DialogAction::none;
    const bool showing = dialog.kind == DialogKind::engine && dialog.page == Page::controller;
    if (dialog.steam_input != steam_input && showing && dialog.controller && controller) {
        // The notice comes or goes above Controller's rows: each row's
        // control moves by one, the focus with its row; a hover, a press or
        // an open list on a row lets go.
        const int32_t moved = steam_input ? 1 : -1;
        if (dialog.focused >= first_row_control)
            dialog.focused = std::max(dialog.focused + moved, first_row_control);
        for (int32_t* control : {&dialog.hovered, &dialog.pressed})
            if (*control >= first_row_control)
                *control = no_control;
        if (dialog.open_list >= first_row_control) {
            dialog.open_list = no_control;
            dialog.list_pressed = -1;
        }
        dialog.dragging = false;
    }
    dialog.steam_input = steam_input;
    if (dialog.controller == controller)
        return DialogAction::redraw;
    dialog.controller = controller;
    if (dialog.kind != DialogKind::engine || controller)
        return DialogAction::redraw;
    // Controller's entry and rows leave the dialog: what pointed at them
    // points at nothing, and Controller's section gives way to the first.
    const int32_t entry = page_control(Page::controller);
    for (int32_t* control : {&dialog.hovered, &dialog.pressed, &dialog.focused})
        if (*control == entry || (showing && *control >= first_row_control))
            *control = no_control;
    if (showing) {
        static_cast<void>(show_page(dialog, dialog_pages(DialogKind::engine).front()));
        dialog.dragging = false;
    }
    return DialogAction::redraw;
}

DialogAction dialog_pointer_move(Dialog& dialog, int32_t x, int32_t y) {
    // A finger's held press moves as it was moved to the control it took.
    if (press_held(dialog)) {
        x += dialog.finger_shift_x;
        y += dialog.finger_shift_y;
    }
    note_pointer(dialog, x, y);
    // The question hovers its own buttons only.
    if (dialog.switch_question != no_question) {
        const int32_t button = question_button_at(dialog, x, y);
        if (button == dialog.hovered)
            return DialogAction::none;
        dialog.hovered = button;
        return DialogAction::redraw;
    }
    layout::ScrolledRows open = layout::open_rows(dialog);
    // An open list marks the item under the pointer.
    if (const auto list = open_list(dialog, open)) {
        const int32_t item = list_item_at(dialog, *list, x, y);
        if (item < 0 || item == dialog.list_marked)
            return DialogAction::none;
        dialog.list_marked = item;
        return DialogAction::redraw;
    }
    if (dialog.dragging) {
        // The thumb follows the pointer's row only, and the offset the thumb.
        if (dialog.pressed == scroll_bar_control)
            return scroll_to(
                dialog,
                open,
                layout::scroll_at(
                    open.area, y - dialog.scroll_grab, open.limit, open.content_height
                )
            );
        if (const layout::ListRow* row = layout::list_row(open.list, dialog.pressed))
            return developer::drag_to(dialog, *row, x);
        const layout::Row* row = row_of(open.rows, dialog.pressed);
        if (row != nullptr)
            return drag_to(dialog, *row, x);
    }
    const int32_t hovered = control_at(dialog, open, x, y);
    // Your files lights the button under the pointer.
    const layout::Row* buttons = row_of(open.rows, hovered);
    const std::size_t button =
        buttons != nullptr && layout::kind_of(buttons->setting) == kit::RowKind::buttons
            ? layout::folder_button_at(buttons->control_area, x, y)
            : folder_button_count;
    if (hovered == dialog.hovered && button == dialog.folder_hovered)
        return DialogAction::none;
    dialog.hovered = hovered;
    dialog.folder_hovered = button;
    return DialogAction::redraw;
}

DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y) {
    dialog.finger_shift_x = 0;
    dialog.finger_shift_y = 0;
    note_pointer(dialog, x, y);
    // The question takes the press: on a button it holds the button.
    if (dialog.switch_question != no_question) {
        const int32_t button = question_button_at(dialog, x, y);
        dialog.hovered = button;
        dialog.pressed = button;
        dialog.dragging = false;
        return button == no_control ? DialogAction::none : DialogAction::redraw;
    }
    layout::ScrolledRows open = layout::open_rows(dialog);
    // An open list takes the press: on an item it holds the item; anywhere
    // else it closes the list, and the press does nothing more.
    if (const auto list = open_list(dialog, open)) {
        dialog.pressed = no_control;
        dialog.dragging = false;
        const int32_t item = list_item_at(dialog, *list, x, y);
        if (item >= 0) {
            dialog.list_pressed = item;
            dialog.list_marked = item;
        } else {
            close_list(dialog);
        }
        return DialogAction::redraw;
    }
    close_list(dialog);
    const int32_t control = control_at(dialog, open, x, y);
    dialog.hovered = control;
    dialog.pressed = control;
    dialog.dragging = false;
    if (control == no_control)
        return DialogAction::none;
    if (control == scroll_bar_control) {
        // On the thumb, the press grabs it at the row pressed; on the well,
        // the thumb's middle jumps to the pointer and the drag starts there.
        // The scroll bar takes no focus, so the focus stays where it is.
        dialog.dragging = true;
        const layout::SourceRect thumb =
            layout::scroll_thumb(open.area, open.scroll, open.limit, open.content_height);
        if (y >= thumb.y && y < thumb.y + thumb.height) {
            dialog.scroll_grab = y - thumb.y;
            return DialogAction::redraw;
        }
        dialog.scroll_grab = thumb.height / 2;
        static_cast<void>(scroll_to(
            dialog,
            open,
            layout::scroll_at(open.area, y - dialog.scroll_grab, open.limit, open.content_height)
        ));
        return DialogAction::redraw;
    }
    if (dialog.focused != no_control)
        dialog.focused = control;
    if (const layout::ListRow* row = layout::list_row(open.list, control)) {
        // A press on a slider of the list moves its knob and drags it.
        if (row->kind != layout::ListRowKind::slider)
            return DialogAction::redraw;
        dialog.dragging = true;
        return developer::drag_to(dialog, *row, x);
    }
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr && layout::kind_of(row->setting) == kit::RowKind::slider) {
        dialog.dragging = true;
        const DialogAction action = drag_to(dialog, *row, x);
        return action;
    }
    // A press on Your files holds the button under it.
    if (row != nullptr && layout::kind_of(row->setting) == kit::RowKind::buttons)
        dialog.folder_hovered = layout::folder_button_at(row->control_area, x, y);
    return DialogAction::redraw;
}

DialogAction dialog_finger_down(Dialog& dialog, int32_t x, int32_t y, int32_t reach) {
    const SourcePoint target = finger_target(dialog, layout::open_rows(dialog), x, y, reach);
    const DialogAction action = dialog_pointer_down(dialog, target.x, target.y);
    if (press_held(dialog)) {
        dialog.finger_shift_x = target.x - x;
        dialog.finger_shift_y = target.y - y;
    }
    return action;
}

DialogAction dialog_pointer_up(Dialog& dialog, int32_t x, int32_t y) {
    // A finger's release lands as its press was moved; the next press
    // starts afresh.
    if (press_held(dialog)) {
        x += dialog.finger_shift_x;
        y += dialog.finger_shift_y;
    }
    dialog.finger_shift_x = 0;
    dialog.finger_shift_y = 0;
    note_pointer(dialog, x, y);
    // A release over the question's button the press held answers it.
    if (dialog.switch_question != no_question) {
        const int32_t pressed = dialog.pressed;
        const int32_t button = question_button_at(dialog, x, y);
        dialog.pressed = no_control;
        dialog.hovered = button;
        if (pressed == no_control)
            return DialogAction::none;
        if (button != pressed)
            return DialogAction::redraw;
        return answer_question(dialog, button == question_yes_control);
    }
    const layout::ScrolledRows open = layout::open_rows(dialog);
    // A release over the list item the press held chooses it.
    if (const auto list = open_list(dialog, open)) {
        const int32_t held = dialog.list_pressed;
        dialog.list_pressed = -1;
        if (held >= 0 && list_item_at(dialog, *list, x, y) == held)
            return choose(dialog, *list, held);
        return held >= 0 ? DialogAction::redraw : DialogAction::none;
    }
    const int32_t pressed = dialog.pressed;
    const bool dragged = dialog.dragging;
    dialog.pressed = no_control;
    dialog.dragging = false;
    dialog.scroll_grab = 0;
    if (pressed == no_control)
        return DialogAction::none;
    const int32_t control = control_at(dialog, open, x, y);
    dialog.hovered = control;
    if (dragged || control != pressed)
        return DialogAction::redraw;
    if (layout::developer_page(dialog)) {
        if (control == active_only_control)
            return developer::set_active_only(
                dialog, x >= layout::active_only_switch.x + layout::active_only_switch.width / 2
            );
        if (const layout::ListRow* row = layout::list_row(open.list, control))
            return developer::release_on(dialog, *row, x);
    }
    // A mod row asks the Switch Mod question; OPEN MODS FOLDER opens it.
    if (layout::mods_page(dialog))
        return activate(dialog, open, control);
    // A row takes the release as its row spec says: a switch's half sets
    // it, a strip's level is chosen, a drop-down opens its list, MANAGE…
    // and the button of Your files the press held ask the host to act.
    const layout::Row* row = row_of(open.rows, control);
    if (row == nullptr)
        return activate(dialog, open, control);
    const auto& spec = layout::row_spec(row->setting);
    if (spec.kind == kit::RowKind::buttons) {
        // A release over the button the press held opens its folder.
        const std::size_t held = dialog.folder_hovered;
        const std::size_t button = layout::folder_button_at(row->control_area, x, y);
        dialog.folder_hovered = button;
        if (button == folder_button_count || button != held)
            return DialogAction::redraw;
    }
    const EngineSettings before = dialog.chosen;
    layout::SettingsModel model = chosen_model(dialog);
    const kit::RowResult result = kit::press(spec, model, placed_row(*row), {x, y});
    if (result.event == kit::RowEvent::open_menu)
        return open_choices(dialog, *row);
    if (result.event == kit::RowEvent::action)
        return row_action(dialog, result.action);
    return changed_or_redraw(dialog, before);
}

DialogAction dialog_key(Dialog& dialog, DialogKey key) {
    if (dialog.switch_question != no_question)
        return question_key(dialog, key);
    layout::ScrolledRows open = layout::open_rows(dialog);
    const layout::Rows& rows = open.rows;
    if (const auto list = open_list(dialog, open))
        return list_key(dialog, open, *list, key);
    close_list(dialog);
    switch (key) {
    case DialogKey::enter:
        return accept(dialog);
    case DialogKey::escape:
        return cancel(dialog);
    case DialogKey::down:
    case DialogKey::tab:
        return move_focus(dialog, open, true);
    case DialogKey::up:
    case DialogKey::back_tab:
        return move_focus(dialog, open, false);
    case DialogKey::page_up:
    case DialogKey::page_down:
    case DialogKey::home:
    case DialogKey::end:
        return scroll_key(dialog, open, key);
    // Y and N answer a question, and do nothing while none shows.
    case DialogKey::yes:
    case DialogKey::no:
        return DialogAction::none;
    default:
        break;
    }
    if (dialog.focused == no_control)
        return move_focus(dialog, open, true);
    // A key that acts on a row brings it into view first, so that the
    // player sees what it changed.
    const DialogAction shown = show_row(dialog, open, dialog.focused);
    const auto or_shown = [shown](DialogAction action) {
        return action == DialogAction::none ? shown : action;
    };
    if (key == DialogKey::space)
        return or_shown(activate(dialog, open, dialog.focused));
    const bool up = key == DialogKey::right;
    if (layout::developer_page(dialog)) {
        if (dialog.focused == active_only_control)
            return or_shown(developer::set_active_only(dialog, up));
        if (const layout::ListRow* row = layout::list_row(open.list, dialog.focused))
            return or_shown(developer::step(dialog, *row, up));
    }
    const layout::Row* row = row_of(rows, dialog.focused);
    const kit::RowKind kind = row != nullptr ? layout::kind_of(row->setting) : kit::RowKind::text;
    if (row != nullptr && row->lock == Lock::none && kind == kit::RowKind::buttons) {
        // Left and Right move Your files' mark along its buttons.
        const auto marked = static_cast<std::size_t>(dialog.folder_marked);
        std::size_t next = marked;
        layout::SettingsModel model = chosen_model(dialog);
        static_cast<void>(kit::step(layout::row_spec(row->setting), model, up, next));
        if (next == marked)
            return shown;
        dialog.folder_marked = static_cast<FolderButton>(next);
        return DialogAction::redraw;
    }
    if (row != nullptr) {
        // A button has no steps.
        if (row->lock != Lock::none || kind == kit::RowKind::value_and_button)
            return shown;
        const EngineSettings before = dialog.chosen;
        step(dialog, row->setting, up);
        return changed_or_redraw(dialog, before);
    }
    // Left and Right move along the footer's buttons.
    constexpr std::array<int32_t, 3> footer{restore_control, cancel_control, ok_control};
    const auto found = std::find(footer.begin(), footer.end(), dialog.focused);
    if (found == footer.end())
        return DialogAction::none;
    const auto at = static_cast<std::size_t>(found - footer.begin());
    const std::size_t next = up ? std::min(at + 1, footer.size() - 1) : (at == 0 ? 0 : at - 1);
    if (next == at)
        return DialogAction::none;
    dialog.focused = footer[next];
    return DialogAction::redraw;
}

DialogAction dialog_wheel(Dialog& dialog, int32_t x, int32_t y, float notches) {
    if (!dialog_contains(x, y) || dialog.pressed != no_control || !std::isfinite(notches) ||
        dialog.switch_question != no_question)
        return DialogAction::none;
    note_pointer(dialog, x, y);
    layout::ScrolledRows open = layout::open_rows(dialog);
    // An open list keeps the section still and scrolls itself, an item a
    // notch, when it holds more items than it shows.
    if (const auto list = open_list(dialog, open)) {
        if (!contains(list->rect, x, y) || static_cast<int32_t>(list->choices) <= list->shown)
            return DialogAction::none;
        const int32_t first = std::clamp(
            dialog.list_first - static_cast<int32_t>(std::lround(notches)),
            int32_t{0},
            static_cast<int32_t>(list->choices) - list->shown
        );
        if (first == dialog.list_first)
            return DialogAction::none;
        dialog.list_first = first;
        return DialogAction::redraw;
    }
    if (open.limit == 0) {
        dialog.wheel_rows = 0.0F;
        return DialogAction::none;
    }
    // Away from the player scrolls towards the top. A turn larger than the
    // section scrolls to its end.
    const float reach = static_cast<float>(open.limit) + 1.0F;
    const float rows = std::clamp(
        dialog.wheel_rows - notches * static_cast<float>(layout::wheel_step), -reach, reach
    );
    const auto whole = static_cast<int32_t>(rows);
    dialog.wheel_rows = rows - static_cast<float>(whole);
    const int32_t next = std::clamp(open.scroll + whole, int32_t{0}, open.limit);
    // What is carried towards an end the section has reached is dropped.
    if ((next == 0 && dialog.wheel_rows < 0.0F) || (next == open.limit && dialog.wheel_rows > 0.0F))
        dialog.wheel_rows = 0.0F;
    return scroll_to(dialog, open, next);
}

DialogAction set_folder_notice(Dialog& dialog, std::string_view reason) {
    if (dialog.folder_notice == reason)
        return DialogAction::none;
    dialog.folder_notice = std::string(reason);
    return DialogAction::redraw;
}

namespace {

/// Tells whether a character separates a path's components.
///
/// @param character the character
/// @return true for '/' and '\\'
bool path_separator(char character) noexcept {
    return character == '/' || character == '\\';
}

} // namespace

std::string path_tail(
    std::string_view path, int32_t width, const std::function<int32_t(std::string_view)>& text_width
) {
    while (path.size() > 1 && path_separator(path.back()))
        path.remove_suffix(1);
    if (text_width(path) <= width)
        return std::string(path);
    const std::string ellipsis(layout::path_ellipsis);
    // The most whole components that fit, the separator before them kept.
    for (std::size_t at = 1; at < path.size(); ++at) {
        if (!path_separator(path[at]))
            continue;
        std::string tail = ellipsis + std::string(path.substr(at));
        if (text_width(tail) <= width)
            return tail;
    }
    // The last component alone is too wide: as much of its end as fits.
    std::size_t last = path.size();
    while (last > 0 && !path_separator(path[last - 1]))
        --last;
    for (std::size_t at = last; at < path.size(); ++at) {
        if ((static_cast<unsigned char>(path[at]) & 0xC0U) == 0x80U)
            continue;
        std::string tail = ellipsis + std::string(path.substr(at));
        if (text_width(tail) <= width)
            return tail;
    }
    return ellipsis;
}

bool dialog_contains(int32_t x, int32_t y) noexcept {
    return x >= 0 && y >= 0 && x < dialog_width && y < dialog_height;
}

namespace {

/// Adds a switch's two halves to the parts, its control on each.
///
/// @param[in,out] parts the parts
/// @param area the switch
/// @param control its control; no_control for one that takes no press
void switch_parts(std::vector<LayoutPart>& parts, const layout::SourceRect& area, int32_t control) {
    const int32_t half = (area.width - 2) / 2;
    parts.push_back(
        LayoutPart{
            {area.x + 1, area.y + 1, half, area.height - 2},
            std::string(layout::off_text),
            DialogFont::small,
            0,
            control,
        }
    );
    parts.push_back(
        LayoutPart{
            {area.x + 1 + half, area.y + 1, half, area.height - 2},
            std::string(layout::on_text),
            DialogFont::small,
            0,
            control,
        }
    );
}

/// Adds the parts under Developer's rows: those of its list's rows that lie
/// wholly in the list's view, and its footer.
///
/// @param dialog the dialog
/// @param open Developer's rows and list (layout::open_rows)
/// @param[in,out] parts the parts
void developer_layout(
    const Dialog& dialog, const layout::ScrolledRows& open, std::vector<LayoutPart>& parts
) {
    const auto text_part =
        [&parts](layout::SourceRect rect, std::string text, DialogFont font, int32_t control) {
            parts.push_back(LayoutPart{rect, std::move(text), font, 0, control});
        };
    // The list's rows, only the parts wholly in its view.
    std::vector<LayoutPart> rows;
    const auto row_text =
        [&rows](layout::SourceRect rect, std::string text, DialogFont font, int32_t control) {
            rows.push_back(LayoutPart{rect, std::move(text), font, 0, control});
        };
    for (const layout::ListRow& row : open.list.rows) {
        const bool takes = row.kind == layout::ListRowKind::area ||
                           row.kind == layout::ListRowKind::hack || !row.locked;
        const int32_t control = takes ? row.control : no_control;
        switch (row.kind) {
        case layout::ListRowKind::area:
            rows.push_back(LayoutPart{row.arrow, {}, DialogFont::regular, 0, no_control});
            row_text(row.label, row.text, DialogFont::regular, control);
            row_text(row.value, row.shown, DialogFont::small, no_control);
            break;
        case layout::ListRowKind::hack:
            rows.push_back(LayoutPart{row.arrow, {}, DialogFont::regular, 0, no_control});
            row_text(row.label, row.text, DialogFont::small, control);
            switch_parts(rows, row.toggle, row.locked ? no_control : row.control);
            break;
        case layout::ListRowKind::id:
        case layout::ListRowKind::text:
        case layout::ListRowKind::scope:
        case layout::ListRowKind::heading:
            row_text(row.label, row.text, DialogFont::small, no_control);
            break;
        case layout::ListRowKind::toggle:
            row_text(row.label, row.text, DialogFont::small, no_control);
            switch_parts(rows, row.control_area, control);
            break;
        case layout::ListRowKind::slider:
            row_text(row.label, row.text, DialogFont::small, no_control);
            row_text(row.value, row.shown, DialogFont::small, no_control);
            rows.push_back(LayoutPart{row.control_area, {}, DialogFont::regular, 0, control});
            break;
        }
    }
    for (LayoutPart& part : rows)
        if (wholly_in(part.rect, layout::developer_view))
            parts.push_back(std::move(part));
    // The list's footer, which never scrolls.
    text_part(
        layout::active_only_label,
        layout::active_only_text(
            active_hack_count(dialog), oa::data::mod_profile::standard_hacks().size()
        ),
        DialogFont::regular,
        no_control
    );
    switch_parts(parts, layout::active_only_switch, active_only_control);
    text_part(
        layout::restore_profile_button,
        std::string(layout::restore_profile_text),
        DialogFont::small,
        developer::restore_profile_enabled(dialog) ? restore_profile_control : no_control
    );
}

} // namespace

namespace {

/// Lists Mods' parts: the lock line while it is locked, each row wholly in
/// the list's view (its badge, title, PLAYING tag, version and
/// description; the row itself the control a press switches with),
/// OPEN MODS FOLDER and the lines under it.
///
/// @param dialog the dialog
/// @param open Mods' rows
/// @param[in,out] parts the parts, which Mods' are added to
/// @param text_width a regular text's width
/// @param small_text_width a small text's width
void mods_layout(
    const Dialog& dialog,
    const layout::ScrolledRows& open,
    std::vector<LayoutPart>& parts,
    const std::function<int32_t(std::string_view)>& text_width,
    const std::function<int32_t(std::string_view)>& small_text_width
) {
    const auto text = [&parts](layout::SourceRect rect, std::string shown, DialogFont font) {
        parts.push_back(LayoutPart{rect, std::move(shown), font, 0, no_control});
    };
    if (dialog.locks.mod != Lock::none) {
        // The reason, beside the padlock, over as many of its two lines as
        // it needs.
        oa::ui::kit::WrapRules rules;
        rules.shorten_word = [&](std::string_view word) {
            return layout::cut_text(word, layout::mods_lock_text.width, small_text_width);
        };
        const auto lines = oa::ui::kit::wrap(
            layout::shown_text(
                dialog.locks.mod == Lock::command_line ? layout::mod_from_command_line_text
                                                       : layout::mod_in_game_text
            ),
            layout::mods_lock_text.width,
            small_text_width,
            rules
        );
        for (std::size_t line = 0; line < lines.size() && line < 2; ++line) {
            layout::SourceRect rect = layout::mods_lock_text;
            rect.y += static_cast<int32_t>(line) * rect.height;
            text(rect, lines[line], DialogFont::small);
        }
    }
    const auto rows = mod_rows(dialog);
    for (std::size_t index = 0; index < open.rows.rows.size() && index < rows.size(); ++index) {
        const layout::Row& row = open.rows.rows[index];
        if (!wholly_in(row.control_area, open.area.view))
            continue;
        const layout::ModRowText shown = layout::mod_row_text(dialog, rows[index]);
        parts.push_back(
            LayoutPart{
                row.control_area,
                {},
                DialogFont::regular,
                0,
                row.lock == Lock::none ? row.control : no_control
            }
        );
        // The row's own parts lie inside it; only the row is listed as its
        // control, and its texts and badge as texts and a mark, and its ROLL
        // BACK as a control of its own.
        static_cast<void>(text_width);
        static_cast<void>(small_text_width);
        if (layout::offers_roll_back(dialog, rows[index]))
            parts.push_back(
                LayoutPart{
                    layout::roll_back_button(row),
                    std::string(layout::shown_text(layout::roll_back_text)),
                    DialogFont::small,
                    0,
                    row.lock == Lock::none ? layout::roll_back_control(open.rows, index)
                                           : no_control
                }
            );
    }
    parts.push_back(
        LayoutPart{
            layout::mods_folder_button,
            std::string(layout::shown_text(layout::open_mods_folder_text)),
            DialogFont::small,
            0,
            dialog.locks.mod == Lock::none ? layout::mods_folder_control(open.rows) : no_control,
        }
    );
    const std::string_view second =
        dialog.folder_notice.empty() ? layout::mods_folders_text[1] : dialog.folder_notice;
    text(
        layout::mods_note_first,
        std::string(layout::shown_text(layout::mods_folders_text[0])),
        DialogFont::small
    );
    text(layout::mods_note_second, std::string(layout::shown_text(second)), DialogFont::small);
}

} // namespace

std::vector<LayoutPart> dialog_layout(const Dialog& dialog, const DialogFonts* fonts) {
    std::vector<LayoutPart> parts;
    // The player's own folder's path is shortened to its place in the fonts, or at
    // an estimated width a character without them.
    const auto text_width = [fonts](std::string_view text) {
        if (fonts != nullptr)
            return dialog_text_width(*fonts, DialogFont::regular, text);
        return static_cast<int32_t>(oa::ui::kit::character_count(text)) * estimated_character_width;
    };
    // Your files' path is shortened to its hint line in the small font.
    const auto small_text_width = [fonts](std::string_view text) {
        if (fonts != nullptr)
            return dialog_text_width(*fonts, DialogFont::small, text);
        return static_cast<int32_t>(oa::ui::kit::character_count(text)) * estimated_character_width;
    };
    // Each text as the dialog shows it, the interface's words in the
    // language shown (layout::shown_text).
    const auto text_part =
        [&parts](
            layout::SourceRect rect, std::string_view text, DialogFont font, int32_t tracking = 0
        ) {
            parts.push_back(
                LayoutPart{rect, std::string(layout::shown_text(text)), font, tracking, no_control}
            );
        };
    const auto control_part = [&parts](layout::SourceRect rect, int32_t control) {
        parts.push_back(LayoutPart{rect, {}, DialogFont::regular, 0, control});
    };

    // The header: the mark, the title, and at the right the version and the
    // shared game's note.
    control_part(layout::header_mark, no_control);
    const int32_t title_left =
        layout::header_mark.x + layout::header_mark.width + layout::header_gap;
    text_part(
        {title_left, layout::header_top, layout::title_width, layout::header_height},
        layout::title_text,
        DialogFont::regular,
        layout::heading_tracking
    );
    const int32_t suffix_left = title_left + layout::title_width + layout::header_gap;
    text_part(
        {suffix_left, layout::header_top, layout::title_suffix_width, layout::header_height},
        layout::title_suffix_text,
        DialogFont::regular,
        layout::heading_tracking
    );
    const layout::SourceRect version{
        layout::content_right - layout::version_width,
        layout::header_top,
        layout::version_width,
        layout::header_height,
    };
    text_part(version, dialog.version, DialogFont::small);
    if (dialog.locks.shared_game) {
        const int32_t shared_left = suffix_left + layout::title_suffix_width + layout::header_gap;
        text_part(
            {shared_left,
             layout::header_top,
             version.x - layout::version_gap - shared_left,
             layout::header_height},
            layout::shared_game_text,
            DialogFont::small
        );
    }

    // The section list.
    for (const Page page :
         dialog_pages(dialog.kind, dialog.touch, dialog.game_files, dialog.controller)) {
        const layout::SourceRect item = layout::dialog_list_item(dialog, page);
        parts.push_back(
            LayoutPart{
                {item.x + layout::list_text_offset,
                 item.y,
                 item.width - layout::list_text_offset - layout::list_text_margin,
                 item.height},
                std::string(layout::shown_text(layout::page_name(page))),
                DialogFont::regular,
                0,
                page_control(page),
            }
        );
    }
    if (dialog.kind == DialogKind::engine)
        control_part(layout::list_divider(), no_control);

    // The open section.
    text_part(
        layout::heading,
        layout::page_heading(dialog.page),
        DialogFont::small,
        layout::heading_tracking
    );
    // The rows: only the parts wholly in the view are listed, so that each
    // listed control is pressed where it is drawn and each text is whole.
    const layout::ScrolledRows open = layout::open_rows(dialog);
    const auto row_part = [&parts](LayoutPart part) {
        if (wholly_in(part.rect, layout::view))
            parts.push_back(std::move(part));
    };
    const auto row_text =
        [&row_part](layout::SourceRect rect, std::string_view text, DialogFont font) {
            row_part(LayoutPart{rect, std::string(layout::shown_text(text)), font, 0, no_control});
        };
    if (layout::mods_page(dialog))
        mods_layout(dialog, open, parts, text_width, small_text_width);
    // Every other section's rows as they are drawn, from their row specs:
    // Developer's stay at its top while its list scrolls.
    oa::ui::kit::PlacedRows placed;
    if (!layout::mods_page(dialog)) {
        static_cast<void>(layout::place_section(
            dialog,
            dialog.page,
            layout::shown_locks(dialog),
            dialog.section_hooks,
            small_text_width,
            &placed
        ));
        oa::ui::kit::scroll(placed, layout::developer_page(dialog) ? 0 : open.scroll);
    }
    const layout::SettingsModel model = layout::reading(dialog.chosen, dialog);
    for (std::size_t index = 0; index < placed.rows.size() && index < open.rows.rows.size();
         ++index) {
        const layout::Row& row = open.rows.rows[index];
        const oa::ui::kit::RowView& view = placed.rows[index].view;
        // A locked row's control is drawn but takes no press.
        const int32_t control = row.lock == Lock::none ? row.control : no_control;
        row_part(LayoutPart{row.label, view.label, DialogFont::regular, 0, no_control});
        if (row.lock != Lock::none) {
            const layout::SourceRect text_area{
                row.lock_area.x + layout::padlock_width + layout::padlock_gap,
                row.lock_area.y,
                row.lock_area.width - layout::padlock_width - layout::padlock_gap,
                row.lock_area.height,
            };
            row_part(
                LayoutPart{
                    {row.lock_area.x, row.lock_area.y, layout::padlock_width, row.lock_area.height},
                    {},
                    DialogFont::regular,
                    0,
                    no_control,
                }
            );
            row_text(text_area, layout::lock_text(row.lock), DialogFont::small);
        }
        for (std::size_t line = 0; line < row.hint_lines && line < view.hints.size(); ++line)
            if (!view.hints[line].empty())
                row_part(
                    LayoutPart{row.hints[line], view.hints[line], DialogFont::small, 0, no_control}
                );
        switch (view.kind) {
        case oa::ui::kit::RowKind::value_and_button:
            // MANAGE…: one button.
            row_part(
                LayoutPart{row.control_area, view.buttons.front(), DialogFont::small, 0, control}
            );
            break;
        case oa::ui::kit::RowKind::buttons:
            if (row.control_area.width > 0)
                for (std::size_t button = 0; button < view.buttons.size(); ++button)
                    row_part(
                        LayoutPart{
                            layout::folder_button(row.control_area, button),
                            view.buttons[button],
                            DialogFont::small,
                            0,
                            control,
                        }
                    );
            break;
        case oa::ui::kit::RowKind::levels:
            if (row.control_area.width > 0)
                for (std::size_t level = 0; level < view.captions.size(); ++level)
                    row_part(
                        LayoutPart{
                            {row.control_area.x + 1 +
                                 static_cast<int32_t>(level) * view.control_width,
                             row.control_area.y + 1,
                             view.control_width,
                             row.control_area.height - 2},
                            view.captions[level],
                            DialogFont::small,
                            0,
                            control,
                        }
                    );
            break;
        case oa::ui::kit::RowKind::choice:
            // The field shows the choice, in the regular font.
            row_part(
                LayoutPart{
                    row.control_area,
                    oa::ui::kit::row_caption(layout::row_spec(row.setting), model, view.index),
                    DialogFont::regular,
                    0,
                    control,
                }
            );
            break;
        case oa::ui::kit::RowKind::slider:
            row_part(LayoutPart{row.control_area, {}, DialogFont::regular, 0, control});
            row_part(LayoutPart{row.value, view.value, DialogFont::regular, 0, no_control});
            break;
        case oa::ui::kit::RowKind::toggle:
            if (row.control_area.width > 0) {
                const int32_t half = (row.control_area.width - 2) / 2;
                for (std::size_t side = 0; side < view.captions.size(); ++side)
                    row_part(
                        LayoutPart{
                            {row.control_area.x + 1 + static_cast<int32_t>(side) * half,
                             row.control_area.y + 1,
                             half,
                             row.control_area.height - 2},
                            view.captions[side],
                            DialogFont::small,
                            0,
                            control,
                        }
                    );
            }
            break;
        case oa::ui::kit::RowKind::text_field:
        case oa::ui::kit::RowKind::link:
        case oa::ui::kit::RowKind::text:
            // Where the files are and the Steam Input notice show text alone.
            break;
        }
    }
    // On Developer, its list and the list's footer under its rows.
    if (layout::developer_page(dialog))
        developer_layout(dialog, open, parts);
    if (open.limit > 0)
        control_part(open.area.well, scroll_bar_control);

    // The footer.
    const std::array<std::pair<int32_t, std::string_view>, 3> buttons{{
        {restore_control, layout::restore_text},
        {cancel_control, layout::cancel_text},
        {ok_control, layout::ok_text},
    }};
    for (const auto& [control, caption] : buttons) {
        parts.push_back(
            LayoutPart{
                layout::footer_button(control),
                std::string(layout::shown_text(caption)),
                DialogFont::small,
                0,
                control,
            }
        );
    }
    // An open drop-down list lies over what is under it, which is not listed:
    // its items are.
    if (const auto list = open_list(dialog, open)) {
        const auto overlaps = [&](const LayoutPart& part) {
            const auto& a = part.rect;
            const auto& b = list->rect;
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
                   b.y < a.y + a.height;
        };
        parts.erase(std::remove_if(parts.begin(), parts.end(), overlaps), parts.end());
        for (int32_t shown = 0; shown < list->shown; ++shown) {
            const int32_t item = dialog.list_first + shown;
            if (item >= static_cast<int32_t>(list->choices))
                break;
            parts.push_back(
                LayoutPart{
                    layout::choice_item(list->rect, shown),
                    oa::ui::kit::row_caption(layout::row_spec(list->row->setting), model, item),
                    DialogFont::regular,
                    0,
                    no_control,
                }
            );
        }
    }
    // The question lies over everything else, which is not listed under it.
    if (dialog.switch_question != no_question) {
        const auto under_question = [](const LayoutPart& part) {
            const auto& a = part.rect;
            const auto& b = layout::question_box;
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
                   b.y < a.y + a.height;
        };
        parts.erase(std::remove_if(parts.begin(), parts.end(), under_question), parts.end());
        text_part(
            layout::question_heading,
            dialog.mod_question == ModQuestion::roll_back ? layout::roll_back_heading_text
                                                          : layout::switch_heading_text,
            DialogFont::small
        );
        control_part(layout::question_badge, no_control);
        const layout::ModRowText offered =
            layout::mod_row_text(dialog, ModRow{dialog.switch_question, false});
        parts.push_back(
            LayoutPart{
                layout::question_title,
                layout::cut_text(offered.title, layout::question_title.width, text_width),
                DialogFont::regular,
                0,
                no_control,
            }
        );
        parts.push_back(
            LayoutPart{
                layout::question_version,
                layout::cut_text(
                    layout::question_version_text(dialog, offered),
                    layout::question_version.width,
                    small_text_width
                ),
                DialogFont::small,
                0,
                no_control,
            }
        );
        const auto lines = layout::question_text_lines(dialog, small_text_width);
        for (std::size_t line = 0; line < lines.size(); ++line) {
            layout::SourceRect rect = layout::question_first_line;
            rect.y += static_cast<int32_t>(line) * rect.height;
            parts.push_back(LayoutPart{rect, lines[line], DialogFont::small, 0, no_control});
        }
        parts.push_back(
            LayoutPart{
                layout::question_no_rect(dialog),
                std::string(layout::shown_text(layout::no_text)),
                DialogFont::small,
                0,
                question_no_control,
            }
        );
        parts.push_back(
            LayoutPart{
                layout::question_yes_rect(dialog),
                std::string(
                    layout::shown_text(
                        dialog.mod_question == ModQuestion::roll_back ? layout::roll_back_text
                                                                      : layout::yes_text
                    )
                ),
                DialogFont::small,
                0,
                question_yes_control,
            }
        );
    }
    return parts;
}

} // namespace oa::ui::engine_settings
