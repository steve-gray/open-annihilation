// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-map-browser: the map browser over the skirmish setup's map picker.
// The check writes the map pack check's two packs into the player's own
// Maps folder, with every built-in registry turned off so that nothing is
// fetched, and at 640x480, 1280x720, 1920x1080 and 2560x1440 opens SELMAP and
// presses MORE MAPS…. It requires the size class and scale the window gives
// and the grid's columns there; MORE MAPS… over none of SELMAP's gadgets;
// every card and control inside the browser's window; Installed packs showing
// the two check maps alone; the search "isle" leaving one; the clashing
// map's card greyed with "New names" in the footer's reason and the primary
// button disabled; and Select on the fitting map closing the browser with
// SELMAP's selection on it. Each size's frame is written as
// map-browser-<w>x<h>.png in the working folder, for review.

#include "map_pack_check_packs.hpp"
#include "map_packs.hpp"
#include "oa_layer.hpp"
#include "oa_layer_check.hpp"

#include "oa/app/content/service.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/package_install/oamap.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/player_registries.hpp"
#include "oa/data/registry/trust.hpp"
#include "oa/formats/png.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/map_browser/screen.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oa::app {

namespace {

namespace kit = oa::ui::kit;
namespace mb = oa::ui::map_browser;
namespace layout = oa::ui::display_layout;
namespace registry = oa::data::registry;

/// One window the check opens the browser in, and what the OA layer gives
/// there: the design's table of windows.
struct CheckWindow {
    int32_t width{};             ///< the window's width, in pixels
    int32_t height{};            ///< its height
    kit::SizeClass size_class{}; ///< the size class
    int32_t scale{};             ///< window pixels a point
    std::size_t columns{};       ///< the grid's columns
};

/// The windows, in the order the check opens them.
constexpr std::array<CheckWindow, 4> kWindows{{
    {640, 480, kit::SizeClass::compact, 1, 3},
    {1280, 720, kit::SizeClass::large, 1, 6},
    {1920, 1080, kit::SizeClass::regular, 2, 4},
    {2560, 1440, kit::SizeClass::large, 2, 6},
}};

/// The frames a step lets pass, enough for the pictures in view to be decoded.
constexpr int32_t kSettleFrames = 16;

/// The words the clashing map's reason holds.
constexpr std::string_view kNewNames = "New names";

/// The search the check types.
constexpr std::string_view kSearch = "isle";

/// Stops the check.
///
/// @param ok the step held
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("map browser check: " + std::string(what));
}

/// Writes a file whole.
///
/// @param file the file
/// @param bytes its bytes
void write_text(const fs::path& file, std::string_view bytes) {
    if (!file.parent_path().empty())
        fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(out), "could not write " + path_to_utf8(file));
}

/// Writes a frame as a PNG file.
///
/// @param file the file
/// @param frame the frame
void write_png(const fs::path& file, const renderer::Surface& frame) {
    std::vector<uint8_t> bytes;
    const oa::formats::png::Header header{
        frame.width, frame.height, 8, oa::formats::png::ColorType::rgb, {}
    };
    require(
        oa::formats::png::write(oa::formats::png::Image{header, {}, frame.rgb}, &bytes),
        "could not encode " + path_to_utf8(file)
    );
    write_text(file, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

/// Maps a rectangle of the 640 by 480 frame to window pixels through the
/// picture's place in the window, the edges outward.
///
/// @param seen what the layer's screens are placed on
/// @param frame_rect the rectangle, in the frame's pixels
/// @return the rectangle, in window pixels
kit::Rect frame_to_window(const LayerView& seen, const kit::Rect& frame_rect) {
    const auto& picture = seen.picture;
    const auto floor_at = [](int64_t numerator, int64_t denominator) {
        return static_cast<int32_t>(numerator / denominator);
    };
    const auto ceil_at = [](int64_t numerator, int64_t denominator) {
        return static_cast<int32_t>((numerator + denominator - 1) / denominator);
    };
    const int64_t across = std::max(seen.picture_size.x, int32_t{1});
    const int64_t down = std::max(seen.picture_size.y, int32_t{1});
    const int32_t left =
        picture.x + floor_at(static_cast<int64_t>(frame_rect.x) * picture.width, across);
    const int32_t top =
        picture.y + floor_at(static_cast<int64_t>(frame_rect.y) * picture.height, down);
    const int32_t right =
        picture.x +
        ceil_at(static_cast<int64_t>(frame_rect.x + frame_rect.width) * picture.width, across);
    const int32_t bottom =
        picture.y +
        ceil_at(static_cast<int64_t>(frame_rect.y + frame_rect.height) * picture.height, down);
    return {left, top, right - left, bottom - top};
}

/// Returns what of a rectangle a clip leaves; the rectangle when no clip.
///
/// @param rect the rectangle
/// @param clip the clip; empty for none
/// @return the part that can show
kit::Rect visible_part(const kit::Rect& rect, const kit::Rect& clip) {
    if (clip.width <= 0 || clip.height <= 0)
        return rect;
    return kit::intersect(rect, clip);
}

/// Returns the game names of the cards a display list shows controls for,
/// in grid order.
///
/// @param list the display list
/// @return the names
std::vector<std::string> card_names(const kit::DisplayList& list) {
    std::vector<std::string> names;
    for (const kit::Control& control : list.controls)
        if (control.name.starts_with(mb::name_card_prefix))
            names.push_back(control.text);
    return names;
}

} // namespace

void Runtime::prepare_map_browser_check() {
    // Written before the content service reads the player's folder: every
    // built-in registry beside the program is off, so the check fetches no
    // catalogue and offers no map online.
    registry::PlayerRegistries list;
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        const fs::path folder =
            path_from_utf8(base) / path_from_utf8(content::builtin_registries_folder_name);
        std::error_code error;
        if (fs::is_directory(folder, error))
            for (const registry::Descriptor& built_in :
                 registry::read_builtin_registries(folder, nullptr))
                list.disabled.push_back(built_in.id);
    }
    write_text(
        user_folder() / std::string(registry::player_registries_file),
        registry::player_registries_text(list)
    );
    std::cout << "map browser check: " << list.disabled.size()
              << " built-in registries turned off\n";
}

void Runtime::check_map_browser() {
    require(sdl_.window != nullptr && sdl_.renderer != nullptr, "needs the SDL presenter");
    require(
        options_.user_folder && !options_.user_folder->empty() && !user_folder_.empty(),
        "the run has no folder of the player's own (--user-folder)"
    );
    // The map pack check's two packs, in the player's own Maps folder.
    const fs::path maps = user_folder_ / std::string(package_install::oamap::maps_folder_name);
    const auto models = assets_.list_effective("objects3d", ".3do", false);
    require(!models.empty(), "the game has no model for the check maps' feature");
    write_map_pack_check_packs(maps, path_to_utf8(path_from_utf8(models.front()).stem()));
    map_packs().refresh();
    const std::string isle(map_pack_check_fitting_map);
    const std::string clash(map_pack_check_clashing_map);
    require(
        map_packs().find(isle) != nullptr && map_packs().find(clash) != nullptr,
        "the check packs are not installed"
    );
    eligible_map_names_.clear();
    first_map_name_.clear();
    discover_first_map();
    if (const std::shared_ptr<const content::Snapshot> snapshot = content_service().snapshot())
        require(snapshot->entries.empty(), "a catalogue offers packages with every registry off");

    bool running = true;
    const auto frames = [&](int32_t count) {
        for (int32_t frame = 0; frame < count; ++frame) {
            run_frame(running);
            require(running, "a frame ended the run");
        }
    };
    const auto named = [this](std::string_view name) -> std::optional<AutomationControl> {
        std::vector<AutomationControl> controls;
        oa_layer().automation_controls(controls);
        for (const AutomationControl& control : controls)
            if (control.name == name && control.visible && control.window_width > 0 &&
                control.window_height > 0)
                return control;
        return std::nullopt;
    };
    const auto click_named = [&](std::string_view name) {
        const std::optional<AutomationControl> control = named(name);
        require(control.has_value(), std::string(name) + " is not shown");
        const layout::Point pixel{
            control->window_x + control->window_width / 2,
            control->window_y + control->window_height / 2
        };
        for (const SDL_EventType type :
             {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event event = window_pointer_event(
                sdl_.window,
                sdl_.renderer,
                type,
                pixel,
                type == SDL_EVENT_MOUSE_MOTION ? 0 : SDL_BUTTON_LEFT
            );
            dispatch_event(event, running);
            require(running, "a click ended the run");
        }
        frames(1);
    };
    const auto browser = [this]() -> LayerScreen* {
        LayerScreen* top = oa_layer().top();
        return top != nullptr && top->name() == mb::screen_name ? top : nullptr;
    };
    const auto shown_cards = [&]() {
        LayerScreen* screen = browser();
        require(screen != nullptr, "the map browser is not on top");
        const kit::DisplayList list = screen->display_list();
        return card_names(list);
    };

    for (const CheckWindow& window : kWindows) {
        const std::string size = std::to_string(window.width) + 'x' + std::to_string(window.height);
        const std::string on = " on the " + size + " window";
        require(
            SDL_SetWindowSize(sdl_.window, window.width, window.height) &&
                SDL_SyncWindow(sdl_.window),
            "the window did not take " + size
        );
        apply_output_mode();
        load(Screen::main_menu);
        require(screen_ == Screen::main_menu, "the main menu did not open" + on);
        frames(2);
        exercise_click(menu::resource_name(menu::Button::single_player));
        exercise_click(entry::resource_name(entry::Button::skirmish));
        require(screen_ == Screen::skirmish, "Skirmish did not open its setup" + on);
        exercise_click(skirmish::resource_name(skirmish::Button::select_map));
        require(screen_ == Screen::map_selection, "the map picker did not open" + on);
        frames(2);

        // MORE MAPS… shows under the map list, over none of SELMAP's gadgets.
        OaLayer& layer = oa_layer();
        require(
            layer.screen_class() == window.size_class && layer.screen_scale() == window.scale,
            "the layer is not " + std::string(size_class_name(window.size_class)) + " at " +
                std::to_string(window.scale) + "x" + on
        );
        require(named("oa.more-maps").has_value(), "MORE MAPS… is not listed as oa.more-maps" + on);
        const LayerPlacement button = layer.placement_of("more-maps");
        const kit::Rect button_rect{
            button.shown.x, button.shown.y, button.shown.width, button.shown.height
        };
        require(button_rect.width > 0 && button_rect.height > 0, "MORE MAPS… does not show" + on);
        const LayerView seen = layer.view();
        const auto& gadgets = resources_.layout.gadgets;
        require(!gadgets.empty(), "SELMAP has no gadgets" + on);
        const auto& root = gadgets.front().common;
        const auto origin = panel_origin();
        const int32_t left = origin.x + (kCanvasWidth - static_cast<int32_t>(root.width)) / 2;
        const int32_t top = origin.y + (kCanvasHeight - static_cast<int32_t>(root.height)) / 2;
        for (std::size_t index = 1; index < gadgets.size(); ++index) {
            const auto& common = gadgets[index].common;
            const kit::Rect gadget = frame_to_window(
                seen,
                {left + common.x,
                 top + common.y,
                 static_cast<int32_t>(common.width),
                 static_cast<int32_t>(common.height)}
            );
            const kit::Rect shared = kit::intersect(button_rect, gadget);
            require(
                shared.width <= 0 || shared.height <= 0,
                "MORE MAPS… overlaps SELMAP's " + std::string(common.name) + on
            );
        }

        // The browser: its class's window, every card and control inside it,
        // and its columns.
        click_named("oa.more-maps");
        frames(kSettleFrames);
        LayerScreen* shown = browser();
        require(shown != nullptr, "MORE MAPS… did not open the map browser" + on);
        const LayerPlacement placed = layer.placement_of(mb::screen_name);
        const kit::Point expected = mb::window_size(seen.frame);
        require(
            placed.points_width == expected.x && placed.points_height == expected.y &&
                placed.shown.width == expected.x * window.scale &&
                placed.shown.height == expected.y * window.scale,
            "the map browser is not its class's window" + on
        );
        {
            const kit::DisplayList list = shown->display_list();
            require(kit::name_problem(list).empty(), kit::name_problem(list) + on);
            const kit::Rect whole{0, 0, expected.x, expected.y};
            for (const kit::Control& control : list.controls) {
                const kit::Rect reach = visible_part(control.rect, control.clip);
                if (reach.width > 0 && reach.height > 0)
                    require(
                        kit::wholly_in(reach, whole), control.name + " lies outside the window" + on
                    );
            }
            for (const kit::Item& item : list.items) {
                const kit::Rect drawn = visible_part(item.rect, item.clip);
                if (drawn.width > 0 && drawn.height > 0)
                    require(kit::wholly_in(drawn, whole), "a part lies outside the window" + on);
            }
            std::vector<int32_t> first_row;
            for (const kit::Control& control : list.controls)
                if (control.name.starts_with(mb::name_card_prefix) &&
                    (first_row.empty() || control.rect.y == first_row.front()))
                    first_row.push_back(control.rect.y);
            require(
                first_row.size() == window.columns,
                "the grid has " + std::to_string(first_row.size()) + " columns, not " +
                    std::to_string(window.columns) + on
            );
            for (const std::string_view listed :
                 {mb::name_filter_all,
                  mb::name_filter_base,
                  mb::name_filter_installed,
                  mb::name_filter_online,
                  mb::name_players_2,
                  mb::name_players_3,
                  mb::name_players_4,
                  mb::name_players_many,
                  mb::name_size_small,
                  mb::name_size_medium,
                  mb::name_size_large,
                  mb::name_search,
                  mb::name_cancel,
                  mb::name_select})
                require(
                    named("oa." + std::string(listed)).has_value(),
                    "oa." + std::string(listed) + " is not listed" + on
                );
        }

        // Installed packs: the two check maps, and nothing else. Online: none.
        click_named("oa.maps-filter-online");
        require(shown_cards().empty(), "a map is offered online with every registry off" + on);
        click_named("oa.maps-filter-installed");
        {
            std::vector<std::string> names = shown_cards();
            std::sort(names.begin(), names.end());
            require(
                names == std::vector<std::string>{clash, isle},
                "Installed packs does not show the two check maps alone" + on
            );
        }

        // The clashing map: greyed, its reason in the footer, Select disabled.
        click_named("oa.maps-card-" + mb::card_word(clash));
        frames(kSettleFrames);
        {
            const kit::DisplayList list = shown->display_list();
            bool greyed = false;
            bool reason = false;
            for (const kit::Item& item : list.items) {
                if (const auto* card = std::get_if<kit::CardLook>(&item.look);
                    card != nullptr && card->selected)
                    greyed = card->greyed;
                if (item.role == kit::Role::text && item.text.find(kNewNames) != std::string::npos)
                    reason = true;
            }
            require(greyed, "the clashing map's card is not greyed" + on);
            require(reason, "the footer does not give the clashing map's reason" + on);
            const std::optional<AutomationControl> select_button = named("oa.maps-select");
            require(
                select_button && !select_button->enabled,
                "Select is not disabled for the clashing map" + on
            );
        }

        // The frame for review: every map, the clashing map selected.
        click_named("oa.maps-filter-all");
        frames(kSettleFrames);
        oa_layer().read_whole_window(true);
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        oa_layer().read_whole_window(false);
        require(
            presented.width == static_cast<uint32_t>(window.width) &&
                presented.height == static_cast<uint32_t>(window.height),
            "the frame is not the window's size" + on
        );
        write_png(fs::path("map-browser-" + size + ".png"), presented);

        // The search narrows the installed maps to one.
        click_named("oa.maps-filter-installed");
        click_named("oa.maps-search");
        {
            const LayerAnswer answer = shown->text(kSearch);
            oa_layer().take_answer(*shown, answer, 0);
            frames(1);
            require(
                shown_cards() == std::vector<std::string>{isle},
                "the search \"" + std::string(kSearch) + "\" does not leave one map" + on
            );
        }

        // Select on the fitting map closes the browser with SELMAP's selection on it.
        click_named("oa.maps-card-" + mb::card_word(isle));
        click_named("oa.maps-select");
        frames(2);
        require(browser() == nullptr, "Select did not close the map browser" + on);
        require(
            modal_map_index_ >= 0 &&
                static_cast<std::size_t>(modal_map_index_) < bound_map_names_.size() &&
                bound_map_names_[static_cast<std::size_t>(modal_map_index_)] == isle,
            "SELMAP's selection is not " + isle + on
        );
        std::cout << "map browser check: " << size_class_name(window.size_class) << " at "
                  << window.scale << "x, " << window.columns << " columns" << on << '\n';
        close_map_modal();
        frames(2);
        require(
            oa_layer().find("more-maps") == nullptr, "MORE MAPS… stayed after SELMAP closed" + on
        );
    }
    load(Screen::main_menu);
    std::cout << "map browser check passed\n";
}

} // namespace oa::app
