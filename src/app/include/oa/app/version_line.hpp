// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The one line `open-annihilation --version` and `oa-tool --version` print.
#pragma once

#include <string>
#include <string_view>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace oa::app {

/// Reports whether an argument is exactly `--version`.
///
/// The program reads this before any other option, so a line that carries
/// `--version` prints the version and leaves without reading the rest.
///
/// @param argc argument count, the program name included
/// @param argv arguments; argv[0] is skipped, and a null entry is skipped
/// @return true when some argument is `--version`
[[nodiscard]] inline bool version_argument_present(int argc, char** argv) noexcept {
    if (argv == nullptr)
        return false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] != nullptr && std::string_view(argv[index]) == "--version")
            return true;
    }
    return false;
}

/// The release package's platform name for this build, as the packages name it
/// (`macos-universal`, `windows-x64`, `linux-x86_64` and the others).
///
/// @return the platform name
[[nodiscard]] inline std::string_view release_platform_name() noexcept {
#if defined(_WIN32)
#if defined(__aarch64__) || defined(_M_ARM64)
    return "windows-arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "windows-x64";
#else
    return "windows-x86";
#endif
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    return "ios";
#else
    return "macos-universal";
#endif
#elif defined(__ANDROID__)
    return "android";
#elif defined(__linux__)
#if defined(__aarch64__)
    return "linux-arm64";
#elif defined(__arm__)
    return "linux-armhf";
#else
    return "linux-x86_64";
#endif
#else
    return "unknown";
#endif
}

/// The architecture this process is running as.
///
/// @return `arm64`, `x86_64`, `x86` or `arm`
[[nodiscard]] inline std::string_view running_architecture() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}

/// The line `--version` prints.
///
/// `Open Annihilation <version> (<platform> <architecture>, <commit>)`, for
/// example `Open Annihilation 0.8.0 (macos-universal arm64, 2ac0217b)`. The
/// platform is release_platform_name and the architecture is
/// running_architecture. The commit is the first eight digits of the commit
/// OA_ENGINE_BUILD names, or `unknown` when the build names none. A version
/// label, when the build has one, follows the version after a hyphen.
///
/// Include `oa_engine_build.hpp` before this header so OA_ENGINE_BUILD is the
/// commit the build wrote. The version is OA_ENGINE_VERSION.
///
/// @return the line, with no newline
[[nodiscard]] inline std::string running_engine_version_line() {
    std::string version =
#ifdef OA_ENGINE_VERSION
        OA_ENGINE_VERSION
#else
        "0.0.0"
#endif
        ;
#ifdef OA_ENGINE_VERSION_LABEL
    constexpr std::string_view label = OA_ENGINE_VERSION_LABEL;
    if (!label.empty()) {
        version.push_back('-');
        version.append(label);
    }
#endif
    std::string commit = "unknown";
#ifdef OA_ENGINE_BUILD
    constexpr std::string_view build = OA_ENGINE_BUILD;
    const auto plus = build.rfind('+');
    if (plus != std::string_view::npos && plus + 1 < build.size()) {
        const std::string_view hex = build.substr(plus + 1, 8);
        const auto bad = hex.find_first_not_of("0123456789abcdef");
        if (bad == std::string_view::npos && hex.size() == 8)
            commit.assign(hex);
    }
#endif
    std::string line = "Open Annihilation ";
    line += version;
    line += " (";
    line += release_platform_name();
    line.push_back(' ');
    line += running_architecture();
    line += ", ";
    line += commit;
    line.push_back(')');
    return line;
}

} // namespace oa::app
