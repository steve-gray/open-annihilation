// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/netgame/launch_switches.hpp"
#include "oa/base/text.hpp"

#include <cstring>
#include <string>

namespace oa::app::netgame::launch {
namespace {

namespace cl = oa::app::command_line;

// The launch block's codes the "-b" words set, as a launch file's keys set them.
constexpr int32_t kCommanderContinues = 1;
constexpr int32_t kCommanderEnds = 2;
constexpr int32_t kCommanderDeathmatch = 3;
constexpr int32_t kSightTrue = 1;
constexpr int32_t kSightCircular = 2;
constexpr int32_t kSightPermanent = 3;
constexpr int32_t kStartsFixed = 1;   // LaunchBlock::location
constexpr int32_t kMapMapped = 2;     // LaunchBlock::mapping
constexpr int32_t kChoiceAllowed = 2; // LaunchBlock::cheats and watching
constexpr uint32_t kOptionsLocked = 1;

/// Which battle room option a "-b" word sets.
enum class BattleOption : uint8_t { lock, commander, sight, starts, mapping, cheats, watching };

/// One "-b" word and the option and code it sets.
struct BattleWord {
    const char* word{};
    BattleOption option{};
    int32_t code{};
};

// Every "-b" word, with the meaning its name gives it.
constexpr BattleWord kBattleWords[] = {
    {"lock", BattleOption::lock, static_cast<int32_t>(kOptionsLocked)},
    {"deathplays", BattleOption::commander, kCommanderContinues},
    {"deathends", BattleOption::commander, kCommanderEnds},
    {"deathmatch", BattleOption::commander, kCommanderDeathmatch},
    {"truelos", BattleOption::sight, kSightTrue},
    {"circlos", BattleOption::sight, kSightCircular},
    {"permlos", BattleOption::sight, kSightPermanent},
    {"mapping", BattleOption::mapping, kMapMapped},
    {"fixedloc", BattleOption::starts, kStartsFixed},
    {"cheating", BattleOption::cheats, kChoiceAllowed},
    {"watching", BattleOption::watching, kChoiceAllowed},
};

/// Lowers an ASCII capital letter; other characters are returned unchanged.
///
/// @param c Character.
/// @return The lower-case character.
char fold(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

/// Compares two texts, ignoring ASCII case.
///
/// @param a First text.
/// @param b Second text.
/// @return Whether they are the same letters.
bool same_word(const char* a, const char* b) noexcept {
    for (; *a != '\0' && fold(*a) == fold(*b); ++a, ++b) {
    }
    return fold(*a) == fold(*b);
}

/// Tests for the C locale's space class: blank, tab, newline, vertical tab, form feed and return.
///
/// @param c Character.
/// @return Whether `c` is a space.
bool is_space(char c) noexcept {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/// Reads a decimal integer: leading space, an optional sign and decimal digits.
///
/// @param text Text to read; reading stops at the first other character.
/// @return The value, wrapping at 32 bits; 0 without digits.
int32_t decode_int(const char* text) noexcept {
    while (is_space(*text))
        ++text;
    const char sign = *text;
    if (sign == '-' || sign == '+')
        ++text;
    uint32_t value = 0;
    for (; *text >= '0' && *text <= '9'; ++text)
        value = value * 10U + static_cast<uint32_t>(*text - '0');
    return static_cast<int32_t>(sign == '-' ? 0U - value : value);
}

/// Copies text into a text field, truncated to kTextBytes - 1 characters.
///
/// @param[out] destination Field receiving the text.
/// @param source Text to copy.
void copy_text(char (&destination)[kTextBytes], const char* source) noexcept {
    oa::base::text::copy_terminated(destination, source);
}

/// Appends text to a text field, stopping at the end of the field.
///
/// At most 63 characters are appended, and the text stops at the end of the
/// field, however many appends came before.
///
/// @param[in,out] destination Field the text is appended to.
/// @param source Text to append.
void append_text(char (&destination)[kTextBytes], const char* source) noexcept {
    oa::base::text::append_terminated(destination, source);
}

/// Returns a switch's argument: the text after the two switch characters, or the next token when none follows.
///
/// @param arguments The switch's arguments.
/// @return The argument, or null when no token follows.
const char* argument(const cl::SwitchArguments* arguments) noexcept {
    return *arguments->attached != '\0' ? arguments->attached
                                        : arguments->next_token(arguments->tokens);
}

/// Parses one network switch (SwitchHandler::take); see launch_switch_handler.
///
/// @param context The LaunchSwitches.
/// @param letter Lower-case switch letter.
/// @param arguments Text after the letter and the following tokens.
/// @param[out] effects Gains switch_effect_skip_intro for -c, -n and -y.
/// @return 1 when the letter is one of -b -c -e -h -n -p -t -y, else 0.
int take_switch(
    void* context, char letter, const cl::SwitchArguments* arguments, uint32_t* effects
) noexcept {
    LaunchSwitches& switches = *static_cast<LaunchSwitches*>(context);
    switch (letter) {
    case 'b': {
        const char* word = argument(arguments);
        if (word == nullptr || *word == '\0')
            switches.notes.emplace_back("-b names no battle room option, so it does nothing");
        else if (!take_battle_word(switches.battle, word))
            switches.notes.push_back(
                std::string("-b ") + word + " names no battle room option, so it does nothing"
            );
        return 1;
    }
    case 'c': {
        // The next token is the file whatever it starts with: "-" alone is
        // standard input, and "-x" a file of that name.
        const char* path = arguments->attached;
        if (*path == '\0')
            path = arguments->next_token(arguments->tokens);
        clear_launch_record(switches);
        switches.launch_seen = true;
        switches.launch_path = path != nullptr ? path : "";
        *effects |= cl::switch_effect_skip_intro;
        return 1;
    }
    case 'e': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0') {
            int32_t percent = *value == '-' ? 0 : decode_int(value);
            if (percent < 0 || percent > kMaximumSendErrorPercent)
                percent = 0;
            switches.send_error_percent = percent;
        }
        return 1;
    }
    case 'h': {
        const char* name = arguments->attached;
        if (*name == '\0') {
            name = arguments->next_token(arguments->tokens);
            if (name == nullptr || *name == '\0' || *name == '-')
                return 1;
        }
        set_host_game(switches, 1, name);
        switches.session_from_switch = true;
        return 1;
    }
    case 'n': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0' && *value != '-') {
            const char* colon = std::strchr(value, ':');
            const int32_t type = decode_int(value);
            if (type == kAddressedConnectionType && colon != nullptr) {
                set_connection_address(switches, colon + 1);
                switches.address_from_switch = true;
            }
            if (type >= kFirstConnectionType && type <= kLastConnectionType) {
                set_connection_type(switches, type);
                switches.connection_type_from_switch = true;
            }
        }
        *effects |= cl::switch_effect_skip_intro;
        return 1;
    }
    case 'p': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0') {
            switches.send_pacing = *value == '-' ? kPacingFromDash : decode_int(value);
            switches.send_pacing_set = 1;
        }
        return 1;
    }
    case 't': {
        const char* value = argument(arguments);
        if (value != nullptr && *value != '\0') {
            int32_t seconds = *value == '-' ? 0 : decode_int(value);
            if (seconds < kDefaultNetTimeoutSeconds || seconds > kMaximumNetTimeoutSeconds)
                seconds = kDefaultNetTimeoutSeconds;
            switches.net_timeout_seconds = seconds;
        }
        return 1;
    }
    case 'y':
        // 3.1c opens its built-in internet lobby; this game has none, so -y
        // keeps only the intro skip.
        switches.notes.emplace_back(
            "-y opens 3.1c's built-in internet lobby, which this game does not have; it only "
            "skips the intro"
        );
        *effects |= cl::switch_effect_skip_intro;
        return 1;
    default:
        return 0;
    }
}

/// Resets the net timeout, send-error percentage and notes before a parse (SwitchHandler::reset).
///
/// @param context The LaunchSwitches.
void reset_switches(void* context) noexcept {
    LaunchSwitches& switches = *static_cast<LaunchSwitches*>(context);
    switches.net_timeout_seconds = kDefaultNetTimeoutSeconds;
    switches.send_error_percent = 0;
    switches.notes.clear();
}

} // namespace

cl::SwitchHandler launch_switch_handler(LaunchSwitches* switches) noexcept {
    return cl::SwitchHandler{switches, take_switch, reset_switches};
}

void set_host_game(LaunchSwitches& switches, uint32_t hosting, const char* name) noexcept {
    switches.block.hosting = hosting != 0 ? 1U : 0U;
    if (name != nullptr) {
        copy_text(switches.block.session_name, name);
        append_text(switches.host_game_name_copy, name);
    }
}

void set_connection_address(LaunchSwitches& switches, const char* address) noexcept {
    copy_text(switches.block.address, address);
    append_text(switches.connection_address_copy, address);
}

void set_connection_type(LaunchSwitches& switches, int32_t type) noexcept {
    if (type >= kFirstConnectionType && type <= kLastConnectionType) {
        switches.block.connection_type = type;
        switches.connection_type_copy = type;
    }
}

void enable_netsetup(LaunchSwitches& switches) noexcept {
    switches.netsetup = 1;
}

void clear_launch_record(LaunchSwitches& switches) {
    std::string lost;
    const auto add = [&lost](const char* name) {
        if (!lost.empty())
            lost += ", ";
        lost += name;
    };
    if (switches.session_from_switch)
        add("-h");
    if (switches.connection_type_from_switch || switches.address_from_switch)
        add("-n");
    if (switches.battle.named())
        add("-b");
    if (!lost.empty())
        switches.notes.push_back(
            "-c clears the launch, as in 3.1c: what " + lost + " set before it is lost"
        );
    switches.block = LaunchBlock{};
    switches.session_from_switch = false;
    switches.connection_type_from_switch = false;
    switches.address_from_switch = false;
    switches.battle = BattleWords{};
}

bool take_battle_word(BattleWords& battle, const char* word) noexcept {
    for (const BattleWord& entry : kBattleWords) {
        if (!same_word(word, entry.word))
            continue;
        switch (entry.option) {
        case BattleOption::lock:
            battle.lock = static_cast<uint32_t>(entry.code);
            break;
        case BattleOption::commander:
            battle.death_option = entry.code;
            break;
        case BattleOption::sight:
            battle.los = entry.code;
            break;
        case BattleOption::starts:
            battle.location = entry.code;
            break;
        case BattleOption::mapping:
            battle.mapping = entry.code;
            break;
        case BattleOption::cheats:
            battle.cheats = entry.code;
            break;
        case BattleOption::watching:
            battle.watching = entry.code;
            break;
        }
        return true;
    }
    return false;
}

void apply_battle_words(const BattleWords& battle, LaunchBlock& block) noexcept {
    if (battle.lock != 0)
        block.lock_options = battle.lock;
    if (battle.death_option != 0)
        block.death_option = battle.death_option;
    if (battle.los != 0)
        block.los = battle.los;
    if (battle.location != 0)
        block.location = battle.location;
    if (battle.mapping != 0)
        block.mapping = battle.mapping;
    if (battle.cheats != 0)
        block.cheats = battle.cheats;
    if (battle.watching != 0)
        block.watching = battle.watching;
}

} // namespace oa::app::netgame::launch
