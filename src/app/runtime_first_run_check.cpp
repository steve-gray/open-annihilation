// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-first-run: walks the welcome setup over the main menu. It switches
// to Deutsch, downloads a language pack from the check's own registry
// (missing, then Retry), skips, and applies both online choices. The pack
// cannot be signed as Core Prime, so the registry is the made-up one Ridge
// and the setup reads it only while this check runs. No game file is
// committed: the check serves the pack from its own server.

#include "oa_layer.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/app/content/service.hpp"
#include "oa/app/content/settings.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/runtime.hpp"
#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/player_registries.hpp"
#include "oa/data/registry/trust.hpp"
#include "oa/formats/url.hpp"
#include "oa/formats/zip.hpp"
#include "oa/netgame/http_fixture/server.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/first_run/first_run.hpp"
#include "oa/ui/kit/layout.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

namespace fs = std::filesystem;
namespace sha = oa::base::sha256;
namespace signing = oa::base::signing;
namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;
namespace zip = oa::formats::zip;
namespace fixture = oa::netgame::http_fixture;
namespace first_run = oa::ui::first_run;
namespace settings = oa::ui::engine_settings;
namespace kit = oa::ui::kit;

/// The check's registry. The setup accepts it only while this check runs.
constexpr std::string_view kRegistry = "ridge";
/// The key that signs its catalogue.
constexpr std::string_view kKeyId = "ridge-check-key";
/// The language pack the catalogue offers.
constexpr std::string_view kTag = "en-XA";
/// How long the check waits for the catalogue, a download or the setup.
constexpr std::chrono::seconds kWait{30};
/// The setup's name on the OA layer.
constexpr std::string_view kScreen = "first-run";

/// The server the check's catalogue and pack are served from. It lives for
/// the run.
fixture::Server& fixture_server() {
    static fixture::Server server;
    return server;
}

/// The pack, served only after the first download has failed.
struct HeldPack {
    std::string path{};
    std::vector<uint8_t> bytes{};
};

/// The pack prepare built. The check serves it on Retry.
HeldPack& held_pack() {
    static HeldPack pack;
    return pack;
}

/// Throws when a step of the check fails.
///
/// @param ok the step passed
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("first-run check: " + std::string(what));
}

/// Writes a text file whole.
///
/// @param file the file
/// @param text its text
void write_text(const fs::path& file, std::string_view text) {
    std::error_code io;
    fs::create_directories(file.parent_path(), io);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(out), "could not write " + path_to_utf8(file));
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

/// Tells whether the wait has run out.
///
/// @param deadline when the wait ends
/// @return true when it has
bool deadline_passed(std::chrono::steady_clock::time_point deadline) {
    return std::chrono::steady_clock::now() >= deadline;
}

} // namespace

void Runtime::prepare_first_run_check() {
    require(
        options_.user_folder && !options_.user_folder->empty() && !user_folder().empty(),
        "the run has no folder of the player's own (--user-folder)"
    );
    require(options_.data_dir.has_value(), "the run has no --data-dir");

    const std::string yaml = "oalang: 1\n"
                             "tag: en-XA\n"
                             "name: Pseudo\n"
                             "english-name: Pseudo\n"
                             "word: Pseudo\n"
                             "version: \"1\"\n"
                             "locales: [en-XA]\n"
                             "fallbacks: []\n"
                             "text: {needs: game-fonts}\n"
                             "requires: {engine: \">= 0.0.1\"}\n";
    const std::vector<uint8_t> yaml_bytes(yaml.begin(), yaml.end());
    const zip::NewEntry entries[] = {{"language.yaml", yaml_bytes}};
    std::vector<uint8_t> archive;
    zip::ZipError zip_error{};
    require(zip::write_archive(entries, archive, zip_error), "the language pack could not be made");
    const sha::Digest digest = sha::digest_of(archive);
    const auto hex_chars = sha::to_hex(digest);
    const std::string hex(hex_chars.begin(), hex_chars.end());
    held_pack().path = "/v1/p/" + hex + ".oalang";
    held_pack().bytes = std::move(archive);

    std::string listen_error;
    require(fixture_server().start(&listen_error), "the server did not listen: " + listen_error);
    const std::string catalogue_url = fixture_server().url("/v1/catalogue.json");

    std::array<uint8_t, 32> seed{};
    seed.fill(0x39);
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
    json += "}],\"packages\":[{\"kind\":\"oalang\",\"id\":";
    json += json_string(kTag);
    json += ",\"name\":\"Pseudo\",\"english_name\":\"Pseudo\",\"word\":\"Pseudo\","
            "\"release\":1,\"size\":";
    json += std::to_string(held_pack().bytes.size());
    json += ",\"sha256\":";
    json += json_string(hex);
    json += ",\"file\":";
    json += json_string(held_pack().path);
    json += ",\"locales\":[\"en-XA\"],\"fallbacks\":[],\"needs\":\"game-fonts\","
            "\"requires\":{\"engine\":\">= 0.0.1\"}}]}";

    const std::vector<uint8_t> catalogue_bytes(json.begin(), json.end());
    const signing::Signature signature = signing::sign(secret, catalogue_bytes);
    secret.fill(0);
    const std::string signature_text =
        catalogue::signature_file_text(catalogue::SignatureLine{std::string(kKeyId), signature});
    const std::vector<uint8_t> signature_bytes(signature_text.begin(), signature_text.end());
    fixture_server().serve_bytes("/v1/catalogue.json", catalogue_bytes, "application/json");
    fixture_server().serve_bytes("/v1/catalogue.json.sig", signature_bytes, "text/plain");

    registry::PlayerRegistries registries;
    // SDL caches this string for the process. The caller does not free it.
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        const fs::path builtins =
            path_from_utf8(base) / path_from_utf8(content::builtin_registries_folder_name);
        std::error_code io;
        if (fs::is_directory(builtins, io))
            for (const registry::Descriptor& built_in :
                 registry::read_builtin_registries(builtins, nullptr))
                registries.disabled.push_back(built_in.id);
    }
    const std::optional<oa::formats::url::Url> parsed =
        oa::formats::url::parse_http_url(catalogue_url);
    require(parsed.has_value(), "the catalogue's address did not read");
    registry::AddedRegistry added;
    added.descriptor.id = std::string(kRegistry);
    added.descriptor.name = "Ridge";
    added.descriptor.catalogue = *parsed;
    added.descriptor.keys.push_back(registry::RegistryKey{std::string(kKeyId), public_key});
    added.descriptor.mode = registry::DownloadMode::direct;
    added.descriptor.install_id = registry::InstallIdUse::required;
    added.added = "2026-10-01";
    added.enabled = true;
    registries.registries.push_back(std::move(added));
    write_text(
        user_folder() / std::string(registry::player_registries_file),
        registry::player_registries_text(registries)
    );
    std::cout << "first-run check: registry " << kRegistry << " at " << catalogue_url << ", "
              << registries.disabled.size() << " built-in registries turned off\n";
}

void Runtime::check_first_run() {
    require(options_.first_run, "needs --first-run");
    require(options_.preferences_file.has_value(), "needs --preferences-file");
    require(sdl_.window != nullptr && sdl_.renderer != nullptr, "the window did not open");
    require(
        SDL_SetWindowSize(sdl_.window, kCanvasWidth, kCanvasHeight) && SDL_SyncWindow(sdl_.window),
        "the window did not take 640x480"
    );
    apply_output_mode();
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(sdl_.window, &width, &height);
    require(width == kCanvasWidth && height == kCanvasHeight, "the window is not 640x480");

    load(Screen::main_menu);
    require(screen_ == Screen::main_menu, "the main menu did not open");
    require(engine_settings_fonts() != nullptr, "the dialog's fonts did not load");

    const auto preference = [this](std::string_view key) -> std::string {
        const auto found = preference_values_.find(std::string(key));
        if (found == preference_values_.end())
            return {};
        return found->second;
    };
    const auto pump = [this] {
        bool running = true;
        run_frame(running);
        require(running, "a frame ended the run");
    };
    const auto setup = [this]() -> LayerScreen* { return oa_layer().find(kScreen); };
    const auto control_named = [&](std::string_view name) -> std::optional<kit::Control> {
        LayerScreen* screen = setup();
        if (screen == nullptr)
            return std::nullopt;
        const kit::DisplayList display = screen->display_list();
        for (const kit::Control& control : display.controls)
            if (control.name == name)
                return control;
        return std::nullopt;
    };
    const auto click = [&](std::string_view name) {
        const std::optional<kit::Control> control = control_named(name);
        require(
            control.has_value() && control->enabled, std::string(name) + " is not on the setup"
        );
        LayerScreen* screen = setup();
        require(screen != nullptr, "the setup is not showing");
        const LayerPlacement placed = screen->placement(oa_layer().view());
        const int32_t scale = placed.points_width > 0 && placed.shown.width > 0
                                  ? placed.shown.width / placed.points_width
                                  : 1;
        const oa::ui::display_layout::Point at{
            placed.shown.x + (control->rect.x + control->rect.width / 2) * scale,
            placed.shown.y + (control->rect.y + control->rect.height / 2) * scale
        };
        send_check_pointer(SDL_EVENT_MOUSE_MOTION, at, 0);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, at, SDL_BUTTON_LEFT);
        send_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, at, SDL_BUTTON_LEFT);
    };
    const auto settle = [&](std::string_view name) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        while (std::chrono::steady_clock::now() < until) {
            if (control_named(name))
                return;
            require(!deadline_passed(deadline), std::string(name) + " did not appear");
            pump();
        }
    };
    const auto show_control = [&](std::string_view name) {
        for (int back = 0; back < 8 && !control_named(name); ++back) {
            const std::optional<kit::Control> previous =
                control_named("first-run.language.previous");
            if (!previous || !previous->enabled)
                break;
            click("first-run.language.previous");
            settle(name);
        }
        for (int forward = 0; forward < 8 && !control_named(name); ++forward) {
            const std::optional<kit::Control> next_card =
                control_named("first-run.language.next-card");
            require(next_card && next_card->enabled, std::string(name) + " is not on the carousel");
            click("first-run.language.next-card");
            settle(name);
        }
        require(control_named(name).has_value(), std::string(name) + " is not on the carousel");
    };
    const auto pick_card = [&](std::string_view name) {
        show_control(name);
        click(name);
        click(name);
    };
    const auto catalogue_lists_pack = [this] {
        if (!content_)
            return false;
        const std::shared_ptr<const content::Snapshot> snapshot = content_service().snapshot();
        if (!snapshot)
            return false;
        for (const content::Entry& entry : snapshot->entries) {
            if (entry.package != nullptr && entry.registry == kRegistry &&
                entry.package->id == kTag)
                return true;
        }
        return false;
    };
    const auto pack_download = [this]() -> std::optional<content::DownloadView> {
        if (!content_)
            return std::nullopt;
        const std::vector<content::DownloadView> items = content_downloads().view();
        for (const content::DownloadView& item : items) {
            if (item.target.registry == kRegistry &&
                settings::language_notice_same_tag(item.target.key, kTag))
                return item;
        }
        return std::nullopt;
    };

    {
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        content_service().refresh(content::RefreshReason::check_now, kRegistry);
        while (setup() == nullptr || !catalogue_lists_pack()) {
            require(!deadline_passed(deadline), "the setup did not show the catalogue's language");
            pump();
        }
    }
    std::cout << "first-run check: showing, catalogue lists " << kTag << '\n';

    pick_card("first-run.language.card.de");
    require(
        settings::language_notice_same_tag(shown_language().tag, "de"), "Deutsch was not chosen"
    );
    std::cout << "first-run check: language de\n";

    pick_card("first-run.language.card.en-xa");
    {
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        while (true) {
            const std::optional<content::DownloadView> item = pack_download();
            if (item && (item->state == content::DownloadState::failed ||
                         item->state == content::DownloadState::cancelled))
                break;
            if (deadline_passed(deadline)) {
                std::string detail = "the missing pack did not fail";
                if (item) {
                    detail += " state " + std::to_string(static_cast<int>(item->state));
                    if (item->failure != content::DownloadFailure::none) {
                        detail += " ";
                        detail += content::failure_text(item->failure);
                    }
                }
                require(false, detail);
            }
            pump();
        }
    }
    std::cout << "first-run check: the pack was missing\n";
    fixture_server().serve_bytes(held_pack().path, held_pack().bytes, "application/octet-stream");
    {
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        while (!control_named("first-run.language.retry")) {
            require(!deadline_passed(deadline), "Retry did not show");
            pump();
        }
    }
    click("first-run.language.retry");
    {
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        while (!settings::language_notice_same_tag(shown_language().tag, kTag)) {
            if (deadline_passed(deadline)) {
                std::string detail = "Pseudo was not shown";
                const std::optional<content::DownloadView> queued = pack_download();
                if (queued) {
                    detail += " state " + std::to_string(static_cast<int>(queued->state));
                    if (queued->failure != content::DownloadFailure::none) {
                        detail += " ";
                        detail += content::failure_text(queued->failure);
                        detail += " ";
                        detail += queued->detail;
                    }
                }
                require(false, detail);
            }
            pump();
        }
    }
    std::cout << "first-run check: language " << kTag << '\n';

    click("first-run.language.skip");
    require(setup() == nullptr, "Skip setup left the setup showing");
    require(
        preference(first_run::preference_key) == first_run::preference_done,
        "Skip setup did not record done"
    );
    std::cout << "first-run check: skipped, preference done\n";

    {
        const auto deadline = std::chrono::steady_clock::now() + kWait;
        while (setup() == nullptr) {
            require(!deadline_passed(deadline), "--first-run did not show the setup again");
            pump();
        }
    }
    click("first-run.language.next");
    require(control_named("first-run.online.not-now").has_value(), "the online step did not show");
    click("first-run.online.not-now");
    click("first-run.online.next");
    require(
        preference(content::content_updates_key) == "never",
        "Not now did not set content updates to never"
    );
    require(
        preference(content::install_id_key("coreprime")) == "off",
        "Not now did not turn Core Prime's install ID off"
    );
    std::cout << "first-run check: online features off\n";

    click("first-run.buttons.back");
    require(control_named("first-run.online.on").has_value(), "Back did not return to online");
    click("first-run.online.on");
    click("first-run.online.next");
    require(
        preference(content::content_updates_key) == "automatically",
        "Turn on did not set content updates to automatically"
    );
    require(
        preference_values_.find(content::install_id_key("coreprime")) == preference_values_.end(),
        "Turn on made an install ID"
    );
    std::cout << "first-run check: online features on, no install ID made\n";

    click("first-run.buttons.start");
    require(setup() == nullptr, "Start playing left the setup showing");
    require(
        preference(first_run::preference_key) == first_run::preference_done,
        "Start playing cleared the finished preference"
    );
    options_.first_run = false;
    for (int frame = 0; frame < 8; ++frame)
        pump();
    require(
        setup() == nullptr && oa_layer().waiting(kScreen) == nullptr,
        "the setup showed again after it was finished"
    );
    std::cout << "first-run check: passed\n" << std::flush;
}

} // namespace oa::app
