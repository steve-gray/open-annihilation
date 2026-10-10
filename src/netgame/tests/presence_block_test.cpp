// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Presence in the setup block: the five spare bytes, the version a build
// writes, and that a block is read only when it says Open Annihilation sent
// it and names a revision.

#include "oa/core/player_setup.h"
#include "oa/netgame/presence_block.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>

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
    std::printf("presence block: %s\n", name);
    std::fflush(stdout);
}

// The constants are the five bytes after the engine signature, and the core
// record lays its fields on the same offsets.
void the_offsets_are_the_spare_bytes() {
    start_case("the_offsets_are_the_spare_bytes");
    CHECK(player_info_presence_revision_offset == 0xaf);
    CHECK(player_info_oa_version_offset == 0xb0);
    CHECK(player_info_presence_flags_offset == 0xb3);
    CHECK(offsetof(oa::PlayerSetupInfo, presence_revision) == 0xaf);
    CHECK(offsetof(oa::PlayerSetupInfo, oa_version_major) == 0xb0);
    CHECK(offsetof(oa::PlayerSetupInfo, oa_version_minor) == 0xb1);
    CHECK(offsetof(oa::PlayerSetupInfo, oa_version_patch) == 0xb2);
    CHECK(offsetof(oa::PlayerSetupInfo, presence_flags) == 0xb3);
    CHECK(sizeof(oa::PlayerSetupInfo) == 0xb9);
}

// A release version keeps its patch. A development label, and a version that
// is not three numbers in range, is patch 255.
void versions_and_development_builds() {
    start_case("versions_and_development_builds");
    const auto release = presence_version("0.8.0", "");
    CHECK(release.revision == 1 && release.flags == 0);
    CHECK(release.major == 0 && release.minor == 8 && release.patch == 0);
    CHECK(!development_build(release));
    const auto development = presence_version("0.8.0", "dev");
    CHECK(development.major == 0 && development.minor == 8 && development.patch == 255);
    CHECK(development_build(development));
    const auto rejected = {"1.2", "1.2.3.4", "0.8.255", "256.0.0", "0.8.x", ""};
    for (const std::string_view version : rejected) {
        const auto block = presence_version(version, "");
        CHECK(block.revision == 1 && block.flags == 0);
        CHECK(block.major == 0 && block.minor == 0 && block.patch == 255);
    }
}

// Stamping writes the five bytes and no others. Flags past the known bits
// are not written. A revision of 0 writes nothing.
void stamping_writes_five_bytes() {
    start_case("stamping_writes_five_bytes");
    // The record is a wire layout. Filling a byte array and copying it in
    // keeps the "every other byte stays 0x5a" proof without writing the
    // struct through memset (its default member initialisers make it
    // non-trivial, so that call is rejected).
    static_assert(std::is_trivially_copyable_v<PlayerInfoRecord>);
    alignas(PlayerInfoRecord) unsigned char filled[sizeof(PlayerInfoRecord)];
    std::memset(filled, 0x5a, sizeof filled);
    PlayerInfoRecord record;
    std::memcpy(&record, filled, sizeof record);
    PresenceBlock block{};
    block.revision = 1;
    block.major = 0;
    block.minor = 8;
    block.patch = 255;
    block.flags = 0xff;
    stamp_presence(record, block);
    const auto* tail = record.info_tail;
    const auto at = player_info_presence_revision_offset - player_info_tail_offset;
    CHECK(tail[at] == 1 && tail[at + 1] == 0 && tail[at + 2] == 8 && tail[at + 3] == 255);
    CHECK(tail[at + 4] == presence_flag::known);
    bool kept = true;
    for (std::size_t index = 0; index < sizeof record.info_tail; ++index)
        if (index < at || index > at + 4)
            kept = kept && tail[index] == 0x5a;
    for (const auto byte : record.info_head)
        kept = kept && byte == 0x5a;
    const auto* id = reinterpret_cast<const uint8_t*>(&record.player_id);
    for (std::size_t index = 0; index < sizeof record.player_id; ++index)
        kept = kept && id[index] == 0x5a;
    CHECK(kept);

    uint8_t kept_block[player_info_block_bytes];
    std::memset(kept_block, 0x5a, sizeof kept_block);
    mark_presence(kept_block, block);
    CHECK(kept_block[0xaf] == 1 && kept_block[0xb3] == presence_flag::known);
    CHECK(kept_block[0xae] == 0x5a && kept_block[0xb4] == 0x5a);

    const auto before = record;
    block.revision = 0;
    stamp_presence(record, block);
    CHECK(std::memcmp(&record, &before, sizeof record) == 0);
    const auto kept_before = kept_block[0xaf];
    mark_presence(kept_block, block);
    CHECK(kept_block[0xaf] == kept_before);
}

// A block is read only with the engine signature and a revision of 1 or
// more. A higher revision is kept. Flag bits past the known ones are dropped.
void reading_needs_the_signature_and_a_revision() {
    start_case("reading_needs_the_signature_and_a_revision");
    CHECK(!read_presence(nullptr));
    uint8_t block[player_info_block_bytes]{};
    block[player_info_presence_revision_offset] = 1;
    CHECK(!read_presence(block));
    mark_engine_signature(block);
    block[player_info_presence_revision_offset] = 0;
    CHECK(!read_presence(block));
    block[player_info_presence_revision_offset] = 1;
    block[player_info_oa_version_offset] = 0;
    block[player_info_oa_version_offset + 1] = 8;
    block[player_info_oa_version_offset + 2] = 255;
    block[player_info_presence_flags_offset] = 0x05;
    const auto read = read_presence(block);
    CHECK(read && read->revision == 1 && read->major == 0 && read->minor == 8);
    CHECK(read && read->patch == 255 && read->flags == 0x05);
    block[player_info_presence_revision_offset] = 7;
    block[player_info_presence_flags_offset] = 0xff;
    const auto higher = read_presence(block);
    CHECK(higher && higher->revision == 7 && higher->flags == presence_flag::known);
}

} // namespace

int main() {
    the_offsets_are_the_spare_bytes();
    versions_and_development_builds();
    stamping_writes_five_bytes();
    reading_needs_the_signature_and_a_revision();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("presence block: all tests passed");
    return 0;
}
