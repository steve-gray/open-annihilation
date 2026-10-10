// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Origin records: the text and the texts it refuses; an install, a replace,
// a roll back, a reinstall and recovery in a scratch Mods folder, each
// record moving with its folder; a package's own origin file left out; a
// catalogue hash that differs refused; the hash phase held to its budget;
// write_origin replacing a record and refusing a missing folder, a link and
// a lock that is held; the folder hooks' records; catalogue outcomes kept,
// capped and taken once, including one a pending change reports.

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/inbox.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/app/package_install/origin.hpp"
#include "oa/base/sha256.hpp"
#include "oa/test/check.hpp"
#include "oa/test/raw_zip.hpp"
#include "oa/test/scratch_directory.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;
namespace raw = oa::test::raw_zip;
using install::Change;
using install::Refusal;

/// The target folder of the tests.
constexpr std::string_view target = "example-mod";

/// The comment a record starts with.
constexpr std::string_view origin_comment =
    "# Written by Open Annihilation when it installed this folder; not part of the package.";

/// A scratch folder, deleted when the test ends.
class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-package-origin-")) {}

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

/// Puts a folder's write permission back when the test has taken it off.
class RestoreWrite {
  public:

    /// Takes write permission off `folder`.
    ///
    /// @param folder the folder
    explicit RestoreWrite(const fs::path& folder) : folder_(folder) {
        std::error_code ignored;
        fs::permissions(
            folder_,
            fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
            fs::perm_options::remove,
            ignored
        );
    }

    ~RestoreWrite() {
        std::error_code ignored;
        fs::permissions(folder_, fs::perms::owner_all, fs::perm_options::add, ignored);
    }

    RestoreWrite(const RestoreWrite&) = delete;
    RestoreWrite& operator=(const RestoreWrite&) = delete;

  private:

    fs::path folder_;
};

/// A file origin with the date the checks record.
///
/// @return the origin
install::Origin file_origin() {
    install::Origin origin{};
    origin.installed = "2000-01-01";
    return origin;
}

/// A catalogue origin whose hash is the empty message, so the text is known.
///
/// @return the origin
install::Origin catalogue_origin() {
    install::Origin origin{};
    origin.kind = install::OriginKind::catalogue;
    origin.registry = "coreprime";
    origin.catalogue_id = "example-mod";
    origin.release = 27;
    origin.sha256 = oa::base::sha256::digest_of({});
    origin.installed = "2026-11-02";
    return origin;
}

/// Writes a digest as lower-case hex.
///
/// @param digest the digest
/// @return 64 hex digits
std::string hex_of(const oa::base::sha256::Digest& digest) {
    const auto hex = oa::base::sha256::to_hex(digest);
    return std::string(hex.begin(), hex.end());
}

/// Returns a text as the bytes parse_origin reads.
///
/// @param text the text
/// @return its bytes
std::span<const uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

/// Reads a file's bytes.
///
/// @param file the file
/// @return its bytes; empty when it cannot be read
std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Writes a text file, making its folders.
///
/// @param file the file
/// @param text its text
void write_text(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

/// Returns the SHA-256 of a file.
///
/// @param file the file
/// @return its digest
oa::base::sha256::Digest file_hash(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::vector<uint8_t> bytes{
        std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}
    };
    return oa::base::sha256::digest_of(bytes);
}

/// Returns the origin record in a folder.
///
/// @param folder the folder
/// @return its path
fs::path record_of(const fs::path& folder) {
    return folder / std::string(install::origin_file_name);
}

/// A profile of the test mod at a revision.
///
/// @param revision its packaging revision
/// @return its text
std::string profile_text(int revision) {
    return "oamod: 1\n"
           "id: example-mod\n"
           "name: Example Mod\n"
           "version: \"1.0\"\n"
           "description: A made-up mod the tests install.\n"
           "requires: {base: ta-3.1c, catalogue: 1}\n"
           "author: {name: unknown}\n"
           "packaging: {revision: " +
           std::to_string(revision) + ", date: 2026-10-04, packager: Open Annihilation}\n";
}

/// Makes a mod folder by hand, with no origin record.
///
/// @param folder the folder
/// @param revision the revision
void make_mod(const fs::path& folder, int revision) {
    write_text(folder / "oamod.yaml", profile_text(revision));
    write_text(folder / "units" / "unit.fbi", "revision " + std::to_string(revision));
}

/// Writes a package of the test mod at a revision.
///
/// @param folder where it goes
/// @param revision its revision
/// @param extra a further stored file, when the archive must be large
/// @return the package
fs::path write_package(const fs::path& folder, int revision, std::string_view extra = {}) {
    const fs::path file = folder / ("example-mod-1.0-" + std::to_string(revision) + ".oamod");
    std::vector<raw::RawFile> files{
        raw::stored_text("oamod.yaml", profile_text(revision)),
        raw::stored_text("units/", ""),
        raw::stored_text("units/unit.fbi", "revision " + std::to_string(revision)),
    };
    if (!extra.empty())
        files.push_back(raw::stored_text("gamedata/big.tdf", extra));
    const auto archive = raw::build_raw_archive(files);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(archive.bytes.data()),
        static_cast<std::streamsize>(archive.bytes.size())
    );
    return file;
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

/// What an install did.
struct Installed {
    install::InstallPlan plan{};
    install::ChangeResult result{};
    install::Problem problem{};
};

/// Installs a package the way the game does, with the origin given.
///
/// @param file the package
/// @param mods the Mods folder
/// @param answer the change
/// @param origin where it came from
/// @return what it did
Installed install_package(
    const fs::path& file, const fs::path& mods, Change answer, const install::Origin& origin
) {
    Installed done{};
    const auto opened = install::open_package(file);
    OA_CHECK(opened.package.has_value());
    if (!opened.package) {
        done.problem = opened.problem;
        return done;
    }
    const auto incoming = install::oamod::incoming_of(*opened.package->profile);
    done.plan =
        install::oamod::plan_install(incoming, install::folder_hooks(install::mod_kind(), mods));
    const std::string folder = done.plan.target.empty() ? std::string(target) : done.plan.target;
    const install::InstalledPackage expected = done.plan.installed;
    install::Unpacking unpacking;
    if (!unpacking.start(*opened.package, mods, folder, expected, origin, done.problem)) {
        discard_all(unpacking.discards());
        return done;
    }
    auto step = oa::formats::zip::StreamStep::more;
    while (step == oa::formats::zip::StreamStep::more)
        step = unpacking.step(uint64_t{1} << 20, done.problem);
    if (step != oa::formats::zip::StreamStep::done) {
        discard_all(unpacking.discards());
        return done;
    }
    install::ChangeOptions options{};
    options.expected = expected;
    options.first_retry_ms = 0;
    done.result = install::commit_change(install::mod_kind(), mods, folder, answer, options);
    discard_all(done.result.discards);
    return done;
}

/// Drops whatever the process inbox still holds.
void drain_inbox() {
    while (install::take_package_file())
        install::finish_package_file();
    (void)install::take_package_outcomes();
    (void)install::take_change_outcome();
    (void)install::take_discards();
}

/// The record text of an origin, built the way the brief spells it.
///
/// @param origin the origin; its hash is written unquoted
/// @param hash the hash's hex
/// @return the record
std::string expected_text(const install::Origin& origin, std::string_view hash) {
    std::string text;
    text.append(origin_comment);
    text.push_back('\n');
    text.append("oa-origin: 1\n");
    if (origin.kind == install::OriginKind::catalogue) {
        text.append("origin: catalogue\n");
        text.append("registry: ");
        text.append(origin.registry);
        text.append("\ncatalogue-id: ");
        text.append(origin.catalogue_id);
        text.append("\nrelease: ");
        text.append(std::to_string(origin.release));
        text.push_back('\n');
    } else {
        text.append("origin: file\n");
    }
    text.append("sha256: ");
    text.append(hash);
    text.append("\ninstalled: ");
    text.append(origin.installed);
    text.push_back('\n');
    return text;
}

void test_text() {
    const install::Origin catalogue = catalogue_origin();
    const std::string hash = hex_of(*catalogue.sha256);
    const std::string catalogue_text = expected_text(catalogue, hash);
    OA_CHECK(install::origin_text(catalogue) == catalogue_text);
    const auto parsed = install::parse_origin(bytes_of(catalogue_text));
    OA_CHECK(parsed.has_value());
    if (parsed) {
        OA_CHECK(parsed->kind == install::OriginKind::catalogue);
        OA_CHECK(parsed->registry == "coreprime");
        OA_CHECK(parsed->catalogue_id == "example-mod");
        OA_CHECK(parsed->release == 27);
        OA_CHECK(parsed->sha256 == catalogue.sha256);
        OA_CHECK(parsed->installed == "2026-11-02");
        OA_CHECK(install::origin_text(*parsed) == catalogue_text);
    }

    install::Origin file = file_origin();
    file.sha256 = catalogue.sha256;
    const std::string file_text = expected_text(file, hash);
    OA_CHECK(install::origin_text(file) == file_text);
    OA_CHECK(file_text.find("registry:") == std::string::npos);
    const auto parsed_file = install::parse_origin(bytes_of(file_text));
    OA_CHECK(parsed_file.has_value());
    if (parsed_file) {
        OA_CHECK(parsed_file->kind == install::OriginKind::file);
        OA_CHECK(parsed_file->registry.empty() && parsed_file->release == 0);
        OA_CHECK(install::origin_text(*parsed_file) == file_text);
    }

    // A hash of digits alone is quoted, and still reads back as that hash.
    install::Origin digits = file_origin();
    digits.sha256 = oa::base::sha256::parse_hex(std::string(64, '0'));
    const std::string quoted = install::origin_text(digits);
    OA_CHECK(
        quoted.find(
            "sha256: \"0000000000000000000000000000000000000000000000000000000000000000\""
        ) != std::string::npos
    );
    const auto parsed_digits = install::parse_origin(bytes_of(quoted));
    OA_CHECK(parsed_digits && parsed_digits->sha256 == digits.sha256);
    OA_CHECK(parsed_digits && install::origin_text(*parsed_digits) == quoted);

    install::Origin bare{};
    bare.installed = "2000-01-01";
    OA_CHECK(install::origin_text(bare).empty());

    const auto refuse = [](std::string_view text) {
        OA_CHECK(!install::parse_origin(bytes_of(text)));
    };
    refuse(catalogue_text + "note: no\n");
    refuse(
        std::string(origin_comment) + "\n"
                                      "oa-origin: 1\n"
                                      "origin: file\n"
                                      "installed: 2000-01-01\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: file\n"
        "release: 1\n"
        "sha256: " +
        hash +
        "\n"
        "installed: 2000-01-01\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: catalogue\n"
        "catalogue-id: example-mod\n"
        "release: 27\n"
        "sha256: " +
        hash +
        "\n"
        "installed: 2026-11-02\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: catalogue\n"
        "registry: coreprime\n"
        "catalogue-id: example-mod\n"
        "release: 0\n"
        "sha256: " +
        hash +
        "\n"
        "installed: 2026-11-02\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: catalogue\n"
        "registry: coreprime\n"
        "catalogue-id: \"example@mod\"\n"
        "release: 27\n"
        "sha256: " +
        hash +
        "\n"
        "installed: 2026-11-02\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: file\n"
        "sha256: " +
        hash.substr(0, 63) +
        "\n"
        "installed: 2000-01-01\n"
    );
    std::string upper = hash;
    for (char& letter : upper)
        if (letter >= 'a' && letter <= 'f')
            letter = static_cast<char>(letter - 'a' + 'A');
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: file\n"
        "sha256: " +
        upper +
        "\n"
        "installed: 2000-01-01\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: file\n"
        "sha256: " +
        hash +
        "\n"
        "installed: 2026-02-31\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 2\n"
        "origin: file\n"
        "sha256: " +
        hash +
        "\n"
        "installed: 2000-01-01\n"
    );
    refuse(std::string(install::origin_most_bytes + 1, 'a'));
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: file\n"
        "installed: 2000-01-01\n"
        "sha256: " +
        hash + "\n"
    );
    refuse(
        std::string(origin_comment) +
        "\n"
        "oa-origin: 1\n"
        "origin: file\n"
        "sha256: " +
        std::string(64, '1') +
        "\n"
        "installed: 2000-01-01\n"
    );
    std::string cr = file_text;
    for (char& letter : cr)
        if (letter == '\n')
            letter = '\r';
    refuse(cr);
    refuse(std::string(origin_comment) + "\norigin: file\n\xFF");

    const auto unix_epoch = std::chrono::system_clock::time_point{};
    const auto y2k = unix_epoch + std::chrono::seconds{946684800};
    OA_CHECK(install::utc_date_text(y2k) == "2000-01-01");
    OA_CHECK(install::utc_date_text(y2k - std::chrono::seconds{1}) == "1999-12-31");
}

void test_moves(const Scratch& scratch) {
    const fs::path root = scratch.path() / "moves";
    const fs::path mods = root / "Mods";
    fs::create_directories(root);
    const fs::path first = write_package(root, 1);
    const fs::path second = write_package(root, 2);
    const fs::path folder = mods / std::string(target);
    const fs::path kept = folder / std::string(install::backup_folder_name);

    const Installed installed = install_package(first, mods, Change::install, file_origin());
    OA_CHECK(installed.plan.kind == install::PlanKind::install && installed.result.changed);
    const auto origin = install::read_origin(folder);
    OA_CHECK(origin.has_value());
    if (origin) {
        OA_CHECK(origin->kind == install::OriginKind::file);
        OA_CHECK(origin->registry.empty() && origin->catalogue_id.empty() && origin->release == 0);
        OA_CHECK(origin->sha256 == file_hash(first));
        OA_CHECK(origin->installed == "2000-01-01");
    }
    const std::string first_record = read_text(record_of(folder));
    if (origin)
        OA_CHECK(first_record == install::origin_text(*origin));

    const Installed replaced = install_package(second, mods, Change::replace, file_origin());
    OA_CHECK(replaced.plan.kind == install::PlanKind::ask_update && replaced.result.changed);
    OA_CHECK(read_text(record_of(kept)) == first_record);
    const auto second_origin = install::read_origin(folder);
    OA_CHECK(second_origin && second_origin->sha256 == file_hash(second));
    const std::string second_record = read_text(record_of(folder));

    const Installed again = install_package(second, mods, Change::reinstall, file_origin());
    OA_CHECK(again.plan.kind == install::PlanKind::ask_reinstall && again.result.changed);
    OA_CHECK(read_text(record_of(kept)) == first_record);
    const auto reinstalled = install::read_origin(folder);
    OA_CHECK(reinstalled && reinstalled->sha256 == file_hash(second));
    const std::string target_record = read_text(record_of(folder));
    const std::string kept_record = read_text(record_of(kept));

    install::ChangeOptions options{};
    options.first_retry_ms = 0;
    const auto rolled =
        install::commit_change(install::mod_kind(), mods, target, Change::roll_back, options);
    OA_CHECK(rolled.changed);
    OA_CHECK(read_text(record_of(folder)) == kept_record);
    OA_CHECK(read_text(record_of(kept)) == target_record);
    OA_CHECK(second_record.find("sha256:") != std::string::npos);
}

void test_recovery(const Scratch& scratch) {
    const fs::path root = scratch.path() / "recover";
    const fs::path mods = root / "Mods";
    fs::create_directories(root);
    const fs::path first = write_package(root, 1);
    const fs::path second = write_package(root, 2);
    const fs::path folder = mods / std::string(target);
    OA_CHECK(install_package(first, mods, Change::install, file_origin()).result.changed);
    const std::string old_record = read_text(record_of(folder));

    const auto opened = install::open_package(second);
    OA_CHECK(opened.package.has_value());
    if (!opened.package)
        return;
    const auto incoming = install::oamod::incoming_of(*opened.package->profile);
    const auto plan =
        install::oamod::plan_install(incoming, install::folder_hooks(install::mod_kind(), mods));
    fs::path staging;
    std::string new_record;
    {
        // The unpacking's end releases the lock and leaves the staging folder.
        install::Unpacking unpacking;
        install::Problem problem{};
        OA_CHECK(
            unpacking.start(*opened.package, mods, target, plan.installed, file_origin(), problem)
        );
        auto step = oa::formats::zip::StreamStep::more;
        while (step == oa::formats::zip::StreamStep::more)
            step = unpacking.step(uint64_t{1} << 20, problem);
        OA_CHECK(step == oa::formats::zip::StreamStep::done);
        staging = unpacking.staging();
        new_record = read_text(record_of(staging));
        OA_CHECK(!new_record.empty() && new_record != old_record);
    }
    OA_CHECK(read_text(record_of(staging)) == new_record);
    std::error_code error;
    fs::rename(folder, mods / (".oamod-old-" + std::string(target)), error);
    OA_CHECK(!error);
    const auto recovery = install::recover_changes(install::mod_kind(), mods);
    OA_CHECK(!recovery.skipped);
    OA_CHECK(read_text(record_of(folder)) == old_record);
    bool found = false;
    for (const fs::path& discard : recovery.discards)
        if (read_text(record_of(discard)) == new_record)
            found = true;
    OA_CHECK(found);
    discard_all(recovery.discards);
}

void test_left_out(const Scratch& scratch) {
    const fs::path root = scratch.path() / "left-out";
    const fs::path mods = root / "Mods";
    fs::create_directories(root);
    const fs::path file = root / "packed-origin.oamod";
    const auto archive = raw::build_raw_archive({
        raw::stored_text("oamod.yaml", profile_text(1)),
        raw::stored_text(".OA-ORIGIN.YAML", "PACKED NOT INSTALLED\n"),
        raw::stored_text("units/", ""),
        raw::stored_text("units/.oa-origin.yaml", "nested copy\n"),
        raw::stored_text("units/unit.fbi", "revision 1"),
    });
    std::ofstream(file, std::ios::binary)
        .write(
            reinterpret_cast<const char*>(archive.bytes.data()),
            static_cast<std::streamsize>(archive.bytes.size())
        );
    const auto opened = install::open_package(file);
    OA_CHECK(opened.package && opened.package->origin_left_out);
    const Installed done = install_package(file, mods, Change::install, file_origin());
    OA_CHECK(done.result.changed);
    const fs::path folder = mods / std::string(target);
    const std::string record = read_text(record_of(folder));
    OA_CHECK(record.find("PACKED NOT INSTALLED") == std::string::npos);
    const auto origin = install::read_origin(folder);
    OA_CHECK(origin && origin->sha256 == file_hash(file));
    OA_CHECK(read_text(folder / "units" / ".oa-origin.yaml") == "nested copy\n");
}

void test_damaged(const Scratch& scratch) {
    const fs::path root = scratch.path() / "damaged";
    const fs::path mods = root / "Mods";
    fs::create_directories(root);
    const fs::path file = write_package(root, 1);
    install::Origin origin = catalogue_origin();
    origin.sha256 = oa::base::sha256::parse_hex(std::string(64, 'a'));
    const Installed done = install_package(file, mods, Change::install, origin);
    OA_CHECK(!done.result.changed);
    OA_CHECK(done.problem.refusal == Refusal::damaged);
    OA_CHECK(done.problem.detail == "its SHA-256 is not the catalogue's");
    OA_CHECK(!fs::exists(mods / std::string(target)));
}

void test_budget(const Scratch& scratch) {
    const fs::path root = scratch.path() / "budget";
    const fs::path mods = root / "Mods";
    fs::create_directories(root);
    const fs::path file = write_package(root, 1, std::string(4096, 'x'));
    const auto opened = install::open_package(file);
    OA_CHECK(opened.package.has_value());
    if (!opened.package)
        return;
    install::Unpacking unpacking;
    install::Problem problem{};
    OA_CHECK(unpacking.start(*opened.package, mods, target, {}, file_origin(), problem));
    OA_CHECK(unpacking.checking());
    OA_CHECK(unpacking.archive_bytes() > 1024);
    const uint64_t budget = 1024;
    bool hashing = false;
    auto step = oa::formats::zip::StreamStep::more;
    while (step == oa::formats::zip::StreamStep::more) {
        const bool checking = unpacking.checking();
        const uint64_t hashed = unpacking.checked_bytes();
        step = unpacking.step(budget, problem);
        if (checking) {
            hashing = true;
            OA_CHECK(unpacking.checked_bytes() - hashed <= budget);
        }
    }
    OA_CHECK(step == oa::formats::zip::StreamStep::done);
    OA_CHECK(hashing);
    OA_CHECK(unpacking.checked_bytes() == unpacking.archive_bytes());
    unpacking.cancel();
    discard_all(unpacking.discards());
}

void test_write_origin(const Scratch& scratch) {
    const fs::path root = scratch.path() / "write";
    const fs::path mods = root / "Mods";
    fs::create_directories(root);
    const fs::path file = write_package(root, 1);
    const fs::path folder = mods / std::string(target);
    OA_CHECK(install_package(file, mods, Change::install, file_origin()).result.changed);
    const std::string installed_record = read_text(record_of(folder));

    install::Origin replaced = catalogue_origin();
    replaced.sha256 = file_hash(file);
    std::string error;
    OA_CHECK(install::write_origin(install::mod_kind(), mods, target, replaced, &error));
    OA_CHECK(read_text(record_of(folder)) == install::origin_text(replaced));
    const auto read_back = install::read_origin(folder);
    OA_CHECK(read_back && read_back->kind == install::OriginKind::catalogue);
    OA_CHECK(read_back && read_back->release == 27);
    const std::string replaced_record = read_text(record_of(folder));
    OA_CHECK(replaced_record != installed_record);

#ifndef _WIN32
    if (::geteuid() != 0) {
        install::Origin other = replaced;
        other.installed = "2026-11-03";
        {
            const RestoreWrite restore(folder);
            OA_CHECK(!install::write_origin(install::mod_kind(), mods, target, other, &error));
            OA_CHECK(read_text(record_of(folder)) == replaced_record);
        }
    }
#endif

    const fs::path absent_root = scratch.path() / "absent" / "Mods";
    fs::create_directories(absent_root);
    OA_CHECK(!install::write_origin(install::mod_kind(), absent_root, "absent", replaced, &error));
    OA_CHECK(!fs::exists(absent_root / "absent"));

    const fs::path link = mods / "linked-mod";
    std::error_code link_error;
    fs::create_directory_symlink(folder, link, link_error);
    OA_CHECK(!link_error);
    install::Origin through = replaced;
    through.release = 28;
    OA_CHECK(!install::write_origin(install::mod_kind(), mods, "linked-mod", through, &error));
    OA_CHECK(read_text(record_of(folder)) == replaced_record);

    const auto side = install::open_package(write_package(root, 2));
    OA_CHECK(side.package.has_value());
    if (!side.package)
        return;
    install::Unpacking unpacking;
    install::Problem problem{};
    OA_CHECK(unpacking.start(*side.package, mods, "side-mod", {}, file_origin(), problem));
    install::Origin while_held = replaced;
    while_held.release = 29;
    OA_CHECK(!install::write_origin(install::mod_kind(), mods, target, while_held, &error));
    OA_CHECK(error == "the folder's lock is taken");
    OA_CHECK(read_text(record_of(folder)) == replaced_record);
    unpacking.cancel();
    discard_all(unpacking.discards());
}

void test_hooks(const Scratch& scratch) {
    const fs::path root = scratch.path() / "hooks";
    const fs::path mods = root / "Mods";
    fs::create_directories(root);
    const fs::path first = write_package(root, 1);
    const fs::path second = write_package(root, 2);
    OA_CHECK(install_package(first, mods, Change::install, file_origin()).result.changed);
    OA_CHECK(install_package(second, mods, Change::replace, file_origin()).result.changed);

    const auto hooks = install::folder_hooks(install::mod_kind(), mods);
    const install::InstalledPackage looked = hooks.look(hooks.context, target);
    OA_CHECK(looked.kind == install::FolderKind::package);
    OA_CHECK(looked.origin && looked.origin->sha256 == file_hash(second));
    const auto backup = hooks.backup_of(hooks.context, target);
    OA_CHECK(backup && backup->kind == install::FolderKind::package);
    OA_CHECK(backup && backup->origin && backup->origin->sha256 == file_hash(first));

    make_mod(mods / "handmade", 4);
    const install::InstalledPackage hand = hooks.look(hooks.context, "handmade");
    OA_CHECK(hand.kind == install::FolderKind::package);
    OA_CHECK(!hand.origin);
    OA_CHECK(!install::read_origin(mods / "handmade"));

    install::InstalledPackage without = looked;
    without.origin.reset();
    OA_CHECK(install::same_package(looked, without));
    without.revision = looked.revision + 1;
    OA_CHECK(!install::same_package(looked, without));
}

/// Posts a catalogue origin for a path.
///
/// @param file the package
/// @param release its release
/// @return the origin
install::Origin post_catalogue(const fs::path& file, int64_t release) {
    install::Origin origin = catalogue_origin();
    origin.release = release;
    install::post_package_file(file, origin);
    return origin;
}

void test_outcomes(const Scratch& scratch) {
    drain_inbox();
    const fs::path folder = scratch.path() / "outcomes";
    fs::create_directories(folder);

    const fs::path twice = folder / "twice.oamod";
    write_text(twice, "x");
    post_catalogue(twice, 1);
    post_catalogue(twice, 2);
    const auto opened = install::take_package_file();
    OA_CHECK(opened && opened->origin.release == 1);
    if (opened)
        install::return_package_file(*opened);
    const auto returned = install::take_package_file();
    OA_CHECK(returned && returned->origin.release == 1);
    OA_CHECK(returned && returned->file.filename() == "twice.oamod");
    if (returned) {
        install::PackageOutcome dropped{};
        dropped.file = returned->file;
        dropped.result = install::OutcomeResult::refused;
        install::report_package_outcome(std::move(dropped));
        install::finish_package_file();
    }
    const auto dropped_outcomes = install::take_package_outcomes();
    OA_CHECK(dropped_outcomes.size() == 1);

    const fs::path from_file = folder / "from-file.oamod";
    write_text(from_file, "x");
    install::post_package_file(from_file, file_origin());
    install::PackageOutcome file_report{};
    file_report.file = from_file;
    file_report.result = install::OutcomeResult::refused;
    file_report.reason = "a file";
    install::report_package_outcome(std::move(file_report));
    OA_CHECK(install::take_package_outcomes().empty());

    for (int index = 0; index <= 256; ++index) {
        const fs::path file = folder / ("package-" + std::to_string(index) + ".oamod");
        write_text(file, "x");
        post_catalogue(file, index + 1);
        install::PackageOutcome outcome{};
        outcome.file = file;
        outcome.result = install::OutcomeResult::installed;
        install::report_package_outcome(std::move(outcome));
    }
    const auto capped = install::take_package_outcomes();
    OA_CHECK(capped.size() == 256);
    OA_CHECK(!capped.empty() && capped.front().file.filename() == "package-1.oamod");
    OA_CHECK(!capped.empty() && capped.back().file.filename() == "package-256.oamod");
    OA_CHECK(install::take_package_outcomes().empty());

    const fs::path mods = folder / "Mods";
    fs::create_directories(mods);
    install::ChangeOptions options{};
    options.first_retry_ms = 0;

    const fs::path waited = folder / "waited.oamod";
    write_text(waited, "x");
    const install::Origin waited_origin = post_catalogue(waited, 3);
    install::PendingChange pending{};
    pending.kind = &install::mod_kind();
    pending.root = mods;
    pending.target = "never-installed";
    pending.change = Change::install;
    pending.file = waited;
    pending.origin = waited_origin;
    install::set_pending_change(std::move(pending));
    install::finish_pending_change(options);
    const auto waited_outcome = install::take_change_outcome();
    OA_CHECK(waited_outcome && !waited_outcome->result.changed);
    OA_CHECK(waited_outcome && waited_outcome->result.refusal == Refusal::not_placed);
    const auto reported = install::take_package_outcomes();
    OA_CHECK(reported.size() == 1);
    OA_CHECK(!reported.empty() && reported.front().result == install::OutcomeResult::failed);
    OA_CHECK(
        !reported.empty() &&
        reported.front().reason.find("could not be put in place") != std::string::npos
    );

    const fs::path plain = folder / "plain.oamod";
    write_text(plain, "x");
    install::post_package_file(plain, file_origin());
    install::PendingChange file_pending{};
    file_pending.kind = &install::mod_kind();
    file_pending.root = mods;
    file_pending.target = "never-installed";
    file_pending.change = Change::install;
    file_pending.file = plain;
    file_pending.origin = file_origin();
    install::set_pending_change(std::move(file_pending));
    install::finish_pending_change(options);
    (void)install::take_change_outcome();
    OA_CHECK(install::take_package_outcomes().empty());

    const fs::path rolled = folder / "rolled.oamod";
    write_text(rolled, "x");
    post_catalogue(rolled, 4);
    install::PendingChange roll{};
    roll.kind = &install::mod_kind();
    roll.root = mods;
    roll.target = "never-installed";
    roll.change = Change::roll_back;
    roll.origin = catalogue_origin();
    install::set_pending_change(std::move(roll));
    install::finish_pending_change(options);
    (void)install::take_change_outcome();
    OA_CHECK(install::take_package_outcomes().empty());

    drain_inbox();
}

} // namespace

int main() {
    test_text();
    const Scratch scratch;
    test_moves(scratch);
    test_recovery(scratch);
    test_left_out(scratch);
    test_damaged(scratch);
    test_budget(scratch);
    test_write_origin(scratch);
    test_hooks(scratch);
    test_outcomes(scratch);
    return oa::test::check_exit_status();
}
