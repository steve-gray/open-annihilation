// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A map installed while the battle room shows is listed and can be read.
//
// The battle room is entered and its map list is loaded. The screen
// callback has then returned, and the context's install pointer no longer
// names the install, as it does not once that callback's context is gone.
// Listing the map the install just added still reads that map.
#include "oa/formats/hpi.hpp"
#include "oa/test/check.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"
#include "oa/ui/screen_registry.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace mp = oa::ui::frontend_multiplayer;

namespace {

constexpr const char* kHeld = "held@ridge";
constexpr const char* kInstalled = "cinder@ridge";

/// The battle room's maps: one grey row, then the map an install adds.
struct RoomMaps {
    bool installed = false;

    static int32_t count(void* context) {
        return static_cast<RoomMaps*>(context)->installed ? 2 : 1;
    }

    static bool at(void* context, int32_t index, mp::LobbyPackMap* out) {
        if (out == nullptr || index < 0)
            return false;
        if (index == 0) {
            *out = {kHeld, "Held back", "1 x 1", 16, "Held Ridge", "Ridge", nullptr, "for ridge"};
            return true;
        }
        if (index == 1 && static_cast<RoomMaps*>(context)->installed) {
            *out = {
                kInstalled,
                "A ring of cinders",
                "1 x 1",
                16,
                "Cinder Ridge",
                "Ridge",
                nullptr,
                nullptr
            };
            return true;
        }
        return false;
    }

    static bool prepare(void*, const char*, char*, std::size_t) { return true; }

    static void release(void*) {}

    static int32_t base_count(void*) { return 0; }

    static bool base_at(void*, int32_t, mp::LobbyPackMap*) { return false; }

    mp::LobbyMapSource source() {
        return {this, count, at, prepare, release, base_count, base_at, nullptr};
    }
};

/// Writes the installed map's files where the battle room reads them.
///
/// @param root the install's folder
/// @return true when both files are written
bool write_installed_map(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::create_directories(root / "maps", error))
        return false;
    const char* ota = "[GlobalHeader]\n"
                      "{\n"
                      "missionname=Cinder Ridge;\n"
                      "missiondescription=A ring of cinders;\n"
                      "[Schema 0]\n"
                      "    {\n"
                      "    Type=Network 1;\n"
                      "    [specials]\n"
                      "        {\n"
                      "        [special0]\n"
                      "            {\n"
                      "            specialwhat=StartPos1;\n"
                      "            }\n"
                      "        [special1]\n"
                      "            {\n"
                      "            specialwhat=StartPos2;\n"
                      "            }\n"
                      "        }\n"
                      "    }\n"
                      "}\n";
    {
        std::ofstream out(root / "maps" / "cinder@ridge.ota", std::ios::binary);
        out << ota;
        if (!out)
            return false;
    }
    std::vector<uint8_t> terrain(0x44, 0);
    const auto put = [&](std::size_t offset, uint32_t value) {
        terrain[offset] = static_cast<uint8_t>(value);
        terrain[offset + 1] = static_cast<uint8_t>(value >> 8);
        terrain[offset + 2] = static_cast<uint8_t>(value >> 16);
        terrain[offset + 3] = static_cast<uint8_t>(value >> 24);
    };
    put(0, 0x2000);
    put(4, 1);
    put(8, 1);
    put(0x10, 0x40);
    terrain[0x40] = 0x11;
    std::ofstream out(root / "maps" / "cinder@ridge.tnt", std::ios::binary);
    out.write(
        reinterpret_cast<const char*>(terrain.data()), static_cast<std::streamsize>(terrain.size())
    );
    return static_cast<bool>(out);
}

/// The list names the installed map.
bool lists_installed_map() {
    const int32_t count = mp::multiplayer_map_count();
    for (int32_t index = 0; index < count; ++index) {
        const char* name = mp::multiplayer_map_at(index);
        if (name != nullptr && std::strcmp(name, kInstalled) == 0)
            return true;
    }
    return false;
}

void installed_map_is_read_after_the_screen_returns() {
    std::random_device entropy;
    const auto root =
        std::filesystem::temp_directory_path() /
        ("oa-G131-map-" + std::to_string(entropy()) + "-" + std::to_string(entropy()));

    struct Clean {
        std::filesystem::path folder;

        ~Clean() {
            std::error_code error;
            std::filesystem::remove_all(folder, error);
        }
    } clean{root};

    OA_CHECK(write_installed_map(root));
    oa::AssetStore store(root);
    oa::app::ScreenContext ctx{nullptr, nullptr, &store, nullptr, nullptr, nullptr, 0};

    RoomMaps room;
    mp::multiplayer_reset();
    mp::multiplayer_bind_map_source(room.source());

    struct Unbind {
        ~Unbind() { mp::multiplayer_bind_map_source({}); }
    } unbind;

    oa::app::ScreenRegistry registry{};
    oa::app::register_multiplayer_screens(&registry);
    const oa::app::ScreenDesc* battle = oa::app::screen_find(&registry, mp::kScreenBattleroom);
    OA_CHECK(battle != nullptr && battle->enter != nullptr);
    if (battle == nullptr || battle->enter == nullptr)
        return;
    battle->enter(&ctx, battle->state);

    OA_CHECK(mp::multiplayer_map_count() == 1);
    OA_CHECK(!lists_installed_map());

    // The install has finished. The callback that showed the room has
    // returned, and its context's install slot no longer names the install.
    room.installed = true;
    ctx.assets = reinterpret_cast<oa::AssetStore*>(static_cast<uintptr_t>(1));
    mp::multiplayer_reload_maps();

    OA_CHECK(mp::multiplayer_map_count() == 2);
    OA_CHECK(lists_installed_map());
    auto& maps = mp::multiplayer_lobby().maps;
    OA_CHECK(maps.select != nullptr && maps.select(maps.context, kInstalled));
    OA_CHECK(maps.selected != nullptr && maps.selected(maps.context));
    OA_CHECK(maps.name != nullptr && std::strcmp(maps.name(maps.context), kInstalled) == 0);
}

} // namespace

int main() {
    installed_map_is_read_after_the_screen_returns();
    return oa::test::check_exit_status();
}
