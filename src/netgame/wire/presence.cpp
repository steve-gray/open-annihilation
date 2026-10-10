// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/presence.hpp"

#include "oa/netgame/presence_block.hpp"

#include <cassert>
#include <cstring>
#include <string_view>

namespace oa::netgame {
namespace {

/// Bit 0 of the developer flags: Developer Mode is on. Every other bit is 0.
constexpr uint8_t developer_mode_flag = 0x01;

constexpr std::size_t digest_bytes = 32;
constexpr std::size_t version_text_max = 32;
constexpr std::size_t platform_text_max = 16;
constexpr std::size_t architecture_text_max = 16;
constexpr std::size_t mod_id_max = 64;
constexpr std::size_t mod_name_max = 64;
constexpr std::size_t mod_version_max = 32;
constexpr std::size_t registry_id_max = 32;
constexpr std::size_t descriptor_url_max = 255;
constexpr std::size_t hack_id_max = 64;
constexpr std::size_t pack_key_max = 64;
constexpr std::size_t pack_name_max = 64;
constexpr std::size_t pack_version_max = 32;

enum class ByteKind { ascii, kebab, id, text };

/// Tells whether a code unit is an ascii byte of the record's strings.
bool ascii_byte(unsigned char byte) noexcept {
    return byte >= 0x21 && byte <= 0x7e;
}

/// Tells whether a string is kebab, or an id when dots are allowed.
bool kebab_text(std::string_view text, bool dots) noexcept {
    if (text.empty() || text.front() == '-' || text.back() == '-')
        return false;
    for (const char byte : text) {
        const auto unit = static_cast<unsigned char>(byte);
        if (unit >= 'a' && unit <= 'z')
            continue;
        if (unit >= '0' && unit <= '9')
            continue;
        if (byte == '-' || (dots && byte == '.'))
            continue;
        return false;
    }
    return true;
}

/// Tells whether text is well-formed UTF-8 with no control character.
bool text_bytes(const uint8_t* bytes, std::size_t size) noexcept {
    std::size_t at = 0;
    while (at < size) {
        const auto lead = bytes[at];
        std::size_t length = 0;
        uint32_t value = 0;
        uint32_t least = 0;
        if (lead < 0x80) {
            if (lead < 0x20 || lead == 0x7f)
                return false;
            ++at;
            continue;
        }
        if ((lead & 0xe0) == 0xc0) {
            length = 2;
            value = lead & 0x1fu;
            least = 0x80;
        } else if ((lead & 0xf0) == 0xe0) {
            length = 3;
            value = lead & 0x0fu;
            least = 0x800;
        } else if ((lead & 0xf8) == 0xf0) {
            length = 4;
            value = lead & 0x07u;
            least = 0x10000;
        } else {
            return false;
        }
        if (size - at < length)
            return false;
        for (std::size_t next = 1; next < length; ++next) {
            const auto byte = bytes[at + next];
            if ((byte & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (byte & 0x3fu);
        }
        constexpr uint32_t first_surrogate = 0xd800;
        constexpr uint32_t last_surrogate = 0xdfff;
        constexpr uint32_t last_code_point = 0x10ffff;
        if (value < least || value > last_code_point ||
            (value >= first_surrogate && value <= last_surrogate))
            return false;
        if (value < 0x20 || value == 0x7f || (value >= 0x80 && value <= 0x9f))
            return false;
        at += length;
    }
    return true;
}

/// Tells whether a string is one of the record's four kinds, of a length in range.
bool string_in_rule(
    std::string_view text, std::size_t least, std::size_t most, ByteKind kind
) noexcept {
    if (text.size() < least || text.size() > most)
        return false;
    switch (kind) {
    case ByteKind::ascii:
        for (const char byte : text)
            if (!ascii_byte(static_cast<unsigned char>(byte)))
                return false;
        return true;
    case ByteKind::kebab:
        return text.empty() ? least == 0 : kebab_text(text, false);
    case ByteKind::id:
        return kebab_text(text, true);
    case ByteKind::text:
        return text_bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
    return false;
}

/// A window over bytes that never reads past its end.
struct Window {
    const uint8_t* data{};
    std::size_t size{};

    /// Takes n bytes, or refuses when they are not there.
    bool take(std::size_t n, const uint8_t** out) noexcept {
        if (n > size)
            return false;
        *out = data;
        data += n;
        size -= n;
        return true;
    }

    bool u8(uint8_t* out) noexcept {
        const uint8_t* bytes = nullptr;
        if (!take(1, &bytes))
            return false;
        *out = bytes[0];
        return true;
    }

    bool u16(uint16_t* out) noexcept {
        const uint8_t* bytes = nullptr;
        if (!take(2, &bytes))
            return false;
        *out = load_u16(bytes);
        return true;
    }

    bool u32(uint32_t* out) noexcept {
        const uint8_t* bytes = nullptr;
        if (!take(4, &bytes))
            return false;
        *out = load_u32(bytes);
        return true;
    }
};

/// Reads one length-prefixed string of the given rule.
bool read_string(
    Window* window, std::size_t least, std::size_t most, ByteKind kind, std::string* out
) {
    uint8_t length = 0;
    if (!window->u8(&length) || length < least || length > most)
        return false;
    const uint8_t* bytes = nullptr;
    if (!window->take(length, &bytes))
        return false;
    const std::string_view text(reinterpret_cast<const char*>(bytes), length);
    if (!string_in_rule(text, least, most, kind))
        return false;
    *out = std::string(text);
    return true;
}

/// Appends raw bytes. The caller has already kept the record within its limit.
void put_bytes(uint8_t* out, std::size_t* used, const void* bytes, std::size_t size) noexcept {
    if (size != 0)
        std::memcpy(out + *used, bytes, size);
    *used += size;
}

void put_u8(uint8_t* out, std::size_t* used, uint8_t value) noexcept {
    out[(*used)++] = value;
}

void put_u16(uint8_t* out, std::size_t* used, uint16_t value) noexcept {
    store_u16(out + *used, value);
    *used += 2;
}

void put_u32(uint8_t* out, std::size_t* used, uint32_t value) noexcept {
    store_u32(out + *used, value);
    *used += 4;
}

void put_string(uint8_t* out, std::size_t* used, std::string_view text) noexcept {
    put_u8(out, used, static_cast<uint8_t>(text.size()));
    put_bytes(out, used, text.data(), text.size());
}

/// Bytes of one length-prefixed string.
std::size_t string_bytes(std::string_view text) noexcept {
    return 1 + text.size();
}

/// Tells whether the record's frame is whole: length, version and fields that fill it.
WireError framed_record(const uint8_t* record, std::size_t size) noexcept {
    uint16_t length = 0;
    const auto error = presence_record_length(record, size, &length);
    if (error != WireError::ok)
        return error;
    if (static_cast<std::size_t>(length) != size)
        return WireError::length_mismatch;
    if (size < presence_record_header_bytes)
        return WireError::truncated;
    assert(size <= presence_record_max_bytes);
    if (record[3] == 0)
        return WireError::bad_argument;
    std::size_t at = presence_record_header_bytes;
    while (at < size) {
        assert(at < size);
        if (size - at < presence_field_header_bytes)
            return WireError::truncated;
        const auto value_size = static_cast<std::size_t>(load_u16(record + at + 1));
        if (value_size > size - at - presence_field_header_bytes)
            return WireError::truncated;
        at += presence_field_header_bytes + value_size;
    }
    assert(at == size);
    return WireError::ok;
}

bool engine_ok(const PresenceEngine& engine) noexcept {
    return string_in_rule(engine.version, 1, version_text_max, ByteKind::ascii) &&
           string_in_rule(engine.platform, 0, platform_text_max, ByteKind::ascii) &&
           string_in_rule(engine.architecture, 0, architecture_text_max, ByteKind::ascii);
}

std::size_t engine_value_bytes(const PresenceEngine& engine) noexcept {
    return string_bytes(engine.version) + string_bytes(engine.platform) +
           string_bytes(engine.architecture);
}

bool mod_ok(const PresenceMod& mod) noexcept {
    return string_in_rule(mod.id, 1, mod_id_max, ByteKind::kebab) &&
           string_in_rule(mod.name, 1, mod_name_max, ByteKind::text) &&
           string_in_rule(mod.version, 0, mod_version_max, ByteKind::text);
}

std::size_t mod_value_bytes(const PresenceMod& mod) noexcept {
    return string_bytes(mod.id) + string_bytes(mod.name) + string_bytes(mod.version) + 2;
}

bool origin_ok(const PresenceOrigin& origin) noexcept {
    return origin.release >= 1 &&
           string_in_rule(origin.registry, 1, registry_id_max, ByteKind::kebab) &&
           string_in_rule(origin.descriptor_url, 0, descriptor_url_max, ByteKind::ascii);
}

std::size_t origin_value_bytes(const PresenceOrigin& origin) noexcept {
    return string_bytes(origin.registry) + string_bytes(origin.descriptor_url) + 4 + digest_bytes;
}

bool hack_id_ok(std::string_view id) noexcept {
    return string_in_rule(id, 1, hack_id_max, ByteKind::id);
}

bool hacks_ok(const PresenceHacks& hacks) noexcept {
    if (hacks.ids.size() > hacks.total)
        return false;
    for (const auto& id : hacks.ids)
        if (!hack_id_ok(id))
            return false;
    return true;
}

bool developer_ok(const PresenceDeveloper& developer) noexcept {
    if (developer.overrides.size() > developer.total)
        return false;
    for (const auto& over : developer.overrides)
        if (!hack_id_ok(over.hack))
            return false;
    return true;
}

bool map_pack_ok(const PresenceMapPack& pack) noexcept {
    return string_in_rule(pack.key, 1, pack_key_max, ByteKind::kebab) &&
           string_in_rule(pack.registry, 1, registry_id_max, ByteKind::kebab) &&
           string_in_rule(pack.descriptor_url, 0, descriptor_url_max, ByteKind::ascii) &&
           string_in_rule(pack.name, 0, pack_name_max, ByteKind::text) &&
           string_in_rule(pack.version, 0, pack_version_max, ByteKind::text);
}

std::size_t map_pack_value_bytes(const PresenceMapPack& pack) noexcept {
    return string_bytes(pack.key) + 4 + digest_bytes + string_bytes(pack.registry) +
           string_bytes(pack.descriptor_url) + string_bytes(pack.name) + string_bytes(pack.version);
}

void write_engine(uint8_t* out, std::size_t* used, const PresenceEngine& engine) noexcept {
    put_u8(out, used, static_cast<uint8_t>(PresenceTag::engine));
    put_u16(out, used, static_cast<uint16_t>(engine_value_bytes(engine)));
    put_string(out, used, engine.version);
    put_string(out, used, engine.platform);
    put_string(out, used, engine.architecture);
}

void write_mod(uint8_t* out, std::size_t* used, const PresenceMod& mod) noexcept {
    put_u8(out, used, static_cast<uint8_t>(PresenceTag::mod));
    put_u16(out, used, static_cast<uint16_t>(mod_value_bytes(mod)));
    put_string(out, used, mod.id);
    put_string(out, used, mod.name);
    put_string(out, used, mod.version);
    put_u16(out, used, mod.packaging_revision);
}

void write_origin(uint8_t* out, std::size_t* used, const PresenceOrigin& origin) noexcept {
    put_u8(out, used, static_cast<uint8_t>(PresenceTag::mod_origin));
    put_u16(out, used, static_cast<uint16_t>(origin_value_bytes(origin)));
    put_string(out, used, origin.registry);
    put_string(out, used, origin.descriptor_url);
    put_u32(out, used, origin.release);
    put_bytes(out, used, origin.sha256.data(), digest_bytes);
}

void write_sim(uint8_t* out, std::size_t* used, const PresenceDigest& hash) noexcept {
    put_u8(out, used, static_cast<uint8_t>(PresenceTag::sim_hash));
    put_u16(out, used, static_cast<uint16_t>(digest_bytes));
    put_bytes(out, used, hash.data(), digest_bytes);
}

void write_map_pack_value(uint8_t* out, std::size_t* used, const PresenceMapPack& pack) noexcept {
    put_string(out, used, pack.key);
    put_u32(out, used, pack.release);
    put_bytes(out, used, pack.sha256.data(), digest_bytes);
    put_string(out, used, pack.registry);
    put_string(out, used, pack.descriptor_url);
    put_string(out, used, pack.name);
    put_string(out, used, pack.version);
}

void write_map_pack(uint8_t* out, std::size_t* used, const PresenceMapPack& pack) noexcept {
    put_u8(out, used, static_cast<uint8_t>(PresenceTag::map_pack));
    put_u16(out, used, static_cast<uint16_t>(map_pack_value_bytes(pack)));
    write_map_pack_value(out, used, pack);
}

bool read_engine(Window window, PresenceEngine* out) {
    PresenceEngine engine;
    if (!read_string(&window, 1, version_text_max, ByteKind::ascii, &engine.version) ||
        !read_string(&window, 0, platform_text_max, ByteKind::ascii, &engine.platform) ||
        !read_string(&window, 0, architecture_text_max, ByteKind::ascii, &engine.architecture) ||
        window.size != 0)
        return false;
    *out = std::move(engine);
    return true;
}

bool read_mod(Window window, PresenceMod* out) {
    PresenceMod mod;
    if (!read_string(&window, 1, mod_id_max, ByteKind::kebab, &mod.id) ||
        !read_string(&window, 1, mod_name_max, ByteKind::text, &mod.name) ||
        !read_string(&window, 0, mod_version_max, ByteKind::text, &mod.version) ||
        !window.u16(&mod.packaging_revision) || window.size != 0)
        return false;
    *out = std::move(mod);
    return true;
}

bool read_origin(Window window, PresenceOrigin* out) {
    PresenceOrigin origin;
    const uint8_t* digest = nullptr;
    if (!read_string(&window, 1, registry_id_max, ByteKind::kebab, &origin.registry) ||
        !read_string(&window, 0, descriptor_url_max, ByteKind::ascii, &origin.descriptor_url) ||
        !window.u32(&origin.release) || origin.release < 1 || !window.take(digest_bytes, &digest) ||
        window.size != 0)
        return false;
    std::memcpy(origin.sha256.data(), digest, digest_bytes);
    *out = std::move(origin);
    return true;
}

bool read_hacks(Window window, PresenceHacks* out) {
    PresenceHacks hacks;
    if (!window.u16(&hacks.total))
        return false;
    while (window.size != 0) {
        std::string id;
        if (!read_string(&window, 1, hack_id_max, ByteKind::id, &id))
            return false;
        hacks.ids.push_back(std::move(id));
    }
    if (hacks.ids.size() > hacks.total)
        return false;
    *out = std::move(hacks);
    return true;
}

bool read_developer(Window window, PresenceDeveloper* out) {
    PresenceDeveloper developer;
    uint8_t flags = 0;
    if (!window.u8(&flags) || (flags != 0 && flags != developer_mode_flag) ||
        !window.u16(&developer.total))
        return false;
    developer.on = flags == developer_mode_flag;
    while (window.size != 0) {
        PresenceOverride over;
        uint8_t on = 0;
        if (!window.u8(&on) || (on != 0 && on != 1) ||
            !read_string(&window, 1, hack_id_max, ByteKind::id, &over.hack))
            return false;
        over.on = on == 1;
        developer.overrides.push_back(std::move(over));
    }
    if (developer.overrides.size() > developer.total)
        return false;
    *out = std::move(developer);
    return true;
}

bool read_map_pack(Window window, PresenceMapPack* out) {
    PresenceMapPack pack;
    const uint8_t* digest = nullptr;
    if (!read_string(&window, 1, pack_key_max, ByteKind::kebab, &pack.key) ||
        !window.u32(&pack.release) || !window.take(digest_bytes, &digest) ||
        !read_string(&window, 1, registry_id_max, ByteKind::kebab, &pack.registry) ||
        !read_string(&window, 0, descriptor_url_max, ByteKind::ascii, &pack.descriptor_url) ||
        !read_string(&window, 0, pack_name_max, ByteKind::text, &pack.name) ||
        !read_string(&window, 0, pack_version_max, ByteKind::text, &pack.version) ||
        window.size != 0)
        return false;
    std::memcpy(pack.sha256.data(), digest, digest_bytes);
    *out = std::move(pack);
    return true;
}

} // namespace

WireError
presence_record_length(const uint8_t* bytes, std::size_t available, uint16_t* length) noexcept {
    if (bytes == nullptr || length == nullptr)
        return WireError::bad_argument;
    if (available < 3)
        return WireError::truncated;
    if (bytes[0] != presence_record_type)
        return WireError::invalid_type;
    const auto word = load_u16(bytes + 1);
    if (word < presence_record_header_bytes || word > presence_record_max_bytes)
        return WireError::length_mismatch;
    *length = word;
    return WireError::ok;
}

WireError encode_presence(
    const PresenceRecord& record, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept {
    if (out == nullptr || written == nullptr)
        return WireError::bad_argument;
    if (record.version == 0)
        return WireError::bad_argument;
    if (record.engine && !engine_ok(*record.engine))
        return WireError::bad_argument;
    if (record.mod && !mod_ok(*record.mod))
        return WireError::bad_argument;
    if (record.mod_origin && !origin_ok(*record.mod_origin))
        return WireError::bad_argument;
    if (record.developer && !developer_ok(*record.developer))
        return WireError::bad_argument;
    if (record.game_hacks && !hacks_ok(*record.game_hacks))
        return WireError::bad_argument;
    if (record.view_hacks && !hacks_ok(*record.view_hacks))
        return WireError::bad_argument;
    if (record.map_pack && !map_pack_ok(*record.map_pack))
        return WireError::bad_argument;

    const auto limit = capacity < presence_record_max_bytes ? capacity : presence_record_max_bytes;
    std::size_t fixed = presence_record_header_bytes;
    if (record.engine)
        fixed += presence_field_header_bytes + engine_value_bytes(*record.engine);
    if (record.mod)
        fixed += presence_field_header_bytes + mod_value_bytes(*record.mod);
    if (record.mod_origin)
        fixed += presence_field_header_bytes + origin_value_bytes(*record.mod_origin);
    if (record.sim_hash)
        fixed += presence_field_header_bytes + digest_bytes;
    if (record.map_pack)
        fixed += presence_field_header_bytes + map_pack_value_bytes(*record.map_pack);
    // A list's count is fixed. Only the ids after it are cut, and only
    // after every later list's count has its room.
    constexpr std::size_t hack_prefix = presence_field_header_bytes + 2;
    constexpr std::size_t developer_prefix = presence_field_header_bytes + 1 + 2;
    if (record.developer)
        fixed += developer_prefix;
    if (record.game_hacks)
        fixed += hack_prefix;
    if (record.view_hacks)
        fixed += hack_prefix;
    if (fixed > limit)
        return WireError::buffer_too_small;

    std::size_t dev_keep = 0;
    std::size_t game_keep = 0;
    std::size_t view_keep = 0;
    std::size_t room = limit - fixed;
    auto cut_overrides = [&]() noexcept {
        dev_keep = 0;
        if (!record.developer)
            return;
        for (const auto& over : record.developer->overrides) {
            const auto item = 2 + over.hack.size();
            if (item > room)
                break;
            room -= item;
            ++dev_keep;
        }
    };
    auto cut_ids = [&](const PresenceHacks* hacks, std::size_t* keep) noexcept {
        *keep = 0;
        if (hacks == nullptr)
            return;
        for (const auto& id : hacks->ids) {
            const auto item = 1 + id.size();
            if (item > room)
                break;
            room -= item;
            ++*keep;
        }
    };
    cut_overrides();
    cut_ids(record.game_hacks ? &*record.game_hacks : nullptr, &game_keep);
    cut_ids(record.view_hacks ? &*record.view_hacks : nullptr, &view_keep);

    uint8_t local[presence_record_max_bytes];
    std::size_t used = 0;
    put_u8(local, &used, presence_record_type);
    put_u16(local, &used, 0);
    put_u8(local, &used, record.version);
    if (record.engine)
        write_engine(local, &used, *record.engine);
    if (record.mod)
        write_mod(local, &used, *record.mod);
    if (record.mod_origin)
        write_origin(local, &used, *record.mod_origin);
    if (record.sim_hash)
        write_sim(local, &used, *record.sim_hash);
    if (record.map_pack)
        write_map_pack(local, &used, *record.map_pack);
    if (record.developer) {
        std::size_t value = 1 + 2;
        for (std::size_t i = 0; i < dev_keep; ++i)
            value += 2 + record.developer->overrides[i].hack.size();
        put_u8(local, &used, static_cast<uint8_t>(PresenceTag::developer));
        put_u16(local, &used, static_cast<uint16_t>(value));
        put_u8(local, &used, record.developer->on ? developer_mode_flag : 0);
        put_u16(local, &used, record.developer->total);
        for (std::size_t i = 0; i < dev_keep; ++i) {
            const auto& over = record.developer->overrides[i];
            put_u8(local, &used, over.on ? 1 : 0);
            put_string(local, &used, over.hack);
        }
    }
    auto write_hacks = [&](PresenceTag tag, const PresenceHacks& hacks, std::size_t keep) noexcept {
        std::size_t value = 2;
        for (std::size_t i = 0; i < keep; ++i)
            value += 1 + hacks.ids[i].size();
        put_u8(local, &used, static_cast<uint8_t>(tag));
        put_u16(local, &used, static_cast<uint16_t>(value));
        put_u16(local, &used, hacks.total);
        for (std::size_t i = 0; i < keep; ++i)
            put_string(local, &used, hacks.ids[i]);
    };
    if (record.game_hacks)
        write_hacks(PresenceTag::game_hacks, *record.game_hacks, game_keep);
    if (record.view_hacks)
        write_hacks(PresenceTag::view_hacks, *record.view_hacks, view_keep);
    assert(used <= limit && used <= presence_record_max_bytes);
    store_u16(local + 1, static_cast<uint16_t>(used));
    std::memcpy(out, local, used);
    *written = used;
    return WireError::ok;
}

WireError decode_presence(const uint8_t* record, std::size_t size, PresenceRecord* out) {
    if (record == nullptr || out == nullptr)
        return WireError::bad_argument;
    const auto error = framed_record(record, size);
    if (error != WireError::ok)
        return error;

    PresenceRecord decoded;
    decoded.version = record[3];
    bool saw_engine = false;
    bool saw_mod = false;
    bool saw_origin = false;
    bool saw_sim = false;
    bool saw_game = false;
    bool saw_view = false;
    bool saw_developer = false;
    bool saw_pack = false;
    std::size_t at = presence_record_header_bytes;
    while (at < size) {
        assert(at + presence_field_header_bytes <= size);
        const auto tag = record[at];
        const auto value_size = static_cast<std::size_t>(load_u16(record + at + 1));
        assert(value_size <= size - at - presence_field_header_bytes);
        Window value{record + at + presence_field_header_bytes, value_size};
        at += presence_field_header_bytes + value_size;
        if (tag == static_cast<uint8_t>(PresenceTag::engine)) {
            if (saw_engine)
                continue;
            saw_engine = true;
            PresenceEngine engine;
            if (read_engine(value, &engine))
                decoded.engine = std::move(engine);
        } else if (tag == static_cast<uint8_t>(PresenceTag::mod)) {
            if (saw_mod)
                continue;
            saw_mod = true;
            PresenceMod mod;
            if (read_mod(value, &mod))
                decoded.mod = std::move(mod);
        } else if (tag == static_cast<uint8_t>(PresenceTag::mod_origin)) {
            if (saw_origin)
                continue;
            saw_origin = true;
            PresenceOrigin origin;
            if (read_origin(value, &origin))
                decoded.mod_origin = std::move(origin);
        } else if (tag == static_cast<uint8_t>(PresenceTag::sim_hash)) {
            if (saw_sim)
                continue;
            saw_sim = true;
            if (value.size == digest_bytes) {
                PresenceDigest hash{};
                std::memcpy(hash.data(), value.data, digest_bytes);
                decoded.sim_hash = hash;
            }
        } else if (tag == static_cast<uint8_t>(PresenceTag::game_hacks)) {
            if (saw_game)
                continue;
            saw_game = true;
            PresenceHacks hacks;
            if (read_hacks(value, &hacks))
                decoded.game_hacks = std::move(hacks);
        } else if (tag == static_cast<uint8_t>(PresenceTag::view_hacks)) {
            if (saw_view)
                continue;
            saw_view = true;
            PresenceHacks hacks;
            if (read_hacks(value, &hacks))
                decoded.view_hacks = std::move(hacks);
        } else if (tag == static_cast<uint8_t>(PresenceTag::developer)) {
            if (saw_developer)
                continue;
            saw_developer = true;
            PresenceDeveloper developer;
            if (read_developer(value, &developer))
                decoded.developer = std::move(developer);
        } else if (tag == static_cast<uint8_t>(PresenceTag::map_pack)) {
            if (saw_pack)
                continue;
            saw_pack = true;
            PresenceMapPack pack;
            if (read_map_pack(value, &pack))
                decoded.map_pack = std::move(pack);
        }
    }
    *out = std::move(decoded);
    return WireError::ok;
}

bool find_presence_field(
    const uint8_t* record, std::size_t size, PresenceTag tag, std::span<const uint8_t>* value
) noexcept {
    if (record == nullptr || value == nullptr || framed_record(record, size) != WireError::ok)
        return false;
    const auto want = static_cast<uint8_t>(tag);
    std::size_t at = presence_record_header_bytes;
    while (at < size) {
        assert(at + presence_field_header_bytes <= size);
        const auto value_size = static_cast<std::size_t>(load_u16(record + at + 1));
        assert(value_size <= size - at - presence_field_header_bytes);
        const auto start = at + presence_field_header_bytes;
        if (record[at] == want) {
            *value = std::span<const uint8_t>(record + start, value_size);
            return true;
        }
        at = start + value_size;
    }
    return false;
}

std::optional<PresenceDigest> presence_sim_hash(const uint8_t* record, std::size_t size) noexcept {
    std::span<const uint8_t> value;
    if (!find_presence_field(record, size, PresenceTag::sim_hash, &value) ||
        value.size() != digest_bytes)
        return std::nullopt;
    PresenceDigest hash{};
    std::memcpy(hash.data(), value.data(), digest_bytes);
    return hash;
}

WireError encode_presence_map_pack(
    const PresenceMapPack& pack, uint8_t* out, std::size_t capacity, std::size_t* written
) noexcept {
    if (out == nullptr || written == nullptr)
        return WireError::bad_argument;
    if (!map_pack_ok(pack))
        return WireError::bad_argument;
    const auto size = map_pack_value_bytes(pack);
    if (size > capacity)
        return WireError::buffer_too_small;
    std::size_t used = 0;
    write_map_pack_value(out, &used, pack);
    assert(used == size);
    *written = used;
    return WireError::ok;
}

WireError append_presence_field(
    uint8_t* record,
    std::size_t size,
    std::size_t capacity,
    PresenceTag tag,
    std::span<const uint8_t> value,
    std::size_t* written
) noexcept {
    if (record == nullptr || written == nullptr)
        return WireError::bad_argument;
    if (value.size() > 0xffff || (value.size() != 0 && value.data() == nullptr))
        return WireError::bad_argument;
    if (framed_record(record, size) != WireError::ok)
        return WireError::bad_argument;
    const auto added = presence_field_header_bytes + value.size();
    if (size > presence_record_max_bytes || added > presence_record_max_bytes - size)
        return WireError::buffer_too_small;
    if (capacity < size + added)
        return WireError::buffer_too_small;
    record[size] = static_cast<uint8_t>(tag);
    store_u16(record + size + 1, static_cast<uint16_t>(value.size()));
    if (!value.empty())
        std::memcpy(record + size + presence_field_header_bytes, value.data(), value.size());
    const auto total = size + added;
    store_u16(record + 1, static_cast<uint16_t>(total));
    *written = total;
    return WireError::ok;
}

bool presence_peer(const uint8_t* block) noexcept {
    return read_presence(block).has_value();
}

} // namespace oa::netgame
