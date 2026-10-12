// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The main menu's two buttons, an overlay on the main menu under the
// extensions' overlays: the OA button, which opens the Library over the
// darkened main menu (runtime_library.cpp) and wears a dot while updates
// wait there, and beside it the Settings button, which opens Settings with
// no Library under it; and the settings dialog as a screen of the OA layer
// (oa_layer.hpp), which the Settings button, the shortcut, the requests and
// the Library's SETTINGS… open on whichever screen can show them.

#include "engine_settings_menu_host.hpp"
#include "engine_settings_state.hpp"
#include "library_host.hpp"
#include "oa_layer.hpp"

#include "oa/app/runtime.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/kit/components.hpp"
#include "oa/ui/kit/looks.hpp"
#include "oa/ui/library/library.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace artless = oa::ui::frontend_renderer;
namespace kit = oa::ui::kit;

namespace {

/// The Settings button's gap to the picture's right edge, and the buttons'
/// gap to its bottom edge (top edge while an extension's overlay stands
/// over the main menu), in source pixels.
constexpr int32_t kButtonInset = 12;
/// The gap between the OA button and the Settings button: a quarter of a
/// button's side, in source pixels.
constexpr int32_t kButtonGap = settings::menu_button_side / 4;
/// The button overlay's z: under the extensions' overlays, which draw over
/// the button and see input before it.
constexpr int16_t kButtonOverlayZ = -100;
/// The front end's clock, in units a second (Runtime::match_clock_scale).
constexpr uint32_t kMenuClockHz = 30;
/// How long one place of the Library button's spinner stays lit, in those
/// units. Eight places are one turn of kit::refresh_revolution_ms, so the
/// rate follows the clock and not the frame rate.
constexpr uint32_t kSpinnerUnitsPerPlace = kit::refresh_revolution_ms * kMenuClockHz / 1000u /
                                           static_cast<uint32_t>(kit::oa_button_spinner_places);
static_assert(kSpinnerUnitsPerPlace == 6u);
/// The sound Settings plays as it opens on the main menu.
constexpr std::string_view kOpenSound = "Options";

/// Returns how a main-menu button looks: lit under the pointer, sunk while
/// a press on it is held there, and at rest under a modal screen.
///
/// @param hovered the pointer is over the button
/// @param pressed a press on the button is held
/// @param covered a modal screen of the OA layer shows
/// @return the look
settings::ButtonLook button_look(bool hovered, bool pressed, bool covered) noexcept {
    if (covered || !hovered)
        return settings::ButtonLook::idle;
    return pressed ? settings::ButtonLook::pressed : settings::ButtonLook::hovered;
}

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle, in source pixels
/// @param x the point's column
/// @param y the point's row
/// @return true inside it
bool contains(const artless::SourceRect& rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/// Returns the source pixel an input's pointer is over: on the main menu
/// the pointer is in the picture's pixels.
///
/// @param input the input
/// @param[out] x the pixel's column
/// @param[out] y the pixel's row
void pointer_pixel(const ScreenInput& input, int32_t& x, int32_t& y) {
    x = static_cast<int32_t>(std::floor(input.x));
    y = static_cast<int32_t>(std::floor(input.y));
}

} // namespace

void Runtime::destroy_engine_settings_menu_host(EngineSettingsMenuHost* host) noexcept {
    delete host;
}

Runtime::EngineSettingsMenuHost& Runtime::engine_settings_menu_host() {
    if (!engine_settings_menu_)
        engine_settings_menu_.reset(new EngineSettingsMenuHost{});
    return *engine_settings_menu_;
}

bool Runtime::EngineSettingsMenuHost::button_shown(Runtime& runtime) {
    return runtime.screen_ == Screen::main_menu && !runtime.frame_owned_by_package() &&
           runtime.surface_.width >= static_cast<uint32_t>(kCanvasWidth) &&
           runtime.surface_.height >= static_cast<uint32_t>(kCanvasHeight) &&
           runtime.engine_settings_fonts() != nullptr;
}

artless::SourceRect Runtime::EngineSettingsMenuHost::button_rect(const Runtime& runtime) {
    const artless::SourceRect beside = settings_button_rect(runtime);
    return {beside.x - kButtonGap - beside.width, beside.y, beside.width, beside.height};
}

artless::SourceRect Runtime::EngineSettingsMenuHost::settings_button_rect(const Runtime& runtime) {
    constexpr int32_t side = settings::menu_button_side;
    const int32_t y =
        runtime.main_menu_overlay_ ? kButtonInset : kCanvasHeight - kButtonInset - side;
    return {kCanvasWidth - kButtonInset - side, y, side, side};
}

artless::RgbaPicture Runtime::EngineSettingsMenuHost::settings_icon(Runtime& runtime) {
    auto& host = runtime.engine_settings_menu_host();
    if (!host.settings_picture && !host.settings_picture_missing) {
        WindowIcon decoded;
        std::string error;
        if (decode_window_icon(settings_icon_png(), decoded, error))
            host.settings_picture = visible_part(decoded);
        if (!host.settings_picture || host.settings_picture->pixels.empty()) {
            host.settings_picture.reset();
            host.settings_picture_missing = true;
            std::cerr << "open-annihilation: the settings icon is missing"
                      << (error.empty() ? std::string{} : ": " + error) << '\n';
        }
    }
    if (!host.settings_picture)
        return {};
    return {
        host.settings_picture->width, host.settings_picture->height, host.settings_picture->pixels
    };
}

artless::RgbaPicture Runtime::EngineSettingsMenuHost::mods_icon(Runtime& runtime) {
    auto& host = runtime.engine_settings_menu_host();
    if (!host.mods_picture && !host.mods_picture_missing) {
        WindowIcon decoded;
        std::string error;
        if (decode_window_icon(mods_icon_png(), decoded, error))
            host.mods_picture = visible_part(decoded);
        if (!host.mods_picture || host.mods_picture->pixels.empty()) {
            host.mods_picture.reset();
            host.mods_picture_missing = true;
            std::cerr << "open-annihilation: the mods icon is missing"
                      << (error.empty() ? std::string{} : ": " + error) << '\n';
        }
    }
    if (!host.mods_picture)
        return {};
    return {host.mods_picture->width, host.mods_picture->height, host.mods_picture->pixels};
}

int Runtime::EngineSettingsMenuHost::button_event(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    auto& host = runtime.engine_settings_menu_host();
    const auto& input = *context->input;
    // Under a modal screen of the OA layer the buttons see no input: the
    // layer takes it first.
    if (runtime.oa_layer().modal_shown(false) || !button_shown(runtime)) {
        host.button_hovered = false;
        host.button_pressed = false;
        host.settings_hovered = false;
        host.settings_pressed = false;
        return 0;
    }
    if (input.kind == ScreenInputKind::key_down &&
        engine_settings_shortcut(input.key, input.modifiers)) {
        runtime.open_engine_settings_from_menu();
        return 1;
    }
    int32_t x = 0;
    int32_t y = 0;
    pointer_pixel(input, x, y);
    const bool over_library = contains(button_rect(runtime), x, y);
    const bool over_settings = contains(settings_button_rect(runtime), x, y);
    switch (input.kind) {
    case ScreenInputKind::pointer_move:
        host.button_hovered = over_library;
        host.settings_hovered = over_settings;
        return 0;
    case ScreenInputKind::pointer_down:
        host.button_hovered = over_library;
        host.settings_hovered = over_settings;
        if (input.button != SDL_BUTTON_LEFT || (!over_library && !over_settings))
            return 0;
        host.button_pressed = over_library;
        host.settings_pressed = over_settings;
        return 1;
    case ScreenInputKind::pointer_up: {
        host.button_hovered = over_library;
        host.settings_hovered = over_settings;
        if (input.button != SDL_BUTTON_LEFT || (!host.button_pressed && !host.settings_pressed))
            return 0;
        // A press opens what its button opens when it is released over that
        // button: the OA button the Library, on its Active tab,
        // and the Settings button Settings, with no Library under it.
        // Released elsewhere it does nothing.
        const bool opens_library = host.button_pressed && over_library;
        const bool opens_settings = host.settings_pressed && over_settings;
        host.button_pressed = false;
        host.settings_pressed = false;
        if (opens_library)
            runtime.open_library(std::nullopt);
        else if (opens_settings)
            runtime.open_engine_settings_from_menu();
        return 1;
    }
    default:
        return 0;
    }
}

kit::OaButtonMark Runtime::EngineSettingsMenuHost::library_mark(Runtime& runtime) {
    LibraryHost& shown = runtime.library_host();
    if (!shown.refreshed)
        shown.rebuild(runtime);
    // The Mods tab's count: every mod it lists, Base Game left out.
    const std::size_t mods = oa::ui::library::tab_count(shown.model, oa::ui::library::Tab::mods);
    kit::OaButtonMark mark;
    mark.dot = runtime.library_update_count() > 0;
    // A fetch that has not yet produced a list has no number to show. Once
    // a list is in, a later fetch keeps that number: the spinner stops.
    const bool arriving = shown.model.inputs.checking && shown.model.inputs.list_age_seconds < 0;
    if (!shown.refreshed || arriving) {
        mark.corner = kit::OaButtonCorner::spinner;
        mark.turn = runtime.frontend_tick() / kSpinnerUnitsPerPlace;
        return mark;
    }
    mark.corner = kit::OaButtonCorner::count;
    mark.count = mods;
    return mark;
}

void Runtime::EngineSettingsMenuHost::button_draw(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!button_shown(runtime) || context->surface == nullptr)
        return;
    const kit::OaButtonMark library_badge = library_mark(runtime);
    const auto& host = runtime.engine_settings_menu_host();
    const bool covered = runtime.oa_layer().modal_shown(false);
    const auto rect = button_rect(runtime);
    // The dot stays in the top right while updates can be installed. The
    // bottom right is the mod count, or the spinner while it is not final.
    kit::draw_oa_button(
        {context->surface,
         {rect.x, rect.y, 1},
         runtime.engine_settings_fonts(),
         mods_icon(runtime)},
        rect.width,
        static_cast<kit::OaButtonState>(
            button_look(host.button_hovered, host.button_pressed, covered)
        ),
        library_badge
    );
    // The Settings button wears neither the dot nor the mod count.
    const auto beside = settings_button_rect(runtime);
    kit::draw_oa_button(
        {context->surface,
         {beside.x, beside.y, 1},
         runtime.engine_settings_fonts(),
         settings_icon(runtime)},
        beside.width,
        static_cast<kit::OaButtonState>(
            button_look(host.settings_hovered, host.settings_pressed, covered)
        ),
        false
    );
}

void Runtime::register_engine_settings_button() {
    OverlayDesc button{};
    button.name = "engine_settings_button";
    button.screen = screen_id(Screen::main_menu);
    button.z = kButtonOverlayZ;
    button.event = EngineSettingsMenuHost::button_event;
    button.draw = EngineSettingsMenuHost::button_draw;
    // A refused overlay is recorded in the registry, and register_screens
    // reports it once every screen and overlay is in.
    overlay_register(&screens_, &button);
}

void Runtime::open_engine_settings_from_menu() {
    // Nothing opens over a message box, over a frame a package owns, or a
    // second time.
    if (screen_ != Screen::main_menu || frame_owned_by_package() ||
        oa::ui::frontend_dialogs::dialog_count() != 0 || engine_settings_dialog() != nullptr ||
        saves_notice_shown() || engine_settings_fonts() == nullptr)
        return;
    // The dialog is drawn and fed from engine_settings_dialog(), as the OA
    // layer's settings screen, over every other screen of the layer. A
    // settings screen whose dialog closed outside its events goes first.
    if (oa_layer().find("settings") != nullptr) {
        oa_layer().close_above("settings");
        oa_layer().close_top();
    }
    open_engine_settings_dialog();
    push_settings_screen(false);
    auto& host = engine_settings_menu_host();
    host.button_hovered = false;
    host.button_pressed = false;
    host.settings_hovered = false;
    host.settings_pressed = false;
    play_ui_sound(kOpenSound, 0);
}

bool Runtime::engine_settings_shortcut(uint32_t key, uint16_t modifiers) noexcept {
#ifdef SDL_PLATFORM_MACOS
    constexpr uint16_t kShortcutModifier = SDL_KMOD_GUI;
#else
    constexpr uint16_t kShortcutModifier = SDL_KMOD_CTRL;
#endif
    return key == SDLK_COMMA && (modifiers & kShortcutModifier) != 0;
}

void Runtime::request_engine_settings() {
    if (screen_ == Screen::main_menu)
        open_engine_settings_from_menu();
    else if (screen_ == Screen::match)
        open_engine_settings_in_match();
}

bool Runtime::take_engine_settings_request(const SDL_Event& event) {
    // Whether the pointer event the screens are about to see is a finger's.
    EngineSettingsState::note_pointer_source(*this, event);
    if (!is_engine_settings_menu_event(event))
        return false;
    request_engine_settings();
    return true;
}

} // namespace oa::app
