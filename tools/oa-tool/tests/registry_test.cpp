// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-tool registry, run in-process. Keygen runs once, with the parameters a
// publisher key uses. The mirrors are served by the HTTP fixture on
// 127.0.0.1 at a port the system chooses. Passphrases come from a scratch
// folder, which is removed when the test ends. Nothing here is a key that
// signs a catalogue anyone ships.

#include "command.hpp"

#include "oa/app/content/downloads.hpp"
#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/formats/json.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/http_fixture/server.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace sha = oa::base::sha256;
namespace signing = oa::base::signing;
namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;
namespace json = oa::formats::json;
namespace url = oa::formats::url;
namespace content = oa::app::content;
namespace fixture = oa::netgame::http_fixture;

constexpr std::string_view other_public = "ed25519:11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=";

constexpr std::string_view challenge_code = "HXQ7-4KP2";

struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-tool-registry")) {}

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

Captured run(std::vector<std::string> arguments) {
    std::ostringstream out;
    std::ostringstream err;
    oa::tool::Output output{out, err};
    const int status = oa::tool::run_tool(arguments, output);
    return {status, out.str(), err.str()};
}

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

void show(std::string_view label, const Captured& captured) {
    std::fprintf(
        stderr,
        "%.*s: status %d\n%.*s%.*s",
        static_cast<int>(label.size()),
        label.data(),
        captured.status,
        static_cast<int>(captured.out.size()),
        captured.out.data(),
        static_cast<int>(captured.err.size()),
        captured.err.data()
    );
}

bool exited(std::string_view label, const Captured& captured, int status) {
    if (captured.status == status)
        return true;
    show(label, captured);
    OA_CHECK(captured.status == status);
    return false;
}

void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    OA_CHECK(static_cast<bool>(out));
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

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (!in || size < 0)
        return {};
    in.seekg(0);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0)
        in.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::string read_text(const fs::path& path) {
    const std::vector<uint8_t> bytes = read_bytes(path);
    return {bytes.begin(), bytes.end()};
}

std::string hex_of(const std::vector<uint8_t>& bytes) {
    const auto hex = sha::to_hex(sha::digest_of(bytes));
    return {hex.begin(), hex.end()};
}

std::string hex_of_file(const fs::path& path) {
    return hex_of(read_bytes(path));
}

void write_zeros(const fs::path& path, std::size_t size) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::vector<char> block(64 * 1024, '\0');
    std::size_t left = size;
    while (left > 0) {
        const std::size_t chunk = left < block.size() ? left : block.size();
        out.write(block.data(), static_cast<std::streamsize>(chunk));
        left -= chunk;
    }
    OA_CHECK(static_cast<bool>(out));
}

void copy_tree(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::remove_all(to, error);
    fs::copy(from, to, fs::copy_options::recursive);
}

using FileMap = std::map<std::string, std::vector<uint8_t>>;

FileMap file_map(const fs::path& root) {
    FileMap files;
    std::error_code error;
    if (!fs::exists(root, error))
        return files;
    for (fs::recursive_directory_iterator cursor(root, error), end; !error && cursor != end;
         cursor.increment(error)) {
        if (error || !cursor->is_regular_file(error))
            continue;
        const fs::path relative = cursor->path().lexically_relative(root);
        files.emplace(relative.generic_string(), read_bytes(cursor->path()));
    }
    return files;
}

bool same_files(const fs::path& origin, const fs::path& copied) {
    const FileMap left = file_map(origin);
    const FileMap right = file_map(copied);
    bool same = left.size() == right.size();
    OA_CHECK(left.size() == right.size());
    for (const auto& [name, bytes] : left) {
        const auto found = right.find(name);
        if (found == right.end() || found->second != bytes) {
            std::fprintf(stderr, "mirror file differs: %s\n", name.c_str());
            same = false;
        }
    }
    OA_CHECK(same);
    return same;
}

/// The fixture, bound to 127.0.0.1. A request whose Host is anything else fails the test.
struct Fixture {
    fixture::Server server;

    explicit Fixture(const fs::path& folder) {
        std::string error;
        const bool started = server.start(&error);
        if (!started)
            std::fprintf(stderr, "fixture: %s\n", error.c_str());
        OA_CHECK(started);
        OA_CHECK(server.url("/").starts_with("http://127.0.0.1:"));
        server.serve_folder(folder);
    }

    std::string url(std::string_view path) const { return server.url(path); }

    std::string base() const { return server.url(""); }

    void expect_loopback() const {
        for (const fixture::LoggedRequest& request : server.requests()) {
            const std::string* host = request.header("Host");
            OA_CHECK(host != nullptr && host->starts_with("127.0.0.1"));
        }
    }
};

struct Packed {
    std::string id;
    std::string name;
    std::string hex;
    uint64_t size = 0;
    std::string badge_hex;
};

std::string mod_yaml(std::string_view id, std::string_view name) {
    std::string text = "oamod: 1\nid: ";
    text += id;
    text += "\nname: ";
    text += name;
    text += "\nversion: \"1.0\"\n";
    text += "description: A made-up mod the tests pack.\n";
    text += "requires: {base: ta-3.1c, catalogue: 1}\n";
    text += "author: {name: unknown}\n";
    text += "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n";
    return text;
}

std::string catalogue_text(int64_t sequence, const std::vector<Packed>& packages) {
    json::JsonWriter writer;
    writer.begin_object();
    writer.key("catalogue");
    writer.integer(catalogue::catalogue_version);
    writer.key("registry");
    writer.string("test-registry");
    writer.key("sequence");
    writer.integer(sequence);
    writer.key("generated");
    writer.string("2026-01-01T00:00:00Z");
    writer.key("expires");
    writer.string("2099-01-01T00:00:00Z");
    writer.key("packages");
    writer.begin_array();
    for (const Packed& package : packages) {
        writer.begin_object();
        writer.key("kind");
        writer.string("oamod");
        writer.key("id");
        writer.string(package.id);
        writer.key("name");
        writer.string(package.name);
        writer.key("version");
        writer.string("1.0");
        writer.key("revision");
        writer.integer(1);
        writer.key("release");
        writer.integer(1);
        writer.key("size");
        writer.integer(static_cast<int64_t>(package.size));
        writer.key("sha256");
        writer.string(package.hex);
        writer.key("file");
        writer.string("/v1/p/" + package.hex + ".oamod");
        if (!package.badge_hex.empty()) {
            writer.key("badge");
            writer.string("/v1/i/" + package.badge_hex + ".png");
        }
        writer.end_object();
    }
    writer.end_array();
    writer.end_object();
    std::string text = writer.text();
    text.push_back('\n');
    return text;
}

bool sign_catalogue(const fs::path& catalogue_path, const fs::path& key, const fs::path& pass) {
    const Captured signed_catalogue = run(
        {"catalogue",
         "sign",
         "--key",
         key.string(),
         catalogue_path.string(),
         "--passphrase-file",
         pass.string()}
    );
    return exited("sign", signed_catalogue, oa::tool::exit_done);
}

Captured init_at(
    const fs::path& folder, const std::string& base, const fs::path& key, const fs::path& pass
) {
    return run(
        {"registry",
         "init",
         folder.string(),
         "--id",
         "test-registry",
         "--name",
         "Test",
         "--base-url",
         base,
         "--key",
         key.string(),
         "--passphrase-file",
         pass.string()}
    );
}

Captured mirror_at(const std::string& address, const fs::path& folder, std::string_view key = {}) {
    std::vector<std::string> arguments = {"registry", "mirror", address, folder.string()};
    if (!key.empty()) {
        arguments.emplace_back("--key");
        arguments.emplace_back(key);
    }
    return run(std::move(arguments));
}

fixture::Reply json_reply(int status, std::string_view body) {
    fixture::Reply reply;
    reply.status = status;
    reply.body.assign(body.begin(), body.end());
    return reply;
}

std::string body_text(const fixture::LoggedRequest& request) {
    return {request.body.begin(), request.body.end()};
}

std::string quoted_field(std::string_view body, std::string_view name) {
    const std::string key = std::string("\"") + std::string(name) + "\":\"";
    const auto at = body.find(key);
    if (at == std::string_view::npos)
        return {};
    const auto start = at + key.size();
    const auto end = body.find('"', start);
    if (end == std::string_view::npos)
        return {};
    return std::string(body.substr(start, end - start));
}

/// Points a copied descriptor's catalogue at the server that holds the copy.
void retarget(const fs::path& descriptor_path, const std::string& base) {
    registry::Descriptor descriptor;
    std::string error;
    OA_CHECK(registry::read_descriptor(read_bytes(descriptor_path), descriptor, &error));
    url::UrlError url_error = url::UrlError::none;
    const std::optional<url::Url> parsed = url::parse_http_url(base, &url_error);
    OA_CHECK(parsed.has_value());
    if (!parsed)
        return;
    url::Url catalogue_url = *parsed;
    std::string target = catalogue_url.target;
    while (!target.empty() && target.back() == '/')
        target.pop_back();
    target += "/v1/catalogue.json";
    catalogue_url.target = std::move(target);
    descriptor.catalogue = catalogue_url;
    write_text(descriptor_path, registry::descriptor_text(descriptor));
}

void write_other_signature(const fs::path& catalogue_path, const fs::path& signature_path) {
    const std::vector<uint8_t> bytes = read_bytes(catalogue_path);
    std::array<uint8_t, 32> seed{};
    seed[0] = 1;
    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    signing::key_pair_from_seed(seed, secret, public_key);
    const signing::Signature signature = signing::sign(secret, bytes);
    secret.fill(0);
    const std::string text =
        catalogue::signature_file_text(catalogue::SignatureLine{"test-key", signature});
    write_text(signature_path, text);
}

Packed pack_mod(
    const fs::path& scratch,
    const fs::path& origin,
    std::string_view id,
    std::string_view name,
    bool large,
    bool badge
) {
    const fs::path folder = scratch / std::string(id);
    write_text(folder / "oamod.yaml", mod_yaml(id, name));
    write_text(folder / "readme.txt", "hello\n");
    if (large)
        write_zeros(folder / "pics" / "blob.png", content::download_step_bytes + (1u << 18));
    const fs::path out = scratch / (std::string(id) + ".oamod");
    const Captured packed = run({"pack", "--out", out.string(), folder.string()});
    exited(id, packed, oa::tool::exit_done);
    const std::vector<uint8_t> bytes = read_bytes(out);
    Packed package;
    package.id = std::string(id);
    package.name = std::string(name);
    package.hex = hex_of(bytes);
    package.size = bytes.size();
    write_bytes(origin / "v1" / "p" / (package.hex + ".oamod"), bytes);
    if (badge) {
        const std::vector<uint8_t> picture = {'r', 'i', 'd', 'g', 'e', '\n'};
        package.badge_hex = hex_of(picture);
        write_bytes(origin / "v1" / "i" / (package.badge_hex + ".png"), picture);
    }
    return package;
}

bool publish_catalogue(
    const fs::path& origin,
    int64_t sequence,
    const std::vector<Packed>& packages,
    const fs::path& key,
    const fs::path& pass
) {
    write_text(origin / "v1" / "catalogue.json", catalogue_text(sequence, packages));
    return sign_catalogue(origin / "v1" / "catalogue.json", key, pass);
}

/// Help, one empty registry, and the refusals that write nothing.
void test_init(
    const fs::path& scratch,
    const fs::path& key,
    const fs::path& pass,
    const fs::path& origin,
    const std::string& base
) {
    const Captured help = run({"help", "registry", "mirror"});
    exited("help", help, oa::tool::exit_done);
    OA_CHECK(contains(help.out, "Range"));
    OA_CHECK(contains(help.out, "SHA-256"));
    OA_CHECK(contains(help.out, "tokens"));
    OA_CHECK(contains(help.out, "resumed"));

    const Captured made = init_at(origin, base, key, pass);
    if (!exited("init", made, oa::tool::exit_done))
        return;
    OA_CHECK(contains(made.out, "registry: "));
    OA_CHECK(contains(made.out, "fingerprint: "));
    OA_CHECK(fs::is_regular_file(origin / "registry.yaml"));
    OA_CHECK(fs::is_regular_file(origin / "v1" / "catalogue.json"));
    OA_CHECK(fs::is_regular_file(origin / "v1" / "catalogue.json.sig"));
    OA_CHECK(fs::is_directory(origin / "v1" / "p"));
    OA_CHECK(fs::is_directory(origin / "v1" / "i"));

    catalogue::Catalogue loaded;
    std::string error;
    const std::vector<uint8_t> catalogue_bytes = read_bytes(origin / "v1" / "catalogue.json");
    OA_CHECK(catalogue::read_catalogue(catalogue_bytes, loaded, &error));
    OA_CHECK(loaded.packages.empty());
    OA_CHECK(loaded.sequence == 1);
    OA_CHECK(loaded.expires == loaded.generated + 30LL * 86400);

    registry::Descriptor descriptor;
    OA_CHECK(registry::read_descriptor(read_bytes(origin / "registry.yaml"), descriptor, &error));
    OA_CHECK(descriptor.mode == registry::DownloadMode::direct);
    const std::vector<uint8_t> signature_bytes = read_bytes(origin / "v1" / "catalogue.json.sig");
    catalogue::CheckRequest request;
    request.catalogue = catalogue_bytes;
    request.signature = signature_bytes;
    request.registry_id = descriptor.id;
    request.trusted_keys = descriptor.keys;
    request.now = loaded.generated;
    const catalogue::Checked checked = catalogue::check_catalogue(request);
    OA_CHECK(checked.verdict == catalogue::Verdict::accepted);

    const Captured verified = run(
        {"catalogue",
         "verify",
         (origin / "v1" / "catalogue.json").string(),
         "--descriptor",
         (origin / "registry.yaml").string()}
    );
    exited("verify", verified, oa::tool::exit_done);

    const fs::path full = scratch / "full";
    write_text(full / "keep.txt", "keep");
    const Captured occupied = init_at(full, base, key, pass);
    exited("occupied", occupied, oa::tool::exit_failed);
    OA_CHECK(contains(occupied.err, "the folder is not empty"));
    OA_CHECK(read_text(full / "keep.txt") == "keep");
    OA_CHECK(!fs::exists(full / "registry.yaml"));

    const fs::path https_folder = scratch / "https-registry";
    const Captured https = init_at(https_folder, "https://127.0.0.1:9", key, pass);
    exited("https", https, oa::tool::exit_failed);
    OA_CHECK(contains(https.err, "a registry address is plain http"));
    OA_CHECK(!fs::exists(https_folder));
}

void test_direct(
    const fs::path& scratch,
    Fixture& server,
    const fs::path& origin,
    const Packed& ridge,
    const Packed& vale
) {
    const fs::path dest = scratch / "direct";
    const Captured copied = mirror_at(server.url("/registry.yaml"), dest);
    if (!exited("direct", copied, oa::tool::exit_done))
        return;
    OA_CHECK(contains(copied.out, "name: Test\n"));
    OA_CHECK(contains(copied.out, "catalogue: " + server.url("/v1/catalogue.json")));
    OA_CHECK(contains(copied.out, "fingerprint: "));
    same_files(origin, dest);
    server.expect_loopback();

    const fs::path vale_file = dest / "v1" / "p" / (vale.hex + ".oamod");
    std::error_code error;
    fs::remove(vale_file, error);
    OA_CHECK(!fs::exists(vale_file));
    server.server.clear_log();
    const Captured again = mirror_at(server.url("/registry.yaml"), dest);
    exited("unchanged", again, oa::tool::exit_done);
    OA_CHECK(hex_of_file(vale_file) == vale.hex);
    same_files(origin, dest);
    server.expect_loopback();

    const fs::path missing_key = scratch / "missing-key";
    server.server.clear_log();
    const Captured pinned = mirror_at(server.url("/registry.yaml"), missing_key, other_public);
    exited("other key pin", pinned, oa::tool::exit_failed);
    OA_CHECK(contains(pinned.err, "the descriptor does not list that key"));
    OA_CHECK(!contains(pinned.out, "name:"));
    OA_CHECK(!fs::exists(missing_key));
    server.expect_loopback();

    const fs::path resume = scratch / "resume";
    fixture::Reply cut;
    cut.status = 200;
    cut.body = read_bytes(origin / "v1" / "p" / (ridge.hex + ".oamod"));
    cut.cut_after = content::download_step_bytes;
    server.server.clear_log();
    server.server.override_next("/v1/p/" + ridge.hex + ".oamod", std::move(cut));
    const Captured stopped = mirror_at(server.url("/registry.yaml"), resume);
    exited("cut", stopped, oa::tool::exit_failed);
    OA_CHECK(contains(stopped.err, "the download stopped before the file was complete"));
    OA_CHECK(!fs::exists(resume / "v1" / "catalogue.json"));
    OA_CHECK(!fs::exists(resume / "registry.yaml"));
    const fs::path part = resume / "v1" / "p" / (ridge.hex + ".oamod.part");
    const auto part_size = fs::file_size(part, error);
    if (error || part_size != content::download_step_bytes)
        std::fprintf(
            stderr, "part size %llu\n", static_cast<unsigned long long>(error ? 0 : part_size)
        );
    OA_CHECK(!error && part_size == content::download_step_bytes);
    OA_CHECK(fs::is_regular_file(resume / "v1" / "p" / (vale.hex + ".oamod")));
    server.expect_loopback();

    server.server.clear_log();
    const Captured resumed = mirror_at(server.url("/registry.yaml"), resume);
    if (!exited("resume", resumed, oa::tool::exit_done))
        return;
    bool ranged = false;
    const std::string package_path = "/v1/p/" + ridge.hex + ".oamod";
    const std::string range = "bytes=" + std::to_string(content::download_step_bytes) + "-";
    for (const fixture::LoggedRequest& request : server.server.requests()) {
        if (request.method == "GET" && request.target == package_path) {
            const std::string* header = request.header("Range");
            ranged = header != nullptr && *header == range;
        }
    }
    OA_CHECK(ranged);
    same_files(origin, resume);
    OA_CHECK(!fs::exists(part));
    server.expect_loopback();
}

void test_bad_hash(
    const fs::path& scratch, const fs::path& origin, const Packed& ridge, const Packed& vale
) {
    const fs::path served = scratch / "bad-hash-origin";
    copy_tree(origin, served);
    Fixture server(served);
    retarget(served / "registry.yaml", server.base());
    std::vector<uint8_t> flipped = read_bytes(served / "v1" / "p" / (ridge.hex + ".oamod"));
    OA_CHECK(!flipped.empty());
    if (!flipped.empty())
        flipped[0] ^= 0x01;
    const std::string got = hex_of(flipped);
    server.server.serve_bytes("/v1/p/" + ridge.hex + ".oamod", std::move(flipped));
    const fs::path dest = scratch / "bad-hash";
    const Captured refused = mirror_at(server.url("/registry.yaml"), dest);
    exited("bad hash", refused, oa::tool::exit_failed);
    OA_CHECK(contains(refused.err, "the file's SHA-256 is " + got));
    OA_CHECK(contains(refused.err, "; the catalogue names " + ridge.hex));
    OA_CHECK(!contains(refused.err, "the file is not the one the catalogue named"));
    OA_CHECK(!fs::exists(dest / "v1" / "p" / (ridge.hex + ".oamod")));
    OA_CHECK(!fs::exists(dest / "v1" / "p" / (ridge.hex + ".oamod.part")));
    OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
    OA_CHECK(!fs::exists(dest / "registry.yaml"));
    const fs::path kept = dest / "v1" / "p" / (vale.hex + ".oamod");
    OA_CHECK(read_bytes(kept) == read_bytes(served / "v1" / "p" / (vale.hex + ".oamod")));
    server.expect_loopback();
}

void test_bad_signature(const fs::path& scratch, const fs::path& origin) {
    const fs::path served = scratch / "bad-sig-origin";
    copy_tree(origin, served);
    write_other_signature(served / "v1" / "catalogue.json", served / "v1" / "catalogue.json.sig");
    Fixture server(served);
    retarget(served / "registry.yaml", server.base());
    const fs::path dest = scratch / "bad-sig";
    const Captured refused = mirror_at(server.url("/registry.yaml"), dest);
    exited("bad sig", refused, oa::tool::exit_failed);
    OA_CHECK(contains(refused.err, "the signature does not match the catalogue"));
    OA_CHECK(!fs::exists(dest));
    server.expect_loopback();
}

void test_sequence(
    const fs::path& scratch,
    const fs::path& origin,
    const std::vector<Packed>& packages,
    const fs::path& key,
    const fs::path& pass
) {
    const fs::path served = scratch / "sequence-origin";
    copy_tree(origin, served);
    if (!publish_catalogue(served, 2, packages, key, pass))
        return;
    Fixture server(served);
    retarget(served / "registry.yaml", server.base());
    const fs::path dest = scratch / "sequence";
    const Captured first = mirror_at(server.url("/registry.yaml"), dest);
    if (!exited("sequence 2", first, oa::tool::exit_done))
        return;
    const FileMap before = file_map(dest);
    if (!publish_catalogue(served, 1, packages, key, pass))
        return;
    server.server.clear_log();
    const Captured second = mirror_at(server.url("/registry.yaml"), dest);
    exited("sequence 1", second, oa::tool::exit_failed);
    OA_CHECK(
        contains(second.out + second.err, "the folder holds sequence 2; the registry serves 1")
    );
    OA_CHECK(file_map(dest) == before);
    server.expect_loopback();
}

void test_collision(const fs::path& scratch, Fixture& catalogue_host, const fs::path& origin) {
    fixture::Server other;
    std::string error;
    OA_CHECK(other.start(&error));
    OA_CHECK(other.url("/").starts_with("http://127.0.0.1:"));
    other.serve_bytes(
        "/v1/catalogue.json", read_bytes(origin / "registry.yaml"), "application/yaml"
    );
    const fs::path dest = scratch / "collision";
    const Captured refused = mirror_at(other.url("/v1/catalogue.json"), dest);
    exited("collision", refused, oa::tool::exit_failed);
    OA_CHECK(contains(refused.err, "two files of one path come from different hosts"));
    OA_CHECK(!fs::exists(dest));
    catalogue_host.expect_loopback();
    for (const fixture::LoggedRequest& request : other.requests()) {
        const std::string* host = request.header("Host");
        OA_CHECK(host != nullptr && host->starts_with("127.0.0.1"));
    }
}

void test_tokens(
    const fs::path& scratch,
    Fixture& catalogue_host,
    const fs::path& origin,
    const fs::path& descriptor
) {
    const fs::path served = scratch / "tokens-origin";
    copy_tree(origin, served);
    Fixture server(served);
    registry::Descriptor loaded;
    std::string error;
    OA_CHECK(registry::read_descriptor(read_bytes(descriptor), loaded, &error));
    loaded.mode = registry::DownloadMode::tokens;
    loaded.install_id = registry::InstallIdUse::required;
    url::UrlError url_error = url::UrlError::none;
    const std::optional<url::Url> api = url::parse_http_url(server.base() + "/api/v1", &url_error);
    OA_CHECK(api.has_value());
    if (!api)
        return;
    loaded.api = *api;
    write_text(served / "registry.yaml", registry::descriptor_text(loaded));

    int posts = 0;
    const std::string downloads = "/api/v1/downloads";
    const std::string challenge = std::string("/api/v1/challenges/") + std::string(challenge_code);
    server.server.handle("POST", downloads, [&](const fixture::LoggedRequest& request) {
        const std::string body = body_text(request);
        if (!contains(body, "\"reason\":\"mirror\""))
            return json_reply(400, "{\"error\":\"bad_reason\",\"message\":\"reason\"}");
        ++posts;
        if (posts == 1) {
            const std::string page = server.url("/verify?c=") + std::string(challenge_code);
            std::string answer =
                "{\"error\":\"challenge_required\",\"message\":\"solve the check\",";
            answer += "\"challenge\":{\"code\":\"";
            answer += challenge_code;
            answer += "\",\"verify\":\"";
            answer += page;
            answer += "\",\"expires\":\"2099-01-01T00:00:00Z\",\"poll\":2}}";
            return json_reply(428, answer);
        }
        const std::string hash = quoted_field(body, "sha256");
        std::string answer = "{\"download\":\"pkg-";
        answer += hash.substr(0, hash.size() < 8 ? hash.size() : 8);
        answer += "\",\"expires\":\"2099-01-01T00:00:00Z\",\"url\":\"";
        answer += server.url("/v1/p/" + hash + ".oamod");
        answer += "\"}";
        return json_reply(201, answer);
    });
    server.server.handle("GET", challenge, [](const fixture::LoggedRequest&) {
        return json_reply(
            200,
            "{\"code\":\"HXQ7-4KP2\",\"state\":\"solved\",\"expires\":\"2099-01-01T00:00:00Z\"}"
        );
    });

    const fs::path dest = scratch / "tokens";
    const Captured copied = mirror_at(server.url("/registry.yaml"), dest);
    if (!exited("tokens", copied, oa::tool::exit_done))
        return;
    OA_CHECK(contains(copied.out, "check: HXQ7-4KP2\n"));
    OA_CHECK(contains(copied.out, "address: "));
    same_files(served, dest);

    std::string install;
    int download_posts = 0;
    bool result = false;
    for (const fixture::LoggedRequest& request : server.server.requests()) {
        if (request.target.find("result") != std::string::npos)
            result = true;
        if (request.method != "POST" || request.target != downloads)
            continue;
        ++download_posts;
        const std::string body = body_text(request);
        OA_CHECK(contains(body, "\"reason\":\"mirror\""));
        const std::string id = quoted_field(body, "install");
        OA_CHECK(id.size() == 36);
        if (install.empty())
            install = id;
        OA_CHECK(id == install);
    }
    OA_CHECK(download_posts >= 3);
    OA_CHECK(!install.empty());
    OA_CHECK(!result);
    OA_CHECK(!contains(copied.out, install));
    OA_CHECK(!contains(copied.err, install));
    for (const auto& [name, bytes] : file_map(dest)) {
        const std::string text(bytes.begin(), bytes.end());
        if (contains(text, install))
            std::fprintf(stderr, "install id written in %s\n", name.c_str());
        OA_CHECK(!contains(text, install));
    }
    server.expect_loopback();
    catalogue_host.expect_loopback();
}

/// One direct registry that lists `package` only, served on 127.0.0.1.
struct ServedPackage {
    fs::path root;
    Fixture server;

    ServedPackage(
        const fs::path& scratch,
        std::string_view name,
        const fs::path& origin,
        const Packed& package,
        const fs::path& key,
        const fs::path& pass
    )
        : root(scratch / name), server(root) {
        copy_tree(origin, root);
        if (!publish_catalogue(root, 1, {package}, key, pass))
            return;
        retarget(root / "registry.yaml", server.base());
    }
};

void test_named_refusals(
    const fs::path& scratch,
    const fs::path& origin,
    const Packed& vale,
    const fs::path& key,
    const fs::path& pass
) {
    {
        ServedPackage served(scratch, "unnamed-picture", origin, vale, key, pass);
        std::string text = read_text(served.root / "v1" / "catalogue.json");
        const std::string picture = "/v1/i/badge.png";
        const std::string file = "\"/v1/p/" + vale.hex + ".oamod\"";
        const auto at = text.find(file);
        OA_CHECK(at != std::string::npos);
        if (at == std::string::npos)
            return;
        text.insert(at + file.size(), ",\"badge\":\"" + picture + "\"");
        write_text(served.root / "v1" / "catalogue.json", text);
        if (!sign_catalogue(served.root / "v1" / "catalogue.json", key, pass))
            return;
        const fs::path dest = scratch / "unnamed-picture-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("unnamed picture", refused, oa::tool::exit_failed);
        OA_CHECK(contains(refused.err, picture));
        OA_CHECK(contains(refused.err, "the picture is not named by its SHA-256"));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "key-not-ready", origin, vale, key, pass);
        registry::Descriptor loaded;
        std::string error;
        OA_CHECK(
            registry::read_descriptor(read_bytes(served.root / "registry.yaml"), loaded, &error)
        );
        loaded.mode = registry::DownloadMode::tokens;
        loaded.install_id = registry::InstallIdUse::required;
        url::UrlError url_error = url::UrlError::none;
        const std::optional<url::Url> api =
            url::parse_http_url(served.server.base() + "/api/v1", &url_error);
        OA_CHECK(api.has_value());
        if (!api)
            return;
        loaded.api = *api;
        write_text(served.root / "registry.yaml", registry::descriptor_text(loaded));
        served.server.server.handle("POST", "/api/v1/downloads", [](const fixture::LoggedRequest&) {
            return json_reply(403, "{\"error\":\"forbidden\",\"message\":\"no key\"}");
        });
        const fs::path dest = scratch / "key-not-ready-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("key", refused, oa::tool::exit_failed);
        OA_CHECK(contains(refused.err, "a download key was not ready"));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod")));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "missing-file", origin, vale, key, pass);
        fixture::Reply missing;
        missing.status = 404;
        served.server.server.override_next("/v1/p/" + vale.hex + ".oamod", std::move(missing));
        const fs::path dest = scratch / "missing-file-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("missing", refused, oa::tool::exit_failed);
        OA_CHECK(contains(refused.err, "/v1/p/" + vale.hex + ".oamod"));
        OA_CHECK(contains(refused.err, "the file was not found"));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod")));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "size-under", origin, vale, key, pass);
        const std::vector<uint8_t> short_body = {'n', 'o'};
        served.server.server.serve_bytes("/v1/p/" + vale.hex + ".oamod", short_body);
        const fs::path dest = scratch / "size-under-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("size under", refused, oa::tool::exit_failed);
        OA_CHECK(contains(
            refused.err,
            "the file is " + std::to_string(short_body.size()) + " bytes; the catalogue names " +
                std::to_string(vale.size)
        ));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod")));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod.part")));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "size-over", origin, vale, key, pass);
        std::vector<uint8_t> long_body =
            read_bytes(served.root / "v1" / "p" / (vale.hex + ".oamod"));
        long_body.insert(long_body.end(), 8, 'x');
        served.server.server.serve_bytes("/v1/p/" + vale.hex + ".oamod", long_body);
        const fs::path dest = scratch / "size-over-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("size over", refused, oa::tool::exit_failed);
        OA_CHECK(contains(
            refused.err,
            "the file is " + std::to_string(long_body.size()) + " bytes; the catalogue names " +
                std::to_string(vale.size)
        ));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod")));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod.part")));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "resume-failed", origin, vale, key, pass);
        fixture::Reply range;
        range.status = 416;
        served.server.server.override_next("/v1/p/" + vale.hex + ".oamod", std::move(range), 2);
        const fs::path dest = scratch / "resume-failed-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("resume failed", refused, oa::tool::exit_failed);
        OA_CHECK(contains(refused.err, "the server did not resume the file"));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod")));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "no-length", origin, vale, key, pass);
        fixture::Reply open;
        open.no_length = true;
        open.body = {'n', 'o'};
        served.server.server.override_next("/v1/p/" + vale.hex + ".oamod", std::move(open));
        const fs::path dest = scratch / "no-length-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("no length", refused, oa::tool::exit_failed);
        OA_CHECK(contains(
            refused.err,
            "the response named no length; the catalogue names " + std::to_string(vale.size) +
                " bytes"
        ));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod")));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "server-status", origin, vale, key, pass);
        fixture::Reply failed;
        failed.status = 500;
        served.server.server.override_next("/v1/p/" + vale.hex + ".oamod", std::move(failed));
        const fs::path dest = scratch / "server-status-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("server status", refused, oa::tool::exit_failed);
        OA_CHECK(contains(refused.err, "the server answered 500"));
        OA_CHECK(!fs::exists(dest / "v1" / "p" / (vale.hex + ".oamod")));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
    {
        ServedPackage served(scratch, "picture-large", origin, vale, key, pass);
        std::string text = read_text(served.root / "v1" / "catalogue.json");
        const std::string picture = "/v1/i/" + vale.hex + ".png";
        const std::string file = "\"/v1/p/" + vale.hex + ".oamod\"";
        const auto at = text.find(file);
        OA_CHECK(at != std::string::npos);
        if (at == std::string::npos)
            return;
        text.insert(at + file.size(), ",\"badge\":\"" + picture + "\"");
        write_text(served.root / "v1" / "catalogue.json", text);
        if (!sign_catalogue(served.root / "v1" / "catalogue.json", key, pass))
            return;
        // One byte past the 64 MiB picture limit. The length is declared and the
        // body is empty, so the refusal is the limit, not a downloaded file.
        const std::string raw =
            "HTTP/1.1 200 OK\r\nContent-Length: 67108865\r\nConnection: close\r\n\r\n";
        fixture::Reply huge;
        huge.raw = std::vector<uint8_t>(raw.begin(), raw.end());
        served.server.server.override_next(picture, std::move(huge));
        const fs::path dest = scratch / "picture-large-copy";
        const Captured refused = mirror_at(served.server.url("/registry.yaml"), dest);
        exited("picture large", refused, oa::tool::exit_failed);
        OA_CHECK(contains(refused.err, picture));
        OA_CHECK(contains(refused.err, "the picture is too large"));
        OA_CHECK(!fs::exists(dest / "v1" / "catalogue.json"));
        served.server.expect_loopback();
    }
}

} // namespace

int main() {
    const Scratch scratch;
    const fs::path pass = scratch.path / "pass";
    const fs::path key = scratch.path / "k";
    write_text(pass, "correct horse battery\n");
    const Captured made = run(
        {"catalogue",
         "keygen",
         "--id",
         "test-key",
         "--out",
         key.string(),
         "--passphrase-file",
         pass.string()}
    );
    if (!exited("keygen", made, oa::tool::exit_done))
        return oa::test::check_exit_status();

    const fs::path origin = scratch.path / "origin";
    Fixture server(origin);
    test_init(scratch.path, key, pass, origin, server.base());
    if (!fs::is_regular_file(origin / "v1" / "catalogue.json"))
        return oa::test::check_exit_status();

    const Packed ridge = pack_mod(scratch.path, origin, "ridge", "Ridge", true, true);
    const Packed vale = pack_mod(scratch.path, origin, "vale", "Vale", false, false);
    OA_CHECK(ridge.size > content::download_step_bytes);
    const std::vector<Packed> packages = {ridge, vale};
    if (!publish_catalogue(origin, 1, packages, key, pass))
        return oa::test::check_exit_status();

    test_direct(scratch.path, server, origin, ridge, vale);
    test_bad_hash(scratch.path, origin, ridge, vale);
    test_named_refusals(scratch.path, origin, vale, key, pass);
    test_bad_signature(scratch.path, origin);
    test_sequence(scratch.path, origin, packages, key, pass);
    test_collision(scratch.path, server, origin);
    test_tokens(scratch.path, server, origin, origin / "registry.yaml");

    if (oa::test::check_exit_status() == 0)
        std::fputs("tools-oa-tool-registry: ok\n", stdout);
    return oa::test::check_exit_status();
}
