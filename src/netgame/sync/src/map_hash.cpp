// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/sync/map_hash.hpp"
#include "oa/base/bytes.hpp"

#include <cstdlib>
#include <cstring>

namespace oa::netgame::sync {

using data::campaign::CampaignFile;
using data::campaign::CampaignFiles;
using data::campaign::CampaignPath;

namespace {
using base::bytes::load_le32;

void copy_bounded(char* out, std::size_t capacity, const char* text) noexcept {
    const std::size_t length = ::strnlen(text, capacity - 1);
    std::memcpy(out, text, length);
    out[length] = '\0';
}

} // namespace

namespace {

constexpr uint32_t kHashCacheGrowth = 16;
constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;
constexpr uint32_t kSourceTextBytes = 4096;

bool path_equal(const char* left, const char* right) noexcept {
    return oa::formats::tdf::compare_nocase(left, right) == 0;
}

// 64-bit FNV-1a of the bytes naming where a file comes from.
uint64_t fnv1a(const char* text, uint32_t length) noexcept {
    uint64_t hash = kFnvOffset;
    for (uint32_t index = 0; index < length; ++index) {
        hash ^= static_cast<uint8_t>(text[index]);
        hash *= kFnvPrime;
    }
    return hash;
}

// The cache key for where the TNT comes from: 0 when the hook is null or
// answers -1, so memory files without one stay keyed by path alone.
uint64_t source_key(const CampaignFiles& files, const char* path) {
    if (files.source == nullptr)
        return 0;
    char stack_text[kSourceTextBytes];
    char* text = stack_text;
    uint32_t capacity = kSourceTextBytes;
    int32_t length = files.source(files.context, path, text, capacity);
    if (length < 0)
        return 0;
    char* owned = nullptr;
    if (static_cast<uint32_t>(length) >= capacity) {
        capacity = static_cast<uint32_t>(length) + 1;
        owned = static_cast<char*>(std::malloc(capacity));
        if (owned == nullptr)
            return fnv1a(stack_text, kSourceTextBytes - 1);
        text = owned;
        length = files.source(files.context, path, text, capacity);
        if (length < 0) {
            std::free(owned);
            return 0;
        }
    }
    const auto count =
        static_cast<uint32_t>(length) < capacity ? static_cast<uint32_t>(length) : capacity - 1;
    const uint64_t key = fnv1a(text, count);
    std::free(owned);
    return key;
}

// Reads the whole TNT; the hash covers slices at header-given offsets.
uint8_t* read_terrain(const CampaignFiles& files, const char* path, uint32_t* size) {
    if (files.size == nullptr || files.read == nullptr || path[0] == '\0')
        return nullptr;
    const int32_t length = files.size(files.context, path);
    if (length < static_cast<int32_t>(kTntHeaderBytes))
        return nullptr;
    auto* bytes = static_cast<uint8_t*>(std::malloc(static_cast<std::size_t>(length)));
    if (bytes == nullptr)
        return nullptr;
    const int32_t read = files.read(
        files.context, path, reinterpret_cast<char*>(bytes), static_cast<uint32_t>(length)
    );
    if (read != length) {
        std::free(bytes);
        return nullptr;
    }
    *size = static_cast<uint32_t>(length);
    return bytes;
}

bool slice_hash(
    const uint8_t* bytes, uint32_t size, uint32_t offset, uint64_t length, uint32_t* hash
) noexcept {
    if (offset > size || length > size - offset)
        return false;
    *hash = oa::formats::tdf::buffer_hash(bytes + offset, static_cast<int32_t>(length));
    return true;
}

// TNT content hash: header, then the cell attributes ("Raw Plot Data"), then
// the feature name records ("Raw Feature Data"). Offsets and sizes past the
// end of the file are rejected rather than hashed.
bool terrain_hash(const uint8_t* bytes, uint32_t size, uint32_t* hash) noexcept {
    if (size < kTntHeaderBytes || load_le32(bytes) != kTntHashVersion)
        return false;
    const uint32_t width = load_le32(bytes + 0x04);
    const uint32_t height = load_le32(bytes + 0x08);
    const uint32_t attributes = load_le32(bytes + 0x10);
    const auto feature_count = static_cast<int32_t>(load_le32(bytes + 0x1c));
    const uint32_t features = load_le32(bytes + 0x20);
    uint32_t term = 0;
    uint32_t result = oa::formats::tdf::buffer_hash(bytes, kTntHeaderBytes);
    if (!slice_hash(
            bytes,
            size,
            attributes,
            static_cast<uint64_t>(width) * height * kTntAttributeBytes,
            &term
        ))
        return false;
    result ^= term;
    if (feature_count > 0) {
        if (!slice_hash(
                bytes,
                size,
                features,
                static_cast<uint64_t>(feature_count) * kTntFeatureRecordBytes,
                &term
            ))
            return false;
        result ^= term;
    }
    *hash = result;
    return true;
}

bool remember_hash(MapHashCache& cache, const char* path, uint64_t key, uint32_t hash) noexcept {
    if (cache.count == cache.capacity) {
        const auto capacity = cache.capacity + kHashCacheGrowth;
        auto* grown = static_cast<MapHashEntry*>(
            std::realloc(cache.entries, capacity * sizeof(MapHashEntry))
        );
        if (grown == nullptr)
            return false;
        cache.entries = grown;
        cache.capacity = capacity;
    }
    auto& entry = cache.entries[cache.count++];
    copy_bounded(entry.path, sizeof(entry.path), path);
    entry.source_key = key;
    entry.hash = hash;
    return true;
}

} // namespace

uint32_t map_compute_content_hash(
    CampaignFile& map_context, const CampaignFiles& files, MapHashCache& cache
) {
    if (map_context.content_hash == 0) {
        const char* path = map_context.paths[static_cast<uint32_t>(CampaignPath::mission)];
        const uint64_t key = source_key(files, path);
        for (uint32_t i = 0; i < cache.count; ++i)
            if (path_equal(cache.entries[i].path, path) && cache.entries[i].source_key == key) {
                map_context.content_hash = cache.entries[i].hash;
                return map_context.header_hash ^ map_context.content_hash;
            }
        uint32_t size = 0;
        uint8_t* bytes = read_terrain(files, path, &size);
        if (bytes == nullptr)
            return 0;
        uint32_t hash = 0;
        const bool hashed = terrain_hash(bytes, size, &hash);
        std::free(bytes);
        if (!hashed)
            return 0;
        map_context.content_hash = hash;
        remember_hash(cache, path, key, map_context.content_hash);
    }
    return map_context.header_hash ^ map_context.content_hash;
}

void map_destroy_hash_cache(MapHashCache& cache) noexcept {
    std::free(cache.entries);
    cache = {};
}

} // namespace oa::netgame::sync
