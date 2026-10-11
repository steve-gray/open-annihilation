// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-registries: Settings › Downloads › Registries and the Add registry
// window over a registry list the check writes itself before the content
// service starts. One added registry, signed with a key the check makes from
// a fixed seed, its catalogue at 127.0.0.1:1, where nothing answers. The
// check switches it off at OK, on at Cancel, and removes it at OK, reading
// the player's registry list after each; types an https address, which is
// refused without a check; and opens an .oareg file of another registry,
// which is refused once its catalogue cannot be reached. It makes no
// connection beyond that refused one to 127.0.0.1.

#include "oa_layer.hpp"
#include "oa_layer_check.hpp"

#include "oa/app/content/service.hpp"
#include "oa/app/package_install/inbox.hpp"
#include "oa/app/runtime.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/player_registries.hpp"
#include "oa/data/registry/trust.hpp"
#include "oa/formats/url.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings/add_registry.hpp"
#include "oa/ui/kit/input.hpp"
#include "oa/ui/kit/layout.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oa::app {

namespace {

namespace kit = oa::ui::kit;
namespace layout = oa::ui::display_layout;
namespace registry = oa::data::registry;
namespace signing = oa::base::signing;

/// The modifier the settings' shortcut takes on this platform.
#ifdef SDL_PLATFORM_MACOS
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_GUI;
#else
constexpr SDL_Keymod kShortcutModifier = SDL_KMOD_CTRL;
#endif

/// The registry the check's list holds.
constexpr std::string_view kAddedId = "check-maps";
/// The registry the check's .oareg file describes.
constexpr std::string_view kOfferedId = "check-lan";
/// Where both registries' catalogues are: a port nothing answers on.
constexpr std::string_view kCatalogue = "http://127.0.0.1:1/v1/catalogue.json";
/// The address the refused .oareg is named by.
constexpr std::string_view kUnreachable = "127.0.0.1:1";
/// The https address the window refuses before it asks anything.
constexpr std::string_view kHttpsAddress = "https://maps.example.org/oa/registry.yaml";
/// The sentence it refuses that address with.
constexpr std::string_view kHttpsSentence =
    "Use the registry's http:// address. OA checks every catalogue's signature itself.";
/// The .oareg file's name.
constexpr std::string_view kOfferedFile = "check-lan.oareg";
/// The window the check drives the dialog at: Large, at 1×, so that every
/// row it presses shows.
constexpr int kWindowWidth = 1280;
/// The window's height.
constexpr int kWindowHeight = 720;
/// The frames the check waits for the content service's answer.
constexpr int kAnswerFrames = 2000;

/// Throws when a step of the check failed.
///
/// @param ok the step held
/// @param what the step
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("registries check: " + std::string(what));
}

/// Returns a descriptor signed with a key made from a fixed seed.
///
/// @param id the registry's id
/// @param name its name
/// @param seed_byte the byte the seed is made of
/// @return the descriptor
registry::Descriptor
signed_descriptor(std::string_view id, std::string_view name, uint8_t seed_byte) {
    std::array<uint8_t, 32> seed{};
    seed.fill(seed_byte);
    signing::SecretKey secret{};
    signing::PublicKey key{};
    signing::key_pair_from_seed(seed, secret, key);
    registry::Descriptor descriptor;
    descriptor.id = std::string(id);
    descriptor.name = std::string(name);
    const std::optional<oa::formats::url::Url> catalogue =
        oa::formats::url::parse_http_url(kCatalogue);
    require(catalogue.has_value(), "the catalogue's address did not read");
    descriptor.catalogue = *catalogue;
    descriptor.keys.push_back(registry::RegistryKey{std::string(id) + "-key", key});
    descriptor.mode = registry::DownloadMode::direct;
    return descriptor;
}

/// Writes a text file whole.
///
/// @param file the file
/// @param text its text
void write_text(const std::filesystem::path& file, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    require(static_cast<bool>(out), "could not write " + file.string());
}

/// Reads a file's bytes whole.
///
/// @param file the file
/// @return its bytes; empty when it cannot be read
std::string file_bytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// Reads the player's registry list as the content service keeps it.
///
/// @param file the list
/// @return what it holds
registry::PlayerRegistries read_list(const std::filesystem::path& file) {
    registry::PlayerRegistries list;
    std::string error;
    require(
        registry::load_player_registries(file, list, &error) == registry::LoadResult::read,
        "the registry list did not read: " + error
    );
    return list;
}

/// Returns the added registry of an id in a list.
///
/// @param list the list
/// @param id the id
/// @return the registry; null when the list does not hold it
const registry::AddedRegistry*
added_of(const registry::PlayerRegistries& list, std::string_view id) {
    for (const registry::AddedRegistry& added : list.registries)
        if (added.descriptor.id == id)
            return &added;
    return nullptr;
}

/// Returns the path of one step's snapshot: <stem>-<step>.ppm beside --snapshot.
///
/// @param snapshot the --snapshot path
/// @param step the step's name
/// @return the step's snapshot path
std::filesystem::path step_snapshot(const std::filesystem::path& snapshot, std::string_view step) {
    return snapshot.parent_path() / (snapshot.stem().string() + '-' + std::string(step) + ".ppm");
}

/// Returns the text of a control of a display list.
///
/// @param list the list
/// @param name the control's name
/// @return its text; none when the list has no such control
std::optional<std::string> text_of(const kit::DisplayList& list, std::string_view name) {
    const kit::Control* control = kit::control_of(list, kit::control_named(list, name));
    if (control == nullptr)
        return std::nullopt;
    return control->text;
}

} // namespace

void Runtime::prepare_registries_check() {
    // Written before the content service reads the player's folder.
    const std::filesystem::path folder = user_folder();
    registry::PlayerRegistries list;
    registry::AddedRegistry added;
    added.descriptor = signed_descriptor(kAddedId, "Check Maps", 0x21);
    added.added = "2026-10-01";
    added.enabled = true;
    list.registries.push_back(added);
    write_text(
        folder / std::string(registry::player_registries_file),
        registry::player_registries_text(list)
    );
    write_text(
        folder / std::string(kOfferedFile),
        registry::descriptor_text(signed_descriptor(kOfferedId, "Check LAN", 0x42))
    );
    std::cout << "registries check: the registry list holds " << kAddedId << '\n';
}

void Runtime::check_registries() {
    require(sdl_.renderer != nullptr && sdl_.window != nullptr, "needs the SDL presenter");
    require(options_.preferences_file.has_value(), "needs --preferences-file");
    preference_values_.clear();
    oa::platform::preferences::save(preference_path_, preference_values_);
    fake_frontend_tick_ = 1000U;
    require(
        SDL_SetWindowSize(sdl_.window, kWindowWidth, kWindowHeight) && SDL_SyncWindow(sdl_.window),
        "the window did not take its size"
    );
    apply_output_mode();
    load(Screen::main_menu);
    require(screen_ == Screen::main_menu, "the main menu did not open");
    require(engine_settings_fonts() != nullptr, "the dialog's fonts did not load");
    const std::filesystem::path list_file =
        user_folder() / std::string(registry::player_registries_file);

    bool running = true;
    const auto tap = [this, &running](SDL_Keycode code, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        for (const bool down : {true, false}) {
            SDL_Event event{};
            event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = SDL_GetWindowID(sdl_.window);
            event.key.key = code;
            event.key.scancode = SDL_GetScancodeFromKey(code, nullptr);
            event.key.mod = modifiers;
            event.key.down = down;
            dispatch_event(event, running);
        }
        require(running, "a key ended the run");
    };
    const auto named = [this](std::string_view name) -> std::optional<AutomationControl> {
        std::vector<AutomationControl> controls;
        oa_layer().automation_controls(controls);
        for (const AutomationControl& control : controls)
            if (control.name == name && control.visible && control.window_width > 0 &&
                control.window_height > 0)
                return control;
        return std::nullopt;
    };
    const auto click_named = [&](std::string_view name) {
        const std::optional<AutomationControl> control = named(name);
        require(control.has_value(), std::string(name) + " is not shown");
        const layout::Point pixel{
            control->window_x + control->window_width / 2,
            control->window_y + control->window_height / 2
        };
        for (const SDL_EventType type :
             {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
            SDL_Event event = window_pointer_event(
                sdl_.window,
                sdl_.renderer,
                type,
                pixel,
                type == SDL_EVENT_MOUSE_MOTION ? 0 : SDL_BUTTON_LEFT
            );
            dispatch_event(event, running);
            require(running, "a click ended the run");
        }
    };
    const auto open_registries = [&] {
        tap(SDLK_COMMA, kShortcutModifier);
        require(engine_settings_dialog() != nullptr, "the shortcut did not open Settings");
        click_named("oa.settings.nav.downloads");
        click_named("oa.settings.registries.manage");
        require(
            named("oa.settings.registries.back").has_value(),
            "MANAGE… did not open the Registries part"
        );
    };
    // With --snapshot, the frame as presented at a step.
    const auto keep_frame = [this](std::string_view step) {
        if (options_.snapshot.empty())
            return;
        renderer::Surface presented;
        capture_frame_ = &presented;
        render();
        capture_frame_ = nullptr;
        write_ppm(step_snapshot(options_.snapshot, step), presented);
    };
    const auto window_list = [this]() -> kit::DisplayList {
        LayerScreen* top = oa_layer().top();
        require(
            top != nullptr && top->name() == "add-registry", "the Add registry window is not on top"
        );
        return top->display_list();
    };

    // The part lists the added registry with REMOVE, and every built-in
    // registry this build has with its switch alone.
    open_registries();
    const std::string added_switch = "oa.settings.registry." + std::string(kAddedId);
    require(named(added_switch).has_value(), "the added registry is not listed");
    require(named(added_switch + ".remove").has_value(), "the added registry has no REMOVE");
    std::size_t built_ins = 0;
    if (const std::shared_ptr<const content::Snapshot> snapshot = content_service().snapshot()) {
        for (const content::RegistryView& listed : snapshot->registries) {
            if (listed.registry.origin != registry::Origin::built_in)
                continue;
            const std::string word = "oa.settings.registry." + listed.registry.descriptor.id;
            require(named(word).has_value(), word + " is not listed");
            require(!named(word + ".remove").has_value(), word + " offers REMOVE");
            require(!named(word + ".add-mirror").has_value(), word + " offers ADD MIRROR…");
            ++built_ins;
        }
    }
    keep_frame("menu-registries");
    std::cout << "registries check: listed " << kAddedId << " with REMOVE and " << built_ins
              << " built-in registr" << (built_ins == 1 ? "y" : "ies") << " without\n";

    // Off, then OK: the list says so.
    click_named(added_switch + ".off");
    click_named("oa.settings.ok");
    require(engine_settings_dialog() == nullptr, "OK did not close Settings");
    {
        const registry::PlayerRegistries list = read_list(list_file);
        const registry::AddedRegistry* added = added_of(list, kAddedId);
        require(added != nullptr && !added->enabled, "OK did not store the registry turned off");
        require(
            file_bytes(list_file).find("enabled: false") != std::string::npos,
            "the list does not say enabled: false"
        );
    }
    std::cout << "registries check: OK stored the registry turned off\n";

    // On, then Cancel: the list is as it was, byte for byte.
    const std::string before_cancel = file_bytes(list_file);
    open_registries();
    click_named(added_switch + ".on");
    click_named("oa.settings.cancel");
    require(engine_settings_dialog() == nullptr, "Cancel did not close Settings");
    require(file_bytes(list_file) == before_cancel, "Cancel changed the registry list");
    std::cout << "registries check: Cancel left the registry list as it was\n";

    // REMOVE, then OK: the list no longer holds it.
    open_registries();
    click_named(added_switch + ".remove");
    require(named(added_switch + ".undo").has_value(), "REMOVE did not offer UNDO");
    click_named("oa.settings.ok");
    require(engine_settings_dialog() == nullptr, "OK did not close Settings");
    require(added_of(read_list(list_file), kAddedId) == nullptr, "OK did not remove the registry");
    std::cout << "registries check: OK removed the registry\n";

    // ADD REGISTRY… with an https address: refused at once, and the content
    // service never asked.
    open_registries();
    const uint64_t asked = registry_checks_asked();
    click_named("oa.settings.add-registry");
    require(!window_list().controls.empty(), "ADD REGISTRY… did not open the window");
    require(SDL_TextInputActive(sdl_.window), "the address field did not start text input");
    SDL_Event typed{};
    typed.type = SDL_EVENT_TEXT_INPUT;
    typed.text.windowID = SDL_GetWindowID(sdl_.window);
    const std::string https(kHttpsAddress);
    typed.text.text = https.c_str();
    dispatch_event(typed, running);
    require(running, "typing ended the run");
    require(
        text_of(window_list(), "add-registry.url") == https, "the field did not take the address"
    );
    tap(SDLK_RETURN);
    require(
        text_of(window_list(), "add-registry.refusal") == std::string(kHttpsSentence),
        "the https address was not refused with its sentence"
    );
    require(
        registry_checks_asked() == asked, "the content service was asked about an https address"
    );
    keep_frame("menu-add-registry-refused");
    click_named("oa.add-registry.cancel");
    require(oa_layer().find("add-registry") == nullptr, "CANCEL did not close the window");
    require(engine_settings_dialog() != nullptr, "CANCEL closed Settings too");
    click_named("oa.settings.cancel");
    require(engine_settings_dialog() == nullptr, "Cancel did not close Settings");
    std::cout << "registries check: an https address was refused without a check\n";

    // An .oareg file of another registry, posted as --open posts it: the
    // window opens filled in, and ends refused, naming the address its
    // catalogue could not be reached at.
    oa::app::package_install::post_opened_file(user_folder() / std::string(kOfferedFile));
    LayerScreen* offered = nullptr;
    for (int frame = 0; frame < 60 && offered == nullptr; ++frame) {
        run_frame(running);
        require(running, "a frame ended the run");
        offered = oa_layer().find("add-registry");
    }
    require(offered != nullptr, "the .oareg file did not open the window");
    std::optional<std::string> refusal;
    for (int frame = 0; frame < kAnswerFrames && !refusal; ++frame) {
        run_frame(running);
        require(running, "a frame ended the run");
        refusal = text_of(window_list(), "add-registry.refusal");
        require(
            !text_of(window_list(), "add-registry.add").has_value(),
            "the window offered ADD for a registry whose catalogue cannot be reached"
        );
    }
    require(refusal.has_value(), "the .oareg file's check never ended");
    require(
        refusal->find(kUnreachable) != std::string::npos,
        "the refusal does not name " + std::string(kUnreachable) + ": " + *refusal
    );
    std::cout << "registries check: the .oareg file was refused: " << *refusal << '\n';
    keep_frame("menu-add-registry-file");
    click_named("oa.add-registry.cancel");
    require(oa_layer().find("add-registry") == nullptr, "CANCEL did not close the window");
    require(
        added_of(read_list(list_file), kOfferedId) == nullptr, "the refused registry was added"
    );
    std::cout << "registries check: passed\n";
}

} // namespace oa::app
