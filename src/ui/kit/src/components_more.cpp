// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A notice and a question: their text wrapped and placed, their display
// lists, what pointer, finger and key events do to them, and their drawing.
// The arithmetic is the settings dialog's notices' and prompts', moved here
// unchanged.

#include "oa/ui/kit/components_more.hpp"

#include "oa/ui/kit/chrome.hpp"

#include "oa/ui/frontend_renderer/artless.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::kit {

namespace renderer = oa::ui::frontend_renderer;

namespace {

/// The Compact metrics every notice and question is placed by.
constexpr const Metrics& metrics = compact_metrics;

/// The text's first column.
constexpr int32_t text_left = compact_metrics.padding;
/// The text's width.
constexpr int32_t line_width = notice_width - 2 * compact_metrics.padding;
/// The width inside the edge.
constexpr int32_t inner_width = notice_width - 2 * compact_metrics.edge;

/// The automation names of a notice's buttons.
constexpr std::string_view ok_name = "notice.ok";
constexpr std::string_view open_name = "notice.open";

/// The text's lines and the row under the last of them.
struct PlacedText {
    std::vector<PlacedLine> lines{}; ///< every line, top to bottom, before any is cut
    int32_t bottom{};                ///< the row under the last line
};

/// Places paragraphs and a failure under the header: paths in the regular
/// font broken at their separators, the rest in the small font broken
/// between words, paragraph_gap rows between them.
///
/// @param paragraphs the text
/// @param failure the failure line's text; empty for none
/// @param regular a text's width in the regular font
/// @param small a text's width in the small font
/// @return the lines and the row under them
PlacedText place_text(
    const std::vector<Paragraph>& paragraphs,
    std::string_view failure,
    const Measure& regular,
    const Measure& small
) {
    PlacedText placed{};
    int32_t row = metrics.text_top;
    bool first = true;
    const auto add = [&](const std::vector<std::string>& wrapped, bool path, bool is_failure) {
        if (!first)
            row += metrics.paragraph_gap;
        first = false;
        const int32_t line_height = path ? metrics.regular_line : metrics.small_line;
        for (const auto& text : wrapped) {
            placed.lines.push_back(
                PlacedLine{text, path, is_failure, {text_left, row, line_width, line_height}}
            );
            row += line_height;
        }
    };
    for (const auto& paragraph : paragraphs)
        add(paragraph.path ? wrap_path(paragraph.text, line_width, regular)
                           : wrap(paragraph.text, line_width, small),
            paragraph.path,
            false);
    if (!failure.empty())
        add(wrap(failure, line_width, small), false, true);
    placed.bottom = row;
    return placed;
}

/// Returns the height of a box whose text ends at a row: its text, the gap
/// under it and the footer, from least_notice_height to greatest_notice_height.
///
/// @param text_bottom the row under the text
/// @return the height
int32_t height_for(int32_t text_bottom) noexcept {
    return std::clamp(
        text_bottom + metrics.text_bottom_gap + 1 + metrics.footer_height + metrics.edge,
        least_notice_height,
        greatest_notice_height
    );
}

/// Returns the row of the line over a box's footer.
///
/// @param height the box's height
/// @return the row
int32_t footer_rule_of(int32_t height) noexcept {
    return height - metrics.edge - metrics.footer_height - 1;
}

/// Returns the top row of a box's footer buttons.
///
/// @param height the box's height
/// @return the row
int32_t button_top_of(int32_t height) noexcept {
    return footer_rule_of(height) + 1 + (metrics.footer_height - metrics.button_height) / 2;
}

/// Tells whether a rectangle's bottom lies above the footer's rule by half
/// the gap kept under the text.
///
/// @param rect the rectangle
/// @param footer_rule the row of the line over the footer
/// @return true when it fits
bool fits_over(const Rect& rect, int32_t footer_rule) noexcept {
    return rect.y + rect.height <= footer_rule - metrics.text_bottom_gap / 2;
}

/// Returns the lines of placed text a box's height does not cut.
///
/// @param lines the lines
/// @param footer_rule the row of the line over the footer
/// @return the lines that fit
std::vector<PlacedLine> lines_that_fit(std::vector<PlacedLine> lines, int32_t footer_rule) {
    std::vector<PlacedLine> fitting;
    for (auto& line : lines)
        if (fits_over(line.rect, footer_rule))
            fitting.push_back(std::move(line));
    return fitting;
}

/// Returns the title's place in the header.
///
/// @return the rectangle, in points
Rect title_rect() noexcept {
    const int32_t title_left = notice_mark.x + notice_mark.width + metrics.header_gap;
    return {
        title_left, metrics.edge, notice_width - metrics.padding - title_left, metrics.header_height
    };
}

/// OK and the open button, placed.
struct NoticeButtons {
    Rect ok{};   ///< OK, at the footer's right
    Rect open{}; ///< the open button, before OK
};

/// Returns a notice's buttons, which depend on its height alone.
///
/// @param height the notice's height
/// @return the buttons' rectangles
NoticeButtons notice_buttons(int32_t height) noexcept {
    const int32_t top = button_top_of(height);
    const Rect ok{
        notice_width - metrics.padding - metrics.button_width,
        top,
        metrics.button_width,
        metrics.button_height
    };
    return {
        ok,
        {ok.x - metrics.button_gap - metrics.open_width,
         top,
         metrics.open_width,
         metrics.button_height}
    };
}

/// Returns a text's width in a font, or its estimated width without fonts.
///
/// @param fonts the fonts; null to estimate
/// @param role which font
/// @return the measure
Measure measure_in(const Fonts* fonts, FontRole role) {
    if (fonts == nullptr)
        return estimated_width;
    return [fonts, role](std::string_view text) { return text_width(*fonts, role, text); };
}

/// Returns a text as a notice shows it.
///
/// @param look_up the caller's look-up; unset shows the text as given
/// @param text the text
/// @return the text shown
std::string shown(const LookUp& look_up, std::string_view text) {
    return look_up ? std::string(look_up(text)) : std::string(text);
}

/// Appends a control.
///
/// @param[in,out] list the display list
/// @param id its number
/// @param rect where it lies
/// @param name its automation name
/// @param text its caption, for automation
void add_button_control(
    DisplayList& list, ControlId id, const Rect& rect, std::string name, std::string text
) {
    Control control;
    control.id = id;
    control.rect = rect;
    control.name = std::move(name);
    control.kind = ControlKind::button;
    control.text = std::move(text);
    list.controls.push_back(std::move(control));
}

/// Returns a notice's controls at a height: OK, then the open button, as a
/// press tests them.
///
/// @param height the notice's height
/// @param ok OK's caption, for automation
/// @param open the open button's caption, for automation
/// @return the list, without items
DisplayList notice_controls(int32_t height, std::string ok = {}, std::string open = {}) {
    const NoticeButtons buttons = notice_buttons(height);
    DisplayList list;
    add_button_control(list, notice_ok, buttons.ok, std::string(ok_name), std::move(ok));
    add_button_control(list, notice_open, buttons.open, std::string(open_name), std::move(open));
    list.tab_order = {notice_open, notice_ok};
    return list;
}

/// Tells whether a button's id may name it to automation.
///
/// @param id the id
/// @return true for a word of a-z, 0-9 and hyphens
bool sound_id(std::string_view id) noexcept {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
               character == '-';
    });
}

/// Returns a question button's automation name.
///
/// @param button the button
/// @param index its place from the left, from 0
/// @return prompt.<id>, or prompt.button-<place from 1>
std::string question_button_name(const QuestionButton& button, std::size_t index) {
    if (sound_id(button.id))
        return "prompt." + button.id;
    return "prompt.button-" + std::to_string(index + 1);
}

/// Returns a question's controls at a height: its buttons, left to right.
///
/// @param question the question
/// @param height its height
/// @return the list, without items
DisplayList question_controls(const Question& question, int32_t height) {
    const std::vector<Rect> rects = question_button_rects(question, height);
    DisplayList list;
    for (std::size_t index = 0; index < rects.size(); ++index) {
        const auto id = static_cast<ControlId>(index);
        add_button_control(
            list,
            id,
            rects[index],
            question_button_name(question.buttons[index], index),
            question.buttons[index].caption
        );
        list.tab_order.push_back(id);
    }
    return list;
}

/// Returns the header a notice or a question shows.
///
/// @param title the title, as shown
/// @return the header's look
HeaderLook header_of(std::string title) {
    HeaderLook header;
    header.width = notice_width;
    header.title = std::move(title);
    header.title_width = title_rect().width;
    header.tracking = metrics.heading_tracking;
    return header;
}

/// Returns a button's look.
///
/// @param caption its caption, as shown
/// @param accent true for the accent look, false for the plain one
/// @param control its number
/// @param hovered the control under the pointer
/// @param pressed the control a held press is on
/// @return the look
ButtonLook button_look(
    std::string caption, bool accent, ControlId control, ControlId hovered, ControlId pressed
) {
    return {
        std::move(caption),
        accent ? ButtonStyle::accent : ButtonStyle::plain,
        hovered == control,
        pressed == control && hovered == control,
        true,
    };
}

/// Draws a line of text.
///
/// @param canvas where it is drawn
/// @param line the line
/// @param text the text, as shown
void draw_text_line(const Canvas& canvas, const PlacedLine& line, std::string_view text) {
    if (canvas.surface == nullptr || canvas.fonts == nullptr)
        return;
    draw_boxed_text(
        *canvas.surface,
        canvas.placement,
        *canvas.fonts,
        line.path ? FontRole::regular : FontRole::small,
        text,
        line.rect,
        Align::left,
        rgb(line.failure ? colour::lock : colour::text)
    );
}

/// Draws a button, and the focus ring round it when the keys mark it.
///
/// @param canvas where it is drawn
/// @param rect the button
/// @param look its look
/// @param marked true when the keys mark it
void draw_marked_button(
    const Canvas& canvas, const Rect& rect, const ButtonLook& look, bool marked
) {
    draw_button(canvas, rect, look);
    if (marked)
        draw_focus_ring(canvas, rect, true);
}

/// Appends a box's face and header: the window's face and the header with
/// its title.
///
/// @param[in,out] list the display list
/// @param height the box's height
/// @param title the title, as shown
void add_face_and_header(DisplayList& list, int32_t height, std::string title) {
    Item face;
    face.role = Role::fill;
    face.rect = {0, 0, notice_width, height};
    face.colour = colour::panel;
    list.items.push_back(std::move(face));
    Item header;
    header.role = Role::header;
    header.rect = {metrics.edge, metrics.edge, inner_width, metrics.header_height};
    header.text = title;
    header.look = header_of(std::move(title));
    list.items.push_back(std::move(header));
}

/// Appends a line of text.
///
/// @param[in,out] list the display list
/// @param line the line
/// @param text the text, as shown
void add_text_line(DisplayList& list, const PlacedLine& line, std::string text) {
    Item item;
    item.role = Role::text;
    item.rect = line.rect;
    item.text = std::move(text);
    item.font = line.path ? FontRole::regular : FontRole::small;
    item.align = Align::left;
    item.colour = line.failure ? colour::lock : colour::text;
    list.items.push_back(std::move(item));
}

/// Appends the footer's rule and band.
///
/// @param[in,out] list the display list
/// @param footer_rule the row of the line over the footer
void add_footer(DisplayList& list, int32_t footer_rule) {
    Item item;
    item.role = Role::footer_band;
    item.rect = {metrics.edge, footer_rule, inner_width, 1 + metrics.footer_height};
    item.look = FooterBandLook{notice_width, footer_rule};
    list.items.push_back(std::move(item));
}

/// Appends a button's item, and the focus ring's when the keys mark it. The
/// control is added apart, in the order a press tests it.
///
/// @param[in,out] list the display list
/// @param rect the button
/// @param look its look
/// @param id its number
/// @param marked true when the keys mark it
void add_button_item(
    DisplayList& list, const Rect& rect, const ButtonLook& look, ControlId id, bool marked
) {
    Item item;
    item.role = Role::button;
    item.rect = rect;
    item.control = id;
    item.text = look.caption;
    item.state.hovered = look.hovered;
    item.state.pressed = look.held;
    item.state.focused = marked;
    item.look = look;
    list.items.push_back(std::move(item));
    if (marked)
        add_focus_ring(list, rect, true);
}

/// Appends the window's edge.
///
/// @param[in,out] list the display list
/// @param height the box's height
void add_edge(DisplayList& list, int32_t height) {
    Item item;
    item.role = Role::bevel;
    item.rect = {0, 0, notice_width, height};
    item.colour = colour::edge_light;
    list.items.push_back(std::move(item));
}

/// Returns what pressing a notice's button asks.
///
/// @param control the button
/// @return NoticeAction::open_folder for the open button, closed for OK
NoticeAction press(ControlId control) noexcept {
    return control == notice_open ? NoticeAction::open_folder : NoticeAction::closed;
}

/// Returns an answer.
///
/// @param question the question
/// @param button the button pressed
/// @return the answer; nothing for a button the question lacks
QuestionAnswer answer(const Question& question, int32_t button) {
    if (button < 0 || static_cast<std::size_t>(button) >= question.buttons.size())
        return {};
    return {QuestionAction::answered, button};
}

/// Returns a redraw, or nothing.
///
/// @param redraw whether the look changed
/// @return the answer
QuestionAnswer look_changed(bool redraw) {
    return {redraw ? QuestionAction::redraw : QuestionAction::none, -1};
}

} // namespace

PlacedNotice place_notice(const Notice& notice, const Measure& regular, const Measure& small) {
    PlacedNotice placed{};
    placed.title = title_rect();
    PlacedText text = place_text(notice.paragraphs, notice.failure, regular, small);
    placed.height = height_for(text.bottom);
    placed.footer_rule = footer_rule_of(placed.height);
    // A line the height cuts is left out.
    placed.lines = lines_that_fit(std::move(text.lines), placed.footer_rule);
    const NoticeButtons buttons = notice_buttons(placed.height);
    placed.ok_button = buttons.ok;
    placed.open_button = buttons.open;
    return placed;
}

PlacedNotice place_notice(const Notice& notice, const Fonts* fonts) {
    return place_notice(
        notice, measure_in(fonts, FontRole::regular), measure_in(fonts, FontRole::small)
    );
}

std::vector<Rect> question_button_rects(const Question& question, int32_t height) {
    const int32_t top = button_top_of(height);
    const std::size_t count = std::min(question.buttons.size(), most_question_buttons);
    std::vector<Rect> rects(count);
    int32_t right = notice_width - metrics.padding;
    for (std::size_t index = count; index-- > 0;) {
        const int32_t width = std::max(
            metrics.button_width,
            estimated_width(question.buttons[index].caption) + metrics.prompt_button_padding
        );
        rects[index] = {right - width, top, width, metrics.button_height};
        right -= width + metrics.button_gap;
    }
    return rects;
}

PlacedQuestion
place_question(const Question& question, const Measure& regular, const Measure& small) {
    PlacedQuestion placed{};
    placed.title = title_rect();
    PlacedText text = place_text(question.paragraphs, question.failure, regular, small);
    int32_t bottom = text.bottom;
    if (question.progress >= 0) {
        bottom += metrics.paragraph_gap;
        placed.bar = {text_left, bottom, line_width, metrics.progress_bar_height};
        bottom += metrics.progress_bar_height;
    }
    placed.height = height_for(bottom);
    placed.footer_rule = footer_rule_of(placed.height);
    const std::size_t all_lines = text.lines.size();
    placed.lines = lines_that_fit(std::move(text.lines), placed.footer_rule);
    const bool bar_cut = question.progress >= 0 && !fits_over(placed.bar, placed.footer_rule);
    placed.cut = placed.lines.size() != all_lines || bar_cut;
    // A bar the height cuts is left out.
    if (bar_cut)
        placed.bar = {};
    placed.buttons = question_button_rects(question, placed.height);
    return placed;
}

PlacedQuestion place_question(const Question& question, const Fonts* fonts) {
    return place_question(
        question, measure_in(fonts, FontRole::regular), measure_in(fonts, FontRole::small)
    );
}

DisplayList notice_list(const Notice& notice, const Fonts* fonts, const LookUp& look_up) {
    const PlacedNotice placed = place_notice(notice, fonts);
    const std::string ok = shown(look_up, notice.ok_caption);
    const std::string open = shown(look_up, notice.open_caption);
    DisplayList list = notice_controls(placed.height, ok, open);
    add_face_and_header(list, placed.height, shown(look_up, notice.title));
    for (const PlacedLine& line : placed.lines)
        add_text_line(list, line, shown(look_up, line.text));
    add_footer(list, placed.footer_rule);
    add_button_item(
        list,
        placed.open_button,
        button_look(open, false, notice_open, notice.hovered, notice.pressed),
        notice_open,
        notice.marked == notice_open
    );
    add_button_item(
        list,
        placed.ok_button,
        button_look(ok, true, notice_ok, notice.hovered, notice.pressed),
        notice_ok,
        notice.marked == notice_ok
    );
    add_edge(list, placed.height);
    return list;
}

DisplayList question_list(const Question& question, const Fonts* fonts) {
    const PlacedQuestion placed = place_question(question, fonts);
    DisplayList list = question_controls(question, placed.height);
    add_face_and_header(list, placed.height, question.title);
    for (const PlacedLine& line : placed.lines)
        add_text_line(list, line, line.text);
    add_footer(list, placed.footer_rule);
    if (placed.bar.width > 0)
        add_progress(list, placed.bar, question.progress, question_progress_whole);
    for (std::size_t index = 0; index < placed.buttons.size(); ++index) {
        const auto control = static_cast<ControlId>(index);
        const QuestionButton& button = question.buttons[index];
        add_button_item(
            list,
            placed.buttons[index],
            button_look(button.caption, button.accent, control, question.hovered, question.pressed),
            control,
            question.marked == control
        );
    }
    add_edge(list, placed.height);
    return list;
}

NoticeAction notice_pointer_move(Notice& notice, Point at, int32_t height) {
    // A finger's held press moves as it was moved to the button it took.
    if (notice.pressed != no_control) {
        at.x += notice.finger_shift_x;
        at.y += notice.finger_shift_y;
    }
    const ControlId hovered = hit(notice_controls(height), at);
    if (hovered == notice.hovered)
        return NoticeAction::none;
    notice.hovered = hovered;
    return NoticeAction::redraw;
}

NoticeAction notice_pointer_down(Notice& notice, Point at, int32_t height) {
    notice.finger_shift_x = 0;
    notice.finger_shift_y = 0;
    notice.hovered = hit(notice_controls(height), at);
    if (notice.hovered == no_control)
        return NoticeAction::none;
    notice.pressed = notice.hovered;
    return NoticeAction::redraw;
}

NoticeAction notice_finger_down(Notice& notice, Point finger, int32_t height, int32_t reach_px) {
    // OK, then the open button: the nearer within reach, at its point nearest
    // the finger.
    const Reached landed = reach(notice_controls(height), finger, reach_px);
    const NoticeAction action = notice_pointer_down(notice, landed.at, height);
    if (action == NoticeAction::redraw) {
        notice.finger_shift_x = landed.at.x - finger.x;
        notice.finger_shift_y = landed.at.y - finger.y;
    }
    return action;
}

NoticeAction notice_pointer_up(Notice& notice, Point at, int32_t height) {
    // A finger's release lands as its press was moved; the next press
    // starts afresh.
    if (notice.pressed != no_control) {
        at.x += notice.finger_shift_x;
        at.y += notice.finger_shift_y;
    }
    notice.finger_shift_x = 0;
    notice.finger_shift_y = 0;
    notice.hovered = hit(notice_controls(height), at);
    const ControlId held = notice.pressed;
    notice.pressed = no_control;
    if (held == no_control)
        return NoticeAction::none;
    if (held != notice.hovered)
        return NoticeAction::redraw;
    return press(held);
}

NoticeAction notice_key(Notice& notice, Key key) {
    switch (key) {
    case Key::enter:
    case Key::escape:
        return NoticeAction::closed;
    case Key::space:
        return press(notice.marked);
    case Key::left:
    case Key::right:
    case Key::up:
    case Key::down:
    case Key::tab:
    case Key::back_tab:
        notice.marked = notice.marked == notice_ok ? notice_open : notice_ok;
        return NoticeAction::redraw;
    default:
        return NoticeAction::none;
    }
}

QuestionAnswer question_pointer_move(Question& question, Point at, int32_t height) {
    if (question.pressed != no_control) {
        at.x += question.finger_shift_x;
        at.y += question.finger_shift_y;
    }
    const ControlId hovered = hit(question_controls(question, height), at);
    if (hovered == question.hovered)
        return {};
    question.hovered = hovered;
    return look_changed(true);
}

QuestionAnswer question_pointer_down(Question& question, Point at, int32_t height) {
    question.finger_shift_x = 0;
    question.finger_shift_y = 0;
    question.hovered = hit(question_controls(question, height), at);
    if (question.hovered == no_control)
        return {};
    question.pressed = question.hovered;
    return look_changed(true);
}

QuestionAnswer
question_finger_down(Question& question, Point finger, int32_t height, int32_t reach_px) {
    // The nearest button within reach, left to right, at its point nearest
    // the finger.
    const Reached landed = reach(question_controls(question, height), finger, reach_px);
    const QuestionAnswer pressed = question_pointer_down(question, landed.at, height);
    if (pressed.action == QuestionAction::redraw) {
        question.finger_shift_x = landed.at.x - finger.x;
        question.finger_shift_y = landed.at.y - finger.y;
    }
    return pressed;
}

QuestionAnswer question_pointer_up(Question& question, Point at, int32_t height) {
    if (question.pressed != no_control) {
        at.x += question.finger_shift_x;
        at.y += question.finger_shift_y;
    }
    question.finger_shift_x = 0;
    question.finger_shift_y = 0;
    question.hovered = hit(question_controls(question, height), at);
    const ControlId held = question.pressed;
    question.pressed = no_control;
    if (held == no_control)
        return {};
    if (held != question.hovered)
        return look_changed(true);
    return answer(question, held);
}

QuestionAnswer question_key(Question& question, Key key) {
    const auto count =
        static_cast<int32_t>(std::min(question.buttons.size(), most_question_buttons));
    if (count == 0)
        return {};
    switch (key) {
    case Key::enter:
    case Key::space:
        return answer(question, question.marked);
    case Key::escape:
    case Key::no:
        return answer(question, question.cancel_button);
    case Key::yes:
        return answer(question, question.primary_button);
    case Key::left:
    case Key::up:
    case Key::back_tab:
        question.marked = (question.marked + count - 1) % count;
        return look_changed(count > 1);
    case Key::right:
    case Key::down:
    case Key::tab:
        question.marked = (question.marked + 1) % count;
        return look_changed(count > 1);
    default:
        return {};
    }
}

void draw_notice(const Canvas& canvas, const Notice& notice, const LookUp& look_up) {
    const PlacedNotice placed = place_notice(notice, canvas.fonts);
    const Rect whole{0, 0, notice_width, placed.height};
    draw_window_face(canvas, whole);
    draw_header(canvas, header_of(shown(look_up, notice.title)));
    // The text: paths in the regular font, the rest in the small one, the
    // failure in amber.
    for (const PlacedLine& line : placed.lines)
        draw_text_line(canvas, line, shown(look_up, line.text));
    draw_footer_band(canvas, notice_width, placed.footer_rule);
    // The footer: the open button plain and OK in the accent, the marked one
    // ringed.
    draw_marked_button(
        canvas,
        placed.open_button,
        button_look(
            shown(look_up, notice.open_caption), false, notice_open, notice.hovered, notice.pressed
        ),
        notice.marked == notice_open
    );
    draw_marked_button(
        canvas,
        placed.ok_button,
        button_look(
            shown(look_up, notice.ok_caption), true, notice_ok, notice.hovered, notice.pressed
        ),
        notice.marked == notice_ok
    );
    draw_window_edge(canvas, whole);
}

void draw_question(const Canvas& canvas, const Question& question) {
    const PlacedQuestion placed = place_question(question, canvas.fonts);
    const Rect whole{0, 0, notice_width, placed.height};
    draw_window_face(canvas, whole);
    draw_header(canvas, header_of(question.title));
    for (const PlacedLine& line : placed.lines)
        draw_text_line(canvas, line, line.text);
    draw_footer_band(canvas, notice_width, placed.footer_rule);
    if (placed.bar.width > 0)
        draw_progress(canvas, placed.bar, question.progress, question_progress_whole);
    for (std::size_t index = 0; index < placed.buttons.size(); ++index) {
        const auto control = static_cast<ControlId>(index);
        const QuestionButton& button = question.buttons[index];
        draw_marked_button(
            canvas,
            placed.buttons[index],
            button_look(button.caption, button.accent, control, question.hovered, question.pressed),
            question.marked == control
        );
    }
    draw_window_edge(canvas, whole);
}

void draw_progress(const Canvas& canvas, const Rect& bar, int32_t done, int32_t whole) {
    if (canvas.surface == nullptr || bar.width <= 0 || bar.height <= 0)
        return;
    renderer::fill_source_rect(*canvas.surface, canvas.placement, bar, rgb(colour::well));
    renderer::draw_outline(*canvas.surface, canvas.placement, bar, rgb(colour::control_border));
    if (whole <= 0)
        return;
    const int32_t inner = bar.width - 2;
    const auto filled =
        static_cast<int32_t>(int64_t{inner} * std::clamp(done, int32_t{0}, whole) / whole);
    if (filled > 0)
        renderer::fill_source_rect(
            *canvas.surface,
            canvas.placement,
            {bar.x + 1, bar.y + 1, filled, bar.height - 2},
            rgb(colour::accent)
        );
}

void add_progress(DisplayList& list, const Rect& bar, int32_t done, int32_t whole) {
    Item item;
    item.role = Role::progress;
    item.rect = bar;
    item.text = std::to_string(done);
    item.look = ProgressLook{done, whole};
    list.items.push_back(std::move(item));
}

} // namespace oa::ui::kit
