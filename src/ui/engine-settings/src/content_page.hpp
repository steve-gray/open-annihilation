// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game Content's rows (content_page.cpp): the sentence about the copy, one
// text row for each bundle the host listed, then Check again, Copy again
// from… and Open folder. The dialog's events call take for those buttons.
#pragma once

#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/kit/rows.hpp"

#include "geometry.hpp"

namespace oa::ui::engine_settings::content {

/// Places Game Content's rows from the bundles the host listed.
///
/// An empty list is the sentence and the three buttons. The host's rows are
/// read as they are: a bundle's name is a product name, and its size and
/// check are already the words to show.
///
/// @param dialog the dialog
/// @param locks unused; nothing on this page is locked
/// @param width unused; the sentence breaks on characters
/// @param[out] placed the kit's rows, when not null
/// @return the rows
[[nodiscard]] geometry::Rows place(
    const Dialog& dialog,
    const Locks& locks,
    const geometry::TextWidth& width,
    oa::ui::kit::PlacedRows* placed
);

/// Returns what one of the three buttons asks for.
///
/// @param dialog the dialog, whose bundle count places the buttons
/// @param row the row
/// @return the button's action, or DialogAction::none for the sentence and
///     a bundle's row
[[nodiscard]] DialogAction take(const Dialog& dialog, const geometry::Row& row) noexcept;

} // namespace oa::ui::engine_settings::content
