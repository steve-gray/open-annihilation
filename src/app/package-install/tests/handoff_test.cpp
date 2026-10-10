// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The instance lock and the hand-off folder: a second take of the lock
// fails while the first is held and succeeds once it is released, and the
// requests a second start writes are taken back in order, each deleted,
// and a request whose package is gone dropped.

#include "oa/app/package_install/handoff.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;

} // namespace

int main() {
    const fs::path scratch = oa::test::make_scratch_directory("oa-mod-install-handoff-");
    const fs::path lock_file = scratch / std::string(install::instance_lock_name);
    {
        const auto held = install::take_instance_lock(lock_file);
        OA_CHECK(held != nullptr);
        OA_CHECK(install::take_instance_lock(lock_file) == nullptr);
    }
    OA_CHECK(install::take_instance_lock(lock_file) != nullptr);

    const fs::path folder = scratch / std::string(install::handoff_folder_name);
    std::vector<fs::path> packages;
    for (const char* name : {"first.oamod", "second.OAMOD", "third.oamod"}) {
        packages.push_back(scratch / name);
        std::ofstream(packages.back()) << "package";
    }
    const auto written = install::hand_files_over(folder, packages);
    OA_CHECK(written.size() == 3);
    for (const auto& request : written)
        OA_CHECK(fs::exists(request) && request.extension() == ".request");
    // A later hand-over sorts after; a package that is gone is dropped.
    std::ofstream(scratch / "gone.oamod") << "package";
    OA_CHECK(install::hand_files_over(folder, {scratch / "gone.oamod"}).size() == 1);
    fs::remove(scratch / "gone.oamod");
    const auto taken = install::take_handed_files(folder);
    OA_CHECK(taken.size() == 3);
    for (std::size_t index = 0; index < taken.size() && index < packages.size(); ++index)
        OA_CHECK(fs::equivalent(taken[index], packages[index]));
    for (const auto& request : written)
        OA_CHECK(!fs::exists(request));
    OA_CHECK(install::take_handed_files(folder).empty());

    // A .oareg and a .oalang are handed over and taken back in that order.
    std::vector<fs::path> opened;
    for (const char* name : {"list.oareg", "words.oalang"}) {
        opened.push_back(scratch / name);
        std::ofstream(opened.back()) << "file";
    }
    const auto handed = install::hand_files_over(folder, opened);
    OA_CHECK(handed.size() == 2);
    const auto taken_opened = install::take_handed_files(folder);
    OA_CHECK(taken_opened.size() == 2);
    for (std::size_t index = 0; index < taken_opened.size() && index < opened.size(); ++index)
        OA_CHECK(fs::equivalent(taken_opened[index], opened[index]));

    std::error_code ignored;
    fs::remove_all(scratch, ignored);
    return oa::test::check_exit_status();
}
