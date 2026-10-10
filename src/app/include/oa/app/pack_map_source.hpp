// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battle room's source of installed pack maps and of the base maps
// gathered in the one start scan. The multiplayer screens bind what this
// returns; they never name the runtime's pack-map methods.
#pragma once

#include "oa/ui/frontend_multiplayer/screens.hpp"

namespace oa::app {

class Runtime;

/// Returns the map source the battle room lists maps from.
///
/// count and at read the installed pack maps. base_count and base_at read
/// the base maps gathered in the one start scan. The strings point into
/// those packs and those summaries, and stay valid until the maps are listed
/// again. prepare mounts the named pack map's files and release unmounts
/// them. A map the game will not play fails prepare and writes the reason.
///
/// @param runtime the running app
/// @return the source, bound with multiplayer_bind_map_source
[[nodiscard]] oa::ui::frontend_multiplayer::LobbyMapSource pack_map_source(Runtime& runtime);

} // namespace oa::app
