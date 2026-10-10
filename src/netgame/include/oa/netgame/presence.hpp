// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The presence record, between OA machines only. A machine running 3.1c
// stops its split at the type byte and loses nothing else, because the
// record travels alone in its frame and is never sent to such a machine.
//
// The record, every integer little-endian. Fields fill it exactly.
//   0x00  u8 type 0xf0
//   0x01  u16 the record's whole length, the type byte included, 4 to 1024
//   0x03  u8 format version (1; a reader reads any version of 1 or more and
//         skips what it does not know)
//   0x04  fields to the end, each u8 tag, u16 value size, then the value
//
// A string of a to b is a u8 length from a to b and that many bytes. The
// bytes are ascii (0x21 to 0x7e), kebab (a to z, 0 to 9 and '-', not
// starting or ending with '-'), id (kebab with '.' too, as hack ids are)
// or text (well-formed UTF-8 without control characters).
//   0x01 engine: ascii string 1 to 32 the OA version text ("0.8.0", or
//        "0.8.0-dev" for a development build), ascii string 0 to 16 the
//        platform ("Windows", "macOS", "Linux", "iOS", "Android"), ascii
//        string 0 to 16 the architecture ("x64", "x86", "arm64", "armhf").
//   0x10 mod: kebab string 1 to 64 the mod's id, text string 1 to 64 its
//        name, text string 0 to 32 its version, u16 its packaging revision.
//        Absent: 3.1c's rules, no mod.
//   0x11 mod origin: kebab string 1 to 32 the registry's id, ascii string
//        0 to 255 its descriptor URL (empty for a built-in registry, for
//        one added from a file, and whenever the sender cannot give it in
//        255 bytes), u32 the catalogue release (1 or more), 32 bytes the
//        package's SHA-256.
//   0x20 sim hash: 32 bytes, the sim hash the machine plays.
//   0x21 game hacks and 0x22 view hacks: u16 the number of such hacks on,
//        then as many ids as were sent, each an id string 1 to 64. Fewer
//        ids than the count means the list was cut.
//   0x23 developer: u8 flags (bit 0 Developer Mode on, other bits 0), u16
//        the number of overrides, then as many as were sent, each u8 on
//        (0 or 1) and an id string 1 to 64.
//   0x40 map pack: kebab string 1 to 64 the package key, u32 the catalogue
//        release, 32 bytes the package's SHA-256, kebab string 1 to 32 the
//        registry's id, ascii string 0 to 255 its descriptor URL (empty as
//        for 0x11), text string 0 to 64 the pack's name, text string 0 to
//        32 its version (both shown only; empty when the sender cannot
//        give them within those sizes).

#include "oa/netgame/wire.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oa::netgame {

/// Type byte of a presence record. Above 3.1c's table, and not a recorder record.
inline constexpr uint8_t presence_record_type = 0xf0;
/// The most bytes a presence record may be, the type byte included.
inline constexpr std::size_t presence_record_max_bytes = 1024;
/// Type byte, length word and format version.
inline constexpr std::size_t presence_record_header_bytes = 4;
/// Format version this engine writes. A reader accepts 1 or more.
inline constexpr uint8_t presence_record_version = 1;
/// Tag byte and the u16 value size that follow it.
inline constexpr std::size_t presence_field_header_bytes = 3;
/// The largest whole map-pack field: 3 bytes of field header plus 65 + 4 +
/// 32 + 33 + 256 + 65 + 33 of value, every string at its longest (the key,
/// the release, the SHA-256, the registry id, the descriptor URL, the name
/// and the version). The sender keeps that much room free for the host's
/// map pack.
inline constexpr std::size_t presence_map_pack_field_max_bytes = 491;
static_assert(
    presence_map_pack_field_max_bytes ==
    presence_field_header_bytes + 65 + 4 + 32 + 33 + 256 + 65 + 33
);

/// Tags of the fields a presence record carries. A tag this engine does not
/// know is skipped.
enum class PresenceTag : uint8_t {
    engine = 0x01,     ///< OA version, platform and architecture
    mod = 0x10,        ///< the mod's id, name, version and packaging revision
    mod_origin = 0x11, ///< the registry, release and SHA-256 the mod came from
    sim_hash = 0x20,   ///< the sim hash the machine plays
    game_hacks = 0x21, ///< game hacks that are on
    view_hacks = 0x22, ///< view hacks that are on
    developer = 0x23,  ///< Developer Mode and its overrides
    map_pack = 0x40,   ///< the map pack the host is playing
};

/// 32 bytes: a SHA-256, or the sim hash.
using PresenceDigest = std::array<uint8_t, 32>;

/// The engine line of a presence record.
struct PresenceEngine {
    std::string version;      ///< OA version text, such as "0.8.0"
    std::string platform;     ///< "Windows", "macOS", "Linux", "iOS" or "Android"; may be empty
    std::string architecture; ///< "x64", "x86", "arm64" or "armhf"; may be empty

    /// Compares two engine lines.
    friend bool operator==(const PresenceEngine&, const PresenceEngine&) = default;
};

/// The mod a presence record names. Absent when the machine plays 3.1c's rules.
struct PresenceMod {
    std::string id;                ///< the mod's id
    std::string name;              ///< the name the card shows
    std::string version;           ///< the mod's version; may be empty
    uint16_t packaging_revision{}; ///< the mod's packaging revision

    /// Compares two mods.
    friend bool operator==(const PresenceMod&, const PresenceMod&) = default;
};

/// Where a mod or a map pack was published.
struct PresenceOrigin {
    std::string registry;       ///< the registry's id
    std::string descriptor_url; ///< the registry's descriptor URL; empty when the sender has none
    uint32_t release{};         ///< the catalogue release, 1 or more
    PresenceDigest sha256{};    ///< the package's SHA-256

    /// Compares two origins.
    friend bool operator==(const PresenceOrigin&, const PresenceOrigin&) = default;
};

/// Hacks of one kind that are on. `ids` may be shorter than `total` when the list was cut.
struct PresenceHacks {
    uint16_t total{};             ///< how many such hacks are on
    std::vector<std::string> ids; ///< the ids that were sent, in order

    /// Compares two hack lists.
    friend bool operator==(const PresenceHacks&, const PresenceHacks&) = default;
};

/// One developer override.
struct PresenceOverride {
    std::string hack; ///< the hack's id
    bool on{};        ///< the override's switch

    /// Compares two overrides.
    friend bool operator==(const PresenceOverride&, const PresenceOverride&) = default;
};

/// Developer Mode and the overrides that were sent. `overrides` may be shorter than `total`.
struct PresenceDeveloper {
    bool on{};                               ///< Developer Mode is on
    uint16_t total{};                        ///< how many overrides are set
    std::vector<PresenceOverride> overrides; ///< the overrides that were sent, in order

    /// Compares two developer blocks.
    friend bool operator==(const PresenceDeveloper&, const PresenceDeveloper&) = default;
};

/// The map pack a presence record names.
struct PresenceMapPack {
    std::string key;            ///< the package key
    uint32_t release{};         ///< the catalogue release
    PresenceDigest sha256{};    ///< the package's SHA-256
    std::string registry;       ///< the registry's id
    std::string descriptor_url; ///< the registry's descriptor URL; empty when the sender has none
    std::string name;           ///< the name the card shows; empty when it would not fit
    std::string version;        ///< the pack's version; empty when it would not fit

    /// Compares two map packs.
    friend bool operator==(const PresenceMapPack&, const PresenceMapPack&) = default;
};

/// One presence record. A field that was not sent, or whose value broke its rule, is absent.
struct PresenceRecord {
    uint8_t version{presence_record_version}; ///< format version, 1 or more
    std::optional<PresenceEngine> engine;
    std::optional<PresenceMod> mod;
    std::optional<PresenceOrigin> mod_origin;
    std::optional<PresenceDigest> sim_hash;
    std::optional<PresenceHacks> game_hacks;
    std::optional<PresenceHacks> view_hacks;
    std::optional<PresenceDeveloper> developer;
    std::optional<PresenceMapPack> map_pack;

    /// Compares two records.
    friend bool operator==(const PresenceRecord&, const PresenceRecord&) = default;
};

/// Returns the length of the presence record at bytes[0], from its length word.
///
/// The bytes after the length word need not be present. A length word of 4
/// to 1024 is the record's length.
///
/// @param bytes record start; null is refused
/// @param available bytes readable from bytes
/// @param[out] length the record's length, the type byte included
/// @return ok; invalid_type when the byte is not 0xf0; truncated when fewer
///         than 3 bytes are readable; length_mismatch when the length word
///         is outside 4 to 1024; bad_argument for a null pointer
[[nodiscard]] WireError
presence_record_length(const uint8_t* bytes, std::size_t available, uint16_t* length) noexcept;

/// Writes a presence record.
///
/// Fields that are present go out in the order engine, mod, mod origin, sim
/// hash, map pack, developer, game hacks, view hacks. A list field carries
/// its full count and as many ids, in order, as keep the record within
/// min(capacity, 1024). A string that does not fit its rule is refused; it
/// is never cut. The record is never longer than 1024 bytes.
///
/// @param record the fields to write
/// @param[out] out storage for the record
/// @param capacity bytes available at out
/// @param[out] written the record's length
/// @return ok; bad_argument for a null pointer, a version of 0, or a value
///         outside its rule; buffer_too_small when the fixed fields do not fit
[[nodiscard]] WireError encode_presence(
    const PresenceRecord& record, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept;

/// Reads a presence record.
///
/// A wrong frame, a version of 0, or a field that runs past the end refuses
/// the whole record and leaves `out` unchanged. An unknown tag is skipped.
/// A known field whose value breaks its rule is left out. A repeated tag
/// keeps the first occurrence. It may allocate.
///
/// @param record the record, type byte first
/// @param size its length in bytes; nothing past size is read
/// @param[out] out the fields that were kept
/// @return ok; invalid_type; truncated; length_mismatch when the length word
///         is not size or is outside 4 to 1024; bad_argument for a null
///         pointer or a version of 0
[[nodiscard]] WireError
decode_presence(const uint8_t* record, std::size_t size, PresenceRecord* out);

/// Finds the first value of a tag in a well-framed record.
///
/// The value is not checked against the tag's rule. A record whose frame is
/// wrong, or a tag that is absent, answers false and leaves `value` unchanged.
///
/// @param record the record, type byte first
/// @param size its length in bytes
/// @param tag the tag to find
/// @param[out] value the first value's bytes, inside the record
/// @return true when the record is well-framed and the tag occurs
[[nodiscard]] bool find_presence_field(
    const uint8_t* record, std::size_t size, PresenceTag tag, std::span<const uint8_t>* value
) noexcept;

/// Reads the sim hash of a well-framed record, without allocating.
///
/// The first 0x20 field is the hash when its value is 32 bytes.
///
/// @param record the record, type byte first; null is nothing
/// @param size its length in bytes
/// @return the hash, or nothing
[[nodiscard]] std::optional<PresenceDigest>
presence_sim_hash(const uint8_t* record, std::size_t size) noexcept;

/// Writes the value of a map-pack field, tag 0x40, without its field header.
///
/// @param pack the map pack
/// @param[out] out storage for the value
/// @param capacity bytes available at out
/// @param[out] written the value's length
/// @return ok; bad_argument for a null pointer or a value outside its rule;
///         buffer_too_small
[[nodiscard]] WireError encode_presence_map_pack(
    const PresenceMapPack& pack, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept;

/// Adds a field to a well-framed presence record and fixes its length word.
///
/// The field is written at the record's end. A result past 1024 bytes, or
/// past capacity, is refused and the record is left unchanged.
///
/// @param[in,out] record the record, type byte first
/// @param size the record's length before the field is added
/// @param capacity bytes available at record
/// @param tag the field's tag
/// @param value the field's value
/// @param[out] written the record's length after the field is added
/// @return ok; bad_argument for a null pointer, a value of more than 65535
///         bytes, or a record that is not well-framed; buffer_too_small
[[nodiscard]] WireError append_presence_field(
    uint8_t* record,
    std::size_t size,
    std::size_t capacity,
    PresenceTag tag,
    std::span<const uint8_t> value,
    std::size_t* written
) noexcept;

/// Tells whether a peer may receive a presence record.
///
/// This is the one rule: the setup block carries the engine signature and a
/// presence revision of 1 or more, as read_presence reads it.
///
/// @param block the 0xb9-byte setup block; null is no
/// @return true when the peer may receive a presence record
[[nodiscard]] bool presence_peer(const uint8_t* block) noexcept;

} // namespace oa::netgame
