// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's long options, their usage text and checks, the 3.1c network
// switches, and the values they hand the frontend and a new match.
#include "net_options.hpp"

#include "oa/app/app.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/base/text.hpp"
#include "oa/core/game_state.h"
#include "oa/netgame/dplay.hpp"

#include <array>
#include <charconv>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::app {
namespace {

using netgame::LaunchPlan;
using netgame::LaunchRefusal;
using netgame::LaunchRole;
using netgame::read_launch_file;

/// Parses a UDP port number.
///
/// Anything but a whole number from 1 through 65535 throws std::runtime_error
/// "--dplay-port expects a port from 1 through 65535".
///
/// @param text The option's value.
/// @return The port.
[[nodiscard]] uint16_t parse_port(std::string_view text) {
    uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0 ||
        value > 65535)
        throw std::runtime_error("--dplay-port expects a port from 1 through 65535");
    return static_cast<uint16_t>(value);
}

/// Parses an application GUID.
///
/// Anything but eight-four-four-four-twelve hexadecimal digits, any case,
/// with or without braces, throws std::runtime_error
/// "--dplay-guid expects a GUID, with or without braces".
///
/// @param text The option's value.
/// @return The GUID in wire byte order.
[[nodiscard]] std::array<uint8_t, 16> parse_guid(std::string_view text) {
    std::array<uint8_t, 16> bytes{};
    if (!oa::netgame::parse_application_guid(text, bytes.data()))
        throw std::runtime_error("--dplay-guid expects a GUID, with or without braces");
    return bytes;
}

/// Returns the NetgameContext behind an extension context pointer.
///
/// @param context The extension table's context.
/// @return The context it points to.
NetgameContext& context_of(void* context) {
    return *static_cast<NetgameContext*>(context);
}

/// Takes one of network play's long options (Extension::take_option).
///
/// --net-loopback-check makes the run an unattended headless one (the check
/// runs only in a headless run; without it the run would sit in the menus),
/// --check-host-not-found an unattended run that skips the intro,
/// --check-recording-hook an unattended headless run that skips the intro,
/// --host, --join and --launch a run that skips the intro, and
/// --list-content the same for a content list. --json is taken only beside
/// --list-content. A missing or malformed value throws
/// std::runtime_error.
///
/// @param context The NetgameContext whose options receive the value.
/// @param name Option name with its dashes.
/// @param values Arguments that follow the option.
/// @param[in,out] effects Gains the option_effect bits the option implies.
/// @return False when the option is not network play's.
bool take_option(void* context, const char* name, const OptionValues& values, uint32_t& effects) {
    auto& result = context_of(context).options;
    const std::string_view argument(name);
    auto value = [&values]() -> std::string_view { return values.next(values.arguments); };
    if (argument == "--net-loopback-check") {
        result.net_loopback_ticks = parse_count(value());
        effects |= option_effect::headless_check | option_effect::unattended;
    } else if (argument == "--net-loopback-watcher")
        result.net_loopback_watcher = true;
    else if (argument == "--net-loopback-computer")
        result.net_loopback_computer = true;
    else if (argument == "--check-host-not-found") {
        result.check_host_not_found = true;
        effects |= option_effect::unattended | option_effect::skip_intro;
    } else if (argument == "--dplay-port") {
        result.dplay_port = parse_port(value());
        result.cli_dplay_port = true;
    } else if (argument == "--dplay-guid") {
        result.dplay_guid = parse_guid(value());
        result.cli_dplay_guid = true;
    } else if (argument == "--launch") {
        result.launch_yaml = std::string(value());
        effects |= option_effect::skip_intro;
    } else if (argument == "--list-content") {
        result.list_content = true;
        effects |= option_effect::skip_intro;
    } else if (argument == "--json")
        result.json_requested = true;
    else if (argument == "--play-demo")
        result.play_demo = path_from_utf8(value());
    else if (argument == "--check-recording-hook") {
        result.check_recording_hook = true;
        effects |=
            option_effect::headless_check | option_effect::unattended | option_effect::skip_intro;
    } else if (argument == "--host" || argument == "--join") {
        using Kind = oa::ui::frontend_multiplayer::DirectGame::Kind;
        const Kind kind = argument == "--host" ? Kind::host : Kind::join;
        auto& game = result.direct_game;
        if (game.kind != Kind::none && game.kind != kind)
            throw std::runtime_error("--host and --join cannot be used together");
        game.kind = kind;
        if (kind == Kind::join)
            game.address = std::string(value());
        effects |= option_effect::skip_intro;
    } else if (argument == "--player-name") {
        result.direct_game.player_name = std::string(value());
        result.cli_player_name = true;
    } else if (argument == "--game-name") {
        result.direct_game.game_name = std::string(value());
        result.cli_game_name = true;
    } else if (argument == "--game-password") {
        result.direct_game.password = std::string(value());
        result.cli_password = true;
    } else if (argument == "--net-record")
        result.net_record = path_from_utf8(value());
    else if (argument == "--replay-viewer")
        result.replay_viewer = true;
    else if (argument == "--demo-unit-table") {
        const auto mode = std::string(value());
        if (mode != "strict" && mode != "ignore")
            throw std::runtime_error("--demo-unit-table expects strict or ignore");
        result.demo_ignore_unit_table = mode == "ignore";
    } else
        return false;
    return true;
}

/// Copies text into a launch field, or refuses when it does not fit.
///
/// The message names the field and the limit. It does not repeat the text,
/// so a password is never written out.
///
/// @param field the field, including room for a terminating NUL
/// @param text the text
/// @param source the launch file's name
/// @param name the setting's name
void store_launch_text(
    std::span<char> field, std::string_view text, std::string_view source, std::string_view name
) {
    if (field.empty() || text.size() >= field.size()) {
        const auto limit = field.empty() ? std::size_t{0} : field.size() - 1;
        throw LaunchRefusal(
            std::string(source) + ": " + std::string(name) + ": is longer than " +
            std::to_string(limit) + " characters"
        );
    }
    oa::base::text::copy_terminated(field, text);
}

/// Reads a launch file into the context, then puts the command line's own fields back.
///
/// --host, --join, --player-name, --game-name, --game-password, --dplay-guid,
/// --dplay-port, "-h" and "-n" each keep the field they named. The password
/// is stored and not written out.
///
/// @param[in,out] netgame the context the file and the command line share
void apply_launch_file(NetgameContext& netgame) {
    namespace nl = oa::app::netgame::launch;
    using Kind = oa::ui::frontend_multiplayer::DirectGame::Kind;
    auto& options = netgame.options;
    auto& switches = netgame.launch;
    const bool yaml = !options.launch_yaml.empty();
    const std::string path = yaml ? options.launch_yaml : switches.ini_path;
    const LaunchPlan plan = read_launch_file(path, yaml);

    const Kind cli_kind = options.direct_game.kind;
    const std::string cli_address = options.direct_game.address;
    const std::string cli_player = options.direct_game.player_name;
    const std::string cli_game = options.direct_game.game_name;
    const std::string cli_password = options.direct_game.password;
    const bool cli_player_set = options.cli_player_name;
    const bool cli_game_set = options.cli_game_name;
    const bool cli_password_set = options.cli_password;
    const auto cli_port = options.dplay_port;
    const auto cli_guid = options.dplay_guid;
    const bool from_session = switches.session_from_switch;
    const std::string saved_session(switches.block.session_name);
    const uint32_t saved_hosting = switches.block.hosting;
    const bool from_connection = switches.connection_from_switch;
    const int32_t saved_type = switches.block.connection_type;
    const std::string saved_address(switches.block.address);

    switches.block = plan.block;
    if (from_session)
        store_launch_text(switches.block.session_name, saved_session, plan.source, "session");
    if (from_connection) {
        switches.block.connection_type = saved_type;
        store_launch_text(switches.block.address, saved_address, plan.source, "address");
    }

    if (cli_kind == Kind::host) {
        options.direct_game.kind = Kind::host;
        options.direct_game.address.clear();
        switches.block.hosting = 1;
    } else if (cli_kind == Kind::join) {
        options.direct_game.kind = Kind::join;
        options.direct_game.address = cli_address;
        switches.block.hosting = 0;
        if (!from_connection)
            store_launch_text(switches.block.address, cli_address, plan.source, "game.address");
    } else if (from_session) {
        options.direct_game.kind = Kind::host;
        options.direct_game.address.clear();
        switches.block.hosting = saved_hosting;
    } else if (plan.role == LaunchRole::host || plan.ini) {
        options.direct_game.kind = Kind::host;
        options.direct_game.address.clear();
        switches.block.hosting = 1;
        if (!from_connection)
            switches.block.connection_type = nl::connection_type::tcpip;
    } else if (plan.role == LaunchRole::join) {
        options.direct_game.kind = Kind::join;
        options.direct_game.address = plan.address;
        switches.block.hosting = 0;
        if (!from_connection) {
            switches.block.connection_type = nl::connection_type::tcpip;
            store_launch_text(switches.block.address, plan.address, plan.source, "game.address");
        }
    } else {
        throw LaunchRefusal(plan.source + ": game.role: expected host or join");
    }

    if (cli_game_set) {
        options.direct_game.game_name = cli_game;
        store_launch_text(switches.block.session_name, cli_game, plan.source, "game.name");
    } else if (from_session) {
        options.direct_game.game_name = saved_session;
    } else {
        options.direct_game.game_name = plan.game_name;
    }

    if (cli_player_set) {
        options.direct_game.player_name = cli_player;
        store_launch_text(switches.block.user_name, cli_player, plan.source, "game.player");
    } else {
        options.direct_game.player_name = plan.player_name;
    }

    if (cli_password_set) {
        options.direct_game.password = cli_password;
        store_launch_text(switches.block.password, cli_password, plan.source, "game.password");
    } else {
        options.direct_game.password = plan.password;
    }

    if (options.cli_dplay_port)
        options.dplay_port = cli_port;
    else
        options.dplay_port = plan.dplay_port;
    if (options.cli_dplay_guid)
        options.dplay_guid = cli_guid;
    else
        options.dplay_guid = plan.dplay_guid;

    options.launch_content = plan.content;
    options.launch_status = plan.status;
    options.launch_from_file = true;
    options.launch_source = plan.source;
}

/// Checks the network game options as a whole (Extension::check_options).
///
/// A launch file is read here, after the long options and the game switches,
/// and each field the command line named is put back. --player-name,
/// --game-name and --game-password need --host or --join, or a launch file
/// that hosts or joins. A game to host or join has the frontend open
/// multiplayer over TCP/IP at once, whatever connection "-n" named.
/// --list-content needs --json, and --json needs --list-content. A refused
/// combination throws std::runtime_error. A launch file that cannot be used
/// throws LaunchRefusal.
///
/// @param context The NetgameContext whose options are checked.
void check_options(void* context) {
    auto& netgame = context_of(context);
    auto& options = netgame.options;
    if (options.list_content && !options.json_requested)
        throw std::runtime_error("--list-content needs --json");
    if (options.json_requested && !options.list_content)
        throw std::runtime_error("--json needs --list-content");
    if (netgame.launch.ini_seen && !options.launch_yaml.empty())
        throw LaunchRefusal(
            options.launch_yaml + ": the file: a launch file and -c cannot both be given"
        );
    if (netgame.launch.ini_seen && netgame.launch.ini_path.empty())
        throw LaunchRefusal("-c: the file: -c needs a file");
    if (netgame.launch.ini_seen || !options.launch_yaml.empty())
        apply_launch_file(netgame);
    const auto& game = options.direct_game;
    if (game.kind == oa::ui::frontend_multiplayer::DirectGame::Kind::none) {
        if (!game.player_name.empty())
            throw std::runtime_error("--player-name needs --host or --join");
        if (!game.game_name.empty())
            throw std::runtime_error("--game-name needs --host or --join");
        if (!game.password.empty())
            throw std::runtime_error("--game-password needs --host or --join");
        return;
    }
    oa::app::netgame::launch::set_connection_type(
        netgame.launch, oa::app::netgame::launch::connection_type::tcpip
    );
}

/// Returns the handler of the 3.1c network switches (Extension::switch_handler).
///
/// @param context The NetgameContext holding the handler.
/// @return The handler fill_option_hooks bound to the context's LaunchSwitches.
const oa::app::command_line::SwitchHandler* switch_handler(void* context) {
    return &context_of(context).switches;
}

/// Returns network play's wording of the usage text and of the -r refusal (Extension::text).
///
/// @param context Extension context (unused).
/// @param which Text asked for.
/// @return The text; "" for no usage note.
const char* text(void* /*context*/, ExtensionText which) {
    switch (which) {
    case ExtensionText::usage_checks:
        return "[--check-recording-hook] ";
    case ExtensionText::usage_runs:
        return "[--host | --join ADDRESS] [--player-name NAME] [--game-name NAME] "
               "[--game-password PASSWORD] [--launch FILE] [--list-content --json] "
               "[--net-loopback-check TICKS] [--net-loopback-watcher] [--net-loopback-computer] "
               "[--check-host-not-found] [--dplay-port PORT] [--dplay-guid GUID] "
               "[--play-demo FILE.tad] "
               "[--demo-unit-table strict|ignore] [--net-record FILE] [--replay-viewer] ";
    case ExtensionText::usage_switches:
        return "-c FILE -t SECONDS -n TYPE[:ADDRESS] -hNAME ";
    case ExtensionText::usage_note:
        return "";
    case ExtensionText::register_switch:
        return "-r registers the game as a DirectPlay application, which this game does not "
               "need";
    }
    return nullptr;
}

/// Hands the frontend the launch's names (Extension::frontend_entry).
///
/// The game name is the launch block's session name, which "-h" and
/// a launch write. While a launch is active the launch's
/// user name is the nickname.
///
/// @param context The NetgameContext holding the switches.
/// @param[out] entry Receives game_name, and nickname under an active launch.
void frontend_entry(void* context, FrontendEntry& entry) {
    const auto& netgame = context_of(context);
    const auto& block = netgame.launch.block;
    entry.game_name = block.session_name;
    const bool launched = netgame.launch_active != nullptr && netgame.launch_active(nullptr);
    if (launched)
        entry.nickname = block.user_name;
}

/// Starts the multiplayer frontend states with the fields the network switches set (Extension::frontend_states).
///
/// A connection type ("-n") opens multiplayer from the main menu at once and
/// a setup request (request_setup, which "-y" makes) asks for step
/// setup_requested on the first main-menu update.
///
/// @param context The NetgameContext holding the switches and the states.
/// @param[out] handler Receives the handler that runs the states.
void frontend_states(void* context, oa::ui::frontend_state::StateHandler& handler) {
    auto& netgame = context_of(context);
    netgame.frontend.state.connection_type = netgame.launch.block.connection_type;
    netgame.frontend.state.setup_request = netgame.launch.netsetup;
    handler = oa::netgame::frontend::state_handler(netgame.frontend);
}

/// Answers init_score_reporting: 0, as joining never waits for a match report to start.
///
/// The report starts as the battle room's match launches instead
/// (NetworkPlay::start_reporter).
///
/// @param ctx Unused.
/// @param state Unused.
/// @return 0.
uint32_t query_score_reporting(ScreenContext* /*ctx*/, void* /*state*/) noexcept {
    return 0;
}

/// Answers transport_kind: TCP/IP, over which every game plays.
///
/// @param ctx Unused.
/// @param state Unused.
/// @return oa::netgame::frontend::transport_kind::tcpip.
uint32_t query_transport_kind(ScreenContext* /*ctx*/, void* /*state*/) noexcept {
    return oa::netgame::frontend::transport_kind::tcpip;
}

/// Writes the -t and -e values into a new match's Game block (Extension::match_game).
///
/// @param context The NetgameContext holding the switches.
/// @param[out] game Receives player_timeout_seconds and send_error_percent.
void match_game(void* context, oa::Game& game) {
    const auto& launch = context_of(context).launch;
    game.player_timeout_seconds = launch.net_timeout_seconds;
    game.send_error_percent = launch.send_error_percent;
}

} // namespace

NetgameContext& netgame_context() {
    static NetgameContext context{};
    return context;
}

void register_frontend_state_queries(ScreenRegistry* registry) noexcept {
    namespace query = oa::netgame::frontend::query;
    (void)query_register(registry, query::init_score_reporting, query_score_reporting, nullptr);
    (void)query_register(registry, query::transport_kind, query_transport_kind, nullptr);
}

void adopt_launch_run(Options& options) {
    auto& net = netgame_context().options;
    if (!net.launch_from_file)
        return;
    if (net.launch_content.named && options.content.empty()) {
        std::string text = net.launch_content.base_game;
        if (!net.launch_content.expansions.empty()) {
            if (!text.empty())
                text.push_back(':');
            for (std::size_t at = 0; at < net.launch_content.expansions.size(); ++at) {
                if (at != 0)
                    text.push_back(',');
                text += net.launch_content.expansions[at];
            }
        }
        if (!text.empty()) {
            options.content = std::move(text);
            net.launch_content_applied = true;
        }
    }
    if (!net.launch_content.mod.empty() && options.mod_file.empty() && options.mod_dir.empty() &&
        !options.base_game)
        net.launch_mod_pending = true;
}

void fill_option_hooks(Extension& table, NetgameContext& context) {
    context.switches = oa::app::netgame::launch::launch_switch_handler(&context.launch);
    table.context = &context;
    table.take_option = take_option;
    table.check_options = check_options;
    table.switch_handler = switch_handler;
    table.text = text;
    table.frontend_entry = frontend_entry;
    table.frontend_states = frontend_states;
    table.match_game = match_game;
}

} // namespace oa::app
