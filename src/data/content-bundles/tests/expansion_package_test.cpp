// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The expansion manifest and the file match. The fixture's identity is
// Cavedog units, so the manifest reads against the built-in table. Its
// bytes match only the test table. A stray file, a missing file, a wrong
// size, a wrong digest, a repeated name and an unsafe name are each refused.

#include "oa/data/content_bundles/expansion_fixture.hpp"

#include "oa/base/sha256.hpp"
#include "oa/data/content_bundles.hpp"
#include "oa/data/content_bundles/expansion_package.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace bundles = oa::data::content_bundles;
namespace fixture = oa::test::expansion_fixture;
namespace sha = oa::base::sha256;

using bundles::ExpansionFault;
using bundles::MeasuredExpansionFile;

std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

MeasuredExpansionFile unit_file() {
    const bundles::Bundle& bundle = fixture::test_bundle();
    MeasuredExpansionFile file;
    file.name = bundle.files[0].copy_name;
    file.size = bundle.files[0].size;
    file.digest = bundle.files[0].sha256[0];
    return file;
}

void test_manifest() {
    bundles::ExpansionManifest manifest;
    std::string error = "set";
    OA_CHECK(bundles::read_expansion_manifest(fixture::manifest_bytes(), manifest, &error));
    OA_CHECK(error == "set");
    OA_CHECK(manifest.id == "cavedog-units");
    OA_CHECK(manifest.name == "Cavedog units");
    OA_CHECK(manifest.version == "1");
    OA_CHECK(manifest.extends.size() == 1 && manifest.extends[0] == "totala");
    OA_CHECK(manifest.needs.size() == 1 && manifest.needs[0] == "core-contingency");
    OA_CHECK(manifest.author.name == "Cavedog");
    OA_CHECK(manifest.author.email == "ridge@example.com");
    OA_CHECK(manifest.packaging.revision == 1);
    OA_CHECK(manifest.packaging.date == "2026-10-13");
    OA_CHECK(manifest.packaging.packager == "Core Prime");
    OA_CHECK(manifest.bundle != nullptr && manifest.bundle->id == "cavedog-units");
    OA_CHECK(bundles::expansion_identity_matches(manifest, fixture::test_bundle()));
    const bundles::Bundle* music = bundles::find_bundle("music");
    OA_CHECK(music != nullptr);
    OA_CHECK(!bundles::expansion_identity_matches(manifest, *music));

    std::string ridge(fixture::manifest_text());
    const auto id_at = ridge.find("id: cavedog-units");
    OA_CHECK(id_at != std::string::npos);
    if (id_at != std::string::npos)
        ridge.replace(id_at, std::string("id: cavedog-units").size(), "id: ridge");
    error = "set";
    bundles::ExpansionManifest refused;
    refused.id = "kept";
    OA_CHECK(!bundles::read_expansion_manifest(bytes_of(ridge), refused, &error));
    OA_CHECK(refused.id == "kept");
    OA_CHECK(error == "It is not one of the game's expansions.");

    std::string unquoted(fixture::manifest_text());
    const auto version_at = unquoted.find("version: \"1\"");
    OA_CHECK(version_at != std::string::npos);
    if (version_at != std::string::npos)
        unquoted.replace(version_at, std::string("version: \"1\"").size(), "version: 1");
    error = "set";
    OA_CHECK(!bundles::read_expansion_manifest(bytes_of(unquoted), refused, &error));
    OA_CHECK(error.find("version") != std::string::npos);

    std::string extra(fixture::manifest_text());
    extra += "notes: no\n";
    error = "set";
    OA_CHECK(!bundles::read_expansion_manifest(bytes_of(extra), refused, &error));
    OA_CHECK(error.find("key") != std::string::npos);
}

void test_match() {
    const bundles::Bundle& bundle = fixture::test_bundle();
    const MeasuredExpansionFile good = unit_file();
    const auto matched = bundles::match_expansion_files(bundle, std::span(&good, 1));
    OA_CHECK(matched.fault == ExpansionFault::ok);

    const MeasuredExpansionFile stray_files[] = {good, {"extra.txt", 1, good.digest}};
    const auto stray = bundles::match_expansion_files(bundle, stray_files);
    OA_CHECK(stray.fault == ExpansionFault::stray);
    OA_CHECK(stray.name == "extra.txt");

    const auto missing = bundles::match_expansion_files(bundle, {});
    OA_CHECK(missing.fault == ExpansionFault::missing);
    OA_CHECK(missing.name == "unit.fbi");

    MeasuredExpansionFile wrong_size = good;
    wrong_size.size = 3;
    const auto sized = bundles::match_expansion_files(bundle, std::span(&wrong_size, 1));
    OA_CHECK(sized.fault == ExpansionFault::size);
    OA_CHECK(sized.name == "unit.fbi");

    static constexpr uint8_t other[] = {'a', 'b', 'c', 'e'};
    MeasuredExpansionFile wrong_hash = good;
    wrong_hash.digest = sha::digest_of(std::span<const uint8_t>(other, 4));
    const auto hashed = bundles::match_expansion_files(bundle, std::span(&wrong_hash, 1));
    OA_CHECK(hashed.fault == ExpansionFault::hash);
    OA_CHECK(hashed.name == "unit.fbi");

    const MeasuredExpansionFile twice[] = {good, good};
    const auto duplicate = bundles::match_expansion_files(bundle, twice);
    OA_CHECK(duplicate.fault == ExpansionFault::duplicate);
    OA_CHECK(duplicate.name == "unit.fbi");

    const MeasuredExpansionFile unsafe_file{"..", 1, good.digest};
    const auto unsafe = bundles::match_expansion_files(bundle, std::span(&unsafe_file, 1));
    OA_CHECK(unsafe.fault == ExpansionFault::unsafe);
    OA_CHECK(unsafe.name == "..");

    const bundles::Bundle* real = bundles::find_bundle("cavedog-units");
    OA_CHECK(real != nullptr);
    if (real != nullptr) {
        const auto unknown = bundles::match_expansion_files(*real, std::span(&good, 1));
        OA_CHECK(unknown.fault == ExpansionFault::stray);
        OA_CHECK(unknown.name == "unit.fbi");
    }
}

} // namespace

int main() {
    test_manifest();
    test_match();
    return oa::test::check_exit_status();
}
