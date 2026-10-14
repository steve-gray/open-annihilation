// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The launch files the system opens in the game, read as they arrive and kept until the game
// takes them, and what the game does with one where it stands (opened_launch.hpp).
#include "oa/app/netgame/opened_launch.hpp"

#include "oa/base/threads.hpp"
#include "oa/netgame/presence.hpp"
#include "oa/platform/file_types.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace oa::app::netgame {

namespace fs = std::filesystem;

namespace {

/// The launch files waiting, oldest first.
struct Waiting {
    base::threads::Mutex lock{};
    std::deque<OpenedLaunch> launches{};
};

/// Returns the process's waiting launch files.
///
/// @return the one list
Waiting& waiting() {
    static Waiting kept;
    return kept;
}

/// Returns an ASCII letter in lower case; any other byte as it is.
///
/// @param character the byte
/// @return the byte, lower case
char lower_ascii(char character) noexcept {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                : character;
}

/// Tells whether two names are equal without ASCII case.
///
/// @param left one name
/// @param right the other
/// @return true when they are
bool same_name(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char one, char other) {
               return lower_ascii(one) == lower_ascii(other);
           });
}

/// Returns a path's UTF-8 spelling.
///
/// @param path the path
/// @return its spelling; empty when it cannot be converted
std::string utf8_of(const fs::path& path) {
    try {
        const std::u8string text = path.u8string();
        return {text.begin(), text.end()};
    } catch (const std::exception&) {
        return {};
    }
}

/// Returns the last part of a path written with either separator.
///
/// @param text the path, or a name
/// @return the part after the last separator
std::string_view last_part(std::string_view text) noexcept {
    const auto cut = text.find_last_of("/\\");
    return cut == std::string_view::npos ? text : text.substr(cut + 1);
}

/// Reads at most `limit` bytes of a file.
///
/// @param file the file
/// @param limit the most bytes read
/// @param[out] bytes what was read
/// @return false when the file cannot be opened or read
bool read_bytes(const fs::path& file, std::size_t limit, std::string& bytes) {
    std::ifstream input(file, std::ios::in | std::ios::binary);
    if (!input)
        return false;
    bytes.resize(limit);
    input.read(bytes.data(), static_cast<std::streamsize>(limit));
    if (input.bad())
        return false;
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    return true;
}

} // namespace

bool is_launch_file(std::string_view extension, std::string_view first_bytes) noexcept {
    if (same_name(extension, oa::platform::file_types::launch_extension))
        return true;
    return launch_file_kind(first_bytes) == LaunchFileKind::open_annihilation;
}

std::optional<OpenedLaunch> read_opened_launch(const fs::path& file) {
    std::string extension = utf8_of(file.extension());
    if (!extension.empty() && extension.front() == '.')
        extension.erase(extension.begin());
    const bool named = is_launch_file(extension, {});
    OpenedLaunch opened;
    opened.source = utf8_of(file);
    // One byte more than a launch may hold says that the file is longer.
    std::string bytes;
    if (!read_bytes(file, kLaunchFileLimit + 1, bytes)) {
        if (!named)
            return std::nullopt;
        opened.problem = opened.source + ": the file: cannot be read";
        return opened;
    }
    if (!named &&
        !is_launch_file(extension, std::string_view(bytes).substr(0, kOpenedFileProbeBytes)))
        return std::nullopt;
    if (bytes.size() > kLaunchFileLimit) {
        opened.problem = opened.source + ": the file: is longer than " +
                         std::to_string(kLaunchFileLimit) + " bytes";
        return opened;
    }
    opened.text = std::move(bytes);
    return opened;
}

void post_opened_launch(OpenedLaunch launch) {
    Waiting& kept = waiting();
    const base::threads::LockGuard guard(kept.lock);
    for (const OpenedLaunch& already : kept.launches)
        if (already.source == launch.source && already.text == launch.text)
            return;
    kept.launches.push_back(std::move(launch));
}

std::optional<OpenedLaunch> take_opened_launch() {
    Waiting& kept = waiting();
    const base::threads::LockGuard guard(kept.lock);
    if (kept.launches.empty())
        return std::nullopt;
    OpenedLaunch taken = std::move(kept.launches.front());
    kept.launches.pop_front();
    return taken;
}

bool opened_launch_waiting() {
    Waiting& kept = waiting();
    const base::threads::LockGuard guard(kept.lock);
    return !kept.launches.empty();
}

LaunchPlan read_opened_launch_plan(const OpenedLaunch& opened) {
    if (!opened.problem.empty())
        throw LaunchRefusal(opened.problem);
    return read_launch_text(opened.source, opened.text);
}

bool launch_content_differs(const LaunchContentChoice& launch, const RunningContent& running) {
    if (!launch.named)
        return false;
    if (!launch.mod.empty()) {
        const std::string_view named = last_part(launch.mod);
        const bool same_mod = (!running.mod_id.empty() && (same_name(launch.mod, running.mod_id) ||
                                                           same_name(named, running.mod_id))) ||
                              (!running.mod_folder.empty() && same_name(named, running.mod_folder));
        if (!same_mod)
            return true;
    }
    if ((launch.base_game.empty() && launch.expansions.empty()) || !running.bundles_known)
        return false;
    if (!launch.base_game.empty() && !same_name(launch.base_game, running.base_game))
        return true;
    // The expansions the launch names are those in play, whatever their order; the music
    // changes no rule and is not compared.
    const auto compared = [](const std::vector<std::string>& ids) {
        std::vector<std::string> kept;
        for (std::string id : ids) {
            std::transform(id.begin(), id.end(), id.begin(), lower_ascii);
            if (id != oa::netgame::presence_uncompared_expansion)
                kept.push_back(std::move(id));
        }
        std::sort(kept.begin(), kept.end());
        return kept;
    };
    return compared(launch.expansions) != compared(running.expansions);
}

OpenedLaunchStep opened_launch_step(GameStanding standing, bool launch_under_way) noexcept {
    switch (standing) {
    case GameStanding::passing:
        return OpenedLaunchStep::wait;
    case GameStanding::match:
        return OpenedLaunchStep::refuse_in_match;
    case GameStanding::elsewhere:
        return OpenedLaunchStep::refuse;
    case GameStanding::single_player:
        return launch_under_way ? OpenedLaunchStep::refuse : OpenedLaunchStep::back_to_main_menu;
    case GameStanding::main_menu:
        return launch_under_way ? OpenedLaunchStep::refuse : OpenedLaunchStep::go_to_battle_room;
    }
    return OpenedLaunchStep::wait;
}

} // namespace oa::app::netgame
