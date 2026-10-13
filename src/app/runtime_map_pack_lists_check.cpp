// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-map-pack-lists: a map pack installed from the Library during the
// run joins the skirmish list and the battle room's map list the next time
// either is shown, including when the skirmish list was opened before the
// download. The pack holds two maps. One stem has a space, an apostrophe
// and parentheses, and the catalogue records it as fitting the base game.
// The other map's files also fit the base game, and the catalogue records
// that as false, so the lists leave it out. A local mod with no origin
// then lists both, because that mod's key is not in the table and the
// files fit, and leaves the second out once a mounted archive defines the
// feature only that map adds. Uninstalling the pack removes both maps.
// No game file is committed: the check serves the pack from its own server.

#include "map_pack_check_packs.hpp"
#include "map_packs.hpp"
#include "mod_install_state.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/app/content/service.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/package_install.hpp"
#include "oa/app/runtime.hpp"
#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/map_pack/manifest.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/player_registries.hpp"
#include "oa/data/registry/trust.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/url.hpp"
#include "oa/formats/zip.hpp"
#include "oa/netgame/http_fixture/server.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace oa::app {
namespace {

namespace fs = std::filesystem;
namespace pack = oa::data::map_pack;
namespace sha = oa::base::sha256;
namespace signing = oa::base::signing;
namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;
namespace zip = oa::formats::zip;
namespace fixture = oa::netgame::http_fixture;
namespace mp = oa::ui::frontend_multiplayer;

/// The check's registry.
constexpr std::string_view kRegistry = "oa-check-lists";
/// The key that signs its catalogue.
constexpr std::string_view kKeyId = "oa-check-lists-key";
/// The pack the catalogue offers.
constexpr std::string_view kPackId = "oa-check-ponds";
/// The pack's name.
constexpr std::string_view kPackName = "Check Ponds";
/// The map that fits the base game. The stem holds a space, an apostrophe
/// and parentheses.
constexpr std::string_view kFittingStem = "A Player's Pond (North)";
/// The title the pack's index gives that map. The scenario names the stem,
/// so a list that shows the scenario's name fails the check.
constexpr std::string_view kFittingTitle = "North Pond";
/// The map the catalogue records as not fitting the base game. Its files
/// fit until an archive defines the feature only this map adds.
constexpr std::string_view kOtherStem = "ridge_only";
/// That map's title.
constexpr std::string_view kOtherTitle = "Ridge Only";
/// The feature only the second map adds. It is new to the base game.
constexpr std::string_view kLocalFeature = "oa check pond local";
/// That feature's file in the pack, listed on the second map alone.
constexpr std::string_view kLocalFile = "features/oa-check/local.tdf";
/// The same feature in the archive mounted for the local mod, at another
/// path, so the map's own file is still one the game did not have.
constexpr std::string_view kMountedFeatureFile = "features/oa-check-game/pond.tdf";
/// The archive the check mounts so the second map's feature is no longer new.
constexpr std::string_view kLocalArchive = "oa-check-pond-local.hpi";
/// How long the check waits for the catalogue, and then for the install.
constexpr std::chrono::seconds kWait{20};

/// The server the check's catalogue and pack are served from. It lives for
/// the run.
fixture::Server& fixture_server() {
    static fixture::Server server;
    return server;
}

/// Throws when a step of the check fails.
///
/// @param ok the step passed
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("map pack lists check: " + std::string(what));
}

/// Writes a text file whole.
///
/// @param file the file
/// @param text its text
void write_text(const fs::path& file, std::string_view text) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(out), "could not write " + path_to_utf8(file));
}

/// Reads a file whole.
///
/// @param file the file
/// @return its bytes
std::vector<uint8_t> read_whole(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    require(static_cast<bool>(in), "could not read " + path_to_utf8(file));
    return {std::istreambuf_iterator<char>{in}, {}};
}

/// Returns a JSON string, with quotes and backslashes escaped.
///
/// @param text the text
/// @return the quoted text
std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char letter : text) {
        const auto byte = static_cast<unsigned char>(letter);
        if (letter == '"' || letter == '\\')
            out.push_back('\\');
        else if (byte < 0x20)
            continue;
        out.push_back(letter);
    }
    out.push_back('"');
    return out;
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

/// The feature file only the second map lists: a new section, drawn with a
/// model of the game.
///
/// @param model the stem of that model
/// @return the TDF's text
std::string local_feature_tdf(std::string_view model) {
    return "[" + std::string(kLocalFeature) +
           "]\n"
           "\t{\n"
           "\tworld=allworld;\n"
           "\tdescription=Pond;\n"
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
}

/// Adds the second map's own feature file and lists it in the manifest.
///
/// The shared feature file stays on both maps. This one is only on the
/// second, so a later archive that defines the same section leaves the
/// first map fitting.
///
/// @param folder the pack's folder
/// @param model the stem of a model of the game
void add_local_feature(const fs::path& folder, std::string_view model) {
    write_text(folder / std::string(kLocalFile), local_feature_tdf(model));
    const std::vector<uint8_t> bytes = read_whole(folder / std::string(pack::manifest_file));
    pack::Manifest manifest;
    std::vector<pack::Problem> problems;
    require(
        pack::read_manifest(bytes, pack::ManifestUse::package, manifest, problems),
        "the pack's manifest cannot be read"
    );
    require(manifest.maps.size() == 2, "the pack does not hold two maps");
    manifest.maps[1].files.push_back(std::string(kLocalFile));
    write_text(folder / std::string(pack::manifest_file), pack::write_manifest(manifest));
}

/// Writes the archive whose feature section the second map also defines.
///
/// @param file the archive
/// @param model the stem of a model of the game
void write_local_archive(const fs::path& file, std::string_view model) {
    const std::string text = local_feature_tdf(model);
    oa::HpiWriteFile entry;
    entry.path = std::string(kMountedFeatureFile);
    entry.bytes.assign(text.begin(), text.end());
    const std::array<oa::HpiWriteFile, 1> files{std::move(entry)};
    const std::vector<uint8_t> archive = oa::write_hpi(files);
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!archive.empty())
        out.write(
            reinterpret_cast<const char*>(archive.data()),
            static_cast<std::streamsize>(archive.size())
        );
    require(static_cast<bool>(out), "could not write " + path_to_utf8(file));
}

/// Stores one check pack as an archive of the files its manifest lists.
///
/// @param folder the pack's folder
/// @param[out] archive the archive
void zip_pack(const fs::path& folder, std::vector<uint8_t>& archive) {
    const std::vector<uint8_t> manifest_bytes =
        read_whole(folder / std::string(pack::manifest_file));
    pack::Manifest manifest;
    std::vector<pack::Problem> problems;
    require(
        pack::read_manifest(manifest_bytes, pack::ManifestUse::package, manifest, problems),
        "the pack's manifest cannot be read"
    );
    std::vector<std::string> listed_files;
    for (const pack::MapEntry& map : manifest.maps)
        for (const std::string& listed : map.files)
            if (std::find(listed_files.begin(), listed_files.end(), listed) == listed_files.end())
                listed_files.push_back(listed);
    std::vector<std::vector<uint8_t>> contents;
    contents.reserve(listed_files.size());
    for (const std::string& listed : listed_files)
        contents.push_back(read_whole(folder / path_from_utf8(listed)));
    std::vector<zip::NewEntry> entries{{pack::manifest_file, manifest_bytes}};
    for (std::size_t at = 0; at < listed_files.size(); ++at)
        entries.push_back({listed_files[at], contents[at]});
    zip::ZipError error{};
    require(zip::write_archive(entries, archive, error), "the pack's archive could not be made");
}

/// Tells whether the content snapshot lists the check's pack.
///
/// @param snapshot the snapshot; null lists nothing
/// @return true when the pack's release is listed
bool snapshot_lists_pack(const content::Snapshot* snapshot) {
    if (snapshot == nullptr)
        return false;
    for (const content::Entry& entry : snapshot->entries) {
        if (entry.package == nullptr)
            continue;
        if (entry.registry == kRegistry && entry.package->id == kPackId &&
            entry.package->release == 1)
            return true;
    }
    return false;
}

} // namespace

void Runtime::prepare_map_pack_lists_check() {
    require(
        options_.user_folder && !options_.user_folder->empty() && !user_folder().empty(),
        "the run has no folder of the player's own (--user-folder)"
    );
    require(options_.data_dir.has_value(), "the run has no --data-dir");
    const auto models = assets_.list_effective("objects3d", ".3do", false);
    require(!models.empty(), "the game has no model for the check's feature");
    const std::string model = path_to_utf8(path_from_utf8(models.front()).stem());

    const fs::path source = user_folder() / "oa-check-ponds-source";
    const MapPackCheckMap maps[] = {
        {kFittingStem, kFittingTitle, false},
        {kOtherStem, kOtherTitle, false},
    };
    write_map_pack_check_folder(source, kPackId, kPackName, maps, model);
    add_local_feature(source, model);
    write_local_archive(user_folder() / std::string(kLocalArchive), model);
    std::vector<uint8_t> archive;
    zip_pack(source, archive);
    const sha::Digest digest = sha::digest_of(archive);
    const auto hex_chars = sha::to_hex(digest);
    const std::string hex(hex_chars.begin(), hex_chars.end());

    std::string listen_error;
    require(fixture_server().start(&listen_error), "the server did not listen: " + listen_error);
    const std::string catalogue_url = fixture_server().url("/v1/catalogue.json");

    std::array<uint8_t, 32> seed{};
    seed.fill(0x23);
    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    signing::key_pair_from_seed(seed, secret, public_key);
    seed.fill(0);
    const std::string key_text(signing::public_key_text(public_key).view());

    std::string json = "{\"catalogue\":1,\"registry\":";
    json += json_string(kRegistry);
    json += ",\"sequence\":1,\"generated\":\"2026-10-01T00:00:00Z\","
            "\"expires\":\"2027-01-01T00:00:00Z\",\"keys\":[{\"id\":";
    json += json_string(kKeyId);
    json += ",\"public\":";
    json += json_string(key_text);
    json += "}],\"packages\":[{\"kind\":\"oamap\",\"id\":";
    json += json_string(kPackId);
    json += ",\"name\":";
    json += json_string(kPackName);
    json += ",\"release\":1,\"size\":";
    json += std::to_string(archive.size());
    json += ",\"sha256\":";
    json += json_string(hex);
    json += ",\"file\":";
    json += json_string("/v1/p/" + hex + ".oamap");
    json += ",\"maps\":[{\"map\":";
    json += json_string(std::string(kFittingStem) + "@" + std::string(kPackId));
    json += ",\"name\":";
    json += json_string(kFittingTitle);
    json += ",\"players\":2,\"size\":\"2x2\",\"compatible\":{\"ta-3.1c\":true}},{\"map\":";
    json += json_string(std::string(kOtherStem) + "@" + std::string(kPackId));
    json += ",\"name\":";
    json += json_string(kOtherTitle);
    json +=
        ",\"players\":2,\"size\":\"2x2\",\"compatible\":{\"ta-3.1c\":false,\"ridge@28\":true}}]}]}";

    const std::vector<uint8_t> catalogue_bytes(json.begin(), json.end());
    const signing::Signature signature = signing::sign(secret, catalogue_bytes);
    secret.fill(0);
    const std::string signature_text =
        catalogue::signature_file_text(catalogue::SignatureLine{std::string(kKeyId), signature});
    const std::vector<uint8_t> signature_bytes(signature_text.begin(), signature_text.end());
    fixture_server().serve_bytes("/v1/catalogue.json", catalogue_bytes, "application/json");
    fixture_server().serve_bytes("/v1/catalogue.json.sig", signature_bytes, "text/plain");
    fixture_server().serve_bytes(
        "/v1/p/" + hex + ".oamap", std::move(archive), "application/octet-stream"
    );

    registry::PlayerRegistries list;
    // SDL caches this string for the process. The caller does not free it.
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        const fs::path builtins =
            path_from_utf8(base) / path_from_utf8(content::builtin_registries_folder_name);
        std::error_code error;
        if (fs::is_directory(builtins, error))
            for (const registry::Descriptor& built_in :
                 registry::read_builtin_registries(builtins, nullptr))
                list.disabled.push_back(built_in.id);
    }
    const std::optional<oa::formats::url::Url> parsed =
        oa::formats::url::parse_http_url(catalogue_url);
    require(parsed.has_value(), "the catalogue's address did not read");
    registry::AddedRegistry added;
    added.descriptor.id = std::string(kRegistry);
    added.descriptor.name = "Check Lists";
    added.descriptor.catalogue = *parsed;
    added.descriptor.keys.push_back(registry::RegistryKey{std::string(kKeyId), public_key});
    added.descriptor.mode = registry::DownloadMode::direct;
    added.added = "2026-10-01";
    added.enabled = true;
    list.registries.push_back(std::move(added));
    write_text(
        user_folder() / std::string(registry::player_registries_file),
        registry::player_registries_text(list)
    );
    std::cout << "map pack lists check: registry " << kRegistry << " at " << catalogue_url << ", "
              << list.disabled.size() << " built-in registries turned off\n";
}

void Runtime::check_map_pack_lists() {
    const std::string fitting = pack::pack_map_name(kFittingStem, kPackId);
    const std::string other = pack::pack_map_name(kOtherStem, kPackId);
    require(pack::map_name_fits(fitting), "the spaced name does not fit a map's name");
    require(
        fitting.find(' ') != std::string::npos && fitting.find('\'') != std::string::npos &&
            fitting.find('(') != std::string::npos && fitting.find(')') != std::string::npos,
        "the check's stem lost a space, an apostrophe or a parenthesis"
    );

    // The lists exist, and were read, before the pack is installed.
    const std::vector<std::string> base_before = eligible_map_names_;
    for (const std::string& name : base_before)
        require(!pack::split_pack_map_name(name).has_value(), "a pack map is already listed");
    require(
        std::find(base_before.begin(), base_before.end(), fitting) == base_before.end(),
        "the spaced map is listed before it is installed"
    );
    std::vector<std::string> room_before;
    const int32_t room_before_count = mp::multiplayer_map_count();
    for (int32_t index = 0; index < room_before_count; ++index) {
        const char* name = mp::multiplayer_map_at(index);
        require(name != nullptr, "the battle room's list has a hole");
        room_before.emplace_back(name);
    }
    require(
        std::find(room_before.begin(), room_before.end(), fitting) == room_before.end() &&
            std::find(room_before.begin(), room_before.end(), other) == room_before.end(),
        "the battle room lists the pack before it is installed"
    );
    std::cout << "map pack lists check: before install, skirmish " << base_before.size()
              << " maps, battle room " << room_before.size() << " maps, absent " << fitting << '\n';

    // The skirmish list is opened before the download, then closed, and the
    // run returns to the main menu, which is where the install happens.
    const auto row_of = [this](const std::string& name) {
        return static_cast<std::size_t>(
            std::find(bound_map_names_.begin(), bound_map_names_.end(), name) -
            bound_map_names_.begin()
        );
    };
    const auto room_lists = [](const std::string& name) {
        const int32_t count = mp::multiplayer_map_count();
        for (int32_t place = 0; place < count; ++place) {
            const char* listed = mp::multiplayer_map_at(place);
            if (listed != nullptr && name == listed)
                return true;
        }
        return false;
    };
    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    require(screen_ == Screen::skirmish, "Skirmish did not open its setup");
    exercise_click(skirmish::resource_name(skirmish::Button::select_map));
    require(
        screen_ == Screen::map_selection && row_of(fitting) == bound_map_names_.size() &&
            row_of(other) == bound_map_names_.size(),
        "the skirmish list shows a pack map before it is installed"
    );
    std::cout << "map pack lists check: skirmish list shown before install, "
              << bound_map_names_.size() << " maps\n";
    close_map_modal();
    require(screen_ == Screen::skirmish, "the skirmish map list did not close");
    exercise_click(skirmish::resource_name(skirmish::Button::previous_menu));
    require(screen_ == Screen::single_player, "PrevMenu did not leave the skirmish setup");
    exercise_click(entry::resource_name(entry::Button::previous_menu));
    require(screen_ == Screen::main_menu, "PrevMenu did not return to the main menu");

    const auto deadline_passed = [](std::chrono::steady_clock::time_point deadline) {
        return std::chrono::steady_clock::now() >= deadline;
    };
    {
        bool running = true;
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        content_service().refresh(content::RefreshReason::check_now, kRegistry);
        while (true) {
            const std::shared_ptr<const content::Snapshot> snapshot = content_service().snapshot();
            const bool listed = snapshot_lists_pack(snapshot.get());
            if (listed)
                break;
            require(!deadline_passed(deadline), "the catalogue did not list the pack");
            run_frame(running);
            require(running, "a frame ended the run while the catalogue was fetched");
        }
    }
    std::cout << "map pack lists check: the catalogue lists " << kPackId << '\n';

    // A watched run installs a catalogue map pack on the main menu. The
    // flag is set before the download is pumped, which is that install.
    mod_install_state().check_shows_prompts = true;
    std::string why;
    const std::optional<uint64_t> queued =
        queue_download(kRegistry, kPackId, content::DownloadReason::install, &why);
    require(queued.has_value(), "the download was not queued: " + why);

    {
        bool running = true;
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        while (map_packs().find(fitting) == nullptr) {
            if (deadline_passed(deadline)) {
                std::string detail;
                for (const content::DownloadView& item : content_downloads().view()) {
                    detail += " item " + std::to_string(item.item);
                    detail += " state " + std::to_string(static_cast<int>(item.state));
                    if (item.failure != content::DownloadFailure::none) {
                        detail += " ";
                        detail += content::failure_text(item.failure);
                        detail += " ";
                        detail += item.detail;
                    }
                }
                const std::vector<std::string> problems = map_packs().problems();
                for (const std::string& problem : problems)
                    detail += " problem " + problem;
                require(false, "the pack was not installed:" + detail);
            }
            run_frame(running);
            require(running, "a frame ended the run while the pack was installed");
        }
    }
    require(map_packs().find(other) != nullptr, "the other map was not installed");
    std::cout << "map pack lists check: installed " << fitting << " and " << other << '\n';

    // A recorded true or false is final. An empty key, and a key the table
    // does not hold, are left to the files.
    const PackMap* fitting_map = map_packs().find(fitting);
    const PackMap* other_map = map_packs().find(other);
    require(fitting_map != nullptr && other_map != nullptr, "an installed map left the index");
    require(
        catalogue_map_fits(*fitting_map, "ta-3.1c") == CatalogueMapFit::fits,
        "the catalogue does not record the spaced map as fitting the base game"
    );
    require(
        catalogue_map_fits(*other_map, "ta-3.1c") == CatalogueMapFit::unfit,
        "the catalogue does not record the other map as not fitting the base game"
    );
    require(
        catalogue_map_fits(*other_map, "ridge@28") == CatalogueMapFit::fits,
        "the catalogue does not record the other map as fitting Ridge"
    );
    require(
        catalogue_map_fits(*fitting_map, "") == CatalogueMapFit::unchecked &&
            catalogue_map_fits(*other_map, "") == CatalogueMapFit::unchecked,
        "an empty key was treated as a recorded answer"
    );
    require(
        catalogue_map_fits(*other_map, "ridge@99") == CatalogueMapFit::unchecked &&
            catalogue_map_fits(*fitting_map, "ridge@99") == CatalogueMapFit::unchecked,
        "a key the table lacks was treated as a recorded answer"
    );

    require(
        eligible_map_names_.size() >= base_before.size() &&
            std::equal(base_before.begin(), base_before.end(), eligible_map_names_.begin()),
        "the base maps changed order or name"
    );
    const auto skirmish_has = [this](const std::string& name) {
        return std::find(eligible_map_names_.begin(), eligible_map_names_.end(), name) !=
               eligible_map_names_.end();
    };
    require(skirmish_has(fitting), "the skirmish list does not show the spaced map");
    require(skirmish_has(other), "the skirmish list hides the map that does not fit");
    require(pack_map_row_reason(fitting).empty(), "the spaced map is greyed for the base game");
    require(
        pack_map_row_reason(other) == "for ridge",
        "the other map is not greyed for ridge (" + pack_map_row_reason(other) + ")"
    );

    std::vector<std::string> room_after;
    const int32_t room_after_count = mp::multiplayer_map_count();
    for (int32_t index = 0; index < room_after_count; ++index) {
        const char* name = mp::multiplayer_map_at(index);
        require(name != nullptr, "the battle room's list has a hole after the install");
        room_after.emplace_back(name);
    }
    std::vector<std::string> room_kept;
    for (const std::string& name : room_after)
        if (std::find(room_before.begin(), room_before.end(), name) != room_before.end())
            room_kept.push_back(name);
    require(room_kept == room_before, "the battle room's earlier maps changed order or name");
    require(
        std::find(room_after.begin(), room_after.end(), fitting) != room_after.end(),
        "the battle room does not list the spaced map"
    );
    require(
        std::find(room_after.begin(), room_after.end(), other) != room_after.end(),
        "the battle room hides the map that does not fit"
    );
    const std::vector<std::string> other_row =
        mp::multiplayer_map_texts(std::vector<std::string>{other});
    require(
        other_row.size() == 1 && other_row.front().find("for ridge") != std::string::npos,
        "the battle room's greyed row is \"" +
            (other_row.empty() ? std::string() : other_row.front()) + "\""
    );
    const char* title_text = mp::multiplayer_pack_map_title(fitting.c_str());
    const std::string room_title = title_text != nullptr ? title_text : std::string();
    require(room_title == kFittingTitle, "the battle room's title is \"" + room_title + "\"");
    const std::vector<std::string> room_texts = mp::multiplayer_map_texts(room_after);
    require(room_texts.size() == room_after.size(), "the battle room's rows lost a title");
    const auto room_place = static_cast<std::size_t>(
        std::find(room_after.begin(), room_after.end(), fitting) - room_after.begin()
    );
    require(
        room_place < room_texts.size() && room_texts[room_place] == kFittingTitle,
        "the battle room's row shows \"" +
            (room_place < room_texts.size() ? room_texts[room_place] : std::string()) + "\""
    );
    if (!room_before.empty()) {
        const auto base_place = static_cast<std::size_t>(
            std::find(room_after.begin(), room_after.end(), room_before.front()) -
            room_after.begin()
        );
        require(
            base_place < room_texts.size() && room_texts[base_place] == room_before.front(),
            "a base map's battle-room row changed"
        );
    }
    std::cout << "map pack lists check: battle room lists " << fitting << ", title " << room_title
              << '\n';

    const std::vector<std::string> lines = pack_map_omission_lines();
    bool omitted = false;
    for (const std::string& line : lines) {
        if (line.find(kPackId) != std::string::npos &&
            line.find("1 map cannot be played with " + played_map_key()) != std::string::npos) {
            omitted = true;
            std::cout << "map pack lists check: " << line << '\n';
        }
    }
    require(omitted, "the map that cannot be played was not named with one log line");
    require(
        played_map_key().starts_with("totala") && played_map_key().find('@') == std::string::npos,
        "the base game's key is \"" + played_map_key() + "\""
    );
    std::cout << "map pack lists check: after install, skirmish " << eligible_map_names_.size()
              << " maps, battle room " << room_after.size() << " maps\n";

    // A folder with no origin record, played in front of the base game. The
    // catalogue has no row for that empty key, so the files decide. Both
    // maps' files fit until the archive that defines the second map's
    // feature is mounted.
    const fs::path local_mod = user_folder() / "local-ridge";
    std::error_code made;
    fs::create_directories(local_mod, made);
    require(!made, "could not make the local mod's folder");
    const std::vector<fs::path> saved_folders = options_.game_folders;
    if (options_.game_folders.empty())
        options_.game_folders.push_back(options_.game_dir);
    options_.game_folders.insert(options_.game_folders.begin(), local_mod);
    require(plays_mod(), "the local folder is not played as a mod");
    require(played_map_key().empty(), "the local mod has a catalogue key");
    drop_pack_map_fits();
    refresh_listed_pack_maps();
    require(
        skirmish_has(fitting) && skirmish_has(other),
        "a map whose files fit a mod the catalogue has no row for was left out"
    );
    require(
        pack_map_row_reason(fitting).empty() && pack_map_row_reason(other).empty(),
        "a map whose files fit the local mod is greyed"
    );
    require(
        room_lists(fitting) && room_lists(other),
        "the battle room left out a map whose files fit a mod the catalogue has no row for"
    );
    std::cout << "map pack lists check: local mod with no origin lists " << fitting << " and "
              << other << '\n';

    const fs::path local_archive = user_folder() / std::string(kLocalArchive);
    std::string mount_error;
    require(
        assets_.try_mount(local_archive, &mount_error),
        "the local feature was not mounted: " + mount_error
    );
    drop_pack_map_fits();
    refresh_listed_pack_maps();
    require(skirmish_has(fitting), "the spaced map no longer fits the local mod");
    require(skirmish_has(other), "the local mod hides the map whose feature it already defines");
    require(
        pack_map_row_reason(other).find("doesn't fit") != std::string::npos,
        "the local mod does not grey the map whose feature it defines (" +
            pack_map_row_reason(other) + ")"
    );
    require(room_lists(fitting), "the battle room dropped the spaced map for the local mod");
    require(room_lists(other), "the battle room hides the map whose feature the local mod defines");
    bool local_omitted = false;
    for (const std::string& line : pack_map_omission_lines()) {
        if (line.find(std::string(kPackId)) != std::string::npos &&
            line.find("1 map cannot be played with the mod being played") != std::string::npos) {
            local_omitted = true;
            std::cout << "map pack lists check: " << line << '\n';
        }
    }
    require(
        local_omitted, "the map that does not fit the local mod was not left out with one log line"
    );

    options_.game_folders = saved_folders;
    drop_pack_map_fits();
    refresh_listed_pack_maps();
    require(!plays_mod(), "the base game is still played as a mod");
    require(
        played_map_key().starts_with("totala") && played_map_key().find('@') == std::string::npos,
        "the base game's key is \"" + played_map_key() + "\""
    );
    require(
        skirmish_has(fitting) && skirmish_has(other) && pack_map_row_reason(other) == "for ridge",
        "the base game's lists changed"
    );
    require(
        room_lists(fitting) && room_lists(other), "the battle room's list changed for the base game"
    );

    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    require(screen_ == Screen::skirmish, "Skirmish did not open its setup");
    exercise_click(skirmish::resource_name(skirmish::Button::select_map));
    require(
        screen_ == Screen::map_selection && row_of(fitting) < bound_map_names_.size() &&
            row_of(other) < bound_map_names_.size(),
        "the skirmish map list does not show both pack maps"
    );
    preview_map_index(row_of(fitting));
    require(
        bound_map_labels_.size() == bound_map_names_.size(),
        "the skirmish rows have no title beside each name"
    );
    for (std::size_t index = 0; index < bound_map_names_.size(); ++index) {
        if (pack::split_pack_map_name(bound_map_names_[index]))
            continue;
        require(
            bound_map_labels_[index] == bound_map_names_[index],
            "a base map's row changed from \"" + bound_map_names_[index] + "\""
        );
    }
    require(
        bound_map_labels_[row_of(fitting)] == kFittingTitle &&
            bound_map_names_[row_of(fitting)] == fitting,
        "the skirmish row shows \"" + bound_map_labels_[row_of(fitting)] + "\""
    );
    require(
        bound_map_labels_[row_of(other)].find("for ridge") != std::string::npos &&
            row_of(other) < bound_map_grey_.size() && bound_map_grey_[row_of(other)] != 0 &&
            row_of(fitting) < bound_map_grey_.size() && bound_map_grey_[row_of(fitting)] == 0,
        "the greyed skirmish row shows \"" + bound_map_labels_[row_of(other)] + "\""
    );
    // The stock skirmish map screen has no name under the list. The title is
    // what that screen writes when its layout has one, and what the battle
    // room's map name shows. The row shows the same title. The name the
    // choice stores stays the joined name.
    const std::string display = map_display_name();
    require(
        display == kFittingTitle && display != kFittingStem,
        "the skirmish map's title is \"" + display + "\""
    );
    if (const auto* name_gadget = widget("MAPNAME"); name_gadget != nullptr) {
        const std::string shown = gadget_text(name_gadget);
        require(shown == kFittingTitle, "the skirmish map's name is \"" + shown + "\"");
    }
    std::cout << "map pack lists check: skirmish row " << fitting << ", title " << display << '\n';
    exercise_click(map_modal::resource_name(map_modal::Button::load));
    close_map_modal();
    require(
        screen_ == Screen::skirmish && skirmish_settings_.map_name == fitting,
        "the skirmish setup did not choose the spaced map"
    );

    state_.player_count = 2;
    require(map_player_capacity() >= 2, "the spaced map lacks two start positions");
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    require(screen_ == Screen::match && match_, "the skirmish did not start");
    for (int step = 0; step < 30 && match_ && match_->simulation().tick < 1; ++step)
        step_match_simulation();
    require(
        screen_ == Screen::match && match_ && match_->simulation().tick >= 1,
        "the skirmish did not play to the first frame"
    );
    std::cout << "map pack lists check: played to tick " << match_->simulation().tick << '\n';

    const fs::path save = user_folder() / "pond.sav";
    require(save_match_game(save, "North Pond", 1), "could not save: " + status_);
    oa::ui::frontend::LoadSummary summary{};
    require(read_save_summary(save, summary), "could not read the save");
    const std::string mission(summary.mission.data());
    require(mission == fitting, "the save names \"" + mission + "\"");
    std::cout << "map pack lists check: saved mission " << mission << '\n';
    leave_match();

    // The match screen is still showing. The lists are what the next visit
    // reads, so this does not click through a menu.
    namespace install = oa::app::package_install;
    const install::PackageKind* kind = install::find_kind("oamap");
    require(kind != nullptr && kind->read_installed != nullptr, "the map pack kind is missing");
    const PackMap* still = map_packs().find(fitting);
    require(still != nullptr, "the spaced map left the index before it was removed");
    const fs::path pack_folder = still->folder;
    const install::InstalledPackage expected = kind->read_installed(pack_folder);
    install::ChangeOptions change{};
    change.expected = expected;
    const install::UninstallResult removed = install::uninstall_package(
        *kind, pack_folder.parent_path(), path_to_utf8(pack_folder.filename()), change
    );
    require(removed.removed, "the pack was not removed: " + removed.detail);
    package_changed(*kind, pack_folder);
    require(
        !skirmish_has(fitting) && !skirmish_has(other), "uninstall left a map in the skirmish list"
    );
    require(
        !room_lists(fitting) && !room_lists(other), "uninstall left a map in the battle room's list"
    );
    require(map_packs().find(fitting) == nullptr, "the spaced map is still installed");
    std::cout << "map pack lists check: uninstalled " << kPackId << '\n';
    std::cout << "map pack lists check passed\n";
}

} // namespace oa::app
