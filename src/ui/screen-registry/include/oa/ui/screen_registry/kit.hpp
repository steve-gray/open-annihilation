// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Settings pages, kit screens, notices and prompts declared outside the
// engine's own screens. The engine hosts them. The extension table does not
// name these types (src/ui/screen-registry/README.md).
#pragma once

#include "oa/ui/kit/components_more.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"
#include "oa/ui/kit/rows.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace oa::app {

/// The most settings pages one registry holds.
inline constexpr uint32_t kMaxSettingsPages = 16;
/// The most kit screens one registry holds.
inline constexpr uint32_t kMaxKitScreens = 16;
/// The most buttons a registered prompt has, as a question's.
inline constexpr std::size_t most_prompt_buttons = oa::ui::kit::most_question_buttons;

/// One settings page a registration asks the engine to host.
struct SettingsPageDesc {
    /// The page's id: dotted words of a to z, 0 to 9 and hyphens, at most 48
    /// bytes, and not an engine section's word. It is the page's word in
    /// automation names.
    const char* id{};
    /// The nav entry's text, UTF-8. Copied at registration.
    const char* title{};
    /// The divider's label, UTF-8. Copied at registration.
    const char* owner{};
    /// The page's rows. The caller keeps them alive for the runtime's life.
    const oa::ui::kit::PreferenceRow* rows{};
    /// How many rows the page has.
    std::size_t row_count{};
    /// Called on the main thread once for each key OK changed, after the
    /// preferences are written. The strings are read at once. Null calls nothing.
    void (*applied)(void* state, const char* key, const char* value){};
    /// Handed to applied.
    void* state{};
};

/// The settings pages registered for one runtime.
struct SettingsRegistry {
    /// The pages taken, in registration order.
    SettingsPageDesc pages[kMaxSettingsPages]{};
    /// How many pages were taken.
    uint32_t page_count{};
    /// Why the last registration was refused; null when the last one was taken.
    const char* rejected{};
    /// Copies of the pages' ids, which pages[i].id points at.
    std::string ids[kMaxSettingsPages]{};
    /// Copies of the pages' titles, which pages[i].title points at.
    std::string titles[kMaxSettingsPages]{};
    /// Copies of the pages' owners, which pages[i].owner points at.
    std::string owners[kMaxSettingsPages]{};
};

/// Takes a settings page into a registry.
///
/// @param registry the registry; null refuses the page
/// @param page the page
/// @return 1 when the page is taken; 0 when it is refused, and registry->rejected
///     then names the reason when registry is not null
int settings_page_register(SettingsRegistry* registry, const SettingsPageDesc& page);

/// What a kit screen's host tells its act: a press on a control, or a key
/// the kit's input did not take.
struct KitScreenAction {
    /// The control pressed, with key enter; no_control when the kit's input
    /// did not take the key.
    oa::ui::kit::ControlId control{oa::ui::kit::no_control};
    /// The key, as the kit names it.
    oa::ui::kit::Key key{};
};

/// One kit screen a registration asks the engine to open by name.
struct KitScreenDesc {
    /// The screen's name, in a page id's form. Its automation dialog is oa.<name>.
    const char* name{};
    /// Handed to lay_out, act and closed.
    void* model{};
    /// Lays the screen out. Its controls' kit names start with the screen's name.
    oa::ui::kit::DisplayList (*lay_out)(void* model, const oa::ui::kit::Frame& frame){};
    /// Takes a press or a key the kit did not. Returns 0 for nothing, 1 to
    /// redraw and 2 to close.
    int (*act)(void* model, const KitScreenAction& action){};
    /// Called when the screen closes. Null calls nothing.
    void (*closed)(void* model){};
};

/// The kit screens registered for one runtime.
struct KitScreenRegistry {
    /// The screens taken, in registration order.
    KitScreenDesc screens[kMaxKitScreens]{};
    /// How many screens were taken.
    uint32_t screen_count{};
    /// Why the last registration was refused; null when the last one was taken.
    const char* rejected{};
    /// Copies of the screens' names, which screens[i].name points at.
    std::string names[kMaxKitScreens]{};
};

/// Takes a kit screen into a registry.
///
/// @param registry the registry; null refuses the screen
/// @param screen the screen
/// @return 1 when the screen is taken; 0 when it is refused, and
///     registry->rejected then names the reason when registry is not null
int kit_screen_register(KitScreenRegistry* registry, const KitScreenDesc& screen);

/// A notice a registration asks the engine to show.
struct NoticeRequest {
    /// The notice's title, UTF-8.
    const char* title{};
    /// The notice's paragraphs, UTF-8, top to bottom.
    const char* const* paragraphs{};
    /// How many paragraphs the notice has.
    std::size_t paragraph_count{};
};

/// A prompt a registration asks the engine to show.
struct PromptRequest {
    /// The prompt's title, UTF-8.
    const char* title{};
    /// The prompt's paragraphs, UTF-8, top to bottom.
    const char* const* paragraphs{};
    /// How many paragraphs the prompt has.
    std::size_t paragraph_count{};
    /// The buttons' captions, left to right, from 1 to most_prompt_buttons.
    const char* const* buttons{};
    /// How many buttons the prompt has.
    std::size_t button_count{};
    /// Each button's word, of a to z, 0 to 9 and hyphens. Null names each by its place.
    const char* const* button_ids{};
    /// The button Escape answers, from 0; -1 for none.
    int32_t cancel_button{-1};
    /// The button drawn as the one the prompt leads to, from 0; -1 for none.
    int32_t primary_button{-1};
    /// Called once, on the main thread, with the button pressed, or with -1
    /// when the runtime ends with the prompt still queued or shown. Null calls nothing.
    void (*answered)(void* user, uint32_t id, int32_t button){};
    /// Handed to answered.
    void* user{};
};

} // namespace oa::app
