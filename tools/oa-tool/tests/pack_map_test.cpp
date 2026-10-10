// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-tool pack for a map folder, in-process. Two maps share a feature file,
// one preview is checked as a palette PNG, two packs of the same folder have
// the same bytes, and a duplicate section, a name over 63 bytes, a missing
// --game-dir and an existing output are refused. The source folder and the
// stand-in game folder are left in the working folder.

#include "command.hpp"
#include "pack_map.hpp"

#include "oa/base/sha256.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/formats/png.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/formats/zip/stream.hpp"
#include "oa/platform/files.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace sha256 = oa::base::sha256;
namespace map_pack = oa::data::map_pack;
namespace png = oa::formats::png;
namespace tnt = oa::formats::tnt;
namespace zip = oa::formats::zip;

struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-tool-pack-map")) {}

    ~Scratch() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

struct Captured {
    int status = 0;
    std::string out;
    std::string err;
};

struct WorkingFolder {
    fs::path previous;

    explicit WorkingFolder(const fs::path& path) : previous(fs::current_path()) {
        fs::current_path(path);
    }

    ~WorkingFolder() {
        std::error_code error;
        fs::current_path(previous, error);
    }

    WorkingFolder(const WorkingFolder&) = delete;
    WorkingFolder& operator=(const WorkingFolder&) = delete;
};

struct OpenFile {
    std::FILE* stream{};
    oa::platform::Files files{};

    OpenFile() = default;
    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;

    ~OpenFile() {
        if (stream != nullptr)
            std::fclose(stream);
    }
};

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!bytes.empty())
        out.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
    OA_CHECK(static_cast<bool>(out));
}

void write_text(const fs::path& path, std::string_view text) {
    write_bytes(path, std::vector<uint8_t>(text.begin(), text.end()));
}

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string hash_bytes(const std::vector<uint8_t>& bytes) {
    sha256::Hasher hasher{};
    sha256::update(hasher, std::span<const uint8_t>(bytes.data(), bytes.size()));
    const auto hex = sha256::to_hex(sha256::finish(hasher));
    return {hex.begin(), hex.end()};
}

std::string printed_hash(std::string_view text) {
    const std::size_t at = text.rfind("sha256 ");
    if (at == std::string_view::npos || at + 7 + 64 > text.size())
        return {};
    return std::string{text.substr(at + 7, 64)};
}

Captured run(std::vector<std::string> arguments) {
    std::ostringstream out;
    std::ostringstream err;
    oa::tool::Output output{out, err};
    const int status = oa::tool::run_tool(std::move(arguments), output);
    return {status, out.str(), err.str()};
}

void put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[at + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[at + 3] = static_cast<uint8_t>(value >> 24U);
}

void put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
}

/// A small TNT: attribute cells of 10 by 16, so the playable share is the
/// whole minimap and the packed size is 1x1. Feature words are the sentinel.
std::vector<uint8_t> make_tnt(const std::vector<std::string>& features, uint8_t first_pixel) {
    constexpr uint32_t width = 10;
    constexpr uint32_t height = 16;
    constexpr uint32_t tile_count = 1;
    constexpr uint32_t minimap_side = 4;
    const uint32_t tile_map_count = (width / 2) * (height / 2);
    const uint32_t attribute_count = width * height;
    const std::size_t tile_map_at = 64;
    const std::size_t attributes_at = tile_map_at + static_cast<std::size_t>(tile_map_count) * 2;
    const std::size_t tiles_at = attributes_at + static_cast<std::size_t>(attribute_count) * 4;
    const std::size_t features_at =
        tiles_at + static_cast<std::size_t>(tile_count) * tnt::layout::tile_bytes;
    const std::size_t minimap_at =
        features_at + features.size() * tnt::layout::feature_record_bytes;
    std::vector<uint8_t> bytes(
        minimap_at + 8 + static_cast<std::size_t>(minimap_side) * minimap_side
    );
    put32(bytes, 0, static_cast<uint32_t>(tnt::Version::total_annihilation));
    put32(bytes, 4, width);
    put32(bytes, 8, height);
    put32(bytes, 12, static_cast<uint32_t>(tile_map_at));
    put32(bytes, 16, static_cast<uint32_t>(attributes_at));
    put32(bytes, 20, static_cast<uint32_t>(tiles_at));
    put32(bytes, 24, tile_count);
    put32(bytes, 28, static_cast<uint32_t>(features.size()));
    put32(bytes, 32, static_cast<uint32_t>(features_at));
    put32(bytes, 40, static_cast<uint32_t>(minimap_at));
    put32(bytes, 44, 1);
    for (uint32_t index = 0; index < attribute_count; ++index)
        put16(bytes, attributes_at + static_cast<std::size_t>(index) * 4 + 1, 0xffff);
    for (std::size_t index = 0; index < features.size(); ++index) {
        const std::size_t at = features_at + index * tnt::layout::feature_record_bytes + 4;
        const std::string& name = features[index];
        for (std::size_t character = 0; character < name.size(); ++character)
            bytes[at + character] = static_cast<uint8_t>(name[character]);
    }
    put32(bytes, minimap_at, minimap_side);
    put32(bytes, minimap_at + 4, minimap_side);
    for (uint32_t pixel = 0; pixel < minimap_side * minimap_side; ++pixel)
        bytes[minimap_at + 8 + pixel] = static_cast<uint8_t>(first_pixel + pixel);
    return bytes;
}

std::string
ota_text(std::string_view title, std::string_view description, int starts, std::string_view unit) {
    std::string text = "[GlobalHeader]\n{\nmissionname=";
    text += title;
    text += ";\nmissiondescription=";
    text += description;
    text += ";\n[Schema 0]\n{\nType=Network 1;\n[specials]\n{\n";
    for (int index = 0; index < starts; ++index) {
        text += "[special" + std::to_string(index) + "]{specialwhat=StartPos" +
                std::to_string(index + 1) + ";XPos=1;ZPos=1;}\n";
    }
    text += "}\n";
    if (!unit.empty()) {
        text += "[units]\n{\n[unit0]\n{\nUnitname=";
        text += unit;
        text += ";\n}\n}\n";
    }
    text += "}\n}\n";
    return text;
}

std::string source_manifest() {
    return "oamap: 1\n"
           "id: \"shoals\"\n"
           "name: \"Shoal Pack\"\n"
           "version: \"1.2\"\n"
           "author:\n"
           "  name: \"Ridge\"\n"
           "packaging:\n"
           "  revision: 3\n"
           "  date: \"2026-11-01\"\n"
           "  packager: \"oa-tool 0.8\"\n"
           "requires:\n"
           "  base: \"ta-3.1c\"\n"
           "maps:\n"
           "  - stem: \"ash\"\n"
           "    name: \"Not The Title\"\n"
           "  - stem: \"fen\"\n";
}

bool read_at(void* context, uint64_t offset, std::span<uint8_t> bytes) {
    auto& file = *static_cast<OpenFile*>(context);
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
    if (file.stream == nullptr ||
        file.files.seek(
            nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
        ) != 0)
        return false;
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::size_t read =
            std::fread(bytes.data() + done, 1, bytes.size() - done, file.stream);
        if (read == 0)
            return false;
        done += read;
    }
    return true;
}

struct Package {
    std::vector<zip::StreamEntry> entries;
    std::vector<std::vector<uint8_t>> bytes;
};

Package read_package(const fs::path& path) {
    Package package;
    OpenFile file;
    file.stream = oa::platform::open_file(path, "rb");
    OA_CHECK(file.stream != nullptr);
    if (file.stream == nullptr)
        return package;
    file.files = oa::platform::stdio_files();
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
    OA_CHECK(file.files.seek(nullptr, handle, 0, oa::platform::SeekOrigin::end) == 0);
    const int64_t end = file.files.tell(nullptr, handle);
    OA_CHECK(end >= 0);
    zip::StreamDirectory directory;
    zip::ZipError error{};
    const bool read = zip::read_stream_directory(
        {&file, read_at}, static_cast<uint64_t>(end), {}, directory, error
    );
    OA_CHECK(read);
    if (!read)
        return package;
    package.entries = directory.entries;
    for (const zip::StreamEntry& entry : directory.entries) {
        std::vector<uint8_t> bytes;
        const bool got = zip::read_stream_entry(
            {&file, read_at}, static_cast<uint64_t>(end), entry, entry.bytes, bytes, error
        );
        OA_CHECK(got);
        package.bytes.push_back(std::move(bytes));
    }
    return package;
}

const std::vector<uint8_t>* entry_bytes(const Package& package, std::string_view name) {
    for (std::size_t index = 0; index < package.entries.size(); ++index) {
        if (package.entries[index].name == name)
            return &package.bytes[index];
    }
    return nullptr;
}

void write_source(const fs::path& source, const fs::path& game) {
    std::error_code error;
    fs::remove_all(source, error);
    fs::remove_all(game, error);
    write_text(source / "oamap.yaml", source_manifest());
    write_text(source / "maps" / "ash.ota", ota_text("Ash Shore", "A ring of ash.", 2, "scout"));
    std::string fen_description(126, 'a');
    fen_description += "\xE4\xB8\xAD";
    write_text(source / "maps" / "fen.ota", ota_text("Fen Crossing", fen_description, 4, ""));
    const std::vector<uint8_t> ash_tnt = make_tnt({"ash_rock", "game_tree"}, 10);
    const std::vector<uint8_t> fen_tnt = make_tnt({"fen_tree"}, 40);
    const auto ash_parsed = tnt::parse(ash_tnt);
    const auto fen_parsed = tnt::parse(fen_tnt);
    OA_CHECK(ash_parsed.ok());
    OA_CHECK(fen_parsed.ok());
    if (!ash_parsed.ok() && ash_parsed.error)
        std::fprintf(stderr, "ash tnt: %s\n", ash_parsed.error->message.c_str());
    write_bytes(source / "maps" / "ash.tnt", ash_tnt);
    write_bytes(source / "maps" / "fen.tnt", fen_tnt);
    write_text(
        source / "features" / "shore" / "shore.tdf",
        "[ash_rock]\n"
        "{\n"
        "object=wreck;\n"
        "featureburnt=ash_cinder;\n"
        "burnweapon=flare;\n"
        "}\n"
        "[ash_cinder]\n"
        "{\n"
        "filename=cinder;\n"
        "}\n"
        "[fen_tree]\n"
        "{\n"
        "filename=frond;\n"
        "}\n"
    );
    write_text(
        source / "features" / "other" / "idle.tdf",
        "[idle]\n"
        "{\n"
        "filename=unused;\n"
        "}\n"
    );
    write_text(source / "objects3d" / "wreck.3do", "3DO");
    write_text(source / "anims" / "frond.gaf", "GAF");
    write_text(source / "anims" / "unused.gaf", "UNUSED");
    write_text(source / "sounds" / "x.wav", "WAV");
    write_text(source / "units" / "x.fbi", "FBI");
    write_text(source / "weapons" / "flare.tdf", "[flare]\n{\n}\n");
    write_text(source / ".DS_Store", "store");
    write_text(source / "._junk", "apple");
    write_text(source / "__MACOSX" / "foo", "mac");
    write_text(source / ".git" / "config", "git");
    write_text(source / ".backup" / "old", "old");

    std::vector<uint8_t> palette(1024);
    for (int index = 0; index < 256; ++index) {
        palette[static_cast<std::size_t>(index) * 4] = static_cast<uint8_t>(index);
        palette[static_cast<std::size_t>(index) * 4 + 1] = static_cast<uint8_t>(255 - index);
        palette[static_cast<std::size_t>(index) * 4 + 2] = 7;
    }
    write_bytes(game / "palettes" / "palette.pal", palette);
}

void check_files(const map_pack::MapEntry& map, const std::vector<std::string>& expected) {
    OA_CHECK(map.files == expected);
}

void check_package(const fs::path& path, const std::vector<uint8_t>& source_manifest_bytes) {
    const Package package = read_package(path);
    const char* order[] = {
        "oamap.yaml",
        "anims/frond.gaf",
        "features/shore/shore.tdf",
        "maps/ash.ota",
        "maps/ash.tnt",
        "maps/fen.ota",
        "maps/fen.tnt",
        "objects3d/wreck.3do",
        "previews/ash.png",
        "previews/fen.png",
    };
    OA_CHECK(package.entries.size() == 10);
    if (package.entries.size() == 10) {
        for (std::size_t index = 0; index < 10; ++index)
            OA_CHECK(package.entries[index].name == order[index]);
    }
    OA_CHECK(!package.entries.empty() && package.entries.front().name == "oamap.yaml");
    for (const zip::StreamEntry& entry : package.entries) {
        OA_CHECK(entry.name.rfind("sounds/", 0) != 0);
        OA_CHECK(entry.name.rfind("units/", 0) != 0);
        OA_CHECK(entry.name.rfind("weapons/", 0) != 0);
        OA_CHECK(entry.name.rfind("palettes/", 0) != 0);
        OA_CHECK(!entry.directory);
        const bool stored = entry.name.ends_with(".png");
        OA_CHECK(entry.method == (stored ? zip::Method::stored : zip::Method::deflated));
    }
    const std::vector<uint8_t>* manifest = entry_bytes(package, "oamap.yaml");
    OA_CHECK(manifest != nullptr);
    if (manifest == nullptr)
        return;
    OA_CHECK(*manifest != source_manifest_bytes);
    map_pack::Manifest read;
    std::vector<map_pack::Problem> problems;
    OA_CHECK(map_pack::read_manifest(*manifest, map_pack::ManifestUse::package, read, problems));
    OA_CHECK(problems.empty());
    OA_CHECK(read.id == "shoals");
    OA_CHECK(read.name == "Shoal Pack");
    OA_CHECK(read.author.name == "Ridge");
    OA_CHECK(read.maps.size() == 2);
    if (read.maps.size() != 2)
        return;
    const map_pack::MapEntry& ash = read.maps[0];
    const map_pack::MapEntry& fen = read.maps[1];
    OA_CHECK(ash.stem == "ash");
    OA_CHECK(ash.title == "Ash Shore");
    OA_CHECK(ash.description == "A ring of ash.");
    OA_CHECK(ash.players == 2);
    OA_CHECK(ash.size == "1x1");
    OA_CHECK(ash.preview == "previews/ash.png");
    check_files(
        ash, {"maps/ash.ota", "maps/ash.tnt", "features/shore/shore.tdf", "objects3d/wreck.3do"}
    );
    OA_CHECK(fen.stem == "fen");
    OA_CHECK(fen.title == "Fen Crossing");
    OA_CHECK(fen.description == std::string(126, 'a'));
    OA_CHECK(fen.players == 4);
    OA_CHECK(fen.size == "1x1");
    OA_CHECK(fen.preview == "previews/fen.png");
    check_files(
        fen, {"maps/fen.ota", "maps/fen.tnt", "anims/frond.gaf", "features/shore/shore.tdf"}
    );
    const std::vector<uint8_t>* wreck = entry_bytes(package, "objects3d/wreck.3do");
    const std::vector<uint8_t>* frond = entry_bytes(package, "anims/frond.gaf");
    OA_CHECK(wreck != nullptr && *wreck == std::vector<uint8_t>({'3', 'D', 'O'}));
    OA_CHECK(frond != nullptr && *frond == std::vector<uint8_t>({'G', 'A', 'F'}));

    const std::vector<uint8_t>* preview = entry_bytes(package, "previews/ash.png");
    OA_CHECK(preview != nullptr);
    if (preview == nullptr)
        return;
    png::Info info{};
    png::Messages messages{};
    OA_CHECK(png::read_info(*preview, messages, &info));
    OA_CHECK(info.header.width == 4);
    OA_CHECK(info.header.height == 4);
    OA_CHECK(info.header.bit_depth == 8);
    OA_CHECK(info.header.color_type == png::ColorType::palette);
    OA_CHECK(info.has_palette);
    OA_CHECK(info.palette[10].r == 10 && info.palette[10].g == 245 && info.palette[10].b == 7);
    const std::size_t row = png::row_bytes(info.header, {});
    std::vector<uint8_t> rows(static_cast<std::size_t>(info.header.height) * row);
    OA_CHECK(png::read_image(*preview, info, {}, messages, rows) == png::Progress::end);
    OA_CHECK(!rows.empty() && rows[0] == 10);
}

void check_summary(const std::string& text) {
    OA_CHECK(contains(
        text,
        "ash@shoals: 4 files; needs from the game: anims/cinder.gaf, flare, game_tree, scout\n"
    ));
    OA_CHECK(contains(text, "fen@shoals: 4 files; needs from the game: nothing\n"));
    OA_CHECK(contains(
        text,
        "left out: anims/unused.gaf, features/other/idle.tdf, sounds/x.wav, units/x.fbi, "
        "weapons/flare.tdf\n"
    ));
}

std::string manifest_text(std::string_view id, std::string_view maps) {
    return std::string("oamap: 1\n") + "id: \"" + std::string{id} + "\"\n" +
           "name: \"Shoal Pack\"\n"
           "version: \"1.2\"\n"
           "author:\n"
           "  name: \"Ridge\"\n"
           "packaging:\n"
           "  revision: 1\n"
           "  date: \"2026-11-01\"\n"
           "  packager: \"oa-tool 0.8\"\n"
           "requires:\n"
           "  base: \"ta-3.1c\"\n"
           "maps:\n" +
           std::string{maps};
}

void test_pack_map() {
    const fs::path root = fs::current_path();
    const fs::path source = root / "pack-map-source";
    const fs::path game = root / "pack-map-game";
    const fs::path out_a = root / "pack-map-a.oamap";
    const fs::path out_b = root / "pack-map-b.oamap";
    std::error_code error;
    fs::remove(out_a, error);
    fs::remove(out_b, error);
    write_source(source, game);
    const std::vector<uint8_t> source_manifest_bytes = read_bytes(source / "oamap.yaml");

    std::ostringstream direct_out;
    std::ostringstream direct_err;
    oa::tool::Output direct{direct_out, direct_err};
    int direct_status = 1;
    try {
        direct_status =
            oa::tool::pack_map(oa::tool::PackMapRequest{source, out_a, false, game}, direct);
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "pack_map threw: %s\n", failure.what());
        OA_CHECK(false);
    }
    OA_CHECK(direct_status == oa::tool::exit_done);
    OA_CHECK(direct_err.str().empty());
    check_summary(direct_out.str());

    const Captured second =
        run({"pack", source.string(), "--game-dir", game.string(), "--out", out_b.string()});
    OA_CHECK(second.status == oa::tool::exit_done);
    OA_CHECK(second.err.empty());
    check_summary(second.out);
    const std::vector<uint8_t> bytes_a = read_bytes(out_a);
    const std::vector<uint8_t> bytes_b = read_bytes(out_b);
    OA_CHECK(!bytes_a.empty());
    OA_CHECK(bytes_a == bytes_b);
    const std::string hash = hash_bytes(bytes_a);
    OA_CHECK(hash_bytes(bytes_b) == hash);
    OA_CHECK(printed_hash(direct_out.str()) == hash);
    OA_CHECK(printed_hash(second.out) == hash);
    check_package(out_a, source_manifest_bytes);

    const Captured missing =
        run({"pack", source.string(), "--out", (root / "pack-map-missing.oamap").string()});
    OA_CHECK(missing.status == oa::tool::exit_failed);
    OA_CHECK(contains(missing.err, "palette"));
    OA_CHECK(contains(missing.err, "map packs need the game's palette for previews"));
    OA_CHECK(!fs::exists(root / "pack-map-missing.oamap"));

    Scratch scratch;
    const fs::path existing = scratch.path / "existing.oamap";
    write_text(existing, "original");
    const Captured kept =
        run({"pack", source.string(), "--game-dir", game.string(), "--out", existing.string()});
    OA_CHECK(kept.status == oa::tool::exit_failed);
    OA_CHECK(contains(kept.err, "the output already exists"));
    OA_CHECK(
        read_bytes(existing) == std::vector<uint8_t>({'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l'})
    );
    OA_CHECK(!fs::exists(fs::path(existing.string() + ".part")));

    const fs::path duplicate = scratch.path / "duplicate";
    write_text(duplicate / "oamap.yaml", manifest_text("shoals", "  - stem: \"ash\"\n"));
    write_text(duplicate / "maps" / "ash.ota", "x");
    write_text(duplicate / "maps" / "ash.tnt", "x");
    write_text(duplicate / "features" / "one.tdf", "[shared]\n{\nobject=wreck;\n}\n");
    write_text(duplicate / "features" / "two.tdf", "[shared]\n{\nobject=wreck;\n}\n");
    const Captured twice = run(
        {"pack",
         duplicate.string(),
         "--game-dir",
         game.string(),
         "--out",
         (scratch.path / "dup.oamap").string()}
    );
    OA_CHECK(twice.status == oa::tool::exit_failed);
    OA_CHECK(contains(twice.err, "features/one.tdf"));
    OA_CHECK(contains(twice.err, "features/two.tdf"));
    OA_CHECK(contains(twice.err, "shared"));

    const std::string long_stem(40, 's');
    const std::string long_id = "abcde-fghij-klmno-pqrst";
    const fs::path named = scratch.path / "long-name";
    write_text(named / "oamap.yaml", manifest_text(long_id, "  - stem: \"" + long_stem + "\"\n"));
    const Captured over = run(
        {"pack",
         named.string(),
         "--game-dir",
         game.string(),
         "--out",
         (scratch.path / "long.oamap").string()}
    );
    OA_CHECK(over.status == oa::tool::exit_failed);
    OA_CHECK(contains(over.err, "64 bytes"));
    OA_CHECK(contains(over.err, "63"));

    const fs::path mod = scratch.path / "mod";
    write_text(mod / "oamod.yaml", "oamod: 1\n");
    const Captured wrong_kind = run(
        {"pack",
         mod.string(),
         "--game-dir",
         game.string(),
         "--out",
         (scratch.path / "mod.oamod").string()}
    );
    OA_CHECK(wrong_kind.status == oa::tool::exit_usage);
    OA_CHECK(contains(wrong_kind.err, "game-dir"));
    OA_CHECK(contains(wrong_kind.err, "help pack"));

    {
        WorkingFolder here(scratch.path);
        const Captured named_out = run({"pack", source.string(), "--game-dir", game.string()});
        OA_CHECK(named_out.status == oa::tool::exit_done);
        OA_CHECK(fs::exists(scratch.path / "shoals-1.2-r3.oamap"));
    }

    fs::remove(out_a, error);
    fs::remove(out_b, error);
}

} // namespace

int main() {
    try {
        test_pack_map();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "pack map test threw: %s\n", failure.what());
        OA_CHECK(false);
    }
    return oa::test::check_exit_status();
}
