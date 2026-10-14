// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the running game plays, for a launch file the system opened, and the line that tells
// the player in a match that the game did not open one (network_play.hpp).
#include "oa/app/runtime.hpp"
#include "network_play.hpp"

#include "net_state.hpp"
#include "oa/app/check_host.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/sim/messages.hpp"
#include "oa/ui/console/console.hpp"

#include <string>
#include <string_view>

namespace oa::app {

netgame::RunningContent NetworkPlay::running_content() const {
    netgame::RunningContent running;
    const Runtime::PresenceAnnouncement announced = runtime_.presence_announcement();
    running.bundles_known = announced.announce_bundles;
    running.base_game = announced.base_game;
    running.expansions = announced.expansions;
    if (const auto* profile = runtime_.mod_profile())
        running.mod_id = profile->id;
    const fs::path folder = runtime_options(runtime_).layered_mod_folder.lexically_normal();
    if (!folder.empty())
        running.mod_folder = path_to_utf8(
            folder.has_filename() ? folder.filename() : folder.parent_path().filename()
        );
    return running;
}

bool NetworkPlay::post_match_line(std::string_view line) {
    if (!net_ || net_->console_host == nullptr || net_->console_host->post_message == nullptr)
        return false;
    const std::string posted(line);
    net_->console_host->post_message(
        net_->console_host->context,
        posted.c_str(),
        oa::sim::messages::kind_notice,
        oa::sim::messages::sender_none
    );
    return true;
}

} // namespace oa::app
