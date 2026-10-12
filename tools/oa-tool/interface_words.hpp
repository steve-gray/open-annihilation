// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The English texts oa-tool interface-words prints: the lists the modules
// that look a text up export, joined, sorted and kept once.

#pragma once

#include <string_view>
#include <vector>

namespace oa::tool {

/// Returns the English texts this build passes to the interface catalogue.
///
/// The order is sorted, and each text is listed once. An empty text a
/// module exports is left out. The views point at the modules' own lists
/// and stay valid for the whole run.
///
/// @return the texts `interface-words --json` prints
[[nodiscard]] std::vector<std::string_view> collected_interface_words();

} // namespace oa::tool
