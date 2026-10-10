// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The presence record: its bytes, the fields a reader keeps, and the one
// place a frame's split takes it.

#include "oa/netgame/frame.hpp"
#include "oa/netgame/presence.hpp"
#include "oa/netgame/presence_block.hpp"
#include "oa/netgame/records.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace oa::netgame;

namespace {

int failures = 0;
const char* current_test = "";

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ++failures;                                                                            \
            std::fprintf(stderr, "%s: %s:%d: %s\n", current_test, __FILE__, __LINE__, #condition); \
        }                                                                                          \
    } while (0)

void start_case(const char* name) {
    current_test = name;
    std::printf("presence: %s\n", name);
    std::fflush(stdout);
}

// A record under construction. The length word is filled in by finish.
struct Raw {
    std::vector<uint8_t> bytes{presence_record_type, 0, 0, presence_record_version};

    void u8(uint8_t value) { bytes.push_back(value); }

    void u16(uint16_t value) {
        bytes.push_back(static_cast<uint8_t>(value));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
    }

    void u32(uint32_t value) {
        u16(static_cast<uint16_t>(value));
        u16(static_cast<uint16_t>(value >> 16));
    }

    void raw(const void* data, std::size_t size) {
        const auto* bytes_in = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), bytes_in, bytes_in + size);
    }

    void str(std::string_view text) {
        u8(static_cast<uint8_t>(text.size()));
        raw(text.data(), text.size());
    }

    void field(uint8_t tag, const std::vector<uint8_t>& value) {
        u8(tag);
        u16(static_cast<uint16_t>(value.size()));
        raw(value.data(), value.size());
    }

    void finish() {
        const auto length = static_cast<uint16_t>(bytes.size());
        bytes[1] = static_cast<uint8_t>(length);
        bytes[2] = static_cast<uint8_t>(length >> 8);
    }
};

struct Value {
    std::vector<uint8_t> bytes;

    void u8(uint8_t value) { bytes.push_back(value); }

    void u16(uint16_t value) {
        bytes.push_back(static_cast<uint8_t>(value));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
    }

    void u32(uint32_t value) {
        u16(static_cast<uint16_t>(value));
        u16(static_cast<uint16_t>(value >> 16));
    }

    void raw(const void* data, std::size_t size) {
        const auto* bytes_in = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), bytes_in, bytes_in + size);
    }

    void str(std::string_view text) {
        u8(static_cast<uint8_t>(text.size()));
        raw(text.data(), text.size());
    }
};

PresenceDigest filled(uint8_t byte) {
    PresenceDigest digest{};
    digest.fill(byte);
    return digest;
}

// The card's sample, with Ridge where a real mod would be named.
PresenceRecord sample_record() {
    PresenceRecord record;
    record.engine = PresenceEngine{"0.8.0", "Windows", "x64"};
    record.mod = PresenceMod{"ridge", "Ridge", "4.8", 3};
    record.mod_origin = PresenceOrigin{"coreprime", "https://example.test/ridge", 28, filled(0x11)};
    record.sim_hash = filled(0xab);
    record.map_pack = PresenceMapPack{
        "archipelago",
        3,
        filled(0x22),
        "example-maps",
        "https://example.test/maps",
        "Archipelago Pack",
        "1.2"
    };
    record.developer = PresenceDeveloper{
        true,
        2,
        {PresenceOverride{"economy.deterministic-wind", true},
         PresenceOverride{"ai.income-multipliers", false}}
    };
    record.game_hacks = PresenceHacks{
        8,
        {"economy.deterministic-wind",
         "ai.income-multipliers",
         "air.gunships-hover-to-strafe",
         "navy.shore-build",
         "combat.damage-falloff",
         "builder.queue-assist",
         "resources.metal-share",
         "units.veteran-speed"}
    };
    record.view_hacks = PresenceHacks{
        4, {"view.unit-ranges", "view.build-icons", "view.resource-bars", "view.path-overlay"}
    };
    return record;
}

// header 4 + engine field 21 (value 18: 6 + 8 + 4) + sim field 35 + developer
// field 6 (flags 0 and a count of 0) = 66.
constexpr uint8_t kSmallRecord[] = {
    0xf0, 0x42, 0x00, 0x01, 0x01, 0x12, 0x00, 0x05, '0',  '.',  '8',  '.',  '0',  0x07,
    'W',  'i',  'n',  'd',  'o',  'w',  's',  0x03, 'x',  '6',  '4',  0x20, 0x20, 0x00,
    0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
    0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
    0xab, 0xab, 0xab, 0xab, 0x23, 0x03, 0x00, 0x00, 0x00, 0x00,
};
static_assert(sizeof kSmallRecord == 66);

// Engine 0.8.0 / Windows / x64, Developer Mode off with no overrides, and a
// sim hash of 32 bytes of 0xab. The bytes above are counted from the layout.
void a_small_record_is_encoded_exactly() {
    start_case("a_small_record_is_encoded_exactly");
    PresenceRecord record;
    record.engine = PresenceEngine{"0.8.0", "Windows", "x64"};
    record.sim_hash = filled(0xab);
    record.developer = PresenceDeveloper{};
    uint8_t out[presence_record_max_bytes];
    std::size_t written = 0;
    CHECK(encode_presence(record, out, sizeof out, &written) == WireError::ok);
    CHECK(written == sizeof kSmallRecord);
    CHECK(std::memcmp(out, kSmallRecord, sizeof kSmallRecord) == 0);
    CHECK(
        encode_presence(record, out, sizeof kSmallRecord - 1, &written) ==
        WireError::buffer_too_small
    );
}

void every_field_round_trips() {
    start_case("every_field_round_trips");
    const auto record = sample_record();
    uint8_t out[presence_record_max_bytes];
    std::size_t written = 0;
    CHECK(encode_presence(record, out, sizeof out, &written) == WireError::ok);
    PresenceRecord decoded;
    CHECK(decode_presence(out, written, &decoded) == WireError::ok);
    CHECK(decoded == record);
}

void lists_are_cut_to_fit() {
    start_case("lists_are_cut_to_fit");
    auto record = sample_record();
    record.game_hacks = PresenceHacks{90, {}};
    record.game_hacks->ids.assign(90, std::string(30, 'a'));
    uint8_t out[presence_record_max_bytes + 64];
    std::size_t written = 0;
    CHECK(encode_presence(record, out, sizeof out, &written) == WireError::ok);
    CHECK(written <= presence_record_max_bytes);
    PresenceRecord decoded;
    CHECK(decode_presence(out, written, &decoded) == WireError::ok);
    CHECK(decoded.engine == record.engine);
    CHECK(decoded.mod == record.mod);
    CHECK(decoded.mod_origin == record.mod_origin);
    CHECK(decoded.sim_hash == record.sim_hash);
    CHECK(decoded.map_pack == record.map_pack);
    CHECK(decoded.developer == record.developer);
    CHECK(decoded.game_hacks && decoded.game_hacks->total == 90);
    CHECK(decoded.game_hacks->ids.size() < 90 && !decoded.game_hacks->ids.empty());
    for (const auto& id : decoded.game_hacks->ids)
        CHECK(id == std::string(30, 'a'));
    CHECK(decoded.view_hacks && decoded.view_hacks->total == record.view_hacks->total);
    CHECK(decoded.view_hacks->ids.size() <= record.view_hacks->ids.size());

    PresenceRecord bad = record;
    bad.game_hacks->ids[0] = std::string(65, 'a');
    CHECK(encode_presence(bad, out, sizeof out, &written) == WireError::bad_argument);
    bad.game_hacks->ids[0] = "Not-an-id";
    CHECK(encode_presence(bad, out, sizeof out, &written) == WireError::bad_argument);
    CHECK(encode_presence(record, out, 8, &written) == WireError::buffer_too_small);
}

void unknown_fields_are_skipped() {
    start_case("unknown_fields_are_skipped");
    Raw record;
    Value engine;
    engine.str("0.8.0");
    engine.str("Linux");
    engine.str("arm64");
    record.field(0x01, engine.bytes);
    record.field(0x02, {});
    Value one;
    one.u8(0xee);
    record.field(0x7f, one.bytes);
    Value sim;
    sim.raw(filled(0xab).data(), 32);
    record.field(0x20, sim.bytes);
    Value wide;
    wide.bytes.assign(300, 0x11);
    record.field(0xff, wide.bytes);
    Value developer;
    developer.u8(0);
    developer.u16(0);
    record.field(0x23, developer.bytes);
    record.finish();
    CHECK(record.bytes.size() <= presence_record_max_bytes);

    PresenceRecord decoded;
    CHECK(decode_presence(record.bytes.data(), record.bytes.size(), &decoded) == WireError::ok);
    CHECK(decoded.version == 1);
    CHECK(decoded.engine == PresenceEngine("0.8.0", "Linux", "arm64"));
    CHECK(decoded.sim_hash == filled(0xab));
    CHECK(decoded.developer == PresenceDeveloper{});
    CHECK(!decoded.mod && !decoded.map_pack && !decoded.game_hacks);

    record.bytes[3] = 2;
    CHECK(decode_presence(record.bytes.data(), record.bytes.size(), &decoded) == WireError::ok);
    CHECK(decoded.version == 2);
    CHECK(decoded.engine == PresenceEngine("0.8.0", "Linux", "arm64"));
    CHECK(decoded.sim_hash == filled(0xab));
}

void a_bad_field_is_left_out() {
    start_case("a_bad_field_is_left_out");
    Raw record;
    // The engine version's length runs past the field. The field is 4 bytes.
    Value engine;
    engine.u8(10);
    engine.raw("abc", 3);
    record.field(0x01, engine.bytes);
    Value mod;
    mod.str("Ridge");
    mod.str("Ridge");
    mod.str("4.8");
    mod.u16(3);
    record.field(0x10, mod.bytes);
    Value sim;
    sim.raw(filled(0x11).data(), 32);
    record.field(0x20, sim.bytes);
    Value again;
    again.raw(filled(0x22).data(), 32);
    record.field(0x20, again.bytes);
    Value short_sim;
    short_sim.raw(filled(0x33).data(), 31);
    record.field(0x20, short_sim.bytes);
    Value developer;
    developer.u8(presence_record_version);
    developer.u16(0);
    record.field(0x23, developer.bytes);
    record.finish();

    PresenceRecord decoded;
    CHECK(decode_presence(record.bytes.data(), record.bytes.size(), &decoded) == WireError::ok);
    CHECK(!decoded.engine);
    CHECK(!decoded.mod);
    CHECK(decoded.sim_hash == filled(0x11));
    CHECK(decoded.developer && decoded.developer->on && decoded.developer->total == 0);

    Raw only_short;
    only_short.field(0x20, short_sim.bytes);
    Value kept;
    kept.str("0.8.0");
    kept.str("Windows");
    kept.str("x64");
    only_short.field(0x01, kept.bytes);
    only_short.finish();
    CHECK(
        decode_presence(only_short.bytes.data(), only_short.bytes.size(), &decoded) == WireError::ok
    );
    CHECK(!decoded.sim_hash);
    CHECK(decoded.engine == PresenceEngine("0.8.0", "Windows", "x64"));
}

void malformed_records_are_refused() {
    start_case("malformed_records_are_refused");
    PresenceRecord out;
    uint8_t byte = 0;
    CHECK(decode_presence(nullptr, 0, &out) == WireError::bad_argument);
    CHECK(decode_presence(&byte, 0, nullptr) == WireError::bad_argument);
    CHECK(decode_presence(&byte, 0, &out) == WireError::truncated);

    const uint8_t three[] = {0xf0, 0x04, 0x00};
    CHECK(decode_presence(three, sizeof three, &out) == WireError::length_mismatch);
    const uint8_t length_three[] = {0xf0, 0x03, 0x00};
    CHECK(decode_presence(length_three, sizeof length_three, &out) == WireError::length_mismatch);

    std::vector<uint8_t> huge(1025, 0);
    huge[0] = presence_record_type;
    huge[1] = 0x01;
    huge[2] = 0x04;
    huge[3] = 1;
    CHECK(decode_presence(huge.data(), huge.size(), &out) == WireError::length_mismatch);

    const uint8_t mismatched[] = {0xf0, 0x06, 0x00, 0x01, 0, 0, 0, 0};
    CHECK(decode_presence(mismatched, sizeof mismatched, &out) == WireError::length_mismatch);
    const uint8_t version_zero[] = {0xf0, 0x04, 0x00, 0x00};
    CHECK(decode_presence(version_zero, sizeof version_zero, &out) == WireError::bad_argument);
    const uint8_t cut_header[] = {0xf0, 0x05, 0x00, 0x01, 0x01};
    CHECK(decode_presence(cut_header, sizeof cut_header, &out) == WireError::truncated);
    // The value claims 5 bytes and the record ends on the size word.
    const uint8_t past_end[] = {0xf0, 0x07, 0x00, 0x01, 0x20, 0x05, 0x00};
    CHECK(decode_presence(past_end, sizeof past_end, &out) == WireError::truncated);
    // A value size of 65535 must refuse without reading past the record.
    const uint8_t enormous[] = {0xf0, 0x07, 0x00, 0x01, 0x20, 0xff, 0xff};
    CHECK(decode_presence(enormous, sizeof enormous, &out) == WireError::truncated);
    const uint8_t other_type[] = {0x05, 0x04, 0x00, 0x01};
    CHECK(decode_presence(other_type, sizeof other_type, &out) == WireError::invalid_type);
}

void append_and_find() {
    start_case("append_and_find");
    PresenceMapPack longest;
    longest.key.assign(64, 'k');
    longest.release = 9;
    longest.sha256 = filled(0x5a);
    longest.registry.assign(32, 'r');
    longest.descriptor_url.assign(255, 'u');
    longest.name.assign(64, 'n');
    longest.version.assign(32, 'v');
    uint8_t pack[presence_map_pack_field_max_bytes];
    std::size_t pack_bytes = 0;
    CHECK(encode_presence_map_pack(longest, pack, sizeof pack, &pack_bytes) == WireError::ok);
    CHECK(pack_bytes == presence_map_pack_field_max_bytes - presence_field_header_bytes);

    PresenceRecord head;
    head.engine = PresenceEngine{"0.8.0", "Windows", "x64"};
    head.sim_hash = filled(0xab);
    uint8_t record[presence_record_max_bytes];
    std::size_t written = 0;
    CHECK(encode_presence(head, record, sizeof record, &written) == WireError::ok);
    const auto before = written;
    std::size_t grown = 0;
    CHECK(
        append_presence_field(
            record,
            written,
            sizeof record,
            PresenceTag::map_pack,
            std::span<const uint8_t>(pack, pack_bytes),
            &grown
        ) == WireError::ok
    );
    CHECK(grown == before + presence_field_header_bytes + pack_bytes);
    CHECK(load_u16(record + 1) == grown);
    std::span<const uint8_t> found;
    CHECK(find_presence_field(record, grown, PresenceTag::map_pack, &found));
    CHECK(found.size() == pack_bytes && std::memcmp(found.data(), pack, pack_bytes) == 0);
    const auto hash = presence_sim_hash(record, grown);
    CHECK(hash == filled(0xab));

    uint8_t second[32];
    std::memset(second, 0x44, sizeof second);
    std::size_t with_second = 0;
    CHECK(
        append_presence_field(
            record,
            grown,
            sizeof record,
            PresenceTag::sim_hash,
            std::span<const uint8_t>(second, sizeof second),
            &with_second
        ) == WireError::ok
    );
    CHECK(presence_sim_hash(record, with_second) == filled(0xab));
    CHECK(find_presence_field(record, with_second, PresenceTag::sim_hash, &found));
    CHECK(found.size() == 32 && found[0] == 0xab);

    const auto room = presence_record_max_bytes - presence_map_pack_field_max_bytes;
    PresenceRecord crowded = sample_record();
    crowded.map_pack.reset();
    crowded.game_hacks = PresenceHacks{200, {}};
    crowded.game_hacks->ids.assign(200, std::string(24, 'b'));
    crowded.view_hacks = PresenceHacks{80, {}};
    crowded.view_hacks->ids.assign(80, std::string(12, 'c'));
    uint8_t limited[presence_record_max_bytes];
    std::size_t limited_bytes = 0;
    CHECK(encode_presence(crowded, limited, room, &limited_bytes) == WireError::ok);
    CHECK(limited_bytes <= room);
    CHECK(limited_bytes + presence_map_pack_field_max_bytes <= presence_record_max_bytes);
    std::size_t packed = 0;
    CHECK(
        append_presence_field(
            limited,
            limited_bytes,
            presence_record_max_bytes,
            PresenceTag::map_pack,
            std::span<const uint8_t>(pack, pack_bytes),
            &packed
        ) == WireError::ok
    );
    CHECK(packed == limited_bytes + presence_map_pack_field_max_bytes);
    CHECK(packed <= presence_record_max_bytes);

    PresenceHacks exact;
    exact.total = 35;
    exact.ids.assign(35, std::string(28, 'a'));
    PresenceRecord full;
    full.game_hacks = exact;
    std::size_t full_bytes = 0;
    CHECK(encode_presence(full, record, sizeof record, &full_bytes) == WireError::ok);
    CHECK(full_bytes == presence_record_max_bytes);
    CHECK(
        append_presence_field(
            record,
            full_bytes,
            sizeof record,
            PresenceTag::engine,
            std::span<const uint8_t>(),
            &grown
        ) == WireError::buffer_too_small
    );
    CHECK(load_u16(record + 1) == presence_record_max_bytes);
    // 1024 is 0x0400, so the low length byte is already zero. Clear the high
    // byte and the length word no longer matches the buffer.
    record[2] = 0;
    CHECK(find_presence_field(record, full_bytes, PresenceTag::game_hacks, &found) == false);
}

struct Split {
    WireError error{WireError::ok};
    std::vector<std::vector<uint8_t>> records;
};

Split split_frame(bool presence, const std::vector<uint8_t>& frame) {
    auto ring = std::make_unique<PeerRecords>();
    ring->presence_records = presence;
    UnpackOutcome outcome{};
    Split out;
    out.error =
        unpack_frame_records(ring.get(), frame.data(), frame.size(), 5, 1, 0, false, &outcome);
    const uint8_t* data = nullptr;
    uint16_t length = 0;
    while (pop_due_record(ring.get(), 5, &data, &length))
        out.records.emplace_back(data, data + length);
    return out;
}

std::vector<uint8_t> frame_of(std::initializer_list<std::vector<uint8_t>> records) {
    std::vector<uint8_t> frame{0xff, 0xff, 0xff, 0xff};
    for (const auto& record : records)
        frame.insert(frame.end(), record.begin(), record.end());
    return frame;
}

void frames_split_a_lone_presence_record() {
    start_case("frames_split_a_lone_presence_record");
    const std::vector<uint8_t> presence(std::begin(kSmallRecord), std::end(kSmallRecord));
    const auto alone = frame_of({presence});
    const auto with = split_frame(true, alone);
    CHECK(with.error == WireError::ok && with.records.size() == 1);
    CHECK(with.records[0] == presence);
    const auto without = split_frame(false, alone);
    CHECK(without.error == WireError::ok && without.records.empty());

    std::vector<uint8_t> chat(65, 0);
    chat[0] = 0x05;
    const auto chat_then = split_frame(true, frame_of({chat, presence}));
    CHECK(chat_then.records.size() == 1 && chat_then.records[0] == chat);
    CHECK(split_frame(false, frame_of({chat, presence})).records == chat_then.records);

    auto presence_then = frame_of({presence});
    presence_then.push_back(0x05);
    CHECK(split_frame(true, presence_then).records.empty());

    const std::vector<uint8_t> over{0xff, 0xff, 0xff, 0xff, 0xf0, 0x64, 0x00, 0x01};
    CHECK(split_frame(true, over).records.empty());

    std::vector<uint8_t> ping(13, 0);
    ping[0] = 0x02;
    const std::vector<uint8_t> probe{0x06};
    const auto classic = frame_of({ping, probe});
    const auto flag_on = split_frame(true, classic);
    const auto flag_off = split_frame(false, classic);
    CHECK(flag_on.error == WireError::ok && flag_off.error == WireError::ok);
    CHECK(flag_on.records.size() == 2 && flag_on.records[0].size() == 13);
    CHECK(flag_on.records[1].size() == 1 && flag_on.records == flag_off.records);
}

void only_oa_blocks_are_peers() {
    start_case("only_oa_blocks_are_peers");
    CHECK(!presence_peer(nullptr));
    uint8_t block[player_info_block_bytes]{};
    CHECK(!presence_peer(block));
    mark_engine_signature(block);
    block[player_info_presence_revision_offset] = 0;
    CHECK(!presence_peer(block));
    block[player_info_presence_revision_offset] = 1;
    CHECK(presence_peer(block));
}

} // namespace

int main() {
    a_small_record_is_encoded_exactly();
    every_field_round_trips();
    lists_are_cut_to_fit();
    unknown_fields_are_skipped();
    a_bad_field_is_left_out();
    malformed_records_are_refused();
    append_and_find();
    frames_split_a_lone_presence_record();
    only_oa_blocks_are_peers();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("presence: all tests passed");
    return 0;
}
