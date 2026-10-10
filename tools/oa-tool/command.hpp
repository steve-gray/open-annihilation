// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-tool's command table. A command is a row: its name, the arguments it
// takes, the line help prints, and the function that runs it. A group names
// subcommands and leaves run null. commands.cpp holds the table; each
// command's function is declared here, so that file includes nothing else.

#pragma once

#include <cstdint>
#include <iosfwd>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::tool {

/// The command finished what it was asked.
inline constexpr int exit_done = 0;

/// The command ran and could not do it, or the answer is no.
inline constexpr int exit_failed = 1;

/// The invocation does not name a command the tool can run.
inline constexpr int exit_usage = 2;

/// No upper bound on how many arguments a command takes.
inline constexpr uint32_t any_count = UINT32_MAX;

/// Where a command prints. Tests pass their own streams; the program passes
/// the standard streams.
struct Output {
    std::ostream& out; ///< ordinary results
    std::ostream& err; ///< failures and the usage shown for a bad invocation
};

/// One command, or a group of subcommands.
///
/// least and most count every argument after the command's own name, options
/// included, before the command runs. A command whose options make that count
/// vary sets most to any_count and checks its own arguments.
struct Command {
    std::string_view name;      ///< the word that selects the command
    std::string_view arguments; ///< usage after the name, empty when it takes none
    std::string_view summary;   ///< one line, printed in the command list
    std::string_view help;      ///< the paragraph `help` prints under the usage line
    uint32_t least{};           ///< fewest arguments, inclusive
    uint32_t most{};            ///< most arguments, inclusive; any_count for no limit
    int (*run)(std::span<const std::string> arguments, Output& output){}; ///< null for a group
    std::span<const Command> subcommands{}; ///< empty for a command that runs itself
};

/// Reports that a command ran and could not finish.
///
/// run_tool prints the message and exits with exit_failed.
class Failure : public std::runtime_error {
  public:

    /// Reports a failure with the message run_tool prints.
    ///
    /// @param what the reason, with no command path; run_tool adds the path
    explicit Failure(const std::string& what) : std::runtime_error(what) {}
};

// The archive commands, defined in archive_commands.cpp.

/// Lists every file in an archive.
///
/// @param arguments the archive path
/// @param[in,out] output receives one `path<TAB>size` line per file
/// @return exit_done
/// @throws Failure when the archive cannot be read
[[nodiscard]] int run_list(std::span<const std::string> arguments, Output& output);

/// Copies one file out of an archive.
///
/// @param arguments the archive path, the entry and the output path
/// @param output unused; the command writes a file and prints nothing
/// @return exit_done
/// @throws Failure when the archive or the entry cannot be read, or the file cannot be written
[[nodiscard]] int run_extract(std::span<const std::string> arguments, Output& output);

/// Copies one file from a game directory's loose files or its archives.
///
/// @param arguments the game directory, the entry, the output path and any archive paths
/// @param[in,out] output receives the loose-or-archive line
/// @return exit_done
/// @throws Failure when the file cannot be read or written
[[nodiscard]] int run_asset_extract(std::span<const std::string> arguments, Output& output);

/// Decodes one image stored in an archive.
///
/// @param arguments the archive path, the image entry and the output path
/// @param[in,out] output receives the image's size line
/// @return exit_done
/// @throws Failure when the image cannot be read, decoded or written
[[nodiscard]] int run_preview(std::span<const std::string> arguments, Output& output);

/// Decodes a PCX image file.
///
/// @param arguments the PCX path and the output path
/// @param[in,out] output receives the image's size line
/// @return exit_done
/// @throws Failure when the image cannot be read, decoded or written
[[nodiscard]] int run_decode_pcx(std::span<const std::string> arguments, Output& output);

// The package command, defined in pack.cpp.

/// Packs a folder as a .oamod or a .oalang.
///
/// One positional argument is the folder. `--out FILE` names the package,
/// and `--force` replaces a file that already exists. Options may be written
/// before or after the folder. The manifest is written first and every other
/// file follows in byte order of its path.
///
/// @param arguments the folder and any options
/// @param[in,out] output receives the packed line, and diagnostics on failure
/// @return exit_done, or exit_usage when the arguments are not the command's
/// @throws Failure when the folder cannot be packed
[[nodiscard]] int run_pack(std::span<const std::string> arguments, Output& output);

/// Returns the tool's commands, in the order help lists them.
///
/// @return the command table
[[nodiscard]] std::span<const Command> commands();

/// Runs one invocation of the tool.
///
/// No arguments prints the command list on output.err and returns exit_usage.
/// `--help`, `-h` or `help` prints it on output.out and returns exit_done.
/// `help NAME` and `NAME --help` print that command. An unknown command, or a
/// count of arguments outside the command's least and most, prints the
/// problem and `oa-tool help` on output.err and returns exit_usage. A
/// Failure, or any other exception the command throws, prints the message on
/// output.err and returns exit_failed.
///
/// @param arguments the program's arguments, without the program name
/// @param[in,out] output where the command and the dispatcher print
/// @return exit_done, exit_failed or exit_usage
[[nodiscard]] int run_tool(std::span<const std::string> arguments, Output& output);

/// Runs one invocation against a chosen command table.
///
/// The table replaces commands() for the duration of the call. Tests use it
/// for a group that the tool does not ship.
///
/// @param table the commands that can be named, and their subcommands
/// @param arguments the program's arguments, without the program name
/// @param[in,out] output where the command and the dispatcher print
/// @return exit_done, exit_failed or exit_usage
[[nodiscard]] int
run_tool(std::span<const Command> table, std::span<const std::string> arguments, Output& output);

} // namespace oa::tool
