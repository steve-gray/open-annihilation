// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// TA Forever's INI and Open Annihilation's YAML, both as one launch plan.
#include "oa/app/netgame/launch_file.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/base/text.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/netgame/dplay.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app::netgame {
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

/// Throws a refusal that names the file, the field and why.
///
/// @param source the file's name
/// @param field the setting
/// @param why why it cannot be used
[[noreturn]] void refuse(std::string_view source, std::string_view field, std::string_view why) {
    throw LaunchRefusal(std::string(source) + ": " + std::string(field) + ": " + std::string(why));
}

/// Tells whether a character is a space the INI reader skips.
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

/// Stores a text when it fits the field, including the terminating NUL.
///
/// @param field the field
/// @param text the text
/// @param source the file's name
/// @param name the setting's name
void store_text(
    std::span<char> field, std::string_view text, std::string_view source, std::string_view name
) {
    if (field.empty() || text.size() >= field.size()) {
        const auto limit = field.empty() ? std::size_t{0} : field.size() - 1;
        refuse(source, name, "is longer than " + std::to_string(limit) + " characters");
    }
    oa::base::text::copy_terminated(field, text);
}

/// Requires a whole number and a reason when it is not one.
///
/// @param text the text
/// @param source the file's name
/// @param name the setting's name
/// @return the number
int64_t require_whole(std::string_view text, std::string_view source, std::string_view name) {
    const auto number = whole_number(text);
    if (!number || !fits_int32(*number))
        refuse(source, name, "expected a whole number");
    return *number;
}

/// Stores metal or energy. Zero leaves the battle room's own amount.
///
/// @param[out] field the launch field
/// @param number the amount
/// @param source the file's name
/// @param name the setting's name
void store_resource(
    int32_t& field, int64_t number, std::string_view source, std::string_view name
) {
    if (number < 0)
        refuse(source, name, std::to_string(number) + " is below 0");
    if (number > kHighestResource)
        refuse(
            source, name, std::to_string(number) + " is above " + std::to_string(kHighestResource)
        );
    field = static_cast<int32_t>(number);
}

/// Stores a unit limit the battle room can show.
///
/// @param[out] field the launch field
/// @param number the limit
/// @param source the file's name
/// @param name the setting's name
void store_units(int32_t& field, int64_t number, std::string_view source, std::string_view name) {
    if (number < kLowestUnitLimit)
        refuse(
            source, name, std::to_string(number) + " is below " + std::to_string(kLowestUnitLimit)
        );
    if (number > kHighestUnitLimit)
        refuse(
            source, name, std::to_string(number) + " is above " + std::to_string(kHighestUnitLimit)
        );
    field = static_cast<int32_t>(number);
}

/// Stores a player limit. An INI may say 0, which leaves the session uncapped.
///
/// @param[out] field the launch field
/// @param number the limit
/// @param source the file's name
/// @param name the setting's name
/// @param allow_unset true when 0 means no cap
void store_players(
    int32_t& field, int64_t number, std::string_view source, std::string_view name, bool allow_unset
) {
    if (allow_unset && number == 0) {
        field = 0;
        return;
    }
    if (number < 0)
        refuse(source, name, std::to_string(number) + " is below 0");
    if (number < kLowestPlayerLimit)
        refuse(
            source, name, std::to_string(number) + " is below " + std::to_string(kLowestPlayerLimit)
        );
    if (number > kHighestPlayerLimit)
        refuse(
            source,
            name,
            std::to_string(number) + " is above " + std::to_string(kHighestPlayerLimit)
        );
    field = static_cast<int32_t>(number);
}

/// Stores a commander rule or a line of sight, each 1, 2 or 3.
///
/// @param[out] field the launch field
/// @param number the code
/// @param source the file's name
/// @param name the setting's name
void store_one_to_three(
    int32_t& field, int64_t number, std::string_view source, std::string_view name
) {
    if (number < 1)
        refuse(source, name, std::to_string(number) + " is below 1");
    if (number > 3)
        refuse(source, name, std::to_string(number) + " is above 3");
    field = static_cast<int32_t>(number);
}

/// Stores any whole number a score report can pass on.
///
/// @param[out] field the launch field
/// @param number the number
/// @param source the file's name
/// @param name the setting's name
void store_kept(int32_t& field, int64_t number, std::string_view source, std::string_view name) {
    if (!fits_int32(number))
        refuse(source, name, "expected a whole number");
    field = static_cast<int32_t>(number);
}

/// Applies one `[totala]` key. `registeredonly` is read and ignored.
///
/// @param[in,out] plan the plan
/// @param key the key, in the file's case
/// @param value the value
/// @param source the file's name
void apply_ini_key(
    LaunchPlan& plan, std::string_view key, std::string_view value, std::string_view source
) {
    auto& block = plan.block;
    const std::string name(key);
    if (same_name(key, "registeredonly"))
        return;
    if (same_name(key, "watching"))
        store_kept(block.watching, require_whole(value, source, name), source, name);
    else if (same_name(key, "mapmode"))
        store_kept(block.mapping, require_whole(value, source, name), source, name);
    else if (same_name(key, "location"))
        store_kept(block.location, require_whole(value, source, name), source, name);
    else if (same_name(key, "cheats"))
        store_kept(block.cheats, require_whole(value, source, name), source, name);
    else if (same_name(key, "los"))
        store_one_to_three(block.los, require_whole(value, source, name), source, name);
    else if (same_name(key, "deathopt"))
        store_one_to_three(block.death_option, require_whole(value, source, name), source, name);
    else if (same_name(key, "metal"))
        store_resource(block.metal, require_whole(value, source, name), source, name);
    else if (same_name(key, "energy"))
        store_resource(block.energy, require_whole(value, source, name), source, name);
    else if (same_name(key, "lockoptions")) {
        int32_t stored = 0;
        store_kept(stored, require_whole(value, source, name), source, name);
        if (stored < 0)
            refuse(source, name, std::to_string(stored) + " is below 0");
        block.lock_options = static_cast<uint32_t>(stored);
    } else if (same_name(key, "playerlimit"))
        store_players(block.player_limit, require_whole(value, source, name), source, name, true);
    else if (same_name(key, "pfs"))
        store_kept(block.pfs, require_whole(value, source, name), source, name);
    else if (same_name(key, "timelimit"))
        store_kept(block.time_limit, require_whole(value, source, name), source, name);
    else if (same_name(key, "tournament") || same_name(key, "tournment"))
        store_kept(block.tournament, require_whole(value, source, name), source, name);
    else if (same_name(key, "session")) {
        store_text(block.session_name, value, source, name);
        plan.game_name = std::string(value);
    } else if (same_name(key, "provider"))
        store_text(block.provider_label, value, source, name);
    else if (same_name(key, "mission"))
        store_text(block.mission, value, source, name);
    else
        refuse(source, name, "is not a launch setting");
}

/// Reads TA Forever's INI. Only `[totala]` is read; other sections are skipped.
///
/// @param source the file's name
/// @param text the file's bytes
/// @return the plan, hosting over TCP/IP
LaunchPlan read_ini(std::string_view source, std::string_view text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb && static_cast<unsigned char>(text[2]) == 0xbf)
        text.remove_prefix(3);
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
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        line = trim(line);
        if (end == std::string_view::npos)
            cursor = text.size() + 1;
        else
            cursor = end + 1;
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

/// Requires a YAML value to be a mapping.
///
/// @param node the value
/// @param source the file's name
/// @param name the setting's name
/// @return the mapping
const oamod::Node&
require_group(const oamod::Node& node, std::string_view source, std::string_view name) {
    if (node.kind != oamod::NodeKind::mapping)
        refuse(source, name, "expected a group of settings");
    return node;
}

/// Requires a YAML string.
///
/// @param node the value
/// @param source the file's name
/// @param name the setting's name
/// @return the text
std::string_view
require_text(const oamod::Node& node, std::string_view source, std::string_view name) {
    if (node.kind != oamod::NodeKind::string)
        refuse(source, name, "expected text");
    return node.text;
}

/// Requires a YAML whole number.
///
/// @param node the value
/// @param source the file's name
/// @param name the setting's name
/// @return the number
int64_t require_number(const oamod::Node& node, std::string_view source, std::string_view name) {
    if (node.kind != oamod::NodeKind::number)
        refuse(source, name, "expected a whole number");
    int64_t number = 0;
    if (!oamod::integer_value(node.number, number) || !fits_int32(number))
        refuse(source, name, "expected a whole number");
    return number;
}

/// Requires a YAML boolean.
///
/// @param node the value
/// @param source the file's name
/// @param name the setting's name
/// @return the value
bool require_bool(const oamod::Node& node, std::string_view source, std::string_view name) {
    if (node.kind != oamod::NodeKind::boolean)
        refuse(source, name, "expected true or false");
    return node.boolean;
}

/// Refuses a key the group does not use.
///
/// @param group the mapping
/// @param allowed the keys it uses
/// @param source the file's name
/// @param prefix the group's dotted name; empty at the top
void require_known(
    const oamod::Node& group,
    std::span<const std::string_view> allowed,
    std::string_view source,
    std::string_view prefix
) {
    for (const oamod::Node& child : group.children) {
        if (child.key.kind != oamod::KeyKind::string)
            refuse(
                source,
                prefix.empty() ? std::string_view{"the document"} : prefix,
                "a key is not text"
            );
        bool known = false;
        for (const std::string_view key : allowed)
            if (child.key.text == key)
                known = true;
        if (!known) {
            const std::string field =
                prefix.empty() ? child.key.text : std::string(prefix) + "." + child.key.text;
            refuse(source, field, "is not a launch setting");
        }
    }
}

/// Stores a word as one of the choices, or refuses it.
///
/// @param word the text
/// @param choices the words and the codes they store, in pairs of pointer and code
/// @param source the file's name
/// @param name the setting's name
/// @param why what the field expected
/// @return the code
int32_t choice_code(
    std::string_view word,
    std::span<const std::pair<std::string_view, int32_t>> choices,
    std::string_view source,
    std::string_view name,
    std::string_view why
) {
    for (const auto& choice : choices)
        if (word == choice.first)
            return choice.second;
    refuse(source, name, why);
}

/// Reads the game group.
///
/// @param[in,out] plan the plan
/// @param group the game mapping
/// @param source the file's name
void read_game(LaunchPlan& plan, const oamod::Node& group, std::string_view source) {
    static constexpr std::string_view keys[] = {
        "role", "address", "name", "player", "password", "dplay"
    };
    require_known(group, keys, source, "game");
    auto& block = plan.block;
    if (const oamod::Node* role = oamod::find_entry(group, "role")) {
        const auto word = require_text(*role, source, "game.role");
        if (word == "host")
            plan.role = LaunchRole::host;
        else if (word == "join")
            plan.role = LaunchRole::join;
        else
            refuse(source, "game.role", "expected host or join");
    }
    if (const oamod::Node* address = oamod::find_entry(group, "address")) {
        const auto text = require_text(*address, source, "game.address");
        store_text(block.address, text, source, "game.address");
        plan.address = std::string(text);
    }
    if (const oamod::Node* name = oamod::find_entry(group, "name")) {
        const auto text = require_text(*name, source, "game.name");
        store_text(block.session_name, text, source, "game.name");
        plan.game_name = std::string(text);
    }
    if (const oamod::Node* player = oamod::find_entry(group, "player")) {
        const auto text = require_text(*player, source, "game.player");
        store_text(block.user_name, text, source, "game.player");
        plan.player_name = std::string(text);
    }
    if (const oamod::Node* password = oamod::find_entry(group, "password")) {
        const auto text = require_text(*password, source, "game.password");
        store_text(block.password, text, source, "game.password");
        plan.password = std::string(text);
    }
    if (const oamod::Node* dplay = oamod::find_entry(group, "dplay")) {
        const oamod::Node& play = require_group(*dplay, source, "game.dplay");
        static constexpr std::string_view play_keys[] = {"guid", "port"};
        require_known(play, play_keys, source, "game.dplay");
        if (const oamod::Node* guid = oamod::find_entry(play, "guid")) {
            const auto text = require_text(*guid, source, "game.dplay.guid");
            std::array<uint8_t, 16> bytes{};
            if (!oa::netgame::parse_application_guid(text, bytes.data()))
                refuse(source, "game.dplay.guid", "expected a GUID, with or without braces");
            plan.dplay_guid = bytes;
        }
        if (const oamod::Node* port = oamod::find_entry(play, "port")) {
            const auto number = require_number(*port, source, "game.dplay.port");
            if (number < 1 || number > 65535)
                refuse(source, "game.dplay.port", "expected a port from 1 through 65535");
            plan.dplay_port = static_cast<uint16_t>(number);
        }
    }
    if (plan.role == LaunchRole::host)
        block.hosting = 1;
    else if (plan.role == LaunchRole::join)
        block.hosting = 0;
    if (plan.role != LaunchRole::unset)
        block.connection_type = launch::connection_type::tcpip;
}

/// Reads the content group.
///
/// @param[in,out] plan the plan
/// @param group the content mapping
/// @param source the file's name
void read_content(LaunchPlan& plan, const oamod::Node& group, std::string_view source) {
    static constexpr std::string_view keys[] = {"base-game", "expansions", "mod"};
    require_known(group, keys, source, "content");
    if (const oamod::Node* base = oamod::find_entry(group, "base-game")) {
        plan.content.base_game = std::string(require_text(*base, source, "content.base-game"));
        plan.content.named = true;
    }
    if (const oamod::Node* expansions = oamod::find_entry(group, "expansions")) {
        if (expansions->kind != oamod::NodeKind::sequence)
            refuse(source, "content.expansions", "expected a list");
        if (plan.content.base_game.empty())
            refuse(source, "content.base-game", "expected a base game");
        for (const oamod::Node& item : expansions->children) {
            if (item.kind != oamod::NodeKind::string)
                refuse(source, "content.expansions", "expected text");
            plan.content.expansions.push_back(item.text);
        }
        plan.content.named = true;
    }
    if (const oamod::Node* mod = oamod::find_entry(group, "mod")) {
        plan.content.mod = std::string(require_text(*mod, source, "content.mod"));
        plan.content.named = true;
    }
}

/// Reads the rules group.
///
/// A boolean false is stored as 1 and true as 2, so the battle room treats
/// each as set. Zero would leave the room's own setting.
///
/// @param[in,out] plan the plan
/// @param group the rules mapping
/// @param source the file's name
void read_rules(LaunchPlan& plan, const oamod::Node& group, std::string_view source) {
    static constexpr std::string_view keys[] = {
        "commander",
        "metal",
        "energy",
        "unit-limit",
        "line-of-sight",
        "mapping",
        "start-positions",
        "cheats",
        "watchers",
        "player-limit",
        "time-limit",
    };
    require_known(group, keys, source, "rules");
    auto& block = plan.block;
    if (const oamod::Node* commander = oamod::find_entry(group, "commander")) {
        static constexpr std::pair<std::string_view, int32_t> words[] = {
            {"continues", 1},
            {"ends", 2},
            {"deathmatch", 3},
        };
        block.death_option = choice_code(
            require_text(*commander, source, "rules.commander"),
            words,
            source,
            "rules.commander",
            "expected continues, ends or deathmatch"
        );
    }
    if (const oamod::Node* metal = oamod::find_entry(group, "metal"))
        store_resource(
            block.metal, require_number(*metal, source, "rules.metal"), source, "rules.metal"
        );
    if (const oamod::Node* energy = oamod::find_entry(group, "energy"))
        store_resource(
            block.energy, require_number(*energy, source, "rules.energy"), source, "rules.energy"
        );
    if (const oamod::Node* units = oamod::find_entry(group, "unit-limit"))
        store_units(
            block.max_units,
            require_number(*units, source, "rules.unit-limit"),
            source,
            "rules.unit-limit"
        );
    if (const oamod::Node* sight = oamod::find_entry(group, "line-of-sight")) {
        if (sight->kind == oamod::NodeKind::boolean) {
            if (!sight->boolean)
                refuse(source, "rules.line-of-sight", "expected true, circular or permanent");
            block.los = 1;
        } else {
            static constexpr std::pair<std::string_view, int32_t> words[] = {
                {"true", 1},
                {"circular", 2},
                {"permanent", 3},
            };
            block.los = choice_code(
                require_text(*sight, source, "rules.line-of-sight"),
                words,
                source,
                "rules.line-of-sight",
                "expected true, circular or permanent"
            );
        }
    }
    if (const oamod::Node* mapping = oamod::find_entry(group, "mapping")) {
        static constexpr std::pair<std::string_view, int32_t> words[] = {
            {"mapped", 2}, {"unmapped", 1}
        };
        block.mapping = choice_code(
            require_text(*mapping, source, "rules.mapping"),
            words,
            source,
            "rules.mapping",
            "expected mapped or unmapped"
        );
    }
    if (const oamod::Node* starts = oamod::find_entry(group, "start-positions")) {
        static constexpr std::pair<std::string_view, int32_t> words[] = {
            {"random", 2}, {"fixed", 1}
        };
        block.location = choice_code(
            require_text(*starts, source, "rules.start-positions"),
            words,
            source,
            "rules.start-positions",
            "expected random or fixed"
        );
    }
    if (const oamod::Node* cheats = oamod::find_entry(group, "cheats"))
        block.cheats = require_bool(*cheats, source, "rules.cheats") ? 2 : 1;
    if (const oamod::Node* watchers = oamod::find_entry(group, "watchers"))
        block.watching = require_bool(*watchers, source, "rules.watchers") ? 2 : 1;
    if (const oamod::Node* players = oamod::find_entry(group, "player-limit"))
        store_players(
            block.player_limit,
            require_number(*players, source, "rules.player-limit"),
            source,
            "rules.player-limit",
            false
        );
    if (const oamod::Node* time_limit = oamod::find_entry(group, "time-limit"))
        store_kept(
            block.time_limit,
            require_number(*time_limit, source, "rules.time-limit"),
            source,
            "rules.time-limit"
        );
}

/// Reads the seat group. `random` and `none` store no side and no team.
///
/// @param[in,out] plan the plan
/// @param group the seat mapping
/// @param source the file's name
void read_seat(LaunchPlan& plan, const oamod::Node& group, std::string_view source) {
    static constexpr std::string_view keys[] = {"side", "team"};
    require_known(group, keys, source, "seat");
    if (const oamod::Node* side = oamod::find_entry(group, "side")) {
        static constexpr std::pair<std::string_view, int32_t> words[] = {
            {"arm", launch::faction::arm},
            {"core", launch::faction::core},
            {"random", launch::faction::none},
        };
        plan.block.faction = choice_code(
            require_text(*side, source, "seat.side"),
            words,
            source,
            "seat.side",
            "expected arm, core or random"
        );
    }
    if (const oamod::Node* team = oamod::find_entry(group, "team")) {
        if (team->kind == oamod::NodeKind::string) {
            if (team->text != "none")
                refuse(source, "seat.team", "expected 1 to 5 or none");
            plan.block.team = 0;
        } else {
            const auto number = require_number(*team, source, "seat.team");
            if (number < 1 || number > 5)
                refuse(source, "seat.team", "expected 1 to 5 or none");
            plan.block.team = static_cast<int32_t>(number);
        }
    }
}

/// Reads the status group. It is kept for a later change to act on.
///
/// @param[in,out] plan the plan
/// @param group the status mapping
/// @param source the file's name
void read_status(LaunchPlan& plan, const oamod::Node& group, std::string_view source) {
    static constexpr std::string_view keys[] = {"stdout", "file", "quit-after-game"};
    require_known(group, keys, source, "status");
    if (const oamod::Node* stdout_value = oamod::find_entry(group, "stdout")) {
        plan.status.standard_output = require_bool(*stdout_value, source, "status.stdout");
        plan.status.stdout_named = true;
    }
    if (const oamod::Node* file = oamod::find_entry(group, "file"))
        plan.status.file = std::string(require_text(*file, source, "status.file"));
    if (const oamod::Node* quit = oamod::find_entry(group, "quit-after-game")) {
        if (quit->kind == oamod::NodeKind::boolean)
            plan.status.quit_after = quit->boolean ? QuitAfter::game : QuitAfter::off;
        else if (quit->kind == oamod::NodeKind::string && quit->text == "after-score")
            plan.status.quit_after = QuitAfter::after_score;
        else
            refuse(source, "status.quit-after-game", "expected true, false or after-score");
    }
}

/// Reads Open Annihilation's YAML with the mod profile's reader.
///
/// @param source the file's name
/// @param text the file's bytes
/// @return the plan
LaunchPlan read_yaml(std::string_view source, std::string_view text) {
    oamod::Node root;
    oamod::ReadError error;
    const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
    if (!oamod::read_document(std::span<const uint8_t>(bytes, text.size()), root, error)) {
        const char* why = error.rule == oamod::Rule::not_mapping ? "expected a group of settings"
                                                                 : oamod::rule_message(error.rule);
        refuse(source, "the document", why);
    }
    static constexpr std::string_view keys[] = {
        "oalaunch",
        "game",
        "content",
        "map",
        "rules",
        "lock-options",
        "seat",
        "status",
    };
    require_known(root, keys, source, {});
    LaunchPlan plan;
    const oamod::Node* header = oamod::find_entry(root, "oalaunch");
    if (header == nullptr || require_number(*header, source, "oalaunch") != 1)
        refuse(source, "oalaunch", "expected 1");
    if (const oamod::Node* game = oamod::find_entry(root, "game"))
        read_game(plan, require_group(*game, source, "game"), source);
    if (const oamod::Node* content = oamod::find_entry(root, "content"))
        read_content(plan, require_group(*content, source, "content"), source);
    if (const oamod::Node* map = oamod::find_entry(root, "map"))
        store_text(plan.block.mission, require_text(*map, source, "map"), source, "map");
    if (const oamod::Node* rules = oamod::find_entry(root, "rules"))
        read_rules(plan, require_group(*rules, source, "rules"), source);
    if (const oamod::Node* lock = oamod::find_entry(root, "lock-options"))
        plan.block.lock_options = require_bool(*lock, source, "lock-options") ? 1 : 0;
    if (const oamod::Node* seat = oamod::find_entry(root, "seat"))
        read_seat(plan, require_group(*seat, source, "seat"), source);
    if (const oamod::Node* status = oamod::find_entry(root, "status"))
        read_status(plan, require_group(*status, source, "status"), source);
    return plan;
}

} // namespace

LaunchRefusal::LaunchRefusal(const std::string& message) : std::runtime_error(message) {
}

LaunchPlan read_launch_text(std::string_view source, std::string_view text, bool yaml) {
    LaunchPlan plan = yaml ? read_yaml(source, text) : read_ini(source, text);
    plan.source = std::string(source);
    return plan;
}

LaunchPlan read_launch_file(std::string_view path, bool yaml) {
    std::string bytes;
    if (path == "-") {
        bytes.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    } else {
        const fs::path file = path_from_utf8(path);
        std::ifstream input(file, std::ios::in | std::ios::binary);
        if (!input)
            refuse(path, "the file", "cannot be read");
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    return read_launch_text(path, bytes, yaml);
}

} // namespace oa::app::netgame
