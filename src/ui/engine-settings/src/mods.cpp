// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mods: the list of the mods the game can play, in the order it shows them,
// each row's texts and place, a row's update and its UPDATE button, the line
// on the check for updates with CHECK NOW and GET MORE MODS… under the list,
// the Switch Mod question's text, and the Roll Back Mod question as a prompt
// of its own.

#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/engine_settings/prompt.hpp"

#include "geometry.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings {

namespace {

/// Returns a text in lower case, ASCII letters only, for ordering titles.
///
/// @param text the text
/// @return the text with A to Z lowered
std::string folded(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered;
}

/// Returns the columns left of a mod row's buttons that its update's line
/// takes: the text's width, less UPDATE's and, when it shows, ROLL BACK's.
///
/// @param dialog the dialog
/// @param row the row
/// @return the columns, in source pixels
int32_t update_room(const Dialog& dialog, const ModRow& row) noexcept {
    const geometry::Sizes& sized = geometry::sizes_of(dialog);
    const int32_t text_left = sized.content_left + geometry::mod_row_inset +
                              geometry::mod_badge_side + geometry::mod_row_inset;
    int32_t room = sized.content_right - geometry::mod_row_inset - text_left;
    if (geometry::shows_update(dialog, row))
        room -= geometry::mod_update_width + geometry::mod_roll_back_gap;
    if (geometry::offers_roll_back(dialog, row))
        room -= geometry::mod_roll_back_width + geometry::mod_roll_back_gap;
    return room;
}

/// Returns how many characters of the small font a width holds, as the
/// dialog counts them for a host's text: the class's hint_line_characters
/// to its content column.
///
/// @param sized the dialog's sizes
/// @param width the width, in source pixels
/// @return the characters, at least 1
std::size_t characters_in(const geometry::Sizes& sized, int32_t width) noexcept {
    if (width <= 0 || sized.content_width <= 0)
        return 1;
    const auto held = static_cast<std::size_t>(width) * sized.hint_line_characters /
                      static_cast<std::size_t>(sized.content_width);
    return std::max(held, std::size_t{1});
}

/// Returns a text's width in the small font as the dialog estimates it
/// without a font: the class's content column for hint_line_characters, a
/// character (a wide one counts twice, as kit::estimated_width counts it).
///
/// @param sized the dialog's sizes
/// @param text the text
/// @return the width, in source pixels, rounded up
int32_t small_width_estimate(const geometry::Sizes& sized, std::string_view text) {
    const int64_t columns =
        oa::ui::kit::estimated_width(text) / oa::ui::kit::estimated_character_width;
    const auto characters =
        static_cast<int64_t>(std::max(sized.hint_line_characters, std::size_t{1}));
    return static_cast<int32_t>((columns * sized.content_width + characters - 1) / characters);
}

/// Returns the details of the row the question asks about.
///
/// @param dialog the dialog, its question showing
/// @return the details; null for No Mod
const ModDetails* question_details(const Dialog& dialog) noexcept {
    const int32_t offered = dialog.switch_question;
    if (offered < 0 || static_cast<std::size_t>(offered) >= dialog.mod_details.size())
        return nullptr;
    return &dialog.mod_details[static_cast<std::size_t>(offered)];
}

} // namespace

std::vector<ModRow> mod_rows(const Dialog& dialog) {
    const std::size_t offered = std::min(dialog.mod_names.size(), dialog.mod_folders.size());
    int32_t playing = no_mod_row;
    for (std::size_t index = 0; index < offered; ++index)
        if (!dialog.playing_mod_folder.empty() &&
            dialog.mod_folders[index] == dialog.playing_mod_folder)
            playing = static_cast<int32_t>(index);
    std::vector<ModRow> rows;
    rows.reserve(offered + 1);
    rows.push_back(ModRow{playing, true});
    if (playing != no_mod_row)
        rows.push_back(ModRow{no_mod_row, false});
    std::vector<int32_t> others;
    for (std::size_t index = 0; index < offered; ++index)
        if (static_cast<int32_t>(index) != playing)
            others.push_back(static_cast<int32_t>(index));
    std::stable_sort(others.begin(), others.end(), [&dialog](int32_t left, int32_t right) {
        return folded(dialog.mod_names[static_cast<std::size_t>(left)]) <
               folded(dialog.mod_names[static_cast<std::size_t>(right)]);
    });
    for (const int32_t index : others)
        rows.push_back(ModRow{index, false});
    return rows;
}

namespace geometry {

std::string filled(
    std::string_view english,
    std::initializer_list<std::pair<std::string_view, std::string_view>> places
) {
    const std::string pattern(geometry::shown_text(english));
    std::string text;
    std::size_t at = 0;
    while (at < pattern.size()) {
        const std::size_t open = pattern.find('{', at);
        const std::size_t close = open == std::string::npos ? open : pattern.find('}', open);
        if (close == std::string::npos) {
            text.append(pattern, at, std::string::npos);
            break;
        }
        text.append(pattern, at, open - at);
        const std::string_view name(pattern.data() + open + 1, close - open - 1);
        bool found = false;
        for (const auto& [place, value] : places)
            if (place == name) {
                text += value;
                found = true;
                break;
            }
        if (!found)
            text.append(pattern, open, close - open + 1);
        at = close + 1;
    }
    return text;
}

bool mods_page(const Dialog& dialog) noexcept {
    // A check's own section takes the place of the list.
    const bool own_section =
        dialog.section_hooks != nullptr && dialog.section_hooks->settings != nullptr;
    return dialog.kind == DialogKind::engine && dialog.page == Page::mods && !own_section;
}

int32_t mods_folder_control(const Rows& rows) noexcept {
    return first_row_control + static_cast<int32_t>(rows.rows.size());
}

int32_t roll_back_control(const Rows& rows, std::size_t index) noexcept {
    return mods_folder_control(rows) + 1 + static_cast<int32_t>(index);
}

int32_t roll_back_row(const Rows& rows, int32_t control) noexcept {
    const int32_t first = mods_folder_control(rows) + 1;
    if (control < first || control - first >= static_cast<int32_t>(rows.rows.size()))
        return -1;
    return control - first;
}

SourceRect roll_back_button(const Row& row) noexcept {
    const SourceRect& box = row.control_area;
    return {
        box.x + box.width - mod_row_inset - mod_roll_back_width,
        box.y + box.height - mod_row_inset + 1 - mod_roll_back_height,
        mod_roll_back_width,
        mod_roll_back_height
    };
}

bool offers_roll_back(const Dialog& dialog, const ModRow& row) noexcept {
    return row.offered >= 0 && static_cast<std::size_t>(row.offered) < dialog.mod_details.size() &&
           !dialog.mod_details[static_cast<std::size_t>(row.offered)].roll_back_from.empty();
}

const ModDetails* update_details(const Dialog& dialog, const ModRow& row) noexcept {
    if (row.offered < 0 || static_cast<std::size_t>(row.offered) >= dialog.mod_details.size() ||
        static_cast<std::size_t>(row.offered) >= dialog.mod_folders.size())
        return nullptr;
    const ModDetails& details = dialog.mod_details[static_cast<std::size_t>(row.offered)];
    return details.update_offered ? &details : nullptr;
}

bool shows_update(const Dialog& dialog, const ModRow& row) noexcept {
    return dialog.locks.mod == Lock::none && update_details(dialog, row) != nullptr;
}

int32_t update_control(const Rows& rows, std::size_t index) noexcept {
    return roll_back_control(rows, rows.rows.size()) + static_cast<int32_t>(index);
}

int32_t update_row(const Rows& rows, int32_t control) noexcept {
    const int32_t first = update_control(rows, 0);
    if (control < first || control - first >= static_cast<int32_t>(rows.rows.size()))
        return -1;
    return control - first;
}

int32_t check_now_control(const Rows& rows) noexcept {
    return update_control(rows, rows.rows.size());
}

int32_t get_more_mods_control(const Rows& rows) noexcept {
    return check_now_control(rows) + 1;
}

SourceRect update_button(const Row& row, bool roll_back) noexcept {
    const SourceRect beside = roll_back_button(row);
    const int32_t right = roll_back ? beside.x - mod_roll_back_gap : beside.x + beside.width;
    return {right - mod_update_width, beside.y, mod_update_width, mod_update_height};
}

std::string_view mod_update_line(const ModDetails& details) noexcept {
    if (!details.update_progress.empty())
        return details.update_progress;
    if (!details.update_enabled && !details.update_blocked.empty())
        return details.update_blocked;
    return details.update_text;
}

std::vector<std::string> update_lines(const Dialog& dialog, const ModRow& row) {
    const ModDetails* details = update_details(dialog, row);
    if (details == nullptr)
        return {};
    return break_lines(
        mod_update_line(*details),
        characters_in(sizes_of(dialog), update_room(dialog, row)),
        most_row_lines
    );
}

bool shows_check_now(const Dialog& dialog) noexcept {
    return dialog.mods_check_offered && dialog.locks.mod == Lock::none;
}

bool shows_get_more_mods(const Dialog& dialog) noexcept {
    return dialog.mods_library_offered && dialog.locks.mod == Lock::none;
}

SourceRect check_now_link(const Dialog& dialog) {
    const Sizes& sized = sizes_of(dialog);
    const SourceRect& line = sized.mods_check_line;
    const int32_t width = small_width_estimate(sized, shown_text(check_now_text));
    const int32_t after = small_width_estimate(sized, dialog.mods_check_text) +
                          small_width_estimate(sized, check_now_separator);
    const int32_t left = std::min(line.x + after, line.x + line.width - width);
    return {std::max(left, line.x), line.y, width, line.height};
}

std::pair<SourceRect, std::string> check_line_text(const Dialog& dialog) {
    const Sizes& sized = sizes_of(dialog);
    SourceRect place = sized.mods_check_line;
    if (!shows_check_now(dialog))
        return {place, dialog.mods_check_text};
    // The text keeps clear of " · " and CHECK NOW after it, cut short only
    // where they would not fit.
    const SourceRect link = check_now_link(dialog);
    place.width =
        std::max(link.x - small_width_estimate(sized, check_now_separator) - place.x, int32_t{0});
    const std::size_t room = characters_in(sized, place.width);
    if (oa::ui::kit::character_count(dialog.mods_check_text) <= room)
        return {place, dialog.mods_check_text};
    const std::vector<std::string> first = break_lines(dialog.mods_check_text, room, 1);
    return {place, first.empty() ? std::string() : first.front() + std::string(path_ellipsis)};
}

SourceRect get_more_mods_button(const Dialog& dialog) {
    const SourceRect& folder = sizes_of(dialog).mods_folder_button;
    const int32_t width = oa::ui::kit::estimated_width(shown_text(get_more_mods_text)) +
                          oa::ui::kit::compact_metrics.prompt_button_padding;
    return {folder.x + folder.width + mods_library_gap, folder.y, width, folder.height};
}

SourceRect question_yes_rect(const Dialog& dialog) noexcept {
    const Sizes& sized = sizes_of(dialog);
    if (dialog.mod_question != ModQuestion::roll_back)
        return sized.question_yes_button;
    // The question's box keeps Compact's padding in every class.
    return {
        sized.question_box.x + sized.question_box.width - padding - question_roll_back_width,
        sized.question_yes_button.y,
        question_roll_back_width,
        sized.question_yes_button.height
    };
}

SourceRect question_no_rect(const Dialog& dialog) noexcept {
    const Sizes& sized = sizes_of(dialog);
    const SourceRect yes = question_yes_rect(dialog);
    const SourceRect& no = sized.question_no_button;
    return {
        yes.x - (sized.question_yes_button.x - no.x - no.width) - no.width,
        yes.y,
        no.width,
        no.height
    };
}

std::string question_version_text(const Dialog& dialog, const ModRowText& offered) {
    const ModDetails* details = question_details(dialog);
    if (dialog.mod_question != ModQuestion::roll_back || details == nullptr)
        return offered.version;
    return filled(
        roll_back_versions_text, {{"from", details->roll_back_from}, {"to", details->roll_back_to}}
    );
}

Rows place_mod_rows(const Dialog& dialog, int32_t scroll) {
    Rows placed{};
    const Sizes& sized = sizes_of(dialog);
    const ScrollArea area = mods_scroll(dialog.locks.mod != Lock::none, dialog.size_class);
    const auto rows = mod_rows(dialog);
    const Lock lock = dialog.locks.mod;
    int32_t top = area.view.y;
    // While the dialog's words are drawn in the modern fonts a row is
    // taller, and its description lower.
    const bool tall = oa::data::languages::interface_language().needs !=
                      oa::data::languages::TextNeeds::game_fonts;
    const int32_t height = mod_row_height + (tall ? tall_mod_row_growth : 0);
    const int32_t description_top = mod_description_top + (tall ? tall_mod_description_drop : 0);
    // A row with an update grows by a line for each of its update's lines
    // after the first; every other row is as it was.
    const int32_t line_step = hint_line_height + (tall ? tall_hint_line_gap : 0);
    placed.rows.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        Row& row = placed.rows.emplace_back();
        row.setting = Setting::mod;
        row.control = first_row_control + static_cast<int32_t>(index);
        // A locked page keeps the mod played: every row is inert.
        row.lock = lock;
        row.top = top;
        const std::size_t lines =
            std::max(update_lines(dialog, rows[index]).size(), std::size_t{1});
        const int32_t grown = height + static_cast<int32_t>(lines - 1) * line_step;
        row.height = grown + mod_row_gap;
        row.control_area = {sized.content_left, top, sized.content_width, grown};
        const int32_t text_left =
            sized.content_left + mod_row_inset + mod_badge_side + mod_row_inset;
        const int32_t text_width = sized.content_right - mod_row_inset - text_left;
        row.label = {text_left, top + 1, text_width, label_line_height};
        row.value = {text_left, top + 3, text_width, hint_line_height};
        for (std::size_t line = 0; line < lines && line < row.hints.size(); ++line)
            row.hints[line] = {
                text_left,
                top + description_top + static_cast<int32_t>(line) * line_step,
                text_width,
                hint_line_height
            };
        row.hint_lines = lines;
        top += row.height;
    }
    placed.bottom = top - mod_row_gap;
    scroll_rows(placed, scroll);
    return placed;
}

ModRowText mod_row_text(const Dialog& dialog, const ModRow& row) {
    ModRowText text;
    if (row.offered < 0 || static_cast<std::size_t>(row.offered) >= dialog.mod_folders.size()) {
        text.title = std::string(shown_text(no_mod_text));
        text.version = std::string(no_mod_version_text);
        text.description = std::string(shown_text(no_mod_description_text));
        return text;
    }
    const auto index = static_cast<std::size_t>(row.offered);
    text.title = index < dialog.mod_names.size() ? dialog.mod_names[index] : std::string();
    if (index < dialog.mod_details.size()) {
        const ModDetails& details = dialog.mod_details[index];
        text.details = &details;
        text.has_profile = details.has_profile;
        text.version = details.version;
        text.description = details.description;
    }
    if (!text.has_profile) {
        if (text.version.empty())
            text.version = std::string(no_profile_version_text);
        if (text.description.empty())
            text.description = std::string(shown_text(no_profile_description_text));
    }
    return text;
}

std::string cut_text(
    std::string_view text, int32_t width, const std::function<int32_t(std::string_view)>& text_width
) {
    if (text_width(text) <= width)
        return std::string(text);
    const std::string ellipsis(path_ellipsis);
    std::size_t end = text.size();
    while (end > 0) {
        --end;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U)
            --end;
        std::string shown = std::string(text.substr(0, end)) + ellipsis;
        if (text_width(shown) <= width)
            return shown;
    }
    return ellipsis;
}

std::vector<std::string> question_text_lines(
    const Dialog& dialog, const std::function<int32_t(std::string_view)>& text_width
) {
    const ModRowText offered = mod_row_text(dialog, ModRow{dialog.switch_question, false});
    if (const ModDetails* details = question_details(dialog);
        dialog.mod_question == ModQuestion::roll_back && details != nullptr) {
        std::string asked = filled(
            roll_back_ask_text,
            {{"title", offered.title},
             {"from", details->roll_back_from},
             {"to", details->roll_back_to}}
        );
        const bool playing =
            !dialog.playing_mod_folder.empty() &&
            static_cast<std::size_t>(dialog.switch_question) < dialog.mod_folders.size() &&
            dialog.mod_folders[static_cast<std::size_t>(dialog.switch_question)] ==
                dialog.playing_mod_folder;
        if (playing)
            asked += " " + std::string(shown_text(roll_back_reload_text));
        oa::ui::kit::WrapRules rules;
        rules.shorten_word = [&](std::string_view word) {
            return cut_text(word, question_first_line.width, text_width);
        };
        std::vector<std::string> lines =
            oa::ui::kit::wrap(asked, question_first_line.width, text_width, rules);
        if (lines.size() > question_lines) {
            lines.resize(question_lines);
            lines.back() = cut_text(
                lines.back() + std::string(path_ellipsis), question_first_line.width, text_width
            );
        }
        return lines;
    }
    const std::string asked = filled(switch_ask_text, {{"title", offered.title}});
    oa::ui::kit::WrapRules rules;
    rules.shorten_word = [&](std::string_view word) {
        return cut_text(word, question_first_line.width, text_width);
    };
    std::vector<std::string> lines =
        oa::ui::kit::wrap(asked, question_first_line.width, text_width, rules);
    std::vector<std::string> note;
    if (!offered.has_profile)
        note =
            oa::ui::kit::wrap(switch_no_profile_text, question_first_line.width, text_width, rules);
    // The note keeps its lines; the question keeps at least one.
    if (note.size() > question_lines - 1)
        note.resize(question_lines - 1);
    const std::size_t room = question_lines - note.size();
    if (lines.size() > room) {
        lines.resize(room);
        lines.back() = cut_text(
            lines.back() + std::string(path_ellipsis), question_first_line.width, text_width
        );
    }
    lines.insert(lines.end(), note.begin(), note.end());
    return lines;
}

} // namespace geometry

Prompt
roll_back_prompt(std::string_view title, std::string_view from, std::string_view to, bool playing) {
    Prompt prompt;
    prompt.title = std::string(geometry::shown_text(geometry::roll_back_heading_text));
    std::string asked = geometry::filled(
        geometry::roll_back_ask_text, {{"title", title}, {"from", from}, {"to", to}}
    );
    if (playing)
        asked += " " + std::string(geometry::shown_text(geometry::roll_back_reload_text));
    prompt.paragraphs.push_back({std::move(asked), false});
    prompt.buttons.push_back(
        {std::string(geometry::shown_text(geometry::no_text)), false, "cancel"}
    );
    prompt.buttons.push_back(
        {std::string(geometry::shown_text(geometry::roll_back_text)), true, "roll-back"}
    );
    prompt.cancel_button = 0;
    prompt.primary_button = 1;
    prompt.marked = 0;
    return prompt;
}

} // namespace oa::ui::engine_settings
