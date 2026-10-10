// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The registries the game ships with match the keys compiled into the
// engine. The folder of descriptors is the first argument.

#include "oa/base/signing/ed25519.hpp"
#include "oa/data/registry/trust.hpp"
#include "oa/formats/url.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace registry = oa::data::registry;
namespace signing = oa::base::signing;
namespace url = oa::formats::url;

char lower_letter(char letter) noexcept {
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

bool same_id(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lower_letter(left[index]) != lower_letter(right[index]))
            return false;
    }
    return true;
}

bool same_key(const signing::PublicKey& left, const signing::PublicKey& right) noexcept {
    return signing::public_key_text(left).view() == signing::public_key_text(right).view();
}

std::size_t yaml_files(const std::filesystem::path& folder) {
    std::size_t count = 0;
    std::error_code failure;
    const std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator cursor(folder, failure); !failure && cursor != end;
         cursor.increment(failure)) {
        std::error_code status_error;
        const bool regular = cursor->is_regular_file(status_error);
        if (status_error || !regular)
            continue;
        if (cursor->path().extension() == ".yaml")
            ++count;
    }
    if (failure)
        return static_cast<std::size_t>(-1);
    return count;
}

bool keys_match(
    const registry::Descriptor& descriptor, std::span<const registry::PinnedKey> pinned
) {
    std::vector<const registry::PinnedKey*> rows;
    for (const registry::PinnedKey& pin : pinned) {
        if (same_id(pin.registry, descriptor.id))
            rows.push_back(&pin);
    }
    if (rows.size() != descriptor.keys.size())
        return false;
    std::vector<uint8_t> used(rows.size(), 0);
    for (const registry::RegistryKey& key : descriptor.keys) {
        bool found = false;
        for (std::size_t index = 0; index < rows.size(); ++index) {
            if (used[index] != 0)
                continue;
            const auto parsed = signing::parse_public_key(rows[index]->public_key);
            if (!parsed || rows[index]->key_id != key.id || !same_key(key.key, *parsed))
                continue;
            used[index] = 1;
            found = true;
            break;
        }
        if (!found)
            return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "data-registry-builtin: pass the registries folder\n";
        return 1;
    }
    const std::filesystem::path folder = argv[1];
    std::vector<std::string> problems;
    const std::vector<registry::Descriptor> descriptors =
        registry::read_builtin_registries(folder, &problems);
    for (const std::string& problem : problems)
        std::cerr << problem << '\n';
    OA_CHECK(problems.empty());
    const std::size_t files = yaml_files(folder);
    OA_CHECK(files != static_cast<std::size_t>(-1));
    OA_CHECK(descriptors.size() == files);

    const std::span<const registry::PinnedKey> pinned = registry::pinned_keys();
    for (const registry::Descriptor& descriptor : descriptors) {
        bool pinned_id = false;
        for (const registry::PinnedKey& pin : pinned) {
            if (same_id(pin.registry, descriptor.id))
                pinned_id = true;
        }
        OA_CHECK(pinned_id);
        OA_CHECK(keys_match(descriptor, pinned));
    }
    for (const registry::PinnedKey& pin : pinned) {
        bool described = false;
        for (const registry::Descriptor& descriptor : descriptors) {
            if (same_id(descriptor.id, pin.registry))
                described = true;
        }
        OA_CHECK(described);
    }

    const registry::Descriptor* coreprime = nullptr;
    for (const registry::Descriptor& descriptor : descriptors) {
        if (descriptor.id == "coreprime")
            coreprime = &descriptor;
    }
    OA_CHECK(coreprime != nullptr);
    if (coreprime != nullptr) {
        OA_CHECK(coreprime->mode == registry::DownloadMode::tokens);
        OA_CHECK(coreprime->install_id == registry::InstallIdUse::required);
        OA_CHECK(
            url::url_text(coreprime->catalogue) ==
            "http://downloads.coreprime.net/v1/catalogue.json"
        );
        OA_CHECK(coreprime->api.has_value());
        if (coreprime->api.has_value())
            OA_CHECK(url::url_text(*coreprime->api) == "http://downloads.coreprime.net/api/v1");
    }
    return oa::test::check_exit_status();
}
