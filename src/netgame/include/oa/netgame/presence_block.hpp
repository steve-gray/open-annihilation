// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Presence in the setup block: five bytes every machine can see, OA or not,
// in the same record 0x20 3.1c sends. 3.1c carries the bytes unchanged and
// never reads them. The 3.1c version fields and the disc bit are not these
// bytes and are left as they are.
//
// 0xAF is the presence revision (1). 0xB0, 0xB1 and 0xB2 are the OA version,
// major, minor and patch, and a patch of 255 is a development build. 0xB3
// is the flags: Developer Mode, rules that differ from 3.1c, view hacks on,
// the sender sends presence records, and the sender can fetch content.
// Bits past those are sent as 0 and ignored when read. A revision of 0
// writes nothing.

#include "oa/core/player_setup.h"
#include "oa/netgame/records.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace oa::netgame {

/// Block offset of the presence revision.
inline constexpr std::size_t player_info_presence_revision_offset = 0xaf;
/// Block offset of the OA version: major, then minor, then patch.
inline constexpr std::size_t player_info_oa_version_offset = 0xb0;
/// Block offset of the presence flags (presence_flag).
inline constexpr std::size_t player_info_presence_flags_offset = 0xb3;
/// The presence revision this engine writes.
inline constexpr uint8_t presence_revision = OA_SETUP_PRESENCE_REVISION;
/// The patch byte of a development build.
inline constexpr uint8_t presence_dev_patch = OA_SETUP_PRESENCE_DEV_PATCH;

/// Bits of the presence flags. known is every bit this engine reads; a bit
/// past it is ignored.
namespace presence_flag {
inline constexpr uint8_t developer_mode = OA_SETUP_PRESENCE_DEVELOPER_MODE;
inline constexpr uint8_t rules_differ = OA_SETUP_PRESENCE_RULES_DIFFER;
inline constexpr uint8_t view_hacks = OA_SETUP_PRESENCE_VIEW_HACKS;
inline constexpr uint8_t sends_records = OA_SETUP_PRESENCE_SENDS_RECORDS;
inline constexpr uint8_t fetches_content = OA_SETUP_PRESENCE_FETCHES_CONTENT;
inline constexpr uint8_t known = 0x1f;
} // namespace presence_flag

/// The five presence bytes of one setup block.
struct PresenceBlock {
    uint8_t revision{}; ///< presence revision; 0 writes nothing
    uint8_t major{};    ///< OA version major
    uint8_t minor{};    ///< OA version minor
    uint8_t patch{};    ///< OA version patch; 255 is a development build
    uint8_t flags{};    ///< presence_flag bits

    /// Compares two blocks: every byte.
    ///
    /// @param left one block
    /// @param right the other
    /// @return true when every byte is equal
    friend bool operator==(const PresenceBlock& left, const PresenceBlock& right) = default;
};

/// Builds the presence this build writes for a version and a label.
///
/// The revision is 1 and the flags are 0. version must be exactly three
/// dot-separated decimal numbers, major and minor 0 to 255 and patch 0 to
/// 254; anything else is major 0, minor 0, patch 255. A label that is not
/// empty is a development build and makes the patch 255.
///
/// @param version the engine version, such as "0.8.0"
/// @param label the version's label; empty for a release build
/// @return the five bytes
[[nodiscard]] PresenceBlock
presence_version(std::string_view version, std::string_view label) noexcept;

/// Writes the presence into a setup block about to be sent (record 0x20).
///
/// The flags are masked to presence_flag::known. A revision of 0 writes
/// nothing at all.
///
/// @param[in,out] record the block, as record 0x20 carries it
/// @param block the presence
void stamp_presence(PlayerInfoRecord& record, const PresenceBlock& block) noexcept;

/// Writes the presence into a setup block this machine keeps, so every copy
/// and every send of it carries the bytes. The flags are masked to
/// presence_flag::known. A revision of 0 writes nothing at all.
///
/// @param[in,out] block the 0xb9-byte block
/// @param presence the presence
void mark_presence(uint8_t* block, const PresenceBlock& presence) noexcept;

/// Reads the presence a setup block carries.
///
/// Answers only when the block says Open Annihilation sent it and the
/// revision is 1 or more. The flags are masked to presence_flag::known.
///
/// @param block the 0xb9-byte block; null is nothing
/// @return the presence, or nothing
[[nodiscard]] std::optional<PresenceBlock> read_presence(const uint8_t* block) noexcept;

/// Tells whether a presence is a development build: its patch is 255.
///
/// @param block the presence
/// @return true when the patch is 255
[[nodiscard]] constexpr bool development_build(const PresenceBlock& block) noexcept {
    return block.patch == presence_dev_patch;
}

} // namespace oa::netgame
