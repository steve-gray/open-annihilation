// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What library.cpp and search.cpp share: folding, the search's cap, what
// each filter keeps, and the visible list in its order.
#pragma once

#include "oa/ui/library/library.hpp"

#include <string>
#include <string_view>
#include <vector>

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

/// Tells, for each entry, whether a filter keeps it on the tab shown,
/// before the tag and the search.
///
/// @param library the Library
/// @param filter the filter
/// @return one flag per entry, in the entries' order
[[nodiscard]] std::vector<bool> kept_by_filter(const Library& library, Filter filter);

/// Tells whether an entry has the tag shown.
///
/// @param library the Library
/// @param entry one of its entries
/// @return true when its listing has the tag, and for every entry while no tag is shown
[[nodiscard]] bool has_shown_tag(const Library& library, const Entry& entry);

/// Recomputes the visible entries and their order from the tab, its filter, the tag and the search.
///
/// A tag the tab does not offer under its filter hides nothing, and is not cleared.
///
/// @param[in,out] library the Library; only `visible` changes
void update_visible(Library& library);

} // namespace oa::ui::library
