// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "command.hpp"

#include <exception>
#include <ostream>
#include <string>

namespace oa::tool {
namespace {

const Command command_table[] = {
    {
        "list",
        "ARCHIVE",
        "List the files in an archive.",
        "Reads an HPI archive and writes nothing. Prints one line per file, in the archive's "
        "order: the path inside the archive, a tab and the file's size in bytes.",
        1,
        1,
        run_list,
        {},
    },
    {
        "extract",
        "ARCHIVE ENTRY OUTPUT",
        "Copy one file out of an archive.",
        "Reads one entry from an HPI archive and writes its bytes to OUTPUT. Prints nothing.",
        3,
        3,
        run_extract,
        {},
    },
    {
        "asset-extract",
        "ROOT ENTRY OUTPUT [ARCHIVE...]",
        "Copy one file from a game directory.",
        "Reads ENTRY from the loose files under ROOT, and from the archives named after OUTPUT "
        "when no loose file has that path. A loose file wins over an archive. Writes the bytes "
        "to OUTPUT. Prints whether the file was loose or in an archive, the path it was read "
        "from, and its size in bytes.",
        3,
        any_count,
        run_asset_extract,
        {},
    },
    {
        "preview",
        "ARCHIVE PCX_ENTRY OUTPUT.ppm|OUTPUT.png",
        "Decode one image from an archive.",
        "Reads a PCX image from an HPI archive and writes it as a PPM file, or as a PNG file when "
        "the output path ends in .png. Prints the image's width and height and the output path.",
        3,
        3,
        run_preview,
        {},
    },
    {
        "decode-pcx",
        "INPUT.pcx OUTPUT.ppm|OUTPUT.png",
        "Decode a PCX image file.",
        "Reads a PCX file and writes it as a PPM file, or as a PNG file when the output path ends "
        "in .png. Prints the image's width and height and the output path.",
        2,
        2,
        run_decode_pcx,
        {},
    },
    {
        "pack",
        "FOLDER [--out FILE] [--force] [--game-dir DIR]",
        "Pack a folder as a mod, a language package or a map pack.",
        "Reads FOLDER and writes a .oamod, a .oalang or a .oamap. The folder's top holds one "
        "manifest, oamod.yaml, language.yaml or oamap.yaml. The manifest is written first, then "
        "every other file in byte order of its path, with fixed times. A file whose extension, "
        "without case, is png, jpg, jpeg, gif, ogg, mp3, zip, gz, bz2, xz, 7z, oamod, oalang or "
        "oamap, and an empty file, is stored; every other file is deflated. The same files give "
        "the same bytes on any machine with the same zlib. --out names the package file; without "
        "it the name comes from the manifest and is written in the current folder. --force "
        "replaces an existing file. A folder the installer would refuse is not packed. A map "
        "pack reads the game's palette from --game-dir: map packs need the game's palette for "
        "previews. --game-dir on a mod or a language pack is a usage error.",
        1,
        any_count,
        run_pack,
        {},
    },
};

/// Returns the command named `name`, or null when the table has none.
const Command* find_command(std::span<const Command> table, std::string_view name) {
    for (const Command& command : table)
        if (command.name == name)
            return &command;
    return nullptr;
}

/// Joins a command path and the next word with a space.
std::string join_path(std::string_view path, std::string_view name) {
    if (path.empty())
        return std::string(name);
    std::string joined;
    joined.reserve(path.size() + 1 + name.size());
    joined.append(path);
    joined.push_back(' ');
    joined.append(name);
    return joined;
}

/// Prints one command's usage line and its one-line summary.
void print_entry(std::ostream& out, std::string_view path, const Command& command) {
    out << "oa-tool " << path;
    if (!command.arguments.empty())
        out << ' ' << command.arguments;
    out << '\n';
    if (!command.summary.empty())
        out << "    " << command.summary << '\n';
}

/// Prints a table, and each group's subcommands under it.
void print_list(std::ostream& out, std::span<const Command> table, std::string_view prefix) {
    for (const Command& command : table) {
        const std::string path = join_path(prefix, command.name);
        print_entry(out, path, command);
        for (const Command& sub : command.subcommands)
            print_entry(out, join_path(path, sub.name), sub);
    }
}

/// Prints one command's usage line, its paragraph and, for a group, its subcommands.
void print_command_help(std::ostream& out, std::string_view path, const Command& command) {
    out << "oa-tool " << path;
    if (!command.arguments.empty())
        out << ' ' << command.arguments;
    out << "\n\n" << command.help << '\n';
    if (!command.subcommands.empty()) {
        out << '\n';
        for (const Command& sub : command.subcommands)
            print_entry(out, join_path(path, sub.name), sub);
    }
}

/// Prints a usage failure and the help command that explains it.
void report_usage(Output& output, std::string_view path, std::string_view problem) {
    output.err << "oa-tool " << path << ": " << problem << '\n';
    output.err << "run 'oa-tool help " << path << "'\n";
}

/// Describes an argument count outside the command's least and most.
std::string count_problem(const Command& command, std::size_t count) {
    std::string problem = "expected ";
    if (command.least == command.most) {
        problem += std::to_string(command.least);
        problem += command.least == 1 ? " argument" : " arguments";
    } else if (command.most == any_count) {
        problem += "at least ";
        problem += std::to_string(command.least);
        problem += command.least == 1 ? " argument" : " arguments";
    } else {
        problem += std::to_string(command.least);
        problem += " to ";
        problem += std::to_string(command.most);
        problem += " arguments";
    }
    problem += ", got ";
    problem += std::to_string(count);
    return problem;
}

/// Reports whether --help or -h appears before a --.
bool asks_for_help(std::span<const std::string> arguments) {
    for (const std::string& argument : arguments) {
        if (argument == "--")
            return false;
        if (argument == "--help" || argument == "-h")
            return true;
    }
    return false;
}

/// Reports whether a row is a group of subcommands rather than a command that runs.
bool is_group(const Command& command) {
    return command.run == nullptr || !command.subcommands.empty();
}

/// Runs one command, or one of a group's subcommands, after help and the argument count.
int invoke(
    const Command& command,
    const std::string& path,
    std::span<const std::string> arguments,
    Output& output
) {
    if (is_group(command)) {
        if (!arguments.empty() && (arguments[0] == "--help" || arguments[0] == "-h")) {
            print_command_help(output.out, path, command);
            return exit_done;
        }
        if (arguments.empty()) {
            print_entry(output.err, path, command);
            for (const Command& sub : command.subcommands)
                print_entry(output.err, join_path(path, sub.name), sub);
            return exit_usage;
        }
        const Command* sub = find_command(command.subcommands, arguments[0]);
        if (sub == nullptr) {
            report_usage(output, join_path(path, arguments[0]), "unknown command");
            return exit_usage;
        }
        return invoke(*sub, join_path(path, sub->name), arguments.subspan(1), output);
    }
    if (asks_for_help(arguments)) {
        print_command_help(output.out, path, command);
        return exit_done;
    }
    if (arguments.size() < command.least || arguments.size() > command.most) {
        report_usage(output, path, count_problem(command, arguments.size()));
        return exit_usage;
    }
    try {
        return command.run(arguments, output);
    } catch (const std::exception& error) {
        output.err << "oa-tool " << path << ": " << error.what() << '\n';
        return exit_failed;
    }
}

/// Selects a command from a table, or prints help or a usage failure.
int dispatch(
    std::span<const Command> table, std::span<const std::string> arguments, Output& output
) {
    if (arguments.empty()) {
        print_list(output.err, table, {});
        return exit_usage;
    }
    const std::string& first = arguments[0];
    if (first == "--help" || first == "-h") {
        print_list(output.out, table, {});
        return exit_done;
    }
    if (first == "help") {
        if (arguments.size() == 1) {
            print_list(output.out, table, {});
            return exit_done;
        }
        const Command* command = find_command(table, arguments[1]);
        if (command == nullptr) {
            report_usage(output, "help", "unknown command '" + arguments[1] + "'");
            return exit_usage;
        }
        if (arguments.size() == 2) {
            print_command_help(output.out, command->name, *command);
            return exit_done;
        }
        if (arguments.size() == 3) {
            const Command* sub = find_command(command->subcommands, arguments[2]);
            if (sub == nullptr) {
                report_usage(
                    output, std::string(command->name), "unknown command '" + arguments[2] + "'"
                );
                return exit_usage;
            }
            print_command_help(output.out, join_path(command->name, sub->name), *sub);
            return exit_done;
        }
        report_usage(output, "help", "expected a command name");
        return exit_usage;
    }
    const Command* command = find_command(table, first);
    if (command == nullptr) {
        report_usage(output, first, "unknown command");
        return exit_usage;
    }
    return invoke(*command, first, arguments.subspan(1), output);
}

} // namespace

std::span<const Command> commands() {
    return command_table;
}

int run_tool(std::span<const std::string> arguments, Output& output) {
    return dispatch(command_table, arguments, output);
}

int run_tool(
    std::span<const Command> table, std::span<const std::string> arguments, Output& output
) {
    return dispatch(table, arguments, output);
}

} // namespace oa::tool
