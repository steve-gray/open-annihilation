// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Options for one oa-tool command. The archive commands take none. A later
// command declares the options it accepts and reads them with
// parse_arguments; options may be written before, between or after the
// positional arguments.

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::tool {

/// One option a command accepts.
struct OptionSpec {
    std::string_view name; ///< the long name, without leading dashes
    bool takes_value{};    ///< true when the option is `--name value` or `--name=value`
    bool repeats{};        ///< true when the option may be given more than once
};

/// The options and positional arguments of one invocation.
struct Arguments {
    /// Positional arguments, in the order they were written.
    std::vector<std::string> positional;
    /// Every value of each option that takes one, in the order it was given.
    std::map<std::string, std::vector<std::string>, std::less<>> values;
    /// Flags that were set. A flag takes no value.
    std::set<std::string, std::less<>> flags;

    /// Returns the first value an option was given.
    ///
    /// The pointer addresses memory owned by this object.
    ///
    /// @param name the option's name, without leading dashes
    /// @return the first value, or null when the option was absent or takes no value
    [[nodiscard]] const std::string* value(std::string_view name) const;
};

/// Splits arguments into options and positional arguments.
///
/// `--name value` and `--name=value` give a value to an option that takes
/// one. `--name` sets a flag. `--` ends options: everything after it is
/// positional, and the `--` itself is not an argument. An unknown option, a
/// missing value, a value on a flag, or a second use of an option that does
/// not repeat (a flag never repeats) is refused.
///
/// @param arguments the command's arguments, without its name
/// @param options the options the command accepts
/// @param[out] problem the reason, when the arguments are refused
/// @return the split arguments, or nothing when they are refused
[[nodiscard]] std::optional<Arguments> parse_arguments(
    std::span<const std::string> arguments,
    std::span<const OptionSpec> options,
    std::string& problem
);

} // namespace oa::tool
