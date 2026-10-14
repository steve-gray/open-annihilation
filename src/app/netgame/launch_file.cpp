// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Open Annihilation's .oalaunch file, TA Forever's INI, the options that name
// each launch setting and a launch piped in, all as one launch plan.
#include "oa/app/netgame/launch_file.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/base/text.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/netgame/dplay.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oa::app::netgame {

/// A value as the file wrote it, or as an option gave it.
struct LaunchValue {
    const oa::formats::oamod::Node* node{}; ///< the file's value; null for an option
    std::string_view text{};                ///< the option's value
};

namespace {

namespace launch = oa::ui::frontend_multiplayer::launch;
namespace oamod = oa::formats::oamod;

// The battle room's metal and energy sliders stop at 10000.
constexpr int64_t kHighestResource = 10000;
// The battle room's unit slider starts at 20. A mod may raise the limit to 6553.
constexpr int64_t kLowestUnitLimit = 20;
constexpr int64_t kHighestUnitLimit = 6553;
constexpr int64_t kLowestPlayerLimit = 2;
constexpr int64_t kHighestPlayerLimit = 10;
constexpr int64_t kHighestPort = 65535;
// LaunchBlock codes for the battle room's choices: 1, 2 and 3 in the order
// the manual lists the words.
constexpr int32_t kCommanderContinues = 1;
constexpr int32_t kCommanderEnds = 2;
constexpr int32_t kCommanderDeathmatch = 3;
constexpr int32_t kSightTrue = 1;
constexpr int32_t kSightCircular = 2;
constexpr int32_t kSightPermanent = 3;
constexpr int32_t kChoiceOff = 1; // cheats, watchers: off; mapping: unmapped; starts: fixed
constexpr int32_t kChoiceOn = 2;  // cheats, watchers: on; mapping: mapped; starts: random
// The header an .oalaunch file begins with, and the one version it may name.
constexpr std::string_view kHeader = "oalaunch:";
constexpr int64_t kLaunchVersion = 1;
// The bytes UTF-8's byte order mark is written with.
constexpr std::string_view kByteOrderMark = "\xef\xbb\xbf";
// The name a refusal gives standard input.
constexpr std::string_view kStandardInputName = "standard input";

/// Throws a refusal that names the place and why.
///
/// @param place the file and the field, or the option
/// @param why why it cannot be used
[[noreturn]] void refuse(const LaunchPlace& place, std::string_view why) {
    std::string line;
    if (!place.source.empty())
        line = std::string(place.source) + ": ";
    line += std::string(place.name) + ": " + std::string(why);
    throw LaunchRefusal(line);
}

/// Throws a refusal that names the file, the field and why.
///
/// @param source the file's name
/// @param field the setting
/// @param why why it cannot be used
[[noreturn]] void refuse(std::string_view source, std::string_view field, std::string_view why) {
    refuse(LaunchPlace{source, field}, why);
}

/// Tells whether a character is a space the readers skip.
///
/// @param character the character
/// @return true for a blank, tab, newline, vertical tab, form feed or return
bool is_space(char character) noexcept {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

/// Drops leading and trailing spaces.
///
/// @param text the text
/// @return the middle
std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return text;
}

/// Compares two names without regard to ASCII case.
///
/// @param left the first name
/// @param right the second name
/// @return true when they are the same letters
bool same_name(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (std::size_t at = 0; at < left.size(); ++at) {
        const auto fold = [](unsigned char character) noexcept {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                        : static_cast<char>(character);
        };
        if (fold(static_cast<unsigned char>(left[at])) !=
            fold(static_cast<unsigned char>(right[at])))
            return false;
    }
    return true;
}

/// Reads a whole number that uses every character.
///
/// @param text the text
/// @return the number, or nothing when it is not a whole number
std::optional<int64_t> whole_number(std::string_view text) noexcept {
    if (text.empty())
        return std::nullopt;
    int64_t number = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return number;
}

/// Tells whether a number fits a 32-bit field.
///
/// @param number the number
/// @return true when it fits
bool fits_int32(int64_t number) noexcept {
    return number >= std::numeric_limits<int32_t>::min() &&
           number <= std::numeric_limits<int32_t>::max();
}

/// Drops a leading byte order mark.
///
/// @param text the file's bytes
/// @return the bytes after it
std::string_view without_byte_order_mark(std::string_view text) noexcept {
    if (text.starts_with(kByteOrderMark))
        text.remove_prefix(kByteOrderMark.size());
    return text;
}

// ---------------------------------------------------------------------------
// Values, from the file or from an option

/// Reads a whole number: a YAML number, or an option's digits.
///
/// @param value the value
/// @param place where a refusal points
/// @return the number, within 32 bits
int64_t read_whole(const LaunchValue& value, const LaunchPlace& place) {
    int64_t number = 0;
    if (value.node != nullptr) {
        if (value.node->kind != oamod::NodeKind::number ||
            !oamod::integer_value(value.node->number, number) || !fits_int32(number))
            refuse(place, "expected a whole number");
        return number;
    }
    const auto parsed = whole_number(value.text);
    if (!parsed || !fits_int32(*parsed))
        refuse(place, "expected a whole number");
    return *parsed;
}

/// Reads a whole number within a range.
///
/// @param value the value
/// @param place where a refusal points
/// @param lowest the least it may be
/// @param highest the most it may be
/// @return the number
int32_t
read_between(const LaunchValue& value, const LaunchPlace& place, int64_t lowest, int64_t highest) {
    const int64_t number = read_whole(value, place);
    if (number < lowest)
        refuse(place, std::to_string(number) + " is below " + std::to_string(lowest));
    if (number > highest)
        refuse(place, std::to_string(number) + " is above " + std::to_string(highest));
    return static_cast<int32_t>(number);
}

/// Reads a scalar as text: a YAML string, number or boolean as written, or an option's value.
///
/// @param value the value
/// @param place where a refusal points
/// @return the text
std::string_view read_text(const LaunchValue& value, const LaunchPlace& place) {
    if (value.node == nullptr)
        return value.text;
    const auto kind = value.node->kind;
    if (kind != oamod::NodeKind::string && kind != oamod::NodeKind::number &&
        kind != oamod::NodeKind::boolean)
        refuse(place, "expected text");
    return value.node->text;
}

/// Reads a text that fits a field, including the terminating NUL.
///
/// The text is not repeated in a refusal, so a password is never written out.
///
/// @param value the value
/// @param place where a refusal points
/// @param field the field it must fit
/// @return the text
std::string_view
read_fitting(const LaunchValue& value, const LaunchPlace& place, std::span<char> field) {
    const std::string_view text = read_text(value, place);
    if (field.empty() || text.size() >= field.size()) {
        const auto limit = field.empty() ? std::size_t{0} : field.size() - 1;
        refuse(place, "is longer than " + std::to_string(limit) + " characters");
    }
    oa::base::text::copy_terminated(field, text);
    return text;
}

/// One word a setting takes and the code the launch block keeps for it.
struct Word {
    std::string_view word{};
    int32_t code{};
};

/// Reads one of a setting's words.
///
/// @param value the value
/// @param place where a refusal points
/// @param words the words and their codes
/// @param expected what the setting takes, for a refusal
/// @return the word's code
int32_t read_word(
    const LaunchValue& value,
    const LaunchPlace& place,
    std::span<const Word> words,
    std::string_view expected
) {
    std::string_view text = value.text;
    if (value.node != nullptr) {
        if (value.node->kind != oamod::NodeKind::string)
            refuse(place, "expected " + std::string(expected));
        text = value.node->text;
    }
    for (const Word& word : words)
        if (text == word.word)
            return word.code;
    refuse(place, "expected " + std::string(expected));
}

/// Reads on or off. The file may also write true or false.
///
/// @param value the value
/// @param place where a refusal points
/// @return true for on
bool read_on_off(const LaunchValue& value, const LaunchPlace& place) {
    if (value.node != nullptr && value.node->kind == oamod::NodeKind::boolean)
        return value.node->boolean;
    static constexpr Word words[] = {{"on", 1}, {"off", 0}, {"true", 1}, {"false", 0}};
    return read_word(value, place, words, "on or off") != 0;
}

/// Reads a team: 1 to 5, or none.
///
/// @param value the value
/// @param place where a refusal points
/// @return 1 to 5, or 0 for none
int32_t read_team(const LaunchValue& value, const LaunchPlace& place) {
    const bool word = value.node != nullptr ? value.node->kind == oamod::NodeKind::string
                                            : !whole_number(value.text).has_value();
    if (word) {
        if (read_text(value, place) != "none")
            refuse(place, "expected 1 to 5 or none");
        return 0;
    }
    const int64_t number = read_whole(value, place);
    if (number < launch::kLaunchTeamFirst || number > launch::kLaunchTeamLast)
        refuse(place, "expected 1 to 5 or none");
    return static_cast<int32_t>(number);
}

// The words of each choice.
constexpr Word kRoleWords[] = {{"host", 1}, {"join", 2}};
constexpr Word kPlayerSideWords[] = {
    {"arm", launch::faction::arm}, {"core", launch::faction::core}, {"watcher", 0}
};
constexpr Word kComputerSideWords[] = {
    {"arm", launch::faction::arm}, {"core", launch::faction::core}
};
constexpr Word kDifficultyWords[] = {
    {"easy", launch::difficulty::easy},
    {"medium", launch::difficulty::medium},
    {"hard", launch::difficulty::hard},
};
constexpr Word kCommanderWords[] = {
    {"continues", kCommanderContinues},
    {"ends", kCommanderEnds},
    {"deathmatch", kCommanderDeathmatch},
};
constexpr Word kSightWords[] = {
    {"true", kSightTrue}, {"circular", kSightCircular}, {"permanent", kSightPermanent}
};
constexpr Word kMappingWords[] = {{"mapped", kChoiceOn}, {"unmapped", kChoiceOff}};
constexpr Word kStartWords[] = {{"random", kChoiceOn}, {"fixed", kChoiceOff}};

// ---------------------------------------------------------------------------
// Storing each setting

/// Stores game.role: host or join, over TCP/IP.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_role(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    const bool host = read_word(value, place, kRoleWords, "host or join") == 1;
    plan.role = host ? LaunchRole::host : LaunchRole::join;
    plan.block.hosting = host ? 1U : 0U;
    plan.block.connection_type = launch::connection_type::tcpip;
}

/// Stores game.address, the address of the game to join.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_address(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.address = std::string(read_fitting(value, place, plan.block.address));
}

/// Stores game.name, the game's name.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_game_name(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.game_name = std::string(read_fitting(value, place, plan.block.session_name));
}

/// Stores game.password, the game's password.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_password(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.password = std::string(read_fitting(value, place, plan.block.password));
}

/// Stores game.player-limit.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_player_limit(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.player_limit = read_between(value, place, kLowestPlayerLimit, kHighestPlayerLimit);
}

/// Stores game.launcher-name, the launcher's name on the battle room's caption.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_launcher_name(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    (void)read_fitting(value, place, plan.block.provider_label);
}

/// Stores game.tournament.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_tournament(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.tournament = read_on_off(value, place) ? 1 : 0;
}

/// Stores game.closed.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_closed(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.closed = read_on_off(value, place) ? 1U : 0U;
}

/// Stores game.directplay.application-guid.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_guid(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    const std::string_view text = read_text(value, place);
    std::array<uint8_t, 16> bytes{};
    if (!oa::netgame::parse_application_guid(text, bytes.data()))
        refuse(place, "expected a GUID, with or without braces");
    plan.dplay_guid = bytes;
}

/// Stores game.directplay.port.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_port(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    const int64_t number = read_whole(value, place);
    if (number < 1 || number > kHighestPort)
        refuse(place, "expected a port from 1 through 65535");
    plan.dplay_port = static_cast<uint16_t>(number);
}

/// Stores player.name, the player's name.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_player_name(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.player_name = std::string(read_fitting(value, place, plan.block.user_name));
}

/// Stores player.side: a faction, or watching.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_side(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    const int32_t side = read_word(value, place, kPlayerSideWords, "arm, core or watcher");
    plan.block.faction = side;
    plan.block.watcher = side == 0 ? 1U : 0U;
}

/// Stores player.team.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_team(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.team = read_team(value, place);
}

/// Stores player.colour.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_colour(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.color =
        read_between(value, place, launch::kLaunchColourFirst, launch::kLaunchColourLast);
}

/// Stores map, the map the host plays.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_map(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    (void)read_fitting(value, place, plan.block.mission);
}

/// Stores rules.commander-death.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_commander(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.death_option =
        read_word(value, place, kCommanderWords, "continues, ends or deathmatch");
}

/// Stores rules.metal, the starting metal.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_metal(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.metal = read_between(value, place, 0, kHighestResource);
}

/// Stores rules.energy, the starting energy.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_energy(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.energy = read_between(value, place, 0, kHighestResource);
}

/// Stores rules.unit-limit.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_unit_limit(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.max_units = read_between(value, place, kLowestUnitLimit, kHighestUnitLimit);
}

/// Stores rules.line-of-sight. The file may write the word true as a boolean.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_sight(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    if (value.node != nullptr && value.node->kind == oamod::NodeKind::boolean) {
        if (!value.node->boolean)
            refuse(place, "expected true, circular or permanent");
        plan.block.los = kSightTrue;
        return;
    }
    plan.block.los = read_word(value, place, kSightWords, "true, circular or permanent");
}

/// Stores rules.mapping.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_mapping(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.mapping = read_word(value, place, kMappingWords, "mapped or unmapped");
}

/// Stores rules.start-positions.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_starts(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.location = read_word(value, place, kStartWords, "fixed or random");
}

/// Stores rules.cheats.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_cheats(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.cheats = read_on_off(value, place) ? kChoiceOn : kChoiceOff;
}

/// Stores rules.watchers.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_watchers(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.watching = read_on_off(value, place) ? kChoiceOn : kChoiceOff;
}

/// Stores rules.time-limit.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_time_limit(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.time_limit = read_between(value, place, 0, std::numeric_limits<int32_t>::max());
}

/// Stores rules.lock-options.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_lock(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.block.lock_options = read_on_off(value, place) ? 1U : 0U;
}

/// Reads a restriction's limit: 0 to 100, or no-limit.
///
/// @param value the value
/// @param place where a refusal points
/// @return the limit, kLaunchNoLimit for no-limit
int32_t read_limit(const LaunchValue& value, const LaunchPlace& place) {
    const bool word = value.node != nullptr ? value.node->kind == oamod::NodeKind::string
                                            : !whole_number(value.text).has_value();
    if (word) {
        if (read_text(value, place) != "no-limit")
            refuse(place, "expected 0 to 100 or no-limit");
        return launch::kLaunchNoLimit;
    }
    return read_between(value, place, 0, launch::kLaunchHighestLimit);
}

/// Sets one unit's restriction in the plan; a later one for the same unit replaces it.
///
/// @param[in,out] plan the plan
/// @param unit the unit's FBI UnitName
/// @param limit 0 to 100, or kLaunchNoLimit
/// @param place where a refusal points
void restrict_unit(
    LaunchPlan& plan, std::string_view unit, int32_t limit, const LaunchPlace& place
) {
    auto& block = plan.block;
    if (unit.empty())
        refuse(place, "expected a unit's name");
    if (unit.size() >= launch::kLaunchUnitNameBytes)
        refuse(
            place,
            "is longer than " + std::to_string(launch::kLaunchUnitNameBytes - 1) + " characters"
        );
    for (int32_t at = 0; at < block.restriction_count; ++at)
        if (same_name(block.restrictions[at].unit, unit)) {
            block.restrictions[at].limit = limit;
            return;
        }
    if (block.restriction_count >= launch::kLaunchRestrictionLimit)
        refuse(
            place,
            "sets at most " + std::to_string(launch::kLaunchRestrictionLimit) + " restrictions"
        );
    auto& restriction = block.restrictions[block.restriction_count];
    oa::base::text::copy_terminated(std::span<char>(restriction.unit), unit);
    restriction.limit = limit;
    ++block.restriction_count;
}

/// Stores rules.unit-restrictions: the file's group of units, or one UNIT=LIMIT option.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_restrictions(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    if (value.node == nullptr) {
        const auto equals = value.text.find('=');
        if (equals == std::string_view::npos)
            refuse(place, "expected UNIT=LIMIT");
        const std::string_view unit = value.text.substr(0, equals);
        const std::string name = std::string(place.name) + " " + std::string(unit);
        const LaunchPlace at{{}, name};
        restrict_unit(
            plan, unit, read_limit(LaunchValue{nullptr, value.text.substr(equals + 1)}, at), at
        );
        return;
    }
    if (value.node->kind != oamod::NodeKind::mapping)
        refuse(place, "expected a group of units and their limits");
    plan.block.restriction_count = 0;
    for (const oamod::Node& entry : value.node->children) {
        if (entry.key.kind != oamod::KeyKind::string)
            refuse(place, "a key is not text");
        const std::string name = std::string(place.name) + "." + entry.key.text;
        const LaunchPlace at{place.source, name};
        restrict_unit(plan, entry.key.text, read_limit(LaunchValue{&entry, {}}, at), at);
    }
}

/// Adds one computer player to the plan.
///
/// @param[in,out] plan the plan
/// @param computer the computer player
/// @param place where a refusal points
void add_computer(
    LaunchPlan& plan, const launch::LaunchComputer& computer, const LaunchPlace& place
) {
    auto& block = plan.block;
    if (block.computer_count >= launch::kLaunchComputerLimit)
        refuse(
            place,
            "seats at most " + std::to_string(launch::kLaunchComputerLimit) + " computer players"
        );
    block.computers[block.computer_count] = computer;
    ++block.computer_count;
}

/// Reads one computer player of the file's list.
///
/// @param item the list's item, a group of side, team, colour and difficulty
/// @param place the item's place, such as computer-players[2]
/// @return the computer player
launch::LaunchComputer read_computer_group(const oamod::Node& item, const LaunchPlace& place) {
    if (item.kind != oamod::NodeKind::mapping)
        refuse(place, "expected a group of side, team, colour and difficulty");
    launch::LaunchComputer computer{};
    const std::string prefix(place.name);
    for (const oamod::Node& entry : item.children) {
        const std::string name = prefix + "." + entry.key.text;
        const LaunchPlace at{place.source, name};
        const LaunchValue value{&entry, {}};
        if (entry.key.kind != oamod::KeyKind::string)
            refuse(place, "a key is not text");
        if (entry.key.text == "side")
            computer.faction = read_word(value, at, kComputerSideWords, "arm or core");
        else if (entry.key.text == "team")
            computer.team = read_team(value, at);
        else if (entry.key.text == "colour")
            computer.color =
                read_between(value, at, launch::kLaunchColourFirst, launch::kLaunchColourLast);
        else if (entry.key.text == "difficulty")
            computer.difficulty = read_word(value, at, kDifficultyWords, "easy, medium or hard");
        else
            refuse(at, "is not a launch setting");
    }
    return computer;
}

/// Reads one computer player an option gives: SIDE[:TEAM[:COLOUR[:DIFFICULTY]]].
///
/// An empty part leaves that choice to the battle room.
///
/// @param text the option's value
/// @param place the option
/// @return the computer player
launch::LaunchComputer read_computer_text(std::string_view text, const LaunchPlace& place) {
    std::array<std::string_view, 4> parts{};
    std::size_t count = 0;
    std::size_t start = 0;
    while (true) {
        const auto colon = text.find(':', start);
        if (count == parts.size())
            refuse(place, "expected SIDE[:TEAM[:COLOUR[:DIFFICULTY]]]");
        parts[count++] = text.substr(
            start, colon == std::string_view::npos ? std::string_view::npos : colon - start
        );
        if (colon == std::string_view::npos)
            break;
        start = colon + 1;
    }
    launch::LaunchComputer computer{};
    const auto part = [&](std::size_t index, std::string_view what) {
        return std::pair<LaunchValue, std::string>{
            LaunchValue{nullptr, parts[index]}, std::string(place.name) + " " + std::string(what)
        };
    };
    {
        const auto [value, name] = part(0, "side");
        computer.faction =
            read_word(value, LaunchPlace{{}, name}, kComputerSideWords, "arm or core");
    }
    if (count > 1 && !parts[1].empty()) {
        const auto [value, name] = part(1, "team");
        computer.team = read_team(value, LaunchPlace{{}, name});
    }
    if (count > 2 && !parts[2].empty()) {
        const auto [value, name] = part(2, "colour");
        computer.color = read_between(
            value, LaunchPlace{{}, name}, launch::kLaunchColourFirst, launch::kLaunchColourLast
        );
    }
    if (count > 3 && !parts[3].empty()) {
        const auto [value, name] = part(3, "difficulty");
        computer.difficulty =
            read_word(value, LaunchPlace{{}, name}, kDifficultyWords, "easy, medium or hard");
    }
    return computer;
}

/// Stores computer-players: the file's list, or one computer player an option adds.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_computers(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    if (value.node == nullptr) {
        add_computer(plan, read_computer_text(value.text, place), place);
        return;
    }
    if (value.node->kind != oamod::NodeKind::sequence)
        refuse(place, "expected a list of computer players");
    plan.block.computer_count = 0;
    int32_t number = 0;
    for (const oamod::Node& item : value.node->children) {
        ++number;
        const std::string name = std::string(place.name) + "[" + std::to_string(number) + "]";
        const LaunchPlace at{place.source, name};
        add_computer(plan, read_computer_group(item, at), at);
    }
}

/// Stores content.base-game.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_base_game(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.content.base_game = std::string(read_text(value, place));
    plan.content.named = true;
}

/// Stores content.expansions.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_expansions(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    if (value.node == nullptr || value.node->kind != oamod::NodeKind::sequence)
        refuse(place, "expected a list");
    plan.content.expansions.clear();
    for (const oamod::Node& item : value.node->children) {
        if (item.kind != oamod::NodeKind::string)
            refuse(place, "expected text");
        plan.content.expansions.push_back(item.text);
    }
    plan.content.named = true;
}

/// Stores content.mod.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_mod(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.content.mod = std::string(read_text(value, place));
    plan.content.named = true;
}

/// Stores status.standard-output.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_standard_output(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    plan.status.standard_output = read_on_off(value, place);
    plan.status.standard_output_named = true;
}

/// Stores status.file.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_status_file(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    const std::string_view text = read_text(value, place);
    if (text.empty())
        refuse(place, "expected a file");
    plan.status.file = std::string(text);
}

/// Stores status.quit-after-game: on, off or after-score.
///
/// @param[in,out] plan the plan
/// @param value the value
/// @param place where a refusal points
void store_quit_after(LaunchPlan& plan, const LaunchValue& value, const LaunchPlace& place) {
    if (value.node != nullptr && value.node->kind == oamod::NodeKind::string &&
        value.node->text == "after-score") {
        plan.status.quit_after = QuitAfter::after_score;
        return;
    }
    if (value.node == nullptr && value.text == "after-score") {
        plan.status.quit_after = QuitAfter::after_score;
        return;
    }
    if (value.node != nullptr && value.node->kind != oamod::NodeKind::boolean &&
        value.node->kind != oamod::NodeKind::string)
        refuse(place, "expected on, off or after-score");
    static constexpr Word words[] = {{"on", 1}, {"off", 0}, {"true", 1}, {"false", 0}};
    const bool on = value.node != nullptr && value.node->kind == oamod::NodeKind::boolean
                        ? value.node->boolean
                        : read_word(value, place, words, "on, off or after-score") != 0;
    plan.status.quit_after = on ? QuitAfter::game : QuitAfter::off;
}

// ---------------------------------------------------------------------------
// The table

using Reader = LaunchOptionReader;

// Every launch setting. The file's reader takes its keys from here, network
// play's options and --help theirs, and docs/command-line.md's table must say
// the same, which netgame-launch-help checks.
constexpr LaunchField kFields[] = {
    {"game.role",
     "--host | --join ADDRESS",
     "Hosts the game, or joins the game at `game.address`: `host` or `join`. A host plays over "
     "TCP/IP.",
     Reader::direct_game,
     store_role},
    {"game.address",
     "--join ADDRESS",
     "The address of the game to join, at most 63 characters. A host needs none.",
     Reader::direct_game,
     store_address},
    {"game.name",
     "--game-name NAME",
     "The game's name, at most 63 characters.",
     Reader::direct_game,
     store_game_name},
    {"game.password",
     "--game-password PASSWORD",
     "The game's password, at most 31 characters. It is never written to the log.",
     Reader::direct_game,
     store_password},
    {"game.player-limit",
     "--player-limit N",
     "How many players the game takes, from 2 through 10.",
     Reader::network_play,
     store_player_limit},
    {"game.launcher-name",
     "--launcher-name NAME",
     "The launcher's name, at most 15 characters. The battle room shows it in place of Previous "
     "Menu when it has at most 11.",
     Reader::network_play,
     store_launcher_name},
    {"game.tournament",
     "--tournament[=on|off]",
     "A tournament game: `on` or `off`. It seats no computer players, and with "
     "`rules.lock-options` the players' sides and teams are locked too.",
     Reader::network_play,
     store_tournament},
    {"game.closed",
     "--closed[=on|off]",
     "Closes the game to more players once the launch's own computer players are seated: `on` "
     "or `off`.",
     Reader::network_play,
     store_closed},
    {"game.directplay.application-guid",
     "--dplay-guid GUID",
     "The DirectPlay application GUID the game hosts under and looks for, with or without "
     "braces.",
     Reader::direct_game,
     store_guid},
    {"game.directplay.port",
     "--dplay-port PORT",
     "The DirectPlay port the game hosts on and looks for games at, from 1 through 65535.",
     Reader::direct_game,
     store_port},
    {"player.name",
     "--player-name NAME",
     "The player's name, at most 31 characters.",
     Reader::direct_game,
     store_player_name},
    {"player.side",
     "--side arm|core|watcher",
     "The player's side, or `watcher` to watch instead of play.",
     Reader::network_play,
     store_side},
    {"player.team",
     "--team 1-5|none",
     "The player's team, from 1 through 5, or `none`.",
     Reader::network_play,
     store_team},
    {"player.colour",
     "--colour 1-10",
     "The player's colour, from 1 through 10 in the order the battle room steps through them. A "
     "colour another player has gives the next free one.",
     Reader::network_play,
     store_colour},
    {"map",
     "--map NAME",
     "The map the host plays: the name the list shows, or its key (`name@package` for a pack "
     "map). At most 63 characters.",
     Reader::network_play,
     store_map},
    {"rules.commander-death",
     "--commander-death continues|ends|deathmatch",
     "What a commander's death does: the game `continues`, the player's game `ends`, or "
     "`deathmatch`.",
     Reader::network_play,
     store_commander},
    {"rules.metal",
     "--metal N",
     "Starting metal, from 0 through 10000. 0 leaves the battle room's own amount.",
     Reader::network_play,
     store_metal},
    {"rules.energy",
     "--energy N",
     "Starting energy, from 0 through 10000. 0 leaves the battle room's own amount.",
     Reader::network_play,
     store_energy},
    {"rules.unit-limit",
     "--unit-limit N",
     "The most units each player may have, from 20 through 6553.",
     Reader::network_play,
     store_unit_limit},
    {"rules.line-of-sight",
     "--line-of-sight true|circular|permanent",
     "What each player sees: `true` line of sight, `circular` or `permanent`.",
     Reader::network_play,
     store_sight},
    {"rules.mapping",
     "--mapping mapped|unmapped",
     "Whether the map starts `mapped` or `unmapped`.",
     Reader::network_play,
     store_mapping},
    {"rules.start-positions",
     "--start-positions fixed|random",
     "Whether players start at `fixed` or `random` positions.",
     Reader::network_play,
     store_starts},
    {"rules.cheats",
     "--cheats on|off",
     "Whether the cheat codes work: `on` or `off`.",
     Reader::network_play,
     store_cheats},
    {"rules.watchers",
     "--watchers on|off",
     "Whether players may watch instead of play: `on` or `off`.",
     Reader::network_play,
     store_watchers},
    {"rules.time-limit",
     "--time-limit N",
     "A whole number of 0 or more, kept and passed on with the score report.",
     Reader::network_play,
     store_time_limit},
    {"rules.lock-options",
     "--lock-options[=on|off]",
     "Locks the battle room's option controls for a host: `on` or `off`.",
     Reader::network_play,
     store_lock},
    {"rules.unit-restrictions",
     "--unit-restriction UNIT=0-100|no-limit",
     "Build limits, as the battle room's Restrictions sets them: each unit by its FBI UnitName "
     "(such as `CORKROG`), from 0, which cannot be built, through 100, or `no-limit`. Give the "
     "option once for each unit. A unit the game's content does not have is left out.",
     Reader::network_play,
     store_restrictions},
    {"computer-players",
     "--computer-player arm|core[:TEAM[:COLOUR[:DIFFICULTY]]]",
     "The computer players this machine seats, at most 9, each with a side, and a team, colour "
     "and difficulty (`easy`, `medium` or `hard`) the battle room otherwise chooses. Give the "
     "option once for each.",
     Reader::network_play,
     store_computers},
    {"content.base-game",
     "--content GAME[:EXPANSION,...]",
     "The base game this run plays, by its id.",
     Reader::engine,
     store_base_game},
    {"content.expansions",
     "--content GAME[:EXPANSION,...]",
     "The expansions this run plays, a list of ids. It needs a base game.",
     Reader::engine,
     store_expansions},
    {"content.mod",
     "--mod FILE | --mod-dir PATH",
     "The mod this run plays: in the file a mod's id or a path, and on the command line its "
     "profile or its folder.",
     Reader::engine,
     store_mod},
    {"status.standard-output",
     "--status-stdout",
     "Writes the status stream on standard output: `on` or `off`.",
     Reader::status_stream,
     store_standard_output},
    {"status.file",
     "--status-file PATH",
     "Writes the status stream to this file.",
     Reader::status_stream,
     store_status_file},
    {"status.quit-after-game",
     "--quit-after-game[=after-score]",
     "Leaves when the game ends (`on`), stays (`off`), or leaves after the score "
     "(`after-score`).",
     Reader::status_stream,
     store_quit_after},
};

/// Returns the option's name without its value: --metal for "--metal N".
///
/// @param option the option column, such as "--tournament[=on|off]"
/// @return the name
std::string_view option_name(std::string_view option) noexcept {
    const auto end = option.find_first_of(" [=");
    return option.substr(0, end);
}

// ---------------------------------------------------------------------------
// The .oalaunch file

/// Tells whether a dotted name is a group of the table, such as rules.
///
/// @param name the dotted name
/// @return true when a field begins with it and a dot
bool is_group(std::string_view name) noexcept {
    for (const LaunchField& field : kFields)
        if (field.field.size() > name.size() && field.field.starts_with(name) &&
            field.field[name.size()] == '.')
            return true;
    return false;
}

/// Reads one group of the file, the top included, through the table.
///
/// @param[in,out] plan the plan
/// @param group the mapping
/// @param source the file's name
/// @param prefix the group's dotted name; empty at the top
void read_group(
    LaunchPlan& plan, const oamod::Node& group, std::string_view source, const std::string& prefix
) {
    for (const oamod::Node& child : group.children) {
        if (child.key.kind != oamod::KeyKind::string)
            refuse(source, prefix.empty() ? std::string("the file") : prefix, "a key is not text");
        const std::string name = prefix.empty() ? child.key.text : prefix + "." + child.key.text;
        if (prefix.empty() && name == "oalaunch")
            continue;
        if (const LaunchField* field = find_launch_field(name)) {
            field->store(plan, LaunchValue{&child, {}}, LaunchPlace{source, name});
            continue;
        }
        if (!is_group(name))
            refuse(source, name, "is not a launch setting");
        if (child.kind != oamod::NodeKind::mapping)
            refuse(source, name, "expected a group of settings");
        read_group(plan, child, source, name);
    }
}

/// Reads Open Annihilation's .oalaunch file with the mod profile's reader.
///
/// @param source the file's name
/// @param text the file's bytes, without a byte order mark
/// @return the plan
LaunchPlan read_yaml(std::string_view source, std::string_view text) {
    oamod::Node root;
    oamod::ReadError error;
    const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
    if (!oamod::read_document(std::span<const uint8_t>(bytes, text.size()), root, error)) {
        const char* why = error.rule == oamod::Rule::not_mapping ? "expected a group of settings"
                                                                 : oamod::rule_message(error.rule);
        refuse(source, "the file", why);
    }
    const oamod::Node* header = oamod::find_entry(root, "oalaunch");
    if (header == nullptr ||
        read_whole(LaunchValue{header, {}}, LaunchPlace{source, "oalaunch"}) != kLaunchVersion)
        refuse(source, "oalaunch", "expected 1");
    LaunchPlan plan;
    read_group(plan, root, source, {});
    if (!plan.content.expansions.empty() && plan.content.base_game.empty())
        refuse(source, "content.base-game", "expected a base game");
    return plan;
}

// ---------------------------------------------------------------------------
// TA Forever's INI

/// Requires a whole number and a reason when it is not one.
///
/// @param text the text
/// @param source the file's name
/// @param key the key as the file wrote it
/// @return the number
int64_t ini_whole(std::string_view text, std::string_view source, std::string_view key) {
    const auto number = whole_number(text);
    if (!number || !fits_int32(*number))
        refuse(source, key, "expected a whole number");
    return *number;
}

/// Stores a whole number that is kept as written.
///
/// @param[out] field the launch field
/// @param text the value
/// @param source the file's name
/// @param key the key
void ini_kept(
    int32_t& field, std::string_view text, std::string_view source, std::string_view key
) {
    field = static_cast<int32_t>(ini_whole(text, source, key));
}

/// Stores a whole number within a range.
///
/// @param[out] field the launch field
/// @param text the value
/// @param source the file's name
/// @param key the key
/// @param lowest the least it may be
/// @param highest the most it may be
void ini_between(
    int32_t& field,
    std::string_view text,
    std::string_view source,
    std::string_view key,
    int64_t lowest,
    int64_t highest
) {
    const int64_t number = ini_whole(text, source, key);
    if (number < lowest)
        refuse(source, key, std::to_string(number) + " is below " + std::to_string(lowest));
    if (number > highest)
        refuse(source, key, std::to_string(number) + " is above " + std::to_string(highest));
    field = static_cast<int32_t>(number);
}

/// Stores a text when it fits the field, including the terminating NUL.
///
/// @param field the field
/// @param text the text
/// @param source the file's name
/// @param key the key
void ini_text(
    std::span<char> field, std::string_view text, std::string_view source, std::string_view key
) {
    if (field.empty() || text.size() >= field.size()) {
        const auto limit = field.empty() ? std::size_t{0} : field.size() - 1;
        refuse(source, key, "is longer than " + std::to_string(limit) + " characters");
    }
    oa::base::text::copy_terminated(field, text);
}

/// What one INI key does with its value.
using IniStore = void (*)(LaunchPlan&, std::string_view, std::string_view, std::string_view);

/// One INI key: the public row and what it stores.
struct IniRule {
    LaunchIniKey row{};
    IniStore store{};
};

// TA Forever's keys and the launch fields they fill, as G52 reads them: the
// numbers TA Forever writes are kept as written where the battle room reads
// only some of them.
constexpr IniRule kIniRules[] = {
    {{"watching",
      "rules.watchers",
      "`2` allows watchers. Any other whole number is kept as written."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.watching, value, source, key);
     }},
    {{"mapmode", "rules.mapping", "`1` starts the map unmapped, `2` mapped. Kept as written."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.mapping, value, source, key);
     }},
    {{"location",
      "rules.start-positions",
      "`1` uses fixed start positions, `2` random. Kept as written."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.location, value, source, key);
     }},
    {{"cheats", "rules.cheats", "`2` allows the cheat codes. `1` turns them off. Kept as written."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.cheats, value, source, key);
     }},
    {{"los", "rules.line-of-sight", "`1` true line of sight, `2` circular, `3` permanent."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_between(plan.block.los, value, source, key, kSightTrue, kSightPermanent);
     }},
    {{"deathopt",
      "rules.commander-death",
      "`1` the commander continues, `2` the game ends, `3` deathmatch."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_between(
             plan.block.death_option, value, source, key, kCommanderContinues, kCommanderDeathmatch
         );
     }},
    {{"metal",
      "rules.metal",
      "Starting metal, from 0 through 10000. 0 leaves the battle room's own amount."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_between(plan.block.metal, value, source, key, 0, kHighestResource);
     }},
    {{"energy",
      "rules.energy",
      "Starting energy, from 0 through 10000. 0 leaves the battle room's own amount."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_between(plan.block.energy, value, source, key, 0, kHighestResource);
     }},
    {{"lockOptions", "rules.lock-options", "`1` locks the battle room's option controls."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         int32_t stored = 0;
         ini_between(stored, value, source, key, 0, std::numeric_limits<int32_t>::max());
         plan.block.lock_options = static_cast<uint32_t>(stored);
     }},
    {{"playerlimit",
      "game.player-limit",
      "Players allowed, from 2 through 10. 0 leaves the session uncapped. 1 is refused."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         if (ini_whole(value, source, key) == 0) {
             plan.block.player_limit = 0;
             return;
         }
         ini_between(
             plan.block.player_limit, value, source, key, kLowestPlayerLimit, kHighestPlayerLimit
         );
     }},
    {{"pfs", {}, "Kept as written and passed on with the score report."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.pfs, value, source, key);
     }},
    {{"timelimit", "rules.time-limit", "Kept as written and passed on with the score report."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.time_limit, value, source, key);
     }},
    {{"session", "game.name", "The game's name, at most 63 characters."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_text(plan.block.session_name, value, source, key);
         plan.game_name = std::string(value);
     }},
    {{"provider",
      "game.launcher-name",
      "The launcher's name on the battle room's previous-menu caption, at most 15 characters."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_text(plan.block.provider_label, value, source, key);
     }},
    {{"mission",
      "map",
      "The map, by the name the list shows or by its key, at most 63 characters."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_text(plan.block.mission, value, source, key);
     }},
    {{"registeredonly", {}, "Read and ignored."},
     [](LaunchPlan&, std::string_view, std::string_view, std::string_view) {}},
    {{"tournment",
      "game.tournament",
      "A tournament game. When it and `tournament` are both written, the later one is kept."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.tournament, value, source, key);
     }},
    {{"tournament",
      "game.tournament",
      "A tournament game. When it and `tournment` are both written, the later one is kept."},
     [](LaunchPlan& plan, std::string_view value, std::string_view source, std::string_view key) {
         ini_kept(plan.block.tournament, value, source, key);
     }},
};

/// The public rows of kIniRules, in its order.
constexpr auto kIniKeys = [] {
    std::array<LaunchIniKey, std::size(kIniRules)> rows{};
    for (std::size_t at = 0; at < rows.size(); ++at)
        rows[at] = kIniRules[at].row;
    return rows;
}();

/// Applies one `[totala]` key through kIniRules.
///
/// @param[in,out] plan the plan
/// @param key the key, in the file's case
/// @param value the value
/// @param source the file's name
void apply_ini_key(
    LaunchPlan& plan, std::string_view key, std::string_view value, std::string_view source
) {
    for (const IniRule& rule : kIniRules)
        if (same_name(key, rule.row.key)) {
            rule.store(plan, value, source, key);
            return;
        }
    refuse(source, key, "is not a launch setting");
}

/// Reads TA Forever's INI. Only `[totala]` is read; other sections are skipped.
///
/// @param source the file's name
/// @param text the file's bytes, without a byte order mark
/// @return the plan, hosting over TCP/IP
LaunchPlan read_ini(std::string_view source, std::string_view text) {
    LaunchPlan plan;
    plan.ini = true;
    plan.role = LaunchRole::host;
    plan.block.hosting = 1;
    plan.block.connection_type = launch::connection_type::tcpip;
    bool in_totala = false;
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const auto end = text.find('\n', cursor);
        auto line = text.substr(
            cursor, end == std::string_view::npos ? text.size() - cursor : end - cursor
        );
        cursor = end == std::string_view::npos ? text.size() + 1 : end + 1;
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#')
            continue;
        if (line.front() == '[') {
            const auto close = line.find(']');
            if (close == std::string_view::npos)
                refuse(source, "the file", "a section has no closing bracket");
            in_totala = same_name(trim(line.substr(1, close - 1)), "totala");
            continue;
        }
        if (!in_totala)
            continue;
        const auto separator = line.find('=');
        if (separator == std::string_view::npos)
            refuse(source, "the file", "a setting needs a value");
        apply_ini_key(
            plan, trim(line.substr(0, separator)), trim(line.substr(separator + 1)), source
        );
    }
    return plan;
}

// ---------------------------------------------------------------------------
// Standard input

/// The time left until a deadline, in whole milliseconds, never below zero.
///
/// @param deadline the deadline
/// @return the milliseconds left
int32_t milliseconds_left(std::chrono::steady_clock::time_point deadline) noexcept {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()
    );
    if (left.count() <= 0)
        return 0;
    return static_cast<int32_t>(
        std::min<int64_t>(left.count(), std::numeric_limits<int32_t>::max())
    );
}

/// The input a look at standard input reads, with how it waits.
class InputReader {
  public:

    /// Looks at the input and tells whether it can hold a launch.
    ///
    /// @param input the input
    explicit InputReader(InputHandle input) noexcept : input_(input) {
#if defined(_WIN32)
        const HANDLE handle = static_cast<HANDLE>(input_);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
            return;
        const DWORD type = GetFileType(handle);
        if (type == FILE_TYPE_PIPE) {
            pipe_ = true;
            usable_ = true;
        } else if (type == FILE_TYPE_DISK) {
            DWORD high = 0;
            const DWORD low = GetFileSize(handle, &high);
            usable_ = !(low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) &&
                      (low != 0 || high != 0);
        }
#else
        if (input_ < 0 || ::isatty(input_) != 0)
            return;
        struct stat info{};
        if (::fstat(input_, &info) != 0)
            return;
        if (S_ISREG(info.st_mode)) {
            usable_ = info.st_size > 0;
        } else if (S_ISFIFO(info.st_mode) || S_ISSOCK(info.st_mode)) {
            pipe_ = true;
            usable_ = true;
        }
#endif
    }

    /// Tells whether the input is a pipe, or a file with something in it.
    ///
    /// @return true when it may hold a launch
    [[nodiscard]] bool usable() const noexcept { return usable_; }

    /// Waits until the input has something to read, or has ended.
    ///
    /// @param deadline the latest time to wait until
    /// @return false when the deadline came first
    [[nodiscard]] bool wait(std::chrono::steady_clock::time_point deadline) const noexcept {
        if (!pipe_)
            return true;
#if defined(_WIN32)
        const HANDLE handle = static_cast<HANDLE>(input_);
        constexpr DWORD kPollMilliseconds = 5;
        while (true) {
            DWORD available = 0;
            if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) == FALSE)
                return true; // the writer has gone: the read reports the end
            if (available != 0)
                return true;
            if (milliseconds_left(deadline) == 0)
                return false;
            Sleep(kPollMilliseconds);
        }
#else
        while (true) {
            pollfd watched{};
            watched.fd = input_;
            watched.events = POLLIN;
            const int ready = ::poll(&watched, 1, milliseconds_left(deadline));
            if (ready > 0)
                return true;
            if (ready == 0)
                return false;
            if (errno != EINTR)
                return true; // the read reports what went wrong
        }
#endif
    }

    /// Reads what the input has, after wait said it has something.
    ///
    /// @param[out] buffer where the bytes go
    /// @return how many bytes were read; 0 at the input's end; -1 when it cannot be read
    [[nodiscard]] int64_t read(std::span<char> buffer) const noexcept {
#if defined(_WIN32)
        const HANDLE handle = static_cast<HANDLE>(input_);
        DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), 0x10000));
        if (pipe_) {
            DWORD available = 0;
            if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) != FALSE &&
                available != 0)
                wanted = std::min(wanted, available);
        }
        DWORD got = 0;
        if (ReadFile(handle, buffer.data(), wanted, &got, nullptr) == FALSE)
            return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
        return static_cast<int64_t>(got);
#else
        while (true) {
            const auto got = ::read(input_, buffer.data(), buffer.size());
            if (got >= 0)
                return static_cast<int64_t>(got);
            if (errno != EINTR)
                return -1;
        }
#endif
    }

  private:

    InputHandle input_;
    bool pipe_ = false;
    bool usable_ = false;
};

} // namespace

LaunchRefusal::LaunchRefusal(const std::string& message) : std::runtime_error(message) {
}

std::span<const LaunchField> launch_fields() noexcept {
    return kFields;
}

const LaunchField* find_launch_field(std::string_view field) noexcept {
    for (const LaunchField& entry : kFields)
        if (entry.field == field)
            return &entry;
    return nullptr;
}

const LaunchField* find_launch_option(std::string_view option) noexcept {
    const auto equals = option.find('=');
    const std::string_view name = option.substr(0, equals);
    for (const LaunchField& entry : kFields)
        if (entry.reader == LaunchOptionReader::network_play && option_name(entry.option) == name) {
            const bool takes_suffix = entry.option.find("[=") != std::string_view::npos;
            if (equals != std::string_view::npos && !takes_suffix)
                return nullptr;
            return &entry;
        }
    return nullptr;
}

bool launch_option_takes_value(std::string_view option) noexcept {
    const LaunchField* field = find_launch_option(option);
    return field != nullptr && field->option.find("[=") == std::string_view::npos;
}

std::span<const LaunchIniKey> launch_ini_keys() noexcept {
    return kIniKeys;
}

LaunchFileKind launch_file_kind(std::string_view text) noexcept {
    text = without_byte_order_mark(text);
    bool first_line = true;
    bool totala = false;
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const auto end = text.find('\n', cursor);
        const auto line = trim(
            text.substr(cursor, end == std::string_view::npos ? text.size() - cursor : end - cursor)
        );
        cursor = end == std::string_view::npos ? text.size() + 1 : end + 1;
        if (line.empty())
            continue;
        if (first_line && line.front() != '#') {
            if (line.starts_with(kHeader))
                return LaunchFileKind::open_annihilation;
            first_line = false;
        }
        if (line.front() == '[') {
            const auto close = line.find(']');
            if (close != std::string_view::npos &&
                same_name(trim(line.substr(1, close - 1)), "totala"))
                totala = true;
        }
    }
    return totala ? LaunchFileKind::ta_forever : LaunchFileKind::neither;
}

LaunchPlan read_launch_text(std::string_view source, std::string_view text) {
    LaunchPlan plan;
    switch (launch_file_kind(text)) {
    case LaunchFileKind::open_annihilation:
        plan = read_yaml(source, without_byte_order_mark(text));
        break;
    case LaunchFileKind::ta_forever:
        plan = read_ini(source, without_byte_order_mark(text));
        break;
    case LaunchFileKind::neither:
        refuse(
            source,
            "the file",
            "is neither a launch file, which begins with oalaunch: 1, nor TA Forever's settings, "
            "which have a [totala] section"
        );
    }
    plan.source = std::string(source);
    return plan;
}

LaunchPlan read_launch_file(std::string_view path) {
    std::string bytes;
    std::string_view source = path;
    if (path == "-") {
        source = kStandardInputName;
        bytes.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    } else {
        const fs::path file = path_from_utf8(path);
        std::ifstream input(file, std::ios::in | std::ios::binary);
        if (!input)
            refuse(path, "the file", "cannot be read");
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    if (bytes.size() > kLaunchFileLimit)
        refuse(source, "the file", "is longer than " + std::to_string(kLaunchFileLimit) + " bytes");
    return read_launch_text(source, bytes);
}

void replace_launch_field(
    LaunchPlan& plan, const LaunchField& field, std::string_view option, std::string_view text
) {
    const std::string name(option_name(option));
    const auto replaced =
        std::find_if(plan.replaced.begin(), plan.replaced.end(), [&field](const auto& entry) {
            return entry.first == field.field;
        });
    // The first option for the computer players, or for the restrictions,
    // replaces the file's list; each adds one.
    if (field.store == store_computers && replaced == plan.replaced.end())
        plan.block.computer_count = 0;
    if (field.store == store_restrictions && replaced == plan.replaced.end())
        plan.block.restriction_count = 0;
    std::string_view value = text;
    if (const auto equals = option.find('='); equals != std::string_view::npos)
        value = option.substr(equals + 1);
    else if (!launch_option_takes_value(option))
        value = "on";
    field.store(plan, LaunchValue{nullptr, value}, LaunchPlace{{}, name});
    if (replaced == plan.replaced.end())
        plan.replaced.emplace_back(std::string(field.field), name);
    else
        replaced->second = name;
}

std::string launch_place(const LaunchPlan& plan, std::string_view field) {
    for (const auto& [replaced_field, option] : plan.replaced)
        if (replaced_field == field)
            return option;
    if (plan.source.empty())
        return std::string(field);
    return plan.source + ": " + std::string(field);
}

void check_launch_plan(const LaunchPlan& plan) {
    const auto& block = plan.block;
    const auto computers_refused = [&plan](std::string_view why) {
        throw LaunchRefusal(launch_place(plan, "computer-players") + ": " + std::string(why));
    };
    if (block.computer_count > 0) {
        if (block.death_option == kCommanderDeathmatch)
            computers_refused("a deathmatch (rules.commander-death) seats no computer players");
        if (block.tournament != 0)
            computers_refused("a tournament game (game.tournament) seats no computer players");
        if (block.player_limit > 0 && block.computer_count + 1 > block.player_limit)
            computers_refused(
                std::to_string(block.computer_count) +
                " computer players and the player are more than the player limit of " +
                std::to_string(block.player_limit)
            );
    }
    if (plan.role == LaunchRole::join && plan.address.empty())
        throw LaunchRefusal(
            launch_place(plan, "game.address") + ": a player who joins needs the game's address"
        );
    if (plan.role == LaunchRole::host && block.watcher != 0 && block.watching == kChoiceOff)
        throw LaunchRefusal(
            launch_place(plan, "player.side") + ": a host who watches needs rules.watchers on"
        );
}

InputHandle standard_input() noexcept {
#if defined(_WIN32)
    return GetStdHandle(STD_INPUT_HANDLE);
#else
    return STDIN_FILENO;
#endif
}

PipedLaunch read_piped_launch(
    InputHandle input,
    std::chrono::milliseconds first_byte_wait,
    std::chrono::milliseconds rest_wait
) {
    const InputReader reader(input);
    if (!reader.usable())
        return {};
    auto deadline = std::chrono::steady_clock::now() + first_byte_wait;
    std::string text;
    // The header, after a byte order mark, is read one byte at a time, so
    // that input meant for another program loses no more than the bytes up to
    // the first that differs.
    const auto next_byte = [&reader, &deadline, &text](char& byte) {
        if (!reader.wait(deadline) || reader.read(std::span<char>(&byte, 1)) != 1)
            return false;
        text.push_back(byte);
        return true;
    };
    char byte = 0;
    if (!next_byte(byte))
        return {};
    deadline = std::chrono::steady_clock::now() + rest_wait;
    if (byte == kByteOrderMark.front()) {
        for (std::size_t at = 1; at < kByteOrderMark.size(); ++at)
            if (!next_byte(byte) || byte != kByteOrderMark[at])
                return {};
        if (!next_byte(byte))
            return {};
    }
    for (std::size_t matched = 0;;) {
        if (byte != kHeader[matched])
            return {};
        if (++matched == kHeader.size())
            break;
        if (!next_byte(byte))
            return {};
    }
    std::array<char, 4096> buffer{};
    while (true) {
        if (!reader.wait(deadline))
            refuse(
                kStandardInputName,
                "the file",
                "did not end within " + std::to_string(rest_wait.count()) + " milliseconds"
            );
        const int64_t got = reader.read(buffer);
        if (got < 0)
            refuse(kStandardInputName, "the file", "cannot be read");
        if (got == 0)
            break;
        text.append(buffer.data(), static_cast<std::size_t>(got));
        if (text.size() > kLaunchFileLimit)
            refuse(
                kStandardInputName,
                "the file",
                "is longer than " + std::to_string(kLaunchFileLimit) + " bytes"
            );
    }
    PipedLaunch piped;
    piped.found = true;
    piped.text = std::move(text);
    return piped;
}

namespace {

/// The status block network play kept for this run.
///
/// @return the block
LaunchStatusChoice& kept_status() {
    static LaunchStatusChoice kept;
    return kept;
}

} // namespace

void keep_launch_status(const LaunchStatusChoice& status) {
    kept_status() = status;
}

const LaunchStatusChoice& kept_launch_status() noexcept {
    return kept_status();
}

} // namespace oa::app::netgame
