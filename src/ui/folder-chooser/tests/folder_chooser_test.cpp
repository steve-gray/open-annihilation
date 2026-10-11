// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The chooser's list and browser laid out at 1280x800 (the Steam Deck's
// screen) and 852x393 (a phone, with its safe area), the focus order, the
// keys, presses and scrolling. Every layout of every case at both sizes
// keeps the digest layout-digests.txt records, the file the test is given;
// --write-digests prints that file's content.
#include "oa/base/sha256.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/folder_chooser.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <map>
#include <span>
#include <stdint.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace chooser = oa::ui::folder_chooser;
namespace gf = oa::ui::game_files;
using chooser::Target;
using chooser::TargetKind;

/// The Steam Deck's screen, one canvas pixel a point.
chooser::Viewport deck_viewport() {
    chooser::Viewport viewport;
    viewport.width = 1280;
    viewport.height = 800;
    viewport.px_per_point = 1.0f;
    return viewport;
}

/// A phone's screen held sideways, at three canvas pixels a point, with its safe area.
chooser::Viewport phone_viewport() {
    chooser::Viewport viewport;
    viewport.width = 852 * 3;
    viewport.height = 393 * 3;
    viewport.px_per_point = 3.0f;
    viewport.safe = {59 * 3, 0, 59 * 3, 21 * 3};
    return viewport;
}

/// The list with two folders found (one usable), as the app fills it.
chooser::Model list_model() {
    chooser::Model model;
    model.version = "v0.7";
    model.installs = {
        {"/home/deck/.local/share/Steam/steamapps/common/Total Annihilation",
         "your Steam library",
         "Total Annihilation 3.1c with Core Contingency and Battle Tactics.",
         true},
        {"/home/deck/Games/Heroic/Total Annihilation",
         "Heroic",
         "Its archives lack guis/mainmenu.gui.",
         false},
    };
    model.offers_dialog = true;
    return model;
}

/// The browser in a folder with many folders, a few of them game folders.
///
/// @param usable the folder shown can be played
chooser::Model browse_model(bool usable) {
    chooser::Model model;
    model.version = "v0.7";
    model.view = chooser::View::browse;
    model.folder = "/home/deck/Games";
    model.place_labels = {"Home", "Downloads", "SD card"};
    model.has_parent = true;
    model.folder_usable = usable;
    model.folder_verdict =
        usable ? "Total Annihilation 3.1c." : "It holds no Total Annihilation archives.";
    for (int index = 0; index < 30; ++index)
        model.entries.push_back({"Folder " + std::to_string(index), index % 7 == 3});
    return model;
}

/// Returns a box's centre.
///
/// @param box the box
/// @return its centre
chooser::Point centre(chooser::Rect box) {
    return {box.x + box.width / 2, box.y + box.height / 2};
}

/// Finds a target's box.
///
/// @param layout the layout
/// @param target the target
/// @return the box; none when the target is not laid out
const chooser::HitBox* find(const chooser::Layout& layout, Target target) {
    for (const chooser::HitBox& hit : layout.targets)
        if (hit.target == target)
            return &hit;
    return nullptr;
}

/// Tells whether a box lies in another.
///
/// @param inner the box
/// @param outer the box it should lie in
/// @return true when it does
bool inside(chooser::Rect inner, chooser::Rect outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

/// Checks what every layout keeps: each control at least 44 points high, inside the safe area
/// across, and a focus order of the enabled targets in their order.
///
/// @param layout the layout
/// @param viewport its canvas
void check_controls(const chooser::Layout& layout, const chooser::Viewport& viewport) {
    const chooser::Rect safe{
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom,
    };
    const int least = static_cast<int>(gf::min_button_points * viewport.px_per_point);
    OA_CHECK(!layout.targets.empty());
    std::vector<Target> enabled;
    for (const chooser::HitBox& hit : layout.targets) {
        OA_CHECK(hit.box.height >= least);
        OA_CHECK(hit.box.x >= safe.x && hit.box.x + hit.box.width <= safe.x + safe.width);
        if (hit.clip.height <= 0)
            OA_CHECK(inside(hit.box, safe));
        if (hit.enabled)
            enabled.push_back(hit.target);
    }
    OA_CHECK(enabled == layout.focus_order);
    // Every painted item but the scrolled ones lies on the canvas.
    for (const oa::ui::kit::Item& item : layout.paint.list.items)
        if (item.clip.height <= 0)
            OA_CHECK(inside(item.rect, {0, 0, viewport.width, viewport.height}));
}

/// Checks the list's targets, their order and where presses go.
void check_list() {
    for (const chooser::Viewport& viewport : {deck_viewport(), phone_viewport()}) {
        const chooser::Model model = list_model();
        const chooser::UiState state;
        const chooser::Layout layout = chooser::lay_out(model, state, viewport, {});
        check_controls(layout, viewport);
        const std::vector<Target> order{
            {TargetKind::install, 0},
            {TargetKind::browse, 0},
            {TargetKind::demo, 0},
            {TargetKind::dialog, 0},
            {TargetKind::quit, 0},
        };
        OA_CHECK(layout.focus_order == order);
        const chooser::HitBox* unusable = find(layout, {TargetKind::install, 1});
        OA_CHECK(unusable != nullptr && !unusable->enabled);
        if (unusable != nullptr && unusable->clip.height <= 0)
            OA_CHECK(
                chooser::hit_test(layout, centre(unusable->box), 100).kind == TargetKind::none
            );
        for (const Target& target : order) {
            const chooser::HitBox* hit = find(layout, target);
            OA_CHECK(hit != nullptr);
            if (hit != nullptr && (hit->clip.height <= 0 || inside(hit->box, hit->clip)))
                OA_CHECK(chooser::hit_test(layout, centre(hit->box), 0) == target);
        }
        // No dialog offered (Game Mode): no row for it.
        chooser::Model game_mode = list_model();
        game_mode.offers_dialog = false;
        const chooser::Layout without = chooser::lay_out(game_mode, state, viewport, {});
        OA_CHECK(find(without, {TargetKind::dialog, 0}) == nullptr);
        OA_CHECK(find(without, {TargetKind::demo, 0}) != nullptr);
        // Nothing found: the ways to a folder and Quit, and a notice above them.
        chooser::Model nothing;
        nothing.notice = "The Total Annihilation folder chosen earlier can no longer be used.";
        const chooser::Layout empty = chooser::lay_out(nothing, state, viewport, {});
        check_controls(empty, viewport);
        OA_CHECK(empty.focus_order.size() == 3);
        OA_CHECK(empty.focus_order.front() == (Target{TargetKind::browse, 0}));
        const bool notice_shown = std::any_of(
            empty.paint.list.items.begin(),
            empty.paint.list.items.end(),
            [](const oa::ui::kit::Item& item) { return item.role == oa::ui::kit::Role::banner; }
        );
        OA_CHECK(notice_shown);
    }
    // Escape does nothing on the list.
    const chooser::Layout layout = chooser::lay_out(list_model(), {}, deck_viewport(), {});
    chooser::UiState state;
    OA_CHECK(chooser::key(layout, state, chooser::Key::escape).command == chooser::Command::none);
}

/// Checks the browser's targets, "Play this folder" and Back.
void check_browser() {
    for (const chooser::Viewport& viewport : {deck_viewport(), phone_viewport()}) {
        const chooser::UiState state;
        const chooser::Layout refused = chooser::lay_out(browse_model(false), state, viewport, {});
        check_controls(refused, viewport);
        const chooser::HitBox* choose = find(refused, {TargetKind::choose, 0});
        OA_CHECK(choose != nullptr && !choose->enabled);
        OA_CHECK(
            std::find(
                refused.focus_order.begin(),
                refused.focus_order.end(),
                Target{TargetKind::choose, 0}
            ) == refused.focus_order.end()
        );
        OA_CHECK(refused.focus_order.size() >= 5);
        OA_CHECK(refused.focus_order[0] == (Target{TargetKind::place, 0}));
        OA_CHECK(refused.focus_order[2] == (Target{TargetKind::place, 2}));
        OA_CHECK(refused.focus_order[3] == (Target{TargetKind::parent, 0}));
        OA_CHECK(refused.focus_order[4] == (Target{TargetKind::entry, 0}));
        OA_CHECK(refused.focus_order.back() == (Target{TargetKind::back, 0}));
        // Thirty folders do not fit: the rows scroll, the buttons stay.
        OA_CHECK(refused.scroll_max_points > 0);
        const chooser::HitBox* back = find(refused, {TargetKind::back, 0});
        OA_CHECK(back != nullptr && back->clip.height <= 0);
        const chooser::Layout usable = chooser::lay_out(browse_model(true), state, viewport, {});
        check_controls(usable, viewport);
        OA_CHECK(usable.focus_order.back() == (Target{TargetKind::choose, 0}));
        const chooser::HitBox* play = find(usable, {TargetKind::choose, 0});
        OA_CHECK(play != nullptr && play->enabled);
        if (play != nullptr)
            OA_CHECK(
                chooser::hit_test(usable, centre(play->box), 0) == (Target{TargetKind::choose, 0})
            );
        // Escape goes back to the list.
        chooser::UiState keys;
        OA_CHECK(
            chooser::key(usable, keys, chooser::Key::escape).command ==
            chooser::Command::back_to_list
        );
        // At the top of a drive there is no parent folder.
        chooser::Model root = browse_model(false);
        root.folder = "/";
        root.has_parent = false;
        const chooser::Layout top = chooser::lay_out(root, state, viewport, {});
        const chooser::HitBox* parent = find(top, {TargetKind::parent, 0});
        OA_CHECK(parent != nullptr && !parent->enabled);
        // An empty folder says so.
        chooser::Model empty = browse_model(false);
        empty.entries.clear();
        const chooser::Layout nothing = chooser::lay_out(empty, state, viewport, {});
        OA_CHECK(find(nothing, {TargetKind::entry, 0}) == nullptr);
        OA_CHECK(nothing.scroll_max_points == 0);
    }
}

/// Checks the keys: the first shows the focus, arrows and Tab move it and scroll it into view,
/// Enter presses, the page keys scroll.
void check_keys() {
    const chooser::Viewport viewport = phone_viewport();
    const chooser::Model model = browse_model(true);
    chooser::UiState state;
    chooser::Layout layout = chooser::lay_out(model, state, viewport, {});
    OA_CHECK(chooser::key(layout, state, chooser::Key::down).command == chooser::Command::none);
    OA_CHECK(state.focus_shown && state.focus == 0);
    layout = chooser::lay_out(model, state, viewport, {});
    const bool marked = std::any_of(
        layout.paint.list.items.begin(),
        layout.paint.list.items.end(),
        [](const oa::ui::kit::Item& item) { return item.state.focused; }
    );
    OA_CHECK(marked);
    // Right along the places, to the parent folder, then down into the folders.
    static_cast<void>(chooser::key(layout, state, chooser::Key::right));
    OA_CHECK(state.focus == 1);
    static_cast<void>(chooser::key(layout, state, chooser::Key::right));
    static_cast<void>(chooser::key(layout, state, chooser::Key::right));
    OA_CHECK(state.focus == 3);
    static_cast<void>(chooser::key(layout, state, chooser::Key::left));
    OA_CHECK(state.focus == 2);
    static_cast<void>(chooser::key(layout, state, chooser::Key::down));
    OA_CHECK(
        layout.focus_order[static_cast<std::size_t>(state.focus)] == (Target{TargetKind::entry, 0})
    );
    // Down through the folders: each one focused is scrolled whole into view.
    for (int step = 0; step < 20; ++step) {
        layout = chooser::lay_out(model, state, viewport, {});
        static_cast<void>(chooser::key(layout, state, chooser::Key::down));
    }
    OA_CHECK(state.scroll_points > 0 && state.scroll_points <= layout.scroll_max_points);
    layout = chooser::lay_out(model, state, viewport, {});
    const Target focused = layout.focus_order[static_cast<std::size_t>(state.focus)];
    OA_CHECK(focused == (Target{TargetKind::entry, 20}));
    const chooser::HitBox* shown = find(layout, focused);
    OA_CHECK(shown != nullptr && inside(shown->box, shown->clip));
    OA_CHECK(
        chooser::key(layout, state, chooser::Key::enter).command == chooser::Command::enter_folder
    );
    OA_CHECK(chooser::key(layout, state, chooser::Key::enter).index == 20);
    // Tab and Shift+Tab go round the focus order.
    const int32_t count = static_cast<int32_t>(layout.focus_order.size());
    state.focus = count - 1;
    static_cast<void>(chooser::key(layout, state, chooser::Key::tab));
    OA_CHECK(state.focus == 0);
    static_cast<void>(chooser::key(layout, state, chooser::Key::back_tab));
    OA_CHECK(state.focus == count - 1);
    OA_CHECK(
        chooser::key(layout, state, chooser::Key::enter).command == chooser::Command::choose_folder
    );
    // Up from Back reaches the last folder, scrolled into view, and down from it the buttons.
    const auto back_place = std::find(
        layout.focus_order.begin(), layout.focus_order.end(), Target{TargetKind::back, 0}
    );
    OA_CHECK(back_place != layout.focus_order.end());
    state.focus = static_cast<int32_t>(back_place - layout.focus_order.begin());
    state.scroll_points = 0;
    layout = chooser::lay_out(model, state, viewport, {});
    static_cast<void>(chooser::key(layout, state, chooser::Key::up));
    OA_CHECK(
        layout.focus_order[static_cast<std::size_t>(state.focus)] == (Target{TargetKind::entry, 29})
    );
    layout = chooser::lay_out(model, state, viewport, {});
    const chooser::HitBox* last = find(layout, {TargetKind::entry, 29});
    OA_CHECK(state.scroll_points > 0 && last != nullptr && inside(last->box, last->clip));
    static_cast<void>(chooser::key(layout, state, chooser::Key::down));
    const TargetKind below = layout.focus_order[static_cast<std::size_t>(state.focus)].kind;
    OA_CHECK(below == TargetKind::back || below == TargetKind::choose);
    // Up from the top row goes nowhere.
    state.focus = 0;
    static_cast<void>(chooser::key(layout, state, chooser::Key::up));
    OA_CHECK(state.focus == 0);
    // The page keys scroll by the rows' height, within the range.
    state.scroll_points = 0;
    static_cast<void>(chooser::key(layout, state, chooser::Key::page_down));
    OA_CHECK(state.scroll_points > 0);
    for (int page = 0; page < 20; ++page)
        static_cast<void>(chooser::key(layout, state, chooser::Key::page_down));
    OA_CHECK(state.scroll_points == layout.scroll_max_points);
    static_cast<void>(chooser::key(layout, state, chooser::Key::page_up));
    OA_CHECK(state.scroll_points < layout.scroll_max_points);
    // Nothing to focus: keys do nothing.
    chooser::UiState none;
    OA_CHECK(chooser::key({}, none, chooser::Key::enter).command == chooser::Command::none);
    OA_CHECK(!none.focus_shown);
}

/// Checks presses: down marks the target, up on it gives its command, up elsewhere none, a
/// disabled target takes nothing and a press near a target reaches it.
void check_presses() {
    const chooser::Viewport viewport = deck_viewport();
    chooser::UiState state;
    chooser::Layout layout = chooser::lay_out(list_model(), state, viewport, {});
    const chooser::HitBox* browse = find(layout, {TargetKind::browse, 0});
    OA_CHECK(browse != nullptr);
    if (browse == nullptr)
        return;
    const chooser::Rect browse_box = browse->box;
    chooser::press_down(layout, state, centre(browse_box));
    OA_CHECK(state.pressed >= 0);
    layout = chooser::lay_out(list_model(), state, viewport, {});
    const bool held = std::any_of(
        layout.paint.list.items.begin(),
        layout.paint.list.items.end(),
        [](const oa::ui::kit::Item& item) { return item.state.pressed; }
    );
    OA_CHECK(held);
    const chooser::Outcome opened = chooser::press_up(layout, state, centre(browse_box));
    OA_CHECK(opened.command == chooser::Command::open_browser);
    OA_CHECK(state.pressed == -1);
    OA_CHECK(
        layout.focus_order[static_cast<std::size_t>(state.focus)] == (Target{TargetKind::browse, 0})
    );
    // Slid off before coming up: nothing.
    chooser::press_down(layout, state, centre(browse_box));
    OA_CHECK(
        chooser::press_up(layout, state, {browse_box.x + browse_box.width / 2, 2}).command ==
        chooser::Command::none
    );
    // A disabled row takes nothing, not even the nearest target.
    const chooser::HitBox* unusable = find(layout, {TargetKind::install, 1});
    OA_CHECK(unusable != nullptr);
    if (unusable != nullptr) {
        chooser::press_down(layout, state, centre(unusable->box));
        OA_CHECK(state.pressed == -1);
        OA_CHECK(
            chooser::press_up(layout, state, centre(unusable->box)).command ==
            chooser::Command::none
        );
    }
    // A press just outside the Quit button reaches it.
    const chooser::HitBox* quit = find(layout, {TargetKind::quit, 0});
    OA_CHECK(quit != nullptr);
    if (quit != nullptr) {
        const chooser::Point near{
            quit->box.x + quit->box.width / 2, quit->box.y + quit->box.height + 10
        };
        chooser::press_down(layout, state, near);
        OA_CHECK(chooser::press_up(layout, state, near).command == chooser::Command::quit);
        OA_CHECK(chooser::hit_test(layout, near, 0).kind == TargetKind::none);
    }
    // The usable folder's row plays it.
    const chooser::HitBox* install = find(layout, {TargetKind::install, 0});
    OA_CHECK(install != nullptr);
    if (install != nullptr) {
        chooser::press_down(layout, state, centre(install->box));
        const chooser::Outcome played = chooser::press_up(layout, state, centre(install->box));
        OA_CHECK(played.command == chooser::Command::play_install && played.index == 0);
    }
    // A pointer press hides the focus the keys showed.
    state.focus_shown = true;
    chooser::press_down(layout, state, {1, 1});
    OA_CHECK(!state.focus_shown && state.pressed == -1);
}

/// Checks scrolling stays within the layout's range.
void check_scroll() {
    const chooser::Viewport viewport = phone_viewport();
    chooser::UiState state;
    const chooser::Layout layout = chooser::lay_out(browse_model(false), state, viewport, {});
    OA_CHECK(layout.scroll_max_points > 0);
    chooser::scroll(layout, state, 1000000);
    OA_CHECK(state.scroll_points == layout.scroll_max_points);
    chooser::scroll(layout, state, -1000000);
    OA_CHECK(state.scroll_points == 0);
    chooser::scroll(layout, state, 25);
    OA_CHECK(state.scroll_points == std::min(25, layout.scroll_max_points));
    // Scrolled to the end, the last folder shows whole in the rows.
    state.scroll_points = layout.scroll_max_points;
    const chooser::Layout end = chooser::lay_out(browse_model(false), state, viewport, {});
    const chooser::HitBox* last = find(end, {TargetKind::entry, 29});
    OA_CHECK(last != nullptr && inside(last->box, last->clip));
    // A layout that does not scroll keeps the scroll at 0.
    const chooser::Layout still = chooser::lay_out(list_model(), {}, deck_viewport(), {});
    OA_CHECK(still.scroll_max_points == 0);
    chooser::UiState held;
    chooser::scroll(still, held, 50);
    OA_CHECK(held.scroll_points == 0);
}

/// Checks that an empty layout reaches nothing.
void check_empty_layout() {
    const chooser::Layout layout{};
    OA_CHECK(chooser::hit_test(layout, {}, 0).kind == TargetKind::none);
    chooser::Viewport nothing;
    OA_CHECK(chooser::lay_out(list_model(), {}, nothing, {}).targets.empty());
}

/// One case of the recorded layouts: a model and what the chooser keeps between frames.
struct Case {
    std::string name{};       ///< what it shows
    chooser::Model model{};   ///< the model
    chooser::UiState state{}; ///< the focus, the press and the scroll
};

/// A size the cases are laid out at.
struct Form {
    std::string name{};           ///< "deck 1280x800@1"
    chooser::Viewport viewport{}; ///< the canvas
};

/// Returns the sizes every case is laid out at: the Steam Deck's screen and a phone held
/// sideways with its safe area.
///
/// @return the forms
std::vector<Form> forms() {
    return {{"deck 1280x800@1", deck_viewport()}, {"phone 852x393@3", phone_viewport()}};
}

/// Returns every model the checks build, with the states they lay them out in, and the views
/// they leave out: one folder found, a long notice, long paths that break at their
/// separators, and the rows scrolled, focused and pressed.
///
/// @return the cases
std::vector<Case> every_case() {
    std::vector<Case> cases;
    const auto add = [&cases](std::string name, chooser::Model model, chooser::UiState state) {
        cases.push_back({std::move(name), std::move(model), state});
    };
    const chooser::UiState rest;
    add("list two found", list_model(), rest);
    chooser::Model game_mode = list_model();
    game_mode.offers_dialog = false;
    add("list game mode", game_mode, rest);
    chooser::Model one = list_model();
    one.installs.pop_back();
    add("list one found", one, rest);
    add("list nothing found", chooser::Model{}, rest);
    chooser::Model nothing;
    nothing.notice = "The Total Annihilation folder chosen earlier can no longer be used.";
    add("list nothing found with a notice", nothing, rest);
    chooser::Model noticed = list_model();
    noticed.notice =
        "The Total Annihilation folder chosen earlier can no longer be used:\n"
        "/run/media/deck/SD/SteamLibrary/steamapps/common/Total-Annihilation-Commander-Pack\n"
        "Its archives lack guis/mainmenu.gui.\n\nThe system's folder dialog did not open: "
        "no portal answered.";
    add("list long notice", noticed, rest);
    chooser::UiState end;
    end.scroll_points = 100000;
    add("list long notice scrolled to the end", noticed, end);
    chooser::Model paths = list_model();
    paths.installs[0].folder =
        "/home/deck/.local/share/Steam/steamapps/compatdata/2000/pfx/drive_c/GOG_Games/"
        "Total_Annihilation_Commander_Pack/Total_Annihilation";
    paths.installs[1].folder =
        "C:\\Program_Files_(x86)\\GOG_Galaxy\\Games\\Total_Annihilation_Commander_Pack\\"
        "Total_Annihilation_Kingdoms_Not_This_One";
    add("list long paths", paths, rest);
    chooser::UiState focused;
    focused.focus = 1;
    focused.focus_shown = true;
    add("list focus on browse", list_model(), focused);
    chooser::UiState pressed;
    pressed.pressed = 2;
    add("list browse pressed", list_model(), pressed);
    chooser::UiState pressed_off;
    pressed_off.pressed = 1;
    add("list unusable row pressed", list_model(), pressed_off);
    add("browse refused", browse_model(false), rest);
    add("browse usable", browse_model(true), rest);
    chooser::Model root = browse_model(false);
    root.folder = "/";
    root.has_parent = false;
    add("browse top of a drive", root, rest);
    chooser::Model empty = browse_model(false);
    empty.entries.clear();
    add("browse empty folder", empty, rest);
    chooser::UiState scrolled;
    scrolled.scroll_points = 120;
    add("browse scrolled", browse_model(false), scrolled);
    add("browse scrolled to the end", browse_model(false), end);
    chooser::UiState on_folder;
    on_folder.focus = 9;
    on_folder.focus_shown = true;
    on_folder.scroll_points = 60;
    add("browse focus on a folder", browse_model(true), on_folder);
    chooser::UiState held;
    held.pressed = 1;
    add("browse place pressed", browse_model(true), held);
    chooser::Model hinted = browse_model(true);
    hinted.folder = "/run/media/deck/SD/Downloads/Total_Annihilation_Demo_1997/"
                    "an_installer_folder_with_a_very_long_name_indeed";
    hinted.hint = "Open the folder that holds the installer of the Total Annihilation demo "
                  "(1997), then Play this folder.";
    hinted.notice = "This folder cannot be played:\n/home/deck/Games\nIt holds no archives.";
    hinted.place_labels = {"HOME", "DOWNLOADS", "SD CARD", "USB STICK", "SECOND DRIVE"};
    hinted.entries[2].name = "Spiele \xE2\x80\x93 Sammlung mit einem sehr langen Namen, der nicht "
                             "in eine Zeile passt";
    add("browse long path with a hint and a notice", hinted, rest);
    return cases;
}

/// Returns the name of a part's role.
///
/// @param role the role
/// @return its enumerator's name
std::string_view role_name(oa::ui::kit::Role role) {
    using oa::ui::kit::Role;
    switch (role) {
    case Role::text:
        return "text";
    case Role::button:
        return "button";
    case Role::toggle:
        return "toggle";
    case Role::header_bar:
        return "header_bar";
    case Role::badge:
        return "badge";
    case Role::header_text:
        return "header_text";
    case Role::version:
        return "version";
    case Role::title:
        return "title";
    case Role::panel:
        return "panel";
    case Role::icon:
        return "icon";
    case Role::banner:
        return "banner";
    case Role::row:
        return "row";
    case Role::divider:
        return "divider";
    case Role::button_main:
        return "button_main";
    case Role::button_danger:
        return "button_danger";
    case Role::progress_track:
        return "progress_track";
    case Role::progress_fill:
        return "progress_fill";
    case Role::progress_busy:
        return "progress_busy";
    case Role::backdrop:
        return "backdrop";
    case Role::sheet:
        return "sheet";
    default:
        return "?";
    }
}

/// Returns the name of a text's look.
///
/// @param style the text's look
/// @return its enumerator's name
std::string_view style_name(oa::ui::kit::TextStyle style) {
    using oa::ui::kit::TextStyle;
    switch (style) {
    case TextStyle::header:
        return "header";
    case TextStyle::title:
        return "title";
    case TextStyle::subtitle:
        return "subtitle";
    case TextStyle::lead:
        return "lead";
    case TextStyle::body:
        return "body";
    case TextStyle::small:
        return "small";
    case TextStyle::row_title:
        return "row_title";
    case TextStyle::row_detail:
        return "row_detail";
    case TextStyle::button:
        return "button";
    case TextStyle::mark_text:
        return "mark_text";
    }
    return "?";
}

/// Returns the name of a mark.
///
/// @param glyph the mark
/// @return its enumerator's name
std::string_view glyph_name(oa::ui::kit::Glyph glyph) {
    using oa::ui::kit::Glyph;
    switch (glyph) {
    case Glyph::none:
        return "none";
    case Glyph::folder:
        return "folder";
    case Glyph::device:
        return "device";
    case Glyph::disk:
        return "disk";
    case Glyph::clock:
        return "clock";
    case Glyph::warning:
        return "warning";
    case Glyph::check:
        return "check";
    case Glyph::cross:
        return "cross";
    case Glyph::dash:
        return "dash";
    case Glyph::info:
        return "info";
    case Glyph::stop:
        return "stop";
    case Glyph::play:
        return "play";
    case Glyph::trash:
        return "trash";
    case Glyph::refresh:
        return "refresh";
    case Glyph::plus:
        return "plus";
    case Glyph::file:
        return "file";
    case Glyph::oa:
        return "oa";
    }
    return "?";
}

/// Returns the name of a kind of target.
///
/// @param kind the kind
/// @return its enumerator's name
std::string_view target_kind_name(TargetKind kind) {
    switch (kind) {
    case TargetKind::none:
        return "none";
    case TargetKind::install:
        return "install";
    case TargetKind::browse:
        return "browse";
    case TargetKind::dialog:
        return "dialog";
    case TargetKind::demo:
        return "demo";
    case TargetKind::quit:
        return "quit";
    case TargetKind::place:
        return "place";
    case TargetKind::parent:
        return "parent";
    case TargetKind::entry:
        return "entry";
    case TargetKind::choose:
        return "choose";
    case TargetKind::back:
        return "back";
    }
    return "?";
}

/// Returns a colour as the name of the screen's colour token whose value it is, or as
/// #rrggbbaa when it is none of them, so that a token whose value changes keeps its name.
///
/// @param colour the colour
/// @return the token's name or the hexadecimal value
std::string colour_name(oa::ui::kit::Colour colour) {
    namespace token = oa::ui::kit::screen_colour;
    const std::array<std::pair<std::string_view, oa::ui::kit::Colour>, 10> tokens{{
        {"background", token::background},
        {"panel", token::panel},
        {"line", token::line},
        {"text", token::text},
        {"dim", token::dim},
        {"green", token::green},
        {"amber", token::amber},
        {"red", token::red},
        {"ink", token::ink},
        {"button_text", token::button_text},
    }};
    for (const auto& [name, value] : tokens)
        if (value == colour)
            return std::string(name);
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex = "#";
    for (const uint8_t channel : {colour.r, colour.g, colour.b, colour.a}) {
        hex += digits[channel >> 4];
        hex += digits[channel & 0xf];
    }
    return hex;
}

/// Writes a rectangle as x,y,width,height.
///
/// @param rect the rectangle
/// @return the text
std::string rect_text(const chooser::Rect& rect) {
    return std::to_string(rect.x) + "," + std::to_string(rect.y) + "," +
           std::to_string(rect.width) + "," + std::to_string(rect.height);
}

/// Writes a target as its kind's name and its index.
///
/// @param target the target
/// @return kind/index
std::string target_text(const Target& target) {
    return std::string(target_kind_name(target.kind)) + "/" + std::to_string(target.index);
}

/// Writes a number of single precision exactly, as the hexadecimal digits of its bits.
///
/// @param value the number
/// @return eight hexadecimal digits
std::string float_text(float value) {
    constexpr std::string_view digits = "0123456789abcdef";
    const auto bits = std::bit_cast<uint32_t>(value);
    std::string written;
    for (int shift = 28; shift >= 0; shift -= 4)
        written += digits[(bits >> shift) & 0xfu];
    return written;
}

/// Writes a line of text in double quotes, a quote or a backslash in it after a backslash.
///
/// @param line the line
/// @return the quoted line
std::string quoted_line(std::string_view line) {
    std::string quoted = "\"";
    for (const char character : line) {
        if (character == '"' || character == '\\')
            quoted += '\\';
        quoted += character;
    }
    return quoted + "\"";
}

/// Writes a layout in a fixed text form: one line of the layout's own fields (the form, the
/// scrolling region, how far it scrolls, the pixels a point and the focus order), one line
/// per painted part in painting order with every field the chooser sets in a fixed order,
/// then one line per target in the order they were laid out. Roles, text looks, marks and
/// kinds of target are written as their enumerators' names, and colours as the names of the
/// screen's colour tokens.
///
/// @param layout the layout
/// @return the text
std::string format_layout(const chooser::Layout& layout) {
    std::string written = "layout device ";
    written += layout.paint.device == gf::DeviceClass::phone ? "phone" : "tablet";
    written += " rows " + rect_text(layout.paint.rows);
    written += " scroll " + std::to_string(layout.scroll_max_points);
    written += " painted_scroll " + std::to_string(layout.paint.scroll_max_points);
    written += " px " + float_text(layout.paint.px_per_point);
    written += " focus";
    for (const Target& target : layout.focus_order)
        written += " " + target_text(target);
    written += "\n";
    for (const oa::ui::kit::Item& item : layout.paint.list.items) {
        written += role_name(item.role);
        written += " box " + rect_text(item.rect);
        written += " lines " + std::to_string(item.lines.size());
        for (const std::string& line : item.lines)
            written += " " + quoted_line(line);
        written += " text ";
        written += style_name(item.style);
        written += " size " + std::to_string(item.pixel_size);
        written += " bold " + std::to_string(item.bold ? 1 : 0);
        written += " colour " + colour_name(item.colour);
        written += " glyph ";
        written += glyph_name(item.glyph);
        written += " glyph_size " + std::to_string(item.glyph_size);
        written += " enabled " + std::to_string(item.state.disabled ? 0 : 1);
        written += " on " + std::to_string(item.state.on ? 1 : 0);
        written += " focused " + std::to_string(item.state.focused ? 1 : 0);
        written += " pressed " + std::to_string(item.state.pressed ? 1 : 0);
        written += " fraction " + float_text(item.fraction);
        written += " clip " + rect_text(item.clip);
        written += "\n";
    }
    for (const chooser::HitBox& hit : layout.targets) {
        written += "target " + target_text(hit.target);
        written += " box " + rect_text(hit.box);
        written += " enabled " + std::to_string(hit.enabled ? 1 : 0);
        written += " clip " + rect_text(hit.clip);
        written += "\n";
    }
    return written;
}

/// Returns the SHA-256 of a layout's text form, in hexadecimal.
///
/// @param layout the layout
/// @return 64 lower-case hexadecimal digits
std::string layout_digest(const chooser::Layout& layout) {
    namespace sha256 = oa::base::sha256;
    const std::string written = format_layout(layout);
    const auto bytes = std::as_bytes(std::span<const char>(written.data(), written.size()));
    const auto hex = sha256::to_hex(
        sha256::digest_of(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size())
        )
    );
    return std::string(hex.begin(), hex.end());
}

/// Lays a case out at a form.
///
/// @param test the case
/// @param form the form
/// @return the layout
chooser::Layout lay_out_case(const Case& test, const Form& form) {
    return chooser::lay_out(test.model, test.state, form.viewport, {});
}

/// Returns the digests of every case at every form, one line each: <case>\t<form>\t<hex>, the
/// content of layout-digests.txt.
///
/// @return the lines
std::string digest_lines() {
    std::string lines;
    for (const Case& test : every_case())
        for (const Form& form : forms())
            lines += test.name + "\t" + form.name + "\t" + layout_digest(lay_out_case(test, form)) +
                     "\n";
    return lines;
}

/// Checks that every layout of every case at every form is the one recorded: its digest is
/// the one layout-digests.txt holds for that case and form, and the file holds no other line.
///
/// @param file layout-digests.txt; null when the test was given none
void check_digests(const char* file) {
    OA_CHECK(file != nullptr);
    if (file == nullptr) {
        std::fprintf(stderr, "the test is given no layout-digests.txt\n");
        return;
    }
    std::ifstream input(file, std::ios::binary);
    OA_CHECK(static_cast<bool>(input));
    if (!input) {
        std::fprintf(stderr, "layout-digests.txt does not open: %s\n", file);
        return;
    }
    std::map<std::string, std::string> recorded;
    std::size_t lines = 0;
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        ++lines;
        const std::size_t tab = line.rfind('\t');
        OA_CHECK(tab != std::string::npos);
        if (tab == std::string::npos)
            continue;
        recorded[line.substr(0, tab)] = line.substr(tab + 1);
    }
    std::size_t compared = 0;
    for (const Case& test : every_case()) {
        for (const Form& form : forms()) {
            const auto found = recorded.find(test.name + "\t" + form.name);
            OA_CHECK(found != recorded.end());
            if (found == recorded.end()) {
                std::fprintf(stderr, "%s at %s: no digest\n", test.name.c_str(), form.name.c_str());
                continue;
            }
            ++compared;
            const bool same = layout_digest(lay_out_case(test, form)) == found->second;
            OA_CHECK(same);
            if (!same)
                std::fprintf(
                    stderr,
                    "%s at %s: the layout differs from the one recorded\n",
                    test.name.c_str(),
                    form.name.c_str()
                );
        }
    }
    OA_CHECK(lines == compared && recorded.size() == compared);
}

} // namespace

/// Runs the checks, or with --write-digests prints the digests of every case's layouts.
///
/// @param argc the arguments' count
/// @param argv the arguments: layout-digests.txt, or --write-digests
/// @return 0 when every check passed
int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--dump") {
        // The text form of the cases whose names hold a text, for looking at a layout.
        for (const Case& test : every_case())
            if (test.name.find(argv[2]) != std::string::npos)
                for (const Form& form : forms())
                    std::printf(
                        "== %s at %s\n%s",
                        test.name.c_str(),
                        form.name.c_str(),
                        format_layout(lay_out_case(test, form)).c_str()
                    );
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--write-digests") {
        std::fputs(digest_lines().c_str(), stdout);
        return 0;
    }
    check_digests(argc == 2 ? argv[1] : nullptr);
    check_empty_layout();
    check_list();
    check_browser();
    check_keys();
    check_presses();
    check_scroll();
    return oa::test::check_exit_status();
}
