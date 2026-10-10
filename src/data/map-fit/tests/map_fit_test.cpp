// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// One map checked against a small game, and against the installed game.
// The small game is an archive in a temporary folder. The map's folder sits
// beside that folder, so the store never sees the map as part of the game.

#include "oa/data/map_fit/map_fit.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/test/check.hpp"
#include "oa/test/game_assets.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace map_fit = oa::data::map_fit;
namespace map_pack = oa::data::map_pack;
namespace tdf = oa::formats::tdf;
namespace tnt = oa::formats::tnt;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kPackId = "isles";

const char* kBaseFeatures = "[rock1]\n"
                            "{\n"
                            "description=Rock;\n"
                            "}\n"
                            "[tree1]\n"
                            "{\n"
                            "featuredead=tree1dead;\n"
                            "}\n"
                            "[tree1dead]\n"
                            "{\n"
                            "description=Dead;\n"
                            "}\n"
                            "[torch]\n"
                            "{\n"
                            "featuredead=no_such_feature;\n"
                            "burnweapon=no_such_weapon;\n"
                            "}\n";

const char* kExtraFeatures = "[ROCK1]\n"
                             "{\n"
                             "object=late;\n"
                             "}\n";

const char* kBroken = "[not closed\n";

const char* kWeapons = "[burnit]\n"
                       "{\n"
                       "description=Burn;\n"
                       "}\n";

/// One map written under a folder, and the manifest entry that lists it.
struct WrittenMap {
    fs::path folder{};
    map_pack::MapEntry entry{};
};

/// The fixture game: one archive, and the names collected from it.
struct Game {
    fs::path scratch{};
    std::unique_ptr<oa::AssetStore> store{};
    oa::data::defs::DataLayout layout{};
    map_fit::GameNames names{};

    Game() = default;
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    /// Moves the game, leaving the source with nothing to remove.
    ///
    /// @param other game left empty
    Game(Game&& other) noexcept
        : scratch(std::move(other.scratch)), store(std::move(other.store)),
          layout(std::move(other.layout)), names(std::move(other.names)) {
        other.scratch.clear();
    }

    Game& operator=(Game&&) = delete;

    /// Drops the store before the folder, so the archive is closed first.
    ~Game() {
        store.reset();
        if (scratch.empty())
            return;
        std::error_code error;
        fs::remove_all(scratch, error);
    }
};

/// Removes a scratch folder when the data case returns.
struct Scratch {
    fs::path path{};

    ~Scratch() {
        if (path.empty())
            return;
        std::error_code error;
        fs::remove_all(path, error);
    }
};

/// Copies text into bytes.
///
/// @param text the text
/// @return the same bytes
std::vector<uint8_t> bytes_of(std::string_view text) {
    std::vector<uint8_t> bytes(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
        bytes[index] = static_cast<uint8_t>(text[index]);
    return bytes;
}

/// Returns a host path's UTF-8 spelling.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8_text(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// Writes one file, creating its parent folders.
///
/// @param path the file
/// @param bytes the bytes
void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    OA_CHECK(static_cast<bool>(output));
    if (!bytes.empty())
        output.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
    OA_CHECK(static_cast<bool>(output));
}

/// Writes a 32-bit little-endian word.
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

/// Writes a 16-bit little-endian word.
///
/// @param bytes the buffer
/// @param at the offset
/// @param value the word
void put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
}

/// Builds a small map whose feature records carry the given names.
///
/// The minimap flag is clear, so the file ends at the feature records.
///
/// @param names the feature names, in record order
/// @return the map bytes
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
        const auto count = std::min(name.size(), std::size_t{127});
        for (std::size_t character = 0; character < count; ++character)
            bytes[at + character] = static_cast<uint8_t>(name[character]);
        ++index;
    }
    return bytes;
}

/// One feature a schema places.
///
/// @param index the entry number
/// @param name the feature name
/// @param placed true to write coordinates at or above zero
/// @param x the X position
/// @param z the Z position
/// @return the entry
std::string feature_entry(int index, std::string_view name, bool placed, int x, int z) {
    std::string text =
        "[feature" + std::to_string(index) + "]\n{\nFeaturename=" + std::string(name) + ";\n";
    if (placed)
        text += "XPos=" + std::to_string(x) + ";\nZPos=" + std::to_string(z) + ";\n";
    text += "}\n";
    return text;
}

/// One unit a schema places.
///
/// @param index the entry number
/// @param name the unit name; empty writes a blank name and an Ident
/// @return the entry
std::string unit_entry(int index, std::string_view name) {
    std::string text = "[unit" + std::to_string(index) + "]\n{\n";
    if (name.empty())
        text += "Unitname=;\nIdent=IGNORED;\n";
    else
        text += "Unitname=" + std::string(name) + ";\n";
    text += "}\n";
    return text;
}

/// The fitting map's OTA, plus one extra schema feature and one extra unit.
///
/// Schema 0 places rock1, isle_rock and loop. A feature with a negative
/// coordinate and one with none are written and must not count. Schema 1
/// places ARMCOM and a blank unit whose Ident is not a unit. Schema 3 sits
/// past a gap and must not be read.
///
/// @param extra_feature a feature added to Schema 0, or empty
/// @param extra_unit a unit added to Schema 1, or empty
/// @return the OTA text
std::string fitting_ota(std::string_view extra_feature, std::string_view extra_unit) {
    std::string text = "[GlobalHeader]\n{\n[Schema 0]\n{\nType=Network 1;\n[features]\n{\n";
    text += feature_entry(0, "rock1", true, 0, 0);
    text += feature_entry(1, "isle_rock", true, 1, 2);
    text += feature_entry(2, "not_placed", true, -1, 3);
    text += feature_entry(3, "no_coordinates", false, 0, 0);
    text += feature_entry(4, "loop", true, 2, 2);
    if (!extra_feature.empty())
        text += feature_entry(5, extra_feature, true, 3, 3);
    text += "}\n}\n[Schema 1]\n{\nType=Network 2;\n[units]\n{\n";
    text += unit_entry(0, "ARMCOM");
    text += unit_entry(1, "");
    if (!extra_unit.empty())
        text += unit_entry(2, extra_unit);
    text += "}\n}\n[Schema 3]\n{\nType=Network 4;\n[units]\n{\n";
    text += unit_entry(0, "SHOULD_NOT");
    text += "}\n[features]\n{\n";
    text += feature_entry(0, "past_gap", true, 1, 1);
    text += "}\n}\n}\n";
    return text;
}

/// The fitting map's feature TDF.
///
/// isle_rock names a model, a dead feature and a burn weapon. The dead
/// feature is a sprite. loop names itself, so the walk must stop.
///
/// @return the TDF text
std::string fitting_tdf() {
    return "[isle_rock]\n"
           "{\n"
           "object=isle;\n"
           "featuredead=isle_rock_dead;\n"
           "burnweapon=burnit;\n"
           "}\n"
           "[isle_rock_dead]\n"
           "{\n"
           "filename=isle;\n"
           "seqname=dead;\n"
           "}\n"
           "[loop]\n"
           "{\n"
           "object=isle;\n"
           "featuredead=loop;\n"
           "}\n";
}

/// Replaces the first occurrence of a piece of text.
///
/// @param text the text
/// @param from the piece
/// @param to the replacement
/// @return the text, with that piece replaced
std::string replaced(std::string text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    OA_CHECK(at != std::string::npos);
    if (at == std::string::npos)
        return text;
    text.replace(at, from.size(), to);
    return text;
}

/// Prints every failure.
///
/// @param fit the result
void print_failures(const map_fit::Fit& fit) {
    for (const auto& failure : fit.failures)
        std::fprintf(stderr, "  %s\n", map_fit::describe(failure).c_str());
}

/// Tells whether two results name the same failures in the same order.
///
/// @param left one result
/// @param right the other
/// @return true when every rule, subject and detail agrees
bool same_fit(const map_fit::Fit& left, const map_fit::Fit& right) {
    if (left.failures.size() != right.failures.size())
        return false;
    for (std::size_t index = 0; index < left.failures.size(); ++index) {
        const auto& a = left.failures[index];
        const auto& b = right.failures[index];
        if (a.rule != b.rule || a.subject != b.subject || a.detail != b.detail)
            return false;
    }
    return true;
}

/// Requires the map to fit, and prints what it broke.
///
/// @param fit the result
void expect_fits(const map_fit::Fit& fit) {
    if (!fit.fits()) {
        std::fprintf(stderr, "expected the map to fit\n");
        print_failures(fit);
    }
    OA_CHECK(fit.fits());
}

/// Requires exactly one failure, and prints whatever came back.
///
/// @param fit the result
/// @param rule the rule
/// @param subject the path or the name
/// @param detail the reason
void expect_one(
    const map_fit::Fit& fit, map_fit::Rule rule, std::string_view subject, std::string_view detail
) {
    const bool ok = fit.failures.size() == 1 && fit.failures[0].rule == rule &&
                    fit.failures[0].subject == subject && fit.failures[0].detail == detail;
    if (!ok) {
        std::fprintf(
            stderr,
            "expected one %s failure on '%.*s'\n  %.*s\n",
            std::string(map_fit::rule_name(rule)).c_str(),
            static_cast<int>(subject.size()),
            subject.data(),
            static_cast<int>(detail.size()),
            detail.data()
        );
        print_failures(fit);
    }
    OA_CHECK(fit.failures.size() == 1);
    if (fit.failures.empty())
        return;
    OA_CHECK(fit.failures[0].rule == rule);
    OA_CHECK(fit.failures[0].subject == subject);
    OA_CHECK(fit.failures[0].detail == detail);
}

/// The archive the fixture game is.
///
/// @return the files, base features before the later copy of rock1
std::vector<oa::HpiWriteFile> game_files() {
    const std::vector<uint8_t> blob{1, 2, 3, 4};
    return {
        {"features/base.tdf", bytes_of(kBaseFeatures), 0},
        {"features/extra.tdf", bytes_of(kExtraFeatures), 0},
        {"features/bad.tdf", bytes_of(kBroken), 0},
        {"anims/rock.gaf", blob, 0},
        {"objects3d/base.3do", blob, 0},
        {"weapons/weapons.tdf", bytes_of(kWeapons), 0},
        {"units/ARMCOM.FBI", bytes_of("ARMCOM"), 0},
    };
}

/// Builds the fixture game in a new scratch folder.
///
/// @return the game; the caller drops it, and the folder goes with it
Game make_game() {
    Game game;
    game.scratch = oa::test::make_scratch_directory("map-fit");
    const auto root = game.scratch / "game";
    fs::create_directories(root);
    const auto archive = root / "totala1.hpi";
    write_bytes(archive, oa::write_hpi(game_files()));
    game.store = std::make_unique<oa::AssetStore>(root);
    game.store->mount(archive);
    game.names = map_fit::collect_game_names(*game.store, game.layout);
    return game;
}

/// Writes one map's listed files, and any file that must stay unlisted.
///
/// @param folder the map's folder
/// @param stem the map's stem
/// @param listed the files the manifest names
/// @param unlisted files on disk that the manifest leaves out
/// @return the folder and the entry
WrittenMap write_tree(
    const fs::path& folder,
    std::string stem,
    std::vector<std::pair<std::string, std::vector<uint8_t>>> listed,
    std::vector<std::pair<std::string, std::vector<uint8_t>>> unlisted
) {
    WrittenMap written;
    written.folder = folder;
    written.entry.stem = std::move(stem);
    written.entry.title = written.entry.stem;
    for (const auto& file : listed) {
        write_bytes(folder / file.first, file.second);
        written.entry.files.push_back(file.first);
    }
    for (const auto& file : unlisted)
        write_bytes(folder / file.first, file.second);
    return written;
}

/// Writes the isle map, with the fitting TNT.
///
/// @param root the scratch folder
/// @param directory the map's directory name
/// @param ota the OTA text
/// @param tdf the feature TDF
/// @param extra further listed files
/// @param stray true to leave anims/not-listed.gaf on disk, unlisted
/// @return the map
WrittenMap isle_map(
    const fs::path& root,
    std::string_view directory,
    std::string_view ota,
    std::string_view feature_tdf,
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& extra,
    bool stray
) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> listed = {
        {"maps/isle.ota", bytes_of(ota)},
        {"maps/isle.tnt", tnt_named({"isle_rock", "tree1", "torch"})},
        {"features/isle/isle.tdf", bytes_of(feature_tdf)},
        {"objects3d/isle.3do", {5, 6, 7}},
        {"anims/isle.gaf", {8, 9}},
    };
    listed.insert(listed.end(), extra.begin(), extra.end());
    std::vector<std::pair<std::string, std::vector<uint8_t>>> unlisted;
    if (stray)
        unlisted.push_back({"anims/not-listed.gaf", {4, 4}});
    return write_tree(root / directory, "isle", std::move(listed), std::move(unlisted));
}

/// Checks a map from its folder.
///
/// @param game the game
/// @param written the map
/// @param id the pack id
/// @return the result
map_fit::Fit check_folder(const Game& game, const WrittenMap& written, std::string_view id) {
    const auto files = map_fit::folder_map_files(written.folder, written.entry, id);
    return map_fit::check_map(*game.store, game.names, *files, written.entry, id, game.layout);
}

/// Mounts a map and requires the same result as the folder check.
///
/// @param game the game
/// @param written the map
/// @param folder_fit the folder check's result
void expect_same_mounted(Game& game, const WrittenMap& written, const map_fit::Fit& folder_fit) {
    oa::PackLayerSpec spec;
    spec.kind = oa::PackLayerKind::folder;
    spec.location = written.folder;
    spec.label = "isles";
    spec.files = map_fit::layer_files(written.entry, kPackId);
    std::string error;
    if (!game.store->mount_pack_layer(spec, &error)) {
        std::fprintf(stderr, "mount failed: %s\n", error.c_str());
        OA_CHECK(false);
        return;
    }
    const auto files = map_fit::mounted_map_files(*game.store);
    const auto mounted =
        map_fit::check_map(*game.store, game.names, *files, written.entry, kPackId, game.layout);
    if (!same_fit(folder_fit, mounted)) {
        std::fprintf(stderr, "folder and mounted results differ\nfolder:\n");
        print_failures(folder_fit);
        std::fprintf(stderr, "mounted:\n");
        print_failures(mounted);
    }
    OA_CHECK(same_fit(folder_fit, mounted));
    OA_CHECK(game.store->unmount_pack_layer());
}

/// The names the fixture game provides.
///
/// @param game the game
void check_game_names(const Game& game) {
    const auto section = [&](const char* name) {
        const auto found = game.names.feature_sections.find(std::string(name));
        if (found == game.names.feature_sections.end()) {
            std::fprintf(stderr, "missing feature section %s\n", name);
            return std::string{};
        }
        return found->second;
    };
    OA_CHECK(section("rock1") == "features/base.tdf");
    OA_CHECK(section("tree1") == "features/base.tdf");
    OA_CHECK(section("tree1dead") == "features/base.tdf");
    OA_CHECK(section("torch") == "features/base.tdf");
    OA_CHECK(game.names.weapons.contains(std::string("burnit")));
    OA_CHECK(game.names.units.contains(std::string("armcom")));
    if (game.names.unreadable.size() != 1)
        for (const auto& path : game.names.unreadable)
            std::fprintf(stderr, "unreadable %s\n", path.c_str());
    OA_CHECK(game.names.unreadable.size() == 1);
    if (!game.names.unreadable.empty())
        OA_CHECK(game.names.unreadable[0] == "features/bad.tdf");
}

/// The sentences a log prints.
void check_sentences() {
    OA_CHECK(map_fit::rule_name(map_fit::Rule::isolated) == "Isolated");
    OA_CHECK(map_fit::rule_name(map_fit::Rule::adds_only) == "Adds only");
    OA_CHECK(map_fit::rule_name(map_fit::Rule::new_names) == "New names");
    OA_CHECK(map_fit::rule_name(map_fit::Rule::complete) == "Complete");

    const map_fit::Failure isolated{
        map_fit::Rule::isolated, "other pack", "another map's files are mounted"
    };
    OA_CHECK(
        map_fit::describe(isolated) == "Isolated: another map's files are mounted ('other pack')"
    );
    const map_fit::Failure adds{map_fit::Rule::adds_only, "anims/rock.gaf", "totala1.hpi"};
    OA_CHECK(
        map_fit::describe(adds) == "Adds only: 'anims/rock.gaf' is already provided by totala1.hpi"
    );
    const map_fit::Failure names{
        map_fit::Rule::new_names, "dragons teeth", "features/corpses/walls.tdf"
    };
    OA_CHECK(
        map_fit::describe(names) ==
        "New names: the feature 'dragons teeth' is defined already by features/corpses/walls.tdf"
    );
    const map_fit::Failure complete{
        map_fit::Rule::complete,
        "objects3d/missing.3do",
        "is needed by the feature 'isle_rock' and is not in the map or the game"
    };
    OA_CHECK(
        map_fit::describe(complete) ==
        "Complete: 'objects3d/missing.3do' is needed by the feature 'isle_rock' and is not in "
        "the map or the game"
    );
}

/// The shown paths of the OTA, the TNT and the other files.
void check_layer_files() {
    map_pack::MapEntry map;
    map.stem = "isle";
    map.files = {"maps/isle.ota", "maps/isle.tnt", "features/isle/isle.tdf", "objects3d/isle.3do"};
    const auto files = map_fit::layer_files(map, kPackId);
    OA_CHECK(files.size() == 4);
    OA_CHECK(files[0].path == "maps/isle@isles.ota" && files[0].source == "maps/isle.ota");
    OA_CHECK(files[1].path == "maps/isle@isles.tnt" && files[1].source == "maps/isle.tnt");
    OA_CHECK(
        files[2].path == "features/isle/isle.tdf" && files[2].source == "features/isle/isle.tdf"
    );
    OA_CHECK(files[3].path == "objects3d/isle.3do" && files[3].source == "objects3d/isle.3do");

    map_pack::MapEntry mixed;
    mixed.stem = "isle";
    mixed.files = {"MAPS/isle.OTA", "anims/isle.gaf"};
    const auto remapped = map_fit::layer_files(mixed, kPackId);
    OA_CHECK(remapped.size() == 2);
    OA_CHECK(remapped[0].path == "maps/isle@isles.ota" && remapped[0].source == "MAPS/isle.OTA");
    OA_CHECK(remapped[1].path == "anims/isle.gaf" && remapped[1].source == "anims/isle.gaf");
}

/// What one section uses: a model wins over a filename, and the three links keep their order.
void check_references() {
    const char* text = "[isle_rock]\n"
                       "{\n"
                       "object=isle;\n"
                       "filename=ignored;\n"
                       "burnweapon=burnit;\n"
                       "featuredead=isle_rock_dead;\n"
                       "}\n"
                       "[isle_rock_dead]\n"
                       "{\n"
                       "filename=isle;\n"
                       "seqname=dead;\n"
                       "}\n"
                       "[linked]\n"
                       "{\n"
                       "featuredead=one;\n"
                       "featurereclamate=two;\n"
                       "featureburnt=three;\n"
                       "}\n";
    tdf::ParseError error{};
    tdf::OwnedDocument document;
    if (!document.parse(text, &error))
        std::fprintf(stderr, "section tdf: %s\n", tdf::describe(error).c_str());
    OA_CHECK(document.root() != nullptr);
    if (document.root() == nullptr)
        return;
    const auto* rock = tdf::find_child(document.root(), "isle_rock");
    const auto* dead = tdf::find_child(document.root(), "isle_rock_dead");
    const auto* linked = tdf::find_child(document.root(), "linked");
    OA_CHECK(rock != nullptr && dead != nullptr && linked != nullptr);
    if (rock == nullptr || dead == nullptr || linked == nullptr)
        return;
    const auto rock_uses = map_fit::references_of(*rock);
    OA_CHECK(rock_uses.model == "objects3d/isle.3do");
    OA_CHECK(rock_uses.gaf.empty());
    OA_CHECK(rock_uses.burn_weapon == "burnit");
    OA_CHECK(rock_uses.features.size() == 1 && rock_uses.features[0] == "isle_rock_dead");
    const auto dead_uses = map_fit::references_of(*dead);
    OA_CHECK(dead_uses.gaf == "anims/isle.gaf");
    OA_CHECK(dead_uses.model.empty());
    OA_CHECK(dead_uses.burn_weapon.empty());
    const auto links = map_fit::references_of(*linked);
    OA_CHECK(links.features.size() == 3);
    if (links.features.size() == 3) {
        OA_CHECK(links.features[0] == "one");
        OA_CHECK(links.features[1] == "two");
        OA_CHECK(links.features[2] == "three");
    }
}

/// The features and units the fitting map places, and a broken OTA that changes nothing.
///
/// @param layout the data layout
void check_uses(const oa::data::defs::DataLayout& layout) {
    const auto ota = bytes_of(fitting_ota("", ""));
    const auto tnt_bytes = tnt_named({"isle_rock", "tree1", "torch"});
    map_fit::MapUses uses;
    std::string error;
    OA_CHECK(map_fit::uses_of(ota, tnt_bytes, layout, uses, &error));
    OA_CHECK(error.empty());
    const char* features[] = {"isle_rock", "tree1", "torch", "rock1", "isle_rock", "loop"};
    const char* from[] = {
        "the TNT",
        "the TNT",
        "the TNT",
        "Schema 0's features",
        "Schema 0's features",
        "Schema 0's features",
    };
    OA_CHECK(uses.features.size() == 6);
    OA_CHECK(uses.features_from.size() == uses.features.size());
    if (uses.features.size() == 6 && uses.features_from.size() == 6) {
        for (std::size_t index = 0; index < 6; ++index) {
            OA_CHECK(uses.features[index] == features[index]);
            OA_CHECK(uses.features_from[index] == from[index]);
        }
    }
    OA_CHECK(uses.units.size() == 1 && uses.units_from.size() == 1);
    if (!uses.units.empty() && !uses.units_from.empty()) {
        OA_CHECK(uses.units[0] == "ARMCOM");
        OA_CHECK(uses.units_from[0] == "Schema 1's units");
    }

    map_fit::MapUses kept;
    kept.features.push_back("stay");
    kept.features_from.push_back("the TNT");
    kept.units.push_back("stay");
    kept.units_from.push_back("Schema 0's units");
    const auto bad = bytes_of("[");
    OA_CHECK(!map_fit::uses_of(bad, tnt_bytes, layout, kept, &error));
    OA_CHECK(error.starts_with("OTA: "));
    OA_CHECK(kept.features.size() == 1 && kept.features[0] == "stay");
    OA_CHECK(kept.units.size() == 1 && kept.units[0] == "stay");
    OA_CHECK(!map_fit::uses_of(bad, tnt_bytes, layout, kept, nullptr));
    OA_CHECK(kept.features.size() == 1 && kept.units.size() == 1);
}

/// A folder read answers the shown paths only.
///
/// @param game the game
/// @param written the fitting map
void check_folder_reads(const Game& game, const WrittenMap& written) {
    const auto files = map_fit::folder_map_files(written.folder, written.entry, kPackId);
    const auto shown = files->read("maps/isle@isles.ota");
    const auto folded = files->read("MAPS/ISLE@ISLES.OTA");
    OA_CHECK(shown.has_value() && !shown->empty());
    OA_CHECK(folded.has_value() && shown.has_value() && folded->size() == shown->size());
    OA_CHECK(!files->read("maps/isle.ota").has_value());
    OA_CHECK(!files->read("anims/not-listed.gaf").has_value());
    OA_CHECK(!files->from_layer());
    for (const auto& file : map_fit::layer_files(written.entry, kPackId)) {
        if (game.store->provided_above_pack_layer(file.path))
            std::fprintf(stderr, "the game already provides %s\n", file.path.c_str());
        OA_CHECK(!game.store->provided_above_pack_layer(file.path));
    }
}

/// Another map's layer fails Isolated. The folder check does not change.
///
/// @param game the game
/// @param fitting the fitting map
/// @param before the folder result from before the mount
void check_isolated(Game& game, const WrittenMap& fitting, const map_fit::Fit& before) {
    const auto other = game.scratch / "other";
    write_bytes(other / "maps" / "other.ota", bytes_of("other"));
    oa::PackLayerSpec spec;
    spec.kind = oa::PackLayerKind::folder;
    spec.location = other;
    spec.label = "other pack";
    spec.files = {{"maps/other.ota", "maps/other.ota"}};
    std::string error;
    if (!game.store->mount_pack_layer(spec, &error)) {
        std::fprintf(stderr, "mount failed: %s\n", error.c_str());
        OA_CHECK(false);
        return;
    }
    const auto folder_files = map_fit::folder_map_files(fitting.folder, fitting.entry, kPackId);
    const auto during = map_fit::check_map(
        *game.store, game.names, *folder_files, fitting.entry, kPackId, game.layout
    );
    if (!same_fit(before, during)) {
        std::fprintf(stderr, "the folder check changed while another map was mounted\n");
        print_failures(during);
    }
    OA_CHECK(same_fit(before, during));
    OA_CHECK(during.fits());
    for (const auto& failure : during.failures)
        OA_CHECK(failure.rule != map_fit::Rule::isolated);

    const auto mounted = map_fit::mounted_map_files(*game.store);
    const auto fit =
        map_fit::check_map(*game.store, game.names, *mounted, fitting.entry, kPackId, game.layout);
    bool saw = false;
    for (const auto& failure : fit.failures) {
        if (failure.rule == map_fit::Rule::isolated && failure.subject == "other pack" &&
            failure.detail == "another map's files are mounted")
            saw = true;
    }
    if (!saw) {
        std::fprintf(stderr, "expected an Isolated failure for the other pack\n");
        print_failures(fit);
    }
    OA_CHECK(saw);
    OA_CHECK(game.store->unmount_pack_layer());
}

/// The fixture game and every rule.
void fixture_cases() {
    Game game = make_game();
    check_game_names(game);
    check_sentences();
    check_layer_files();
    check_references();
    check_uses(game.layout);

    const auto fitting =
        isle_map(game.scratch, "fitting", fitting_ota("", ""), fitting_tdf(), {}, true);
    check_folder_reads(game, fitting);
    const auto fitted = check_folder(game, fitting, kPackId);
    expect_fits(fitted);
    expect_same_mounted(game, fitting, fitted);

    const auto adds = isle_map(
        game.scratch,
        "adds",
        fitting_ota("", ""),
        fitting_tdf(),
        {{"anims/rock.gaf", {9, 9}}},
        false
    );
    const auto added = check_folder(game, adds, kPackId);
    const auto provider = utf8_text(game.store->provider("anims/rock.gaf").path);
    expect_one(added, map_fit::Rule::adds_only, "anims/rock.gaf", provider);
    OA_CHECK(provider.find("totala1.hpi") != std::string::npos);
    expect_same_mounted(game, adds, added);

    const auto renamed = isle_map(
        game.scratch,
        "names",
        fitting_ota("", ""),
        fitting_tdf() + "[ROCK1]\n{\nobject=dup;\n}\n",
        {},
        false
    );
    const auto clash = check_folder(game, renamed, kPackId);
    expect_one(clash, map_fit::Rule::new_names, "ROCK1", "features/base.tdf");

    const auto missing = isle_map(
        game.scratch,
        "missing",
        fitting_ota("", ""),
        replaced(
            fitting_tdf(),
            "object=isle;\nfeaturedead=isle_rock_dead;",
            "object=missing;\nfeaturedead=isle_rock_dead;"
        ),
        {},
        false
    );
    expect_one(
        check_folder(game, missing, kPackId),
        map_fit::Rule::complete,
        "objects3d/missing.3do",
        "is needed by the feature 'isle_rock' and is not in the map or the game"
    );

    const auto ghost = isle_map(
        game.scratch,
        "ghost",
        fitting_ota("", ""),
        replaced(fitting_tdf(), "featuredead=isle_rock_dead;", "featuredead=ghost;"),
        {},
        false
    );
    expect_one(
        check_folder(game, ghost, kPackId),
        map_fit::Rule::complete,
        "ghost",
        "is needed by the feature 'isle_rock' and is defined nowhere"
    );

    const auto ghost2 = isle_map(
        game.scratch,
        "ghost2",
        fitting_ota("", ""),
        replaced(
            fitting_tdf(),
            "featuredead=isle_rock_dead;\n",
            "featuredead=isle_rock_dead;\nfeatureburnt=ghost2;\n"
        ),
        {},
        false
    );
    expect_one(
        check_folder(game, ghost2, kPackId),
        map_fit::Rule::complete,
        "ghost2",
        "is needed by the feature 'isle_rock' and is defined nowhere"
    );

    const auto nothing = isle_map(
        game.scratch,
        "nothing",
        fitting_ota("", ""),
        replaced(fitting_tdf(), "burnweapon=burnit;", "burnweapon=nothing;"),
        {},
        false
    );
    expect_one(
        check_folder(game, nothing, kPackId),
        map_fit::Rule::complete,
        "nothing",
        "is used as a burn weapon by the feature 'isle_rock' and is not in the game"
    );

    const auto cornot =
        isle_map(game.scratch, "cornot", fitting_ota("", "CORNOT"), fitting_tdf(), {}, false);
    expect_one(
        check_folder(game, cornot, kPackId),
        map_fit::Rule::complete,
        "CORNOT",
        "is placed in Schema 1's units and is not in the game"
    );

    const auto schema = isle_map(
        game.scratch,
        "schema",
        fitting_ota("only_schema", ""),
        fitting_tdf() + "[only_schema]\n{\nobject=gone;\n}\n",
        {},
        false
    );
    expect_one(
        check_folder(game, schema, kPackId),
        map_fit::Rule::complete,
        "objects3d/gone.3do",
        "is needed by the feature 'only_schema' and is not in the map or the game"
    );

    const auto broken = isle_map(
        game.scratch,
        "broken",
        fitting_ota("", ""),
        fitting_tdf(),
        {{"features/isle/broken.tdf", bytes_of(kBroken)}},
        false
    );
    const auto unread = check_folder(game, broken, kPackId);
    if (unread.failures.size() != 1)
        print_failures(unread);
    OA_CHECK(unread.failures.size() == 1);
    if (!unread.failures.empty()) {
        OA_CHECK(unread.failures[0].rule == map_fit::Rule::complete);
        OA_CHECK(unread.failures[0].subject == "features/isle/broken.tdf");
        OA_CHECK(unread.failures[0].detail.find("could not be read") != std::string::npos);
        OA_CHECK(unread.failures[0].rule != map_fit::Rule::new_names);
    }

    check_isolated(game, fitting, fitted);
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

/// Tells whether a stem is letters, digits, '_' , '-' or spaces.
///
/// @param stem the stem
/// @return true when every character is one of those
bool plain_stem(std::string_view stem) {
    if (stem.empty())
        return false;
    for (const unsigned char byte : stem) {
        const bool ok = (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
                        byte == '_' || byte == '-' || byte == ' ';
        if (!ok)
            return false;
    }
    return true;
}

/// The OTA that places DragonsTeeth, the marker and ARMCOM.
///
/// @return the OTA text
std::string installed_ota() {
    return "[GlobalHeader]\n"
           "{\n"
           "[Schema 0]\n"
           "{\n"
           "Type=Network 1;\n"
           "[features]\n"
           "{\n"
           "[feature0]\n"
           "{\n"
           "Featurename=DragonsTeeth;\n"
           "XPos=1;\n"
           "ZPos=1;\n"
           "}\n"
           "[feature1]\n"
           "{\n"
           "Featurename=zz fit marker;\n"
           "XPos=2;\n"
           "ZPos=2;\n"
           "}\n"
           "}\n"
           "}\n"
           "[Schema 1]\n"
           "{\n"
           "Type=Network 2;\n"
           "[units]\n"
           "{\n"
           "[unit0]\n"
           "{\n"
           "Unitname=ARMCOM;\n"
           "}\n"
           "}\n"
           "}\n"
           "}\n";
}

/// The marker's feature TDF, and DragonsTeeth when the map must clash.
///
/// @param stem the GAF stem the marker names
/// @param clash true to define dragons teeth as well
/// @return the TDF text
std::string installed_tdf(std::string_view stem, bool clash) {
    std::string text = "[zz fit marker]\n{\nfilename=" + std::string(stem) + ";\n}\n";
    if (clash)
        text += "[DragonsTeeth]\n{\nfilename=" + std::string(stem) + ";\n}\n";
    return text;
}

/// The installed game: DragonsTeeth is known, a new marker fits, defining it does not.
void installed_game() {
    auto store = oa::test::require_game_assets("a map checked against the installed game");
    const oa::data::defs::DataLayout layout;
    const auto names = map_fit::collect_game_names(store, layout);
    if (names.feature_sections.size() < 100 ||
        !names.feature_sections.contains(std::string("dragonsteeth"))) {
        std::fprintf(
            stderr,
            "feature sections %zu, unreadable %zu\n",
            names.feature_sections.size(),
            names.unreadable.size()
        );
    }
    OA_CHECK(names.feature_sections.size() >= 100);
    OA_CHECK(names.feature_sections.contains(std::string("dragonsteeth")));
    if (!names.units.contains(std::string("armcom")))
        std::fprintf(stderr, "units %zu\n", names.units.size());
    OA_CHECK(names.units.contains(std::string("armcom")));
    OA_CHECK(!names.feature_sections.contains(std::string("zz fit marker")));

    const auto gafs = store.list_effective("anims", ".gaf", false);
    std::string gaf_path;
    std::string stem;
    for (const auto& path : gafs) {
        const auto candidate = gaf_stem(path);
        if (!plain_stem(candidate))
            continue;
        gaf_path = path;
        stem = candidate;
        break;
    }
    if (gaf_path.empty()) {
        std::fprintf(stderr, "no plain gaf among %zu\n", gafs.size());
        for (std::size_t index = 0; index < gafs.size() && index < 8; ++index)
            std::fprintf(stderr, "  %s\n", gafs[index].c_str());
    }
    OA_CHECK(!gaf_path.empty());
    if (gaf_path.empty())
        return;
    OA_CHECK(store.provided_above_pack_layer(gaf_path));

    Scratch scratch;
    scratch.path = oa::test::make_scratch_directory("map-fit-data");
    const auto tnt_bytes = tnt_named({"DragonsTeeth", "zz fit marker"});
    const auto ota = installed_ota();
    constexpr std::string_view id = "fitcheck";
    const auto write_marker = [&](bool clash) {
        return write_tree(
            scratch.path / (clash ? "clash" : "fitting"),
            "zzfit",
            {
                {"maps/zzfit.ota", bytes_of(ota)},
                {"maps/zzfit.tnt", tnt_bytes},
                {"features/zz/marker.tdf", bytes_of(installed_tdf(stem, clash))},
            },
            {}
        );
    };
    const auto fitting = write_marker(false);
    for (const auto& file : map_fit::layer_files(fitting.entry, id)) {
        if (store.provided_above_pack_layer(file.path))
            std::fprintf(stderr, "the installed game already provides %s\n", file.path.c_str());
        OA_CHECK(!store.provided_above_pack_layer(file.path));
    }
    const auto files = map_fit::folder_map_files(fitting.folder, fitting.entry, id);
    const auto fitted = map_fit::check_map(store, names, *files, fitting.entry, id, layout);
    expect_fits(fitted);

    const auto clashing = write_marker(true);
    const auto clash_files = map_fit::folder_map_files(clashing.folder, clashing.entry, id);
    const auto clash = map_fit::check_map(store, names, *clash_files, clashing.entry, id, layout);
    const auto known = names.feature_sections.find(std::string("dragonsteeth"));
    OA_CHECK(known != names.feature_sections.end());
    if (known == names.feature_sections.end())
        return;
    OA_CHECK(!known->second.empty());
    expect_one(clash, map_fit::Rule::new_names, "DragonsTeeth", known->second);
    if (!clash.failures.empty()) {
        const auto sentence =
            "New names: the feature 'DragonsTeeth' is defined already by " + known->second;
        OA_CHECK(map_fit::describe(clash.failures[0]) == sentence);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv))
        installed_game();
    else
        fixture_cases();
    return oa::test::check_exit_status();
}
