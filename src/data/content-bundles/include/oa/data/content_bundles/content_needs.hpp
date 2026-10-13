// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The base games and expansions a package names, shared by oamod.yaml,
// oamap.yaml and language.yaml. One reading, so a mod, a map pack and a
// language pack agree on which game they run with and which expansions
// must be on.
#pragma once

#include "oa/formats/oamod/package_keys.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace oa::formats::oamod {

/// Which base games a package runs with when it does not say.
enum class GamesWhenOmitted : uint8_t {
    totala,     ///< mods and map packs: totala
    every_game, ///< language packs: every base game
};

/// The games and expansions a manifest names, with the defaults filled in.
struct ContentNeeds {
    /// Base games the package runs with, in the order written. When the
    /// manifest omits supportedGames, totala, or every base game for a
    /// language pack. When requires.base is ta-3.1c and supportedGames is
    /// omitted, totala.
    std::vector<std::string> supported_games{};
    /// Expansions that must be on, in the order written. Empty when the
    /// manifest omits requiredExpansions, or writes an empty list.
    std::vector<std::string> required_expansions{};
    /// True when supportedGames was written and the list passed its rules.
    bool games_written{false};
    /// True when requiredExpansions was written and the list passed its rules.
    bool expansions_written{false};
    /// True when requires.base is the string ta-3.1c.
    bool legacy_base{false};
};

/// Reads supportedGames and requiredExpansions, and requires.base as the old form.
///
/// A key that is absent takes its default. A key that breaks a rule appends
/// one problem for each broken item and is left empty; the other key is
/// still read. An id is a built-in base game or expansion. A required
/// expansion must extend at least one supported game. Each problem names
/// the id. An empty requiredExpansions list means none. An empty
/// supportedGames list is refused, because leaving the key out is the
/// default. requires.base of ta-3.1c, with supportedGames omitted, reads as
/// supportedGames of totala. Any other requires.base is left to the caller.
///
/// @param root the manifest's top-level mapping
/// @param requires_block the requires mapping, or null
/// @param when_omitted which games an omitted supportedGames means
/// @param[out] needs the lists that passed, and the defaults
/// @param[in,out] problems one entry appended for each broken rule
void read_content_needs(
    const Node& root,
    const Node* requires_block,
    GamesWhenOmitted when_omitted,
    ContentNeeds& needs,
    std::vector<KeyProblem>& problems
);

} // namespace oa::formats::oamod
