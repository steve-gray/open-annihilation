// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-tool check of a map pack, run in-process. A stand-in game holds rock1.
// A made-up mod holds isle_rock. One pack fits both, one redefines rock1, and
// one redefines isle_rock. With --data the same check runs against the
// installed game and a section that game already defines.

#include "command.hpp"

#include "oa/app/package_install/origin.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/json.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/formats/zip.hpp"
#include "oa/test/check.hpp"
#include "oa/test/game_data.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;
namespace json = oa::formats::json;
namespace map_pack = oa::data::map_pack;
namespace sha256 = oa::base::sha256;
namespace tnt = oa::formats::tnt;
namespace zip = oa::formats::zip;

constexpr std::string_view pack_id = "archipelago";
constexpr std::string_view map_stem = "isle_of_ashes";

constexpr std::string_view mod_profile =
    "oamod: 1\n"
    "id: example-mod\n"
    "name: Example Mod\n"
    "version: \"1\"\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: test}\n";

constexpr std::string_view other_profile =
    "oamod: 1\n"
    "id: other-mod\n"
    "name: Other Mod\n"
    "version: \"1\"\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: test}\n";

struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-tool-check-map")) {}

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

struct Piece {
    std::string name;
    std::vector<uint8_t> bytes;
};

/// Writes a 32-bit little-endian word.
void put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[at + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[at + 3] = static_cast<uint8_t>(value >> 24U);
}

/// Writes a 16-bit little-endian word.
void put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
}

/// Builds a 4x4 map whose feature records carry the given names.
std::vector<uint8_t> tnt_named(std::initializer_list<std::string_view> names) {
    constexpr std::size_t tile_map_at = 64;
    constexpr std::size_t attributes_at = 72;
    constexpr std::size_t tiles_at = 136;
    const auto features_at = tiles_at + 2 * tnt::layout::tile_bytes;
    std::vector<uint8_t> bytes(features_at + names.size() * tnt::layout::feature_record_bytes);
    put32(bytes, 0, static_cast<uint32_t>(tnt::Version::total_annihilation));
    put32(bytes, 4, 4);
    put32(bytes, 8, 4);
    put32(bytes, 12, static_cast<uint32_t>(tile_map_at));
    put32(bytes, 16, static_cast<uint32_t>(attributes_at));
    put32(bytes, 20, static_cast<uint32_t>(tiles_at));
    put32(bytes, 24, 2);
    put32(bytes, 28, static_cast<uint32_t>(names.size()));
    put32(bytes, 32, static_cast<uint32_t>(features_at));
    put32(bytes, 36, 17);
    put32(bytes, 40, 0);
    put32(bytes, 44, 0);
    put16(bytes, tile_map_at, 0);
    put16(bytes, tile_map_at + 2, 1);
    put16(bytes, tile_map_at + 4, 1);
    put16(bytes, tile_map_at + 6, 0);
    std::size_t index = 0;
    for (const auto name : names) {
        const auto at = features_at + index * tnt::layout::feature_record_bytes +
                        offsetof(tnt::FeatureDiskRecord, name);
        const auto count = name.size() < 127 ? name.size() : std::size_t{127};
        for (std::size_t character = 0; character < count; ++character)
            bytes[at + character] = static_cast<uint8_t>(name[character]);
        ++index;
    }
    return bytes;
}

/// An OTA that places one feature at the origin.
std::string ota_placing(std::string_view feature) {
    return "[GlobalHeader]\n{\n[Schema 0]\n{\nType=Network 1;\n[features]\n{\n"
           "[feature0]\n{\nFeaturename=" +
           std::string(feature) +
           ";\nXPos=0;\nZPos=0;\n}\n"
           "}\n}\n}\n";
}

/// One feature section, with nothing of its own.
std::string section_of(std::string_view name) {
    return "[" + std::string(name) + "]\n{\n}\n";
}

std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

Captured run(std::vector<std::string> arguments) {
    std::ostringstream out;
    std::ostringstream err;
    oa::tool::Output output{out, err};
    const int status = oa::tool::run_tool(arguments, output);
    return {status, out.str(), err.str()};
}

void show(const Captured& captured) {
    std::fprintf(
        stderr,
        "status %d\nstdout:\n%sstderr:\n%s",
        captured.status,
        captured.out.c_str(),
        captured.err.c_str()
    );
}

void expect_status(const Captured& captured, int status) {
    if (captured.status != status)
        show(captured);
    OA_CHECK(captured.status == status);
}

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

void write_bytes(const fs::path& path, std::span<const uint8_t> bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    OA_CHECK(static_cast<bool>(out));
}

void write_text(const fs::path& path, std::string_view text) {
    write_bytes(path, bytes_of(text));
}

/// Writes a stored zip. The pieces outlive the entry views.
void write_zip(const fs::path& path, const std::vector<Piece>& pieces) {
    std::vector<zip::NewEntry> entries;
    entries.reserve(pieces.size());
    for (const Piece& piece : pieces)
        entries.push_back({piece.name, piece.bytes});
    std::vector<uint8_t> archive;
    zip::ZipError error{};
    OA_CHECK(zip::write_archive(entries, archive, error));
    write_bytes(path, archive);
}

/// A pack of one map. `section` is the feature the map defines. `placed` is
/// the feature its OTA and TNT place. `id` is the pack id and `stem` the map.
map_pack::Manifest manifest_of(
    std::string_view id,
    std::string_view stem,
    std::string_view section_file,
    std::string_view engine
) {
    map_pack::Manifest manifest;
    manifest.format = map_pack::format_version;
    manifest.id = std::string(id);
    manifest.name = "Ridge Pack";
    manifest.version = "1";
    manifest.description = "Islands of ash.";
    manifest.homepage = "https://example.org/maps";
    manifest.author = {"Ridge", {}};
    manifest.packaging = {1, "2026-10-11", "test"};
    manifest.tags = {"example"};
    manifest.requires_base = "ta-3.1c";
    manifest.requires_engine = std::string(engine);
    map_pack::MapEntry map;
    map.stem = std::string(stem);
    map.title = "Isle of Ashes";
    map.size = "4x4";
    map.players = 2;
    map.files = {
        "maps/" + map.stem + ".ota",
        "maps/" + map.stem + ".tnt",
        std::string(section_file),
    };
    manifest.maps = {std::move(map)};
    return manifest;
}

/// Writes one map pack.
fs::path write_pack(
    const Scratch& scratch,
    std::string_view file_name,
    std::string_view id,
    std::string_view stem,
    std::string_view section,
    std::string_view placed,
    std::string_view section_file,
    std::string_view engine
) {
    const map_pack::Manifest manifest = manifest_of(id, stem, section_file, engine);
    std::vector<Piece> pieces;
    pieces.push_back({"oamap.yaml", bytes_of(map_pack::write_manifest(manifest))});
    pieces.push_back({"maps/" + std::string(stem) + ".ota", bytes_of(ota_placing(placed))});
    pieces.push_back({"maps/" + std::string(stem) + ".tnt", tnt_named({placed})});
    pieces.push_back({std::string(section_file), bytes_of(section_of(section))});
    const fs::path path = scratch.path / file_name;
    write_zip(path, pieces);
    return path;
}

/// The stand-in game: rev31.gp3 defines rock1 and holds a sprite.
fs::path write_game(const Scratch& scratch) {
    const fs::path game = scratch.path / "game";
    const std::vector<oa::HpiWriteFile> files = {
        {"features/base.tdf", bytes_of(section_of("rock1")), 0},
        {"anims/rock.gaf", std::vector<uint8_t>{1, 2, 3, 4}, 0},
    };
    write_bytes(game / "rev31.gp3", oa::write_hpi(files));
    return game;
}

/// A made-up mod. `origin` writes a catalogue record for example-mod at release 28.
fs::path write_mod(const Scratch& scratch, std::string_view folder, bool origin) {
    const fs::path path = scratch.path / folder;
    const bool other = folder == "other-mod";
    write_text(path / "oamod.yaml", other ? other_profile : mod_profile);
    if (!other)
        write_text(path / "features" / "mod.tdf", section_of("isle_rock"));
    if (origin) {
        install::Origin record{};
        record.kind = install::OriginKind::catalogue;
        record.registry = "example";
        record.catalogue_id = "example-mod";
        record.release = 28;
        record.sha256 = sha256::digest_of({});
        record.installed = "2026-10-11";
        write_text(path / ".oa-origin.yaml", install::origin_text(record));
    }
    return path;
}

const json::Json* member(const json::Json& object, std::string_view name) {
    return object.find(name);
}

std::optional<json::Json> parsed_object(const Captured& captured) {
    json::JsonError error{};
    std::optional<json::Json> value = json::parse_json(captured.out, error);
    if (!value || value->type() != json::JsonType::object) {
        show(captured);
        OA_CHECK(false);
        return std::nullopt;
    }
    return value;
}

std::vector<std::string> names_of(const json::Json& object) {
    const std::span<const std::string> names = object.names();
    return {names.begin(), names.end()};
}

std::string text_of(const json::Json* value) {
    if (value == nullptr || value->string() == nullptr)
        return {};
    return *value->string();
}

/// The compatible object of the first map, or null.
const json::Json* compatible_of(const json::Json& report) {
    const json::Json* maps = member(report, "maps");
    if (maps == nullptr || maps->elements().empty())
        return nullptr;
    return member(maps->elements().front(), "compatible");
}

void expect_compatible(const json::Json& report, std::vector<std::pair<std::string, bool>> keys) {
    const json::Json* compatible = compatible_of(report);
    if (compatible == nullptr) {
        OA_CHECK(false);
        return;
    }
    std::vector<std::string> names;
    names.reserve(keys.size());
    for (const auto& key : keys)
        names.push_back(key.first);
    OA_CHECK(names_of(*compatible) == names);
    for (const auto& key : keys) {
        const json::Json* value = member(*compatible, key.first);
        OA_CHECK(value != nullptr && value->boolean() == key.second);
    }
}

/// Checks the three packs against the stand-in game and the made-up mods.
void fixture_cases() {
    Scratch scratch;
    const fs::path game = write_game(scratch);
    const fs::path mod = write_mod(scratch, "example-mod", true);
    const fs::path plain = write_mod(scratch, "plain-mod", false);
    const fs::path other = write_mod(scratch, "other-mod", false);
    const std::string mod_arg = mod.string();
    const std::string other_arg = other.string() + "=other-mod@3";
    const fs::path fitting = write_pack(
        scratch,
        "fitting.oamap",
        pack_id,
        map_stem,
        "ash_vent",
        "rock1",
        "features/ash.tdf",
        ">= 0.0.1"
    );
    const fs::path base_clash = write_pack(
        scratch,
        "base-clash.oamap",
        pack_id,
        map_stem,
        "rock1",
        "rock1",
        "features/ash.tdf",
        ">= 0.0.1"
    );
    const fs::path mod_clash = write_pack(
        scratch,
        "mod-clash.oamap",
        pack_id,
        map_stem,
        "isle_rock",
        "rock1",
        "features/ash.tdf",
        ">= 0.0.1"
    );

    const std::vector<std::string> expected_names = {
        "check",   "ok",      "problems", "warnings", "file",     "kind",          "id",
        "name",    "version", "revision", "size",     "sha256",   "unpacked_size", "files",
        "summary", "author",  "homepage", "tags",     "requires", "packaging",     "maps",
    };

    const auto check_json = [&](const fs::path& pack, std::vector<std::string> extra) {
        std::vector<std::string> arguments = {
            "check",
            pack.string(),
            "--game-dir",
            game.string(),
            "--mod",
            mod_arg,
            "--json",
        };
        arguments.insert(arguments.end(), extra.begin(), extra.end());
        return run(std::move(arguments));
    };

    const Captured fits = check_json(fitting, {"--mod", other_arg});
    expect_status(fits, oa::tool::exit_done);
    OA_CHECK(fits.err.empty());
    if (const std::optional<json::Json> report = parsed_object(fits)) {
        OA_CHECK(names_of(*report) == expected_names);
        OA_CHECK(member(*report, "ok") && member(*report, "ok")->boolean() == true);
        OA_CHECK(member(*report, "problems") && member(*report, "problems")->elements().empty());
        OA_CHECK(text_of(member(*report, "kind")) == "oamap");
        OA_CHECK(text_of(member(*report, "id")) == pack_id);
        OA_CHECK(text_of(member(*report, "summary")) == "Islands of ash.");
        OA_CHECK(text_of(member(*report, "author")) == "Ridge");
        OA_CHECK(text_of(member(*report, "homepage")) == "https://example.org/maps");
        OA_CHECK(member(*report, "revision") && member(*report, "revision")->integer() == 1);
        const json::Json* requirement = member(*report, "requires");
        OA_CHECK(requirement != nullptr);
        if (requirement != nullptr) {
            OA_CHECK(names_of(*requirement) == std::vector<std::string>({"base", "engine"}));
            OA_CHECK(text_of(member(*requirement, "engine")) == ">= 0.0.1");
        }
        const json::Json* packaging = member(*report, "packaging");
        OA_CHECK(packaging != nullptr);
        if (packaging != nullptr)
            OA_CHECK(
                names_of(*packaging) == std::vector<std::string>({"revision", "date", "packager"})
            );
        const json::Json* maps = member(*report, "maps");
        OA_CHECK(maps != nullptr && maps->elements().size() == 1);
        if (maps != nullptr && !maps->elements().empty()) {
            const json::Json& map = maps->elements().front();
            OA_CHECK(
                names_of(map) == std::vector<std::string>(
                                     {"map", "name", "players", "size", "compatible", "failures"}
                                 )
            );
            OA_CHECK(text_of(member(map, "map")) == "isle_of_ashes@archipelago");
            OA_CHECK(text_of(member(map, "name")) == "Isle of Ashes");
            OA_CHECK(member(map, "players") && member(map, "players")->integer() == 2);
            OA_CHECK(text_of(member(map, "size")) == "4x4");
            OA_CHECK(member(map, "preview") == nullptr);
            const json::Json* failures = member(map, "failures");
            OA_CHECK(failures != nullptr && failures->type() == json::JsonType::object);
            if (failures != nullptr)
                OA_CHECK(names_of(*failures).empty());
        }
        expect_compatible(
            *report, {{"ta-3.1c", true}, {"example-mod@28", true}, {"other-mod@3", true}}
        );
    }

    const Captured text = run({
        "check",
        fitting.string(),
        "--game-dir",
        game.string(),
        "--mod",
        mod_arg,
    });
    expect_status(text, oa::tool::exit_done);
    const std::string base_line = "isle_of_ashes@archipelago  ta-3.1c  fits\n";
    const std::string mod_line = "isle_of_ashes@archipelago  example-mod@28  fits\n";
    const auto base_at = text.out.find(base_line);
    const auto mod_at = text.out.find(mod_line);
    const auto result_at = text.out.find("result: ok\n");
    OA_CHECK(base_at != std::string::npos);
    OA_CHECK(mod_at != std::string::npos);
    OA_CHECK(result_at != std::string::npos);
    OA_CHECK(base_at < result_at && mod_at < result_at);
    OA_CHECK(text.out.find("packaging.packager:") < base_at);

    const Captured clashing = check_json(base_clash, {});
    expect_status(clashing, oa::tool::exit_failed);
    if (const std::optional<json::Json> report = parsed_object(clashing)) {
        OA_CHECK(member(*report, "ok") && member(*report, "ok")->boolean() == false);
        expect_compatible(*report, {{"ta-3.1c", false}, {"example-mod@28", false}});
        const json::Json* problems = member(*report, "problems");
        OA_CHECK(problems != nullptr && problems->elements().size() == 1);
        if (problems != nullptr && !problems->elements().empty()) {
            const std::string sentence = text_of(&problems->elements().front());
            OA_CHECK(contains(
                sentence,
                "isle_of_ashes@archipelago does not fit ta-3.1c: New names: the feature 'rock1' "
                "is defined already by features/base.tdf"
            ));
        }
        const json::Json* maps = member(*report, "maps");
        const json::Json* failures = maps != nullptr && !maps->elements().empty()
                                         ? member(maps->elements().front(), "failures")
                                         : nullptr;
        const json::Json* base = failures != nullptr ? member(*failures, "ta-3.1c") : nullptr;
        OA_CHECK(base != nullptr && !base->elements().empty());
        if (base != nullptr && !base->elements().empty()) {
            const json::Json& failure = base->elements().front();
            OA_CHECK(text_of(member(failure, "rule")) == "new_names");
            OA_CHECK(text_of(member(failure, "subject")) == "rock1");
            OA_CHECK(text_of(member(failure, "detail")) == "features/base.tdf");
        }
    }

    const Captured clash_text = run({
        "check",
        base_clash.string(),
        "--game-dir",
        game.string(),
    });
    expect_status(clash_text, oa::tool::exit_failed);
    OA_CHECK(contains(clash_text.out, "isle_of_ashes@archipelago  ta-3.1c  does not fit\n"));
    OA_CHECK(contains(clash_text.out, "  New names: the feature 'rock1'"));
    const auto failure_at = clash_text.out.find("does not fit\n");
    const auto refused_at = clash_text.out.find("result: refused\n");
    OA_CHECK(failure_at != std::string::npos && refused_at != std::string::npos);
    OA_CHECK(failure_at < refused_at);

    const Captured mod_only = check_json(mod_clash, {});
    expect_status(mod_only, oa::tool::exit_done);
    if (const std::optional<json::Json> report = parsed_object(mod_only)) {
        OA_CHECK(member(*report, "ok") && member(*report, "ok")->boolean() == true);
        OA_CHECK(member(*report, "problems") && member(*report, "problems")->elements().empty());
        expect_compatible(*report, {{"ta-3.1c", true}, {"example-mod@28", false}});
        const json::Json* maps = member(*report, "maps");
        const json::Json* failures = maps != nullptr && !maps->elements().empty()
                                         ? member(maps->elements().front(), "failures")
                                         : nullptr;
        OA_CHECK(
            failures != nullptr && names_of(*failures) == std::vector<std::string>{"example-mod@28"}
        );
        const json::Json* listed =
            failures != nullptr ? member(*failures, "example-mod@28") : nullptr;
        OA_CHECK(listed != nullptr && !listed->elements().empty());
        if (listed != nullptr && !listed->elements().empty()) {
            OA_CHECK(text_of(member(listed->elements().front(), "rule")) == "new_names");
            OA_CHECK(text_of(member(listed->elements().front(), "subject")) == "isle_rock");
            OA_CHECK(text_of(member(listed->elements().front(), "detail")) == "features/mod.tdf");
        }
    }

    const Captured no_release = run({
        "check",
        fitting.string(),
        "--json",
        "--game-dir",
        game.string(),
        "--mod",
        plain.string(),
    });
    expect_status(no_release, oa::tool::exit_done);
    OA_CHECK(
        no_release.err ==
        "warning: example-mod has no release, so its catalogue key is the profile's id\n"
    );
    OA_CHECK(!contains(no_release.out, "has no release"));
    if (const std::optional<json::Json> report = parsed_object(no_release))
        expect_compatible(*report, {{"ta-3.1c", true}, {"example-mod", true}});

    const Captured missing_game = run({"check", fitting.string(), "--json"});
    expect_status(missing_game, oa::tool::exit_usage);
    OA_CHECK(missing_game.out.empty());
    OA_CHECK(contains(missing_game.err, "option '--game-dir' is required for a map pack\n"));
    OA_CHECK(contains(missing_game.err, "run 'oa-tool help check'\n"));

    const fs::path refused = write_pack(
        scratch,
        "refused.oamap",
        pack_id,
        map_stem,
        "ash_vent",
        "rock1",
        "features/ash.tdf",
        ">= 9999.0.0"
    );
    const Captured refused_run = run({"check", refused.string()});
    expect_status(refused_run, oa::tool::exit_failed);
    OA_CHECK(contains(refused_run.out, "result: refused\n"));
    OA_CHECK(!contains(refused_run.err, "required for a map pack"));
    OA_CHECK(!contains(refused_run.out, "ta-3.1c"));

    const Captured empty_mod =
        run({"check", fitting.string(), "--game-dir", game.string(), "--mod="});
    expect_status(empty_mod, oa::tool::exit_usage);
    OA_CHECK(empty_mod.out.empty());
    OA_CHECK(contains(empty_mod.err, "option '--mod' needs a folder"));

    const Captured empty_key =
        run({"check", fitting.string(), "--game-dir", game.string(), "--mod", "folder="});
    expect_status(empty_key, oa::tool::exit_usage);
    OA_CHECK(empty_key.out.empty());

    const Captured ignored = run({"check", "missing.oamod", "--mod", plain.string()});
    expect_status(ignored, oa::tool::exit_failed);
    OA_CHECK(contains(ignored.out, "result: refused\n"));

    const Captured help = run({"help", "check"});
    expect_status(help, oa::tool::exit_done);
    OA_CHECK(contains(help.out, "--mod"));
}

/// A map that defines DragonsTeeth does not fit the installed base game.
void installed_case() {
    const fs::path game = oa::test::require_game_directory("a map pack checked against the game");
    Scratch scratch;
    const fs::path pack = write_pack(
        scratch,
        "teeth.oamap",
        "oa-check",
        "oa_check_teeth",
        "DragonsTeeth",
        "DragonsTeeth",
        "features/oa-check/teeth.tdf",
        ">= 0.0.1"
    );
    const Captured checked = run({"check", pack.string(), "--json", "--game-dir", game.string()});
    expect_status(checked, oa::tool::exit_failed);
    if (const std::optional<json::Json> report = parsed_object(checked)) {
        OA_CHECK(text_of(member(*report, "kind")) == "oamap");
        OA_CHECK(member(*report, "ok") && member(*report, "ok")->boolean() == false);
        expect_compatible(*report, {{"ta-3.1c", false}});
        const json::Json* problems = member(*report, "problems");
        OA_CHECK(problems != nullptr && !problems->elements().empty());
        if (problems != nullptr && !problems->elements().empty()) {
            const std::string sentence = text_of(&problems->elements().front());
            OA_CHECK(contains(sentence, "oa_check_teeth@oa-check does not fit ta-3.1c:"));
            OA_CHECK(contains(sentence, "DragonsTeeth"));
        }
        const json::Json* maps = member(*report, "maps");
        const json::Json* failures = maps != nullptr && !maps->elements().empty()
                                         ? member(maps->elements().front(), "failures")
                                         : nullptr;
        const json::Json* base = failures != nullptr ? member(*failures, "ta-3.1c") : nullptr;
        OA_CHECK(base != nullptr && !base->elements().empty());
        if (base != nullptr && !base->elements().empty()) {
            OA_CHECK(text_of(member(base->elements().front(), "rule")) == "new_names");
            OA_CHECK(text_of(member(base->elements().front(), "subject")) == "DragonsTeeth");
            OA_CHECK(!text_of(member(base->elements().front(), "detail")).empty());
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv))
        installed_case();
    else
        fixture_cases();
    return oa::test::check_exit_status();
}
