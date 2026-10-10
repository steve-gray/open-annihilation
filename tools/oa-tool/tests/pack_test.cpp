// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-tool pack, run in-process: a made-up mod folder packs to a package the
// installer accepts, in a fixed order and with the fixed methods, and a
// second pack after the files' times change is the same bytes. The pseudo
// language pack packs to a .oalang whose manifest reads back. A folder the
// installer would refuse, and an output that already exists, are refused
// with nothing written.

#include "command.hpp"
#include "pack_names.hpp"

#include "oa/app/package_install.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/languages/language_pack.hpp"
#include "oa/formats/zip/stream.hpp"
#include "oa/platform/files.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;
namespace languages = oa::data::languages;
namespace sha256 = oa::base::sha256;
namespace zip = oa::formats::zip;

struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-tool-pack")) {}

    ~Scratch() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

struct Captured {
    int status = 0;
    std::string out;
    std::string err;
};

/// A made-up profile, in the shape the install check uses.
constexpr std::string_view mod_text =
    "oamod: 1\n"
    "id: example-mod\n"
    "name: Example Mod\n"
    "version: \"1.0\"\n"
    "description: A made-up mod the tests pack.\n"
    "requires: {base: ta-3.1c, catalogue: 1}\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n";

Captured run(std::vector<std::string> arguments) {
    std::ostringstream out;
    std::ostringstream err;
    oa::tool::Output output{out, err};
    const int status = oa::tool::run_tool(arguments, output);
    return {status, out.str(), err.str()};
}

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    OA_CHECK(static_cast<bool>(out));
}

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Tells whether a folder can hold two names that differ only in case.
///
/// @param folder the folder
/// @return true when it can
bool holds_distinct_case(const fs::path& folder) {
    const fs::path upper = folder / "PACKCASE";
    const fs::path lower = folder / "packcase";
    write_text(upper, "U");
    write_text(lower, "L");
    std::ifstream in(upper);
    std::string text;
    in >> text;
    std::error_code error;
    fs::remove(upper, error);
    fs::remove(lower, error);
    return text == "U";
}

/// Restores the process's current folder.
struct WorkingFolder {
    fs::path previous;

    /// Makes path the current folder.
    ///
    /// @param path the folder
    explicit WorkingFolder(const fs::path& path) : previous(fs::current_path()) {
        fs::current_path(path);
    }

    ~WorkingFolder() {
        std::error_code error;
        fs::current_path(previous, error);
    }

    WorkingFolder(const WorkingFolder&) = delete;
    WorkingFolder& operator=(const WorkingFolder&) = delete;
};

struct OpenFile {
    std::FILE* stream{};
    oa::platform::Files files{};

    OpenFile() = default;
    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;

    ~OpenFile() {
        if (stream != nullptr)
            std::fclose(stream);
    }
};

bool read_at(void* context, uint64_t offset, std::span<uint8_t> bytes) {
    auto& file = *static_cast<OpenFile*>(context);
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
    if (file.stream == nullptr ||
        file.files.seek(
            nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
        ) != 0)
        return false;
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::size_t read =
            std::fread(bytes.data() + done, 1, bytes.size() - done, file.stream);
        if (read == 0)
            return false;
        done += read;
    }
    return true;
}

/// Reads a package's central directory.
///
/// @param path the package
/// @param[out] directory its entries
/// @return true when it was read
bool read_directory(const fs::path& path, zip::StreamDirectory& directory) {
    OpenFile file;
    file.stream = oa::platform::open_file(path, "rb");
    if (file.stream == nullptr)
        return false;
    file.files = oa::platform::stdio_files();
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
    if (file.files.seek(nullptr, handle, 0, oa::platform::SeekOrigin::end) != 0)
        return false;
    const int64_t end = file.files.tell(nullptr, handle);
    if (end < 0)
        return false;
    zip::ZipError error{};
    return zip::read_stream_directory(
        {&file, read_at}, static_cast<uint64_t>(end), {}, directory, error
    );
}

/// The mod folder the order and method checks pack.
///
/// @param folder the folder
void make_mod(const fs::path& folder) {
    write_text(folder / "oamod.yaml", mod_text);
    write_text(folder / "readme.txt", "hello readme\n");
    write_text(folder / "blank.txt", "");
    write_text(folder / "data.zip", "PK stored");
    write_text(folder / "units" / "armcom.fbi", "unit\n");
    write_text(folder / "pics" / "badge.png", "not-really-a-png");
    write_text(folder / "pics" / "empty.ogg", "");
    write_text(folder / "shot.PNG", "PNGDATA");
    fs::create_directories(folder / "notes");
    write_text(folder / ".DS_Store", "finder");
    write_text(folder / ".backup" / "old.txt", "kept");
    write_text(folder / "__MACOSX" / "junk", "attr");
    write_text(folder / "._readme.txt", "apple");
    write_text(folder / ".git" / "config", "git");
    write_text(folder / ".oa-origin.yaml", "origin");
    write_text(folder / "units" / ".DS_Store", "nested");
}

void touch_tree(const fs::path& folder) {
    const auto when = fs::file_time_type::clock::now() + std::chrono::hours(48);
    std::error_code error;
    fs::last_write_time(folder, when, error);
    for (fs::recursive_directory_iterator cursor(folder), end; cursor != end; ++cursor)
        fs::last_write_time(cursor->path(), when, error);
}

void test_mod(const fs::path& scratch) {
    const fs::path folder = scratch / "mod";
    make_mod(folder);
    const fs::path out = scratch / "example.oamod";
    const Captured packed = run({"pack", "--out", out.string(), folder.string()});
    OA_CHECK(packed.status == oa::tool::exit_done);
    OA_CHECK(packed.err.empty());
    OA_CHECK(contains(packed.out, "packed " + out.generic_string() + ": 8 files, "));
    OA_CHECK(contains(packed.out, " sha256 "));
    OA_CHECK(!fs::exists(fs::path(out.string() + ".part")));

    const install::PackageResult opened = install::open_package(out);
    OA_CHECK(opened.package.has_value());
    if (opened.package) {
        OA_CHECK(opened.package->incoming.id == "example-mod");
        OA_CHECK(opened.package->incoming.version == "1.0");
        OA_CHECK(opened.package->incoming.revision == 1);
        for (const install::PackagedFile& file : opened.package->files) {
            OA_CHECK(file.name != ".DS_Store");
            OA_CHECK(file.name != ".oa-origin.yaml");
            OA_CHECK(!file.name.starts_with(".backup"));
            OA_CHECK(file.name.find("__MACOSX") == std::string::npos);
            OA_CHECK(file.name.find(".git") == std::string::npos);
            OA_CHECK(!file.name.starts_with("._"));
            OA_CHECK(file.name.find("/._") == std::string::npos);
        }
    }

    zip::StreamDirectory directory;
    OA_CHECK(read_directory(out, directory));
    const std::pair<std::string_view, zip::Method> expect[] = {
        {"oamod.yaml", zip::Method::deflated},
        {"blank.txt", zip::Method::stored},
        {"data.zip", zip::Method::stored},
        {"notes/", zip::Method::stored},
        {"pics/badge.png", zip::Method::stored},
        {"pics/empty.ogg", zip::Method::stored},
        {"readme.txt", zip::Method::deflated},
        {"shot.PNG", zip::Method::stored},
        {"units/armcom.fbi", zip::Method::deflated},
    };
    OA_CHECK(directory.entries.size() == 9);
    if (directory.entries.size() == 9) {
        for (std::size_t index = 0; index < 9; ++index) {
            OA_CHECK(directory.entries[index].name == expect[index].first);
            OA_CHECK(directory.entries[index].method == expect[index].second);
            OA_CHECK(directory.entries[index].directory == (expect[index].first == "notes/"));
        }
    }

    touch_tree(folder);
    const fs::path again = scratch / "example-again.oamod";
    const Captured repacked = run({"pack", folder.string(), "--out", again.string()});
    OA_CHECK(repacked.status == oa::tool::exit_done);
    OA_CHECK(read_bytes(out) == read_bytes(again));

    const fs::path cwd = scratch / "cwd";
    fs::create_directories(cwd);
    {
        const WorkingFolder here(cwd);
        const Captured named = run({"pack", folder.string()});
        OA_CHECK(named.status == oa::tool::exit_done);
        OA_CHECK(contains(named.out, "packed example-mod-1.0-r1.oamod: "));
    }
    OA_CHECK(fs::exists(cwd / "example-mod-1.0-r1.oamod"));
    OA_CHECK(read_bytes(cwd / "example-mod-1.0-r1.oamod") == read_bytes(out));
}

void test_language(const fs::path& scratch, const fs::path& pseudo) {
    const fs::path folder = scratch / "pseudo";
    fs::copy(pseudo, folder, fs::copy_options::recursive);
    const fs::path out = scratch / "pseudo.oalang";
    const Captured packed = run({"pack", folder.string(), "--out", out.string()});
    OA_CHECK(packed.status == oa::tool::exit_done);
    OA_CHECK(contains(packed.out, "packed " + out.generic_string() + ": "));

    zip::StreamDirectory directory;
    OA_CHECK(read_directory(out, directory));
    OA_CHECK(!directory.entries.empty());
    if (!directory.entries.empty())
        OA_CHECK(directory.entries.front().name == "language.yaml");

    OpenFile file;
    file.stream = oa::platform::open_file(out, "rb");
    OA_CHECK(file.stream != nullptr);
    if (file.stream != nullptr) {
        file.files = oa::platform::stdio_files();
        auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
        OA_CHECK(file.files.seek(nullptr, handle, 0, oa::platform::SeekOrigin::end) == 0);
        const int64_t end = file.files.tell(nullptr, handle);
        const zip::StreamEntry* manifest = nullptr;
        for (const zip::StreamEntry& entry : directory.entries)
            if (entry.name == "language.yaml")
                manifest = &entry;
        OA_CHECK(manifest != nullptr);
        if (manifest != nullptr && end >= 0) {
            std::vector<uint8_t> bytes;
            zip::ZipError error{};
            OA_CHECK(
                zip::read_stream_entry(
                    {&file, read_at},
                    static_cast<uint64_t>(end),
                    *manifest,
                    manifest->bytes,
                    bytes,
                    error
                )
            );
            languages::PackManifest read{};
            std::string why;
            OA_CHECK(languages::read_manifest(bytes, read, &why));
            OA_CHECK(read.tag == "en-XA");
            OA_CHECK(read.version == "1");
        }
    }

    touch_tree(folder);
    const fs::path again = scratch / "pseudo-again.oalang";
    const Captured repacked = run({"pack", "--out", again.string(), folder.string()});
    OA_CHECK(repacked.status == oa::tool::exit_done);
    OA_CHECK(read_bytes(out) == read_bytes(again));
}

/// Runs a pack that must fail and leave the output, and its .part, absent.
///
/// @param scratch the scratch folder
/// @param folder the folder to pack
/// @param name the output's name
/// @return what the command printed
Captured refuse(const fs::path& scratch, const fs::path& folder, std::string_view name) {
    const fs::path out = scratch / name;
    const Captured result = run({"pack", folder.string(), "--out", out.string()});
    OA_CHECK(result.status == oa::tool::exit_failed);
    OA_CHECK(!fs::exists(out));
    OA_CHECK(!fs::exists(fs::path(out.string() + ".part")));
    return result;
}

/// One file or folder named for check_names. A folder carries no bytes.
///
/// @param name the path inside the folder
/// @param folder it is a folder
/// @return the item
oa::tool::detail::Item named(std::string name, bool folder) {
    oa::tool::detail::Item item;
    item.name = std::move(name);
    item.folder = folder;
    item.bytes = folder ? 0 : 1;
    return item;
}

/// Returns the refusal check_names raises, or an empty string when it accepts.
///
/// @param items the names
/// @return the message
std::string refusal(const std::vector<oa::tool::detail::Item>& items) {
    try {
        oa::tool::detail::check_names(items);
    } catch (const oa::tool::Failure& failure) {
        return failure.what();
    }
    return {};
}

/// The case rule and the folder limit, with no volume underneath them.
void test_name_rules() {
    OA_CHECK(
        refusal({named("readme.txt", false), named("units/arm.txt", false), named("notes", true)})
            .empty()
    );
    OA_CHECK(contains(
        refusal({named("units/A.txt", false), named("units/a.txt", false)}),
        "differ only in case: units/a.txt"
    ));
    OA_CHECK(contains(
        refusal({named("Maps/x.ota", false), named("maps/y.ota", false)}),
        "differ only in case: maps/y.ota"
    ));
    OA_CHECK(contains(refusal({named("a", false), named("A", true)}), "differ only in case: A"));

    std::vector<oa::tool::detail::Item> folders;
    folders.reserve(install::max_package_folders + 1);
    for (std::size_t index = 0; index <= install::max_package_folders; ++index)
        folders.push_back(named("f" + std::to_string(index), true));
    OA_CHECK(contains(refusal(folders), "more folders than a package may"));
}

void test_refusals(const fs::path& scratch) {
    const fs::path empty = scratch / "empty";
    fs::create_directories(empty);
    write_text(empty / "readme.txt", "no manifest\n");
    OA_CHECK(contains(refuse(scratch, empty, "none.oamod").err, "no oamod.yaml or language.yaml"));

    const fs::path both = scratch / "both";
    write_text(both / "oamod.yaml", mod_text);
    write_text(both / "language.yaml", "oalang: 1\n");
    OA_CHECK(
        contains(refuse(scratch, both, "both.oamod").err, "both oamod.yaml and language.yaml")
    );

    const fs::path bad = scratch / "bad-profile";
    write_text(
        bad / "oamod.yaml",
        "oamod: 1\n"
        "id: Not-Kebab\n"
        "name: Example Mod\n"
        "version: \"1.0\"\n"
        "requires: {base: ta-3.1c, catalogue: 1}\n"
        "author: {name: unknown}\n"
        "packaging: {revision: 1, date: 2026-10-04, packager: Open Annihilation}\n"
    );
    OA_CHECK(contains(refuse(scratch, bad, "bad.oamod").err, "kebab-case"));

    const fs::path colon = scratch / "colon";
    write_text(colon / "oamod.yaml", mod_text);
    write_text(colon / "has:colon.txt", "nope\n");
    OA_CHECK(contains(refuse(scratch, colon, "colon.oamod").err, "Windows refuses"));

    const fs::path cases = scratch / "case-plain";
    fs::create_directories(cases);
    if (holds_distinct_case(cases)) {
        write_text(cases / "oamod.yaml", mod_text);
        write_text(cases / "A.txt", "A\n");
        write_text(cases / "a.txt", "a\n");
        OA_CHECK(contains(refuse(scratch, cases, "case.oamod").err, "differ only in case"));
    } else {
        std::printf(
            "skipping the filesystem case check: this volume does not hold two names "
            "that differ only in case\n"
        );
    }

#ifndef _WIN32
    const fs::path linked = scratch / "linked";
    write_text(linked / "oamod.yaml", mod_text);
    write_text(scratch / "outside.txt", "outside\n");
    std::error_code error;
    fs::create_symlink(scratch / "outside.txt", linked / "alias.txt", error);
    OA_CHECK(!error);
    OA_CHECK(contains(refuse(scratch, linked, "link.oamod").err, "holds a link"));
#endif

    const fs::path mod = scratch / "mod-exists";
    make_mod(mod);
    const fs::path out = scratch / "exists.oamod";
    write_text(out, "original-bytes");
    const Captured blocked = run({"pack", mod.string(), "--out", out.string()});
    OA_CHECK(blocked.status == oa::tool::exit_failed);
    OA_CHECK(contains(blocked.err, "already exists"));
    const std::vector<uint8_t> kept = read_bytes(out);
    OA_CHECK(std::string(kept.begin(), kept.end()) == "original-bytes");
    OA_CHECK(!fs::exists(fs::path(out.string() + ".part")));

    const Captured forced = run({"pack", "--force", mod.string(), "--out", out.string()});
    OA_CHECK(forced.status == oa::tool::exit_done);
    OA_CHECK(std::string(read_bytes(out).begin(), read_bytes(out).end()) != "original-bytes");
}

void test_usage(const fs::path& scratch) {
    const fs::path folder = scratch / "usage";
    write_text(folder / "oamod.yaml", mod_text);
    const fs::path out = scratch / "usage.oamod";
    const Captured before = run({"pack", "--out", out.string(), folder.string()});
    OA_CHECK(before.status == oa::tool::exit_done);
    const fs::path after_path = scratch / "usage-after.oamod";
    const Captured after = run({"pack", folder.string(), "--out", after_path.string()});
    OA_CHECK(after.status == oa::tool::exit_done);
    OA_CHECK(read_bytes(out) == read_bytes(after_path));

    const Captured extra = run({"pack", folder.string(), "extra.oamod"});
    OA_CHECK(extra.status == oa::tool::exit_usage);
    OA_CHECK(contains(extra.err, "run 'oa-tool help pack'\n"));
    OA_CHECK(!fs::exists(scratch / "extra.oamod"));

    const Captured none = run({"pack", "--out", out.string()});
    OA_CHECK(none.status == oa::tool::exit_usage);
}

void test_help() {
    const Captured help = run({"help", "pack"});
    OA_CHECK(help.status == oa::tool::exit_done);
    OA_CHECK(contains(help.out, "oa-tool pack FOLDER [--out FILE] [--force] [--game-dir DIR]\n"));
    OA_CHECK(contains(help.out, "png"));
    OA_CHECK(contains(help.out, "deflated"));
    OA_CHECK(contains(help.out, "empty file"));

    const Captured flag = run({"pack", "--help"});
    OA_CHECK(flag.status == oa::tool::exit_done);
    OA_CHECK(flag.out == help.out);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "the pseudo pack folder is missing\n");
        return 2;
    }
    const Scratch scratch;
    test_mod(scratch.path);
    test_language(scratch.path, fs::path(argv[1]));
    test_name_rules();
    test_refusals(scratch.path);
    test_usage(scratch.path);
    test_help();
    return oa::test::check_exit_status();
}
