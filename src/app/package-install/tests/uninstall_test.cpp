// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Uninstalls in a scratch Mods folder: a mod and the copy ROLL BACK keeps
// moved aside whole in one rename, then deleted a step at a time, a later
// install finding the folder free; links never followed, whether the
// package's folder, its .backup or a file inside it is the link, each
// leaving what it names whole; names that are not one plain folder of the
// root refused; a folder of the player's own, a missing folder and one that
// changed since it was listed refused; an unfinished change's folder in the
// way, and the lock another copy holds, refused; a refused rename leaving
// the package whole where it was; and a deletion that stops halfway leaving
// only a discard folder, which nothing reads as a package and the next
// start's recovery deletes.

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;
using install::Refusal;

/// The folder of the tests' mod.
constexpr std::string_view target = "example-mod";

/// A scratch folder, deleted when the test ends.
class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-package-uninstall-")) {}

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

/// Writes a text file, making its folders.
///
/// @param file the file
/// @param text its text
void write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

/// Reads a text file.
///
/// @param file the file
/// @return its text; empty when it cannot be read
std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Makes a mod folder by hand: its profile at a revision and one file.
///
/// @param folder the folder
/// @param revision the revision
void make_mod(const fs::path& folder, int revision) {
    write(
        folder / "oamod.yaml",
        "oamod: 1\n"
        "id: example-mod\n"
        "name: Example Mod\n"
        "version: \"1.0\"\n"
        "description: A made-up mod the tests uninstall.\n"
        "requires: {base: ta-3.1c, catalogue: 1}\n"
        "author: {name: unknown}\n"
        "packaging: {revision: " +
            std::to_string(revision) + ", date: 2026-10-12, packager: Open Annihilation}\n"
    );
    write(folder / "units" / "unit.fbi", "revision " + std::to_string(revision));
}

/// Makes a mod folder with the copy ROLL BACK keeps: revision 2, its
/// .backup revision 1.
///
/// @param folder the folder
void make_mod_with_backup(const fs::path& folder) {
    make_mod(folder, 2);
    make_mod(folder / ".backup", 1);
}

/// Returns the revision a folder holds; -1 when it holds no mod.
///
/// @param folder the folder
/// @return the revision
int64_t revision_in(const fs::path& folder) {
    const auto held = install::oamod::read_installed_mod(folder);
    return held.kind == install::FolderKind::package ? held.revision : -1;
}

/// Returns the names in a Mods folder but its lock, sorted.
///
/// @param mods the Mods folder
/// @return the names
std::vector<std::string> names_in(const fs::path& mods) {
    std::vector<std::string> names;
    std::error_code error;
    for (fs::directory_iterator entry{mods, error}, end; !error && entry != end;
         entry.increment(error)) {
        const std::string name = entry->path().filename().string();
        if (name != ".oamod-lock")
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// Deletes discard folders to the end.
///
/// @param folders the folders
void discard_all(const std::vector<fs::path>& folders) {
    install::Discarder discarder;
    discarder.add(folders);
    for (int step = 0; step < 100'000 && discarder.step(); ++step) {
    }
    OA_CHECK(!discarder.busy());
}

/// Makes a link to a folder or a file, or says why the test skips it. Some
/// systems report a link made and make none; that skips it too.
///
/// @param to what it links to
/// @param made_link the link
/// @return true when it was made
bool make_link(const fs::path& to, const fs::path& made_link) {
    std::error_code error;
    if (fs::is_directory(to))
        fs::create_directory_symlink(to, made_link, error);
    else
        fs::create_symlink(to, made_link, error);
    if (!error && !install::is_link_or_junction(made_link))
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    if (error)
        std::fprintf(stderr, "skipped: this system made no link: %s\n", error.message().c_str());
    return !error;
}

/// Uninstalls the tests' mod from a Mods folder.
///
/// @param mods the Mods folder
/// @param name the folder to uninstall
/// @param options the hooks, the waits and what the folder must hold
/// @return what it did
install::UninstallResult
uninstall(const fs::path& mods, std::string_view name, const install::ChangeOptions& options = {}) {
    return install::uninstall_package(install::mod_kind(), mods, name, options);
}

/// Tells whether a later install of the tests' mod finds its folder free
/// and asks nothing.
///
/// @param mods the Mods folder
/// @return true when it does
bool installs_afresh(const fs::path& mods) {
    const install::Incoming incoming{std::string(target), "Example Mod", "1.0", 3};
    const install::InstallPlan plan =
        install::oamod::plan_install(incoming, install::folder_hooks(install::mod_kind(), mods));
    return plan.kind == install::PlanKind::install && plan.target == target;
}

void test_uninstalls(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "uninstalls" / "Mods";
    const fs::path folder = mods / std::string(target);
    make_mod_with_backup(folder);
    make_mod(mods / "other-mod", 5);
    install::ChangeOptions options{};
    options.expected = install::oamod::read_installed_mod(folder);
    const auto result = uninstall(mods, target, options);
    OA_CHECK(result.removed && result.refusal == Refusal::none);
    OA_CHECK(result.discards.size() == 1);
    if (result.discards.size() != 1)
        return;
    // The folder and its .backup moved aside whole, under a name that
    // starts with the kind's prefix: nothing reads it as a package.
    const fs::path aside = result.discards.front();
    OA_CHECK(aside.parent_path() == mods);
    OA_CHECK(aside.filename().string().starts_with(".oamod-discard-example-mod-"));
    OA_CHECK(!fs::exists(fs::symlink_status(folder)));
    OA_CHECK(revision_in(aside) == 2 && revision_in(aside / ".backup") == 1);
    OA_CHECK(installs_afresh(mods));
    // Deleted a step at a time; the other mod is left as it was.
    discard_all(result.discards);
    OA_CHECK((names_in(mods) == std::vector<std::string>{"other-mod"}));
    OA_CHECK(revision_in(mods / "other-mod") == 5);
    // A second uninstall finds nothing to remove.
    const auto again = uninstall(mods, target);
    OA_CHECK(!again.removed && again.refusal == Refusal::changed && again.discards.empty());
    // A discard folder an earlier uninstall left takes the next number.
    make_mod_with_backup(folder);
    make_mod(mods / ".oamod-discard-example-mod-1", 1);
    const auto numbered = uninstall(mods, target);
    OA_CHECK(numbered.removed && numbered.discards.size() == 1);
    if (numbered.discards.size() == 1)
        OA_CHECK(numbered.discards.front().filename() == ".oamod-discard-example-mod-2");
    discard_all(numbered.discards);
    discard_all({mods / ".oamod-discard-example-mod-1"});
    OA_CHECK((names_in(mods) == std::vector<std::string>{"other-mod"}));
}

void test_links(const Scratch& scratch) {
    const fs::path root = scratch.path() / "links";
    const fs::path mods = root / "Mods";
    const fs::path outside = root / "outside";
    make_mod(outside / "linked-mod", 7);
    write(outside / "kept.txt", "kept");
    write(outside / "sub" / "deeper.txt", "kept");
    const auto outside_whole = [&] {
        return revision_in(outside / "linked-mod") == 7 && read(outside / "kept.txt") == "kept" &&
               read(outside / "sub" / "deeper.txt") == "kept";
    };
    fs::create_directories(mods);
    // The package's folder is a link: refused, the link and what it names left.
    const fs::path linked = mods / std::string(target);
    if (!make_link(outside / "linked-mod", linked))
        return;
    const auto refused = uninstall(mods, target);
    OA_CHECK(!refused.removed && refused.refusal == Refusal::link && refused.discards.empty());
    OA_CHECK(install::is_link_or_junction(linked) && outside_whole());
    fs::remove(linked);
    // Its .backup is a link, and a file inside it is: the links go, never
    // followed, and what they name stays whole.
    make_mod(linked, 2);
    if (!make_link(outside, linked / ".backup") ||
        !make_link(outside / "kept.txt", linked / "units" / "kept.txt"))
        return;
    const auto removed = uninstall(mods, target);
    OA_CHECK(removed.removed && removed.discards.size() == 1);
    discard_all(removed.discards);
    OA_CHECK(names_in(mods).empty());
    OA_CHECK(outside_whole());
}

void test_names(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "names" / "Mods";
    const fs::path folder = mods / std::string(target);
    make_mod(folder, 1);
    make_mod(scratch.path() / "names" / "beside", 1);
    for (const std::string_view name :
         {std::string_view{},
          std::string_view{"."},
          std::string_view{".."},
          std::string_view{"../beside"},
          std::string_view{"example-mod/units"},
          std::string_view{"example-mod\\units"},
          std::string_view{"C:"},
          std::string_view{".oamod-staging-example-mod"},
          std::string_view{".backup"},
          std::string_view{"tab\tname"}}) {
        OA_CHECK(!install::plain_folder_name(name));
        const auto result = uninstall(mods, name);
        OA_CHECK(!result.removed && result.refusal == Refusal::unsafe_name);
    }
    OA_CHECK(install::plain_folder_name(target));
    OA_CHECK(install::plain_folder_name("ridge-1.0"));
    OA_CHECK(install::plain_folder_name("caf\xc3\xa9"));
    OA_CHECK(!install::plain_folder_name(std::string(256, 'a')));
    OA_CHECK(revision_in(folder) == 1 && revision_in(scratch.path() / "names" / "beside") == 1);
    OA_CHECK((names_in(mods) == std::vector<std::string>{std::string(target)}));
}

void test_refused(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "refused" / "Mods";
    const fs::path folder = mods / std::string(target);
    // A folder of the player's own, a file and a missing folder.
    write(mods / "own-folder" / "notes.txt", "not a mod");
    write(mods / "a-file", "not a folder");
    for (const std::string_view name : {"own-folder", "a-file", "missing"}) {
        const auto result = uninstall(mods, name);
        OA_CHECK(!result.removed && result.refusal == Refusal::changed);
    }
    OA_CHECK(read(mods / "own-folder" / "notes.txt") == "not a mod");
    OA_CHECK(read(mods / "a-file") == "not a folder");
    // A folder that changed since it was listed.
    make_mod_with_backup(folder);
    install::ChangeOptions listed{};
    listed.expected = install::oamod::read_installed_mod(folder);
    make_mod(folder, 3);
    const auto changed = uninstall(mods, target, listed);
    OA_CHECK(!changed.removed && changed.refusal == Refusal::changed);
    OA_CHECK(revision_in(folder) == 3 && revision_in(folder / ".backup") == 1);
    // An unfinished change's folder in the way, which recovery would put back.
    for (const std::string_view prefix :
         {".oamod-staging-", ".oamod-old-", ".oamod-replaced-", ".oamod-restore-"}) {
        const fs::path role = mods / (std::string(prefix) + std::string(target));
        make_mod(role, 4);
        const auto busy = uninstall(mods, target);
        OA_CHECK(!busy.removed && busy.refusal == Refusal::busy);
        OA_CHECK(revision_in(folder) == 3 && revision_in(role) == 4);
        fs::remove_all(role);
    }
    // Another copy of the game holds the folder.
    {
        const auto other = install::FileLock::take(mods / ".oamod-lock");
        OA_CHECK(other != nullptr);
        const auto busy = uninstall(mods, target);
        OA_CHECK(!busy.removed && busy.refusal == Refusal::busy);
        OA_CHECK(revision_in(folder) == 3);
    }
    // A refused rename leaves the package whole where it was.
    install::ChangeOptions options{};
    options.first_retry_ms = 0;
    options.hooks.rename = [](void*, const fs::path&, const fs::path&, std::error_code& error) {
        error = std::make_error_code(std::errc::io_error);
    };
    const auto not_moved = uninstall(mods, target, options);
    OA_CHECK(!not_moved.removed && not_moved.refusal == Refusal::not_placed);
    OA_CHECK(not_moved.discards.empty() && !not_moved.detail.empty());
    OA_CHECK(revision_in(folder) == 3 && revision_in(folder / ".backup") == 1);
    OA_CHECK(
        (names_in(mods) == std::vector<std::string>{"a-file", std::string(target), "own-folder"})
    );
}

void test_stopped_deletion(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "stopped" / "Mods";
    const fs::path folder = mods / std::string(target);
    make_mod_with_backup(folder);
    // More files than one step of the deletion removes.
    for (int file = 0; file < 200; ++file)
        write(folder / "maps" / ("map-" + std::to_string(file) + ".txt"), "map");
    const auto result = uninstall(mods, target);
    OA_CHECK(result.removed && result.discards.size() == 1);
    if (result.discards.size() != 1)
        return;
    const fs::path aside = result.discards.front();
    // A stop before any deletion: the folder is gone whole, and what is left
    // is the discard folder alone, which no plan reads as the package.
    OA_CHECK(!fs::exists(folder) && fs::exists(aside) && installs_afresh(mods));
    // A stop after one step of the deletion, part of the folder deleted.
    {
        install::Discarder partly;
        partly.add(result.discards);
        OA_CHECK(partly.step());
    }
    OA_CHECK(!fs::exists(folder) && fs::exists(aside) && installs_afresh(mods));
    OA_CHECK((names_in(mods) == std::vector<std::string>{aside.filename().string()}));
    // The next start's recovery finds the discard folder and deletes it.
    const auto recovery = install::recover_changes(install::mod_kind(), mods);
    OA_CHECK(!recovery.skipped && recovery.discards.size() == 1);
    discard_all(recovery.discards);
    OA_CHECK(names_in(mods).empty() && installs_afresh(mods));
}

} // namespace

int main() {
    const Scratch scratch;
    test_uninstalls(scratch);
    test_links(scratch);
    test_names(scratch);
    test_refused(scratch);
    test_stopped_deletion(scratch);
    return oa::test::check_exit_status();
}
