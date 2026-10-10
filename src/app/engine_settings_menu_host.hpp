// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The main menu's OA button (Runtime::EngineSettingsMenuHost), which
// runtime_engine_settings_menu.cpp draws and drives as an overlay on the
// main menu. The dialog it opens is a screen of the OA layer (oa_layer.hpp).
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

namespace oa::app {

struct Runtime::EngineSettingsMenuHost {
    bool button_hovered{}; ///< the pointer is over the OA button
    bool button_pressed{}; ///< a press on the OA button is held

    /// Tells whether the OA button shows on the main menu: the menu's own
    /// panel is drawn, no package owns the frame, and the dialog's fonts are
    /// there.
    ///
    /// @param runtime the runtime
    /// @return true while the button shows and answers the pointer
    [[nodiscard]] static bool button_shown(Runtime& runtime);

    /// Returns where the OA button stands on the main menu's picture: its
    /// bottom-right corner, or its top-right corner while an extension's
    /// overlay stands over the main menu.
    ///
    /// @param runtime the runtime
    /// @return the button's square, in source pixels
    [[nodiscard]] static oa::ui::frontend_renderer::SourceRect button_rect(const Runtime& runtime);

    /// The button overlay's input: hovering and pressing the OA button, and
    /// the shortcut.
    ///
    /// @param context the screen context, with the input
    /// @param state unused
    /// @return 1 when the input was taken
    static int button_event(oa::app::ScreenContext* context, void* state);

    /// Draws the OA button over the composed main menu.
    ///
    /// @param context the screen context, with the frame
    /// @param state unused
    static void button_draw(oa::app::ScreenContext* context, void* state);
};

} // namespace oa::app
