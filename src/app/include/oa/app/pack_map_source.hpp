// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battle room's source of installed pack maps. The multiplayer screens
// bind what this returns; they never name the runtime's pack-map methods.
#pragma once

#include "oa/ui/frontend_multiplayer/screens.hpp"

namespace oa::app {

class Runtime;

/// Returns the map source the battle room lists installed pack maps from.
///
/// count and at read the installed packs. The strings point into those packs
/// and stay valid while the runtime lives. prepare mounts the named map's
/// files and release unmounts them. A map the game will not play fails
/// prepare and writes the reason.
///
/// @param runtime the running app
/// @return the source, bound with multiplayer_bind_map_source
[[nodiscard]] oa::ui::frontend_multiplayer::LobbyMapSource pack_map_source(Runtime& runtime);

} // namespace oa::app
