// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Adding, removing and turning off registries against a local fixture.
#include "oa/app/content/service.hpp"

#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/player_registries.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/http_fixture/server.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace content = oa::app::content;
namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;
namespace fixture = oa::netgame::http_fixture;
namespace sha = oa::base::sha256;
namespace signing = oa::base::signing;
namespace threads = oa::base::threads;
namespace url = oa::formats::url;
namespace fs = std::filesystem;

/// 2026-01-01T00:00:00Z.
constexpr int64_t kEpoch = 1767225600;

std::atomic<int64_t> g_now{kEpoch};

/// The test clock, in seconds since 1970.
int64_t test_clock() {
    return g_now.load();
}

/// Resolves only 127.0.0.1, so a built-in host fails at once.
bool resolve_loopback(const char* host, uint8_t ip[4]) {
    if (host == nullptr || std::string_view(host) != "127.0.0.1")
        return false;
    ip[0] = 127;
    ip[1] = 0;
    ip[2] = 0;
    ip[3] = 1;
    return true;
}

/// Waits until a check has an outcome.
std::optional<content::RegistryCheck> await(content::Service& service, uint64_t request) {
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(8)) {
        if (std::optional<content::RegistryCheck> check = service.registry_check(request))
            return check;
        threads::sleep_ms(20);
    }
    std::fprintf(stderr, "check %llu did not finish\n", static_cast<unsigned long long>(request));
    return std::nullopt;
}

/// A scratch directory removed when the test ends.
struct Scratch {
    fs::path root;

    explicit Scratch(std::string_view name) : root(oa::test::make_scratch_directory(name)) {
        fs::create_directory(root / "data");
        fs::create_directory(root / "player");
        fs::create_directory(root / "builtin");
    }

    ~Scratch() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    fs::path data() const { return root / "data"; }

    fs::path player() const { return root / "player"; }

    fs::path builtin() const { return root / "builtin"; }
};

/// One signing key made from a fixed seed. The secret is wiped on destruction.
struct TestKey {
    signing::SecretKey secret{};
    registry::RegistryKey key{};

    TestKey(std::string id, uint8_t fill) {
        std::array<uint8_t, 32> seed{};
        seed.fill(fill);
        key.id = std::move(id);
        signing::key_pair_from_seed(seed, secret, key.key);
        seed.fill(0);
    }

    ~TestKey() { secret.fill(0); }

    TestKey(const TestKey&) = delete;
    TestKey& operator=(const TestKey&) = delete;
};

/// A catalogue and the detached signature over those exact bytes.
struct SignedBytes {
    std::vector<uint8_t> catalogue;
    std::vector<uint8_t> signature;
};

/// The lower-case hex of a short label, used as a package digest.
std::string hex_of(std::string_view text) {
    const auto digest = sha::digest_of(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size())
    );
    const auto hex = sha::to_hex(digest);
    return std::string(hex.data(), hex.size());
}

/// Builds one catalogue document with one mod.
std::string catalogue_json(std::string_view id, int sequence) {
    const std::string hash = hex_of("ridge-bytes");
    std::string json = "{\"catalogue\":1,\"registry\":\"";
    json += id;
    json += "\",\"sequence\":";
    json += std::to_string(sequence);
    json += ",\"generated\":\"2026-01-01T00:00:00Z\",\"expires\":\"2026-02-01T00:00:00Z\"";
    json += ",\"packages\":[{\"kind\":\"oamod\",\"id\":\"ridge\",\"name\":\"Ridge\"";
    json += ",\"version\":\"1\",\"revision\":1,\"release\":1,\"size\":11,\"sha256\":\"";
    json += hash;
    json += "\",\"file\":\"/v1/p/";
    json += hash;
    json += ".oamod\"}]}";
    return json;
}

/// Signs a catalogue with a test key.
SignedBytes sign_json(std::string_view json, const TestKey& key) {
    SignedBytes out;
    out.catalogue.assign(json.begin(), json.end());
    const signing::Signature signature = signing::sign(key.secret, out.catalogue);
    const catalogue::SignatureLine line{key.key.id, signature};
    const std::string text = catalogue::signature_file_text(line);
    out.signature.assign(text.begin(), text.end());
    return out;
}

/// Writes bytes to a file.
bool write_text(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

/// Reads a file. An unreadable file is empty and fails a check.
std::vector<uint8_t> file_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Builds a descriptor. A null key leaves the registry unsigned.
registry::Descriptor
descriptor_for(std::string id, std::string name, std::string catalogue_url, const TestKey* key) {
    registry::Descriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.name = std::move(name);
    const auto parsed = url::parse_http_url(catalogue_url);
    OA_CHECK(parsed.has_value());
    if (parsed)
        descriptor.catalogue = *parsed;
    if (key != nullptr)
        descriptor.keys.push_back(key->key);
    return descriptor;
}

/// Starts a fixture server.
bool listening(fixture::Server& server) {
    std::string error;
    const bool started = server.start(&error);
    if (!started)
        std::fprintf(stderr, "fixture did not start: %s\n", error.c_str());
    OA_CHECK(started);
    return started;
}

/// Serves a catalogue and its signature.
void serve_document(fixture::Server& server, const std::string& path, const SignedBytes& document) {
    server.serve_bytes(path, document.catalogue, "application/json");
    server.serve_bytes(path + ".sig", document.signature, "text/plain");
}

/// Serves a descriptor.
void serve_descriptor(
    fixture::Server& server, const std::string& path, const registry::Descriptor& descriptor
) {
    const std::string text = registry::descriptor_text(descriptor);
    server.serve_bytes(path, std::vector<uint8_t>(text.begin(), text.end()), "application/yaml");
}

/// Counts requests whose target is exactly this path.
int count_target(const fixture::Server& server, std::string_view target) {
    int count = 0;
    for (const fixture::LoggedRequest& request : server.requests()) {
        if (request.target == target)
            ++count;
    }
    return count;
}

/// Options pointed at a scratch directory, with no refresh at start.
content::ServiceOptions options_for(const Scratch& scratch) {
    content::ServiceOptions options;
    options.data_folder = scratch.data();
    options.player_folder = scratch.player();
    options.builtin_folder = scratch.builtin();
    options.automatic = false;
    options.clock = test_clock;
    options.http.resolve = resolve_loopback;
    return options;
}

/// The player's registry file.
fs::path registries_file(const Scratch& scratch) {
    return scratch.player() / std::string(registry::player_registries_file);
}

/// Loads the player's registries.
registry::PlayerRegistries load_player(const Scratch& scratch) {
    registry::PlayerRegistries player;
    std::string error;
    const registry::LoadResult loaded =
        registry::load_player_registries(registries_file(scratch), player, &error);
    OA_CHECK(loaded == registry::LoadResult::read);
    if (loaded != registry::LoadResult::read)
        std::fprintf(stderr, "load: %s\n", error.c_str());
    return player;
}

/// Finds a registry view by id.
const content::RegistryView* find_view(const content::Snapshot& snapshot, std::string_view id) {
    for (const content::RegistryView& view : snapshot.registries) {
        if (view.registry.descriptor.id == id)
            return &view;
    }
    return nullptr;
}

/// Checks a refusal and prints the detail when it is not the one expected.
void expect_refusal(
    const std::optional<content::RegistryCheck>& check, registry::Refusal expected
) {
    OA_CHECK(check.has_value());
    if (!check)
        return;
    if (check->refusal != expected) {
        std::fprintf(
            stderr,
            "refusal %d (%s) detail: %s\n",
            static_cast<int>(check->refusal),
            registry::refusal_text(check->refusal),
            check->detail.c_str()
        );
    }
    OA_CHECK(check->refusal == expected);
}

/// The built-in registry these tests ship beside the program.
registry::Descriptor install_builtin(const Scratch& scratch, const TestKey& key) {
    registry::Descriptor descriptor = descriptor_for(
        "core-example", "Core Example", "http://builtin.example/catalogue.json", &key
    );
    OA_CHECK(
        write_text(scratch.builtin() / "core-example.yaml", registry::descriptor_text(descriptor))
    );
    return descriptor;
}

/// A signed registry served by `server`, with its catalogue beside it.
registry::Descriptor serve_registry(
    fixture::Server& server,
    const TestKey& key,
    std::string id,
    std::string name,
    std::string descriptor_path,
    std::string catalogue_url,
    std::string catalogue_path,
    const TestKey* signer
) {
    registry::Descriptor descriptor =
        descriptor_for(std::move(id), std::move(name), catalogue_url, &key);
    serve_descriptor(server, descriptor_path, descriptor);
    const SignedBytes document =
        sign_json(catalogue_json(descriptor.id, 1), signer == nullptr ? key : *signer);
    serve_document(server, catalogue_path, document);
    return descriptor;
}

void test_add_by_url() {
    g_now = kEpoch;
    const TestKey builtin_key("core-2026", 0x11);
    const TestKey key("ridge-2026", 0x22);
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f23-url");
    install_builtin(scratch, builtin_key);
    const std::string catalogue_url = server.url("/v1/catalogue.json");
    const registry::Descriptor descriptor = serve_registry(
        server,
        key,
        "ridge-maps",
        "Ridge Maps",
        "/oa/registry.yaml",
        catalogue_url,
        "/v1/catalogue.json",
        nullptr
    );
    content::Service service(options_for(scratch));
    service.start();
    const uint64_t request = service.check_registry_url(server.url("/oa/registry.yaml"));
    const std::optional<content::RegistryCheck> pending = service.registry_check(request);
    OA_CHECK(!pending.has_value());
    const std::optional<content::RegistryCheck> check = await(service, request);
    expect_refusal(check, registry::Refusal::none);
    if (!check || check->refusal != registry::Refusal::none)
        return;
    OA_CHECK(check->url == server.url("/oa/registry.yaml"));
    OA_CHECK(check->address == registry::address_text(descriptor));
    OA_CHECK(check->descriptor.has_value());
    OA_CHECK(check->descriptor && check->descriptor->id == "ridge-maps");
    OA_CHECK(check->fingerprints == registry::key_fingerprints(descriptor));
    OA_CHECK(check->packages == 1);
    OA_CHECK(check->kinds.size() == 1);
    OA_CHECK(check->kinds.size() == 1 && check->kinds[0].first == catalogue::Kind::oamod);
    OA_CHECK(check->kinds.size() == 1 && check->kinds[0].second == 1);
    OA_CHECK(check->catalogue && check->catalogue->packages.size() == 1);

    std::string why;
    OA_CHECK(service.add_checked_registry(request, &why) == registry::Refusal::none);
    const auto snapshot = service.snapshot();
    const content::RegistryView* view = snapshot ? find_view(*snapshot, "ridge-maps") : nullptr;
    OA_CHECK(view != nullptr);
    OA_CHECK(view && view->registry.origin == registry::Origin::added);
    OA_CHECK(view && !view->registry.descriptor.keys.empty());
    OA_CHECK(snapshot && snapshot->entries.size() == 1);
    OA_CHECK(snapshot && snapshot->entries.size() == 1 && snapshot->entries[0].reviewed == false);
    OA_CHECK(
        snapshot && snapshot->entries.size() == 1 && snapshot->entries[0].registry == "ridge-maps"
    );

    const registry::PlayerRegistries player = load_player(scratch);
    OA_CHECK(player.registries.size() == 1);
    if (player.registries.size() == 1) {
        OA_CHECK(player.registries[0].descriptor.id == "ridge-maps");
        OA_CHECK(player.registries[0].url == server.url("/oa/registry.yaml"));
        OA_CHECK(player.registries[0].added == "2026-01-01");
        OA_CHECK(player.registries[0].descriptor.keys.size() == 1);
        OA_CHECK(player.registries[0].descriptor.keys[0].key == key.key.key);
    }
    const fs::path cache =
        scratch.data() / "content" / "catalogues" / "ridge-maps" / "catalogue.json";
    OA_CHECK(file_bytes(cache) == sign_json(catalogue_json("ridge-maps", 1), key).catalogue);
}

void test_add_from_file() {
    g_now = kEpoch;
    const TestKey builtin_key("core-2026", 0x11);
    const TestKey key("ridge-2026", 0x23);
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f23-file");
    install_builtin(scratch, builtin_key);
    const registry::Descriptor descriptor =
        descriptor_for("ridge-file", "Ridge File", server.url("/v1/catalogue.json"), &key);
    serve_document(server, "/v1/catalogue.json", sign_json(catalogue_json("ridge-file", 1), key));
    const std::string text = registry::descriptor_text(descriptor);
    content::Service service(options_for(scratch));
    service.start();
    const uint64_t request =
        service.check_registry_file(std::vector<uint8_t>(text.begin(), text.end()));
    const std::optional<content::RegistryCheck> check = await(service, request);
    expect_refusal(check, registry::Refusal::none);
    if (!check)
        return;
    OA_CHECK(check->url.empty());
    OA_CHECK(check->address == registry::address_text(descriptor));
    std::string why;
    OA_CHECK(service.add_checked_registry(request, &why) == registry::Refusal::none);
    const registry::PlayerRegistries player = load_player(scratch);
    OA_CHECK(player.registries.size() == 1);
    OA_CHECK(player.registries.size() == 1 && player.registries[0].url.empty());
    OA_CHECK(player.registries.size() == 1 && player.registries[0].added == "2026-01-01");
    const std::string stored = registry::player_registries_text(player);
    OA_CHECK(stored.find("\n    url:") == std::string::npos);
}

void test_refusals() {
    g_now = kEpoch;
    const TestKey builtin_key("core-2026", 0x11);
    const TestKey key("ridge-2026", 0x24);
    const TestKey other("other-2026", 0x25);
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f23-refuse");
    const registry::Descriptor builtin = install_builtin(scratch, builtin_key);
    const std::string catalogue_url = server.url("/v1/catalogue.json");
    serve_registry(
        server,
        key,
        "ridge-maps",
        "Ridge Maps",
        "/oa/registry.yaml",
        catalogue_url,
        "/v1/catalogue.json",
        nullptr
    );
    content::Service service(options_for(scratch));
    service.start();

    const int before = static_cast<int>(server.requests().size());
    const uint64_t https = service.check_registry_url("https://127.0.0.1/oa/registry.yaml");
    const std::optional<content::RegistryCheck> https_check = await(service, https);
    expect_refusal(https_check, registry::Refusal::unreadable);
    OA_CHECK(https_check && !https_check->descriptor.has_value());
    OA_CHECK(https_check && https_check->address.empty());
    OA_CHECK(
        https_check && https_check->detail == url::url_error_text(url::UrlError::https_not_fetched)
    );
    OA_CHECK(static_cast<int>(server.requests().size()) == before);

    server.serve_bytes("/bad.yaml", {'n', 'o', '\n'}, "text/plain");
    const uint64_t broken = service.check_registry_url(server.url("/bad.yaml"));
    const std::optional<content::RegistryCheck> broken_check = await(service, broken);
    expect_refusal(broken_check, registry::Refusal::unreadable);
    OA_CHECK(broken_check && !broken_check->descriptor.has_value());
    OA_CHECK(broken_check && !broken_check->detail.empty());

    serve_descriptor(
        server, "/id.yaml", descriptor_for("core-example", "Other Name", catalogue_url, &key)
    );
    const uint64_t same_id = service.check_registry_url(server.url("/id.yaml"));
    const std::optional<content::RegistryCheck> id_check = await(service, same_id);
    expect_refusal(id_check, registry::Refusal::built_in_id);
    OA_CHECK(id_check && id_check->descriptor && id_check->descriptor->id == "core-example");
    OA_CHECK(id_check && id_check->address == registry::address_text(*id_check->descriptor));

    serve_descriptor(
        server, "/name.yaml", descriptor_for("other-name", "Core Example", catalogue_url, &key)
    );
    expect_refusal(
        await(service, service.check_registry_url(server.url("/name.yaml"))),
        registry::Refusal::built_in_name
    );

    serve_descriptor(
        server, "/key.yaml", descriptor_for("other-key", "Other Key", catalogue_url, &builtin_key)
    );
    expect_refusal(
        await(service, service.check_registry_url(server.url("/key.yaml"))),
        registry::Refusal::built_in_key
    );

    serve_descriptor(
        server,
        "/host.yaml",
        descriptor_for("other-host", "Other Host", "http://builtin.example/catalogue.json", &key)
    );
    const std::optional<content::RegistryCheck> host =
        await(service, service.check_registry_url(server.url("/host.yaml")));
    expect_refusal(host, registry::Refusal::built_in_host);
    OA_CHECK(host && host->descriptor.has_value());
    OA_CHECK(host && host->address == "builtin.example");

    const uint64_t added = service.check_registry_url(server.url("/oa/registry.yaml"));
    const std::optional<content::RegistryCheck> added_check = await(service, added);
    expect_refusal(added_check, registry::Refusal::none);
    std::string why;
    OA_CHECK(service.add_checked_registry(added, &why) == registry::Refusal::none);
    const uint64_t again = service.check_registry_url(server.url("/oa/registry.yaml"));
    const std::optional<content::RegistryCheck> again_check = await(service, again);
    expect_refusal(again_check, registry::Refusal::already_added);
    OA_CHECK(again_check && again_check->descriptor.has_value());

    serve_descriptor(
        server, "/unsigned.yaml", descriptor_for("ridge-open", "Ridge Open", catalogue_url, nullptr)
    );
    const std::optional<content::RegistryCheck> unsigned_check =
        await(service, service.check_registry_url(server.url("/unsigned.yaml")));
    expect_refusal(unsigned_check, registry::Refusal::unsigned_needs_developer_mode);
    OA_CHECK(unsigned_check && unsigned_check->descriptor.has_value());

    serve_registry(
        server,
        key,
        "ridge-forged",
        "Ridge Forged",
        "/forged.yaml",
        server.url("/forged/catalogue.json"),
        "/forged/catalogue.json",
        &other
    );
    const std::optional<content::RegistryCheck> forged =
        await(service, service.check_registry_url(server.url("/forged.yaml")));
    expect_refusal(forged, registry::Refusal::unreadable);
    OA_CHECK(forged && forged->descriptor && forged->descriptor->id == "ridge-forged");
    OA_CHECK(forged && forged->address == registry::address_text(*forged->descriptor));
    OA_CHECK(forged && forged->detail == catalogue::verdict_text(catalogue::Verdict::unknown_key));

    serve_descriptor(
        server,
        "/down.yaml",
        descriptor_for("ridge-down", "Ridge Down", "http://127.0.0.1:1/catalogue.json", &key)
    );
    const std::optional<content::RegistryCheck> down =
        await(service, service.check_registry_url(server.url("/down.yaml")));
    expect_refusal(down, registry::Refusal::unreadable);
    OA_CHECK(down && down->descriptor && down->descriptor->id == "ridge-down");
    OA_CHECK(down && down->address == "127.0.0.1:1");
    OA_CHECK(down && down->detail.find("cannot be reached") != std::string::npos);

    OA_CHECK(service.add_checked_registry(added, &why) == registry::Refusal::unreadable);
    OA_CHECK(why == "This check was already used.");
    const registry::PlayerRegistries player = load_player(scratch);
    OA_CHECK(player.registries.size() == 1);
    (void)builtin;
}

void test_remove_and_enable() {
    g_now = kEpoch;
    const TestKey builtin_key("core-2026", 0x11);
    const TestKey key("ridge-2026", 0x26);
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f23-edit");
    install_builtin(scratch, builtin_key);
    const std::string catalogue_path = "/v1/catalogue.json";
    serve_registry(
        server,
        key,
        "ridge-maps",
        "Ridge Maps",
        "/oa/registry.yaml",
        server.url(catalogue_path),
        catalogue_path,
        nullptr
    );
    content::Service service(options_for(scratch));
    service.start();
    const uint64_t request = service.check_registry_url(server.url("/oa/registry.yaml"));
    OA_CHECK(await(service, request).has_value());
    std::string why;
    OA_CHECK(service.add_checked_registry(request, &why) == registry::Refusal::none);

    const fs::path catalogues = scratch.data() / "content" / "catalogues";
    const fs::path added_cache = catalogues / "ridge-maps";
    const fs::path kept = catalogues / "keep-me";
    const fs::path builtin_cache = catalogues / "core-example";
    fs::create_directories(kept);
    fs::create_directories(builtin_cache);
    OA_CHECK(write_text(kept / "note.txt", "keep"));
    OA_CHECK(write_text(builtin_cache / "note.txt", "built-in"));
    OA_CHECK(fs::is_directory(added_cache));

    OA_CHECK(service.remove_registry("core-example", &why) == registry::Refusal::not_added);
    OA_CHECK(why == "can be turned off, not removed");
    OA_CHECK(fs::is_regular_file(builtin_cache / "note.txt"));
    OA_CHECK(load_player(scratch).registries.size() == 1);

    OA_CHECK(service.remove_registry("ridge-maps", &why) == registry::Refusal::none);
    OA_CHECK(!fs::exists(added_cache));
    OA_CHECK(fs::is_regular_file(kept / "note.txt"));
    OA_CHECK(fs::is_regular_file(builtin_cache / "note.txt"));
    OA_CHECK(load_player(scratch).registries.empty());
    const auto removed = service.snapshot();
    OA_CHECK(removed && find_view(*removed, "ridge-maps") == nullptr);
    OA_CHECK(removed && find_view(*removed, "core-example") != nullptr);

    const uint64_t again = service.check_registry_url(server.url("/oa/registry.yaml"));
    OA_CHECK(await(service, again).has_value());
    OA_CHECK(service.add_checked_registry(again, &why) == registry::Refusal::none);

    OA_CHECK(service.set_registry_enabled("core-example", false, &why) == registry::Refusal::none);
    registry::PlayerRegistries off = load_player(scratch);
    OA_CHECK(off.disabled.size() == 1);
    OA_CHECK(off.disabled.size() == 1 && off.disabled[0] == "core-example");
    const auto disabled = service.snapshot();
    const content::RegistryView* builtin_view =
        disabled ? find_view(*disabled, "core-example") : nullptr;
    OA_CHECK(builtin_view && builtin_view->status == content::RegistryStatus::disabled);

    OA_CHECK(service.set_registry_enabled("core-example", true, &why) == registry::Refusal::none);
    OA_CHECK(load_player(scratch).disabled.empty());
    const auto enabled = service.snapshot();
    const content::RegistryView* builtin_on =
        enabled ? find_view(*enabled, "core-example") : nullptr;
    OA_CHECK(builtin_on && builtin_on->status != content::RegistryStatus::disabled);

    const int fetches = count_target(server, catalogue_path);
    OA_CHECK(service.set_registry_enabled("ridge-maps", false, &why) == registry::Refusal::none);
    const registry::PlayerRegistries added_off = load_player(scratch);
    OA_CHECK(added_off.registries.size() == 1 && added_off.registries[0].enabled == false);
    const auto hidden = service.snapshot();
    OA_CHECK(hidden && hidden->entries.empty());
    threads::sleep_ms(200);
    OA_CHECK(count_target(server, catalogue_path) == fetches);

    OA_CHECK(service.set_registry_enabled("ridge-maps", true, &why) == registry::Refusal::none);
    const auto started = std::chrono::steady_clock::now();
    while (count_target(server, catalogue_path) == fetches &&
           std::chrono::steady_clock::now() - started < std::chrono::seconds(8))
        threads::sleep_ms(20);
    OA_CHECK(count_target(server, catalogue_path) > fetches);
    const registry::PlayerRegistries added_on = load_player(scratch);
    OA_CHECK(added_on.registries.size() == 1 && added_on.registries[0].enabled == true);
}

void test_mirror() {
    g_now = kEpoch;
    const TestKey builtin_key("core-2026", 0x11);
    const TestKey key("ridge-2026", 0x27);
    const TestKey other("other-2026", 0x28);
    fixture::Server server;
    fixture::Server mirror;
    if (!listening(server) || !listening(mirror))
        return;
    Scratch scratch("f23-mirror");
    install_builtin(scratch, builtin_key);
    serve_registry(
        server,
        key,
        "ridge-maps",
        "Ridge Maps",
        "/oa/registry.yaml",
        server.url("/v1/catalogue.json"),
        "/v1/catalogue.json",
        nullptr
    );
    const SignedBytes same = sign_json(catalogue_json("ridge-maps", 1), key);
    serve_document(mirror, "/v1/catalogue.json", same);
    const SignedBytes forged = sign_json(catalogue_json("ridge-maps", 1), other);
    serve_document(mirror, "/forged/catalogue.json", forged);

    content::Service service(options_for(scratch));
    service.start();
    const uint64_t added = service.check_registry_url(server.url("/oa/registry.yaml"));
    OA_CHECK(await(service, added).has_value());
    std::string why;
    OA_CHECK(service.add_checked_registry(added, &why) == registry::Refusal::none);

    const std::string mirror_url = mirror.url("/v1/catalogue.json");
    const uint64_t good = service.check_mirror("ridge-maps", mirror_url);
    const std::optional<content::RegistryCheck> good_check = await(service, good);
    expect_refusal(good_check, registry::Refusal::none);
    OA_CHECK(good_check && good_check->url == mirror_url);
    OA_CHECK(
        good_check && good_check->address == url::host_header(*url::parse_http_url(mirror_url))
    );
    OA_CHECK(good_check && good_check->descriptor && good_check->descriptor->id == "ridge-maps");
    OA_CHECK(good_check && good_check->descriptor->name == "Ridge Maps");
    OA_CHECK(service.add_checked_mirror(good, &why) == registry::Refusal::none);
    const registry::PlayerRegistries player = load_player(scratch);
    OA_CHECK(player.registries.size() == 1);
    OA_CHECK(
        player.registries.size() == 1 && player.registries[0].player_mirrors.size() == 1 &&
        url::url_text(player.registries[0].player_mirrors[0]) == mirror_url
    );
    const auto snapshot = service.snapshot();
    const content::RegistryView* view = snapshot ? find_view(*snapshot, "ridge-maps") : nullptr;
    OA_CHECK(view && view->registry.player_mirrors.size() == 1);

    const std::string forged_url = mirror.url("/forged/catalogue.json");
    const uint64_t bad = service.check_mirror("ridge-maps", forged_url);
    const std::optional<content::RegistryCheck> bad_check = await(service, bad);
    expect_refusal(bad_check, registry::Refusal::unreadable);
    OA_CHECK(bad_check && bad_check->descriptor && bad_check->descriptor->id == "ridge-maps");
    OA_CHECK(bad_check && bad_check->url == forged_url);
    OA_CHECK(bad_check && bad_check->address == url::host_header(*url::parse_http_url(forged_url)));
    OA_CHECK(
        bad_check && bad_check->detail == catalogue::verdict_text(catalogue::Verdict::unknown_key)
    );
    OA_CHECK(service.add_checked_mirror(bad, &why) != registry::Refusal::none);
    OA_CHECK(load_player(scratch).registries[0].player_mirrors.size() == 1);

    const uint64_t builtin = service.check_mirror("core-example", mirror_url);
    const std::optional<content::RegistryCheck> builtin_check = await(service, builtin);
    expect_refusal(builtin_check, registry::Refusal::built_in_mirror);
    OA_CHECK(
        builtin_check && builtin_check->descriptor &&
        builtin_check->descriptor->id == "core-example"
    );
    OA_CHECK(builtin_check && builtin_check->url == mirror_url);
    OA_CHECK(
        builtin_check &&
        builtin_check->address == url::host_header(*url::parse_http_url(mirror_url))
    );
    OA_CHECK(service.add_checked_mirror(builtin, &why) == registry::Refusal::built_in_mirror);
    OA_CHECK(load_player(scratch).registries.size() == 1);
    OA_CHECK(load_player(scratch).registries[0].player_mirrors.size() == 1);
}

void test_malformed_file() {
    g_now = kEpoch;
    const TestKey key("ridge-2026", 0x29);
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f23-bad-file");
    const std::string catalogue_url = server.url("/v1/catalogue.json");
    serve_registry(
        server,
        key,
        "ridge-maps",
        "Ridge Maps",
        "/oa/registry.yaml",
        catalogue_url,
        "/v1/catalogue.json",
        nullptr
    );
    const std::string original = "not a registry file\n";
    OA_CHECK(write_text(registries_file(scratch), original));
    content::Service service(options_for(scratch));
    service.start();
    const auto snapshot = service.snapshot();
    OA_CHECK(snapshot && !snapshot->registries_file_error.empty());
    const std::vector<uint8_t> before = file_bytes(registries_file(scratch));
    OA_CHECK(std::string(before.begin(), before.end()) == original);

    const uint64_t request = service.check_registry_url(server.url("/oa/registry.yaml"));
    const std::optional<content::RegistryCheck> check = await(service, request);
    expect_refusal(check, registry::Refusal::none);
    std::string why = "unchanged";
    OA_CHECK(service.add_checked_registry(request, &why) == registry::Refusal::unreadable);
    OA_CHECK(snapshot && why == snapshot->registries_file_error);

    why = "unchanged";
    OA_CHECK(service.remove_registry("ridge-maps", &why) == registry::Refusal::unreadable);
    OA_CHECK(snapshot && why == snapshot->registries_file_error);

    why = "unchanged";
    OA_CHECK(
        service.set_registry_enabled("ridge-maps", false, &why) == registry::Refusal::unreadable
    );
    OA_CHECK(snapshot && why == snapshot->registries_file_error);

    const uint64_t mirror = service.check_mirror("ridge-maps", server.url("/v1/catalogue.json"));
    OA_CHECK(await(service, mirror).has_value());
    why = "unchanged";
    OA_CHECK(service.add_checked_mirror(mirror, &why) == registry::Refusal::unreadable);
    OA_CHECK(snapshot && why == snapshot->registries_file_error);

    OA_CHECK(file_bytes(registries_file(scratch)) == before);
}

} // namespace

int main() {
    test_add_by_url();
    test_add_from_file();
    test_refusals();
    test_remove_and_enable();
    test_mirror();
    test_malformed_file();
    return oa::test::check_exit_status();
}
