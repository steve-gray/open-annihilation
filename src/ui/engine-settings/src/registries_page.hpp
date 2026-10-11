// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Registries part of Downloads (registries_page.cpp): every registry the
// host offers, each with its avatar, name, subtitle and switch, and for the
// ones the player added REMOVE and ADD MIRROR…; the note under them; the
// heading with its ‹ DOWNLOADS link; and ADD REGISTRY… in the footer. The
// dialog's events (dialog.cpp) and its drawing (dialog_draw.cpp) go through
// here while the part shows.
#pragma once

#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/layout.hpp"

#include "geometry.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings::registries {

/// The part's heading, in English.
inline constexpr std::string_view heading_text = "DOWNLOADS · REGISTRIES";
/// The link at the heading's left that goes back to Downloads' own rows.
inline constexpr std::string_view back_text = "‹ DOWNLOADS";
/// The footer's button in Restore defaults' place.
inline constexpr std::string_view add_registry_text = "ADD REGISTRY…";
/// The lock ADD REGISTRY… and ADD MIRROR… show during a game.
inline constexpr std::string_view game_lock_text = "Choose from the main menu.";
/// What a row marked for removal says in its subtitle's place.
inline constexpr std::string_view removed_text = "Removed when you press OK";
/// The chip after a built-in registry's name.
inline constexpr std::string_view built_in_text = "Built in";

/// Tells whether the Registries part shows.
///
/// @param dialog the dialog
/// @return true on the engine's Downloads with its Registries part open
[[nodiscard]] bool shown(const Dialog& dialog) noexcept;

/// Opens the Registries part from its top. A focus the keys have shown goes
/// to the ‹ DOWNLOADS link.
///
/// @param[in,out] dialog the dialog, on Downloads
void show(Dialog& dialog) noexcept;

/// Goes back to Downloads' own rows, from their top, as ‹ DOWNLOADS, Escape
/// and a pad's B do. A focus the keys have shown goes to the Registries
/// row's MANAGE….
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::redraw
DialogAction back(Dialog& dialog) noexcept;

/// Returns the Registries row's value, from the switches as the player has
/// them: "{n} on of {m}", the one built-in registry's name and "only" while
/// it alone is on, or "None" while no registry is listed. A registry marked
/// for removal is not counted.
///
/// @param dialog the dialog
/// @return the value
[[nodiscard]] std::string summary(const Dialog& dialog);

/// Returns a registry's avatar: the first letters of its first three words
/// when its name has two or more, or the first two letters of its one word,
/// in upper case.
///
/// @param name the registry's name, in UTF-8
/// @return at most three characters
[[nodiscard]] std::string initials(std::string_view name);

/// Returns how long ago something was, as a subtitle says it: "just now"
/// under a minute, then whole minutes, hours and days.
///
/// @param seconds how long ago, in seconds
/// @return such as "4 min ago", "2 h ago" or "3 days ago"
[[nodiscard]] std::string age_text(int64_t seconds);

/// Returns a registry's subtitle. A built-in one: "{n} packages · checked
/// {age}". An added one: "{address} · {n} packages · not reviewed", with
/// " · {k} mirrors" after it when the player added mirrors. When the last
/// refresh found nothing usable, "couldn't reach · list from {age}" stands
/// where a built-in registry's checked age stands, and after an added one's.
///
/// @param row the registry
/// @return the subtitle
[[nodiscard]] std::string subtitle(const RegistryRow& row);

/// Returns the note under the list: that content from registries other than
/// the built-in ones, named, is not reviewed by the OA team.
///
/// @param dialog the dialog
/// @return the note
[[nodiscard]] std::string note_text(const Dialog& dialog);

/// Tells whether ADD REGISTRY… and ADD MIRROR… are locked: during a game.
///
/// @param dialog the dialog
/// @return true while a game is running
[[nodiscard]] bool adding_locked(const Dialog& dialog) noexcept;

/// Returns the part's rows placed at its scroll offset, as the section's
/// scroll reads them: no rows of the dialog's own, and the part's content
/// height, limit and offset.
///
/// @param dialog the dialog
/// @return the scroll; its rows are empty
[[nodiscard]] geometry::ScrolledRows scrolled(const Dialog& dialog);

/// Returns the offset that shows the row of a control whole, the least
/// move from the open one.
///
/// @param dialog the dialog
/// @param open the part's scroll (scrolled)
/// @param control a control of the part
/// @return the offset; the open one for a control of no row
[[nodiscard]] int32_t
scroll_showing(const Dialog& dialog, const geometry::ScrolledRows& open, int32_t control);

/// Appends the part's heading, link, rows and note to a display list: its
/// items in drawing order, its controls and the order Tab takes them in.
///
/// @param[in,out] part the list it is appended to
/// @param dialog the dialog
/// @param fonts the fonts it is drawn in; null measures at the estimated width
void add_part(oa::ui::kit::DisplayList& part, const Dialog& dialog, const DialogFonts* fonts);

/// Appends the parts the Registries part draws wholly in its view, and its
/// heading and link.
///
/// @param dialog the dialog
/// @param[in,out] parts the parts
void append_layout(const Dialog& dialog, std::vector<LayoutPart>& parts);

/// Returns the padlock and text ADD REGISTRY… gives way to in a game.
///
/// @param dialog the dialog
/// @return the lock's area, in Restore defaults' place
[[nodiscard]] geometry::SourceRect footer_lock(const Dialog& dialog) noexcept;

/// Presses a control of the part with Space or Enter: the link goes back, a
/// switch flips, REMOVE marks its row and UNDO unmarks it, ADD MIRROR… and
/// ADD REGISTRY… ask the host. Every other control is left to the dialog.
///
/// @param[in,out] dialog the dialog
/// @param control the control
/// @return what it asks; nothing when the part does not show or the control is not its own
[[nodiscard]] std::optional<DialogAction> activate(Dialog& dialog, int32_t control);

/// Takes a released press on a control of the part: a switch takes the half
/// under the pointer, and every other control acts as activate.
///
/// @param[in,out] dialog the dialog
/// @param control the control the press held and the pointer is over
/// @param x the pointer's column, in source pixels
/// @param y the pointer's row
/// @return what it asks; nothing when the control is not the part's
[[nodiscard]] std::optional<DialogAction>
release(Dialog& dialog, int32_t control, int32_t x, int32_t y);

/// Takes Left or Right on a registry's switch: Off and On.
///
/// @param[in,out] dialog the dialog
/// @param control the focused control
/// @param up true for Right
/// @return what it did; nothing when the control is not a switch of the part
[[nodiscard]] std::optional<DialogAction> step(Dialog& dialog, int32_t control, bool up);

} // namespace oa::ui::engine_settings::registries
