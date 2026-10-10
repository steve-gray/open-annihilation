// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The files a language pack answers for the game data's language folders
// (language_packs.hpp): a briefing under camps/briefs-<word>, as a mission
// asks for it, is read from the pack's files folder whatever the case of its
// names; another word's folder, a path with no language folder, a folder
// itself and a path that climbs out of the files folder are answered by
// none, so the game data's own files in another language stay as they are.
//
// The command line names the folder that holds the pseudo-language's pack.

#include "language_packs.hpp"

#include "oa/platform/text_font.hpp"
#include "oa/test/check.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using oa::app::LoadedLanguagePack;

/// The folder that holds the pseudo-language's pack, from the command line.
fs::path packs_folder;

/// Reads the pseudo-language's pack.
///
/// @return the pack; the check fails without it
std::unique_ptr<LoadedLanguagePack> pseudo_pack() {
    std::vector<std::unique_ptr<LoadedLanguagePack>> packs;
    oa::app::read_language_packs(packs_folder, packs, nullptr);
    OA_CHECK(packs.size() == 1);
    if (packs.empty())
        return nullptr;
    OA_CHECK(packs.front()->pack.manifest().word == "Pseudo");
    return std::move(packs.front());
}

/// A briefing in the pack's own word is answered at any depth of its path.
void own_briefing_is_read() {
    const auto pack = pseudo_pack();
    if (pack == nullptr)
        return;
    const fs::path briefing = oa::app::language_pack_file(*pack, "camps/briefs-Pseudo/brief.TXT");
    OA_CHECK(!briefing.empty());
    std::string failure;
    const auto bytes = oa::app::read_pack_file(briefing, failure);
    OA_CHECK(bytes.has_value() && !bytes->empty() && failure.empty());
    OA_CHECK(!oa::app::language_pack_file(*pack, "CAMPS\\BRIEFS-PSEUDO\\BRIEF.TXT").empty());
}

/// Every other path is the game data's alone.
void other_paths_are_not_answered() {
    const auto pack = pseudo_pack();
    if (pack == nullptr)
        return;
    const auto answered = [&](std::string_view path) {
        return !oa::app::language_pack_file(*pack, path).empty();
    };
    OA_CHECK(!answered("camps/briefs-German/brief.txt"));
    OA_CHECK(!answered("camps/brief.txt"));
    OA_CHECK(!answered("brief.txt"));
    OA_CHECK(!answered("camps/briefs-Pseudo"));
    OA_CHECK(!answered("camps/briefs-Pseudo/missing.txt"));
    OA_CHECK(!answered("camps/briefs-Pseudo/../briefs-Pseudo/brief.txt"));
    OA_CHECK(!answered("camps/-Pseudo/brief.txt"));
}

/// The pseudo pack's warm-up text is the file the manifest names.
void the_pseudo_pack_reads_its_warmup() {
    const auto pack = pseudo_pack();
    if (pack == nullptr)
        return;
    OA_CHECK(pack->warmup == "测试［］\n");
    OA_CHECK(oa::app::pack_fonts(*pack).empty());
}

/// A folder of one pack, removed when this dies.
struct TemporaryPack {
    fs::path root;

    TemporaryPack() {
        static int made = 0;
        root = fs::temp_directory_path() /
               ("oa-lang-pack-" + std::to_string(++made) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "en-XA");
    }

    ~TemporaryPack() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    TemporaryPack(const TemporaryPack&) = delete;
    TemporaryPack& operator=(const TemporaryPack&) = delete;

    /// Writes the pack's manifest, with extra keys after the required ones.
    void write(std::string_view extra) const {
        std::ofstream manifest(root / "en-XA" / "language.yaml");
        manifest << "oalang: 1\ntag: en-XA\nword: Pseudo\n" << extra;
    }
};

/// A listed font that is not a file leaves the pack out; the file present, it is read.
void a_missing_font_leaves_the_pack_out() {
    TemporaryPack pack;
    pack.write("fonts:\n  - {file: Ridge.otf, role: letters}\n");
    std::vector<std::unique_ptr<LoadedLanguagePack>> missing;
    oa::app::read_language_packs(pack.root, missing, nullptr);
    OA_CHECK(missing.empty());
    fs::create_directories(pack.root / "en-XA" / "fonts");
    {
        std::ofstream font(pack.root / "en-XA" / "fonts" / "Ridge.otf", std::ios::binary);
        font << "not a font\n";
    }
    std::vector<std::unique_ptr<LoadedLanguagePack>> present;
    oa::app::read_language_packs(pack.root, present, nullptr);
    OA_CHECK(present.size() == 1);
    if (present.size() != 1)
        return;
    const auto fonts = oa::app::pack_fonts(*present.front());
    OA_CHECK(fonts.size() == 1);
    if (fonts.size() != 1)
        return;
    OA_CHECK(fonts.front().role == oa::platform::text_font::FaceRole::letters);
    OA_CHECK(fonts.front().file.filename() == "Ridge.otf");
}

/// A warm-up past 4,096 bytes is refused, and 4,096 bytes of text is read.
void a_long_warm_up_is_refused() {
    TemporaryPack pack;
    pack.write("warmup: warmup.txt\n");
    const fs::path file = pack.root / "en-XA" / "warmup.txt";
    {
        std::ofstream out(file, std::ios::binary);
        out << std::string(4097, 'A');
    }
    std::vector<std::unique_ptr<LoadedLanguagePack>> refused;
    oa::app::read_language_packs(pack.root, refused, nullptr);
    OA_CHECK(refused.empty());
    {
        std::ofstream out(file, std::ios::binary);
        out << std::string(4096, 'B');
    }
    std::vector<std::unique_ptr<LoadedLanguagePack>> read;
    oa::app::read_language_packs(pack.root, read, nullptr);
    OA_CHECK(read.size() == 1);
    if (read.size() == 1)
        OA_CHECK(read.front()->warmup.size() == 4096);
}

/// A warm-up that is not UTF-8 is refused.
void a_warm_up_that_is_not_utf8_is_refused() {
    TemporaryPack pack;
    pack.write("warmup: warmup.txt\n");
    {
        std::ofstream out(pack.root / "en-XA" / "warmup.txt", std::ios::binary);
        out << "ok\x80";
    }
    std::vector<std::unique_ptr<LoadedLanguagePack>> packs;
    oa::app::read_language_packs(pack.root, packs, nullptr);
    OA_CHECK(packs.empty());
}

/// The pseudo pack's manifest is the registry entry of an installed language.
void the_pseudo_pack_adds_its_language() {
    std::vector<std::unique_ptr<LoadedLanguagePack>> packs;
    oa::app::read_language_packs(packs_folder, packs, nullptr);
    const auto entries = oa::app::installed_entries({&packs});
    OA_CHECK(entries.size() == 1);
    if (entries.empty())
        return;
    OA_CHECK(entries.front().tag == "en-XA");
    OA_CHECK(entries.front().word == "Pseudo");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr
            << "usage: oa-app-language-packs-test <folder holding the pseudo-language's pack>\n";
        return 2;
    }
    packs_folder = argv[1];
    own_briefing_is_read();
    other_paths_are_not_answered();
    the_pseudo_pack_reads_its_warmup();
    a_missing_font_leaves_the_pack_out();
    a_long_warm_up_is_refused();
    a_warm_up_that_is_not_utf8_is_refused();
    the_pseudo_pack_adds_its_language();
    return oa::test::check_exit_status();
}
