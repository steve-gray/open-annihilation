// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The download queue against a local fixture: resume, a match, a challenge,
// mirrors and the saved queue.
#include "oa/app/content/download_api.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/app/content/service.hpp"
#include "oa/app/content/settings.hpp"
#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
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
#include <limits>
#include <memory>
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

/// An install ID the key request can carry.
constexpr std::string_view kInstallId = "7f3a90d2-4c1b-4e8a-9d07-c91e00000001";

/// A second install ID, used when the first is replaced.
constexpr std::string_view kInstallIdNext = "a1b2c3d4-e5f6-4789-a012-3456789abcde";

std::atomic<int64_t> g_now{kEpoch};

/// The test clock, in seconds since 1970.
int64_t test_clock() {
    return g_now.load();
}

/// Waits until `pred` is true, or the bound passes.
template <typename Pred>
bool wait_until(Pred pred, uint32_t bound_ms) {
    uint32_t slept = 0;
    while (slept <= bound_ms) {
        if (pred())
            return true;
        if (slept == bound_ms)
            break;
        const uint32_t slice = bound_ms - slept > 20 ? 20 : bound_ms - slept;
        threads::sleep_ms(slice);
        slept += slice;
    }
    return pred();
}

/// Lines the queue and the service logged.
struct Logs {
    threads::Mutex mutex;
    std::string text;
};

void remember_log(void* context, std::string_view line) {
    auto* logs = static_cast<Logs*>(context);
    threads::LockGuard guard(logs->mutex);
    logs->text.append(line);
    logs->text.push_back('\n');
}

std::string logs_text(Logs& logs) {
    threads::LockGuard guard(logs.mutex);
    return logs.text;
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

/// The package bytes a catalogue names.
struct PackageFile {
    std::vector<uint8_t> bytes;
    sha::Digest digest{};
    std::string hex;
    std::string path;
};

/// Bytes of `size` whose SHA-256 the catalogue can name.
PackageFile package_file(std::size_t size, uint8_t fill) {
    PackageFile file;
    file.bytes.resize(size);
    for (std::size_t index = 0; index < size; ++index)
        file.bytes[index] = static_cast<uint8_t>(fill + (index % 251));
    file.digest = sha::digest_of(std::span<const uint8_t>(file.bytes.data(), file.bytes.size()));
    const auto hex = sha::to_hex(file.digest);
    file.hex.assign(hex.begin(), hex.end());
    file.path = "/v1/p/" + file.hex + ".oamod";
    return file;
}

/// One package listed in a catalogue.
struct Listed {
    const PackageFile* file = nullptr;
    std::string id = "ridge";
    std::string name = "Ridge";
    int release = 1;
};

/// Builds one catalogue document.
std::string catalogue_json(
    const std::string& registry_id,
    int sequence,
    const registry::RegistryKey* publish,
    std::span<const Listed> packages
) {
    std::string json = "{\"catalogue\":1,\"registry\":\"";
    json += registry_id;
    json += "\",\"sequence\":";
    json += std::to_string(sequence);
    json += ",\"generated\":\"2026-01-01T00:00:00Z\",\"expires\":\"2026-02-01T00:00:00Z\"";
    if (publish != nullptr) {
        json += ",\"keys\":[{\"id\":\"";
        json += publish->id;
        json += "\",\"public\":\"";
        json += std::string(signing::public_key_text(publish->key).view());
        json += "\"}]";
    }
    json += ",\"packages\":[";
    bool first = true;
    for (const Listed& listed : packages) {
        if (!first)
            json += ',';
        first = false;
        json += "{\"kind\":\"oamod\",\"id\":\"";
        json += listed.id;
        json += "\",\"name\":\"";
        json += listed.name;
        json += "\",\"version\":\"1\",\"revision\":1,\"release\":";
        json += std::to_string(listed.release);
        json += ",\"size\":";
        json += std::to_string(listed.file->bytes.size());
        json += ",\"sha256\":\"";
        json += listed.file->hex;
        json += "\",\"file\":\"";
        json += listed.file->path;
        json += "\"}";
    }
    json += "]}";
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

bool write_bytes(const fs::path& path, std::span<const uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    return static_cast<bool>(out);
}

std::vector<uint8_t> file_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
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

bool install_builtin(
    const Scratch& scratch, const std::string& filename, const registry::Descriptor& descriptor
) {
    const std::string text = registry::descriptor_text(descriptor);
    const bool wrote = write_bytes(
        scratch.builtin() / filename,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size())
    );
    OA_CHECK(wrote);
    return wrote;
}

bool listening(fixture::Server& server) {
    std::string error;
    const bool started = server.start(&error);
    if (!started)
        std::fprintf(stderr, "fixture did not start: %s\n", error.c_str());
    OA_CHECK(started);
    return started;
}

void serve_document(fixture::Server& server, const std::string& path, const SignedBytes& document) {
    server.serve_bytes(path, document.catalogue, "application/json");
    server.serve_bytes(path + ".sig", document.signature, "text/plain");
}

/// Files the hand-off was given.
struct Handed {
    threads::Mutex mutex;
    std::vector<uint64_t> items;
    std::vector<fs::path> files;
    std::vector<content::InstallOrigin> origins;
};

bool record_handoff(
    void* context, uint64_t item, const fs::path& file, const content::InstallOrigin& origin
) {
    auto* handed = static_cast<Handed*>(context);
    threads::LockGuard guard(handed->mutex);
    handed->items.push_back(item);
    handed->files.push_back(file);
    handed->origins.push_back(origin);
    return true;
}

std::size_t handed_count(Handed& handed) {
    threads::LockGuard guard(handed.mutex);
    return handed.items.size();
}

/// One signed registry on a fixture, and the service that has fetched it.
struct Session {
    fixture::Server server;
    fixture::Server mirror;
    Scratch scratch;
    TestKey key;
    PackageFile package;
    std::optional<PackageFile> extra;
    Logs logs;
    Handed handed;
    std::unique_ptr<content::Service> service;

    bool tokens = false;
    bool use_mirror = false;
    bool serve_package = true;
    registry::InstallIdUse install_id = registry::InstallIdUse::none;
    int release = 1;
    int sequence = 1;
    std::string registry_id = "ridge";
    std::string package_id = "ridge";
    std::string extra_id = "vale";

    Session(std::string scratch_name, std::size_t size, uint8_t fill)
        : scratch(std::move(scratch_name)), key("release-2026", fill),
          package(package_file(size, fill)) {}

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    fs::path downloads() const { return content::downloads_folder(scratch.data()); }

    fs::path final_path() const { return downloads() / (package.hex + ".oamod"); }

    fs::path part_path() const { return downloads() / (package.hex + ".oamod.part"); }

    /// Serves the catalogue, starts the service and waits until the package is listed.
    bool open() {
        g_now = kEpoch;
        if (!listening(server))
            return false;
        if (use_mirror && !listening(mirror))
            return false;
        std::error_code error;
        fs::create_directories(downloads(), error);
        const std::string catalogue_path = "/v1/catalogue.json";
        std::vector<Listed> listed;
        listed.push_back(Listed{&package, package_id, "Ridge", release});
        if (extra)
            listed.push_back(Listed{&*extra, extra_id, "Vale", release});
        const SignedBytes document =
            sign_json(catalogue_json(registry_id, sequence, &key.key, listed), key);
        serve_document(server, catalogue_path, document);
        if (use_mirror)
            serve_document(mirror, catalogue_path, document);
        std::vector<std::string> mirrors;
        if (use_mirror)
            mirrors.push_back(mirror.url(catalogue_path));
        registry::Descriptor descriptor = descriptor_for(
            registry_id, "Ridge", server.url(catalogue_path), &key, std::move(mirrors)
        );
        if (tokens) {
            descriptor.mode = registry::DownloadMode::tokens;
            const auto api = url::parse_http_url(server.url("/api/v1"));
            OA_CHECK(api.has_value());
            if (!api)
                return false;
            descriptor.api = *api;
            descriptor.install_id = install_id;
        }
        if (!install_builtin(scratch, registry_id + ".yaml", descriptor))
            return false;
        if (serve_package)
            server.serve_bytes(package.path, package.bytes);
        if (extra)
            server.serve_bytes(extra->path, extra->bytes);
        content::ServiceOptions options;
        options.data_folder = scratch.data();
        options.player_folder = scratch.player();
        options.builtin_folder = scratch.builtin();
        options.log = remember_log;
        options.log_context = &logs;
        options.clock = test_clock;
        service = std::make_unique<content::Service>(std::move(options));
        service->start();
        const bool ready = wait_until(
            [&] {
                const auto snapshot = service->snapshot();
                if (!snapshot)
                    return false;
                for (const content::Entry& entry : snapshot->entries) {
                    if (entry.package != nullptr && entry.package->id == package_id &&
                        entry.package->release == release)
                        return true;
                }
                return false;
            },
            8000
        );
        if (!ready) {
            const auto snapshot = service->snapshot();
            if (snapshot) {
                for (const content::RegistryView& view : snapshot->registries)
                    std::fprintf(
                        stderr,
                        "registry %s status %d error %s\n",
                        view.registry.descriptor.id.c_str(),
                        static_cast<int>(view.status),
                        view.last_error.c_str()
                    );
            }
            std::fprintf(stderr, "catalogue was not ready\n%s\n", logs_text(logs).c_str());
            OA_CHECK(false);
            return false;
        }
        server.clear_log();
        if (use_mirror)
            mirror.clear_log();
        return true;
    }

    content::DownloadsOptions queue_options() {
        content::DownloadsOptions options;
        options.data_folder = scratch.data();
        options.language = "en";
        options.handoff = record_handoff;
        options.handoff_context = &handed;
        options.log = remember_log;
        options.log_context = &logs;
        options.clock = test_clock;
        options.retry_base_ms = 40;
        options.poll_ms = 40;
        return options;
    }

    std::optional<uint64_t>
    enqueue(content::Downloads& downloads, content::DownloadReason reason, std::string* why) {
        const auto snapshot = service->snapshot();
        if (!snapshot)
            return std::nullopt;
        std::optional<content::DownloadTarget> target =
            content::download_target(*snapshot, registry_id, package_id, why);
        if (!target)
            return std::nullopt;
        return downloads.queue(std::move(*target), reason, why);
    }
};

fixture::Reply json_reply(int status, std::string body) {
    fixture::Reply reply;
    reply.status = status;
    reply.body.assign(body.begin(), body.end());
    reply.headers.push_back({"Content-Type", "application/json"});
    return reply;
}

/// A 201 whose package address is this server's copy of the file.
fixture::Reply
issued(const fixture::Server& server, const PackageFile& package, std::string_view expires) {
    const std::string body = std::string("{\"download\":\"ridge-dl-1\",\"expires\":\"") +
                             std::string(expires) + "\",\"url\":\"" + server.url(package.path) +
                             "\"}";
    return json_reply(201, body);
}

int count_method(const fixture::Server& server, std::string_view method, std::string_view path) {
    int count = 0;
    for (const fixture::LoggedRequest& request : server.requests()) {
        if (request.method == method && request.target == path)
            ++count;
    }
    return count;
}

std::string
body_at(const fixture::Server& server, std::string_view method, std::string_view path, int index) {
    int seen = 0;
    for (const fixture::LoggedRequest& request : server.requests()) {
        if (request.method != method || request.target != path)
            continue;
        if (seen == index)
            return std::string(request.body.begin(), request.body.end());
        ++seen;
    }
    return {};
}

int count_results(const fixture::Server& server) {
    int count = 0;
    for (const fixture::LoggedRequest& request : server.requests()) {
        if (request.method == "POST" && request.target.find("/result") != std::string::npos)
            ++count;
    }
    return count;
}

std::string result_body(const fixture::Server& server) {
    std::string last;
    for (const fixture::LoggedRequest& request : server.requests()) {
        if (request.method == "POST" && request.target.find("/result") != std::string::npos)
            last.assign(request.body.begin(), request.body.end());
    }
    return last;
}

bool saw_range(const fixture::Server& server, std::string_view value) {
    for (const fixture::LoggedRequest& request : server.requests()) {
        const std::string* range = request.header("Range");
        if (range != nullptr && *range == value)
            return true;
    }
    return false;
}

/// True when a resume asked for a whole number of steps.
bool saw_step_range(const fixture::Server& server) {
    for (const fixture::LoggedRequest& request : server.requests()) {
        const std::string* range = request.header("Range");
        if (range == nullptr || range->size() < 8 || range->compare(0, 6, "bytes=") != 0)
            continue;
        if (range->back() != '-')
            continue;
        uint64_t start = 0;
        bool digits = false;
        bool ok = true;
        for (std::size_t index = 6; index + 1 < range->size(); ++index) {
            const char character = (*range)[index];
            if (character < '0' || character > '9') {
                ok = false;
                break;
            }
            digits = true;
            start = start * 10 + static_cast<uint64_t>(character - '0');
        }
        if (ok && digits && start > 0 && start % content::download_step_bytes == 0)
            return true;
    }
    return false;
}

std::optional<content::DownloadView> view_of(content::Downloads& downloads, uint64_t id) {
    for (const content::DownloadView& view : downloads.view()) {
        if (view.item == id)
            return view;
    }
    return std::nullopt;
}

bool wait_state(
    content::Downloads& downloads, uint64_t id, content::DownloadState state, uint32_t bound_ms
) {
    return wait_until(
        [&] {
            const auto view = view_of(downloads, id);
            return view.has_value() && view->state == state;
        },
        bound_ms
    );
}

/// Serves the package in pieces, honouring one Range, so a test can pause it.
void serve_paced(
    fixture::Server& server, const PackageFile& package, std::size_t piece, uint32_t delay_ms
) {
    server.handle("GET", package.path, [=](const fixture::LoggedRequest& request) {
        fixture::Reply reply;
        reply.piece_bytes = piece;
        reply.piece_delay_ms = delay_ms;
        const std::string* range = request.header("Range");
        uint64_t start = 0;
        bool partial = false;
        if (range != nullptr && range->compare(0, 6, "bytes=") == 0) {
            partial = true;
            for (std::size_t index = 6; index < range->size(); ++index) {
                const char character = (*range)[index];
                if (character < '0' || character > '9')
                    break;
                start = start * 10 + static_cast<uint64_t>(character - '0');
            }
        }
        if (partial && start < package.bytes.size()) {
            reply.status = 206;
            reply.body.assign(
                package.bytes.begin() + static_cast<std::ptrdiff_t>(start), package.bytes.end()
            );
            reply.headers.push_back(
                {"Content-Range",
                 "bytes " + std::to_string(start) + "-" + std::to_string(package.bytes.size() - 1) +
                     "/" + std::to_string(package.bytes.size())}
            );
        } else {
            reply.status = 200;
            reply.body = package.bytes;
        }
        return reply;
    });
}

std::string expected_key(
    const Session& session, std::optional<std::string> install, content::DownloadReason reason
) {
    content::KeyRequest request;
    request.install = std::move(install);
    request.package = session.package_id;
    request.kind = catalogue::Kind::oamod;
    request.release = session.release;
    request.sha256 = session.package.digest;
    request.engine = std::string(content::engine_version());
    request.platform = std::string(content::platform_name());
    request.arch = std::string(content::arch_name());
    request.language = "en";
    request.reason = reason;
    return content::key_request_body(request);
}

void finish(content::Downloads& downloads, uint64_t id, content::InstallOutcome outcome) {
    OA_CHECK(wait_state(downloads, id, content::DownloadState::installing, 20000));
    if (outcome == content::InstallOutcome::installed)
        g_now = kEpoch + 5;
    downloads.report_install(id, outcome);
}

void test_direct_large() {
    Session session("f10-direct", (3u << 20) + 123, 0x31);
    if (!session.open())
        return;
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    OA_CHECK(!downloads.busy());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::repair, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(downloads.busy());
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 5000));
    OA_CHECK(!downloads.busy());
    OA_CHECK(!fs::exists(session.part_path()));
    OA_CHECK(file_bytes(session.final_path()) == session.package.bytes);
    OA_CHECK(handed_count(session.handed) == 1);
    threads::LockGuard guard(session.handed.mutex);
    OA_CHECK(session.handed.origins[0].registry == "ridge");
    OA_CHECK(session.handed.origins[0].key == "ridge");
    OA_CHECK(session.handed.origins[0].release == 1);
    OA_CHECK(session.handed.origins[0].kind == catalogue::Kind::oamod);
    OA_CHECK(session.handed.origins[0].sha256 == session.package.digest);
    OA_CHECK(session.handed.origins[0].reason == content::DownloadReason::repair);
}

void test_tokens_outcome(content::InstallOutcome outcome, std::string_view word, bool check_body) {
    Session session(std::string("f10-tokens-") + std::string(word), 80, 0x32);
    session.tokens = true;
    session.install_id = registry::InstallIdUse::required;
    if (!session.open())
        return;
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        return issued(session.server, session.package, "2026-02-01T00:00:00Z");
    });
    session.server.handle(
        "POST", "/api/v1/downloads/ridge-dl-1/result", [](const fixture::LoggedRequest&) {
            return json_reply(204, "");
        }
    );
    content::Downloads downloads(session.queue_options());
    downloads.set_install_id(session.registry_id, std::string(kInstallId));
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    finish(downloads, *id, outcome);
    const content::DownloadState want =
        outcome == content::InstallOutcome::installed   ? content::DownloadState::done
        : outcome == content::InstallOutcome::set_aside ? content::DownloadState::cancelled
                                                        : content::DownloadState::failed;
    OA_CHECK(wait_state(downloads, *id, want, 5000));
    OA_CHECK(wait_until([&] { return count_results(session.server) == 1; }, 5000));
    const std::string result = result_body(session.server);
    if (check_body) {
        const std::string wanted = std::string("{\"result\":\"") + std::string(word) +
                                   "\",\"bytes\":" + std::to_string(session.package.bytes.size()) +
                                   ",\"seconds\":5}";
        if (result != wanted)
            std::fprintf(stderr, "result %s\nwanted %s\n", result.c_str(), wanted.c_str());
        OA_CHECK(result == wanted);
        const std::string sent = body_at(session.server, "POST", "/api/v1/downloads", 0);
        const std::string expect =
            expected_key(session, std::string(kInstallId), content::DownloadReason::update);
        if (sent != expect)
            std::fprintf(stderr, "key\n%s\nwanted\n%s\n", sent.c_str(), expect.c_str());
        OA_CHECK(sent == expect);
    } else {
        OA_CHECK(
            result.find(std::string("\"result\":\"") + std::string(word) + "\"") !=
            std::string::npos
        );
    }
    OA_CHECK(count_results(session.server) == 1);
}

void test_cut_then_range() {
    Session session("f10-cut", (3u << 20) + 123, 0x33);
    if (!session.open())
        return;
    fixture::Reply cut;
    cut.status = 200;
    cut.body = session.package.bytes;
    cut.cut_after = (2u << 20) + 10;
    session.server.override_next(session.package.path, std::move(cut), 1);
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 20000));
    OA_CHECK(saw_range(session.server, "bytes=2097152-"));
    OA_CHECK(file_bytes(session.final_path()) == session.package.bytes);
    OA_CHECK(!fs::exists(session.part_path()));
}

void test_whole_answer_to_range() {
    Session session("f10-whole", (3u << 20) + 123, 0x34);
    session.serve_package = false;
    if (!session.open())
        return;
    fixture::Reply cut;
    cut.status = 200;
    cut.body = session.package.bytes;
    cut.cut_after = (2u << 20) + 10;
    fixture::Reply whole;
    whole.status = 200;
    whole.body = session.package.bytes;
    session.server.override_next(session.package.path, std::move(cut), 1);
    session.server.override_next(session.package.path, std::move(whole), 1);
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 20000));
    OA_CHECK(saw_range(session.server, "bytes=2097152-"));
    OA_CHECK(file_bytes(session.final_path()) == session.package.bytes);
}

void test_package_rekey() {
    Session session("f10-rekey", 64, 0x35);
    session.tokens = true;
    session.install_id = registry::InstallIdUse::required;
    if (!session.open())
        return;
    std::atomic<int> posts{0};
    threads::Mutex gate_mutex;
    threads::ConditionVariable gate;
    bool release = false;
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        posts.fetch_add(1);
        return issued(session.server, session.package, "2026-02-01T00:00:00Z");
    });
    session.server.handle("GET", session.package.path, [&](const fixture::LoggedRequest&) {
        if (posts.load() == 1) {
            threads::LockGuard guard(gate_mutex);
            gate.wait(gate_mutex, [&] { return release; });
            fixture::Reply reply;
            reply.status = 403;
            return reply;
        }
        fixture::Reply reply;
        reply.status = 200;
        reply.body = session.package.bytes;
        return reply;
    });
    session.server.handle(
        "POST", "/api/v1/downloads/ridge-dl-1/result", [](const fixture::LoggedRequest&) {
            return json_reply(204, "");
        }
    );
    content::Downloads downloads(session.queue_options());

    /// Unblocks the fixture if the test leaves before the package answer.
    struct ReleaseGate {
        threads::Mutex& mutex;
        threads::ConditionVariable& gate;
        bool& release;

        ReleaseGate(threads::Mutex& mutex_in, threads::ConditionVariable& gate_in, bool& release_in)
            : mutex(mutex_in), gate(gate_in), release(release_in) {}

        ~ReleaseGate() {
            threads::LockGuard guard(mutex);
            release = true;
            gate.notify_all();
        }
    } release_gate{gate_mutex, gate, release};

    downloads.set_install_id(session.registry_id, std::string(kInstallId));
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_until(
        [&] { return count_method(session.server, "GET", session.package.path) >= 1; }, 5000
    ));
    downloads.set_install_id(session.registry_id, std::string(kInstallIdNext));
    {
        threads::LockGuard guard(gate_mutex);
        release = true;
        gate.notify_all();
    }
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 8000));
    OA_CHECK(count_method(session.server, "POST", "/api/v1/downloads") == 2);
    OA_CHECK(
        body_at(session.server, "POST", "/api/v1/downloads", 0).find(kInstallId) !=
        std::string::npos
    );
    const std::string second = body_at(session.server, "POST", "/api/v1/downloads", 1);
    OA_CHECK(
        second ==
        expected_key(session, std::string(kInstallIdNext), content::DownloadReason::update)
    );
    OA_CHECK(count_results(session.server) == 1);
}

void test_key_expires_soon() {
    Session session("f10-expiry", 40, 0x36);
    session.tokens = true;
    session.install_id = registry::InstallIdUse::required;
    if (!session.open())
        return;
    std::atomic<int> posts{0};
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        const int which = posts.fetch_add(1);
        const char* expires = which == 0 ? "2026-01-01T00:00:10Z" : "2026-02-01T00:00:00Z";
        return issued(session.server, session.package, expires);
    });
    content::Downloads downloads(session.queue_options());
    downloads.set_install_id(session.registry_id, std::string(kInstallId));
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_until(
        [&] { return count_method(session.server, "GET", session.package.path) >= 1; }, 5000
    ));
    OA_CHECK(count_method(session.server, "POST", "/api/v1/downloads") == 2);
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 5000));
}

void test_challenge_solved() {
    Session session("f10-solved", 48, 0x37);
    session.tokens = true;
    session.install_id = registry::InstallIdUse::none;
    if (!session.open())
        return;
    std::atomic<int> polls{0};
    std::atomic<bool> solved{false};
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        if (!solved.load()) {
            return json_reply(
                428,
                "{\"error\":\"challenge_required\",\"message\":\"confirm you are a person\","
                "\"challenge\":{\"code\":\"HXQ7-4KP2\","
                "\"verify\":\"https://downloads.example.net/verify?c=HXQ7-4KP2\","
                "\"expires\":\"2026-02-01T00:00:00Z\",\"poll\":3}}"
            );
        }
        return issued(session.server, session.package, "2026-02-01T00:00:00Z");
    });
    session.server.handle(
        "GET", "/api/v1/challenges/HXQ7-4KP2", [&](const fixture::LoggedRequest&) {
            const int count = polls.fetch_add(1);
            if (count >= 1)
                solved.store(true);
            const char* state = count >= 1 ? "solved" : "pending";
            return json_reply(
                200,
                std::string("{\"code\":\"HXQ7-4KP2\",\"state\":\"") + state +
                    "\",\"expires\":\"2026-02-01T00:00:00Z\"}"
            );
        }
    );
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::challenge, 5000));
    const auto shown = view_of(downloads, *id);
    OA_CHECK(shown.has_value() && shown->challenge.has_value());
    if (shown && shown->challenge) {
        OA_CHECK(shown->challenge->code == "HXQ7-4KP2");
        OA_CHECK(shown->challenge->address == "downloads.example.net/verify");
    }
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 8000));
    OA_CHECK(polls.load() >= 2);
    OA_CHECK(count_method(session.server, "GET", "/api/v1/challenges/HXQ7-4KP2") >= 2);
}

void test_challenge_expired() {
    Session session("f10-expired", 32, 0x38);
    session.tokens = true;
    session.use_mirror = true;
    if (!session.open())
        return;
    session.server.handle("POST", "/api/v1/downloads", [](const fixture::LoggedRequest&) {
        return json_reply(
            428,
            "{\"error\":\"challenge_required\",\"message\":\"confirm you are a person\","
            "\"challenge\":{\"code\":\"HXQ7-4KP2\","
            "\"verify\":\"https://downloads.example.net/verify?c=HXQ7-4KP2\","
            "\"expires\":\"2026-02-01T00:00:00Z\",\"poll\":3}}"
        );
    });
    session.server.handle("GET", "/api/v1/challenges/HXQ7-4KP2", [](const fixture::LoggedRequest&) {
        return json_reply(
            200,
            "{\"code\":\"HXQ7-4KP2\",\"state\":\"expired\",\"expires\":\"2026-02-01T00:00:00Z\"}"
        );
    });
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 5000));
    const auto view = view_of(downloads, *id);
    OA_CHECK(view.has_value());
    if (view)
        OA_CHECK(view->failure == content::DownloadFailure::challenge_expired);
    OA_CHECK(count_results(session.server) == 0);
    OA_CHECK(session.mirror.requests().empty());
    OA_CHECK(count_method(session.server, "GET", session.package.path) == 0);
}

void test_challenge_cancel() {
    Session session("f10-cancel-check", 32, 0x39);
    session.tokens = true;
    if (!session.open())
        return;
    std::atomic<int> polls{0};
    session.server.handle("POST", "/api/v1/downloads", [](const fixture::LoggedRequest&) {
        return json_reply(
            428,
            "{\"error\":\"challenge_required\",\"message\":\"confirm you are a person\","
            "\"challenge\":{\"code\":\"HXQ7-4KP2\","
            "\"verify\":\"https://downloads.example.net/verify?c=HXQ7-4KP2\","
            "\"expires\":\"2026-02-01T00:00:00Z\",\"poll\":3}}"
        );
    });
    session.server.handle(
        "GET", "/api/v1/challenges/HXQ7-4KP2", [&](const fixture::LoggedRequest&) {
            polls.fetch_add(1);
            return json_reply(
                200,
                "{\"code\":\"HXQ7-4KP2\",\"state\":\"pending\",\"expires\":\"2026-02-01T00:00:"
                "00Z\"}"
            );
        }
    );
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::challenge, 5000));
    downloads.cancel(*id);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::cancelled, 5000));
    // A poll already on the wire when the cancel lands may still arrive. Let it
    // land, then no other may come in six poll times (40 ms apart here).
    const int cancelled_at = polls.load();
    threads::sleep_ms(100);
    const int stopped = polls.load();
    OA_CHECK(stopped <= cancelled_at + 1);
    threads::sleep_ms(250);
    OA_CHECK(polls.load() == stopped);
    OA_CHECK(count_results(session.server) == 0);
}

void test_not_offered(int status, const char* error) {
    Session session(std::string("f10-offer-") + error, 32, 0x41);
    session.tokens = true;
    session.use_mirror = true;
    session.serve_package = false;
    if (!session.open())
        return;
    session.server.handle("POST", "/api/v1/downloads", [=](const fixture::LoggedRequest&) {
        return json_reply(
            status, std::string("{\"error\":\"") + error + "\",\"message\":\"not this release\"}"
        );
    });
    content::Downloads downloads(session.queue_options());
    downloads.set_install_id(session.registry_id, std::string(kInstallId));
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 5000));
    const auto view = view_of(downloads, *id);
    OA_CHECK(view.has_value());
    if (view)
        OA_CHECK(view->failure == content::DownloadFailure::not_offered);
    threads::sleep_ms(200);
    OA_CHECK(count_method(session.server, "POST", "/api/v1/downloads") == 1);
    OA_CHECK(session.mirror.requests().empty());
}

void test_client_fault(int status) {
    Session session("f10-fault-" + std::to_string(status), 32, static_cast<uint8_t>(status));
    session.tokens = true;
    session.use_mirror = true;
    session.serve_package = false;
    if (!session.open())
        return;
    session.server.handle("POST", "/api/v1/downloads", [=](const fixture::LoggedRequest&) {
        return json_reply(
            status,
            "{\"error\":\"bad_request\",\"message\":\"the package is "
            "missing\",\"field\":\"package\"}"
        );
    });
    content::Downloads downloads(session.queue_options());
    downloads.set_install_id(session.registry_id, std::string(kInstallId));
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 5000));
    const auto view = view_of(downloads, *id);
    OA_CHECK(view.has_value());
    if (view)
        OA_CHECK(view->failure == content::DownloadFailure::refused);
    threads::sleep_ms(200);
    OA_CHECK(count_method(session.server, "POST", "/api/v1/downloads") == 1);
    OA_CHECK(session.mirror.requests().empty());
    OA_CHECK(logs_text(session.logs).find("package") != std::string::npos);
}

void test_rate_limit() {
    Session session("f10-rate", 32, 0x42);
    session.tokens = true;
    if (!session.open())
        return;
    std::atomic<int> posts{0};
    threads::Mutex mutex;
    std::vector<std::chrono::steady_clock::time_point> times;
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        {
            threads::LockGuard guard(mutex);
            times.push_back(std::chrono::steady_clock::now());
        }
        if (posts.fetch_add(1) == 0) {
            fixture::Reply reply =
                json_reply(429, "{\"error\":\"rate_limited\",\"message\":\"wait\"}");
            reply.headers.push_back({"Retry-After", "1"});
            return reply;
        }
        return issued(session.server, session.package, "2026-02-01T00:00:00Z");
    });
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_until([&] { return posts.load() >= 2; }, 8000));
    std::chrono::steady_clock::time_point first{};
    std::chrono::steady_clock::time_point second{};
    {
        threads::LockGuard guard(mutex);
        OA_CHECK(times.size() >= 2);
        if (times.size() >= 2) {
            first = times[0];
            second = times[1];
        }
    }
    const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(second - first).count();
    if (gap < 800)
        std::fprintf(stderr, "retry gap %lld ms\n", static_cast<long long>(gap));
    OA_CHECK(gap >= 800);
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 5000));
}

void test_unavailable_then_mirror() {
    Session session("f10-mirror", 70, 0x43);
    session.tokens = true;
    session.use_mirror = true;
    session.serve_package = false;
    if (!session.open())
        return;
    session.mirror.serve_bytes(session.package.path, session.package.bytes);
    std::atomic<int> posts{0};
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        posts.fetch_add(1);
        return json_reply(503, "{\"error\":\"unavailable\",\"message\":\"later\"}");
    });
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 8000));
    OA_CHECK(posts.load() == 4);
    OA_CHECK(count_method(session.mirror, "GET", session.package.path) >= 1);
    OA_CHECK(file_bytes(session.final_path()) == session.package.bytes);
}

void test_refusal_skips_mirror() {
    Session session("f10-no-mirror", 32, 0x44);
    session.tokens = true;
    session.use_mirror = true;
    session.serve_package = false;
    if (!session.open())
        return;
    session.server.handle("POST", "/api/v1/downloads", [](const fixture::LoggedRequest&) {
        return json_reply(403, "{\"error\":\"refused\",\"message\":\"no\"}");
    });
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 5000));
    threads::sleep_ms(200);
    OA_CHECK(count_method(session.server, "POST", "/api/v1/downloads") == 1);
    OA_CHECK(session.mirror.requests().empty());
}

void test_wrong_hash_once() {
    Session session("f10-hash-once", (1u << 20) + 50, 0x45);
    if (!session.open())
        return;
    std::vector<uint8_t> wrong = session.package.bytes;
    wrong[0] = static_cast<uint8_t>(wrong[0] + 1);
    fixture::Reply once;
    once.status = 200;
    once.body = std::move(wrong);
    session.server.override_next(session.package.path, std::move(once), 1);
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 20000));
    OA_CHECK(count_method(session.server, "GET", session.package.path) >= 2);
    OA_CHECK(file_bytes(session.final_path()) == session.package.bytes);
}

void test_wrong_hash_twice() {
    Session session("f10-hash-twice", (1u << 20) + 50, 0x46);
    session.tokens = true;
    session.install_id = registry::InstallIdUse::required;
    session.serve_package = false;
    if (!session.open())
        return;
    std::vector<uint8_t> wrong = session.package.bytes;
    wrong[0] = static_cast<uint8_t>(wrong[0] + 1);
    fixture::Reply bad;
    bad.status = 200;
    bad.body = wrong;
    session.server.override_next(session.package.path, bad, 1);
    session.server.override_next(session.package.path, std::move(bad), 1);
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        return issued(session.server, session.package, "2026-02-01T00:00:00Z");
    });
    session.server.handle(
        "POST", "/api/v1/downloads/ridge-dl-1/result", [](const fixture::LoggedRequest&) {
            return json_reply(204, "");
        }
    );
    content::Downloads downloads(session.queue_options());
    downloads.set_install_id(session.registry_id, std::string(kInstallId));
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 20000));
    const auto view = view_of(downloads, *id);
    OA_CHECK(view.has_value());
    if (view)
        OA_CHECK(view->failure == content::DownloadFailure::hash_mismatch);
    OA_CHECK(wait_until([&] { return count_results(session.server) == 1; }, 5000));
    OA_CHECK(result_body(session.server).find("\"result\":\"failed\"") != std::string::npos);
}

void test_wrong_size() {
    Session session("f10-size", 100, 0x47);
    if (!session.open())
        return;
    std::vector<uint8_t> wrong = session.package.bytes;
    wrong.push_back(9);
    wrong.push_back(8);
    session.server.serve_bytes(session.package.path, wrong);
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 5000));
    const auto view = view_of(downloads, *id);
    OA_CHECK(view.has_value());
    if (view) {
        if (view->failure != content::DownloadFailure::server_file_differs)
            std::fprintf(
                stderr,
                "size failure %d %s\n",
                static_cast<int>(view->failure),
                view->detail.c_str()
            );
        OA_CHECK(view->failure == content::DownloadFailure::server_file_differs);
    }
    std::error_code error;
    if (fs::exists(session.part_path(), error))
        OA_CHECK(fs::file_size(session.part_path(), error) <= session.package.bytes.size());
    OA_CHECK(!fs::exists(session.final_path()));
}

void test_match_pauses() {
    Session session("f10-match", (3u << 20) + 123, 0x48);
    session.extra = package_file(64, 0x49);
    if (!session.open())
        return;
    serve_paced(session.server, session.package, 256 * 1024, 40);
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    const auto snapshot = session.service->snapshot();
    std::optional<content::DownloadTarget> other =
        content::download_target(*snapshot, session.registry_id, session.extra_id, &why);
    OA_CHECK(other.has_value());
    if (!other)
        return;
    const auto second = downloads.queue(std::move(*other), content::DownloadReason::update, &why);
    OA_CHECK(second.has_value());
    OA_CHECK(wait_until(
        [&] {
            const auto view = view_of(downloads, *id);
            return view && view->state == content::DownloadState::downloading &&
                   view->done >= content::download_step_bytes;
        },
        8000
    ));
    downloads.set_match_running(true);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::paused_for_match, 8000));
    const auto paused = view_of(downloads, *id);
    OA_CHECK(paused.has_value());
    if (paused) {
        OA_CHECK(paused->done % content::download_step_bytes == 0);
        OA_CHECK(paused->done > 0);
    }
    std::error_code error;
    const auto part_size = fs::file_size(session.part_path(), error);
    OA_CHECK(!error);
    OA_CHECK(part_size % content::download_step_bytes == 0);
    const auto waiting = view_of(downloads, *second);
    OA_CHECK(waiting.has_value());
    if (waiting)
        OA_CHECK(waiting->state == content::DownloadState::waiting);
    threads::sleep_ms(150);
    const auto still = view_of(downloads, *second);
    OA_CHECK(still.has_value() && still->state == content::DownloadState::waiting);
    downloads.set_match_running(false);
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 20000));
    OA_CHECK(saw_step_range(session.server));
    finish(downloads, *second, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *second, content::DownloadState::done, 8000));
}

void test_one_at_a_time() {
    Session session("f10-serial", 90, 0x4a);
    session.extra = package_file(70, 0x4b);
    if (!session.open())
        return;
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto first = session.enqueue(downloads, content::DownloadReason::update, &why);
    const auto snapshot = session.service->snapshot();
    auto other = content::download_target(*snapshot, session.registry_id, session.extra_id, &why);
    OA_CHECK(first.has_value() && other.has_value());
    if (!first || !other)
        return;
    const auto second = downloads.queue(std::move(*other), content::DownloadReason::install, &why);
    OA_CHECK(second.has_value());
    OA_CHECK(wait_state(downloads, *first, content::DownloadState::installing, 8000));
    const auto held = view_of(downloads, *second);
    OA_CHECK(held.has_value() && held->state == content::DownloadState::waiting);
    OA_CHECK(downloads.busy());
    downloads.report_install(*first, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *first, content::DownloadState::done, 5000));
    finish(downloads, *second, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *second, content::DownloadState::done, 8000));
    OA_CHECK(!downloads.busy());
}

void test_resume_after_restart() {
    Session session("f10-restart", (2u << 20) + 80, 0x4c);
    if (!session.open())
        return;
    serve_paced(session.server, session.package, 256 * 1024, 40);
    {
        content::Downloads downloads(session.queue_options());
        downloads.start(*session.service->snapshot());
        std::string why;
        const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
        OA_CHECK(id.has_value());
        if (!id)
            return;
        OA_CHECK(wait_until(
            [&] {
                std::error_code error;
                return fs::is_regular_file(session.part_path(), error) &&
                       fs::file_size(session.part_path(), error) >= content::download_step_bytes;
            },
            8000
        ));
    }
    OA_CHECK(fs::is_regular_file(session.part_path()));
    session.server.clear_log();
    content::Downloads again(session.queue_options());
    again.start(*session.service->snapshot());
    OA_CHECK(wait_until([&] { return !again.view().empty(); }, 5000));
    OA_CHECK(!again.view().empty());
    if (again.view().empty())
        return;
    const uint64_t id = again.view().front().item;
    finish(again, id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(again, id, content::DownloadState::done, 20000));
    OA_CHECK(saw_step_range(session.server));
    OA_CHECK(file_bytes(session.final_path()) == session.package.bytes);
}

void test_drop_changed_release() {
    Session session("f10-drop", 60, 0x4d);
    if (!session.open())
        return;
    {
        content::Downloads downloads(session.queue_options());
        downloads.start(*session.service->snapshot());
        std::string why;
        const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
        OA_CHECK(id.has_value());
        const fs::path queue = session.downloads() / std::string(content::queue_file_name);
        OA_CHECK(wait_until(
            [&] {
                const std::vector<uint8_t> bytes = file_bytes(queue);
                const std::string text(bytes.begin(), bytes.end());
                return text.find(session.package_id) != std::string::npos;
            },
            5000
        ));
    }
    Listed listed{&session.package, session.package_id, "Ridge", 2};
    const SignedBytes document = sign_json(
        catalogue_json(session.registry_id, 2, &session.key.key, std::span(&listed, 1)), session.key
    );
    serve_document(session.server, "/v1/catalogue.json", document);
    session.service->refresh(content::RefreshReason::check_now);
    OA_CHECK(wait_until(
        [&] {
            const auto snapshot = session.service->snapshot();
            if (!snapshot)
                return false;
            for (const content::Entry& entry : snapshot->entries) {
                if (entry.package != nullptr && entry.package->id == session.package_id)
                    return entry.package->release == 2;
            }
            return false;
        },
        8000
    ));
    Logs fresh;
    content::DownloadsOptions options = session.queue_options();
    options.log_context = &fresh;
    content::Downloads again(options);
    again.start(*session.service->snapshot());
    OA_CHECK(wait_until(
        [&] { return logs_text(fresh).find("no longer has that release") != std::string::npos; },
        5000
    ));
    OA_CHECK(logs_text(fresh).find("no longer has that release") != std::string::npos);
    OA_CHECK(again.view().empty());
}

void test_install_id_off() {
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f10-id-off");
    content::DownloadTarget target;
    target.registry = "ridge";
    target.registry_name = "Ridge";
    target.key = "ridge";
    target.name = "Ridge";
    target.kind = catalogue::Kind::oamod;
    target.release = 1;
    target.size = 32;
    target.mode = registry::DownloadMode::tokens;
    target.install_id = registry::InstallIdUse::required;
    const auto api = url::parse_http_url(server.url("/api/v1"));
    const auto file = url::parse_http_url(server.url("/v1/p/file.oamod"));
    OA_CHECK(api.has_value() && file.has_value());
    if (!api || !file)
        return;
    target.api = *api;
    target.files.push_back(*file);
    Logs logs;
    Handed handed;
    content::DownloadsOptions options;
    options.data_folder = scratch.data();
    options.language = "en";
    options.handoff = record_handoff;
    options.handoff_context = &handed;
    options.log = remember_log;
    options.log_context = &logs;
    options.clock = test_clock;
    options.retry_base_ms = 40;
    content::Downloads downloads(std::move(options));
    downloads.start(content::Snapshot{});
    std::string why;
    OA_CHECK(!downloads.queue(target, content::DownloadReason::update, &why));
    OA_CHECK(why == content::failure_text(content::DownloadFailure::install_id_off));
    downloads.set_match_running(true);
    downloads.set_install_id("ridge", std::string(kInstallId));
    const auto id = downloads.queue(target, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    downloads.set_install_id("ridge", std::nullopt);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 3000));
    const auto view = view_of(downloads, *id);
    OA_CHECK(view.has_value());
    if (view)
        OA_CHECK(view->failure == content::DownloadFailure::install_id_off);
    OA_CHECK(server.requests().empty());
}

void test_no_install_member() {
    Session session("f10-no-id", 36, 0x4e);
    session.tokens = true;
    session.install_id = registry::InstallIdUse::none;
    if (!session.open())
        return;
    session.server.handle("POST", "/api/v1/downloads", [&](const fixture::LoggedRequest&) {
        return issued(session.server, session.package, "2026-02-01T00:00:00Z");
    });
    content::Downloads downloads(session.queue_options());
    downloads.set_install_id(session.registry_id, std::string(kInstallId));
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_until(
        [&] { return count_method(session.server, "POST", "/api/v1/downloads") >= 1; }, 5000
    ));
    const std::string sent = body_at(session.server, "POST", "/api/v1/downloads", 0);
    OA_CHECK(sent.find("install") == std::string::npos);
    OA_CHECK(sent == expected_key(session, std::nullopt, content::DownloadReason::update));
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 5000));
}

void test_cancel_keeps_part() {
    // Three steps, served 128 KiB each 40 ms: the part holds a whole step for
    // about 640 ms before the download ends, which a busy machine's polls see.
    Session session("f10-cancel", (3u << 20) + 200, 0x4f);
    if (!session.open())
        return;
    serve_paced(session.server, session.package, 128 * 1024, 40);
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_until(
        [&] {
            std::error_code error;
            if (!fs::is_regular_file(session.part_path(), error) || error)
                return false;
            const auto size = fs::file_size(session.part_path(), error);
            return !error && size >= content::download_step_bytes;
        },
        8000
    ));
    downloads.cancel(*id);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::cancelled, 5000));
    std::error_code kept_error;
    const auto kept = fs::file_size(session.part_path(), kept_error);
    OA_CHECK(!kept_error);
    OA_CHECK(kept >= content::download_step_bytes);
    session.server.clear_log();
    const auto again = session.enqueue(downloads, content::DownloadReason::update, &why);
    OA_CHECK(again.has_value());
    if (!again)
        return;
    finish(downloads, *again, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *again, content::DownloadState::done, 20000));
    OA_CHECK(saw_step_range(session.server));
    OA_CHECK(file_bytes(session.final_path()) == session.package.bytes);
}

void test_cancel_registry() {
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f10-cancel-reg");
    TestKey key("release-2026", 0x51);
    const PackageFile ridge = package_file(40, 0x51);
    const PackageFile vale = package_file(40, 0x52);
    Listed ridge_listed{&ridge, "ridge", "Ridge", 1};
    Listed vale_listed{&vale, "vale", "Vale", 1};
    const SignedBytes ridge_doc =
        sign_json(catalogue_json("ridge", 1, &key.key, std::span(&ridge_listed, 1)), key);
    const SignedBytes vale_doc =
        sign_json(catalogue_json("vale", 1, &key.key, std::span(&vale_listed, 1)), key);
    serve_document(server, "/ridge.json", ridge_doc);
    serve_document(server, "/vale.json", vale_doc);
    server.serve_bytes(ridge.path, ridge.bytes);
    server.serve_bytes(vale.path, vale.bytes);
    OA_CHECK(install_builtin(
        scratch, "ridge.yaml", descriptor_for("ridge", "Ridge", server.url("/ridge.json"), &key)
    ));
    OA_CHECK(install_builtin(
        scratch, "vale.yaml", descriptor_for("vale", "Vale", server.url("/vale.json"), &key)
    ));
    Logs logs;
    content::ServiceOptions options;
    options.data_folder = scratch.data();
    options.player_folder = scratch.player();
    options.builtin_folder = scratch.builtin();
    options.log = remember_log;
    options.log_context = &logs;
    options.clock = test_clock;
    content::Service service(std::move(options));
    service.start();
    OA_CHECK(wait_until(
        [&] {
            const auto snapshot = service.snapshot();
            return snapshot && snapshot->entries.size() >= 2;
        },
        8000
    ));
    Handed handed;
    content::DownloadsOptions queue;
    queue.data_folder = scratch.data();
    queue.language = "en";
    queue.handoff = record_handoff;
    queue.handoff_context = &handed;
    queue.log = remember_log;
    queue.log_context = &logs;
    queue.clock = test_clock;
    queue.retry_base_ms = 40;
    content::Downloads downloads(std::move(queue));
    downloads.set_match_running(true);
    downloads.start(*service.snapshot());
    std::string why;
    const auto snapshot = service.snapshot();
    auto ridge_target = content::download_target(*snapshot, "ridge", "ridge", &why);
    auto vale_target = content::download_target(*snapshot, "vale", "vale", &why);
    OA_CHECK(ridge_target.has_value() && vale_target.has_value());
    if (!ridge_target || !vale_target)
        return;
    const auto ridge_id =
        downloads.queue(std::move(*ridge_target), content::DownloadReason::update, &why);
    const auto vale_id =
        downloads.queue(std::move(*vale_target), content::DownloadReason::update, &why);
    OA_CHECK(ridge_id.has_value() && vale_id.has_value());
    downloads.cancel_registry("ridge");
    const auto ridge_view = view_of(downloads, *ridge_id);
    const auto vale_view = view_of(downloads, *vale_id);
    OA_CHECK(ridge_view.has_value() && ridge_view->state == content::DownloadState::cancelled);
    OA_CHECK(vale_view.has_value() && vale_view->state == content::DownloadState::waiting);
    downloads.set_match_running(false);
    finish(downloads, *vale_id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *vale_id, content::DownloadState::done, 8000));
    OA_CHECK(view_of(downloads, *ridge_id)->state == content::DownloadState::cancelled);
}

void test_final_already_there() {
    Session session("f10-present", 55, 0x53);
    if (!session.open())
        return;
    OA_CHECK(write_bytes(session.final_path(), session.package.bytes));
    session.server.clear_log();
    content::Downloads downloads(session.queue_options());
    downloads.start(*session.service->snapshot());
    std::string why;
    const auto id = session.enqueue(downloads, content::DownloadReason::repair, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    finish(downloads, *id, content::InstallOutcome::installed);
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::done, 5000));
    OA_CHECK(session.server.requests().empty());
    OA_CHECK(handed_count(session.handed) == 1);
    threads::LockGuard guard(session.handed.mutex);
    OA_CHECK(session.handed.origins[0].reason == content::DownloadReason::repair);
}

void test_no_space() {
    fixture::Server server;
    if (!listening(server))
        return;
    Scratch scratch("f10-space");
    std::error_code error;
    fs::create_directories(content::downloads_folder(scratch.data()), error);
    content::DownloadTarget target;
    target.registry = "ridge";
    target.key = "ridge";
    target.name = "Ridge";
    target.kind = catalogue::Kind::oamod;
    target.release = 1;
    target.size = std::numeric_limits<uint64_t>::max();
    target.mode = registry::DownloadMode::direct;
    const auto file = url::parse_http_url(server.url("/v1/p/too-big.oamod"));
    OA_CHECK(file.has_value());
    if (!file)
        return;
    target.files.push_back(*file);
    Logs logs;
    Handed handed;
    content::DownloadsOptions options;
    options.data_folder = scratch.data();
    options.language = "en";
    options.handoff = record_handoff;
    options.handoff_context = &handed;
    options.log = remember_log;
    options.log_context = &logs;
    options.clock = test_clock;
    content::Downloads downloads(std::move(options));
    downloads.start(content::Snapshot{});
    server.clear_log();
    std::string why;
    const auto id = downloads.queue(std::move(target), content::DownloadReason::update, &why);
    OA_CHECK(id.has_value());
    if (!id)
        return;
    OA_CHECK(wait_state(downloads, *id, content::DownloadState::failed, 5000));
    const auto view = view_of(downloads, *id);
    OA_CHECK(view.has_value());
    if (view)
        OA_CHECK(view->failure == content::DownloadFailure::no_space);
    OA_CHECK(server.requests().empty());
}

} // namespace

int main() {
    test_direct_large();
    test_tokens_outcome(content::InstallOutcome::installed, "installed", true);
    test_tokens_outcome(content::InstallOutcome::refused, "refused", false);
    test_tokens_outcome(content::InstallOutcome::set_aside, "cancelled", false);
    test_cut_then_range();
    test_whole_answer_to_range();
    test_package_rekey();
    test_key_expires_soon();
    test_challenge_solved();
    test_challenge_expired();
    test_challenge_cancel();
    test_not_offered(404, "unknown_package");
    test_not_offered(409, "release_mismatch");
    test_not_offered(404, "other_code");
    test_client_fault(400);
    test_client_fault(413);
    test_rate_limit();
    test_unavailable_then_mirror();
    test_refusal_skips_mirror();
    test_wrong_hash_once();
    test_wrong_hash_twice();
    test_wrong_size();
    test_match_pauses();
    test_one_at_a_time();
    test_resume_after_restart();
    test_drop_changed_release();
    test_install_id_off();
    test_no_install_member();
    test_cancel_keeps_part();
    test_cancel_registry();
    test_final_already_there();
    test_no_space();
    return oa::test::check_exit_status();
}
