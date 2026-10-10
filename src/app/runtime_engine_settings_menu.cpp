// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The main menu's OA button, an overlay on the main menu under the
// extensions' overlays, and the settings dialog it opens over the darkened
// main menu as a screen of the OA layer (oa_layer.hpp). Also the shortcut and
// the requests that open the settings on whichever screen can show them.

#include "engine_settings_menu_host.hpp"
#include "engine_settings_state.hpp"
#include "oa_layer.hpp"

#include "oa/app/runtime.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace artless = oa::ui::frontend_renderer;

namespace {

/// The OA button's gap to the picture's right edge, and to its bottom edge
/// (top edge while an extension's overlay stands over the main menu), in
/// source pixels.
constexpr int32_t kButtonInset = 12;
/// The button overlay's z: under the extensions' overlays, which draw over
/// the button and see input before it.
constexpr int16_t kButtonOverlayZ = -100;
/// The sound the OA button plays as it opens the dialog.
constexpr std::string_view kOpenSound = "Options";

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
    constexpr int32_t side = settings::menu_button_side;
    const int32_t y =
        runtime.main_menu_overlay_ ? kButtonInset : kCanvasHeight - kButtonInset - side;
    return {kCanvasWidth - kButtonInset - side, y, side, side};
}

int Runtime::EngineSettingsMenuHost::button_event(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    auto& host = runtime.engine_settings_menu_host();
    const auto& input = *context->input;
    // Under a modal screen of the OA layer the button sees no input: the
    // layer takes it first.
    if (runtime.oa_layer().modal_shown(false) || !button_shown(runtime)) {
        host.button_hovered = false;
        host.button_pressed = false;
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
    const bool over = contains(button_rect(runtime), x, y);
    switch (input.kind) {
    case ScreenInputKind::pointer_move:
        host.button_hovered = over;
        return 0;
    case ScreenInputKind::pointer_down:
        host.button_hovered = over;
        if (input.button != SDL_BUTTON_LEFT || !over)
            return 0;
        host.button_pressed = true;
        return 1;
    case ScreenInputKind::pointer_up: {
        host.button_hovered = over;
        if (input.button != SDL_BUTTON_LEFT || !host.button_pressed)
            return 0;
        // A press opens the dialog when it is released over the button;
        // released elsewhere it does nothing.
        host.button_pressed = false;
        if (over)
            runtime.open_engine_settings_from_menu();
        return 1;
    }
    default:
        return 0;
    }
}

void Runtime::EngineSettingsMenuHost::button_draw(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!button_shown(runtime) || context->surface == nullptr)
        return;
    const auto& host = runtime.engine_settings_menu_host();
    auto look = settings::ButtonLook::idle;
    if (!runtime.oa_layer().modal_shown(false) && host.button_hovered)
        look = host.button_pressed ? settings::ButtonLook::pressed : settings::ButtonLook::hovered;
    const auto rect = button_rect(runtime);
    settings::draw_oa_button(
        *context->surface,
        {rect.x, rect.y, 1},
        rect.width,
        look,
        *runtime.engine_settings_fonts(),
        runtime.engine_settings_icon()
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
