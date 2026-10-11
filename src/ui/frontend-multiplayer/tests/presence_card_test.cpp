// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The presence card over hand-made battle rooms: its words for the design's
// sample player (Kirby, on Ridge 4.8 rev 3 under a host on rev 4), for a
// development build, for an OA player whose record has not come and for the
// host's own card with a map pack in each state, and no card for a player on
// 3.1c.

#include "oa/netgame/presence.hpp"
#include "oa/netgame/records.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/frontend_multiplayer/lobby.hpp"
#include "oa/ui/frontend_multiplayer/lobby_net.hpp"
#include "oa/ui/frontend_multiplayer/presence_card.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace mp = oa::ui::frontend_multiplayer;
namespace ng = oa::netgame;

constexpr uint32_t kHostId = 0x100;
constexpr uint32_t kGuestId = 0x200;
constexpr uint32_t kThirdId = 0x300;

uint32_t clock_ticks = 1000;

uint32_t tick(void*) {
    return clock_ticks;
}

/// The words a test translation gives, by their English text.
std::map<std::string, std::string, std::less<>> translations;

/// Translates through `translations`, as LobbyServices::translate does.
///
/// @param text the English text
/// @return its translation; null for a text it does not know
const char* translate(void*, const char* text) {
    const auto found = translations.find(std::string_view(text));
    return found != translations.end() ? found->second.c_str() : nullptr;
}

/// A battle room with no layout: the local player in slot 0 and the
/// players of other machines seated after it.
struct Room {
    std::unique_ptr<oa::Game> game = std::make_unique<oa::Game>();
    std::unique_ptr<mp::Lobby> lobby = std::make_unique<mp::Lobby>();
    std::unique_ptr<mp::LoopbackNet> loopback = std::make_unique<mp::LoopbackNet>();

    /// @param hosting true seats the local player as the host
    /// @param local_name the local player's name
    Room(bool hosting, const char* local_name) {
        mp::loopback_reset(*loopback);
        mp::lobby_reset(*lobby, *game);
        lobby->net = mp::loopback_lobby_net(*loopback);
        lobby->services.tick = tick;
        lobby->local_version_major = 3;
        game->session_flags |= 1;
        loopback->opened = true;
        game->players[0].player_id = hosting ? kHostId : kGuestId;
        mp::lobby_seat_local(*lobby, 0, hosting, local_name);
    }

    /// Seats a player of another machine and applies its setup block,
    /// a human's, with the 'OA' signature or without, and the bytes after
    /// the signature zero, as every block carries them.
    ///
    /// @param id the player
    /// @param name the player's name
    /// @param open_annihilation the block carries the 'OA' signature
    /// @param host the player hosts
    /// @return the player's slot
    int32_t seat(uint32_t id, const char* name, bool open_annihilation, bool host = false) {
        mp::LobbyEvent joined{};
        joined.kind = mp::LobbyEventKind::player_joined;
        joined.player_id = id;
        std::snprintf(joined.name, sizeof joined.name, "%s", name);
        (void)mp::lobby_apply_event(*lobby, joined);
        ng::PlayerInfoRecord block{};
        block.player_id = id;
        const auto set = [&block](std::size_t offset, uint8_t value) {
            if (offset < sizeof block.info_head)
                block.info_head[offset] = value;
            else if (offset >= ng::player_info_tail_offset)
                block.info_tail[offset - ng::player_info_tail_offset] = value;
        };
        set(offsetof(mp::PlayerSetupInfo, state), mp::kInfoStatePlaying);
        if (host)
            set(offsetof(mp::PlayerSetupInfo, role), mp::kRoleHost);
        if (open_annihilation) {
            set(ng::player_info_engine_signature_offset, ng::engine_signature_first);
            set(ng::player_info_engine_signature_offset + 1, ng::engine_signature_second);
        }
        mp::LobbyEvent event{};
        event.kind = mp::LobbyEventKind::record;
        event.player_id = id;
        std::size_t written = 0;
        (void)ng::encode_record(block, event.data, sizeof event.data, &written);
        event.size = static_cast<uint16_t>(written);
        OA_CHECK(mp::lobby_apply_event(*lobby, event));
        const int32_t slot = mp::slot_for_player_id(*lobby, id);
        OA_CHECK(slot > 0);
        // The block can name the host; a test also sets it on the slot.
        if (host && slot > 0)
            mp::slot_info(*lobby, slot)->role = mp::kRoleHost;
        return slot;
    }

    /// Keeps a presence record a player of another machine sent.
    ///
    /// @param id the player
    /// @param record the record's bytes
    void receive(uint32_t id, const std::vector<uint8_t>& record) {
        mp::LobbyEvent event{};
        event.kind = mp::LobbyEventKind::record;
        event.player_id = id;
        std::copy(record.begin(), record.end(), event.data);
        event.size = static_cast<uint16_t>(record.size());
        OA_CHECK(mp::lobby_apply_event(*lobby, event));
    }

    /// Makes a record this machine's own, as the screens' binding does.
    ///
    /// @param record the record's bytes
    void own(const std::vector<uint8_t>& record) {
        auto& records = lobby->presence_records;
        std::copy(record.begin(), record.end(), records.source);
        records.source_size = static_cast<uint16_t>(record.size());
        mp::presence_refresh(*lobby);
    }
};

/// Encodes a presence record.
///
/// @param record the fields
/// @return the bytes
std::vector<uint8_t> encoded(const ng::PresenceRecord& record) {
    std::vector<uint8_t> bytes(ng::presence_record_max_bytes);
    std::size_t written = 0;
    OA_CHECK(
        ng::encode_presence(record, bytes.data(), bytes.size(), &written) == ng::WireError::ok
    );
    bytes.resize(written);
    return bytes;
}

/// Returns a sim hash of 32 equal bytes.
///
/// @param value each byte
/// @return the hash
ng::PresenceDigest filled_hash(uint8_t value) {
    ng::PresenceDigest hash{};
    hash.fill(value);
    return hash;
}

/// The game hacks of the sample: eight, three of them listed first.
const std::vector<std::string> sample_game_hacks{
    "economy.deterministic-wind",
    "ai.income-multipliers",
    "air.gunships-hover-to-strafe",
    "sea.subs-surface-to-fire",
    "economy.storage-caps",
    "combat.dgun-cost",
    "ai.attack-wave-size",
    "air.stall-speed"
};

/// Returns the design's sample record: OA 0.8.0 on Windows x64, Ridge 4.8
/// at packaging revision 3, eight game hacks, four view hacks, Developer
/// Mode off.
///
/// @param version the OA version text
/// @return the record
ng::PresenceRecord sample_record(const char* version = "0.8.0") {
    ng::PresenceRecord record;
    record.engine = ng::PresenceEngine{version, "Windows", "x64"};
    record.mod = ng::PresenceMod{"ridge", "Ridge", "4.8", 3};
    record.sim_hash = filled_hash(0x77);
    record.game_hacks = ng::PresenceHacks{8, sample_game_hacks};
    record.view_hacks = ng::PresenceHacks{4, {"ui.megamap", "ui.health-bars", "ui.whiteboard"}};
    record.developer = ng::PresenceDeveloper{false, 0, {}};
    return record;
}

/// Returns the sample host's record: Ridge 4.8 at packaging revision 4.
///
/// @return the record
ng::PresenceRecord host_record() {
    ng::PresenceRecord record;
    record.engine = ng::PresenceEngine{"0.8.0", "macOS", "arm64"};
    record.mod = ng::PresenceMod{"ridge", "Ridge", "4.8", 4};
    record.sim_hash = filled_hash(0x5a);
    record.game_hacks = ng::PresenceHacks{0, {}};
    record.view_hacks = ng::PresenceHacks{0, {}};
    record.developer = ng::PresenceDeveloper{false, 0, {}};
    return record;
}

/// Tells whether a card's line is as expected.
///
/// @param card the card
/// @param index the line's place, from 0
/// @param label its label
/// @param value its value
/// @param tone its colour
/// @return true when it is
bool line_is(
    const mp::PresenceCard& card,
    std::size_t index,
    std::string_view label,
    std::string_view value,
    mp::CardTone tone = mp::CardTone::plain
) {
    if (index >= card.lines.size()) {
        std::fprintf(stderr, "  no line %zu\n", index);
        return false;
    }
    const auto& line = card.lines[index];
    if (line.label == label && line.value == value && line.tone == tone)
        return true;
    std::fprintf(
        stderr, "  line %zu is \"%s: %s\"\n", index, line.label.c_str(), line.value.c_str()
    );
    return false;
}

/// Answers every map pack with one state.
///
/// @param state the state
/// @return the answer
std::function<mp::PackState(const ng::PresenceMapPack&)> answering(mp::PackState state) {
    return [state](const ng::PresenceMapPack&) { return state; };
}

/// The sample room: the local player hosts on Ridge rev 4, and Kirby, on
/// another machine, plays Ridge rev 3 by other rules.
struct SampleRoom {
    Room room{true, "Steve"};
    int32_t kirby{};

    SampleRoom() {
        room.own(encoded(host_record()));
        kirby = room.seat(kGuestId, "Kirby", true);
        room.receive(kGuestId, encoded(sample_record()));
    }
};

// The design's sample: every line of Kirby's card, the amber Rules line
// naming the host's revision, and the hacks.
void the_sample_card_says_what_the_record_says() {
    SampleRoom sample;
    const auto card = mp::presence_card(*sample.room.lobby, sample.kirby, {});
    OA_CHECK(card.has_value());
    if (!card)
        return;
    OA_CHECK(card->title == "KIRBY");
    OA_CHECK(card->aside == "Windows · x64");
    OA_CHECK(card->lines.size() == 6);
    OA_CHECK(line_is(*card, 0, "Engine", "Open Annihilation 0.8.0"));
    OA_CHECK(line_is(*card, 1, "Mod", "Ridge 4.8 · rev 3"));
    OA_CHECK(line_is(*card, 2, "Rules", "Differ from the host's (rev 4)", mp::CardTone::amber));
    OA_CHECK(line_is(*card, 3, "Game", "8 hacks change the game"));
    OA_CHECK(line_is(*card, 4, "View", "4 view-only hacks"));
    OA_CHECK(line_is(*card, 5, "Dev mode", "Off"));
    OA_CHECK(
        card->hacks ==
        std::vector<std::string>(sample_game_hacks.begin(), sample_game_hacks.begin() + 3)
    );
    OA_CHECK(card->hacks_more == "· 5 more");

    // The host's own card: "The host's", and its own mod.
    const auto host = mp::presence_card(*sample.room.lobby, 0, {});
    OA_CHECK(host.has_value());
    if (host) {
        OA_CHECK(host->title == "STEVE");
        OA_CHECK(host->aside == "macOS · arm64");
        OA_CHECK(line_is(*host, 1, "Mod", "Ridge 4.8 · rev 4"));
        OA_CHECK(line_is(*host, 2, "Rules", "The host's"));
        OA_CHECK(line_is(*host, 3, "Game", "No hacks change the game"));
        OA_CHECK(line_is(*host, 4, "View", "No view-only hacks"));
        OA_CHECK(host->hacks.empty() && host->hacks_more.empty());
    }
}

// The Rules line from the records' sim hashes: the same hash, no hash, a
// different hash on another mod, and a 3.1c host playing the plain rules.
void the_rules_line_compares_with_the_host() {
    SampleRoom sample;
    auto record = sample_record();
    record.sim_hash = host_record().sim_hash;
    sample.room.receive(kGuestId, encoded(record));
    auto card = mp::presence_card(*sample.room.lobby, sample.kirby, {});
    OA_CHECK(card && line_is(*card, 2, "Rules", "Same as the host's"));

    record.sim_hash.reset();
    sample.room.receive(kGuestId, encoded(record));
    card = mp::presence_card(*sample.room.lobby, sample.kirby, {});
    OA_CHECK(card && line_is(*card, 2, "Rules", "Unknown"));

    record = sample_record();
    record.mod = ng::PresenceMod{"stone", "Stone", "", 3};
    sample.room.receive(kGuestId, encoded(record));
    card = mp::presence_card(*sample.room.lobby, sample.kirby, {});
    OA_CHECK(card && line_is(*card, 1, "Mod", "Stone · rev 3"));
    OA_CHECK(card && line_is(*card, 2, "Rules", "Differ from the host's", mp::CardTone::amber));

    record.mod.reset();
    record.developer = ng::PresenceDeveloper{true, 2, {{"ai.attack-wave-size", true}}};
    record.game_hacks = ng::PresenceHacks{1, {"ai.attack-wave-size"}};
    record.view_hacks = ng::PresenceHacks{1, {"ui.megamap"}};
    sample.room.receive(kGuestId, encoded(record));
    card = mp::presence_card(*sample.room.lobby, sample.kirby, {});
    OA_CHECK(card && line_is(*card, 1, "Mod", "Total Annihilation 3.1c"));
    OA_CHECK(card && line_is(*card, 3, "Game", "1 hack changes the game"));
    OA_CHECK(card && line_is(*card, 4, "View", "1 view-only hack"));
    OA_CHECK(card && line_is(*card, 5, "Dev mode", "On · 2 overrides"));
    OA_CHECK(card && card->hacks.size() == 1 && card->hacks_more.empty());

    // Under a 3.1c host, which sends no record, the plain baseline's rules.
    Room joined(false, "Kirby");
    const int32_t host = joined.seat(kHostId, "OldSchool", false, true);
    OA_CHECK(mp::lobby_host_slot(*joined.lobby) == host);
    OA_CHECK(!mp::presence_card(*joined.lobby, host, {}).has_value());
    const auto& plain = joined.lobby->presence_records.plain_sim_hash;
    if (plain) {
        record = sample_record();
        record.sim_hash = *plain;
        joined.own(encoded(record));
        card = mp::presence_card(*joined.lobby, 0, {});
        OA_CHECK(card && line_is(*card, 2, "Rules", "Same as the host's"));
        record.sim_hash = filled_hash(0x13);
        joined.own(encoded(record));
        card = mp::presence_card(*joined.lobby, 0, {});
        OA_CHECK(card && line_is(*card, 2, "Rules", "Differ from the host's", mp::CardTone::amber));
    }
}

// A development build's version: its label becomes "(development build)".
void a_development_build_says_so() {
    SampleRoom sample;
    sample.room.receive(kGuestId, encoded(sample_record("0.8.0-dev")));
    const auto card = mp::presence_card(*sample.room.lobby, sample.kirby, {});
    OA_CHECK(card && line_is(*card, 0, "Engine", "Open Annihilation 0.8.0 (development build)"));
}

// An OA player whose record has not come, an OA build before 0.8: its
// block carries 'OA' and the five bytes after it zero, and the card has the
// title and one line. A record that comes later replaces it.
void a_player_without_a_record_gets_one_line() {
    Room room(true, "Steve");
    const int32_t slot = room.seat(kThirdId, "Mira", true);
    const auto* block = reinterpret_cast<const uint8_t*>(mp::slot_info(*room.lobby, slot));
    OA_CHECK(block != nullptr);
    if (block != nullptr) {
        OA_CHECK(ng::sent_by_open_annihilation(block));
        bool spare_zero = true;
        for (std::size_t offset = 0xaf; offset <= 0xb3; ++offset)
            spare_zero = spare_zero && block[offset] == 0;
        OA_CHECK(spare_zero);
    }
    auto card = mp::presence_card(*room.lobby, slot, {});
    OA_CHECK(card.has_value());
    if (card) {
        OA_CHECK(card->title == "MIRA");
        OA_CHECK(card->aside.empty());
        OA_CHECK(card->lines.size() == 1);
        OA_CHECK(line_is(*card, 0, "Engine", "Open Annihilation (before 0.8)"));
        OA_CHECK(card->hacks.empty() && card->hacks_more.empty());
    }
    room.receive(kThirdId, encoded(sample_record()));
    card = mp::presence_card(*room.lobby, slot, {});
    OA_CHECK(card && card->lines.size() == 6);
}

// The host's own card names its map pack, and says what this machine has
// of it in each state; nobody else's card has a Map line.
void the_hosts_card_names_its_map_pack() {
    Room room(false, "Kirby");
    const int32_t host = room.seat(kHostId, "Steve", true, true);
    OA_CHECK(mp::lobby_host_slot(*room.lobby) == host);
    auto record = host_record();
    record.map_pack =
        ng::PresenceMapPack{"archipelago", 2, {}, "example-registry", "", "Archipelago", "1.2"};
    room.receive(kHostId, encoded(record));
    room.own(encoded(record));

    const struct {
        mp::PackState state;
        const char* value;
        mp::CardTone tone;
    } states[] = {
        {mp::PackState::installed, "Archipelago 1.2 ✓ installed", mp::CardTone::lime},
        {mp::PackState::other_release,
         "Archipelago 1.2 · another release here",
         mp::CardTone::amber},
        {mp::PackState::missing, "Archipelago 1.2 · not installed", mp::CardTone::plain},
        {mp::PackState::unknown, "Archipelago 1.2", mp::CardTone::plain},
    };

    for (const auto& expected : states) {
        const auto card = mp::presence_card(*room.lobby, host, answering(expected.state));
        OA_CHECK(card && line_is(*card, 2, "Rules", "The host's"));
        OA_CHECK(card && line_is(*card, 6, "Map", expected.value, expected.tone));
    }
    const auto unbound = mp::presence_card(*room.lobby, host, {});
    OA_CHECK(unbound && line_is(*unbound, 6, "Map", "Archipelago 1.2"));
    // A pack whose name did not fit is named by its key.
    record.map_pack->name.clear();
    record.map_pack->version.clear();
    room.receive(kHostId, encoded(record));
    const auto keyed = mp::presence_card(*room.lobby, host, answering(mp::PackState::missing));
    OA_CHECK(keyed && line_is(*keyed, 6, "Map", "archipelago · not installed"));
    // The local joiner's own record names the same pack, but it is not the host's.
    const auto own = mp::presence_card(*room.lobby, 0, answering(mp::PackState::installed));
    OA_CHECK(own && own->lines.size() == 6);
}

// No card for a player on 3.1c, an open slot or a slot out of range.
void a_31c_player_has_no_card() {
    Room room(true, "Steve");
    const int32_t old_school = room.seat(kThirdId, "OldSchool", false);
    OA_CHECK(!mp::presence_card(*room.lobby, old_school, {}).has_value());
    OA_CHECK(!mp::presence_card(*room.lobby, 5, {}).has_value());
    OA_CHECK(!mp::presence_card(*room.lobby, -1, {}).has_value());
    OA_CHECK(!mp::presence_card(*room.lobby, mp::kSlotCount, {}).has_value());
    // Not even with a record of its own: only the 'OA' signature gives a badge.
    room.receive(kThirdId, encoded(sample_record()));
    OA_CHECK(!mp::presence_card(*room.lobby, old_school, {}).has_value());
}

// Every label and fixed word goes through the lobby's translation.
void the_words_go_through_the_translation() {
    SampleRoom sample;
    translations = {
        {"Engine", "Moteur"},
        {"Open Annihilation {version}", "Open Annihilation version {version}"},
        {"{mod} · rev {revision}", "{mod} (révision {revision})"},
        {"Differ from the host's (rev {revision})", "Diffèrent de l'hôte ({revision})"},
        {"{count} hacks change the game", "{count} modifications du jeu"},
        {"Off", "Arrêt"},
        {"· {count} more", "· {count} de plus"},
    };
    sample.room.lobby->services.translate = translate;
    const auto card = mp::presence_card(*sample.room.lobby, sample.kirby, {});
    sample.room.lobby->services.translate = nullptr;
    translations.clear();
    OA_CHECK(card && line_is(*card, 0, "Moteur", "Open Annihilation version 0.8.0"));
    OA_CHECK(card && line_is(*card, 1, "Mod", "Ridge 4.8 (révision 3)"));
    OA_CHECK(card && line_is(*card, 2, "Rules", "Diffèrent de l'hôte (4)", mp::CardTone::amber));
    OA_CHECK(card && line_is(*card, 3, "Game", "8 modifications du jeu"));
    OA_CHECK(card && line_is(*card, 5, "Dev mode", "Arrêt"));
    OA_CHECK(card && card->hacks_more == "· 5 de plus");
}

} // namespace

int main() {
    the_sample_card_says_what_the_record_says();
    the_rules_line_compares_with_the_host();
    a_development_build_says_so();
    a_player_without_a_record_gets_one_line();
    the_hosts_card_names_its_map_pack();
    a_31c_player_has_no_card();
    the_words_go_through_the_translation();
    return oa::test::check_exit_status();
}
