// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The network play switches parsed through oa::app::command_line::parse with the
// handler launch_switch_handler returns: each 3.1c switch of network play, the
// battle room words -b names, what -c clears, the notes for the log, and an
// argument with a space in it taken whole.
#include "oa/app/netgame/launch_switches.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace cl = oa::app::command_line;
namespace nl = oa::app::netgame::launch;

namespace {
int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "launch switches test failed: %s\n", message);
        ++failures;
    }
}

struct Parsed {
    cl::Switches engine;
    nl::LaunchSwitches launch;
};

cl::Status parse(const char* line, Parsed& parsed) {
    const cl::SwitchHandler handler = nl::launch_switch_handler(&parsed.launch);
    return cl::parse(line, parsed.engine, &handler);
}

Parsed parsed(const char* line, cl::Status expected = cl::Status::run) {
    Parsed result;
    require(parse(line, result) == expected, line);
    return result;
}

/// Parses the arguments a program was started with, each one word.
///
/// @param words the arguments
/// @return the engine's switches and the launch switches
Parsed parsed_words(const std::vector<const char*>& words) {
    Parsed result;
    const cl::SwitchHandler handler = nl::launch_switch_handler(&result.launch);
    require(cl::parse(words, result.engine, &handler) == cl::Status::run, "words run");
    return result;
}

void defaults_and_resets() {
    const auto none = parsed("");
    require(
        none.launch.net_timeout_seconds == 30 && none.launch.send_error_percent == 0, "defaults"
    );
    Parsed kept;
    kept.launch.net_timeout_seconds = 200;
    kept.launch.send_error_percent = 50;
    kept.launch.send_pacing = 9;
    kept.engine.skip_intro = 1;
    require(parse("english", kept) == cl::Status::run, "bare language");
    require(
        kept.launch.net_timeout_seconds == 30 && kept.launch.send_error_percent == 0 &&
            kept.engine.skip_intro == 0,
        "three fields reset before the scan"
    );
    require(kept.launch.send_pacing == 9, "other fields keep their values");
}

void timeouts_and_percentages() {
    require(parsed("-t120").launch.net_timeout_seconds == 120, "attached timeout");
    require(parsed("-T 300").launch.net_timeout_seconds == 300, "separate timeout, upper case");
    require(parsed("-t29").launch.net_timeout_seconds == 30, "below the range falls back to 30");
    require(parsed("-t301").launch.net_timeout_seconds == 30, "above the range falls back to 30");
    require(parsed("-t -e5").launch.net_timeout_seconds == 30, "dash argument falls back to 30");
    const auto consumed = parsed("-t -e5");
    require(consumed.launch.send_error_percent == 0, "the dash token is consumed as the argument");
    require(parsed("/e100").launch.send_error_percent == 100, "slash switch, full percentage");
    require(parsed("-e101").launch.send_error_percent == 0, "percentage above 100 is zero");
    require(parsed("-e 42x").launch.send_error_percent == 42, "decimal prefix");
}

void intro_and_connection() {
    const auto join = parsed("-n1:10.0.0.5");
    require(
        join.launch.block.connection_type == 1 && join.engine.skip_intro == 1 &&
            join.launch.connection_type_from_switch && join.launch.address_from_switch,
        "typed connection skips intro"
    );
    require(std::strcmp(join.launch.block.address, "10.0.0.5") == 0, "address after the colon");
    const auto ipx = parsed("-n 2:ignored");
    require(
        ipx.launch.block.connection_type == 2 && ipx.launch.block.address[0] == '\0' &&
            ipx.launch.connection_type_from_switch && !ipx.launch.address_from_switch,
        "only type 1 stores an address"
    );
    const auto bad = parsed("-n5");
    require(
        bad.launch.block.connection_type == 0 && bad.engine.skip_intro == 1 &&
            !bad.launch.connection_type_from_switch && !bad.launch.address_from_switch,
        "type outside 1..4 is ignored"
    );
    const auto dash = parsed("-n -x");
    require(
        dash.launch.block.connection_type == 0 && dash.engine.skip_intro == 1,
        "dash argument still skips"
    );
    // "-c" names a launch file of either kind and skips the intro; "-c -"
    // names standard input.
    const auto module = parsed("-c module.cfg");
    require(
        module.launch.launch_seen && module.launch.launch_path == "module.cfg" &&
            module.engine.skip_intro == 1,
        "-c takes a file and skips the intro"
    );
    const auto attached_file = parsed("-cmodule.cfg");
    require(attached_file.launch.launch_path == "module.cfg", "an attached -c file");
    const auto piped = parsed("-c -");
    require(
        piped.launch.launch_seen && piped.launch.launch_path == "-" && piped.engine.skip_intro == 1,
        "-c - names standard input"
    );
    // The next token is the file whatever it starts with, as in 3.1c.
    const auto dashed = parsed("-c -t60");
    require(
        dashed.launch.launch_seen && dashed.launch.launch_path == "-t60" &&
            dashed.launch.net_timeout_seconds == 30 && dashed.engine.skip_intro == 1,
        "-c takes a next token that starts with a dash as its file"
    );
    const auto upper = parsed("/C game.ini");
    require(upper.launch.launch_path == "game.ini", "/C is -c");
    const auto alone = parsed("-c");
    require(
        alone.launch.launch_seen && alone.launch.launch_path.empty() &&
            alone.engine.skip_intro == 1 && alone.launch.notes.empty(),
        "-c alone names no file and still skips the intro"
    );
    // "-y" opens 3.1c's built-in internet lobby, which this game does not
    // have: it is taken, skips the intro and is noted, and asks for no setup.
    const auto setup = parsed("-Y english");
    require(
        setup.engine.skip_intro == 1 && setup.launch.netsetup == 0 &&
            std::strcmp(setup.engine.language, "english") == 0,
        "-y skips the intro and takes no argument"
    );
    require(
        setup.launch.notes.size() == 1 && setup.launch.notes[0].starts_with("-y opens"),
        "-y is noted"
    );
}

/// "-c" clears the launch record: what -h, -n and -b set before it is lost
/// and noted, its copies stay, and the switches after it count.
void launch_clears_the_record() {
    const auto before = parsed("-n1:10.0.0.5 -hEarly -b deathends -c game.ini");
    const auto& block = before.launch.block;
    require(
        block.connection_type == 0 && block.hosting == 0 && block.address[0] == '\0' &&
            block.session_name[0] == '\0',
        "-c clears the block"
    );
    require(
        !before.launch.session_from_switch && !before.launch.connection_type_from_switch &&
            !before.launch.address_from_switch && !before.launch.battle.named(),
        "nothing before -c replaces the file"
    );
    require(
        before.launch.connection_type_copy == 1 &&
            std::strcmp(before.launch.host_game_name_copy, "Early") == 0 &&
            std::strcmp(before.launch.connection_address_copy, "10.0.0.5") == 0,
        "the second copy survives -c"
    );
    require(
        before.launch.notes.size() == 1 &&
            before.launch.notes[0] ==
                "-c clears the launch, as in 3.1c: what -h, -n, -b set before it is lost",
        "what -c cleared is noted"
    );
    const auto after = parsed("-c game.ini -n1:10.0.0.6 -hLater /BtrueLOS");
    require(
        after.launch.block.connection_type == 1 && after.launch.block.hosting == 1 &&
            std::strcmp(after.launch.block.address, "10.0.0.6") == 0 &&
            std::strcmp(after.launch.block.session_name, "Later") == 0 &&
            after.launch.session_from_switch && after.launch.connection_type_from_switch &&
            after.launch.address_from_switch && after.launch.battle.los == 1 &&
            after.launch.notes.empty(),
        "the switches after -c are kept to replace the file's"
    );
    const auto twice = parsed("-c first.ini -hMiddle -c second.ini");
    require(
        twice.launch.launch_path == "second.ini" && !twice.launch.session_from_switch &&
            twice.launch.notes.size() == 1,
        "the last -c names the file, and clears what came between"
    );
}

/// "-b WORD" sets the battle room option the word names, in any case,
/// attached or as the next token; a word that names none is noted.
void battle_words() {
    struct Case {
        const char* line;
        int32_t nl::BattleWords::* field;
        int32_t code;
    };

    const Case cases[] = {
        {"-bdeathplays", &nl::BattleWords::death_option, 1},
        {"-b deathends", &nl::BattleWords::death_option, 2},
        {"-b DEATHMATCH", &nl::BattleWords::death_option, 3},
        {"/btruelos", &nl::BattleWords::los, 1},
        {"-B circlos", &nl::BattleWords::los, 2},
        {"-b permlos", &nl::BattleWords::los, 3},
        {"-b mapping", &nl::BattleWords::mapping, 2},
        {"-b fixedloc", &nl::BattleWords::location, 1},
        {"-b cheating", &nl::BattleWords::cheats, 2},
        {"-bWatching", &nl::BattleWords::watching, 2},
    };
    for (const Case& entry : cases) {
        const auto taken = parsed(entry.line);
        require(taken.launch.battle.*entry.field == entry.code, entry.line);
        require(taken.launch.notes.empty() && taken.engine.language[0] == '\0', entry.line);
    }
    const auto lock = parsed("-b lock");
    require(
        lock.launch.battle.lock == 1 && lock.engine.language[0] == '\0' &&
            lock.engine.game_options == 0,
        "-b lock locks the launch's options, and lock is not a language"
    );
    const auto later = parsed("-b deathends -b deathmatch");
    require(later.launch.battle.death_option == 3, "a later word replaces an earlier one");
    const auto unknown = parsed("-b sometimes");
    require(
        !unknown.launch.battle.named() && unknown.launch.notes.size() == 1 &&
            unknown.launch.notes[0] ==
                "-b sometimes names no battle room option, so it does nothing",
        "a word that names no option is noted"
    );
    const auto bare = parsed("-b");
    require(
        !bare.launch.battle.named() && bare.launch.notes.size() == 1, "-b with no word is noted"
    );
    nl::BattleWords words;
    require(nl::take_battle_word(words, "PermLos") && words.los == 3, "take_battle_word");
    require(!nl::take_battle_word(words, "permanent") && words.los == 3, "an unknown word");
    nl::LaunchBlock block{};
    block.death_option = 1;
    block.los = 1;
    words.lock = 1;
    nl::apply_battle_words(words, block);
    require(
        block.los == 3 && block.lock_options == 1 && block.death_option == 1,
        "the words replace the fields they name and leave the rest"
    );
}

void hosting() {
    const auto attached = parsed("-hMy Game");
    require(
        attached.launch.block.hosting == 1 &&
            std::strcmp(attached.launch.block.session_name, "My") == 0 &&
            attached.launch.session_from_switch,
        "attached name ends at the blank"
    );
    require(
        std::strcmp(attached.engine.language, "Game") == 0, "the next token is a bare argument"
    );
    const auto separate = parsed("-h Lobby");
    require(
        separate.launch.block.hosting == 1 &&
            std::strcmp(separate.launch.block.session_name, "Lobby") == 0,
        "separate name"
    );
    const auto dash = parsed("-h -s");
    require(
        dash.launch.block.hosting == 0 && dash.engine.playback_suppressed == 0,
        "a dash token is consumed and not a name"
    );
    std::string long_name = "-h";
    long_name.append(80, 'x');
    const auto truncated = parsed(long_name.c_str());
    require(
        std::strlen(truncated.launch.block.session_name) == 63, "name truncated to 63 characters"
    );
}

/// Each argument is one word: -c takes a path with a space in it whole,
/// however it is written, and -h a game name with a space in it.
void spaced_arguments() {
    const char* path = "/Users/me/Launch Files/host.oalaunch";
    for (const char* letter : {"-c", "-C", "/c", "/C"}) {
        const std::string attached = std::string(letter) + path;
        const auto apart = parsed_words({letter, path, "german"});
        const auto joined = parsed_words({attached.c_str()});
        require(
            apart.launch.launch_seen && apart.launch.launch_path == path &&
                std::strcmp(apart.engine.language, "german") == 0 && apart.engine.skip_intro == 1,
            "-c takes a spaced path apart, and the next word is the language"
        );
        require(
            joined.launch.launch_path == path && joined.engine.language[0] == '\0',
            "-c takes a spaced path attached"
        );
    }
    const auto named = parsed_words({"-c", path, "-h", "Friday game", "-n", "1:10.0.0.5"});
    require(
        named.launch.launch_path == path && named.launch.block.hosting == 1 &&
            std::strcmp(named.launch.block.session_name, "Friday game") == 0 &&
            named.launch.block.connection_type == 1 && named.engine.language[0] == '\0',
        "-h after -c names a game with a space in it"
    );
    const auto attached_name = parsed_words({"-hMy Game"});
    require(
        std::strcmp(attached_name.launch.block.session_name, "My Game") == 0 &&
            attached_name.engine.language[0] == '\0',
        "an attached name keeps its space when it is one argument"
    );
}

void pacing_and_debug_switches() {
    const auto pacing = parsed("-p12");
    require(pacing.launch.send_pacing_set == 1 && pacing.launch.send_pacing == 12, "pacing value");
    const auto dash = parsed("-p -z");
    require(
        dash.launch.send_pacing_set == 1 && dash.launch.send_pacing == nl::kPacingFromDash,
        "dash pacing"
    );
    require(parsed("-p").launch.send_pacing_set == 0, "no pacing argument");
    const auto debug = parsed("-saveresources -dprinton -fpufussy -performancestatus");
    require(
        debug.launch.send_pacing_set == 0 && debug.engine.developer_switches == 4,
        "-performancestatus is not -p"
    );
    const auto others = parsed("-s -w -x -z");
    require(
        others.engine.system_sound == 1 && others.engine.skip_intro == 0 &&
            others.launch.notes.empty(),
        "letters the handler does not know are left alone"
    );
}

void registration_stops_the_scan() {
    Parsed switches;
    require(
        parse("-s -r   C:\\TA\\Game.exe -t60", switches) == cl::Status::register_application,
        "-r stops the scan"
    );
    require(
        switches.engine.playback_suppressed == 1 && switches.launch.net_timeout_seconds == 30,
        "earlier switches applied, later ones not"
    );
}

void helpers() {
    nl::LaunchSwitches switches;
    nl::set_connection_type(switches, 4);
    nl::set_connection_type(switches, 0);
    require(switches.block.connection_type == 4, "type 0 ignored");
    nl::set_host_game(switches, 0, nullptr);
    require(switches.block.hosting == 0 && switches.block.session_name[0] == '\0', "no name");
    nl::set_host_game(switches, 7, "Name");
    require(switches.block.hosting == 1, "hosting flag is boolean");
    require(std::strcmp(switches.host_game_name_copy, "Name") == 0, "name copied");
    nl::set_host_game(switches, 1, "Two");
    require(
        std::strcmp(switches.block.session_name, "Two") == 0 &&
            std::strcmp(switches.host_game_name_copy, "NameTwo") == 0,
        "the copy accumulates names"
    );
    require(switches.connection_type_copy == 4, "type written to the copy");
    nl::set_connection_address(switches, "a");
    nl::set_connection_address(switches, "b");
    require(std::strcmp(switches.block.address, "b") == 0, "address replaced");
    require(std::strcmp(switches.connection_address_copy, "ab") == 0, "address appended");
    const std::string long_text(80, 'x');
    nl::set_connection_address(switches, long_text.c_str());
    require(
        std::strlen(switches.block.address) == 63 &&
            std::strlen(switches.connection_address_copy) == 63,
        "both address fields stop at 63 characters"
    );
    Parsed setup;
    nl::enable_netsetup(setup.launch);
    require(
        setup.launch.netsetup == 1 && setup.engine.skip_intro == 0, "netsetup byte alone is set"
    );
}

void switches_fill_the_block() {
    const auto host = parsed("-n1:10.0.0.9 -hBattle");
    require(
        host.launch.block.connection_type == nl::connection_type::tcpip &&
            host.launch.block.hosting == 1 &&
            std::strcmp(host.launch.block.session_name, "Battle") == 0 &&
            std::strcmp(host.launch.block.address, "10.0.0.9") == 0,
        "-n and -h fill the block's connection fields"
    );
}

} // namespace

int main() {
    defaults_and_resets();
    timeouts_and_percentages();
    intro_and_connection();
    hosting();
    launch_clears_the_record();
    spaced_arguments();
    battle_words();
    pacing_and_debug_switches();
    registration_stops_the_scan();
    helpers();
    switches_fill_the_block();
    if (failures != 0)
        return 1;
    std::puts("launch switches passed");
    return 0;
}
