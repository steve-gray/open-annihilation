// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The table of launch settings is what --help and the manual say. Every
// option the table names is in --help, and network play's with the value it
// takes; docs/command-line.md's table of launch settings and
// docs/multiplayer/launch-file.md's table of TA Forever's keys are the
// table's rows, word for word. With --print the test prints both tables as
// the manual should hold them.
#include "net_options.hpp"
#include "oa/app/app.hpp"
#include "oa/app/netgame/launch_file.hpp"
#include "options.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace launch_file = oa::app::netgame;

int failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
        ++failures;
    }
}

/// Writes text into a Markdown table's code span, its pipes escaped.
///
/// @param text the text
/// @return the escaped text
std::string escaped(std::string_view text) {
    std::string out;
    for (const char character : text) {
        if (character == '|')
            out += "\\|";
        else
            out += character;
    }
    return out;
}

/// Returns the manual's table of launch settings, made from the table.
///
/// @return the Markdown table
std::string settings_table() {
    std::string table = "| Field | Option | What it sets |\n|---|---|---|\n";
    for (const auto& field : launch_file::launch_fields())
        table += "| `" + std::string(field.field) + "` | `" + escaped(field.option) + "` | " +
                 std::string(field.meaning) + " |\n";
    return table;
}

/// Returns the manual's table of TA Forever's keys, made from the table.
///
/// @return the Markdown table
std::string ini_table() {
    std::string table = "| Key | Field | What the battle room uses |\n|---|---|---|\n";
    for (const auto& key : launch_file::launch_ini_keys())
        table += "| `" + std::string(key.key) + "` | " +
                 (key.field.empty() ? std::string("none") : "`" + std::string(key.field) + "`") +
                 " | " + std::string(key.meaning) + " |\n";
    return table;
}

/// Reads a whole file.
///
/// @param path the file
/// @return its text; empty when it cannot be read
std::string read_text(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::string text;
    if (input)
        text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return text;
}

/// Checks that a manual page holds a table word for word, and says what it should hold when not.
///
/// @param path the page
/// @param table the table
/// @param what the table's name
void check_page(const char* path, const std::string& table, const char* what) {
    const std::string page = read_text(path);
    expect(!page.empty(), std::string(path) + " is read");
    if (page.find(table) != std::string::npos)
        return;
    expect(false, std::string(path) + " holds the " + what + " as the code's table says");
    std::fprintf(stderr, "The %s should read:\n%s\n", what, table.c_str());
}

/// Returns the names a text writes as options: each word that starts with --.
///
/// @param text the text
/// @return the names
std::vector<std::string> option_names(std::string_view text) {
    const auto in_name = [](char letter) {
        return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') ||
               letter == '-';
    };
    std::vector<std::string> names;
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (!text.substr(at).starts_with("--") || (at > 0 && in_name(text[at - 1])))
            continue;
        std::size_t end = at + 2;
        while (end < text.size() && in_name(text[end]))
            ++end;
        names.emplace_back(text.substr(at, end - at));
        at = end;
    }
    return names;
}

/// --help's text, captured as the program ends.
///
/// @return the stream
std::ostringstream& help_text() {
    static std::ostringstream text;
    return text;
}

/// Checks the captured --help as the program ends, and ends it with the status of every check.
void check_help_at_exit() {
    const std::string text = help_text().str();
    const std::vector<std::string> listed = option_names(text);
    const auto names = [&listed](const std::string& name) {
        for (const std::string& option : listed)
            if (option == name)
                return true;
        return false;
    };
    for (const auto& field : launch_file::launch_fields()) {
        for (const std::string& name : option_names(field.option))
            expect(
                names(name), name + ", the option of " + std::string(field.field) + ", is in --help"
            );
        if (field.reader == launch_file::LaunchOptionReader::network_play ||
            field.reader == launch_file::LaunchOptionReader::direct_game)
            expect(
                text.find(std::string(field.option)) != std::string::npos,
                "--help shows " + std::string(field.option)
            );
    }
    expect(text.find("[--launch FILE|-]") != std::string::npos, "--help shows --launch FILE|-");
    expect(text.find("-c FILE|-") != std::string::npos, "--help shows -c FILE|-");
    std::cout.rdbuf(nullptr);
    if (failures != 0)
        std::fprintf(stderr, "%d launch help checks failed\n", failures);
    std::_Exit(failures == 0 ? 0 : 1);
}

/// The two option tables --help reads: network play's and the status stream's.
struct Tables {
    oa::app::NetgameContext netgame{};
    oa::app::automation::EndpointOptions endpoint{};
    oa::app::Extension network_play{};
    oa::app::Extension status_stream{};
    std::string joined{};
};

/// Joins the two tables' texts as the game's extension list does.
///
/// @param context the Tables
/// @param which the text asked for
/// @return the text; the status stream's note, which comes later in the list, for the note
const char* joined_text(void* context, oa::app::ExtensionText which) {
    auto& tables = *static_cast<Tables*>(context);
    const auto part = [which](const oa::app::Extension& table) -> std::string {
        const char* text = table.text(table.context, which);
        return text != nullptr ? text : "";
    };
    if (which == oa::app::ExtensionText::usage_note ||
        which == oa::app::ExtensionText::register_switch) {
        const char* later = tables.status_stream.text(tables.status_stream.context, which);
        return later != nullptr ? later
                                : tables.network_play.text(tables.network_play.context, which);
    }
    tables.joined = part(tables.network_play) + part(tables.status_stream);
    return tables.joined.c_str();
}

/// Writes --help into help_text and ends the program there, which check_help_at_exit reads.
[[noreturn]] void check_help() {
    static Tables tables;
    oa::app::fill_option_hooks(tables.network_play, tables.netgame);
    oa::app::automation::fill_option_hooks(tables.status_stream, tables.endpoint);
    oa::app::Extension table{};
    table.context = &tables;
    table.text = joined_text;
    std::ignore = help_text();
    std::cout.rdbuf(help_text().rdbuf());
    if (std::atexit(check_help_at_exit) != 0) {
        std::fputs("FAILED: the --help check could not be registered\n", stderr);
        std::_Exit(1);
    }
    std::string program = "open-annihilation";
    std::string help = "--help";
    char* argv[] = {program.data(), help.data()};
    std::ignore = oa::app::parse_options(2, argv, table);
    std::fputs("FAILED: --help did not end the program\n", stderr);
    std::_Exit(1);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--print") {
        std::cout << settings_table() << '\n' << ini_table();
        return 0;
    }
    if (argc != 3) {
        std::fputs(
            "usage: oa-netgame-launch-help-test COMMAND-LINE.md LAUNCH-FILE.md | --print\n", stderr
        );
        return 2;
    }
    check_page(argv[1], settings_table(), "table of launch settings");
    check_page(argv[2], ini_table(), "table of TA Forever's keys");
    // The manual's example is a launch the game takes.
    const std::string page = read_text(argv[2]);
    const auto start = page.find("```yaml\n");
    const auto end = start == std::string::npos ? start : page.find("```\n", start + 8);
    expect(end != std::string::npos, "the launch file page has an example");
    if (end != std::string::npos) {
        try {
            const auto plan = launch_file::read_launch_text(
                "example", std::string_view(page).substr(start + 8, end - start - 8)
            );
            launch_file::check_launch_plan(plan);
            expect(plan.block.computer_count == 2, "the example seats two computer players");
        } catch (const launch_file::LaunchRefusal& refusal) {
            expect(false, std::string("the manual's example is refused: ") + refusal.what());
        }
    }
    // The options network play shows name each launch setting it reads.
    oa::app::NetgameContext context{};
    oa::app::Extension table{};
    oa::app::fill_option_hooks(table, context);
    const std::string runs = table.text(table.context, oa::app::ExtensionText::usage_runs);
    for (const auto& field : launch_file::launch_fields())
        if (field.reader == launch_file::LaunchOptionReader::network_play)
            expect(
                runs.find("[" + std::string(field.option) + "] ") != std::string::npos,
                "network play's usage shows [" + std::string(field.option) + "]"
            );
    check_help();
}
