// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Installing an expansion. The fixture package is refused by the built-in
// table, and nothing is written. The same package, checked against the test
// table, lands in expansions/<game>/<id> with the .oa-content record. A
// second install drops a file the table does not name. Uninstall removes
// that folder and leaves a sibling. A link in the expansion's place is left
// where it is. A package that also holds a stray file writes nothing.

#include "oa/app/package_install.hpp"
#include "oa/data/content_bundles/expansion_fixture.hpp"
#include "oa/app/package_install/oaexp.hpp"
#include "oa/data/content_bundles/record.hpp"
#include "oa/formats/zip.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace bundles = oa::data::content_bundles;
namespace fixture = oa::test::expansion_fixture;
namespace install = oa::app::package_install;
namespace zip = oa::formats::zip;
using install::Refusal;

class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-package-oaexp-")) {}

    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

  private:

    fs::path path_;
};

void write_text(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

bool write_zip(const fs::path& file, std::span<const zip::NewEntry> entries) {
    std::vector<uint8_t> archive;
    zip::ZipError error;
    if (!zip::write_archive(entries, archive, error))
        return false;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size())
    );
    return static_cast<bool>(out);
}

bool make_link(const fs::path& to, const fs::path& made_link) {
    std::error_code error;
    fs::create_directory_symlink(to, made_link, error);
    if (!error && !install::is_link_or_junction(made_link))
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    if (error)
        std::fprintf(stderr, "skipped: this system made no link: %s\n", error.message().c_str());
    return !error;
}

void test_install() {
    Scratch scratch;
    const fs::path package = scratch.path() / "cavedog-units.oaexp";
    const zip::NewEntry entries[] = {
        {"oaexp.yaml", fixture::manifest_bytes()},
        {"unit.fbi", fixture::unit_bytes()},
    };
    OA_CHECK(write_zip(package, entries));

    static constexpr uint8_t extra_byte[] = {'x'};
    const fs::path stray_package = scratch.path() / "stray.oaexp";
    const zip::NewEntry stray_entries[] = {
        {"oaexp.yaml", fixture::manifest_bytes()},
        {"unit.fbi", fixture::unit_bytes()},
        {"extra.txt", std::span<const uint8_t>(extra_byte, 1)},
    };
    OA_CHECK(write_zip(stray_package, stray_entries));

    const fs::path player = scratch.path() / "player";
    fs::create_directories(player);
    install::Problem problem;
    fs::path installed;
    OA_CHECK(!install::oaexp::install_oaexp(package, player, "core-prime", &installed, problem));
    OA_CHECK(fs::is_empty(player));

    OA_CHECK(!install::oaexp::install_expansion_package(
        stray_package, player, "core-prime", fixture::test_bundle(), &installed, problem
    ));
    OA_CHECK(problem.refusal == Refusal::stray_file);
    OA_CHECK(fs::is_empty(player));

    OA_CHECK(
        install::oaexp::install_expansion_package(
            package, player, "core-prime", fixture::test_bundle(), &installed, problem
        )
    );
    const fs::path folder = installed;
    OA_CHECK(folder.filename() == "cavedog-units");
    OA_CHECK(folder.parent_path().filename() == "totala");
    // The managed expansions folder, the name the installer uses.
    OA_CHECK(folder.parent_path().parent_path().filename() == "expansions");
    OA_CHECK(folder.parent_path().parent_path().parent_path() == player);
    OA_CHECK(read_text(folder / "unit.fbi") == "abcd");
    OA_CHECK(!fs::exists(folder / "oaexp.yaml"));
    const bundles::ContentRecord record = bundles::read_bundle_folder_record(folder, nullptr);
    OA_CHECK(record.valid);
    OA_CHECK(record.bundle_id == "cavedog-units");
    OA_CHECK(record.source == "registry core-prime");
    OA_CHECK(record.files.size() == 1);
    if (!record.files.empty()) {
        OA_CHECK(record.files[0].copy_name == "unit.fbi");
        OA_CHECK(record.files[0].size == 4);
    }

    write_text(folder / "junk.txt", "junk");
    OA_CHECK(
        install::oaexp::install_expansion_package(
            package, player, "core-prime", fixture::test_bundle(), &installed, problem
        )
    );
    OA_CHECK(!fs::exists(folder / "junk.txt"));
    OA_CHECK(read_text(folder / "unit.fbi") == "abcd");
    OA_CHECK(!fs::exists(folder.parent_path() / ".oaexp-staging-cavedog-units"));
    OA_CHECK(!fs::exists(folder.parent_path() / ".oaexp-old-cavedog-units"));

    const fs::path sibling = folder.parent_path() / "music";
    write_text(sibling / "keep.txt", "keep");
    OA_CHECK(install::oaexp::uninstall_expansion(player, "cavedog-units", problem));
    OA_CHECK(!fs::exists(folder));
    OA_CHECK(read_text(sibling / "keep.txt") == "keep");

    const fs::path linked_player = scratch.path() / "linked";
    const fs::path outside = scratch.path() / "outside";
    write_text(outside / "marker.txt", "keep");
    const fs::path link = linked_player / fs::relative(folder, player);
    fs::create_directories(link.parent_path());
    if (!make_link(outside, link))
        return;
    OA_CHECK(!install::oaexp::install_expansion_package(
        package, linked_player, "core-prime", fixture::test_bundle(), &installed, problem
    ));
    OA_CHECK(problem.refusal == Refusal::link);
    OA_CHECK(install::is_link_or_junction(link));
    OA_CHECK(read_text(outside / "marker.txt") == "keep");
    OA_CHECK(!install::oaexp::uninstall_expansion(linked_player, "cavedog-units", problem));
    OA_CHECK(problem.refusal == Refusal::link);
    OA_CHECK(install::is_link_or_junction(link));
    OA_CHECK(read_text(outside / "marker.txt") == "keep");
}

} // namespace

int main() {
    test_install();
    return oa::test::check_exit_status();
}
