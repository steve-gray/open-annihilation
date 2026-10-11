// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The presence card (presence_card.hpp): its words from a player's presence
// record.

#include "oa/ui/frontend_multiplayer/presence_card.hpp"

#include "oa/present/game_text.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::frontend_multiplayer {

namespace {

/// Returns a word of the card in the language shown.
///
/// @param lobby the battle room, whose translation it goes through
/// @param english the word, in English
/// @return the word
std::string word(const Lobby& lobby, const char* english) {
    const char* shown = lobby_translated(lobby, english);
    return shown != nullptr ? std::string(shown) : std::string(english);
}

/// One placeholder of a phrase and what takes its place.
struct Filling {
    std::string_view placeholder{}; ///< the placeholder, such as "{count}"
    std::string_view value{};       ///< what takes its place
};

/// Returns a phrase of the card in the language shown, with values in place
/// of its placeholders.
///
/// @param lobby the battle room, whose translation it goes through
/// @param english the phrase, in English, holding the placeholders
/// @param fillings each placeholder and what takes its place
/// @return the phrase
std::string
filled(const Lobby& lobby, const char* english, std::initializer_list<Filling> fillings) {
    std::string phrase = word(lobby, english);
    for (const Filling& filling : fillings) {
        const std::size_t at = phrase.find(filling.placeholder);
        if (at != std::string::npos)
            phrase.replace(at, filling.placeholder.size(), filling.value);
    }
    return phrase;
}

/// Returns a player's name as UTF-8 in capitals: a to z, and the small
/// letters of Latin-1 from à to þ (all but ÷), made capitals.
///
/// @param player the player
/// @return the name
std::string name_in_capitals(const Player& player) {
    std::size_t length = 0;
    while (length < sizeof player.name && player.name[length] != '\0')
        ++length;
    std::string name = oa::present::decode_game_text(
        std::string_view(player.name, length), oa::present::game_text_settings().utf8
    );
    constexpr auto latin_lead = static_cast<unsigned char>(0xc3);
    constexpr auto small_first = static_cast<unsigned char>(0xa0); // à
    constexpr auto small_last = static_cast<unsigned char>(0xbe);  // þ
    constexpr auto division = static_cast<unsigned char>(0xb7);    // ÷
    constexpr unsigned char capital_step = 0x20;
    for (std::size_t index = 0; index < name.size(); ++index) {
        const auto code = static_cast<unsigned char>(name[index]);
        if (code >= 'a' && code <= 'z') {
            name[index] = static_cast<char>(code - capital_step);
        } else if (code == latin_lead && index + 1 < name.size()) {
            const auto next = static_cast<unsigned char>(name[index + 1]);
            if (next >= small_first && next <= small_last && next != division)
                name[index + 1] = static_cast<char>(next - capital_step);
            ++index;
        }
    }
    return name;
}

/// Returns the words before and after a separator, either left out when empty.
///
/// @param first the first words
/// @param second the second words
/// @return "<first> · <second>", "<first>", "<second>" or nothing
std::string joined(std::string_view first, std::string_view second) {
    std::string text(first);
    if (!first.empty() && !second.empty())
        text += " · ";
    text += second;
    return text;
}

/// Returns a presence record's sim hash, or the plain baseline's for a host
/// on 3.1c, which sends no record and plays 3.1c's own rules.
///
/// @param lobby the battle room
/// @param slot the slot
/// @param record the slot's record; nothing when it has none
/// @return the hash; nothing when no record or rule tells it
std::optional<netgame::PresenceDigest>
known_sim_hash(Lobby& lobby, int32_t slot, const std::optional<netgame::PresenceRecord>& record) {
    if (record)
        return record->sim_hash;
    if (lobby_row_badge(lobby, slot).badge == RowBadge::open_annihilation)
        return std::nullopt;
    return lobby.presence_records.plain_sim_hash;
}

/// Reads the presence record of a slot's player.
///
/// @param lobby the battle room
/// @param slot the slot
/// @return the record; nothing when none came or it does not read
std::optional<netgame::PresenceRecord> record_of(Lobby& lobby, int32_t slot) {
    const auto bytes = lobby_presence_record(lobby, slot);
    if (bytes.empty())
        return std::nullopt;
    netgame::PresenceRecord record;
    if (netgame::decode_presence(bytes.data(), bytes.size(), &record) != netgame::WireError::ok)
        return std::nullopt;
    return record;
}

/// Returns the Engine line's value.
///
/// @param lobby the battle room
/// @param record the player's record
/// @return the value
std::string engine_value(const Lobby& lobby, const netgame::PresenceRecord& record) {
    if (!record.engine || record.engine->version.empty())
        return word(lobby, "Open Annihilation");
    const std::string& version = record.engine->version;
    const std::size_t label = version.find('-');
    if (label == std::string::npos)
        return filled(lobby, "Open Annihilation {version}", {{"{version}", version}});
    return filled(
        lobby,
        "Open Annihilation {version} (development build)",
        {{"{version}", std::string_view(version).substr(0, label)}}
    );
}

/// Returns the Rules line.
///
/// @param lobby the battle room
/// @param slot the player's slot
/// @param record the player's record
/// @return the line, its label left to the caller
PresenceCardLine rules_line(Lobby& lobby, int32_t slot, const netgame::PresenceRecord& record) {
    const uint8_t host = lobby_host_slot(lobby);
    if (host == static_cast<uint8_t>(slot))
        return {{}, word(lobby, "The host's"), CardTone::plain};
    if (host == kNoSlot)
        return {{}, word(lobby, "Unknown"), CardTone::plain};
    const auto host_record = record_of(lobby, host);
    if (lobby_rules_differ_from_host(lobby, slot)) {
        if (record.mod && host_record && host_record->mod &&
            host_record->mod->id == record.mod->id &&
            host_record->mod->packaging_revision != record.mod->packaging_revision)
            return {
                {},
                filled(
                    lobby,
                    "Differ from the host's (rev {revision})",
                    {{"{revision}", std::to_string(host_record->mod->packaging_revision)}}
                ),
                CardTone::amber
            };
        return {{}, word(lobby, "Differ from the host's"), CardTone::amber};
    }
    const auto own_hash = record.sim_hash;
    const auto host_hash = known_sim_hash(lobby, host, host_record);
    if (own_hash && host_hash && *own_hash == *host_hash)
        return {{}, word(lobby, "Same as the host's"), CardTone::plain};
    return {{}, word(lobby, "Unknown"), CardTone::plain};
}

/// Returns a count as one of three phrases: none, one and more.
///
/// @param lobby the battle room
/// @param count the count
/// @param none the phrase for none
/// @param one the phrase for one
/// @param more the phrase for more, holding "{count}"
/// @return the phrase
std::string
counted(const Lobby& lobby, uint32_t count, const char* none, const char* one, const char* more) {
    if (count == 0)
        return word(lobby, none);
    if (count == 1)
        return word(lobby, one);
    return filled(lobby, more, {{"{count}", std::to_string(count)}});
}

/// Returns the Map line's value and colour.
///
/// @param lobby the battle room
/// @param pack the host's map pack
/// @param pack_state what this machine has of it; unset counts as unknown
/// @return the line, its label left to the caller
PresenceCardLine map_line(
    const Lobby& lobby,
    const netgame::PresenceMapPack& pack,
    const std::function<PackState(const netgame::PresenceMapPack&)>& pack_state
) {
    std::string named = pack.name.empty() ? pack.key : pack.name;
    if (!pack.version.empty())
        named += " " + pack.version;
    switch (pack_state ? pack_state(pack) : PackState::unknown) {
    case PackState::installed:
        return {{}, filled(lobby, "{pack} ✓ installed", {{"{pack}", named}}), CardTone::lime};
    case PackState::other_release:
        return {
            {}, filled(lobby, "{pack} · another release here", {{"{pack}", named}}), CardTone::amber
        };
    case PackState::missing:
        return {{}, filled(lobby, "{pack} · not installed", {{"{pack}", named}}), CardTone::plain};
    case PackState::unknown:
        break;
    }
    return {{}, std::move(named), CardTone::plain};
}

/// Returns a line with its label.
///
/// @param label the label, translated
/// @param line the value and its colour
/// @return the line
PresenceCardLine labelled(std::string label, PresenceCardLine line) {
    line.label = std::move(label);
    return line;
}

} // namespace

std::optional<PresenceCard> presence_card(
    Lobby& lobby,
    int32_t slot,
    const std::function<PackState(const netgame::PresenceMapPack&)>& pack_state
) {
    if (slot < 0 || slot >= kSlotCount ||
        lobby_row_badge(lobby, slot).badge != RowBadge::open_annihilation)
        return std::nullopt;
    PresenceCard card;
    card.title = name_in_capitals(slot_player(lobby, slot));
    const auto record = record_of(lobby, slot);
    if (!record) {
        // An OA build before 0.8 carries the signature and sends no record.
        card.lines.push_back(
            {word(lobby, "Engine"), word(lobby, "Open Annihilation (before 0.8)"), CardTone::plain}
        );
        return card;
    }
    if (record->engine)
        card.aside = joined(record->engine->platform, record->engine->architecture);
    card.lines.push_back({word(lobby, "Engine"), engine_value(lobby, *record), CardTone::plain});
    if (record->mod) {
        const auto& mod = *record->mod;
        const std::string named = mod.version.empty() ? mod.name : mod.name + " " + mod.version;
        const std::string revision = std::to_string(mod.packaging_revision);
        card.lines.push_back(
            {word(lobby, "Mod"),
             filled(lobby, "{mod} · rev {revision}", {{"{mod}", named}, {"{revision}", revision}}),
             CardTone::plain}
        );
    } else {
        card.lines.push_back(
            {word(lobby, "Mod"), word(lobby, "Total Annihilation 3.1c"), CardTone::plain}
        );
    }
    card.lines.push_back(labelled(word(lobby, "Rules"), rules_line(lobby, slot, *record)));
    if (record->game_hacks)
        card.lines.push_back(
            {word(lobby, "Game"),
             counted(
                 lobby,
                 record->game_hacks->total,
                 "No hacks change the game",
                 "1 hack changes the game",
                 "{count} hacks change the game"
             ),
             CardTone::plain}
        );
    if (record->view_hacks)
        card.lines.push_back(
            {word(lobby, "View"),
             counted(
                 lobby,
                 record->view_hacks->total,
                 "No view-only hacks",
                 "1 view-only hack",
                 "{count} view-only hacks"
             ),
             CardTone::plain}
        );
    if (record->developer) {
        const auto& developer = *record->developer;
        std::string value = word(lobby, "Off");
        if (developer.on)
            value =
                counted(lobby, developer.total, "On", "On · 1 override", "On · {count} overrides");
        card.lines.push_back({word(lobby, "Dev mode"), std::move(value), CardTone::plain});
    }
    if (record->map_pack && lobby_host_slot(lobby) == static_cast<uint8_t>(slot))
        card.lines.push_back(
            labelled(word(lobby, "Map"), map_line(lobby, *record->map_pack, pack_state))
        );
    if (record->game_hacks) {
        const auto& hacks = *record->game_hacks;
        const std::size_t listed = std::min(hacks.ids.size(), presence_card_listed_hacks);
        card.hacks.assign(
            hacks.ids.begin(), hacks.ids.begin() + static_cast<std::ptrdiff_t>(listed)
        );
        const auto total = static_cast<std::size_t>(hacks.total);
        if (total > presence_card_listed_hacks)
            card.hacks_more = filled(
                lobby,
                "· {count} more",
                {{"{count}", std::to_string(total - presence_card_listed_hacks)}}
            );
    }
    return card;
}

} // namespace oa::ui::frontend_multiplayer
