// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The .oa-content record: a round trip, a record this reader does not
// understand, and a field it skips.

#include "oa/data/content_bundles/record.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

namespace {

using oa::data::content_bundles::ContentRecord;
using oa::data::content_bundles::ContentRecordFile;
using oa::data::content_bundles::format_content_record;
using oa::data::content_bundles::parse_content_record;
using oa::data::content_bundles::managed_games_folder;
using oa::data::content_bundles::read_bundle_folder_record;

constexpr std::string_view kHash =
    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";

ContentRecord sample() {
    ContentRecord record;
    record.bundle_id = "totala";
    record.source = "line\nslash\\end";
    ContentRecordFile item;
    item.copy_name = "rev31.gp3";
    item.size = 42;
    item.modified = -5;
    item.digest = *oa::base::sha256::parse_hex(kHash);
    record.files.push_back(item);
    ContentRecordFile oldest;
    oldest.copy_name = "totala1.hpi";
    oldest.size = 0;
    oldest.modified = std::numeric_limits<int64_t>::min();
    oldest.digest = *oa::base::sha256::parse_hex(kHash);
    record.files.push_back(std::move(oldest));
    return record;
}

void test_round_trip() {
    const ContentRecord record = sample();
    const std::optional<std::string> text = format_content_record(record);
    OA_CHECK(text.has_value());
    const ContentRecord read = parse_content_record(*text);
    OA_CHECK(read.valid);
    OA_CHECK(!read.present);
    OA_CHECK(read.bundle_id == record.bundle_id);
    OA_CHECK(read.source == record.source);
    OA_CHECK(read.files.size() == record.files.size());
    for (std::size_t index = 0; index < record.files.size(); ++index) {
        OA_CHECK(read.files[index].copy_name == record.files[index].copy_name);
        OA_CHECK(read.files[index].size == record.files[index].size);
        OA_CHECK(read.files[index].modified == record.files[index].modified);
        OA_CHECK(read.files[index].digest == record.files[index].digest);
    }

    ContentRecord bare;
    bare.bundle_id = "totala";
    bare.source = "";
    const std::optional<std::string> empty_files = format_content_record(bare);
    OA_CHECK(empty_files.has_value());
    const ContentRecord parsed_bare = parse_content_record(*empty_files);
    OA_CHECK(parsed_bare.valid);
    OA_CHECK(parsed_bare.files.empty());
    OA_CHECK(parsed_bare.source.empty());

    ContentRecord broken_name = record;
    broken_name.files[0].copy_name = "has\nline";
    OA_CHECK(!format_content_record(broken_name).has_value());
}

void test_malformed() {
    OA_CHECK(!parse_content_record("not a record\n").valid);
    OA_CHECK(!parse_content_record("").valid);
    OA_CHECK(!parse_content_record("oa-content 1\nbundle \nsource disc\n").valid);
    OA_CHECK(!parse_content_record("oa-content 1\nsource disc\nbundle totala\n").valid);
    const std::string truncated =
        "oa-content 1\nbundle totala\nsource disc\nfile rev31.gp3\nsize 1\n";
    OA_CHECK(!parse_content_record(truncated).valid);
    const std::string bad_hash =
        "oa-content 1\nbundle totala\nsource disc\nfile rev31.gp3\nsize 1\nmodified 0\nsha256 "
        "zz\n";
    OA_CHECK(!parse_content_record(bad_hash).valid);
    const std::string bad_escape = "oa-content 1\nbundle totala\nsource bad\\q\n";
    OA_CHECK(!parse_content_record(bad_escape).valid);
    const std::string stray_size = "oa-content 1\nbundle totala\nsource disc\nsize 4\n";
    OA_CHECK(!parse_content_record(stray_size).valid);
}

void test_unknown_field() {
    const std::string text = "oa-content 1\n"
                             "bundle totala\n"
                             "origin later-reader\n"
                             "source disc\n"
                             "file rev31.gp3\n"
                             "size 42\n"
                             "modified -5\n"
                             "sha256 " +
                             std::string(kHash) +
                             "\n"
                             "kept yes\n";
    const ContentRecord read = parse_content_record(text);
    OA_CHECK(read.valid);
    OA_CHECK(read.bundle_id == "totala");
    OA_CHECK(read.source == "disc");
    OA_CHECK(read.files.size() == 1);
    OA_CHECK(read.files[0].copy_name == "rev31.gp3");
    OA_CHECK(read.files[0].size == 42);
    OA_CHECK(read.files[0].modified == -5);
    OA_CHECK(!read.files[0].not_in_source);
}

// "not-in-source" follows a file the source did not hold. A record from
// before that line reads the file as held, and a reader that skips an
// unknown line still has the size and the hash.
void test_not_in_source() {
    const std::string held = "oa-content 1\n"
                             "bundle music\n"
                             "source disc\n"
                             "file music/9.mp3\n"
                             "size 4\n"
                             "modified 3\n"
                             "sha256 " +
                             std::string(kHash) + "\n";
    const ContentRecord older = parse_content_record(held);
    OA_CHECK(older.valid);
    OA_CHECK(older.files.size() == 1);
    OA_CHECK(!older.files[0].not_in_source);
    const std::optional<std::string> held_again = format_content_record(older);
    OA_CHECK(held_again.has_value());
    OA_CHECK(held_again->find("not-in-source") == std::string::npos);

    ContentRecord record = older;
    ContentRecordFile absent;
    absent.copy_name = "music/13.mp3";
    absent.size = 0;
    absent.modified = 0;
    absent.digest = *oa::base::sha256::parse_hex(kHash);
    absent.not_in_source = true;
    record.files.push_back(absent);
    const std::optional<std::string> text = format_content_record(record);
    OA_CHECK(text.has_value());
    OA_CHECK(text->find("not-in-source\n") != std::string::npos);
    const ContentRecord read = parse_content_record(*text);
    OA_CHECK(read.valid);
    OA_CHECK(read.files.size() == 2);
    OA_CHECK(!read.files[0].not_in_source);
    OA_CHECK(read.files[1].copy_name == "music/13.mp3");
    OA_CHECK(read.files[1].not_in_source);
    OA_CHECK(read.files[1].digest == absent.digest);
}

// A copy stores a record after each file. While the next file is still being
// written, the record names only the files already stored. A bundle with no
// file stored yet is the header alone; the copy does not write that file.
void test_in_progress() {
    const std::string text = "oa-content 1\n"
                             "bundle totala\n"
                             "source /games/Total Annihilation\n"
                             "file rev31.gp3\n"
                             "size 14\n"
                             "modified 1600000000\n"
                             "sha256 " +
                             std::string(kHash) + "\n";
    const ContentRecord read = parse_content_record(text);
    OA_CHECK(read.valid);
    OA_CHECK(read.bundle_id == "totala");
    OA_CHECK(read.source == "/games/Total Annihilation");
    OA_CHECK(read.files.size() == 1);
    OA_CHECK(read.files[0].copy_name == "rev31.gp3");
    OA_CHECK(read.files[0].size == 14);
    OA_CHECK(read.files[0].modified == 1600000000);
    const std::optional<std::string> again = format_content_record(read);
    OA_CHECK(again.has_value());
    OA_CHECK(*again == text);

    const ContentRecord header = parse_content_record("oa-content 1\nbundle totala\nsource disc\n");
    OA_CHECK(header.valid);
    OA_CHECK(header.files.empty());
    OA_CHECK(header.bundle_id == "totala");
    OA_CHECK(header.source == "disc");
}

void test_folder() {
    const std::filesystem::path folder = oa::test::make_scratch_directory("G25-content-record");
    std::error_code failure;
    std::filesystem::remove_all(folder, failure);
    std::filesystem::create_directories(folder, failure);
    OA_CHECK(!failure);
    const ContentRecord absent_tree =
        read_bundle_folder_record(folder / std::string(managed_games_folder) / "totala", nullptr);
    OA_CHECK(!absent_tree.present);
    OA_CHECK(!absent_tree.valid);
    OA_CHECK(!absent_tree.io_error);
    const ContentRecord missing = read_bundle_folder_record(folder, nullptr);
    OA_CHECK(!missing.present);
    OA_CHECK(!missing.valid);

    const ContentRecord record = sample();
    const std::optional<std::string> text = format_content_record(record);
    OA_CHECK(text.has_value());
    const std::filesystem::path path =
        folder / std::string(oa::data::content_bundles::content_record_name);
    std::ofstream out(path, std::ios::binary);
    out << *text;
    out.close();
    std::string error = "left";
    const ContentRecord read = read_bundle_folder_record(folder, &error);
    OA_CHECK(error.empty());
    OA_CHECK(read.present);
    OA_CHECK(read.valid);
    OA_CHECK(!read.io_error);
    OA_CHECK(read.bundle_id == "totala");
    OA_CHECK(read.files.size() == 2);
    OA_CHECK(read.source == record.source);
    std::filesystem::remove_all(folder, failure);
}

} // namespace

int main() {
    test_round_trip();
    test_malformed();
    test_unknown_field();
    test_not_in_source();
    test_in_progress();
    test_folder();
    return oa::test::check_exit_status();
}
