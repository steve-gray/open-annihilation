// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Every setting's row, written out as literals: its kind, its label, the
// lines its hint takes without and with Controller's Steam Input notice and
// a Steam Deck's screen rate, the stops, levels or choices it offers, a
// strip's level width or a drop-down's field width, the levels a strip lets
// a player choose, whether its hint lines are its status, its lock while a
// game is in progress, and the field of the dialog's locks that locks it.

#include "geometry.hpp"

#include "oa/test/check.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/rows.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

namespace settings = oa::ui::engine_settings;
namespace geometry = oa::ui::engine_settings::geometry;
namespace kit = oa::ui::kit;

using kit::RowKind;
using settings::Lock;
using settings::Locks;
using settings::Setting;

/// What a setting's row is, as the dialog shows it.
struct Expected {
    Setting setting{};      ///< the setting
    RowKind kind{};         ///< what its control is
    std::string_view label; ///< its label, in English
    std::size_t lines{};    ///< the lines its hint takes
    /// The lines its hint takes with the Steam Input notice and a Steam
    /// Deck's screen rate.
    std::size_t deck_lines{};
    /// A slider's stops, a strip's levels or a drop-down's choices; 0 for none.
    int32_t values{};
    int32_t width{};          ///< a strip's level width or a drop-down's field width; 0 for none
    int32_t offered{};        ///< the levels of a strip a player may choose; 0 for none
    bool hint_is_status{};    ///< its hint lines are its status
    Lock in_game{};           ///< its lock while a game is in progress
    Lock Locks::* own_lock{}; ///< the field of the dialog's locks that locks it; null for none
};

// The rows, in Setting's order. A strip's offered levels and a drop-down's
// choices are those of a dialog of the engine's settings opened over the
// defaults: Language offers System default and the built-in languages.
// clang-format off
constexpr std::array<Expected, 68> kExpected{{
    {Setting::path_search, RowKind::slider, "Pathfinding cycles", 1, 1, 8, 0, 0, false, Lock::in_game, &Locks::path_search},
    {Setting::wheel_zoom, RowKind::toggle, "Mouse wheel zoom", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::max_zoom_out, RowKind::choice, "Maximum zoom out", 2, 2, 7, 200, 0, false, Lock::none, nullptr},
    {Setting::max_zoom_in, RowKind::choice, "Maximum zoom in", 2, 2, 4, 200, 0, false, Lock::none, nullptr},
    {Setting::view_past_map_edge, RowKind::levels, "View past the map's edge", 2, 2, 3, 34, 3, false, Lock::none, nullptr},
    {Setting::escape_opens_menu, RowKind::toggle, "Escape opens the game menu", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::switch_alt, RowKind::toggle, "Select groups without Alt", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::unit_limit, RowKind::slider, "Unit limit", 2, 2, 30, 0, 0, false, Lock::in_game, &Locks::unit_limit},
    {Setting::max_frame_rate, RowKind::slider, "Maximum frame rate", 1, 2, 19, 0, 0, false, Lock::none, &Locks::max_frame_rate},
    {Setting::anti_aliasing, RowKind::levels, "Enhanced anti-aliasing", 2, 2, 5, 23, 5, false, Lock::none, nullptr},
    {Setting::screen_size, RowKind::slider, "Screen size", 2, 2, 5, 0, 0, false, Lock::none, nullptr},
    {Setting::developer_mode, RowKind::toggle, "Enable Developer Mode", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::frame_stats, RowKind::toggle, "Show performance statistics", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::hardware_acceleration, RowKind::levels, "Hardware acceleration", 2, 2, 3, 34, 3, true, Lock::none, &Locks::hardware_acceleration},
    {Setting::vertical_sync, RowKind::toggle, "Vertical sync", 1, 1, 0, 0, 0, false, Lock::none, &Locks::vertical_sync},
    {Setting::language, RowKind::choice, "Language", 2, 2, 6, 200, 0, false, Lock::none, &Locks::language},
    {Setting::modern_fonts, RowKind::toggle, "Use modern fonts for game text", 2, 2, 0, 0, 0, false, Lock::none, &Locks::modern_fonts},
    {Setting::text_outline, RowKind::toggle, "Font outline", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::text_shadow, RowKind::toggle, "Font shadow", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::text_background, RowKind::toggle, "Game text background", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::unicode_chat, RowKind::toggle, "Enable Unicode Multiplayer Chat", 2, 2, 0, 0, 0, false, Lock::none, &Locks::unicode_chat},
    {Setting::text_size, RowKind::slider, "Text size", 2, 2, 26, 0, 0, false, Lock::none, &Locks::text_size},
    {Setting::touch_drag, RowKind::levels, "One-finger drag", 2, 2, 3, 59, 3, false, Lock::none, nullptr},
    {Setting::touch_hold_delay, RowKind::slider, "Hold delay", 1, 1, 10, 0, 0, false, Lock::none, nullptr},
    {Setting::touch_latches, RowKind::levels, "QUEUE and ADD", 2, 2, 2, 64, 2, false, Lock::none, nullptr},
    {Setting::touch_haptics, RowKind::toggle, "Haptics", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::touch_left_handed, RowKind::toggle, "Left-handed layout", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::touch_control_size, RowKind::levels, "Control size", 2, 2, 3, 53, 3, false, Lock::none, nullptr},
    {Setting::pad_scheme, RowKind::levels, "Scheme", 2, 2, 2, 60, 2, false, Lock::none, nullptr},
    {Setting::pad_right_trackpad, RowKind::levels, "Right trackpad", 1, 1, 2, 51, 2, false, Lock::none, nullptr},
    {Setting::pad_pointer_speed, RowKind::slider, "Pointer speed", 1, 1, 26, 0, 0, false, Lock::none, nullptr},
    {Setting::pad_acceleration, RowKind::levels, "Pointer acceleration", 1, 1, 3, 34, 3, false, Lock::none, nullptr},
    {Setting::pad_glide, RowKind::toggle, "Trackpad glide", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::pad_right_stick, RowKind::levels, "Right stick", 2, 2, 3, 46, 3, false, Lock::none, nullptr},
    {Setting::pad_magnetism, RowKind::toggle, "Magnetism (stick pointer)", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::pad_gyro, RowKind::choice, "Gyro pointer", 1, 1, 4, 248, 0, false, Lock::none, nullptr},
    {Setting::pad_gyro_speed, RowKind::slider, "Gyro speed", 1, 1, 36, 0, 0, false, Lock::none, nullptr},
    {Setting::pad_haptics, RowKind::levels, "Haptics", 1, 1, 3, 41, 3, false, Lock::none, nullptr},
    {Setting::pad_prompts, RowKind::choice, "Button prompts", 1, 1, 6, 200, 0, false, Lock::none, nullptr},
    {Setting::pad_left_handed, RowKind::toggle, "Left-handed", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::pad_steam_input_notice, RowKind::text, "Steam Input", 4, 4, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::mod, RowKind::toggle, "Mod", 2, 2, 0, 0, 0, false, Lock::in_game, &Locks::mod},
    {Setting::snap_override_key, RowKind::slider, "Snap override key", 1, 1, 16, 0, 0, false, Lock::none, nullptr},
    {Setting::autoclick_key, RowKind::slider, "Autoclick key", 1, 1, 16, 0, 0, false, Lock::none, nullptr},
    {Setting::rotate_build_key, RowKind::slider, "Rotate build key", 1, 1, 16, 0, 0, false, Lock::none, nullptr},
    {Setting::patrol_hold, RowKind::slider, "Hold position", 1, 1, 3, 0, 0, false, Lock::none, nullptr},
    {Setting::patrol_maneuver, RowKind::slider, "Maneuver", 1, 1, 3, 0, 0, false, Lock::none, nullptr},
    {Setting::patrol_roam, RowKind::slider, "Roam", 1, 1, 3, 0, 0, false, Lock::none, nullptr},
    {Setting::guard_hold, RowKind::slider, "Hold position", 1, 1, 3, 0, 0, false, Lock::none, nullptr},
    {Setting::guard_maneuver, RowKind::slider, "Maneuver", 1, 1, 3, 0, 0, false, Lock::none, nullptr},
    {Setting::guard_roam, RowKind::slider, "Roam", 1, 1, 3, 0, 0, false, Lock::none, nullptr},
    {Setting::mex_snap_radius, RowKind::slider, "Mex snap radius", 1, 1, 2, 0, 0, false, Lock::none, &Locks::mex_snap},
    {Setting::wreck_snap_radius, RowKind::slider, "Wreck snap radius", 1, 1, 2, 0, 0, false, Lock::none, &Locks::wreck_snap},
    {Setting::optimize_dt_rows, RowKind::toggle, "Optimize DT rows", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::full_rings, RowKind::toggle, "Full rings", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::chat_backdrop, RowKind::toggle, "Accessible chat", 1, 1, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::panel_background, RowKind::slider, "Resource bar background", 1, 1, 3, 0, 0, false, Lock::none, nullptr},
    {Setting::game_files_summary, RowKind::value_and_button, "Installed", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::game_files_backed_up, RowKind::toggle, "Include in device backups", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::game_files_location, RowKind::text, "Where the files are", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::user_folder, RowKind::buttons, "Your files", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
    {Setting::menu_scaling, RowKind::levels, "Menu scaling", 2, 2, 3, 71, 3, false, Lock::none, nullptr},
    {Setting::native_density, RowKind::toggle, "Native pixel density", 2, 2, 0, 0, 0, false, Lock::none, &Locks::native_density},
    {Setting::explosion_flash, RowKind::levels, "Explosion flash", 2, 2, 3, 50, 3, false, Lock::none, nullptr},
    {Setting::zoomed_out_units, RowKind::levels, "Zoomed out units", 2, 2, 3, 56, 2, false, Lock::none, nullptr},
    {Setting::zoomed_out_after, RowKind::choice, "After zoom", 2, 2, 7, 200, 0, false, Lock::none, &Locks::zoomed_out_after},
    {Setting::window_frame, RowKind::levels, "Window frame", 2, 2, 2, 85, 2, false, Lock::none, nullptr},
    {Setting::hud_scaling, RowKind::toggle, "HUD scaling", 2, 2, 0, 0, 0, false, Lock::none, nullptr},
}};
// clang-format on

static_assert(
    kExpected.size() == static_cast<std::size_t>(Setting::hud_scaling) + 1,
    "every setting has its row"
);

/// Every field of the dialog's locks that locks a row.
constexpr std::array<Lock Locks::*, 14> kLockFields{
    &Locks::path_search,
    &Locks::unit_limit,
    &Locks::max_frame_rate,
    &Locks::hardware_acceleration,
    &Locks::vertical_sync,
    &Locks::mex_snap,
    &Locks::wreck_snap,
    &Locks::text_size,
    &Locks::language,
    &Locks::modern_fonts,
    &Locks::unicode_chat,
    &Locks::mod,
    &Locks::native_density,
    &Locks::zoomed_out_after,
};

/// Returns a dialog of the engine's settings opened over the defaults.
///
/// @return the dialog
settings::Dialog engine_dialog() {
    settings::Dialog dialog;
    settings::open_dialog(dialog, {}, {}, {}, "v0.8.0", settings::Page::controls);
    return dialog;
}

/// Returns a model of a dialog's chosen settings, the dialog's only read.
///
/// @param dialog the dialog
/// @return the model
geometry::SettingsModel model_of(const settings::Dialog& dialog) {
    return geometry::reading(dialog.chosen, dialog);
}

/// Returns what a setting's row is: its row spec's kind.
///
/// @param setting the setting
/// @return its kind
RowKind row_kind(Setting setting) {
    return geometry::kind_of(setting);
}

/// Returns the lines a setting's hint takes, as its row spec gives them.
///
/// @param setting the setting
/// @param deck with the Steam Input notice and a Steam Deck's 90 Hz screen
/// @return the lines
std::size_t row_lines(Setting setting, bool deck) {
    settings::Dialog dialog = engine_dialog();
    dialog.steam_input = deck;
    dialog.steam_deck_panel_hz = deck ? 90 : 0;
    return kit::view_of(geometry::row_spec(setting), model_of(dialog), {}).hints.size();
}

/// Returns the stops, levels or choices a setting's row offers.
///
/// @param dialog a dialog opened over the defaults
/// @param setting the setting
/// @return the count; 0 for a row that offers none
int32_t row_values(const settings::Dialog& dialog, Setting setting) {
    return kit::row_count(geometry::row_spec(setting), model_of(dialog));
}

/// Returns a strip's level width or a drop-down's field width.
///
/// @param setting the setting
/// @return the width; 0 for any other row
int32_t row_width(Setting setting) {
    const auto& spec = geometry::row_spec(setting);
    if (spec.kind != RowKind::levels && spec.kind != RowKind::choice)
        return 0;
    return spec.control_width;
}

/// Returns the levels a strip lets a player choose.
///
/// @param setting the setting
/// @return the levels; 0 for any other row
int32_t row_offered(Setting setting) {
    const auto& spec = geometry::row_spec(setting);
    if (spec.kind != RowKind::levels)
        return 0;
    return kit::row_offered(spec, model_of(engine_dialog()));
}

/// Returns whether a setting's hint lines are its status.
///
/// @param setting the setting
/// @return true when they are
bool row_status(Setting setting) {
    return geometry::row_spec(setting).hint_is_status;
}

/// Returns a setting's lock among the dialog's locks: the field of the
/// locks its row reads.
///
/// @param locks the dialog's locks
/// @param setting the setting
/// @return its lock
Lock row_lock(const Locks& locks, Setting setting) {
    return geometry::lock_of(locks, setting);
}

/// Returns a setting's label.
///
/// @param setting the setting
/// @return the label, in English
std::string_view row_label(Setting setting) {
    return geometry::row_spec(setting).label;
}

void every_setting_has_its_row() {
    const settings::Dialog dialog = engine_dialog();
    const Locks game = settings::settings_locks(settings::GameState{true, false, false, false});
    for (std::size_t index = 0; index < kExpected.size(); ++index) {
        const Expected& expected = kExpected[index];
        const Setting setting = expected.setting;
        OA_CHECK(static_cast<std::size_t>(setting) == index);
        OA_CHECK(row_kind(setting) == expected.kind);
        OA_CHECK(row_label(setting) == expected.label);
        OA_CHECK(row_lines(setting, false) == expected.lines);
        OA_CHECK(row_lines(setting, true) == expected.deck_lines);
        OA_CHECK(row_values(dialog, setting) == expected.values);
        OA_CHECK(row_width(setting) == expected.width);
        OA_CHECK(row_offered(setting) == expected.offered);
        OA_CHECK(row_status(setting) == expected.hint_is_status);
        OA_CHECK(row_lock(game, setting) == expected.in_game);
    }
}

void each_row_reads_its_own_lock() {
    for (const Expected& expected : kExpected) {
        // Its own field locks it, and no other.
        for (const auto field : kLockFields) {
            Locks locks{};
            locks.*field = Lock::set_by_host;
            const Lock lock = row_lock(locks, expected.setting);
            OA_CHECK(lock == (field == expected.own_lock ? Lock::set_by_host : Lock::none));
        }
        Locks every{};
        for (const auto field : kLockFields)
            every.*field = Lock::command_line;
        OA_CHECK(
            row_lock(every, expected.setting) ==
            (expected.own_lock != nullptr ? Lock::command_line : Lock::none)
        );
    }
}

} // namespace

int main() {
    every_setting_has_its_row();
    each_row_reads_its_own_lock();
    return oa::test::check_exit_status();
}
