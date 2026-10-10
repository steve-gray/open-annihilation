// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Installed map packs: two packs list four maps, sorted, and a refresh reads
// a manifest again only when that pack's key changed. A broken manifest is
// named and left out. Removing a folder drops its maps. With the game's
// data, a map that uses only its own feature fits the base game, and one
// that defines DragonsTeeth does not.

#include "map_packs.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/package_install/origin.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/map_fit/map_fit.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/test/check.hpp"
#include "oa/test/game_data.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace pack = oa::data::map_pack;
using oa::app::MapPacks;
using oa::app::PackMap;

/// A scratch folder, deleted when the test ends.
class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-app-map-packs-")) {}

    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

  private:

    fs::path path_;
};

/// Writes a file, making its folders.
///
/// @param file the file
/// @param bytes its bytes
void write_bytes(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    OA_CHECK(static_cast<bool>(out));
}

/// A catalogue origin record whose key is the digest given.
///
/// @param folder the pack's folder
/// @param registry the registry
/// @param release the release
/// @param digest the pack's key
void write_origin(
    const fs::path& folder,
    std::string_view registry,
    int64_t release,
    const oa::base::sha256::Digest& digest
) {
    oa::app::package_install::Origin origin{};
    origin.kind = oa::app::package_install::OriginKind::catalogue;
    origin.registry = std::string(registry);
    origin.catalogue_id = folder.filename().string();
    origin.release = release;
    origin.sha256 = digest;
    origin.installed = "2006-01-02";
    write_bytes(folder / ".oa-origin.yaml", oa::app::package_install::origin_text(origin));
}

/// One map of a pack written for the index.
struct Listed {
    std::string stem{};
    std::string title{};
    int32_t players{};
    std::string size{};
    std::string terrain{};
    std::string preview{};
};

/// Writes a pack folder and its origin record.
///
/// @param folder the folder
/// @param id the pack's id
/// @param name the pack's name
/// @param version the pack's version
/// @param registry the catalogue registry
/// @param release the catalogue release
/// @param digest the origin's SHA-256
/// @param maps the maps
void write_pack(
    const fs::path& folder,
    std::string_view id,
    std::string_view name,
    std::string_view version,
    std::string_view registry,
    int64_t release,
    const oa::base::sha256::Digest& digest,
    const std::vector<Listed>& maps
) {
    pack::Manifest manifest;
    manifest.format = pack::format_version;
    manifest.id = std::string(id);
    manifest.name = std::string(name);
    manifest.version = std::string(version);
    manifest.author = {"Ridge", {}};
    manifest.packaging = {1, "2026-10-11", "Open Annihilation"};
    manifest.requires_base = "ta-3.1c";
    for (const Listed& listed : maps) {
        pack::MapEntry entry;
        entry.stem = listed.stem;
        entry.title = listed.title;
        entry.players = listed.players;
        entry.size = listed.size;
        entry.preview = listed.preview;
        entry.files = {"maps/" + listed.stem + ".ota", "maps/" + listed.stem + ".tnt"};
        manifest.maps.push_back(std::move(entry));
        write_bytes(folder / "maps" / (listed.stem + ".ota"), "ota\n");
        write_bytes(folder / "maps" / (listed.stem + ".tnt"), listed.terrain);
        write_bytes(folder / listed.preview, "preview\n");
    }
    write_bytes(folder / "oamap.yaml", pack::write_manifest(manifest));
    write_origin(folder, registry, release, digest);
}

/// Counts a manifest the index parses.
///
/// @param context the count
void count_read(void* context) {
    ++*static_cast<int*>(context);
}

void test_index(const Scratch& scratch) {
    const fs::path maps = scratch.path() / "Maps";
    const uint8_t mark_r[] = {'r'};
    const uint8_t mark_h[] = {'h'};
    const uint8_t mark_next[] = {'R'};
    const auto ridge_key = oa::base::sha256::digest_of(mark_r);
    const auto harbour_key = oa::base::sha256::digest_of(mark_h);
    const auto ridge_next = oa::base::sha256::digest_of(mark_next);
    write_pack(
        maps / "ridge",
        "ridge",
        "Ridge Pack",
        "1.0",
        "ridge",
        3,
        ridge_key,
        {{"north", "North Shore", 2, "4x4", std::string(40, 'n'), "previews/north.png"},
         {"south", "South Shore", 4, "8x8", std::string(80, 's'), "previews/south.png"}}
    );
    write_pack(
        maps / "harbour",
        "harbour",
        "Harbour Pack",
        "2.0",
        "harbour",
        5,
        harbour_key,
        {{"bay", "The Bay", 2, "1x1", std::string(10, 'b'), "previews/bay.png"},
         {"cove", "The Cove", 6, "2x2", std::string(20, 'c'), "previews/cove.png"}}
    );
    write_bytes(maps / ".hidden" / "oamap.yaml", "hidden\n");

    int reads = 0;
    MapPacks packs(maps);
    packs.set_manifest_watch(&count_read, &reads);
    const auto listed = packs.maps();
    OA_CHECK(listed.size() == 4);
    OA_CHECK(reads == 2);
    const char* names[] = {"bay@harbour", "cove@harbour", "north@ridge", "south@ridge"};
    for (int index = 0; index < 4; ++index)
        OA_CHECK(listed[static_cast<std::size_t>(index)].name == names[index]);
    const PackMap* north = packs.find("north@ridge");
    OA_CHECK(north != nullptr);
    if (north != nullptr) {
        OA_CHECK(north->title == "North Shore");
        OA_CHECK(north->players == 2);
        OA_CHECK(north->size == "4x4");
        OA_CHECK(north->terrain_bytes == 40);
        OA_CHECK(north->preview.filename() == "north.png");
        OA_CHECK(north->pack_name == "Ridge Pack");
        OA_CHECK(north->registry == "ridge");
        OA_CHECK(north->release == 3);
        OA_CHECK(north->sha256.size() == oa::base::sha256::hex_size);
    }
    const PackMap* cove = packs.find("cove@harbour");
    OA_CHECK(cove != nullptr && cove->players == 6 && cove->terrain_bytes == 20);
    OA_CHECK(cove != nullptr && cove->size == "2x2" && cove->title == "The Cove");
    OA_CHECK(cove != nullptr && cove->preview.filename() == "cove.png");
    OA_CHECK(packs.find("no-such@ridge") == nullptr);
    OA_CHECK(packs.problems().empty());

    write_pack(
        maps / "ridge",
        "ridge",
        "Ridge Pack",
        "1.0",
        "ridge",
        3,
        ridge_key,
        {{"north", "North Changed", 2, "4x4", std::string(40, 'n'), "previews/north.png"},
         {"south", "South Shore", 4, "8x8", std::string(80, 's'), "previews/south.png"}}
    );
    write_pack(
        maps / "harbour",
        "harbour",
        "Harbour Pack",
        "2.0",
        "harbour",
        5,
        harbour_key,
        {{"bay", "Bay Changed", 2, "1x1", std::string(10, 'b'), "previews/bay.png"},
         {"cove", "Cove Changed", 6, "2x2", std::string(20, 'c'), "previews/cove.png"}}
    );
    write_origin(maps / "ridge", "ridge", 4, ridge_next);
    packs.refresh();
    OA_CHECK(reads == 3);
    const PackMap* reread = packs.find("north@ridge");
    const PackMap* kept = packs.find("bay@harbour");
    OA_CHECK(reread != nullptr && reread->title == "North Changed" && reread->release == 4);
    OA_CHECK(kept != nullptr && kept->title == "The Bay");

    write_bytes(maps / "broken" / "oamap.yaml", "this is not a map pack\n");
    packs.refresh();
    bool named = false;
    for (const std::string& problem : packs.problems())
        if (problem.starts_with("broken:"))
            named = true;
    OA_CHECK(named);
    OA_CHECK(packs.find("north@ridge") != nullptr);
    OA_CHECK(packs.maps().size() == 4);

    fs::remove_all(maps / "harbour");
    packs.refresh();
    OA_CHECK(packs.maps().size() == 2);
    OA_CHECK(packs.find("bay@harbour") == nullptr);
    OA_CHECK(packs.find("north@ridge") != nullptr && packs.find("south@ridge") != nullptr);
}

/// Writes a little-endian 32-bit word.
///
/// @param bytes the buffer
/// @param at the offset
/// @param value the word
void put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[at + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[at + 3] = static_cast<uint8_t>(value >> 24U);
}

/// Writes a little-endian 16-bit word.
///
/// @param bytes the buffer
/// @param at the offset
/// @param value the word
void put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
}

/// Builds a 4 by 4 map whose one feature record carries a name.
///
/// @param name the feature name
/// @return the map bytes
std::vector<uint8_t> tnt_named(std::string_view name) {
    namespace tnt = oa::formats::tnt;
    constexpr std::size_t tile_map_at = 64;
    constexpr std::size_t attributes_at = 72;
    constexpr std::size_t tiles_at = 136;
    const auto features_at = tiles_at + 2 * tnt::layout::tile_bytes;
    std::vector<uint8_t> bytes(features_at + tnt::layout::feature_record_bytes);
    put32(bytes, 0, static_cast<uint32_t>(tnt::Version::total_annihilation));
    put32(bytes, 4, 4);
    put32(bytes, 8, 4);
    put32(bytes, 12, static_cast<uint32_t>(tile_map_at));
    put32(bytes, 16, static_cast<uint32_t>(attributes_at));
    put32(bytes, 20, static_cast<uint32_t>(tiles_at));
    put32(bytes, 24, 2);
    put32(bytes, 28, 1);
    put32(bytes, 32, static_cast<uint32_t>(features_at));
    put32(bytes, 36, 17);
    put32(bytes, 40, 0);
    put32(bytes, 44, 0);
    put16(bytes, tile_map_at, 0);
    put16(bytes, tile_map_at + 2, 1);
    put16(bytes, tile_map_at + 4, 1);
    put16(bytes, tile_map_at + 6, 0);
    const auto at = features_at + offsetof(tnt::FeatureDiskRecord, name);
    const auto count = std::min(name.size(), std::size_t{127});
    for (std::size_t character = 0; character < count; ++character)
        bytes[at + character] = static_cast<uint8_t>(name[character]);
    return bytes;
}

/// The stem of a listed GAF.
///
/// @param path the listed path
/// @return the stem
std::string gaf_stem(std::string_view path) {
    const auto slash = path.find_last_of('/');
    auto file = slash == std::string_view::npos ? path : path.substr(slash + 1);
    constexpr std::string_view suffix = ".gaf";
    if (file.size() > suffix.size() && file.ends_with(suffix))
        file.remove_suffix(suffix.size());
    return std::string(file);
}

/// Tells whether a stem is letters, digits, '_' or '-'.
///
/// @param stem the stem
/// @return true when every character is one of those
bool plain_stem(std::string_view stem) {
    if (stem.empty())
        return false;
    for (const unsigned char byte : stem) {
        const bool ok = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                        byte == '_' || byte == '-';
        if (!ok)
            return false;
    }
    return true;
}

/// The OTA that places one feature.
///
/// @param feature the feature's name
/// @return the OTA text
std::string ota_placing(std::string_view feature) {
    return "[GlobalHeader]\n"
           "{\n"
           "[Schema 0]\n"
           "{\n"
           "Type=Network 1;\n"
           "[features]\n"
           "{\n"
           "[feature0]\n"
           "{\n"
           "Featurename=" +
           std::string(feature) +
           ";\n"
           "XPos=1;\n"
           "ZPos=1;\n"
           "}\n"
           "}\n"
           "}\n"
           "}\n";
}

/// A feature TDF. The clashing one also defines DragonsTeeth.
///
/// @param stem the GAF stem the marker names
/// @param clash true to define DragonsTeeth as well
/// @return the TDF text
std::string marker_tdf(std::string_view stem, bool clash) {
    std::string text = "[zz ridge marker]\n{\nfilename=" + std::string(stem) + ";\n}\n";
    if (clash)
        text += "[DragonsTeeth]\n{\nfilename=" + std::string(stem) + ";\n}\n";
    return text;
}

void test_base_fit() {
    const fs::path game_dir =
        oa::test::require_game_directory("a map checked against the base game");
    const oa::app::GameInstall install = oa::app::inspect_game_install(game_dir);
    OA_CHECK(oa::app::usable(install));
    if (!oa::app::usable(install))
        return;
    oa::AssetStore store(install.folders);
    for (const fs::path& archive : install.archives) {
        std::string error;
        if (!store.try_mount(archive, &error)) {
            std::fprintf(stderr, "mount failed: %s\n", error.c_str());
            OA_CHECK(false);
            return;
        }
    }
    const auto gafs = store.list_effective("anims", ".gaf", false);
    std::string stem;
    for (const std::string& path : gafs) {
        const std::string candidate = gaf_stem(path);
        if (plain_stem(candidate)) {
            stem = candidate;
            break;
        }
    }
    OA_CHECK(!stem.empty());
    if (stem.empty())
        return;

    Scratch scratch;
    const fs::path folder = scratch.path() / "ridge-pack";
    constexpr std::string_view feature = "zz ridge marker";
    const auto tnt = tnt_named(feature);
    const std::string ota = ota_placing(feature);
    const auto write_map = [&](std::string_view map_stem, std::string_view tdf, bool clash) {
        write_bytes(folder / "maps" / (std::string(map_stem) + ".ota"), ota);
        std::ofstream terrain(
            folder / "maps" / (std::string(map_stem) + ".tnt"), std::ios::binary | std::ios::trunc
        );
        terrain.write(
            reinterpret_cast<const char*>(tnt.data()), static_cast<std::streamsize>(tnt.size())
        );
        OA_CHECK(static_cast<bool>(terrain));
        write_bytes(folder / tdf, marker_tdf(stem, clash));
    };
    write_map("north", "features/ridgepack/marker.tdf", false);
    write_map("south", "features/ridgepack/clash.tdf", true);

    const auto entry = [](std::string stem, std::string tdf) {
        pack::MapEntry map;
        map.stem = std::move(stem);
        map.title = map.stem;
        map.players = 2;
        map.size = "4x4";
        map.files = {
            "maps/" + map.stem + ".ota",
            "maps/" + map.stem + ".tnt",
            std::move(tdf),
        };
        return map;
    };
    const pack::MapEntry passing = entry("north", "features/ridgepack/marker.tdf");
    const oa::data::map_fit::Fit fitted =
        oa::app::check_base_game_fit(game_dir, folder, passing, "ridge-pack");
    if (!fitted.fits()) {
        for (const auto& failure : fitted.failures)
            std::fprintf(stderr, "fitting: %s\n", oa::data::map_fit::describe(failure).c_str());
    }
    OA_CHECK(fitted.fits());

    const pack::MapEntry clashing = entry("south", "features/ridgepack/clash.tdf");
    const oa::data::map_fit::Fit clash =
        oa::app::check_base_game_fit(game_dir, folder, clashing, "ridge-pack");
    bool redefined = false;
    for (const auto& failure : clash.failures) {
        if (failure.rule == oa::data::map_fit::Rule::new_names && failure.subject == "DragonsTeeth")
            redefined = true;
        else
            std::fprintf(stderr, "clash: %s\n", oa::data::map_fit::describe(failure).c_str());
    }
    OA_CHECK(redefined);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv))
        test_base_fit();
    else {
        const Scratch scratch;
        test_index(scratch);
    }
    return oa::test::check_exit_status();
}
