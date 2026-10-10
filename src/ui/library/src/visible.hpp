// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What library.cpp and search.cpp share: folding, the search's cap, and the
// visible list in its order.
#pragma once

#include "oa/ui/library/library.hpp"

#include <string>
#include <string_view>

namespace oa::ui::library {

/// Folds ASCII letters to lower case; other bytes stay as they are.
///
/// @param text the text
/// @return the folded text
[[nodiscard]] std::string folded(std::string_view text);

/// Cuts a search to max_query_bytes at a character boundary.
///
/// @param query the search
/// @return the search, whole when it fits
[[nodiscard]] std::string_view capped_query(std::string_view query);

/// Tells whether an entry shows on the library's tab, under its filter and tag.
///
/// @param library the Library
/// @param entry one of its entries
/// @param use_tag also keep only the entries with the library's tag
/// @return true when it shows
[[nodiscard]] bool shown_in_tab(const Library& library, const Entry& entry, bool use_tag);

/// Recomputes the visible entries and their order from the tab, filter, tag and search.
///
/// @param[in,out] library the Library; only `visible` changes
void update_visible(Library& library);

} // namespace oa::ui::library
