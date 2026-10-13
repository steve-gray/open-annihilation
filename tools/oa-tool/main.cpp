// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "command.hpp"

#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#if __has_include("oa_engine_build.hpp")
#include "oa_engine_build.hpp"
#endif
#include "oa/app/version_line.hpp"

int main(int argc, char** argv) {
    // The same line as the game, before any command reads a file.
    if (oa::app::version_argument_present(argc, argv)) {
        std::cout << oa::app::running_engine_version_line() << '\n';
        return oa::tool::exit_done;
    }
    try {
        std::vector<std::string> arguments;
        if (argc > 1)
            arguments.assign(argv + 1, argv + argc);
        oa::tool::Output output{std::cout, std::cerr};
        return oa::tool::run_tool(
            std::span<const std::string>(arguments.data(), arguments.size()), output
        );
    } catch (const std::exception& error) {
        std::cerr << "oa-tool: " << error.what() << '\n';
        return oa::tool::exit_failed;
    }
}
