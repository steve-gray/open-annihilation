// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registry descriptors and the player's registry file: the sample read field
// by field, every refused field, and a file that is never written over when
// it could not be read.

#include "oa/base/signing/ed25519.hpp"
#include "oa/data/registry/player_registries.hpp"
#include "oa/formats/url.hpp"
#include "oa/test/check.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace registry = oa::data::registry;
namespace signing = oa::base::signing;
namespace url = oa::formats::url;

std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

signing::PublicKey key_from_seed(uint8_t fill) {
    std::array<uint8_t, 32> seed{};
    seed.fill(fill);
    signing::SecretKey secret{};
    signing::PublicKey key{};
    signing::key_pair_from_seed(seed, secret, key);
    secret.fill(0);
    return key;
}

std::string key_text(const signing::PublicKey& key) {
    return std::string(signing::public_key_text(key).view());
}

const signing::PublicKey& sample_key() {
    static const signing::PublicKey key = key_from_seed(0x11);
    return key;
}

const signing::PublicKey& other_key() {
    static const signing::PublicKey key = key_from_seed(0x22);
    return key;
}

std::string direct_tail() {
    return "catalogue: http://downloads.example.net/v1/catalogue.json\n"
           "downloads:\n"
           "  mode: direct\n";
}

void refuse(std::string_view yaml, std::string_view needle) {
    registry::Descriptor out;
    out.id = "untouched";
    std::string error = "none";
    OA_CHECK(!registry::read_descriptor(bytes_of(yaml), out, &error));
    OA_CHECK(out.id == "untouched");
    OA_CHECK(error.find(needle) != std::string::npos);
}

bool same_descriptor(const registry::Descriptor& left, const registry::Descriptor& right) {
    if (left.id != right.id || left.name != right.name || left.catalogue != right.catalogue ||
        left.mode != right.mode || left.install_id != right.install_id ||
        left.homepage != right.homepage || left.api != right.api || left.mirrors != right.mirrors ||
        left.keys.size() != right.keys.size())
        return false;
    for (std::size_t index = 0; index < left.keys.size(); ++index) {
        if (left.keys[index].id != right.keys[index].id ||
            left.keys[index].key != right.keys[index].key)
            return false;
    }
    return true;
}

/// The design's descriptor, with a real key, read field by field and written back.
void design_sample_reads_and_writes() {
    const std::string yaml = "registry: 1\n"
                             "id: example-maps\n"
                             "name: Example Maps\n"
                             "homepage: https://maps.example.org\n"
                             "catalogue: http://downloads.example.net/v1/catalogue.json\n"
                             "mirrors: [http://mirror.example.org/oa/v1/catalogue.json]\n"
                             "keys:\n"
                             "  - {id: maps-2026, public: \"" +
                             key_text(sample_key()) +
                             "\"}\n"
                             "downloads:\n"
                             "  mode: tokens\n"
                             "  api: http://downloads.example.net/api/v1\n"
                             "  install-id: required\n";
    registry::Descriptor out;
    std::string error;
    OA_CHECK(registry::read_descriptor(bytes_of(yaml), out, &error));
    OA_CHECK(out.id == "example-maps");
    OA_CHECK(out.name == "Example Maps");
    OA_CHECK(out.homepage.has_value());
    if (out.homepage)
        OA_CHECK(url::url_text(*out.homepage) == "https://maps.example.org/");
    OA_CHECK(url::url_text(out.catalogue) == "http://downloads.example.net/v1/catalogue.json");
    OA_CHECK(out.mirrors.size() == 1);
    if (!out.mirrors.empty())
        OA_CHECK(url::url_text(out.mirrors[0]) == "http://mirror.example.org/oa/v1/catalogue.json");
    OA_CHECK(out.keys.size() == 1);
    if (!out.keys.empty()) {
        OA_CHECK(out.keys[0].id == "maps-2026");
        OA_CHECK(out.keys[0].key == sample_key());
    }
    OA_CHECK(out.mode == registry::DownloadMode::tokens);
    OA_CHECK(out.api.has_value());
    if (out.api)
        OA_CHECK(url::url_text(*out.api) == "http://downloads.example.net/api/v1");
    OA_CHECK(out.install_id == registry::InstallIdUse::required);
    OA_CHECK(out.is_signed());
    OA_CHECK(registry::address_text(out) == "downloads.example.net");

    registry::Descriptor again;
    OA_CHECK(registry::read_descriptor(bytes_of(registry::descriptor_text(out)), again, &error));
    OA_CHECK(same_descriptor(out, again));
}

/// A port other than 80 is part of the address a player sees.
void address_includes_a_port() {
    registry::Descriptor out;
    const std::string yaml = "registry: 1\n"
                             "id: example-maps\n"
                             "name: Example Maps\n"
                             "catalogue: http://downloads.example.net:8080/v1/catalogue.json\n"
                             "downloads:\n"
                             "  mode: direct\n";
    OA_CHECK(registry::read_descriptor(bytes_of(yaml), out, nullptr));
    OA_CHECK(registry::address_text(out) == "downloads.example.net:8080");
    OA_CHECK(!out.is_signed());
}

void missing_required_keys_are_refused() {
    refuse("id: example-maps\nname: Example Maps\n" + direct_tail(), "registry");
    refuse("registry: 1\nname: Example Maps\n" + direct_tail(), "id");
    refuse("registry: 1\nid: example-maps\n" + direct_tail(), "name");
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\ndownloads:\n  mode: direct\n",
        "catalogue"
    );
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n"
        "catalogue: http://downloads.example.net/v1/catalogue.json\n",
        "downloads"
    );
}

void registry_version_must_be_one() {
    refuse("registry: 2\nid: example-maps\nname: Example Maps\n" + direct_tail(), "registry");
}

void unknown_key_is_refused() {
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\nextra: 1\n" + direct_tail(), "extra"
    );
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n" + direct_tail() + "  extra: 1\n",
        "extra"
    );
}

void bad_id_and_name_are_refused() {
    refuse("registry: 1\nid: Example\nname: Example Maps\n" + direct_tail(), "id");
    refuse("registry: 1\nid: -example\nname: Example Maps\n" + direct_tail(), "id");
    refuse("registry: 1\nid: example-\nname: Example Maps\n" + direct_tail(), "id");
    refuse("registry: 1\nid: exam--ple\nname: Example Maps\n" + direct_tail(), "id");
    const std::string long_name(65, 'a');
    refuse("registry: 1\nid: example-maps\nname: \"" + long_name + "\"\n" + direct_tail(), "name");
    refuse("registry: 1\nid: example-maps\nname: \"a\\nb\"\n" + direct_tail(), "name");
    refuse("registry: 1\nid: example-maps\nname: \"\"\n" + direct_tail(), "name");
}

void catalogue_rules_are_refused() {
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n"
        "catalogue: https://downloads.example.net/v1/catalogue.json\n"
        "downloads:\n  mode: direct\n",
        "catalogue"
    );
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n"
        "catalogue: \"http://[::1]/catalogue.json\"\n"
        "downloads:\n  mode: direct\n",
        "catalogue"
    );
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n"
        "catalogue: http://user@downloads.example.net/v1/catalogue.json\n"
        "downloads:\n  mode: direct\n",
        "catalogue"
    );
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n"
        "catalogue: http://downloads.example.net/v1/catalogue.json?x=1\n"
        "downloads:\n  mode: direct\n",
        "catalogue"
    );
}

void download_rules_are_refused_and_defaulted() {
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n"
        "catalogue: http://downloads.example.net/v1/catalogue.json\n"
        "downloads:\n  mode: direct\n  api: http://downloads.example.net/api/v1\n",
        "api"
    );
    refuse(
        "registry: 1\nid: example-maps\nname: Example Maps\n"
        "catalogue: http://downloads.example.net/v1/catalogue.json\n"
        "downloads:\n  mode: direct\n  install-id: required\n",
        "install-id"
    );
    registry::Descriptor tokens;
    OA_CHECK(
        registry::read_descriptor(
            bytes_of(
                "registry: 1\nid: example-maps\nname: Example Maps\n"
                "catalogue: http://downloads.example.net/v1/catalogue.json\n"
                "downloads:\n  mode: tokens\n  api: http://downloads.example.net/api/v1\n"
            ),
            tokens,
            nullptr
        )
    );
    OA_CHECK(tokens.install_id == registry::InstallIdUse::required);
    registry::Descriptor direct;
    OA_CHECK(
        registry::read_descriptor(
            bytes_of("registry: 1\nid: example-maps\nname: Example Maps\n" + direct_tail()),
            direct,
            nullptr
        )
    );
    OA_CHECK(direct.install_id == registry::InstallIdUse::none);
    OA_CHECK(!direct.api.has_value());
    registry::Descriptor none;
    OA_CHECK(
        registry::read_descriptor(
            bytes_of(
                "registry: 1\nid: example-maps\nname: Example Maps\n"
                "catalogue: http://downloads.example.net/v1/catalogue.json\n"
                "downloads:\n  mode: tokens\n  api: http://downloads.example.net/api/v1\n"
                "  install-id: none\n"
            ),
            none,
            nullptr
        )
    );
    OA_CHECK(none.install_id == registry::InstallIdUse::none);
    OA_CHECK(none.mode == registry::DownloadMode::tokens);
}

void key_rules_are_refused() {
    const std::string head =
        "registry: 1\nid: example-maps\nname: Example Maps\n" + direct_tail() + "keys:\n";
    refuse(
        head + "  - {id: maps-2026, public: \"" + key_text(sample_key()) +
            "\"}\n  - {id: maps-2026, public: \"" + key_text(other_key()) + "\"}\n",
        "repeats the id"
    );
    refuse(
        head + "  - {id: maps-2026, public: \"" + key_text(sample_key()) +
            "\"}\n  - {id: other-2026, public: \"" + key_text(sample_key()) + "\"}\n",
        "repeats a public key"
    );
    refuse(head + "  - {id: maps-2026, public: \"ed25519:nope\"}\n", "public");
    std::string nine = head;
    for (uint8_t index = 1; index <= 9; ++index) {
        nine += "  - {id: key-" + std::to_string(index) + ", public: \"" +
                key_text(key_from_seed(index)) + "\"}\n";
    }
    refuse(nine, "8");
    std::string eight = head;
    for (uint8_t index = 1; index <= 8; ++index) {
        eight += "  - {id: key-" + std::to_string(index) + ", public: \"" +
                 key_text(key_from_seed(index)) + "\"}\n";
    }
    registry::Descriptor out;
    OA_CHECK(registry::read_descriptor(bytes_of(eight), out, nullptr));
    OA_CHECK(out.keys.size() == 8);
}

void mirror_count_is_capped_at_eight() {
    std::string eight =
        "registry: 1\nid: example-maps\nname: Example Maps\n" + direct_tail() + "mirrors:\n";
    std::string nine = eight;
    for (int index = 1; index <= 8; ++index)
        eight += "  - http://mirror" + std::to_string(index) + ".example.org/catalogue.json\n";
    for (int index = 1; index <= 9; ++index)
        nine += "  - http://mirror" + std::to_string(index) + ".example.org/catalogue.json\n";
    registry::Descriptor out;
    OA_CHECK(registry::read_descriptor(bytes_of(eight), out, nullptr));
    OA_CHECK(out.mirrors.size() == 8);
    refuse(nine, "mirrors");
}

void a_descriptor_over_64_kib_is_refused() {
    std::vector<uint8_t> bytes(registry::max_descriptor_bytes + 1, static_cast<uint8_t>('a'));
    registry::Descriptor out;
    out.id = "untouched";
    std::string error;
    OA_CHECK(!registry::read_descriptor(bytes, out, &error));
    OA_CHECK(out.id == "untouched");
    OA_CHECK(error.find("64 KiB") != std::string::npos);
}

void ids_match_their_rules() {
    OA_CHECK(registry::valid_registry_id("a"));
    OA_CHECK(registry::valid_registry_id("example-maps"));
    OA_CHECK(registry::valid_registry_id(std::string(32, 'a')));
    OA_CHECK(!registry::valid_registry_id(""));
    OA_CHECK(!registry::valid_registry_id(std::string(33, 'a')));
    OA_CHECK(!registry::valid_registry_id("Example"));
    OA_CHECK(!registry::valid_registry_id("-a"));
    OA_CHECK(!registry::valid_registry_id("a-"));
    OA_CHECK(!registry::valid_registry_id("a--b"));
    OA_CHECK(!registry::valid_registry_id("a_b"));
    OA_CHECK(registry::valid_key_id("a"));
    OA_CHECK(registry::valid_key_id("maps-2026"));
    OA_CHECK(registry::valid_key_id("a.b"));
    OA_CHECK(registry::valid_key_id("a..b"));
    OA_CHECK(registry::valid_key_id("a-"));
    OA_CHECK(registry::valid_key_id(std::string(64, 'a')));
    OA_CHECK(!registry::valid_key_id(""));
    OA_CHECK(!registry::valid_key_id(std::string(65, 'a')));
    OA_CHECK(!registry::valid_key_id(".a"));
    OA_CHECK(!registry::valid_key_id("-a"));
    OA_CHECK(!registry::valid_key_id("A"));
    OA_CHECK(!registry::valid_key_id("a_b"));
}

void quoting_round_trips() {
    registry::Descriptor out;
    OA_CHECK(
        registry::read_descriptor(
            bytes_of(
                "registry: 1\nid: 'example-maps'\nname: \"Example Maps\"\n"
                "catalogue: http://downloads.example.net/v1/catalogue.json\n"
                "downloads:\n  mode: \"direct\"\n"
            ),
            out,
            nullptr
        )
    );
    OA_CHECK(out.id == "example-maps");
    OA_CHECK(out.name == "Example Maps");
    out.name = "say \"hi\" \\ there";
    registry::Descriptor again;
    OA_CHECK(registry::read_descriptor(bytes_of(registry::descriptor_text(out)), again, nullptr));
    OA_CHECK(again.name == out.name);
}

std::string read_text(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary);
    OA_CHECK(static_cast<bool>(output));
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct Scratch {
    std::filesystem::path path{};

    Scratch() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("oa-registry-" + std::to_string(tick));
        std::filesystem::create_directories(path);
    }

    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string player_sample() {
    return "registries:\n"
           "  - id: \"example-maps\"\n"
           "    name: \"Example Maps\"\n"
           "    url: \"http://maps.example.org/oa/registry.yaml\"\n"
           "    homepage: \"http://maps.example.org/\"\n"
           "    catalogue: \"http://maps.example.org/oa/v1/catalogue.json\"\n"
           "    mirrors: []\n"
           "    player-mirrors: []\n"
           "    trusted-keys:\n"
           "      - {id: \"maps-2026\", public: \"" +
           key_text(sample_key()) +
           "\"}\n"
           "    downloads: {mode: \"direct\"}\n"
           "    added: \"2026-11-03\"\n"
           "    enabled: true\n"
           "disabled: [\"example\"]\n";
}

bool same_player(const registry::PlayerRegistries& left, const registry::PlayerRegistries& right) {
    if (left.disabled != right.disabled || left.registries.size() != right.registries.size())
        return false;
    for (std::size_t index = 0; index < left.registries.size(); ++index) {
        const registry::AddedRegistry& a = left.registries[index];
        const registry::AddedRegistry& b = right.registries[index];
        if (a.url != b.url || a.added != b.added || a.enabled != b.enabled ||
            a.player_mirrors != b.player_mirrors || !same_descriptor(a.descriptor, b.descriptor))
            return false;
    }
    return true;
}

void player_sample_reads_and_writes() {
    registry::PlayerRegistries out;
    std::string error;
    OA_CHECK(registry::read_player_registries(bytes_of(player_sample()), out, &error));
    OA_CHECK(out.registries.size() == 1);
    if (out.registries.empty())
        return;
    const registry::AddedRegistry& added = out.registries[0];
    OA_CHECK(added.descriptor.id == "example-maps");
    OA_CHECK(added.descriptor.name == "Example Maps");
    OA_CHECK(added.url == "http://maps.example.org/oa/registry.yaml");
    OA_CHECK(added.descriptor.homepage.has_value());
    OA_CHECK(added.descriptor.mirrors.empty());
    OA_CHECK(added.player_mirrors.empty());
    OA_CHECK(added.descriptor.keys.size() == 1);
    OA_CHECK(added.descriptor.mode == registry::DownloadMode::direct);
    OA_CHECK(added.descriptor.install_id == registry::InstallIdUse::none);
    OA_CHECK(!added.descriptor.api.has_value());
    OA_CHECK(added.added == "2026-11-03");
    OA_CHECK(added.enabled);
    OA_CHECK(out.disabled.size() == 1);
    if (!out.disabled.empty())
        OA_CHECK(out.disabled[0] == "example");
    registry::PlayerRegistries again;
    OA_CHECK(
        registry::read_player_registries(
            bytes_of(registry::player_registries_text(out)), again, &error
        )
    );
    OA_CHECK(same_player(out, again));
}

void plain_empty_lists_read() {
    registry::PlayerRegistries out;
    OA_CHECK(
        registry::read_player_registries(
            bytes_of("registries: []\ndisabled: [example]\n"), out, nullptr
        )
    );
    OA_CHECK(out.registries.empty());
    OA_CHECK(out.disabled.size() == 1);
    if (!out.disabled.empty())
        OA_CHECK(out.disabled[0] == "example");
    registry::PlayerRegistries only_disabled;
    OA_CHECK(
        registry::read_player_registries(
            bytes_of("disabled: ['example']\n"), only_disabled, nullptr
        )
    );
    OA_CHECK(only_disabled.registries.empty());
    OA_CHECK(only_disabled.disabled.size() == 1);
}

void player_entries_are_refused() {
    const std::string entry = "registries:\n  - id: example-maps\n    name: Example Maps\n"
                              "    catalogue: http://maps.example.org/oa/v1/catalogue.json\n"
                              "    trusted-keys: []\n    downloads: {mode: direct}\n"
                              "    added: \"2026-11-03\"\n    enabled: true\n";
    registry::PlayerRegistries out;
    out.disabled.push_back("keep");
    std::string error;
    OA_CHECK(!registry::read_player_registries(
        bytes_of(
            "registries:\n  - id: example-maps\n    name: Example Maps\n"
            "    trusted-keys: []\n    downloads: {mode: direct}\n"
            "    added: \"2026-11-03\"\n    enabled: true\n"
        ),
        out,
        &error
    ));
    OA_CHECK(error.find("catalogue") != std::string::npos);
    OA_CHECK(out.disabled.size() == 1);
    error.clear();
    OA_CHECK(!registry::read_player_registries(bytes_of(entry + "extra: 1\n"), out, &error));
    OA_CHECK(error.find("extra") != std::string::npos);
    error.clear();
    OA_CHECK(!registry::read_player_registries(
        bytes_of(
            "registries:\n  - id: example-maps\n    name: Example Maps\n"
            "    catalogue: http://maps.example.org/oa/v1/catalogue.json\n"
            "    trusted-keys: []\n    downloads: {mode: direct}\n"
            "    added: \"2026-11-03\"\n    enabled: \"true\"\n"
        ),
        out,
        &error
    ));
    OA_CHECK(error.find("enabled") != std::string::npos);
    error.clear();
    OA_CHECK(!registry::read_player_registries(
        bytes_of(
            "registries:\n  - id: example-maps\n    name: Example Maps\n"
            "    catalogue: http://maps.example.org/oa/v1/catalogue.json\n"
            "    trusted-keys: []\n    downloads: {mode: direct}\n"
            "    added: \"2026-13-01\"\n    enabled: true\n"
        ),
        out,
        &error
    ));
    OA_CHECK(error.find("added") != std::string::npos);
    error.clear();
    OA_CHECK(!registry::read_player_registries(
        bytes_of(
            "registries:\n  - id: example-maps\n    name: Example Maps\n"
            "    catalogue: http://maps.example.org/oa/v1/catalogue.json\n"
            "    downloads: {mode: direct}\n    added: \"2026-11-03\"\n    enabled: true\n"
        ),
        out,
        &error
    ));
    OA_CHECK(error.find("trusted-keys") != std::string::npos);
    OA_CHECK(out.disabled.size() == 1);
}

void nine_player_mirrors_are_kept() {
    std::string yaml = "registries:\n  - id: example-maps\n    name: Example Maps\n"
                       "    catalogue: http://maps.example.org/oa/v1/catalogue.json\n"
                       "    trusted-keys: []\n    player-mirrors:\n";
    for (int index = 1; index <= 9; ++index)
        yaml += "      - http://mirror" + std::to_string(index) + ".example.org/catalogue.json\n";
    yaml += "    downloads: {mode: direct}\n    added: \"2026-11-03\"\n    enabled: false\n";
    registry::PlayerRegistries out;
    OA_CHECK(registry::read_player_registries(bytes_of(yaml), out, nullptr));
    OA_CHECK(out.registries.size() == 1);
    if (!out.registries.empty()) {
        OA_CHECK(out.registries[0].player_mirrors.size() == 9);
        OA_CHECK(!out.registries[0].enabled);
    }
}

void missing_and_malformed_files() {
    const Scratch scratch;
    registry::PlayerRegistries out;
    out.disabled.push_back("keep");
    std::string error = "set";
    OA_CHECK(
        registry::load_player_registries(scratch.path / "missing.yaml", out, &error) ==
        registry::LoadResult::missing
    );
    OA_CHECK(out.registries.empty());
    OA_CHECK(out.disabled.empty());

    const auto malformed = scratch.path / "broken.yaml";
    write_text(malformed, "this: [\n");
    out.disabled.push_back("keep");
    OA_CHECK(
        registry::load_player_registries(malformed, out, &error) == registry::LoadResult::unreadable
    );
    OA_CHECK(out.disabled.size() == 1);
    if (!out.disabled.empty())
        OA_CHECK(out.disabled[0] == "keep");
}

void save_creates_and_replaces() {
    const Scratch scratch;
    const auto file = scratch.path / "fresh" / "Registries.yaml";
    registry::PlayerRegistries list;
    OA_CHECK(registry::read_player_registries(bytes_of(player_sample()), list, nullptr));
    std::string error;
    OA_CHECK(registry::save_player_registries(file, list, &error));
    registry::PlayerRegistries loaded;
    OA_CHECK(registry::load_player_registries(file, loaded, &error) == registry::LoadResult::read);
    OA_CHECK(same_player(list, loaded));
    list.registries[0].enabled = false;
    OA_CHECK(registry::save_player_registries(file, list, &error));
    registry::PlayerRegistries replaced;
    OA_CHECK(
        registry::load_player_registries(file, replaced, &error) == registry::LoadResult::read
    );
    OA_CHECK(same_player(list, replaced));
    OA_CHECK(!replaced.registries.empty());
    if (!replaced.registries.empty())
        OA_CHECK(!replaced.registries[0].enabled);
}

void save_leaves_an_unreadable_file() {
    const Scratch scratch;
    const auto file = scratch.path / "Registries.yaml";
    const std::string garbage(256U * 1024U + 1, 'x');
    write_text(file, garbage);
    registry::PlayerRegistries list;
    std::string error;
    OA_CHECK(
        registry::load_player_registries(file, list, &error) == registry::LoadResult::unreadable
    );
    OA_CHECK(error.find("256 KiB") != std::string::npos);
    OA_CHECK(!registry::save_player_registries(file, list, &error));
    OA_CHECK(read_text(file) == garbage);
    write_text(file, "this: [\n");
    const std::string before = read_text(file);
    OA_CHECK(!registry::save_player_registries(file, list, &error));
    OA_CHECK(read_text(file) == before);
}

} // namespace

int main() {
    design_sample_reads_and_writes();
    address_includes_a_port();
    missing_required_keys_are_refused();
    registry_version_must_be_one();
    unknown_key_is_refused();
    bad_id_and_name_are_refused();
    catalogue_rules_are_refused();
    download_rules_are_refused_and_defaulted();
    key_rules_are_refused();
    mirror_count_is_capped_at_eight();
    a_descriptor_over_64_kib_is_refused();
    ids_match_their_rules();
    quoting_round_trips();
    player_sample_reads_and_writes();
    plain_empty_lists_read();
    player_entries_are_refused();
    nine_player_mirrors_are_kept();
    missing_and_malformed_files();
    save_creates_and_replaces();
    save_leaves_an_unreadable_file();
    return oa::test::check_exit_status();
}
