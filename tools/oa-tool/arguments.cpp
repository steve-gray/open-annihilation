// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "arguments.hpp"

namespace oa::tool {
namespace {

/// Returns the option named `name`, or null when the command does not accept it.
const OptionSpec* find_option(std::span<const OptionSpec> options, std::string_view name) {
    for (const OptionSpec& option : options)
        if (option.name == name)
            return &option;
    return nullptr;
}

/// Spells an option the way an invocation writes it.
std::string option_text(std::string_view name) {
    return "--" + std::string(name);
}

} // namespace

const std::string* Arguments::value(std::string_view name) const {
    const auto found = values.find(name);
    if (found == values.end() || found->second.empty())
        return nullptr;
    return &found->second.front();
}

std::optional<Arguments> parse_arguments(
    std::span<const std::string> arguments,
    std::span<const OptionSpec> options,
    std::string& problem
) {
    Arguments parsed;
    bool ended = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& token = arguments[index];
        if (!ended && token == "--") {
            ended = true;
            continue;
        }
        if (!ended && token.size() > 2 && token.starts_with("--")) {
            const std::string_view body(token.data() + 2, token.size() - 2);
            const auto equals = body.find('=');
            const std::string_view name = body.substr(0, equals);
            const bool attached = equals != std::string_view::npos;
            const OptionSpec* spec = find_option(options, name);
            if (spec == nullptr) {
                problem = "unknown option '" + option_text(name) + "'";
                return std::nullopt;
            }
            if (!spec->takes_value) {
                if (attached) {
                    problem = "option '" + option_text(name) + "' takes no value";
                    return std::nullopt;
                }
                if (!parsed.flags.insert(std::string(name)).second) {
                    problem = "option '" + option_text(name) + "' was given twice";
                    return std::nullopt;
                }
                continue;
            }
            if (!spec->repeats && parsed.values.find(name) != parsed.values.end()) {
                problem = "option '" + option_text(name) + "' was given twice";
                return std::nullopt;
            }
            std::string value;
            if (attached) {
                value = std::string(body.substr(equals + 1));
            } else if (index + 1 >= arguments.size()) {
                problem = "option '" + option_text(name) + "' needs a value";
                return std::nullopt;
            } else {
                value = arguments[++index];
            }
            parsed.values[std::string(name)].push_back(std::move(value));
            continue;
        }
        parsed.positional.push_back(token);
    }
    problem.clear();
    return parsed;
}

} // namespace oa::tool
