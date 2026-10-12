// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Lookups, both dependency closures, turning a bundle on and off, matching
// a file by name and size, and the table's own consistency. The demo
// archive's size and hash are the ones demo_1997 already checks.

#include "oa/app/demo_installer.hpp"
#include "oa/data/content_bundles.hpp"
#include "oa/test/check.hpp"

#include <initializer_list>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <string_view>

namespace {

using oa::data::content_bundles::Bundle;
using oa::data::content_bundles::ContentFile;
using oa::data::content_bundles::FileMatch;
using oa::data::content_bundles::Installer;
using oa::data::content_bundles::InstallerKind;
using oa::data::content_bundles::Kind;
using oa::data::content_bundles::after_turning_off;
using oa::data::content_bundles::after_turning_on;
using oa::data::content_bundles::bundles;
using oa::data::content_bundles::find_bundle;
using oa::data::content_bundles::find_installer;
using oa::data::content_bundles::installers;
using oa::data::content_bundles::known_hash;
using oa::data::content_bundles::known_installer_hash;
using oa::data::content_bundles::match_file;
using oa::data::content_bundles::removal_closure;
using oa::data::content_bundles::requirement_closure;
using oa::data::content_bundles::table_fault;

/// Reports whether `got` is `expected`, in that order.
///
/// @param got the ids a function returned
/// @param expected the ids the case wants
/// @return true when the two lists are the same
bool same_ids(std::span<const std::string_view> got, std::initializer_list<const char*> expected) {
    if (got.size() != expected.size())
        return false;
    size_t slot = 0;
    for (const char* id : expected) {
        if (got[slot] != id)
            return false;
        ++slot;
    }
    return true;
}

void test_find_bundle() {
    const std::span<const Bundle> table = bundles();
    OA_CHECK(table.size() == 8);
    OA_CHECK(table[0].id == "totala");
    OA_CHECK(table[0].english_name == "Total Annihilation 3.1c");
    OA_CHECK(table[0].kind == Kind::base_game);
    OA_CHECK(table[0].files.size() == 6);
    const Bundle* totala = find_bundle("totala");
    OA_CHECK(totala == &table[0]);
    const Bundle* units = find_bundle("cavedog-units");
    OA_CHECK(units != nullptr && units->kind == Kind::expansion);
    OA_CHECK(units->english_name == "Cavedog units");
    OA_CHECK(units->files.size() == 6);
    OA_CHECK(find_bundle("campaign")->files.size() == 3);
    OA_CHECK(find_bundle("core-contingency")->files.size() == 3);
    OA_CHECK(find_bundle("battle-tactics")->files.size() == 10);
    OA_CHECK(find_bundle("music")->files.size() == 18);
    OA_CHECK(find_bundle("cavedog-tilesets")->files.size() == 1);
    OA_CHECK(find_bundle("Totala") == nullptr);
    OA_CHECK(find_bundle("ridge") == nullptr);
}

void test_closures() {
    const std::string_view units_id[]{"cavedog-units"};
    const auto units_need = requirement_closure(units_id);
    OA_CHECK(same_ids(units_need, {"totala", "campaign", "core-contingency", "cavedog-units"}));

    const std::string_view campaign_id[]{"campaign"};
    const auto campaign_off = removal_closure(campaign_id);
    OA_CHECK(
        same_ids(campaign_off, {"campaign", "core-contingency", "battle-tactics", "cavedog-units"})
    );

    const std::string_view totala_id[]{"totala"};
    const auto totala_off = removal_closure(totala_id);
    OA_CHECK(same_ids(
        totala_off,
        {"totala",
         "campaign",
         "core-contingency",
         "battle-tactics",
         "music",
         "cavedog-units",
         "cavedog-tilesets"}
    ));

    const std::string_view demo_id[]{"totala-demo"};
    OA_CHECK(same_ids(requirement_closure(demo_id), {"totala-demo"}));
    OA_CHECK(same_ids(removal_closure(demo_id), {"totala-demo"}));

    const std::string_view both[]{"music", "cavedog-units"};
    const auto both_need = requirement_closure(both);
    OA_CHECK(
        same_ids(both_need, {"totala", "campaign", "core-contingency", "music", "cavedog-units"})
    );

    const std::string_view unknown[]{"ridge"};
    OA_CHECK(requirement_closure(unknown).empty());
    OA_CHECK(removal_closure(unknown).empty());
    OA_CHECK(table_fault().empty());
}

void test_turning_on_and_off() {
    const auto from_empty = after_turning_on({}, "campaign");
    OA_CHECK(same_ids(from_empty, {"totala", "campaign"}));

    const std::string_view base_on[]{"totala"};
    const auto waiting = after_turning_on(base_on, "campaign");
    OA_CHECK(same_ids(waiting, {"totala", "campaign", "core-contingency", "battle-tactics"}));

    const std::string_view all[]{
        "totala",
        "campaign",
        "core-contingency",
        "battle-tactics",
        "music",
        "cavedog-units",
        "cavedog-tilesets",
        "ridge"
    };
    const auto campaign_gone = after_turning_off(all, "campaign");
    OA_CHECK(same_ids(campaign_gone, {"totala", "music", "cavedog-tilesets"}));

    const auto unchanged = after_turning_off(base_on, "campaign");
    OA_CHECK(same_ids(unchanged, {"totala"}));
    OA_CHECK(same_ids(after_turning_on(base_on, "ridge"), {"totala"}));

    const auto units_on = after_turning_on({}, "cavedog-units");
    OA_CHECK(same_ids(units_on, {"totala", "campaign", "core-contingency", "cavedog-units"}));
}

void test_match_and_hash() {
    const FileMatch archive = match_file("TOTALA1.HPI", 32311035);
    OA_CHECK(archive);
    OA_CHECK(archive.bundle != nullptr && archive.bundle->id == "totala");
    OA_CHECK(archive.file->copy_name == "totala1.hpi");
    OA_CHECK(known_hash(*archive.file, archive.file->sha256[0]));
    oa::base::sha256::Digest other = archive.file->sha256[0];
    other[0] = static_cast<uint8_t>(other[0] ^ 0x01);
    OA_CHECK(!known_hash(*archive.file, other));

    const FileMatch movie = match_file("data\\1.ZRB", 6919100);
    OA_CHECK(movie && movie.file->copy_name == "Data/1.zrb");
    OA_CHECK(movie.file->source_names.size() == 1);
    OA_CHECK(movie.file->source_names[0] == "Data/1.ZRB");

    const FileMatch by_last = match_file("2.zrb", 41229000);
    OA_CHECK(by_last && by_last.file->copy_name == "Data/2.zrb");
    OA_CHECK(!match_file("foo/totala1.hpi", 32311035));
    OA_CHECK(!match_file("totala1.hpi", 1));
    OA_CHECK(!match_file("totala3.hpi", 1));
    OA_CHECK(!match_file("Example.ufo", 1));

    const FileMatch track_one = match_file("music/1.mp3", 2849041);
    const FileMatch track_ten = match_file("music/10.mp3", 2849041);
    OA_CHECK(track_one && track_ten);
    OA_CHECK(track_one.file != track_ten.file);
    OA_CHECK(track_one.file->sha256[0] == track_ten.file->sha256[0]);
    const FileMatch track_zero = match_file("music/0.mp3", 2576323);
    const FileMatch track_nine = match_file("music/9.mp3", 2576323);
    OA_CHECK(track_zero && track_nine && track_zero.file != track_nine.file);
    OA_CHECK(track_zero.file->sha256[0] == track_nine.file->sha256[0]);
}

void test_demo_archive_matches_todays_recognition() {
    const Bundle* demo = find_bundle("totala-demo");
    OA_CHECK(demo != nullptr);
    OA_CHECK(demo->kind == Kind::base_game);
    OA_CHECK(demo->extends.empty() && demo->needs.empty());
    OA_CHECK(demo->files.size() == 1);
    const ContentFile& unpacked = demo->files[0];
    OA_CHECK(unpacked.copy_name == oa::app::demo_1997.archive_name);
    OA_CHECK(unpacked.size == oa::app::demo_1997.archive_size);
    OA_CHECK(unpacked.sha256.size() == 1);
    OA_CHECK(unpacked.sha256[0] == oa::app::demo_1997.archive_sha256);
    OA_CHECK(known_hash(unpacked, oa::app::demo_1997.archive_sha256));
    OA_CHECK(unpacked.size != oa::app::demo_1997.installer_size);
}

void test_installers() {
    const std::span<const Installer> known = installers();
    OA_CHECK(known.size() == 3);
    const Installer* windows = find_installer(1093885504);
    OA_CHECK(windows != nullptr && windows->kind == InstallerKind::windows);
    OA_CHECK(windows == &known[0]);
    OA_CHECK(known_installer_hash(*windows, windows->sha256));
    oa::base::sha256::Digest other = windows->sha256;
    other[0] = static_cast<uint8_t>(other[0] ^ 0x01);
    OA_CHECK(!known_installer_hash(*windows, other));
    const Installer* macos = find_installer(2559350261);
    OA_CHECK(macos != nullptr && macos->kind == InstallerKind::macos);
    const Installer* older = find_installer(1144736288);
    OA_CHECK(older != nullptr && older->kind == InstallerKind::older_mac);
    OA_CHECK(find_installer(oa::app::demo_1997.installer_size) == nullptr);
    OA_CHECK(find_installer(1) == nullptr);
}

} // namespace

int main() {
    test_find_bundle();
    test_closures();
    test_turning_on_and_off();
    test_match_and_hash();
    test_demo_archive_matches_todays_recognition();
    test_installers();
    return oa::test::check_exit_status();
}
