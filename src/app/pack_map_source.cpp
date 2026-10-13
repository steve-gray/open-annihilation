// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battle room's source of installed pack maps, over the runtime's index
// and the one pack layer a match mounts.
#include "oa/app/pack_map_source.hpp"

#include "map_packs.hpp"
#include "oa/app/content/service.hpp"
#include "oa/app/package_install/origin.hpp"
#include "oa/app/runtime.hpp"
#include "oa/netgame/presence.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
        const auto& names = static_cast<Runtime*>(context)->listed_pack_maps();
        if (names.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        return static_cast<int32_t>(names.size());
    };
    source.at = [](void* context, int32_t index, mp::LobbyPackMap* out) {
        if (out == nullptr || index < 0)
            return false;
        auto& runtime = *static_cast<Runtime*>(context);
        const auto& names = runtime.listed_pack_maps();
        if (static_cast<std::size_t>(index) >= names.size())
            return false;
        // Copy the name before find: find may refresh the index, and the
        // name must not point into a list that refresh frees.
        const std::string name = names[static_cast<std::size_t>(index)];
        const PackMap* map = runtime.map_packs().find(name);
        if (map == nullptr)
            return false;
        out->name = map->name.c_str();
        out->description = map->description.c_str();
        out->size = map->size.c_str();
        out->title = map->title.c_str();
        out->pack_name = map->pack_name.c_str();
        out->file_title = nullptr;
        out->blocked_reason = runtime.pack_map_blocked_reason(name);
        const uint64_t bytes = map->terrain_bytes;
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
    // The pack of the map whose files are mounted: on the host, the selected
    // map's, which the presence record names. Only a pack installed from a
    // registry's catalogue is named: its key, the release and SHA-256 its
    // folder's origin record gives, the registry, the address the registry's
    // descriptor was read from, and the pack's name and version.
    source.pack = [](void* context, oa::netgame::PresenceMapPack* out) {
        auto& runtime = *static_cast<Runtime*>(context);
        const PackMap* map = runtime.mounted_pack_map();
        if (out == nullptr || map == nullptr)
            return false;
        std::optional<package_install::Origin> origin;
        try {
            origin = package_install::read_origin(map->folder);
        } catch (const std::exception&) {
            // A record that cannot be read is no record.
        }
        if (!origin || origin->kind != package_install::OriginKind::catalogue || !origin->sha256 ||
            origin->release < 1 || origin->release > std::numeric_limits<uint32_t>::max())
            return false;
        oa::netgame::PresenceMapPack pack;
        pack.key = map->id;
        pack.release = static_cast<uint32_t>(origin->release);
        pack.sha256 = *origin->sha256;
        pack.registry = origin->registry;
        // Empty for a built-in registry, one added from a file, and one no
        // longer in effect.
        if (runtime.content_) {
            const std::shared_ptr<const content::Snapshot> snapshot =
                runtime.content_service().snapshot();
            if (snapshot != nullptr)
                for (const content::RegistryView& view : snapshot->registries)
                    if (view.registry.descriptor.id == pack.registry) {
                        pack.descriptor_url = view.registry.url;
                        break;
                    }
        }
        pack.name = map->pack_name;
        pack.version = map->pack_version;
        *out = std::move(pack);
        return true;
    };
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
    out->title = nullptr;
    out->pack_name = nullptr;
    out->file_title = nullptr;
    out->blocked_reason = nullptr;
    return true;
}

} // namespace oa::app
