// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The content service against a local fixture: cache, mirrors, expiry,
// key rotation, the player's setting, and the worker.
#include "oa/app/content/service.hpp"

#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/data/registry/player_registries.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/http/client.hpp"
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
namespace http = oa::netgame::http;
namespace fixture = oa::netgame::http_fixture;
namespace sha = oa::base::sha256;
namespace signing = oa::base::signing;
namespace threads = oa::base::threads;
namespace url = oa::formats::url;
namespace fs = std::filesystem;

/// 2026-01-01T00:00:00Z. Catalogues in these tests expire on 2026-02-01.
constexpr int64_t kEpoch = 1767225600;

std::atomic<int64_t> g_now{kEpoch};

/// The test clock, in seconds since 1970.
int64_t test_clock() {
    return g_now.load();
}

/// A condition the test thread waits on, with a bound a timer thread applies.
struct Gate {
    threads::Mutex mutex;
    threads::ConditionVariable cv;
    bool open = false;
    bool give_up = false;
};

/// Opens a gate.
void signal(Gate& gate) {
    threads::LockGuard guard(gate.mutex);
    gate.open = true;
    gate.cv.notify_all();
}

/// Arguments of the thread that ends a wait.
struct TimerArg {
    Gate* gate = nullptr;
    uint32_t bound_ms = 0;
    std::atomic<bool>* cancel = nullptr;
};

/// Opens the give-up flag when the bound passes.
void timer_main(void* argument) {
    auto* const timer = static_cast<TimerArg*>(argument);
    uint32_t left = timer->bound_ms;
    while (left > 0 && !timer->cancel->load()) {
        const uint32_t slice = left > 20 ? 20 : left;
        threads::sleep_ms(slice);
        left -= slice;
    }
    if (timer->cancel->load())
        return;
    threads::LockGuard guard(timer->gate->mutex);
    timer->gate->give_up = true;
    timer->gate->cv.notify_all();
}

/// Waits until the gate opens or the bound passes.
bool wait_for(Gate& gate, uint32_t bound_ms) {
    std::atomic<bool> cancel{false};
    TimerArg timer{&gate, bound_ms, &cancel};
    threads::Thread thread{};
    if (!threads::start_thread(thread, timer_main, &timer)) {
        OA_CHECK(false);
        return false;
    }
    bool opened = false;
    {
        threads::LockGuard guard(gate.mutex);
        gate.cv.wait(gate.mutex, [&] { return gate.open || gate.give_up; });
        opened = gate.open;
    }
    cancel.store(true);
    threads::join_thread(thread);
    return opened;
}

/// Arguments of the thread that watches a fixture for a request.
struct RequestWait {
    const fixture::Server* server = nullptr;
    Gate gate{};
    std::atomic<bool> cancel{false};
    uint32_t bound_ms = 0;
    threads::Thread thread{};
};

/// Signals when the fixture has logged a request, or gives up at the bound.
void request_wait_main(void* argument) {
    auto* const wait = static_cast<RequestWait*>(argument);
    uint32_t left = wait->bound_ms;
    while (left > 0 && !wait->cancel.load()) {
        if (!wait->server->requests().empty()) {
            signal(wait->gate);
            return;
        }
        const uint32_t slice = left > 20 ? 20 : left;
        threads::sleep_ms(slice);
        left -= slice;
    }
    if (wait->cancel.load())
        return;
    threads::LockGuard guard(wait->gate.mutex);
    wait->gate.give_up = true;
    wait->gate.cv.notify_all();
}

/// Waits until the fixture logs a request or the bound passes.
bool wait_for_request(const fixture::Server& server, uint32_t bound_ms) {
    RequestWait wait;
    wait.server = &server;
    wait.bound_ms = bound_ms;
    if (!threads::start_thread(wait.thread, request_wait_main, &wait)) {
        OA_CHECK(false);
        return false;
    }
    bool arrived = false;
    {
        threads::LockGuard guard(wait.gate.mutex);
        wait.gate.cv.wait(wait.gate.mutex, [&] { return wait.gate.open || wait.gate.give_up; });
        arrived = wait.gate.open;
    }
    wait.cancel.store(true);
    threads::join_thread(wait.thread);
    return arrived;
}

/// Waits until a job queued behind the current work has run.
bool wait_job(content::Service& service, uint32_t bound_ms = 8000) {
    Gate gate;
    service.run_on_worker([&](content::WorkerTools&) { signal(gate); });
    return wait_for(gate, bound_ms);
}

/// Log lines collected from the worker.
struct Logs {
    threads::Mutex mutex;
    std::string text;
};

/// Appends one log line.
void remember_log(void* context, std::string_view line) {
    auto* const logs = static_cast<Logs*>(context);
    threads::LockGuard guard(logs->mutex);
    logs->text.append(line);
    logs->text.push_back('\n');
}

/// Copies the log.
std::string logs_text(Logs& logs) {
    threads::LockGuard guard(logs.mutex);
    return logs.text;
}

/// Checks that the log contains a phrase.
void expect_log(Logs& logs, std::string_view phrase) {
    const std::string text = logs_text(logs);
    if (text.find(phrase) == std::string::npos) {
        std::fprintf(
            stderr, "missing log phrase: %s\nlog:\n%s\n", std::string(phrase).c_str(), text.c_str()
        );
    }
    OA_CHECK(text.find(phrase) != std::string::npos);
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

/// Fields of one catalogue these tests serve.
struct CatalogueSpec {
    std::string registry;
    std::string package_id = "ridge";
    std::string package_name = "Ridge";
    std::string bytes_label = "ridge-bytes";
    std::string generated = "2026-01-01T00:00:00Z";
    std::string expires = "2026-02-01T00:00:00Z";
    int sequence = 1;
    const registry::RegistryKey* publish = nullptr;
};

/// The lower-case hex of a short label, used as a package digest.
std::string hex_of(std::string_view text) {
    const auto digest = sha::digest_of(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size())
    );
    const auto hex = sha::to_hex(digest);
    return std::string(hex.data(), hex.size());
}

/// Builds one catalogue document.
std::string catalogue_json(const CatalogueSpec& spec) {
    const std::string hash = hex_of(spec.bytes_label);
    std::string json = "{\"catalogue\":1,\"registry\":\"";
    json += spec.registry;
    json += "\",\"sequence\":";
    json += std::to_string(spec.sequence);
    json += ",\"generated\":\"";
    json += spec.generated;
    json += "\",\"expires\":\"";
    json += spec.expires;
    json += '"';
    if (spec.publish != nullptr) {
        json += ",\"keys\":[{\"id\":\"";
        json += spec.publish->id;
        json += "\",\"public\":\"";
        json += std::string(signing::public_key_text(spec.publish->key).view());
        json += "\"}]";
    }
    json += ",\"packages\":[{\"kind\":\"oamod\",\"id\":\"";
    json += spec.package_id;
    json += "\",\"name\":\"";
    json += spec.package_name;
    json += "\",\"version\":\"1\",\"revision\":1,\"release\":1,\"size\":11,\"sha256\":\"";
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
    if (!in) {
        OA_CHECK(false);
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The modification time of a file.
fs::file_time_type write_time(const fs::path& path) {
    std::error_code error;
    const auto time = fs::last_write_time(path, error);
    OA_CHECK(!error);
    return time;
}

/// Builds a descriptor. A null key leaves the registry unsigned.
registry::Descriptor descriptor_for(
    std::string id,
    std::string name,
    std::string catalogue_url,
    const TestKey* key,
    std::vector<std::string> mirrors = {}
) {
    registry::Descriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.name = std::move(name);
    const auto parsed_catalogue = url::parse_http_url(catalogue_url);
    OA_CHECK(parsed_catalogue.has_value());
    if (parsed_catalogue)
        descriptor.catalogue = *parsed_catalogue;
    for (const std::string& mirror : mirrors) {
        const auto parsed = url::parse_http_url(mirror);
        OA_CHECK(parsed.has_value());
        if (parsed)
            descriptor.mirrors.push_back(*parsed);
    }
    if (key != nullptr)
        descriptor.keys.push_back(key->key);
    return descriptor;
}

/// Writes a built-in descriptor.
bool install_builtin(
    const Scratch& scratch, const std::string& filename, const registry::Descriptor& descriptor
) {
    const bool wrote =
        write_text(scratch.builtin() / filename, registry::descriptor_text(descriptor));
    OA_CHECK(wrote);
    return wrote;
}

/// Writes Registries.yaml.
bool install_player(const Scratch& scratch, const registry::PlayerRegistries& player) {
    const fs::path file = scratch.player() / std::string(registry::player_registries_file);
    const bool wrote = write_text(file, registry::player_registries_text(player));
    OA_CHECK(wrote);
    return wrote;
}

/// An added registry dated 2026-01-01.
registry::AddedRegistry added_registry(registry::Descriptor descriptor) {
    registry::AddedRegistry added;
    added.descriptor = std::move(descriptor);
    added.added = "2026-01-01";
    added.enabled = true;
    return added;
}

/// Options pointed at a scratch directory and the test clock.
content::ServiceOptions options_for(const Scratch& scratch, Logs& logs) {
    content::ServiceOptions options;
    options.data_folder = scratch.data();
    options.player_folder = scratch.player();
    options.builtin_folder = scratch.builtin();
    options.log = remember_log;
    options.log_context = &logs;
    options.clock = test_clock;
    return options;
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

/// Counts requests whose target is exactly this path.
int count_target(const fixture::Server& server, std::string_view target) {
    int count = 0;
    for (const fixture::LoggedRequest& request : server.requests()) {
        if (request.target == target)
            ++count;
    }
    return count;
}

/// Checks how many times a path was requested.
void expect_count(const fixture::Server& server, std::string_view target, int expected) {
    const int count = count_target(server, target);
    if (count != expected) {
        std::fprintf(
            stderr, "target %s count %d wanted %d\n", std::string(target).c_str(), count, expected
        );
        for (const fixture::LoggedRequest& request : server.requests())
            std::fprintf(stderr, "  saw %s\n", request.target.c_str());
    }
    OA_CHECK(count == expected);
}

/// Finds a registry in a snapshot.
const content::RegistryView* find_view(const content::Snapshot& snapshot, std::string_view id) {
    for (const content::RegistryView& view : snapshot.registries) {
        if (view.registry.descriptor.id == id)
            return &view;
    }
    return nullptr;
}

/// Checks one registry's status and returns its view.
const content::RegistryView* expect_status(
    const content::Snapshot& snapshot, std::string_view id, content::RegistryStatus status
) {
    const content::RegistryView* view = find_view(snapshot, id);
    OA_CHECK(view != nullptr);
    if (view == nullptr)
        return nullptr;
    if (view->status != status) {
        std::fprintf(
            stderr,
            "registry %s status %d error %s\n",
            std::string(id).c_str(),
            static_cast<int>(view->status),
            view->last_error.c_str()
        );
    }
    OA_CHECK(view->status == status);
    return view;
}

/// Checks the cache files of one accepted catalogue.
void expect_cached(
    const Scratch& scratch,
    std::string_view id,
    const SignedBytes& document,
    int sequence,
    int64_t checked,
    const std::string& source
) {
    const fs::path directory = scratch.data() / "content" / "catalogues" / std::string(id);
    const std::vector<uint8_t> catalogue_file = file_bytes(directory / "catalogue.json");
    const std::vector<uint8_t> signature_file = file_bytes(directory / "catalogue.json.sig");
    OA_CHECK(catalogue_file == document.catalogue);
    OA_CHECK(signature_file == document.signature);
    const std::vector<uint8_t> note = file_bytes(directory / "state.yaml");
    const std::string text(note.begin(), note.end());
    const auto digest = sha::digest_of(std::span<const uint8_t>(document.catalogue));
    const auto hex = sha::to_hex(digest);
    const std::string sha_text(hex.data(), hex.size());
    const bool sequence_ok =
        text.find("sequence: " + std::to_string(sequence) + "\n") != std::string::npos;
    const bool sha_ok = text.find("sha256: \"" + sha_text + "\"") != std::string::npos;
    const bool checked_ok =
        text.find("checked: " + std::to_string(checked) + "\n") != std::string::npos;
    const bool source_ok = text.find("source: \"" + source + "\"") != std::string::npos;
    if (!sequence_ok || !sha_ok || !checked_ok || !source_ok)
        std::fprintf(stderr, "state:\n%s\nwanted source %s\n", text.c_str(), source.c_str());
    OA_CHECK(sequence_ok);
    OA_CHECK(sha_ok);
    OA_CHECK(checked_ok);
    OA_CHECK(source_ok);
}

void test_no_registries() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f08-none");
    Logs logs;
    content::Service service(options_for(scratch, logs));
    OA_CHECK(service.generation() == 0);
    OA_CHECK(service.content_folder() == scratch.data() / "content");
    service.start();
    const auto snapshot = service.snapshot();
    OA_CHECK(snapshot != nullptr);
    if (snapshot) {
        OA_CHECK(snapshot->registries.empty());
        OA_CHECK(snapshot->entries.empty());
        OA_CHECK(snapshot->registries_file_error.empty());
    }
    OA_CHECK(service.generation() == 1);
    OA_CHECK(!wait_for_request(server, 500));
    OA_CHECK(server.requests().empty());
    OA_CHECK(service.generation() == 1);
}

void test_refresh_before_start() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    const SignedBytes document = sign_json(catalogue_json(spec), key);
    Gate entered;
    server.handle("GET", "/v1/catalogue.json", [&](const fixture::LoggedRequest&) {
        signal(entered);
        fixture::Reply reply;
        reply.body = document.catalogue;
        return reply;
    });
    Scratch scratch("f08-before");
    install_builtin(
        scratch,
        "ridge.yaml",
        descriptor_for("ridge", "Ridge", server.url("/v1/catalogue.json"), &key)
    );
    Logs logs;
    content::Service service(options_for(scratch, logs));
    OA_CHECK(service.generation() == 0);
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(service.generation() == 0);
    OA_CHECK(!wait_for(entered, 500));
    OA_CHECK(server.requests().empty());
}

void test_automatic_off() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    const SignedBytes document = sign_json(catalogue_json(spec), key);
    serve_document(server, "/v1/catalogue.json", document);
    Scratch scratch("f08-manual");
    install_builtin(
        scratch,
        "ridge.yaml",
        descriptor_for("ridge", "Ridge", server.url("/v1/catalogue.json"), &key)
    );
    Logs logs;
    content::ServiceOptions options = options_for(scratch, logs);
    options.automatic = false;
    content::Service service(std::move(options));
    service.start();
    const auto snapshot = service.snapshot();
    const content::RegistryView* view =
        snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::never_fetched)
                 : nullptr;
    OA_CHECK(view != nullptr && !view->refreshing);
    OA_CHECK(!wait_for_request(server, 500));
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(wait_job(service));
    const auto after = service.snapshot();
    if (after)
        expect_status(*after, "ridge", content::RegistryStatus::fresh);
    expect_count(server, "/v1/catalogue.json", 1);
}

void test_builtin_cache_and_offline() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    spec.sequence = 5;
    const SignedBytes document = sign_json(catalogue_json(spec), key);
    const std::string path = "/v1/catalogue.json";
    const std::string source = server.url(path);
    serve_document(server, path, document);
    Scratch scratch("f08-cache");
    install_builtin(scratch, "ridge.yaml", descriptor_for("ridge", "Ridge", source, &key));
    {
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        OA_CHECK(wait_job(service));
        const auto snapshot = service.snapshot();
        OA_CHECK(snapshot != nullptr);
        if (!snapshot)
            return;
        const content::RegistryView* view =
            expect_status(*snapshot, "ridge", content::RegistryStatus::fresh);
        OA_CHECK(view != nullptr);
        if (view == nullptr)
            return;
        OA_CHECK(view->catalogue != nullptr);
        OA_CHECK(snapshot->entries.size() == 1);
        if (!snapshot->entries.empty()) {
            OA_CHECK(snapshot->entries[0].registry == "ridge");
            OA_CHECK(snapshot->entries[0].reviewed);
            OA_CHECK(snapshot->entries[0].package != nullptr);
            if (snapshot->entries[0].package)
                OA_CHECK(snapshot->entries[0].package->id == "ridge");
        }
        expect_log(logs, "accepted, sequence 5");
        expect_cached(scratch, "ridge", document, 5, kEpoch, source);
        server.clear_log();
        service.start();
        OA_CHECK(!wait_for_request(server, 500));
    }
    server.stop();
    {
        g_now = kEpoch + 3600;
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        const auto snapshot = service.snapshot();
        OA_CHECK(snapshot != nullptr);
        if (!snapshot)
            return;
        const content::RegistryView* view =
            expect_status(*snapshot, "ridge", content::RegistryStatus::fresh);
        OA_CHECK(view != nullptr && view->catalogue != nullptr && !view->refreshing);
        OA_CHECK(!snapshot->entries.empty());
        OA_CHECK(logs_text(logs).find("cannot be reached") == std::string::npos);
        OA_CHECK(service.generation() == 1);
    }
    {
        g_now = kEpoch + 7 * 60 * 60;
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        OA_CHECK(wait_job(service));
        const auto snapshot = service.snapshot();
        OA_CHECK(snapshot != nullptr);
        if (!snapshot)
            return;
        const content::RegistryView* view =
            expect_status(*snapshot, "ridge", content::RegistryStatus::failed);
        OA_CHECK(view != nullptr && view->catalogue != nullptr);
        if (view != nullptr) {
            OA_CHECK(view->catalogue->sequence == 5);
            OA_CHECK(view->last_error.find("cannot be reached") != std::string::npos);
            OA_CHECK(
                view->last_error.find("the connection could not be opened") != std::string::npos
            );
        }
        OA_CHECK(snapshot->entries.size() == 1);
        expect_cached(scratch, "ridge", document, 5, kEpoch, source);
    }
}

void test_unchanged_and_sequence() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    const std::string path = "/v1/catalogue.json";
    CatalogueSpec spec;
    spec.registry = "ridge";
    spec.sequence = 5;
    const SignedBytes original = sign_json(catalogue_json(spec), key);
    serve_document(server, path, original);
    Scratch scratch("f08-sequence");
    install_builtin(
        scratch, "ridge.yaml", descriptor_for("ridge", "Ridge", server.url(path), &key)
    );
    Logs logs;
    content::Service service(options_for(scratch, logs));
    std::atomic<int> heard{0};
    service.on_refreshed([&](const content::RegistryView&, content::WorkerTools&) {
        heard.fetch_add(1);
    });
    service.start();
    OA_CHECK(wait_job(service));
    OA_CHECK(heard.load() == 1);
    const fs::path catalogue_file =
        scratch.data() / "content" / "catalogues" / "ridge" / "catalogue.json";
    const auto kept_time = write_time(catalogue_file);
    const std::vector<uint8_t> kept_bytes = file_bytes(catalogue_file);

    g_now = kEpoch + 10;
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(wait_job(service));
    OA_CHECK(heard.load() == 2);
    OA_CHECK(write_time(catalogue_file) == kept_time);
    OA_CHECK(file_bytes(catalogue_file) == kept_bytes);
    const std::vector<uint8_t> note_bytes = file_bytes(catalogue_file.parent_path() / "state.yaml");
    const std::string note(note_bytes.begin(), note_bytes.end());
    OA_CHECK(note.find("checked: " + std::to_string(kEpoch + 10)) != std::string::npos);
    expect_log(logs, "unchanged, sequence 5");

    spec.sequence = 4;
    serve_document(server, path, sign_json(catalogue_json(spec), key));
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(wait_job(service));
    {
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::failed) : nullptr;
        OA_CHECK(view != nullptr && view->catalogue != nullptr && view->catalogue->sequence == 5);
        OA_CHECK(!snapshot->entries.empty());
    }
    OA_CHECK(file_bytes(catalogue_file) == original.catalogue);
    OA_CHECK(heard.load() == 2);
    expect_log(logs, "the sequence went backwards");

    spec.sequence = 5;
    spec.package_name = "Other";
    serve_document(server, path, sign_json(catalogue_json(spec), key));
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(wait_job(service));
    {
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::failed) : nullptr;
        OA_CHECK(view != nullptr && view->catalogue != nullptr && view->catalogue->sequence == 5);
    }
    OA_CHECK(file_bytes(catalogue_file) == original.catalogue);
    OA_CHECK(heard.load() == 2);
    expect_log(logs, "the sequence did not advance");
}

void test_mirrors() {
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    spec.sequence = 3;
    const SignedBytes document = sign_json(catalogue_json(spec), key);
    const std::string path = "/v1/catalogue.json";

    {
        g_now = kEpoch;
        fixture::Server primary;
        fixture::Server mirror;
        if (!listening(primary) || !listening(mirror))
            return;
        fixture::Reply failure;
        failure.status = 500;
        primary.override_next(path, failure, 1);
        serve_document(mirror, path, document);
        Scratch scratch("f08-mirror-500");
        install_builtin(
            scratch,
            "ridge.yaml",
            descriptor_for("ridge", "Ridge", primary.url(path), &key, {mirror.url(path)})
        );
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        OA_CHECK(wait_job(service));
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::fresh) : nullptr;
        OA_CHECK(view != nullptr && view->served_from.has_value());
        if (view != nullptr && view->served_from)
            OA_CHECK(url::url_text(*view->served_from) == mirror.url(path));
        expect_log(logs, "the server answered 500");
        expect_log(logs, "accepted, sequence 3");
    }
    {
        g_now = kEpoch;
        fixture::Server primary;
        fixture::Server mirror;
        if (!listening(primary) || !listening(mirror))
            return;
        const TestKey other("release-2026", 0x33);
        const SignedBytes bad = sign_json(catalogue_json(spec), other);
        primary.serve_bytes(path, document.catalogue, "application/json");
        primary.serve_bytes(path + ".sig", bad.signature, "text/plain");
        serve_document(mirror, path, document);
        Scratch scratch("f08-mirror-sig");
        install_builtin(
            scratch,
            "ridge.yaml",
            descriptor_for("ridge", "Ridge", primary.url(path), &key, {mirror.url(path)})
        );
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        OA_CHECK(wait_job(service));
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::fresh) : nullptr;
        OA_CHECK(view != nullptr && view->served_from.has_value());
        if (view != nullptr && view->served_from)
            OA_CHECK(url::url_text(*view->served_from) == mirror.url(path));
        expect_log(logs, "the signature does not match the catalogue");
    }
    {
        g_now = kEpoch;
        fixture::Server primary;
        fixture::Server mirror;
        if (!listening(primary) || !listening(mirror))
            return;
        primary.redirect(path, "http://192.0.2.1/v1/catalogue.json");
        serve_document(mirror, path, document);
        Scratch scratch("f08-mirror-redirect");
        install_builtin(
            scratch,
            "ridge.yaml",
            descriptor_for("ridge", "Ridge", primary.url(path), &key, {mirror.url(path)})
        );
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        OA_CHECK(wait_job(service));
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::fresh) : nullptr;
        OA_CHECK(view != nullptr && view->served_from.has_value());
        if (view != nullptr && view->served_from)
            OA_CHECK(url::url_text(*view->served_from) == mirror.url(path));
        expect_log(logs, "cannot be reached: the redirect was refused");
    }
}

void test_expired() {
    g_now = kEpoch + 86400;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    spec.generated = "2026-01-01T00:00:00Z";
    spec.expires = "2026-01-02T00:00:00Z";
    const SignedBytes document = sign_json(catalogue_json(spec), key);
    serve_document(server, "/v1/catalogue.json", document);
    Scratch scratch("f08-expired");
    install_builtin(
        scratch,
        "ridge.yaml",
        descriptor_for("ridge", "Ridge", server.url("/v1/catalogue.json"), &key)
    );
    Logs logs;
    content::Service service(options_for(scratch, logs));
    service.start();
    OA_CHECK(wait_job(service));
    const auto snapshot = service.snapshot();
    const content::RegistryView* view =
        snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::out_of_date)
                 : nullptr;
    OA_CHECK(view != nullptr && view->expired && view->catalogue != nullptr);
    OA_CHECK(snapshot != nullptr && snapshot->entries.size() == 1);
}

void test_key_rotation() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key_a("release-a", 0x11);
    const TestKey key_b("release-b", 0x22);
    const std::string path = "/v1/catalogue.json";
    CatalogueSpec spec;
    spec.registry = "ridge";
    spec.sequence = 1;
    spec.publish = &key_b.key;
    const SignedBytes first = sign_json(catalogue_json(spec), key_a);
    serve_document(server, path, first);
    Scratch scratch("f08-rotate");
    registry::PlayerRegistries player;
    player.registries.push_back(
        added_registry(descriptor_for("ridge", "Ridge", server.url(path), &key_a))
    );
    install_player(scratch, player);
    Logs logs;
    content::Service service(options_for(scratch, logs));
    service.start();
    OA_CHECK(wait_job(service));
    {
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::fresh) : nullptr;
        OA_CHECK(view != nullptr);
        if (view != nullptr) {
            OA_CHECK(view->registry.origin == registry::Origin::added);
            OA_CHECK(view->registry.descriptor.keys.size() == 1);
            if (!view->registry.descriptor.keys.empty())
                OA_CHECK(view->registry.descriptor.keys[0].key == key_b.key.key);
        }
        OA_CHECK(snapshot != nullptr && snapshot->entries.size() == 1);
        if (snapshot && !snapshot->entries.empty())
            OA_CHECK(!snapshot->entries[0].reviewed);
    }
    registry::PlayerRegistries stored;
    std::string error;
    const registry::LoadResult loaded = registry::load_player_registries(
        scratch.player() / std::string(registry::player_registries_file), stored, &error
    );
    OA_CHECK(loaded == registry::LoadResult::read);
    OA_CHECK(stored.registries.size() == 1);
    if (!stored.registries.empty()) {
        OA_CHECK(stored.registries[0].descriptor.keys.size() == 1);
        if (!stored.registries[0].descriptor.keys.empty()) {
            OA_CHECK(stored.registries[0].descriptor.keys[0].id == "release-b");
            OA_CHECK(stored.registries[0].descriptor.keys[0].key == key_b.key.key);
        }
    }

    spec.sequence = 2;
    spec.publish = nullptr;
    const SignedBytes second = sign_json(catalogue_json(spec), key_b);
    serve_document(server, path, second);
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(wait_job(service));
    {
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::fresh) : nullptr;
        OA_CHECK(view != nullptr && view->catalogue != nullptr && view->catalogue->sequence == 2);
    }

    spec.sequence = 3;
    serve_document(server, path, sign_json(catalogue_json(spec), key_a));
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(wait_job(service));
    {
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "ridge", content::RegistryStatus::failed) : nullptr;
        OA_CHECK(view != nullptr && view->catalogue != nullptr && view->catalogue->sequence == 2);
    }
    const std::vector<uint8_t> cached =
        file_bytes(scratch.data() / "content" / "catalogues" / "ridge" / "catalogue.json");
    OA_CHECK(cached == second.catalogue);
    expect_log(logs, "the signature names a key that is not trusted");
}

void test_unsigned_developer_mode() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    CatalogueSpec spec;
    spec.registry = "local-ridge";
    const std::string json = catalogue_json(spec);
    const std::string path = "/v1/catalogue.json";
    server.serve_bytes(path, {json.begin(), json.end()}, "application/json");
    Scratch scratch("f08-unsigned");
    registry::PlayerRegistries player;
    player.registries.push_back(
        added_registry(descriptor_for("local-ridge", "Local Ridge", server.url(path), nullptr))
    );
    install_player(scratch, player);
    Logs logs;
    content::ServiceOptions options = options_for(scratch, logs);
    options.check = content::CheckForUpdates::never;
    options.developer_mode = false;
    content::Service service(std::move(options));
    service.start();
    {
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(
                           *snapshot, "local-ridge", content::RegistryStatus::needs_developer_mode
                       )
                     : nullptr;
        OA_CHECK(view != nullptr && !view->refreshing);
        OA_CHECK(snapshot != nullptr && snapshot->entries.empty());
    }
    OA_CHECK(!wait_for_request(server, 500));
    service.set_developer_mode(true);
    OA_CHECK(wait_job(service));
    {
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "local-ridge", content::RegistryStatus::fresh)
                     : nullptr;
        OA_CHECK(view != nullptr && view->catalogue != nullptr);
        OA_CHECK(snapshot != nullptr && snapshot->entries.size() == 1);
        if (snapshot && !snapshot->entries.empty())
            OA_CHECK(!snapshot->entries[0].reviewed);
    }
    expect_count(server, path, 1);
    service.set_developer_mode(false);
    {
        const auto snapshot = service.snapshot();
        if (snapshot)
            expect_status(*snapshot, "local-ridge", content::RegistryStatus::needs_developer_mode);
        OA_CHECK(snapshot != nullptr && snapshot->entries.empty());
    }
}

void test_disabled_and_malformed() {
    g_now = kEpoch;
    {
        fixture::Server server;
        if (!listening(server))
            return;
        const TestKey key("release-2026", 0x11);
        CatalogueSpec spec;
        spec.registry = "alpha";
        serve_document(server, "/v1/catalogue.json", sign_json(catalogue_json(spec), key));
        Scratch scratch("f08-disabled");
        install_builtin(
            scratch,
            "alpha.yaml",
            descriptor_for("alpha", "Alpha Ridge", server.url("/v1/catalogue.json"), &key)
        );
        registry::PlayerRegistries player;
        player.disabled.push_back("alpha");
        install_player(scratch, player);
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        const auto snapshot = service.snapshot();
        const content::RegistryView* view =
            snapshot ? expect_status(*snapshot, "alpha", content::RegistryStatus::disabled)
                     : nullptr;
        OA_CHECK(view != nullptr && !view->refreshing);
        OA_CHECK(snapshot != nullptr && snapshot->registries.size() == 1);
        OA_CHECK(snapshot->entries.empty());
        OA_CHECK(!wait_for_request(server, 500));
    }
    {
        fixture::Server server;
        if (!listening(server))
            return;
        const TestKey key("release-2026", 0x11);
        CatalogueSpec spec;
        spec.registry = "alpha";
        const std::string path = "/v1/catalogue.json";
        serve_document(server, path, sign_json(catalogue_json(spec), key));
        Scratch scratch("f08-malformed");
        install_builtin(
            scratch, "alpha.yaml", descriptor_for("alpha", "Alpha Ridge", server.url(path), &key)
        );
        const fs::path file = scratch.player() / std::string(registry::player_registries_file);
        OA_CHECK(write_text(file, "hello: true\n"));
        const std::vector<uint8_t> before = file_bytes(file);
        Logs logs;
        content::Service service(options_for(scratch, logs));
        service.start();
        OA_CHECK(wait_job(service));
        const auto snapshot = service.snapshot();
        OA_CHECK(snapshot != nullptr && !snapshot->registries_file_error.empty());
        if (snapshot)
            expect_status(*snapshot, "alpha", content::RegistryStatus::fresh);
        OA_CHECK(snapshot != nullptr && snapshot->entries.size() == 1);
        OA_CHECK(file_bytes(file) == before);
        expect_count(server, path, 1);
    }
}

void test_merged_registries() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec alpha_spec;
    alpha_spec.registry = "alpha";
    CatalogueSpec beta_spec;
    beta_spec.registry = "beta";
    const std::string alpha_path = "/alpha/catalogue.json";
    const std::string beta_path = "/beta/catalogue.json";
    serve_document(server, alpha_path, sign_json(catalogue_json(alpha_spec), key));
    serve_document(server, beta_path, sign_json(catalogue_json(beta_spec), key));
    Scratch scratch("f08-merge");
    install_builtin(
        scratch, "b-beta.yaml", descriptor_for("beta", "Beta Ridge", server.url(beta_path), &key)
    );
    install_builtin(
        scratch,
        "a-alpha.yaml",
        descriptor_for("alpha", "Alpha Ridge", server.url(alpha_path), &key)
    );
    Logs logs;
    content::Service service(options_for(scratch, logs));
    service.start();
    OA_CHECK(wait_job(service));
    const auto snapshot = service.snapshot();
    OA_CHECK(snapshot != nullptr);
    if (!snapshot)
        return;
    OA_CHECK(snapshot->registries.size() == 2);
    if (snapshot->registries.size() == 2) {
        OA_CHECK(snapshot->registries[0].registry.descriptor.id == "alpha");
        OA_CHECK(snapshot->registries[1].registry.descriptor.id == "beta");
    }
    OA_CHECK(snapshot->entries.size() == 2);
    if (snapshot->entries.size() == 2) {
        OA_CHECK(snapshot->entries[0].registry == "alpha" && snapshot->entries[0].reviewed);
        OA_CHECK(
            snapshot->entries[0].package != nullptr && snapshot->entries[0].package->id == "ridge"
        );
        OA_CHECK(snapshot->entries[1].registry == "beta" && snapshot->entries[1].reviewed);
        OA_CHECK(
            snapshot->entries[1].package != nullptr && snapshot->entries[1].package->id == "ridge"
        );
    }
}

void test_update_setting() {
    const TestKey key("release-2026", 0x11);
    {
        g_now = kEpoch;
        fixture::Server server;
        if (!listening(server))
            return;
        CatalogueSpec alpha_spec;
        alpha_spec.registry = "alpha";
        CatalogueSpec beta_spec;
        beta_spec.registry = "beta";
        const std::string alpha_path = "/alpha/catalogue.json";
        const std::string beta_path = "/beta/catalogue.json";
        serve_document(server, alpha_path, sign_json(catalogue_json(alpha_spec), key));
        serve_document(server, beta_path, sign_json(catalogue_json(beta_spec), key));
        Scratch scratch("f08-never");
        install_builtin(
            scratch,
            "a-alpha.yaml",
            descriptor_for("alpha", "Alpha Ridge", server.url(alpha_path), &key)
        );
        install_builtin(
            scratch,
            "b-beta.yaml",
            descriptor_for("beta", "Beta Ridge", server.url(beta_path), &key)
        );
        Logs logs;
        content::ServiceOptions options = options_for(scratch, logs);
        options.check = content::CheckForUpdates::never;
        content::Service service(std::move(options));
        service.start();
        OA_CHECK(!wait_for_request(server, 500));
        service.refresh(content::RefreshReason::library_opened);
        OA_CHECK(wait_job(service));
        expect_count(server, alpha_path, 0);
        expect_count(server, beta_path, 0);
        service.refresh(content::RefreshReason::check_now, "alpha");
        OA_CHECK(wait_job(service));
        expect_count(server, alpha_path, 1);
        expect_count(server, beta_path, 0);
        const auto snapshot = service.snapshot();
        if (snapshot) {
            expect_status(*snapshot, "alpha", content::RegistryStatus::fresh);
            expect_status(*snapshot, "beta", content::RegistryStatus::never_fetched);
        }
    }
    {
        g_now = kEpoch;
        fixture::Server server;
        if (!listening(server))
            return;
        CatalogueSpec spec;
        spec.registry = "ridge";
        const std::string path = "/v1/catalogue.json";
        serve_document(server, path, sign_json(catalogue_json(spec), key));
        Scratch scratch("f08-library");
        install_builtin(
            scratch, "ridge.yaml", descriptor_for("ridge", "Ridge", server.url(path), &key)
        );
        Logs logs;
        content::ServiceOptions options = options_for(scratch, logs);
        options.check = content::CheckForUpdates::library_only;
        content::Service service(std::move(options));
        service.start();
        OA_CHECK(!wait_for_request(server, 500));
        const int64_t opened = g_now.load();
        service.refresh(content::RefreshReason::library_opened);
        OA_CHECK(wait_job(service));
        expect_count(server, path, 1);
        server.clear_log();
        g_now = opened + 30;
        service.refresh(content::RefreshReason::library_opened);
        OA_CHECK(wait_job(service));
        expect_count(server, path, 0);
        g_now = opened + content::library_refresh_gap_seconds;
        service.refresh(content::RefreshReason::library_opened);
        OA_CHECK(wait_job(service));
        expect_count(server, path, 1);
    }
}

void test_gzip() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    spec.sequence = 8;
    const SignedBytes document = sign_json(catalogue_json(spec), key);
    const std::string path = "/v1/catalogue.json";
    server.handle("GET", path, [&](const fixture::LoggedRequest&) {
        fixture::Reply reply;
        reply.body = document.catalogue;
        reply.gzip = true;
        return reply;
    });
    server.serve_bytes(path + ".sig", document.signature, "text/plain");
    Scratch scratch("f08-gzip");
    const std::string source = server.url(path);
    install_builtin(scratch, "ridge.yaml", descriptor_for("ridge", "Ridge", source, &key));
    Logs logs;
    content::Service service(options_for(scratch, logs));
    service.start();
    OA_CHECK(wait_job(service));
    const auto snapshot = service.snapshot();
    if (snapshot)
        expect_status(*snapshot, "ridge", content::RegistryStatus::fresh);
    expect_cached(scratch, "ridge", document, 8, kEpoch, source);
    expect_log(logs, "accepted, sequence 8");
}

void test_generation() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    const std::string path = "/v1/catalogue.json";
    serve_document(server, path, sign_json(catalogue_json(spec), key));
    Scratch scratch("f08-generation");
    install_builtin(
        scratch, "ridge.yaml", descriptor_for("ridge", "Ridge", server.url(path), &key)
    );
    Logs logs;
    content::ServiceOptions options = options_for(scratch, logs);
    options.check = content::CheckForUpdates::library_only;
    content::Service service(std::move(options));
    OA_CHECK(service.generation() == 0);
    const auto before_start = service.snapshot();
    OA_CHECK(before_start != nullptr);
    OA_CHECK(service.generation() == 0);
    service.start();
    OA_CHECK(service.generation() == 1);
    OA_CHECK(!wait_for_request(server, 500));
    const uint64_t settled_start = service.generation();
    const auto reading = service.snapshot();
    OA_CHECK(reading != nullptr);
    service.set_check_for_updates(content::CheckForUpdates::library_only);
    service.refresh(content::RefreshReason::start);
    OA_CHECK(service.generation() == settled_start);

    const uint64_t before = service.generation();
    service.refresh(content::RefreshReason::check_now);
    OA_CHECK(service.generation() > before);
    const uint64_t queued = service.generation();
    OA_CHECK(wait_job(service));
    OA_CHECK(service.generation() > queued);
    const uint64_t settled = service.generation();
    const auto snapshot = service.snapshot();
    if (snapshot)
        expect_status(*snapshot, "ridge", content::RegistryStatus::fresh);
    service.set_check_for_updates(content::CheckForUpdates::automatically);
    service.refresh(content::RefreshReason::library_opened);
    service.refresh(content::RefreshReason::start);
    OA_CHECK(service.generation() == settled);
}

void test_worker_and_listener() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec good_spec;
    good_spec.registry = "good-ridge";
    const std::string good_path = "/good/catalogue.json";
    const std::string bad_path = "/bad/catalogue.json";
    serve_document(server, good_path, sign_json(catalogue_json(good_spec), key));
    server.handle("GET", bad_path, [](const fixture::LoggedRequest&) {
        fixture::Reply reply;
        reply.status = 500;
        return reply;
    });
    server.serve_bytes("/probe", std::vector<uint8_t>{'o', 'k'}, "text/plain");
    Scratch scratch("f08-worker");
    install_builtin(
        scratch,
        "a-good.yaml",
        descriptor_for("good-ridge", "Good Ridge", server.url(good_path), &key)
    );
    install_builtin(
        scratch, "b-bad.yaml", descriptor_for("bad-ridge", "Bad Ridge", server.url(bad_path), &key)
    );
    Logs logs;
    content::Service service(options_for(scratch, logs));
    std::atomic<int> good_heard{0};
    std::atomic<int> bad_heard{0};
    service.on_refreshed([&](const content::RegistryView& view, content::WorkerTools&) {
        if (view.registry.descriptor.id == "good-ridge" && view.catalogue != nullptr)
            good_heard.fetch_add(1);
        else
            bad_heard.fetch_add(1);
    });
    service.start();
    OA_CHECK(wait_job(service));
    OA_CHECK(good_heard.load() == 1);
    OA_CHECK(bad_heard.load() == 0);
    expect_count(server, good_path, 1);
    expect_count(server, bad_path, 1);

    struct JobNote {
        int catalogue_requests = 0;
        bool saw_catalogue = false;
        bool fetched = false;
        int status = 0;
        fs::path content_folder;
    };

    JobNote note;
    std::atomic<int> phase{0};
    Gate started;
    Gate finished;
    threads::Mutex caller;
    caller.lock();
    const int before = count_target(server, good_path);
    service.refresh(content::RefreshReason::check_now, "good-ridge");
    service.run_on_worker([&](content::WorkerTools& tools) {
        int seen = 0;
        bool saw = false;
        for (const fixture::LoggedRequest& request : server.requests()) {
            if (request.target == good_path) {
                ++seen;
                saw = true;
            }
        }
        phase.store(1);
        signal(started);
        caller.lock();
        note.catalogue_requests = seen;
        note.saw_catalogue = saw;
        note.content_folder = tools.content_folder;
        http::Request request;
        request.url = server.url("/probe");
        request.cancel = tools.cancel;
        request.timeouts.connect_ms = 5000;
        request.timeouts.idle_ms = 5000;
        request.timeouts.total_ms = 10000;
        std::vector<uint8_t> body;
        const http::Response response = tools.http.fetch(request, body);
        note.status = response.status;
        note.fetched =
            response.failure == http::Failure::none && response.status == 200 && body.size() == 2;
        phase.store(2);
        caller.unlock();
        signal(finished);
    });
    OA_CHECK(wait_for(started, 8000));
    OA_CHECK(phase.load() == 1);
    caller.unlock();
    OA_CHECK(wait_for(finished, 8000));
    OA_CHECK(phase.load() == 2);
    OA_CHECK(note.saw_catalogue);
    OA_CHECK(note.catalogue_requests > before);
    OA_CHECK(note.fetched);
    OA_CHECK(note.content_folder == scratch.data() / "content");
    OA_CHECK(service.content_folder() == note.content_folder);
    OA_CHECK(good_heard.load() == 2);
    OA_CHECK(bad_heard.load() == 0);
    expect_count(server, bad_path, 1);

    service.refresh(content::RefreshReason::check_now, "bad-ridge");
    OA_CHECK(wait_job(service));
    OA_CHECK(good_heard.load() == 2);
    OA_CHECK(bad_heard.load() == 0);
    expect_count(server, bad_path, 2);
    expect_count(server, good_path, 2);
}

void test_destructor() {
    g_now = kEpoch;
    fixture::Server server;
    if (!listening(server))
        return;
    const TestKey key("release-2026", 0x11);
    CatalogueSpec spec;
    spec.registry = "ridge";
    const SignedBytes document = sign_json(catalogue_json(spec), key);
    Gate entered;
    server.handle("GET", "/v1/catalogue.json", [&](const fixture::LoggedRequest&) {
        signal(entered);
        fixture::Reply reply;
        reply.body = document.catalogue;
        reply.head_delay_ms = 60000;
        return reply;
    });
    Scratch scratch("f08-stop");
    install_builtin(
        scratch,
        "ridge.yaml",
        descriptor_for("ridge", "Ridge", server.url("/v1/catalogue.json"), &key)
    );
    Logs logs;
    auto service = std::make_unique<content::Service>(options_for(scratch, logs));
    service->start();
    const bool fetching = wait_for(entered, 8000);
    OA_CHECK(fetching);
    if (!fetching) {
        service.reset();
        return;
    }
    std::atomic<bool> job_ran{false};
    service->run_on_worker([&](content::WorkerTools&) { job_ran.store(true); });
    const auto began = std::chrono::steady_clock::now();
    service.reset();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began
    );
    if (elapsed.count() >= 1000)
        std::fprintf(stderr, "destructor took %lld ms\n", static_cast<long long>(elapsed.count()));
    OA_CHECK(elapsed.count() < 1000);
    OA_CHECK(!job_ran.load());
}

} // namespace

int main() {
    test_no_registries();
    test_refresh_before_start();
    test_automatic_off();
    test_builtin_cache_and_offline();
    test_unchanged_and_sequence();
    test_mirrors();
    test_expired();
    test_key_rotation();
    test_unsigned_developer_mode();
    test_disabled_and_malformed();
    test_merged_registries();
    test_update_setting();
    test_gzip();
    test_generation();
    test_worker_and_listener();
    test_destructor();
    return oa::test::check_exit_status();
}
