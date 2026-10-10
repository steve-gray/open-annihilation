// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-map-packs: pack maps in the skirmish list, one mounted at a time.
// The check writes two map packs into the player's own Maps folder: one whose
// map fits the game, and one whose feature file defines a feature the game
// already has. It requires both listed after every base map, the first
// mounted alone while it is chosen and through a skirmish played on it, and
// unmounted for a base map and when the match ends, and the second refused,
// the map picker showing why and keeping the earlier map.

#include "map_packs.hpp"
#include "map_picture_state.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/package_install/oamap.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/ui/frontend_dialogs.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace oa::app {

namespace {

namespace pack = oa::data::map_pack;
namespace tnt = oa::formats::tnt;

/// Attribute cells along each side of the check's maps: 1024 pixels.
constexpr uint32_t kMapCells = 64;

/// Distinct tiles the check's maps are drawn with.
constexpr uint32_t kMapTiles = 4;

/// Pixels along each side of the check's minimaps.
constexpr uint32_t kMinimapSide = 64;

/// Height of every cell, above the sea level of 0.
constexpr uint8_t kGroundHeight = 40;

/// The cell, column then row, the map's one feature stands on.
constexpr uint32_t kFeatureColumn = 40;
constexpr uint32_t kFeatureRow = 20;

/// The attribute cells' feature value that places none.
constexpr uint16_t kNoFeature = 0xffffU;

/// The first palette entry of the generated tile and minimap pixels, and how
/// many entries they cycle through.
constexpr uint8_t kFirstShade = 64;
constexpr uint32_t kShades = 8;

/// The match ticks the skirmish on the pack map plays.
constexpr uint32_t kMatchTicks = 30;

/// One map pack of the check: its id, the folder it is written to, its map's
/// stem and title, and whether its feature file also defines a feature the
/// game already has.
struct CheckPack {
    std::string_view id;    ///< the pack's id, and its folder's name
    std::string_view name;  ///< the pack's name
    std::string_view stem;  ///< its map's stem
    std::string_view title; ///< its map's title
    bool clashes{};         ///< true to define the game's dragon's teeth again
};

constexpr CheckPack kIslesPack{
    "oa-check-isles", "Map Pack Check Isles", "check_isle", "Check Isle"
};
constexpr CheckPack kClashPack{
    "oa-check-clash", "Map Pack Check Clash", "check_clash", "Check Clash", true
};

/// The feature the check's maps place, defined in their own feature file.
constexpr std::string_view kRockFeature = "oa check rock";

/// A feature section the base game defines, in other letters' case: a map
/// that defines it again breaks the New names rule.
constexpr std::string_view kClashingFeature = "dragonsteeth";

/// The feature file of each check map, as the pack holds and shows it.
constexpr std::string_view kFeatureFile = "features/oa-check/isle.tdf";

/// Throws when a step of the check fails.
///
/// @param ok the step passed
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("map pack check: " + std::string(what));
}

/// Tells whether a text holds another, ignoring ASCII case.
///
/// @param text the text searched
/// @param part the text sought
/// @return true when `part` occurs in `text`
bool holds_without_case(std::string_view text, std::string_view part) {
    const auto found =
        std::search(text.begin(), text.end(), part.begin(), part.end(), [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                   std::tolower(static_cast<unsigned char>(right));
        });
    return found != text.end();
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

/// Builds a check map's terrain: kMapCells by kMapCells cells of level
/// ground drawn with kMapTiles generated tiles, a minimap, and one feature
/// record naming kRockFeature, placed on one cell.
///
/// @return the TNT file's bytes
std::vector<uint8_t> terrain_bytes() {
    const std::size_t tile_columns = kMapCells / tnt::layout::attribute_cells_per_tile_edge;
    const std::size_t tile_map_at = tnt::layout::header_bytes;
    const std::size_t attributes_at = tile_map_at + tile_columns * tile_columns * sizeof(uint16_t);
    const std::size_t tiles_at =
        attributes_at + std::size_t{kMapCells} * kMapCells * tnt::layout::current_attribute_bytes;
    const std::size_t features_at = tiles_at + kMapTiles * tnt::layout::tile_bytes;
    const std::size_t minimap_at = features_at + tnt::layout::feature_record_bytes;
    const std::size_t end =
        minimap_at + tnt::layout::minimap_header_bytes + std::size_t{kMinimapSide} * kMinimapSide;
    std::vector<uint8_t> bytes(end);
    put32(
        bytes,
        offsetof(tnt::Header, id_version),
        static_cast<uint32_t>(tnt::Version::total_annihilation)
    );
    put32(bytes, offsetof(tnt::Header, width), kMapCells);
    put32(bytes, offsetof(tnt::Header, height), kMapCells);
    put32(bytes, offsetof(tnt::Header, tile_map_offset), static_cast<uint32_t>(tile_map_at));
    put32(bytes, offsetof(tnt::Header, attribute_offset), static_cast<uint32_t>(attributes_at));
    put32(bytes, offsetof(tnt::Header, tile_pixels_offset), static_cast<uint32_t>(tiles_at));
    put32(bytes, offsetof(tnt::Header, tile_count), kMapTiles);
    put32(bytes, offsetof(tnt::Header, feature_count), 1);
    put32(bytes, offsetof(tnt::Header, feature_offset), static_cast<uint32_t>(features_at));
    put32(bytes, offsetof(tnt::Header, sea_level), 0);
    put32(bytes, offsetof(tnt::Header, minimap_offset), static_cast<uint32_t>(minimap_at));
    put32(bytes, offsetof(tnt::Header, minimap_presence_flags), tnt::layout::minimap_present_flag);
    for (std::size_t row = 0; row < tile_columns; ++row)
        for (std::size_t column = 0; column < tile_columns; ++column)
            put16(
                bytes,
                tile_map_at + (row * tile_columns + column) * sizeof(uint16_t),
                static_cast<uint16_t>((row + column) % kMapTiles)
            );
    for (uint32_t row = 0; row < kMapCells; ++row)
        for (uint32_t column = 0; column < kMapCells; ++column) {
            const std::size_t at = attributes_at + (std::size_t{row} * kMapCells + column) *
                                                       tnt::layout::current_attribute_bytes;
            bytes[at + offsetof(tnt::TileAttr, height)] = kGroundHeight;
            const bool rock = row == kFeatureRow && column == kFeatureColumn;
            put16(bytes, at + offsetof(tnt::TileAttr, feature), rock ? uint16_t{0} : kNoFeature);
        }
    for (uint32_t tile = 0; tile < kMapTiles; ++tile)
        for (std::size_t y = 0; y < tnt::layout::tile_edge_pixels; ++y)
            for (std::size_t x = 0; x < tnt::layout::tile_edge_pixels; ++x)
                bytes
                    [tiles_at + tile * tnt::layout::tile_bytes + y * tnt::layout::tile_edge_pixels +
                     x] = static_cast<uint8_t>(kFirstShade + (x / 4 + y / 4 + tile) % kShades);
    const std::size_t name_at = features_at + tnt::layout::feature_name_offset;
    std::copy(kRockFeature.begin(), kRockFeature.end(), bytes.begin() + name_at);
    put32(bytes, minimap_at + offsetof(tnt::MinimapHeader, width), kMinimapSide);
    put32(bytes, minimap_at + offsetof(tnt::MinimapHeader, height), kMinimapSide);
    for (std::size_t y = 0; y < kMinimapSide; ++y)
        for (std::size_t x = 0; x < kMinimapSide; ++x)
            bytes[minimap_at + tnt::layout::minimap_header_bytes + y * kMinimapSide + x] =
                static_cast<uint8_t>(kFirstShade + ((x + y) / 8) % kShades);
    return bytes;
}

/// Writes a check map's OTA: one Network 1 schema with two start positions.
///
/// @param title the map's title
/// @return the OTA's text
std::string scenario_text(std::string_view title) {
    return "[GlobalHeader]\n"
           "\t{\n"
           "\tmissionname=" +
           std::string(title) +
           ";\n"
           "\tmissiondescription=A small island the map pack check plays on.;\n"
           "\tplanet=Green Planet;\n"
           "\tmissionhint=;\n"
           "\tbrief=;\n"
           "\tnarration=;\n"
           "\tglamour=;\n"
           "\tlineofsight=0;\n"
           "\tmapping=0;\n"
           "\ttidalstrength=0;\n"
           "\tsolarstrength=20;\n"
           "\tlavaworld=0;\n"
           "\tkillmul=50;\n"
           "\ttimemul=0;\n"
           "\tminwindspeed=0;\n"
           "\tmaxwindspeed=2000;\n"
           "\tgravity=112;\n"
           "\tnumplayers=2;\n"
           "\tsize=2 x 2;\n"
           "\tmemory=16 mb;\n"
           "\tSCHEMACOUNT=1;\n"
           "\t[Schema 0]\n"
           "\t\t{\n"
           "\t\tType=Network 1;\n"
           "\t\taiprofile=;\n"
           "\t\tSurfaceMetal=3;\n"
           "\t\tMohoMetal=40;\n"
           "\t\tHumanMetal=1000;\n"
           "\t\tComputerMetal=1000;\n"
           "\t\tHumanEnergy=1000;\n"
           "\t\tComputerEnergy=1000;\n"
           "\t\tMeteorWeapon=;\n"
           "\t\tMeteorRadius=0;\n"
           "\t\tMeteorDensity=0;\n"
           "\t\tMeteorDuration=0;\n"
           "\t\tMeteorInterval=0;\n"
           "\t\t[specials]\n"
           "\t\t\t{\n"
           "\t\t\t[special0]\n"
           "\t\t\t\t{\n"
           "\t\t\t\tspecialwhat=StartPos1;\n"
           "\t\t\t\tXPos=256;\n"
           "\t\t\t\tZPos=256;\n"
           "\t\t\t\t}\n"
           "\t\t\t[special1]\n"
           "\t\t\t\t{\n"
           "\t\t\t\tspecialwhat=StartPos2;\n"
           "\t\t\t\tXPos=768;\n"
           "\t\t\t\tZPos=768;\n"
           "\t\t\t\t}\n"
           "\t\t\t}\n"
           "\t\t}\n"
           "\t}\n";
}

/// Writes a check map's feature file: kRockFeature, a model of the game,
/// and for the clashing pack the game's own dragon's teeth again.
///
/// @param model the stem of the game's model the rock is drawn with
/// @param clashes true to define kClashingFeature too
/// @return the TDF's text
std::string feature_text(std::string_view model, bool clashes) {
    std::string text = "[" + std::string(kRockFeature) +
                       "]\n"
                       "\t{\n"
                       "\tworld=allworld;\n"
                       "\tdescription=Rock;\n"
                       "\tcategory=rocks;\n"
                       "\tobject=" +
                       std::string(model) +
                       ";\n"
                       "\tfootprintx=1;\n"
                       "\tfootprintz=1;\n"
                       "\theight=16;\n"
                       "\tblocking=1;\n"
                       "\thitdensity=100;\n"
                       "\treclaimable=0;\n"
                       "\tdamage=1000;\n"
                       "\t}\n";
    if (clashes)
        text += "[" + std::string(kClashingFeature) +
                "]\n"
                "\t{\n"
                "\tworld=allworld;\n"
                "\tdescription=Teeth;\n"
                "\tcategory=dragonteeth;\n"
                "\tobject=" +
                std::string(model) +
                ";\n"
                "\tfootprintx=1;\n"
                "\tfootprintz=1;\n"
                "\t}\n";
    return text;
}

/// Writes a file whole.
///
/// @param file the file
/// @param bytes its bytes
void write_file(const fs::path& file, std::string_view bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(out), "could not write " + path_to_utf8(file));
}

/// Writes one check pack into a Maps folder, in place of any earlier copy.
///
/// @param maps the Maps folder
/// @param check the pack
/// @param model the stem of the game's model its rock is drawn with
void write_pack(const fs::path& maps, const CheckPack& check, std::string_view model) {
    const fs::path folder = maps / std::string(check.id);
    std::error_code removed;
    fs::remove_all(folder, removed);
    require(!removed, "could not remove an earlier " + std::string(check.id));
    const std::string stem(check.stem);
    const std::string ota = "maps/" + stem + ".ota";
    const std::string terrain = "maps/" + stem + ".tnt";
    write_file(folder / ota, scenario_text(check.title));
    const std::vector<uint8_t> tnt_bytes = terrain_bytes();
    write_file(
        folder / terrain,
        std::string_view(reinterpret_cast<const char*>(tnt_bytes.data()), tnt_bytes.size())
    );
    write_file(folder / std::string(kFeatureFile), feature_text(model, check.clashes));

    pack::Manifest manifest{};
    manifest.format = pack::format_version;
    manifest.id = std::string(check.id);
    manifest.name = std::string(check.name);
    manifest.version = "1.0";
    manifest.description = "A map the map pack check writes.";
    manifest.author.name = "unknown";
    manifest.packaging.revision = 1;
    manifest.packaging.date = "2026-10-11";
    manifest.packaging.packager = "Open Annihilation";
    manifest.requires_base = "ta-3.1c";
    pack::MapEntry map{};
    map.stem = stem;
    map.title = std::string(check.title);
    map.description = "A small island the map pack check plays on.";
    map.size = "2x2";
    map.players = 2;
    map.files = {ota, terrain, std::string(kFeatureFile)};
    manifest.maps.push_back(map);
    write_file(folder / std::string(pack::manifest_file), pack::write_manifest(manifest));
}

/// Returns the text of a label or a button.
///
/// @param gadget the gadget, or null
/// @return its text; empty for another gadget or none
std::string gadget_text(const oa::ui::gui_layout::Gadget* gadget) {
    if (gadget == nullptr)
        return {};
    if (const auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget->fields))
        return label->text;
    if (const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget->fields))
        return button->text;
    return {};
}

} // namespace

void Runtime::check_map_packs() {
    namespace dialogs = oa::ui::frontend_dialogs;
    // The check writes into the Maps folder of a player's folder named for
    // this run alone, never into the player's own Documents.
    require(
        options_.user_folder && !options_.user_folder->empty() && !user_folder_.empty(),
        "the run has no folder of the player's own (--user-folder)"
    );
    const fs::path maps = user_folder_ / std::string(package_install::oamap::maps_folder_name);
    // The rock is drawn with a model of the game: a map may use the game's.
    const auto models = assets_.list_effective("objects3d", ".3do", false);
    require(!models.empty(), "the game has no model for the check's feature");
    const fs::path first_model = path_from_utf8(models.front());
    const std::string model = path_to_utf8(first_model.stem());
    write_pack(maps, kIslesPack, model);
    write_pack(maps, kClashPack, model);
    map_packs().refresh();
    const std::string isle = pack::pack_map_name(kIslesPack.stem, kIslesPack.id);
    const std::string clash = pack::pack_map_name(kClashPack.stem, kClashPack.id);
    require(
        map_packs().find(isle) != nullptr && map_packs().find(clash) != nullptr,
        "the packs written are not installed"
    );

    // Both maps are listed after every base map, and the first map is a base map.
    eligible_map_names_.clear();
    first_map_name_.clear();
    discover_first_map();
    std::size_t base_maps = 0;
    while (base_maps < eligible_map_names_.size() &&
           pack_map(eligible_map_names_[base_maps]) == nullptr)
        ++base_maps;
    const auto listed_at = [this](const std::string& name) {
        return static_cast<std::size_t>(
            std::find(eligible_map_names_.begin(), eligible_map_names_.end(), name) -
            eligible_map_names_.begin()
        );
    };
    require(
        base_maps > 0 && first_map_name_ == eligible_map_names_.front() &&
            listed_at(isle) >= base_maps && listed_at(isle) < eligible_map_names_.size() &&
            listed_at(clash) >= base_maps && listed_at(clash) < eligible_map_names_.size(),
        "the pack maps are not listed after every base map"
    );
    require(!assets_.pack_layer_mounted(), "a pack map is mounted before one is chosen");
    std::cout << "map pack check: " << base_maps << " base maps, then "
              << eligible_map_names_.size() - base_maps << " pack maps\n";

    // Choosing the pack map mounts its files alone; a base map unmounts them.
    const std::vector<fs::path> mounts_before(
        assets_.mount_paths().begin(), assets_.mount_paths().end()
    );
    const std::string terrain = "maps/" + isle + ".tnt";
    const auto isle_mounted = [this] {
        const auto label = assets_.pack_layer_label();
        return assets_.pack_layer_mounted() && label &&
               label->starts_with(std::string(kIslesPack.id) + " ");
    };
    require(!read(terrain), "the pack map's terrain is readable before it is chosen");
    require(select_map(isle) == 1, "the pack map that fits was not selected");
    require(isle_mounted() && read(terrain).has_value(), "the chosen pack map is not mounted");
    require(
        select_map(first_map_name_) == 1 && !assets_.pack_layer_mounted() && !read(terrain),
        "choosing a base map did not unmount the pack map"
    );

    // The pack map chosen in the picker: highlighted, it is mounted and
    // previewed; LOAD makes it the skirmish's map.
    // SELMAP.GUI may have no title gadget; one it has names the map.
    const auto shows_title = [this](std::string_view title) {
        const auto* gadget = widget("MAPNAME");
        return gadget == nullptr || gadget_text(gadget) == title;
    };
    const auto row_of = [this](const std::string& name) {
        return static_cast<std::size_t>(
            std::find(bound_map_names_.begin(), bound_map_names_.end(), name) -
            bound_map_names_.begin()
        );
    };
    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    require(screen_ == Screen::skirmish, "Skirmish did not open its setup");
    exercise_click(skirmish::resource_name(skirmish::Button::select_map));
    require(
        screen_ == Screen::map_selection && row_of(isle) < bound_map_names_.size(),
        "the map picker does not list the pack map"
    );

    // Highlighting a base map reads its terrain once: the match parses it,
    // and the preview takes the minimap from that parse.
    struct TntLookups {
        std::string path;
        int count = 0;
    } watched{"maps/" + first_map_name_ + ".tnt", 0};

    assets_.observe_lookups({&watched, [](void* context, std::string_view path) {
                                 auto* lookups = static_cast<TntLookups*>(context);
                                 if (path == lookups->path)
                                     ++lookups->count;
                             }});
    preview_map_index(row_of(first_map_name_));
    assets_.observe_lookups({});
    require(
        watched.count == 1 && row_of(first_map_name_) < bound_map_names_.size() && map_picture_ &&
            !map_picture_->rgb.empty(),
        "highlighting a base map does not read its terrain once"
    );
    preview_map_index(row_of(isle));
    require(
        isle_mounted() && shows_title(kIslesPack.title) && map_picture_ &&
            !map_picture_->rgb.empty() &&
            gadget_text(widget("DESCRIPTION")) == "A small island the map pack check plays on.",
        "the map picker does not preview the pack map"
    );
    exercise_click(map_modal::resource_name(map_modal::Button::load));
    close_map_modal();
    require(
        screen_ == Screen::skirmish && skirmish_settings_.map_name == isle && isle_mounted(),
        "the skirmish setup did not choose the pack map"
    );

    // A skirmish on the pack map reads its files; the match's end unmounts them.
    state_.player_count = 2;
    require(map_player_capacity() >= 2, "the pack map lacks two start positions");
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    require(screen_ == Screen::match && match_, "the skirmish on the pack map did not start");
    for (uint32_t tick = 0; tick < kMatchTicks; ++tick)
        step_match_simulation();
    require(
        screen_ == Screen::match && match_ && match_->simulation().tick >= kMatchTicks,
        "the skirmish on the pack map did not play its ticks"
    );
    require(
        isle_mounted() && read(terrain).has_value(),
        "the pack map's files are not readable during its match"
    );
    leave_match();
    const std::vector<fs::path> mounts_after(
        assets_.mount_paths().begin(), assets_.mount_paths().end()
    );
    require(
        !assets_.pack_layer_mounted() && !read(terrain) && mounts_after == mounts_before,
        "the match's end did not unmount the pack map, or changed the archives"
    );
    std::cout << "map pack check: a skirmish on " << isle << " played " << kMatchTicks
              << " ticks\n";

    // The map whose feature the game already defines is refused, and says why.
    require(select_map(clash) == 0, "the clashing pack map was selected");
    const auto refusal = pack_map_refusal(clash);
    require(
        refusal && refusal->find("New names") != std::string::npos &&
            holds_without_case(*refusal, kClashingFeature),
        "the clashing pack map was not refused for its feature's name"
    );
    require(!assets_.pack_layer_mounted(), "the clashing pack map is mounted");

    // The picker shows the refused map's reason, and LOAD keeps the earlier map.
    load(Screen::main_menu);
    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    const std::string earlier = skirmish_settings_.map_name;
    require(earlier == isle && isle_mounted(), "the skirmish setup lost the pack map");
    exercise_click(skirmish::resource_name(skirmish::Button::select_map));
    require(screen_ == Screen::map_selection, "the map picker did not open");
    const auto row = row_of(clash);
    require(row < bound_map_names_.size(), "the map picker does not list the clashing map");
    const auto first_pack_row = static_cast<std::size_t>(
        std::find_if(
            bound_map_names_.begin(),
            bound_map_names_.end(),
            [](const std::string& name) { return pack::split_pack_map_name(name).has_value(); }
        ) -
        bound_map_names_.begin()
    );
    require(
        first_pack_row == base_maps &&
            std::all_of(
                bound_map_names_.begin() + static_cast<std::ptrdiff_t>(first_pack_row),
                bound_map_names_.end(),
                [](const std::string& name) { return pack::split_pack_map_name(name).has_value(); }
            ),
        "the map picker does not list the pack maps after every base map"
    );
    preview_map_index(row);
    const std::string description = gadget_text(widget("DESCRIPTION"));
    require(
        description.starts_with("Doesn't fit ") && description.find(*refusal) != std::string::npos,
        "the map picker does not say why the clashing map does not fit"
    );
    require(
        shows_title(kClashPack.title) && (!map_picture_ || map_picture_->rgb.empty()),
        "the map picker does not show the refused map's title without a picture"
    );
    std::cout << "map pack check: the picker says \"" << description << "\"\n";
    exercise_click(map_modal::resource_name(map_modal::Button::load));
    close_map_modal();
    require(
        screen_ == Screen::skirmish && skirmish_settings_.map_name == earlier && isle_mounted(),
        "LOAD on the refused map did not keep the earlier map"
    );
    std::string message;
    if (const auto* shown = dialogs::dialog_resources(); shown != nullptr)
        for (const auto& gadget : shown->layout.gadgets)
            message += gadget_text(&gadget) + " ";
    require(
        dialogs::dialog_kind() == dialogs::DialogKind::message_box &&
            message.find("New names") != std::string::npos,
        "LOAD on the refused map did not say why"
    );
    auto context = screen_context();
    require(
        dialogs::dialog_click(&context, "OK") && dialogs::dialog_count() == 0,
        "OK did not close the refusal"
    );
    load(Screen::main_menu);
    require(!assets_.pack_layer_mounted(), "the main menu shows a pack map's files");
    std::cout << "map pack check passed\n";
}

} // namespace oa::app
