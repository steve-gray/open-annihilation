// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's rows as one table: for each setting, in Setting's
// order, its row spec (its id, its kind, its label and hints, and its binding
// to the settings the dialog shows), the field of the dialog's locks that
// locks it, and how Restore defaults copies it. Every decision the dialog
// makes about one setting is read from here; adding a setting adds a row.
#pragma once

#include "oa/ui/engine_settings.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/rows.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace oa::ui::engine_settings::geometry {

/// What a setting's row reads and sets: the settings, and the dialog that
/// shows them, for what a row shows beyond them (the unit limit slider's
/// highest stop, the Screen size slider's stops, Hardware acceleration's
/// status, the host's texts, the locks).
struct SettingsModel {
    EngineSettings* settings{}; ///< the settings the row reads and sets
    const Dialog* dialog{};     ///< the dialog the row is shown in
};

/// Returns a model that only reads settings: nothing is changed through it.
///
/// @param settings the settings
/// @param dialog the dialog they are shown in
/// @return the model
[[nodiscard]] inline SettingsModel
reading(const EngineSettings& settings, const Dialog& dialog) noexcept {
    return {const_cast<EngineSettings*>(&settings), &dialog};
}

/// What a setting's row is: the kit's row kinds, which kind_of gives.
using RowKind = oa::ui::kit::RowKind;

/// What MANAGE…, Game files' button, asks for: the Game files screen.
inline constexpr oa::ui::kit::ActionId manage_game_files_action = 0;
/// What Your files' first button asks for; each next button's is one more,
/// in FolderButton's order.
inline constexpr oa::ui::kit::ActionId first_folder_action = 1;

/// Returns a setting's row spec.
///
/// @param setting the setting
/// @return its spec, in the table in Setting's order
[[nodiscard]] const oa::ui::kit::RowSpec<SettingsModel>& row_spec(Setting setting) noexcept;

/// Returns what a setting's row is: its spec's kind.
///
/// @param setting the setting
/// @return the kind
[[nodiscard]] oa::ui::kit::RowKind kind_of(Setting setting) noexcept;

/// Returns a setting's name, in kebab case: its row spec's id, as
/// vertical-sync for Setting::vertical_sync.
///
/// @param setting the setting
/// @return the name
[[nodiscard]] std::string_view setting_name(Setting setting) noexcept;

/// Returns the lock a setting's row has among a dialog's locks: the field
/// that locks it.
///
/// @param locks the dialog's locks
/// @param setting the setting
/// @return its lock; Lock::none for a row no field locks
[[nodiscard]] Lock lock_of(const Locks& locks, Setting setting) noexcept;

/// Returns a setting's lock among a dialog's locks, a check's own section's
/// when it gives one.
///
/// @param locks the dialog's locks
/// @param setting the setting
/// @param section a check's own section; null for the dialog's
/// @return why it cannot be changed now
[[nodiscard]] Lock section_lock(const Locks& locks, Setting setting, const SectionHooks* section);

/// Tells whether a setting's hint lines are its status, as a check's own
/// section has it when it says.
///
/// @param setting the setting
/// @param section a check's own section; null for the dialog's
/// @return true when they are
[[nodiscard]] bool section_hint_is_status(Setting setting, const SectionHooks* section);

/// Tells whether a setting's first hint line is a folder's path, which the
/// dialog shows as its tail that fits (path_tail): Your files'.
///
/// @param setting the setting
/// @return true when it is
[[nodiscard]] bool hint_is_path(Setting setting) noexcept;

/// Copies a setting's value from one model to another: through its own
/// fields where its stops or items cannot hold every value it may have (a
/// slider's value between two stops, a language not offered, a strip's level
/// not offered, the mod), else through its kind's get and set. A row that
/// keeps no setting copies nothing.
///
/// @param setting the setting
/// @param to the model it is copied into
/// @param from the model it is copied from
void copy_row(Setting setting, const SettingsModel& to, const SettingsModel& from);

/// Returns a section's name in kebab case, as its entry's control is named:
/// common-tweaks for Page::common_tweaks.
///
/// @param page the section
/// @return the name
[[nodiscard]] std::string_view page_word(Page page) noexcept;

} // namespace oa::ui::engine_settings::geometry
