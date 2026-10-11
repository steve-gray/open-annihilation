// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Settings › Mods with its registries' updates, on dialogs built here: rows
// with and without an update, a row without one drawn as it was before;
// an update that changes the rules and one whose rules cannot be known,
// their lines whole beside UPDATE and ROLL BACK at Compact with a title of
// 24 characters; a blocked update; an update the queue holds; UPDATE and
// ROLL BACK apart; the line on the check for updates in each state; UPDATE,
// CHECK NOW and GET MORE MODS… pressed and keyed; the parts hidden in a
// game; Tab's order, the arrows and the names; every part inside the
// dialog at Compact, Regular and Large; and a page with no updates, whose
// parts are today's but for the list's height, the check line and GET MORE
// MODS….

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/text.hpp"
#include "oa/ui/kit/theme.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

namespace settings = oa::ui::engine_settings;
namespace renderer = oa::ui::frontend_renderer;
namespace geometry = oa::ui::engine_settings::geometry;
namespace kit = oa::ui::kit;

using renderer::SourceRect;
using settings::DialogAction;
using settings::DialogKey;
using settings::Lock;
using settings::Page;

/// The made-up mods: a title of 24 characters, then four more.
const std::vector<std::string> kNames{
    "Thunderhead Valley Redux", "Quartz", "Basalt", "Slate", "Marble"
};
const std::vector<std::string> kFolders{
    "/games/ta/mods/thunderhead",
    "/games/ta/mods/quartz",
    "/games/ta/mods/basalt",
    "/games/ta/mods/slate",
    "/games/ta/mods/marble",
};
/// Their places among kFolders.
constexpr int32_t kThunderhead = 0;
constexpr int32_t kQuartz = 1;
constexpr int32_t kBasalt = 2;
constexpr int32_t kSlate = 3;
constexpr int32_t kMarble = 4;

/// The rows' lines on their updates.
constexpr std::string_view kChanges = "Revision 4 changes the rules · 3.2 MB";
constexpr std::string_view kMayChange = "Revision 7 may change the rules · 3.2 MB";
constexpr std::string_view kBlocked = "Downloads from Fixture Extra are off (Settings › Downloads)";
constexpr std::string_view kUpdating = "Updating · 28%";

/// Returns what Mods shows of kFolders: Thunderhead Valley Redux and Quartz
/// keep an earlier version and have an update, one changing the rules and
/// one whose rules cannot be known; Basalt keeps an earlier version and has
/// no update; Slate's update is blocked; Marble's is on its way.
///
/// @param updates false leaves every update out, as a page with no catalogue
/// @return the details
std::vector<settings::ModDetails> details_of(bool updates) {
    std::vector<settings::ModDetails> details(kFolders.size());
    const std::vector<std::string> ids{"thunderhead", "quartz", "basalt", "slate", "marble"};
    for (std::size_t index = 0; index < details.size(); ++index) {
        details[index].version = "1.0";
        details[index].description = "A made-up mod of the tests.";
        details[index].profile_id = ids[index];
    }
    for (const int32_t kept : {kThunderhead, kQuartz, kBasalt}) {
        details[static_cast<std::size_t>(kept)].roll_back_from = "1.0 revision 3";
        details[static_cast<std::size_t>(kept)].roll_back_to = "1.0 revision 2";
    }
    if (!updates)
        return details;
    const auto offer = [&details](int32_t place, std::string_view text, bool enabled) {
        settings::ModDetails& row = details[static_cast<std::size_t>(place)];
        row.update_offered = true;
        row.update_text = std::string(text);
        row.update_enabled = enabled;
        row.update_entry = "fixture-extra\n" + row.profile_id;
    };
    offer(kThunderhead, kChanges, true);
    offer(kQuartz, kMayChange, true);
    offer(kSlate, "Revision 2 available · 1.0 MB", false);
    details[static_cast<std::size_t>(kSlate)].update_blocked = std::string(kBlocked);
    offer(kMarble, "Revision 9 available · 64.0 MB", false);
    details[static_cast<std::size_t>(kMarble)].update_progress = std::string(kUpdating);
    return details;
}

/// Opens the dialog on Mods with kFolders offered and none played.
///
/// @param updates false leaves every update out
/// @param locks what cannot be changed
/// @return the dialog
settings::Dialog mods_dialog(bool updates = true, const settings::Locks& locks = {}) {
    const auto details = details_of(updates);
    settings::Dialog dialog;
    settings::open_dialog(
        dialog,
        settings::EngineSettings{},
        settings::EngineSettings{},
        locks,
        "v0.8.0",
        Page::mods,
        {},
        settings::highest_unit_limit,
        settings::ModOffer{kNames, kFolders, details, {}}
    );
    return dialog;
}

/// Gives a dialog the line on the check for updates and GET MORE MODS…, as
/// the host does on the main menu.
///
/// @param[in,out] dialog the dialog
void offer_checks(settings::Dialog& dialog) {
    dialog.mods_check_text = "Checked for updates 4 min ago";
    dialog.mods_check_offered = true;
    dialog.mods_library_offered = true;
}

/// Returns the place among Mods' rows of the row that lists a folder.
///
/// @param dialog the dialog
/// @param offered the folder's place among Dialog::mod_folders
/// @return the row's place
std::size_t row_of(const settings::Dialog& dialog, int32_t offered) {
    const auto rows = settings::mod_rows(dialog);
    for (std::size_t index = 0; index < rows.size(); ++index)
        if (rows[index].offered == offered)
            return index;
    return rows.size();
}

/// Returns the mod row that lists a folder.
settings::ModRow mod_row(int32_t offered) {
    return settings::ModRow{offered, false};
}

bool inside(const SourceRect& inner, const SourceRect& outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

bool overlap(const SourceRect& a, const SourceRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

bool same_rect(const SourceRect& a, const SourceRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

struct Point {
    int32_t x{};
    int32_t y{};
};

Point centre(const SourceRect& rect) {
    return {rect.x + rect.width / 2, rect.y + rect.height / 2};
}

DialogAction click(settings::Dialog& dialog, Point point) {
    static_cast<void>(settings::dialog_pointer_move(dialog, point.x, point.y));
    static_cast<void>(settings::dialog_pointer_down(dialog, point.x, point.y));
    return settings::dialog_pointer_up(dialog, point.x, point.y);
}

/// Returns a control of a display list by its name.
///
/// @param list the list
/// @param name the name
/// @return a copy of the control; nothing when the list has none of that name
std::optional<kit::Control> named(const kit::DisplayList& list, std::string_view name) {
    for (const kit::Control& control : list.controls)
        if (control.name == name)
            return control;
    return std::nullopt;
}

/// Returns the texts a display list draws inside a rectangle, top to bottom,
/// in one colour.
///
/// @param list the list
/// @param area the rectangle
/// @param colour the colour
/// @return the texts
std::vector<std::string>
texts_in(const kit::DisplayList& list, const SourceRect& area, kit::Colour colour) {
    std::vector<std::string> found;
    for (const kit::Item& item : list.items)
        if (item.role == kit::Role::text && item.colour == colour && inside(item.rect, area))
            found.push_back(item.text);
    return found;
}

/// Joins lines with spaces.
std::string joined(const std::vector<std::string>& lines) {
    std::string whole;
    for (const std::string& line : lines) {
        if (!whole.empty())
            whole += ' ';
        whole += line;
    }
    return whole;
}

/// Walks Tab's order once round from no focus.
std::vector<int32_t> tab_round(settings::Dialog dialog) {
    std::vector<int32_t> order;
    for (int32_t press = 0; press < 200; ++press) {
        if (settings::dialog_key(dialog, DialogKey::tab) != DialogAction::redraw)
            break;
        if (!order.empty() && dialog.focused == order.front())
            break;
        order.push_back(dialog.focused);
    }
    return order;
}

void rows_without_an_update_draw_as_before() {
    // Basalt's row and No Mod's draw as they did, item for item, wherever
    // the rows with updates above them have moved them.
    settings::Dialog with = mods_dialog(true);
    settings::Dialog without = mods_dialog(false);
    for (settings::Dialog* dialog : {&with, &without})
        dialog->size_class = kit::SizeClass::large;
    const auto with_open = geometry::open_rows(with);
    const auto without_open = geometry::open_rows(without);
    const kit::DisplayList with_list = geometry::dialog_list(with, nullptr);
    const kit::DisplayList without_list = geometry::dialog_list(without, nullptr);
    for (const int32_t offered : {settings::no_mod_row, kBasalt}) {
        const std::size_t at = row_of(with, offered);
        const SourceRect& near = with_open.rows.rows[at].control_area;
        const SourceRect& before = without_open.rows.rows[at].control_area;
        CHECK(near.height == geometry::mod_row_height && before.height == geometry::mod_row_height);
        const int32_t moved = near.y - before.y;
        std::vector<std::string> drawn;
        std::vector<std::string> drew;
        for (const kit::Item& item : with_list.items)
            if (inside(item.rect, near))
                drawn.push_back(
                    std::to_string(static_cast<int>(item.role)) + " " + item.text + " " +
                    std::to_string(item.rect.x) + "," + std::to_string(item.rect.y - moved) + "," +
                    std::to_string(item.rect.width) + "," + std::to_string(item.rect.height) + " " +
                    std::to_string(item.colour.r) + std::to_string(item.colour.g) +
                    std::to_string(item.control)
                );
        for (const kit::Item& item : without_list.items)
            if (inside(item.rect, before))
                drew.push_back(
                    std::to_string(static_cast<int>(item.role)) + " " + item.text + " " +
                    std::to_string(item.rect.x) + "," + std::to_string(item.rect.y) + "," +
                    std::to_string(item.rect.width) + "," + std::to_string(item.rect.height) + " " +
                    std::to_string(item.colour.r) + std::to_string(item.colour.g) +
                    std::to_string(item.control)
                );
        CHECK(!drawn.empty() && drawn == drew);
    }
    // Without updates no row has UPDATE, and every row is 28 rows tall.
    for (const auto& row : without_open.rows.rows)
        CHECK(row.control_area.height == geometry::mod_row_height && row.hint_lines == 1);
    for (const kit::Control& control : without_list.controls)
        CHECK(control.name.find(".update") == std::string::npos);
    // With them, each row with an update has UPDATE, and the others none.
    for (const std::string_view name :
         {"settings.mod.thunderhead.update",
          "settings.mod.quartz.update",
          "settings.mod.slate.update",
          "settings.mod.marble.update"})
        CHECK(named(with_list, name).has_value());
    CHECK(!named(with_list, "settings.mod.basalt.update").has_value());
    CHECK(!named(with_list, "settings.mod.no-mod.update").has_value());
}

void an_update_line_shows_whole_beside_update_and_roll_back() {
    // At Compact, beside UPDATE and ROLL BACK, a title of 24 characters: the
    // line on an update that changes the rules, and on one whose rules
    // cannot be known, show whole over the lines they need, in the warning
    // colour, and the row grows for them.
    settings::Dialog dialog = mods_dialog();
    CHECK(kNames[static_cast<std::size_t>(kThunderhead)].size() == 24);
    for (const auto& [offered, line] :
         {std::pair{kThunderhead, kChanges}, std::pair{kQuartz, kMayChange}}) {
        const std::vector<std::string> lines = geometry::update_lines(dialog, mod_row(offered));
        CHECK(lines.size() == 2);
        CHECK(joined(lines) == line);
        // Each line fits the room left of the buttons at the dialog's count of
        // the small font's characters, so it is never cut.
        const geometry::Sizes& sized = geometry::sizes_of(dialog);
        const int32_t room = sized.content_width - 3 * geometry::mod_row_inset -
                             geometry::mod_badge_side - geometry::mod_update_width -
                             geometry::mod_roll_back_width - 2 * geometry::mod_roll_back_gap;
        for (const std::string& each : lines)
            CHECK(
                static_cast<int64_t>(kit::character_count(each)) * sized.content_width <=
                static_cast<int64_t>(room) * static_cast<int64_t>(sized.hint_line_characters)
            );
        dialog.scroll[static_cast<std::size_t>(Page::mods)] = 0;
        const std::size_t at = row_of(dialog, offered);
        // Bring the row into view.
        auto open = geometry::open_rows(dialog);
        dialog.scroll[static_cast<std::size_t>(Page::mods)] =
            std::min(open.limit, std::max(open.rows.rows[at].top - open.area.view.y, int32_t{0}));
        open = geometry::open_rows(dialog);
        const auto& row = open.rows.rows[at];
        CHECK(row.control_area.height == geometry::mod_row_height + geometry::hint_line_height);
        const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
        const std::vector<std::string> shown = texts_in(list, row.control_area, kit::colour::lock);
        CHECK(shown == lines);
        const SourceRect update = geometry::update_button(row, true);
        const SourceRect roll_back = geometry::roll_back_button(row);
        for (const kit::Item& item : list.items)
            if (item.role == kit::Role::text && item.colour == kit::colour::lock &&
                inside(item.rect, row.control_area)) {
                CHECK(item.text.find(geometry::path_ellipsis) == std::string::npos);
                CHECK(item.rect.x + item.rect.width <= update.x);
                CHECK(!overlap(item.rect, update) && !overlap(item.rect, roll_back));
            }
        // The description does not show in its place.
        CHECK(texts_in(list, row.control_area, kit::colour::hint).size() == 1);
    }
}

void a_blocked_update_says_why_and_takes_no_press() {
    settings::Dialog dialog = mods_dialog();
    dialog.size_class = kit::SizeClass::large;
    const auto open = geometry::open_rows(dialog);
    const std::size_t at = row_of(dialog, kSlate);
    const auto& row = open.rows.rows[at];
    CHECK(
        geometry::mod_update_line(*geometry::update_details(dialog, mod_row(kSlate))) == kBlocked
    );
    const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
    CHECK(joined(texts_in(list, row.control_area, kit::colour::lock)) == kBlocked);
    const auto update = named(list, "settings.mod.slate.update");
    CHECK(update.has_value() && !update->enabled);
    // A press on it asks nothing, not even the Switch Mod question of the
    // row under it.
    const SourceRect button = geometry::update_button(row, false);
    CHECK(update.has_value() && same_rect(update->rect, button));
    CHECK(click(dialog, centre(button)) == DialogAction::none);
    CHECK(dialog.switch_question == settings::no_question);
    // Its part is listed with no control.
    const auto parts = settings::dialog_layout(dialog);
    bool listed = false;
    for (const auto& part : parts)
        if (same_rect(part.rect, button)) {
            listed = true;
            CHECK(part.control == settings::no_control);
            CHECK(part.text == geometry::mod_update_caption);
        }
    CHECK(listed);
}

void an_update_the_queue_holds_says_where_it_stands() {
    settings::Dialog dialog = mods_dialog();
    dialog.size_class = kit::SizeClass::large;
    const auto open = geometry::open_rows(dialog);
    const auto& row = open.rows.rows[row_of(dialog, kMarble)];
    const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
    CHECK(
        texts_in(list, row.control_area, kit::colour::lock) ==
        std::vector<std::string>{std::string(kUpdating)}
    );
    const auto update = named(list, "settings.mod.marble.update");
    CHECK(update.has_value() && !update->enabled);
    // While the queue holds it, its progress shows in place of its line
    // even where UPDATE could be pressed.
    settings::ModDetails held;
    held.update_offered = true;
    held.update_enabled = true;
    held.update_text = "Revision 2 available · 1.0 MB";
    held.update_progress = "Waiting · finishes when you leave Marble";
    CHECK(geometry::mod_update_line(held) == held.update_progress);
    held.update_progress.clear();
    CHECK(geometry::mod_update_line(held) == held.update_text);
}

void update_and_roll_back_lie_apart() {
    for (const kit::SizeClass size_class :
         {kit::SizeClass::compact, kit::SizeClass::regular, kit::SizeClass::large}) {
        settings::Dialog dialog = mods_dialog();
        dialog.size_class = size_class;
        const auto open = geometry::open_rows(dialog);
        const auto& both = open.rows.rows[row_of(dialog, kThunderhead)];
        const SourceRect update = geometry::update_button(both, true);
        const SourceRect roll_back = geometry::roll_back_button(both);
        CHECK(update.width == 60 && update.height == 12);
        CHECK(!overlap(update, roll_back));
        CHECK(update.x + update.width + geometry::mod_roll_back_gap == roll_back.x);
        CHECK(update.y == roll_back.y);
        CHECK(inside(update, both.control_area) && inside(roll_back, both.control_area));
        // Without ROLL BACK, UPDATE takes its place.
        const auto& alone = open.rows.rows[row_of(dialog, kSlate)];
        CHECK(same_rect(geometry::update_button(alone, false), geometry::roll_back_button(alone)));
    }
}

void the_check_line_says_where_the_check_stands() {
    struct Case {
        std::string_view text;
        bool warning;
        bool offered;
    };

    for (const Case& each : {
             Case{"Checked for updates 4 min ago", false, true},
             Case{"Checking for updates…", false, false},
             Case{"Couldn't check for updates", true, true},
             Case{"No registries are on.", false, false},
         }) {
        settings::Dialog dialog = mods_dialog();
        dialog.mods_check_text = std::string(each.text);
        dialog.mods_check_warning = each.warning;
        dialog.mods_check_offered = each.offered;
        const SourceRect& line = geometry::mods_check_line;
        const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
        const kit::Colour colour = each.warning ? kit::colour::lock : kit::colour::hint;
        const std::vector<std::string> drawn = texts_in(list, line, colour);
        CHECK(!drawn.empty() && drawn.front() == each.text);
        const auto link = named(list, "settings.check-now");
        CHECK(link.has_value() == each.offered);
        const auto parts = settings::dialog_layout(dialog);
        bool text_listed = false;
        bool link_listed = false;
        for (const auto& part : parts) {
            text_listed = text_listed || (part.text == each.text && inside(part.rect, line));
            link_listed = link_listed || part.text == geometry::check_now_text;
        }
        CHECK(text_listed);
        CHECK(link_listed == each.offered);
        if (!link)
            continue;
        // CHECK NOW follows the text and " · " on the line, inside it.
        CHECK(link->kind == kit::ControlKind::link && link->text == geometry::check_now_text);
        CHECK(inside(link->rect, line));
        CHECK(link->rect.x >= line.x + static_cast<int32_t>(kit::character_count(each.text)) * 5);
        CHECK(texts_in(list, line, kit::colour::hint).back() == geometry::check_now_separator);
    }
    // No text, as a host gives none: nothing shows on the line.
    settings::Dialog quiet = mods_dialog();
    const kit::DisplayList list = geometry::dialog_list(quiet, nullptr);
    for (const kit::Item& item : list.items)
        CHECK(!(item.role == kit::Role::text && inside(item.rect, geometry::mods_check_line)));
}

void update_check_now_and_get_more_mods_ask_the_host() {
    settings::Dialog dialog = mods_dialog();
    offer_checks(dialog);
    dialog.size_class = kit::SizeClass::large;
    // UPDATE names the row's folder.
    auto open = geometry::open_rows(dialog);
    const std::size_t at = row_of(dialog, kThunderhead);
    CHECK(
        click(dialog, centre(geometry::update_button(open.rows.rows[at], true))) ==
        DialogAction::update_mod
    );
    CHECK(dialog.update_folder == kFolders[static_cast<std::size_t>(kThunderhead)]);
    CHECK(dialog.switch_question == settings::no_question);
    // CHECK NOW asks the host to check.
    const auto link = named(geometry::dialog_list(dialog, nullptr), "settings.check-now");
    CHECK(link.has_value());
    if (link)
        CHECK(click(dialog, centre(link->rect)) == DialogAction::check_for_updates);
    // GET MORE MODS… asks for the Library on Mods.
    dialog.library_tab = settings::LibraryTab::languages;
    const SourceRect more = geometry::get_more_mods_button(dialog);
    CHECK(click(dialog, centre(more)) == DialogAction::open_library);
    CHECK(dialog.library_tab == settings::LibraryTab::mods);
    // Space presses each, focused, as a controller's A does, which sends
    // Space to the dialog's focused control; Enter stays OK, as on every
    // control of the dialog.
    open = geometry::open_rows(dialog);
    for (const auto& [control, action] :
         {std::pair{
              geometry::update_control(open.rows, row_of(dialog, kQuartz)), DialogAction::update_mod
          },
          std::pair{geometry::check_now_control(open.rows), DialogAction::check_for_updates},
          std::pair{geometry::get_more_mods_control(open.rows), DialogAction::open_library}}) {
        dialog.focused = control;
        CHECK(settings::dialog_key(dialog, DialogKey::space) == action);
        settings::Dialog entered = dialog;
        entered.focused = control;
        CHECK(settings::dialog_key(entered, DialogKey::enter) == DialogAction::accepted);
    }
    CHECK(dialog.update_folder == kFolders[static_cast<std::size_t>(kQuartz)]);
    // A finger near GET MORE MODS… takes it.
    CHECK(
        settings::dialog_finger_down(dialog, more.x + more.width / 2, more.y - 3, 8) ==
        DialogAction::redraw
    );
    CHECK(dialog.pressed == geometry::get_more_mods_control(open.rows));
    CHECK(
        settings::dialog_pointer_up(dialog, more.x + more.width / 2, more.y - 3) ==
        DialogAction::open_library
    );
    // Not offered, they are neither drawn nor pressed.
    settings::Dialog plain = mods_dialog();
    const kit::DisplayList list = geometry::dialog_list(plain, nullptr);
    CHECK(!named(list, "settings.check-now") && !named(list, "settings.get-more-mods"));
    CHECK(click(plain, centre(geometry::get_more_mods_button(plain))) == DialogAction::none);
}

void in_a_game_update_check_now_and_get_more_mods_are_hidden() {
    settings::Locks locks{};
    locks.mod = Lock::in_game;
    settings::Dialog dialog = mods_dialog(true, locks);
    offer_checks(dialog);
    dialog.size_class = kit::SizeClass::large;
    const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
    for (const kit::Control& control : list.controls) {
        CHECK(control.name.find(".update") == std::string::npos);
        CHECK(control.name != "settings.check-now" && control.name != "settings.get-more-mods");
    }
    const auto parts = settings::dialog_layout(dialog);
    for (const auto& part : parts) {
        CHECK(part.text != geometry::mod_update_caption);
        CHECK(part.text != geometry::check_now_text && part.text != geometry::get_more_mods_text);
    }
    // The lines on the updates and the check still say what they say.
    const auto open = geometry::open_rows(dialog);
    const auto& row = open.rows.rows[row_of(dialog, kThunderhead)];
    CHECK(joined(texts_in(list, row.control_area, kit::colour::lock)) == kChanges);
    CHECK(!texts_in(list, geometry::sizes_of(dialog).mods_check_line, kit::colour::hint).empty());
    // A press where they would be asks nothing.
    CHECK(click(dialog, centre(geometry::update_button(row, true))) == DialogAction::none);
    CHECK(click(dialog, centre(geometry::get_more_mods_button(dialog))) == DialogAction::none);
    CHECK(click(dialog, centre(geometry::check_now_link(dialog))) == DialogAction::none);
}

void the_focus_reaches_update_after_its_row() {
    settings::Dialog dialog = mods_dialog();
    offer_checks(dialog);
    dialog.size_class = kit::SizeClass::large;
    const auto open = geometry::open_rows(dialog);
    const auto rows = settings::mod_rows(dialog);
    // Tab: each row, then its UPDATE while it can be pressed, then its ROLL
    // BACK; then OPEN MODS FOLDER, CHECK NOW and GET MORE MODS….
    std::vector<int32_t> expected;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        expected.push_back(settings::first_row_control + static_cast<int32_t>(index));
        const settings::ModDetails* update = geometry::update_details(dialog, rows[index]);
        if (update != nullptr && update->update_enabled)
            expected.push_back(geometry::update_control(open.rows, index));
        if (geometry::offers_roll_back(dialog, rows[index]))
            expected.push_back(geometry::roll_back_control(open.rows, index));
    }
    expected.push_back(geometry::mods_folder_control(open.rows));
    expected.push_back(geometry::check_now_control(open.rows));
    expected.push_back(geometry::get_more_mods_control(open.rows));
    const std::vector<int32_t> walked = tab_round(dialog);
    CHECK(walked.size() > expected.size());
    CHECK(std::equal(expected.begin(), expected.end(), walked.begin()));
    CHECK(walked.size() > expected.size() && walked[expected.size()] == settings::restore_control);
    // Right from a row reaches its UPDATE, then its ROLL BACK; Left comes back.
    const std::size_t at = row_of(dialog, kThunderhead);
    dialog.focused = settings::first_row_control + static_cast<int32_t>(at);
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.focused == geometry::update_control(open.rows, at));
    CHECK(settings::dialog_key(dialog, DialogKey::right) == DialogAction::redraw);
    CHECK(dialog.focused == geometry::roll_back_control(open.rows, at));
    CHECK(settings::dialog_key(dialog, DialogKey::left) == DialogAction::redraw);
    CHECK(dialog.focused == settings::first_row_control + static_cast<int32_t>(at));
    // The names, beside the row's own, and the controls' captions.
    const kit::DisplayList list = geometry::dialog_list(dialog, nullptr);
    const auto update = named(list, "settings.mod.thunderhead.update");
    CHECK(update.has_value() && update->id == geometry::update_control(open.rows, at));
    CHECK(update.has_value() && update->kind == kit::ControlKind::button && update->enabled);
    CHECK(update.has_value() && update->text == geometry::mod_update_caption);
    CHECK(named(list, "settings.mod.thunderhead.switch").has_value());
    CHECK(named(list, "settings.mod.thunderhead.roll-back").has_value());
    const auto more = named(list, "settings.get-more-mods");
    CHECK(more.has_value() && more->text == geometry::get_more_mods_text);
    CHECK(named(list, "settings.check-now").has_value());
    CHECK(named(list, "settings.open-mods-folder").has_value());
    // Focusing a row's UPDATE brings its row into view.
    settings::Dialog compact = mods_dialog();
    const auto compact_open = geometry::open_rows(compact);
    const std::size_t last = row_of(compact, kThunderhead);
    CHECK(compact_open.limit > 0);
    compact.focused = settings::first_row_control + static_cast<int32_t>(last);
    CHECK(settings::dialog_key(compact, DialogKey::tab) == DialogAction::redraw);
    CHECK(compact.focused == geometry::update_control(compact_open.rows, last));
    const auto shown = geometry::open_rows(compact);
    CHECK(inside(shown.rows.rows[last].control_area, shown.area.view));
}

void every_part_lies_inside_at_every_class() {
    for (const kit::SizeClass size_class :
         {kit::SizeClass::compact, kit::SizeClass::regular, kit::SizeClass::large}) {
        settings::Dialog dialog = mods_dialog();
        offer_checks(dialog);
        dialog.size_class = size_class;
        const geometry::Sizes& sized = geometry::sizes_of(dialog);
        const SourceRect whole{0, 0, sized.dialog_width, sized.dialog_height};
        const auto first = geometry::open_rows(dialog);
        // Under the list, the check line, then OPEN MODS FOLDER and GET MORE
        // MODS… in one row, then the notes, all in the content column.
        const SourceRect& view = first.area.view;
        const SourceRect& check_line = sized.mods_check_line;
        const SourceRect& folder = sized.mods_folder_button;
        const SourceRect more = geometry::get_more_mods_button(dialog);
        CHECK(view.y + view.height + geometry::mods_list_gap == check_line.y);
        CHECK(check_line.y + check_line.height + geometry::mods_check_gap == folder.y);
        CHECK(more.y == folder.y && more.height == folder.height);
        CHECK(more.x == folder.x + folder.width + geometry::mods_library_gap);
        CHECK(more.x + more.width <= sized.content_right);
        CHECK(
            more.width == kit::estimated_width(geometry::get_more_mods_text) +
                              kit::compact_metrics.prompt_button_padding
        );
        CHECK(folder.y + folder.height <= sized.mods_note_first.y);
        CHECK(inside(geometry::check_now_link(dialog), check_line));
        for (const SourceRect& part :
             {check_line, folder, more, sized.mods_note_first, sized.mods_note_second})
            CHECK(
                inside(part, whole) && part.x >= sized.content_left &&
                part.x + part.width <= sized.content_right
            );
        CHECK(!overlap(more, folder) && !overlap(check_line, folder) && !overlap(view, check_line));
        // At every scroll, each part listed lies inside the dialog, a row's
        // parts inside the list's view, and a row's UPDATE and ROLL BACK
        // inside their row.
        for (int32_t scroll = 0; scroll <= first.limit + 5; scroll += 5) {
            dialog.scroll[static_cast<std::size_t>(Page::mods)] = scroll;
            const auto open = geometry::open_rows(dialog);
            const auto parts = settings::dialog_layout(dialog);
            for (const auto& part : parts)
                CHECK(inside(part.rect, whole));
            const auto rows = settings::mod_rows(dialog);
            for (std::size_t index = 0; index < open.rows.rows.size(); ++index) {
                const auto& row = open.rows.rows[index];
                if (!inside(row.control_area, open.area.view))
                    continue;
                const bool roll_back = geometry::offers_roll_back(dialog, rows[index]);
                if (geometry::shows_update(dialog, rows[index]))
                    CHECK(inside(geometry::update_button(row, roll_back), row.control_area));
                for (std::size_t line = 0; line < row.hint_lines; ++line)
                    CHECK(inside(row.hints[line], row.control_area));
                for (const auto& part : parts)
                    if (overlap(part.rect, row.control_area))
                        CHECK(inside(part.rect, open.area.view));
            }
        }
    }
}

void a_page_with_no_updates_keeps_todays_parts() {
    // The parts Mods listed before updates, at Compact, as dialog_test pins
    // them: the rows from the first row's top, ROLL BACK in each row that
    // keeps a version, OPEN MODS FOLDER and the two notes under it. Only the
    // list's view is a line shorter, for the check line over OPEN MODS
    // FOLDER, and the check line and GET MORE MODS… show where the host
    // gives them.
    settings::Dialog dialog = mods_dialog(false);
    const auto open = geometry::open_rows(dialog);
    CHECK(same_rect(open.area.view, {158, 54, 309, 169}));
    CHECK(same_rect(geometry::mods_check_line, {158, 225, 309, 12}));
    CHECK(same_rect(geometry::mods_folder_button, {158, 241, 104, 17}));
    CHECK(same_rect(geometry::mods_note_first, {158, 262, 309, 12}));
    CHECK(same_rect(geometry::mods_note_second, {158, 274, 309, 12}));
    CHECK(same_rect(geometry::get_more_mods_button(dialog), {268, 241, 114, 17}));
    const int32_t stride = geometry::mod_row_height + geometry::mod_row_gap;
    for (std::size_t index = 0; index < open.rows.rows.size(); ++index) {
        const auto& row = open.rows.rows[index];
        CHECK(same_rect(
            row.control_area,
            {158, 54 + static_cast<int32_t>(index) * stride, 309, geometry::mod_row_height}
        ));
        CHECK(row.hint_lines == 1 && row.hints[0].y == row.top + geometry::mod_description_top);
    }
    const auto parts = settings::dialog_layout(dialog);
    // With the host's check line and GET MORE MODS…, the same parts and
    // three more.
    settings::Dialog offered = mods_dialog(false);
    offer_checks(offered);
    const auto more_parts = settings::dialog_layout(offered);
    CHECK(more_parts.size() == parts.size() + 3);
    std::size_t matched = 0;
    for (const auto& part : parts)
        for (const auto& other : more_parts)
            if (part.text == other.text && part.control == other.control &&
                same_rect(part.rect, other.rect)) {
                ++matched;
                break;
            }
    CHECK(matched == parts.size());
    // No part of today's lies in the check line or GET MORE MODS…'s place.
    for (const auto& part : parts) {
        CHECK(!overlap(part.rect, geometry::mods_check_line));
        CHECK(!overlap(part.rect, geometry::get_more_mods_button(dialog)));
    }
}

} // namespace

int main() {
    rows_without_an_update_draw_as_before();
    an_update_line_shows_whole_beside_update_and_roll_back();
    a_blocked_update_says_why_and_takes_no_press();
    an_update_the_queue_holds_says_where_it_stands();
    update_and_roll_back_lie_apart();
    the_check_line_says_where_the_check_stands();
    update_check_now_and_get_more_mods_ask_the_host();
    in_a_game_update_check_now_and_get_more_mods_are_hidden();
    the_focus_reaches_update_after_its_row();
    every_part_lies_inside_at_every_class();
    a_page_with_no_updates_keeps_todays_parts();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    return 0;
}
