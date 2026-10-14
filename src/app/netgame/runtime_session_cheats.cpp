// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's cheat gate in a network game, against the loopback peer: the
// host's CHEATING option on both machines, the Game Settings sheet's Cheat
// Codes row, and where an accepted cheat's line goes.
#include "oa/app/runtime.hpp"
#include "network_play.hpp"

#include "oa/sim/scenario/commander_rules.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/sim/simulation_state.hpp"
#include "oa/ui/console/console.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/frontend/ingame_menu.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace {

constexpr int kSessionWaitSteps = 200;

/// Fails the console cheat check unless a condition holds.
///
/// @param ok Condition that must hold.
/// @param what Failure text; thrown as std::runtime_error "console cheat check: <what>".
void require(bool ok, const std::string& what) {
    if (!ok)
        throw std::runtime_error("console cheat check: " + what);
}

/// Returns the "<name> " that starts a chat line the local player sends.
///
/// @param world Match world whose local player speaks.
/// @return The prefix, with the player's second name.
std::string speaker_prefix(const oa::World& world) {
    const auto& name = world.game.players[world.game.local_player_index].second_name;
    std::string prefix = "<";
    prefix.append(name, strnlen(name, sizeof name));
    prefix += "> ";
    return prefix;
}

/// Tests whether a message log holds a line exactly.
///
/// @param lines Message log lines.
/// @param text Line looked for.
/// @return Whether the line is logged.
bool logged(const std::vector<std::string>& lines, std::string_view text) {
    return std::find(lines.begin(), lines.end(), text) != lines.end();
}

/// Returns the value row that follows a label row of the Game Settings sheet.
///
/// @param sheet Game Settings sheet.
/// @param label Label row text, such as "Cheat Codes:".
/// @return The value row's text, or null when the label is absent or last.
const char*
settings_value(const oa::ui::frontend::GameSettingsSheet& sheet, std::string_view label) {
    for (std::size_t i = 0; i + 1 < sheet.count; ++i)
        if (label == sheet.entries[i].text.data())
            return sheet.entries[i + 1].text.data();
    return nullptr;
}

} // namespace

void NetworkPlay::check_console_session_cheats(
    Runtime& peer, const std::function<void(bool)>& step
) {
    require(
        runtime_.match_session_kind() == oa::data::campaign::SessionKind::multiplayer &&
            peer.match_session_kind() == oa::data::campaign::SessionKind::multiplayer,
        "the loopback is not a multiplayer session"
    );
    oa::World& world = runtime_.match_->state();
    oa::Game& game = world.game;
    const auto host = sim::scenario::host_player_index(world);
    const auto* host_info =
        host != OA_PLAYER_COUNT ? oa::world_player_info(&world, &game.players[host]) : nullptr;
    require(host_info != nullptr, "no host is seated");
    const bool allowed = (host_info->options & OA_SETUP_OPTION_CHEATS_ALLOWED) != 0;
    require(
        runtime_.session_cheats_allowed_ == allowed && peer.session_cheats_allowed_ == allowed,
        "a machine's cheat flag is not the host's CHEATING option"
    );
    for (Runtime* side : {&runtime_, &peer}) {
        oa::ui::frontend::GameSettingsSheet sheet;
        oa::ui::frontend::ingame_build_game_settings(side->game_settings_view(), sheet);
        const char* shown = settings_value(sheet, "Cheat Codes:");
        require(
            shown != nullptr && std::string_view(shown) == (allowed ? "Allowed" : "Disallowed") &&
                settings_value(sheet, "Difficulty:") == nullptr,
            "the Game Settings sheet does not show the host's CHEATING option"
        );
    }
    const auto wait = [&](const auto& done, const char* what) {
        for (int i = 0; i < kSessionWaitSteps && !done(); ++i)
            step(true);
        require(done(), what);
    };
    const auto prefix = speaker_prefix(world);
    const std::string atm = prefix + "+atm";
    require(reported_chat_ != prefix + "+clock", "an option command's echo was reported");
    // Each console adds its ATM amount: 1000, or a mod profile's.
    const auto atm_amount = [](const Runtime& side) {
        const auto* profile = side.mod_profile();
        return oa::ui::console::console_atm_amount(
            profile != nullptr ? profile->rules : oa::data::match_rules::MatchRules{}
        );
    };
    const auto viewer = game.viewpoint_player;
    const float metal = game.players[viewer].metal;
    runtime_.enter_console_check_line("+atm");
    require(
        game.players[viewer].metal == metal + (allowed ? atm_amount(runtime_) : 0.0F),
        allowed ? "+atm did not run with cheats allowed" : "+atm ran with cheats disallowed"
    );
    require(reported_chat_ == atm, "the +atm line was not reported");
    wait(
        [&] { return logged(peer.match_message_lines(), atm); },
        "the +atm line did not reach the peer"
    );

    if (peer.local_player_watches()) {
        bool running = true;
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.key = SDLK_RETURN;
        enter.key.scancode = SDL_SCANCODE_RETURN;
        peer.handle_sdl_event(enter, running);
        require(!peer.chat_composing_, "Enter opened the watcher's chat line");
    } else {
        oa::Game& peer_game = peer.match_->state().game;
        const auto peer_viewer = peer_game.viewpoint_player;
        const float peer_metal = peer_game.players[peer_viewer].metal;
        const std::string peer_atm = speaker_prefix(peer.match_->state()) + "+atm";
        peer.enter_console_check_line("+atm");
        require(
            peer_game.players[peer_viewer].metal ==
                peer_metal + (allowed ? atm_amount(peer) : 0.0F),
            "the peer's +atm did not follow the host's CHEATING option"
        );
        wait(
            [&] { return logged(runtime_.match_message_lines(), peer_atm); },
            "the peer's +atm line did not reach this machine"
        );
    }

    const auto count_line = [](Runtime& side, std::string_view text) {
        int count = 0;
        for (const auto& row : side.match_message_lines())
            count += row == text ? 1 : 0;
        return count;
    };
    const auto flag_on = [](Runtime& side) {
        return (oa::ui::console::console_flags(side.match_->state().game) &
                oa::ui::console::console_flag::shoot_all) != 0;
    };
    // +ShootAll changes who units fire at, so both machines run it. Turn it
    // back off before the wait, or the units keep firing while the line travels.
    const bool shoot = flag_on(runtime_);
    const std::string shoot_line = prefix + "+shootall";
    const int shoot_seen = count_line(peer, shoot_line);
    runtime_.enter_console_check_line("+shootall");
    require(flag_on(runtime_) != shoot, "+shootall did not run");
    runtime_.enter_console_check_line("+shootall");
    require(flag_on(runtime_) == shoot, "+shootall did not turn back");
    wait(
        [&] { return count_line(peer, shoot_line) >= shoot_seen + 2 && flag_on(peer) == shoot; },
        "+shootall did not run on the peer and turn back"
    );

    const uint8_t mine = game.local_player_index;
    const auto mine_status = game.players[mine].status;
    oa::Game& remote_game = peer.match_->state().game;
    const auto peer_status = remote_game.players[mine].status;
    auto* peer_info = oa::world_player_info(&peer.match_->state(), &remote_game.players[mine]);
    const auto peer_state = peer_info != nullptr ? peer_info->state : static_cast<uint8_t>(0);
    const std::string ai = "+ai " + std::to_string(static_cast<unsigned>(mine));
    const std::string ai_line = prefix + ai;
    const int ai_seen = count_line(peer, ai_line);
    if (allowed) {
        runtime_.enter_console_check_line(ai.c_str());
        require(game.players[mine].status != mine_status, "+ai did not take the local player");
        runtime_.enter_console_check_line(ai.c_str());
        require(game.players[mine].status == mine_status, "+ai did not hand the local player back");
        wait(
            [&] {
                return count_line(peer, ai_line) >= ai_seen + 2 &&
                       peer.match_->state().game.players[mine].status == peer_status &&
                       (peer_info == nullptr || peer_info->state == peer_state);
            },
            "+ai did not reach the peer and hand the player back"
        );
    } else {
        runtime_.enter_console_check_line(ai.c_str());
        require(game.players[mine].status == mine_status, "+ai ran with cheats disallowed");
        for (int step_index = 0;
             step_index < kSessionWaitSteps && count_line(peer, ai_line) == ai_seen;
             ++step_index)
            step(true);
        require(
            peer.match_->state().game.players[mine].status == peer_status &&
                (peer_info == nullptr || peer_info->state == peer_state),
            "+ai changed the peer with cheats disallowed"
        );
    }
    require(
        peer.match_->state().game.players[mine].status == OA_PLAYER_STATUS_MIRRORED,
        "the peer's copy of this player is not mirrored"
    );

    uint8_t target = OA_PLAYER_COUNT;
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index)
        if (index != mine && sim::simulation_state::player_slot_active(index, game.players[index]))
            target = index;
    require(target != OA_PLAYER_COUNT, "no other player to control");
    const auto peer_index = peer.match_->state().game.local_player_index;
    const std::string control = "+control " + std::to_string(static_cast<unsigned>(target));
    const std::string control_line = prefix + control;
    if (allowed) {
        runtime_.enter_console_check_line(control.c_str());
        require(game.local_player_index == target, "+control did not take the other player");
        runtime_.enter_console_check_line(
            ("+control " + std::to_string(static_cast<unsigned>(mine))).c_str()
        );
        require(game.local_player_index == mine, "+control did not hand the player back");
        // The command runs before the line is shown, so the echo carries the
        // player just taken, not the one who typed it.
        wait(
            [&] {
                const auto lines = peer.match_message_lines();
                return std::any_of(lines.begin(), lines.end(), [&](const std::string& row) {
                    return row.find(control) != std::string::npos;
                });
            },
            "+control did not reach the peer"
        );
    } else {
        runtime_.enter_console_check_line(control.c_str());
        require(game.local_player_index == mine, "+control ran with cheats disallowed");
        for (int step_index = 0;
             step_index < kSessionWaitSteps && !logged(peer.match_message_lines(), control_line);
             ++step_index)
            step(true);
    }
    require(
        peer.match_->state().game.local_player_index == peer_index,
        "+control changed the peer's player"
    );

    std::cout << "console cheat check: both machines take the host's CHEATING option ("
              << (allowed ? "allowed" : "disallowed") << "), +atm "
              << (allowed ? "ran" : "was refused")
              << " and its line reached the peer and the match report; +shootall ran on both "
              << "machines; +ai "
              << (allowed ? "took this player and handed it back" : "was refused")
              << " without changing the peer's player; +control stayed on this machine\n";
}

} // namespace oa::app
