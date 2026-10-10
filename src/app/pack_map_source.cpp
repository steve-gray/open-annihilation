// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battle room's source of installed pack maps, over the runtime's index
// and the one pack layer a match mounts.
#include "oa/app/pack_map_source.hpp"

#include "map_packs.hpp"
#include "oa/app/runtime.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace oa::app {

namespace {

/// Copies a reason into a fixed buffer, terminated, cut to fit.
///
/// @param[out] reason the buffer; null writes nothing
/// @param capacity bytes of `reason`, counting the terminating NUL
/// @param text the reason
void copy_reason(char* reason, std::size_t capacity, std::string_view text) {
    if (reason == nullptr || capacity == 0)
        return;
    const std::size_t count = std::min(text.size(), capacity - 1);
    if (count != 0)
        std::memcpy(reason, text.data(), count);
    reason[count] = '\0';
}

} // namespace

oa::ui::frontend_multiplayer::LobbyMapSource pack_map_source(Runtime& runtime) {
    namespace mp = oa::ui::frontend_multiplayer;
    mp::LobbyMapSource source;
    source.context = &runtime;
    source.count = [](void* context) {
        const auto maps = static_cast<Runtime*>(context)->map_packs().maps();
        if (maps.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        return static_cast<int32_t>(maps.size());
    };
    source.at = [](void* context, int32_t index, mp::LobbyPackMap* out) {
        if (out == nullptr || index < 0)
            return false;
        const auto maps = static_cast<Runtime*>(context)->map_packs().maps();
        if (static_cast<std::size_t>(index) >= maps.size())
            return false;
        const PackMap& map = maps[static_cast<std::size_t>(index)];
        out->name = map.name.c_str();
        out->description = map.description.c_str();
        out->size = map.size.c_str();
        const uint64_t bytes = map.terrain_bytes;
        const auto clamped = bytes > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())
                                 ? std::numeric_limits<int32_t>::max()
                                 : static_cast<int32_t>(bytes);
        out->memory_mb = mp::map_memory_mb(clamped);
        return true;
    };
    source.prepare = [](void* context, const char* name, char* reason, std::size_t capacity) {
        std::string why;
        const bool mounted = static_cast<Runtime*>(context)->prepare_pack_map(
            name != nullptr ? std::string_view{name} : std::string_view{}, &why
        );
        if (!mounted)
            copy_reason(reason, capacity, why);
        return mounted;
    };
    source.release = [](void* context) { static_cast<Runtime*>(context)->release_pack_map(); };
    source.base_count = &Runtime::bound_base_count;
    source.base_at = &Runtime::bound_base_at;
    return source;
}

int32_t Runtime::bound_base_count(void* context) {
    const auto& maps = static_cast<const Runtime*>(context)->base_map_summaries_;
    if (maps.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        return std::numeric_limits<int32_t>::max();
    return static_cast<int32_t>(maps.size());
}

bool Runtime::bound_base_at(
    void* context, int32_t index, oa::ui::frontend_multiplayer::LobbyPackMap* out
) {
    if (out == nullptr || index < 0)
        return false;
    const auto& maps = static_cast<const Runtime*>(context)->base_map_summaries_;
    if (static_cast<std::size_t>(index) >= maps.size())
        return false;
    const Runtime::BaseMapSummary& map = maps[static_cast<std::size_t>(index)];
    out->name = map.name.c_str();
    out->description = map.description.c_str();
    out->size = map.size.c_str();
    out->memory_mb = map.memory_mb;
    return true;
}

} // namespace oa::app
