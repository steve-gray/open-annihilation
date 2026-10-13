// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A launch file fills one plan from TA Forever's INI or from Open
// Annihilation's YAML, and the command line beside the file replaces the
// fields it names.
#include "net_options.hpp"
#include "oa/app/app.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/netgame/launch_file.hpp"
#include "oa/netgame/dplay.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace mp = oa::ui::frontend_multiplayer;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

/// Removes the scratch directory when the checks end.
struct Scratch {
    fs::path dir;

    Scratch() {
        dir = fs::temp_directory_path() / "G52-launch-file";
        std::error_code failure;
        fs::remove_all(dir, failure);
        fs::create_directories(dir);
    }

    ~Scratch() {
        std::error_code failure;
        fs::remove_all(dir, failure);
    }

    /// Writes one scratch file and returns its path.
    ///
    /// @param name the file's name
    /// @param text the bytes
    /// @return the path
    fs::path write(const char* name, std::string_view text) const {
        const fs::path file = dir / name;
        std::ofstream output(file, std::ios::binary);
        expect(static_cast<bool>(output), "the scratch file opens");
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        expect(static_cast<bool>(output), "the scratch file is written");
        return file;
    }
};

constexpr std::string_view kIni = R"([totala]
watching=2
mapmode=2
location=2
cheats=1
los=1
deathopt=2
metal=1000
energy=1000
lockOptions=0
playerlimit=4
pfs=0
session=Example game
provider=TAForever
mission=Acid Foursome
timelimit=0
registeredonly=0
tournment=1
tournament=1
)";

constexpr std::string_view kYaml = R"(oalaunch: 1
game:
  role: host
  address: 192.0.2.10
  name: Example game
  player: Commander
  password: secret
  dplay:
    guid: "{99797420-F5F5-11CF-9827-00A0241496C8}"
    port: 47624
content:
  base-game: totala
  expansions: [campaign, core-contingency]
  mod: ridge
map: "Acid Foursome"
rules:
  commander: ends
  metal: 1000
  energy: 1000
  unit-limit: 250
  line-of-sight: true
  mapping: mapped
  start-positions: random
  cheats: false
  watchers: true
  player-limit: 4
  time-limit: 0
lock-options: false
seat:
  side: arm
  team: 1
status:
  stdout: true
  file: status.jsonl
  quit-after-game: true
)";

constexpr std::string_view kNamedYaml = R"(oalaunch: 1
game:
  role: host
  name: From file
  password: secret
)";

bool same_text(const char* field, const char* expected) {
    return std::strcmp(field, expected) == 0;
}

/// The message a YAML text is refused with, or empty when it is accepted.
///
/// @param text the file's bytes
/// @return the refusal
std::string refused_yaml(std::string_view text) {
    try {
        (void)oa::app::netgame::read_launch_text("launch.yaml", text, true);
    } catch (const oa::app::netgame::LaunchRefusal& error) {
        return error.what();
    }
    return {};
}

/// TA Forever's example INI fills every field it names, and ignores registeredonly.
///
/// @param file the example file
void check_ini(const fs::path& file) {
    const auto plan = oa::app::netgame::read_launch_file(oa::app::path_to_utf8(file), false);
    const auto& block = plan.block;
    expect(plan.ini && plan.role == oa::app::netgame::LaunchRole::host, "an INI hosts");
    expect(block.hosting == 1, "an INI creates the game");
    expect(block.connection_type == mp::launch::connection_type::tcpip, "an INI plays over TCP/IP");
    expect(block.watching == 2, "watching");
    expect(block.mapping == 2, "mapmode");
    expect(block.location == 2, "location");
    expect(block.cheats == 1, "cheats");
    expect(block.los == 1, "los");
    expect(block.death_option == 2, "deathopt");
    expect(block.metal == 1000 && block.energy == 1000, "metal and energy");
    expect(block.lock_options == 0, "lockOptions");
    expect(block.player_limit == 4, "playerlimit");
    expect(block.pfs == 0 && block.time_limit == 0, "pfs and timelimit stay as written");
    expect(block.tournament == 1, "tournment and tournament both set the tournament");
    expect(same_text(block.session_name, "Example game"), "session");
    expect(same_text(block.provider_label, "TAForever"), "provider");
    expect(same_text(block.mission, "Acid Foursome"), "mission");
    expect(plan.game_name == "Example game", "the game's name");
    expect(
        block.password[0] == '\0' && block.user_name[0] == '\0' && plan.password.empty() &&
            plan.player_name.empty() && block.faction == 0 && block.team == 0 &&
            block.max_units == 0,
        "registeredonly and the unnamed fields add nothing"
    );
}

/// The YAML example fills the plan, including DirectPlay, content, seat and status.
void check_yaml() {
    const auto plan = oa::app::netgame::read_launch_text("launch.yaml", kYaml, true);
    const auto& block = plan.block;
    expect(!plan.ini && plan.role == oa::app::netgame::LaunchRole::host, "the example hosts");
    expect(plan.source == "launch.yaml", "the refusal names the file it was given");
    expect(block.hosting == 1, "a host creates the game");
    expect(block.connection_type == mp::launch::connection_type::tcpip, "a host plays over TCP/IP");
    expect(plan.address == "192.0.2.10" && same_text(block.address, "192.0.2.10"), "address");
    expect(
        plan.game_name == "Example game" && same_text(block.session_name, "Example game"),
        "game name"
    );
    expect(plan.player_name == "Commander" && same_text(block.user_name, "Commander"), "player");
    expect(plan.password == "secret" && same_text(block.password, "secret"), "password is stored");
    std::array<uint8_t, 16> guid{};
    expect(
        oa::netgame::parse_application_guid(
            "{99797420-F5F5-11CF-9827-00A0241496C8}", guid.data()
        ) && plan.dplay_guid &&
            *plan.dplay_guid == guid,
        "dplay guid"
    );
    expect(plan.dplay_port && *plan.dplay_port == 47624, "dplay port");
    expect(
        plan.content.named && plan.content.base_game == "totala" &&
            plan.content.expansions.size() == 2 && plan.content.expansions[0] == "campaign" &&
            plan.content.expansions[1] == "core-contingency" && plan.content.mod == "ridge",
        "content"
    );
    expect(same_text(block.mission, "Acid Foursome"), "map");
    expect(block.death_option == 2, "commander ends");
    expect(block.metal == 1000 && block.energy == 1000, "resources");
    expect(block.max_units == 250, "unit limit");
    expect(block.los == 1, "true line of sight");
    expect(block.mapping == 2 && block.location == 2, "mapped, random starts");
    expect(block.cheats == 1 && block.watching == 2, "cheats off, watchers on");
    expect(block.player_limit == 4 && block.time_limit == 0, "player limit and time limit");
    expect(block.lock_options == 0, "options stay open");
    expect(block.faction == mp::launch::faction::arm && block.team == 1, "Arm on team 1");
    expect(
        plan.status.stdout_named && plan.status.standard_output &&
            plan.status.file == "status.jsonl" &&
            plan.status.quit_after == oa::app::netgame::QuitAfter::game,
        "status is kept"
    );
}

/// Each refusal names its field, and none of them repeats the password.
void check_refusals() {
    const std::string unknown = refused_yaml("oalaunch: 1\nrules:\n  nope: 1\n");
    expect(
        unknown.find("rules.nope") != std::string::npos &&
            unknown.find("is not a launch setting") != std::string::npos,
        "an unknown key names its field"
    );
    const std::string metal =
        refused_yaml("oalaunch: 1\ngame:\n  password: secret\nrules:\n  metal: 250000\n");
    expect(
        metal.find("rules.metal") != std::string::npos &&
            metal.find("above 10000") != std::string::npos &&
            metal.find("secret") == std::string::npos,
        "metal above the slider names the field and not the password"
    );
    const std::string typed = refused_yaml("oalaunch: 1\nrules:\n  metal: lots\n");
    expect(
        typed.find("rules.metal") != std::string::npos &&
            typed.find("expected a whole number") != std::string::npos,
        "a wrong type names the field"
    );
    const std::string header = refused_yaml("oalaunch: 2\n");
    expect(
        header.find("oalaunch") != std::string::npos &&
            header.find("expected 1") != std::string::npos,
        "oalaunch must be 1"
    );
    const std::string players = refused_yaml("oalaunch: 1\nrules:\n  player-limit: 0\n");
    expect(
        players.find("rules.player-limit") != std::string::npos &&
            players.find("is below 2") != std::string::npos,
        "a player limit of 0 names the field"
    );
    constexpr std::string_view password = "G52-password-that-must-stay-out-XX";
    const std::string long_password =
        refused_yaml("oalaunch: 1\ngame:\n  password: G52-password-that-must-stay-out-XX\n");
    expect(
        long_password.find("game.password") != std::string::npos &&
            long_password.find("is longer than 31") != std::string::npos &&
            long_password.find(password) == std::string::npos,
        "a long password is refused without being repeated"
    );
}

struct Parsed {
    oa::app::NetOptions net;
    oa::app::Options app;
    oa::app::netgame::launch::LaunchSwitches launch;
};

/// Parses one command line through the game's option hooks.
///
/// @param arguments the arguments after the program name
/// @return the options and the launch switches
Parsed parse(const std::vector<const char*>& arguments) {
    std::vector<std::string> storage{"open-annihilation"};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : storage)
        argv.push_back(argument.data());
    oa::app::NetgameContext context{};
    oa::app::Extension table{};
    oa::app::fill_option_hooks(table, context);
    auto app = oa::app::parse_options(static_cast<int>(argv.size()), argv.data(), table);
    return {context.options, std::move(app), context.launch};
}

/// The message a command line is refused with, or empty when it is accepted.
///
/// @param arguments the arguments after the program name
/// @return the refusal
std::string rejection(const std::vector<const char*>& arguments) {
    try {
        (void)parse(arguments);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

/// Options beside a file replace that field, in either order.
///
/// @param scratch the directory the files are written in
void check_command_line(const Scratch& scratch) {
    const std::string yaml = oa::app::path_to_utf8(scratch.write("game.yaml", kNamedYaml));
    const std::string ini = oa::app::path_to_utf8(scratch.write("totala.ini", kIni));
    const std::string bad = oa::app::path_to_utf8(scratch.write("bad.yaml", R"(oalaunch: 1
game:
  role: host
  password: secret
rules:
  metal: 250000
)"));

    const auto name_first = parse({"--game-name", "Override", "--launch", yaml.c_str()});
    const auto file_first = parse({"--launch", yaml.c_str(), "--game-name", "Override"});
    expect(
        same_text(name_first.launch.block.session_name, "Override") &&
            name_first.net.direct_game.game_name == "Override" &&
            same_text(file_first.launch.block.session_name, "Override") &&
            file_first.net.direct_game.game_name == "Override",
        "--game-name wins in either order"
    );
    expect(
        name_first.app.skip_intro && name_first.net.launch_from_file, "--launch skips the intro"
    );

    const auto join_first = parse({"--join", "192.0.2.10", "-c", ini.c_str()});
    const auto file_join = parse({"-c", ini.c_str(), "--join", "192.0.2.10"});
    const auto joined = [&](const Parsed& taken) {
        return taken.net.direct_game.kind == mp::DirectGame::Kind::join &&
               taken.launch.block.hosting == 0 && taken.net.direct_game.address == "192.0.2.10" &&
               same_text(taken.launch.block.address, "192.0.2.10");
    };
    expect(joined(join_first) && joined(file_join), "--join overrides -c");

    const std::string refused = rejection({"--launch", bad.c_str()});
    expect(
        refused.find("rules.metal") != std::string::npos &&
            refused.find("above 10000") != std::string::npos &&
            refused.find("secret") == std::string::npos,
        "a bad file names the field and not the password"
    );
    expect(
        rejection({"--list-content"}).find("--list-content needs --json") != std::string::npos,
        "--list-content needs --json"
    );
    expect(
        rejection({"--json"}).find("--json needs --list-content") != std::string::npos,
        "--json needs --list-content"
    );
    const auto listed = parse({"--list-content", "--json"});
    expect(
        listed.net.list_content && listed.net.json_requested && listed.app.skip_intro,
        "--list-content --json lists content and skips the intro"
    );
}

} // namespace

int main() {
    const Scratch scratch;
    check_ini(scratch.write("taforever.ini", kIni));
    check_yaml();
    check_refusals();
    check_command_line(scratch);
    if (failures != 0)
        std::fprintf(stderr, "%d launch file checks failed\n", failures);
    return failures == 0 ? 0 : 1;
}
