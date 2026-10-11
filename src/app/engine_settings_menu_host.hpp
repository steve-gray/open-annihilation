// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The main menu's two buttons (Runtime::EngineSettingsMenuHost), which
// runtime_engine_settings_menu.cpp draws and drives as an overlay on the
// main menu: the OA button, the Open Annihilation icon, which opens the
// Library, and beside it the Settings button, the settings icon, which opens
// Settings. The Library is a screen of the OA layer (oa_layer.hpp,
// runtime_library.cpp), and so is Settings.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/app/window_icon.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <optional>

namespace oa::app {

struct Runtime::EngineSettingsMenuHost {
    bool button_hovered{};   ///< the pointer is over the OA button
    bool button_pressed{};   ///< a press on the OA button is held
    bool settings_hovered{}; ///< the pointer is over the Settings button
    bool settings_pressed{}; ///< a press on the Settings button is held
    /// The settings icon's visible part, which the Settings button and the
    /// in-game menu's OA button draw, decoded on first use.
    std::optional<WindowIcon> settings_picture{};
    bool settings_picture_missing{}; ///< decoding it failed; it is not tried again

    /// Tells whether the main menu's buttons show: the menu's own panel is
    /// drawn, no package owns the frame, and the dialog's fonts are there.
    ///
    /// @param runtime the runtime
    /// @return true while the buttons show and answer the pointer
    [[nodiscard]] static bool button_shown(Runtime& runtime);

    /// Returns where the OA button, which opens the Library, stands on the
    /// main menu's picture: left of the Settings button, a quarter of a
    /// button's side from it.
    ///
    /// @param runtime the runtime
    /// @return the button's square, in source pixels
    [[nodiscard]] static oa::ui::frontend_renderer::SourceRect button_rect(const Runtime& runtime);

    /// Returns where the Settings button stands on the main menu's picture:
    /// its bottom-right corner, or its top-right corner while an extension's
    /// overlay stands over the main menu.
    ///
    /// @param runtime the runtime
    /// @return the button's square, in source pixels
    [[nodiscard]] static oa::ui::frontend_renderer::SourceRect
    settings_button_rect(const Runtime& runtime);

    /// Returns the settings icon the Settings button and the in-game menu's
    /// OA button draw: its visible part, decoded on first use.
    ///
    /// @param runtime the runtime
    /// @return the icon's pixels, which last as long as the runtime; an
    ///         empty picture, which draws the OA mark, when it cannot be decoded
    [[nodiscard]] static oa::ui::frontend_renderer::RgbaPicture settings_icon(Runtime& runtime);

    /// The buttons overlay's input: hovering and pressing the OA button and
    /// the Settings button, and the shortcut.
    ///
    /// @param context the screen context, with the input
    /// @param state unused
    /// @return 1 when the input was taken
    static int button_event(oa::app::ScreenContext* context, void* state);

    /// Draws the OA button and the Settings button over the composed main menu.
    ///
    /// @param context the screen context, with the frame
    /// @param state unused
    static void button_draw(oa::app::ScreenContext* context, void* state);
};

} // namespace oa::app
