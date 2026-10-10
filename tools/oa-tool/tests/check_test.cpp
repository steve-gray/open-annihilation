// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-tool check, run in-process: a made-up mod package reports the catalogue
// facts in order, with hashes of the profile resolved without its settings;
// a damaged archive, an unknown kind and an unsafe name are refused and
// still report the file's size and hash; a language manifest read on its
// own gives the language block.

#include "check.hpp"
#include "command.hpp"

#include "oa/app/package_install.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/mod_profile.hpp"
#include "oa/data/mod_profile/registry.hpp"
#include "oa/formats/json.hpp"
#include "oa/test/check.hpp"
#include "oa/test/raw_zip.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::package_install;
namespace json = oa::formats::json;
namespace mod_profile = oa::data::mod_profile;
namespace raw = oa::test::raw_zip;
namespace registry = oa::data::mod_profile::registry;
namespace sha256 = oa::base::sha256;

/// A made-up profile. One sim hack, one view hack, and a sim limit bound to
/// a packaged settings file so the installer's resolution differs from the
/// catalogue's.
constexpr std::string_view mod_text =
    "oamod: 1\n"
    "id: example-mod\n"
    "name: Example Mod\n"
    "version: \"1.0\"\n"
    "description: A made-up mod the check reports.\n"
    "homepage: \"https://example.org/mod\"\n"
    "tags: [example]\n"
    "requires: {base: ta-3.1c, catalogue: 1, engine: \">= 0.0.1\"}\n"
    "author: {name: Ada Example}\n"
    "packaging: {revision: 3, date: 2026-11-01, packager: Open Annihilation}\n"
    "identity: {settings-file: Example.ini}\n"
    "limits:\n"
    "  path-search-budget: true\n"
    "settings:\n"
    "  limits.path-search-budget.nodes: {ini: Preferences/AISearchMapEntries}\n"
    "hacks:\n"
    "  ai.squad5-factory-tick: true\n"
    "  ui.whiteboard: true\n";

constexpr std::string_view ini_text = "[Preferences]\nAISearchMapEntries = 1000;\n";

/// A language manifest with a packaging revision, made up for this test.
constexpr std::string_view language_with_revision =
    "oalang: 1\n"
    "tag: en-XB\n"
    "name: Made Up\n"
    "english-name: Made Up\n"
    "word: Made\n"
    "version: \"2\"\n"
    "locales: [en-XB]\n"
    "fallbacks: [en]\n"
    "text: {needs: modern-fonts}\n"
    "unicode: false\n"
    "packaging: {revision: 4, date: 2026-11-01, packager: Open Annihilation}\n";

struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-tool-check")) {}

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

void write_bytes(const fs::path& path, std::span<const uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    OA_CHECK(static_cast<bool>(out));
}

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string hex_of(const sha256::Digest& digest) {
    const auto hex = sha256::to_hex(digest);
    return {hex.data(), hex.size()};
}

std::string digest_of(std::span<const uint8_t> bytes) {
    return hex_of(sha256::digest_of(bytes));
}

void show(const Captured& captured) {
    std::fprintf(
        stderr,
        "status %d\nstdout:\n%sstderr:\n%s",
        captured.status,
        captured.out.c_str(),
        captured.err.c_str()
    );
}

void expect_status(const Captured& captured, int status) {
    if (captured.status != status)
        show(captured);
    OA_CHECK(captured.status == status);
}

std::optional<json::Json> parsed_object(const Captured& captured) {
    json::JsonError error{};
    std::optional<json::Json> value = json::parse_json(captured.out, error);
    if (!value || value->type() != json::JsonType::object) {
        show(captured);
        OA_CHECK(false);
        return std::nullopt;
    }
    return value;
}

const json::Json* member(const json::Json& object, std::string_view name) {
    return object.find(name);
}

std::string text_of(const json::Json* value) {
    if (value == nullptr || value->string() == nullptr)
        return {};
    return *value->string();
}

std::vector<std::string> names_of(const json::Json& object) {
    const std::span<const std::string> names = object.names();
    return {names.begin(), names.end()};
}

void expect_names(const json::Json& object, std::vector<std::string> expected) {
    OA_CHECK(names_of(object) == expected);
}

std::vector<std::string> strings_of(const json::Json* value) {
    std::vector<std::string> lines;
    if (value == nullptr)
        return lines;
    for (const json::Json& item : value->elements())
        if (item.string() != nullptr)
            lines.push_back(*item.string());
    return lines;
}

void expect_strings(const json::Json* value, std::vector<std::string> expected) {
    OA_CHECK(strings_of(value) == expected);
}

/// The first unimplemented hack, or empty when this build carries every one out.
std::string unimplemented_hack() {
    for (const registry::Entry& entry : registry::table().entries)
        if (entry.kind == registry::EntryKind::hack && !entry.implemented)
            return std::string(entry.id);
    return {};
}

std::string profile_with_hack(std::string_view hack) {
    return "oamod: 1\n"
           "id: example-mod\n"
           "name: Example Mod\n"
           "version: \"1.0\"\n"
           "description: A made-up mod the check reports.\n"
           "requires: {base: ta-3.1c, catalogue: 1}\n"
           "author: {name: Ada Example}\n"
           "packaging: {revision: 1, date: 2026-11-01, packager: Open Annihilation}\n"
           "hacks:\n"
           "  " +
           std::string(hack) + ": true\n";
}

fs::path write_package(const Scratch& scratch, std::string_view name, std::string_view profile) {
    const raw::RawArchive archive = raw::build_raw_archive(
        {raw::stored_text("oamod.yaml", profile), raw::stored_text("Example.ini", ini_text)}
    );
    const fs::path path = scratch.path / name;
    write_bytes(path, archive.bytes);
    return path;
}

void test_mod(const Scratch& scratch) {
    const fs::path path = write_package(scratch, "example-mod-1.0-r3.oamod", mod_text);
    const std::vector<uint8_t> bytes = read_bytes(path);
    const Captured checked = run({"check", path.string(), "--json"});
    expect_status(checked, oa::tool::exit_done);
    const std::optional<json::Json> report = parsed_object(checked);
    if (!report)
        return;

    const std::vector<std::string> expected_names{
        "check",   "ok",        "problems", "warnings", "file",     "kind",          "id",
        "name",    "version",   "revision", "size",     "sha256",   "unpacked_size", "files",
        "summary", "homepage",  "tags",     "author",   "requires", "sim_hash",      "full_hash",
        "hacks",   "packaging",
    };
    OA_CHECK(names_of(*report) == expected_names);
    OA_CHECK(member(*report, "check") && member(*report, "check")->integer() == 1);
    OA_CHECK(member(*report, "ok") && member(*report, "ok")->boolean() == true);
    OA_CHECK(strings_of(member(*report, "problems")).empty());
    OA_CHECK(strings_of(member(*report, "warnings")).empty());
    OA_CHECK(text_of(member(*report, "file")) == "example-mod-1.0-r3.oamod");
    OA_CHECK(text_of(member(*report, "kind")) == "oamod");
    OA_CHECK(text_of(member(*report, "id")) == "example-mod");
    OA_CHECK(text_of(member(*report, "name")) == "Example Mod");
    OA_CHECK(text_of(member(*report, "version")) == "1.0");
    OA_CHECK(member(*report, "revision") && member(*report, "revision")->integer() == 3);
    OA_CHECK(
        member(*report, "size") &&
        member(*report, "size")->integer() == static_cast<int64_t>(bytes.size())
    );
    OA_CHECK(text_of(member(*report, "sha256")) == digest_of(bytes));
    OA_CHECK(text_of(member(*report, "sha256")).size() == 64);
    OA_CHECK(
        member(*report, "unpacked_size") &&
        member(*report, "unpacked_size")->integer() ==
            static_cast<int64_t>(mod_text.size() + ini_text.size())
    );
    OA_CHECK(member(*report, "files") && member(*report, "files")->integer() == 2);
    OA_CHECK(text_of(member(*report, "summary")) == "A made-up mod the check reports.");
    OA_CHECK(text_of(member(*report, "homepage")) == "https://example.org/mod");
    expect_strings(member(*report, "tags"), {"example"});
    OA_CHECK(text_of(member(*report, "author")) == "Ada Example");

    const json::Json* required = member(*report, "requires");
    OA_CHECK(required != nullptr);
    if (required != nullptr) {
        expect_names(*required, {"base", "engine"});
        OA_CHECK(text_of(member(*required, "base")) == "ta-3.1c");
        OA_CHECK(text_of(member(*required, "engine")) == ">= 0.0.1");
        OA_CHECK(member(*required, "catalogue") == nullptr);
    }

    const auto profile_bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(mod_text.data()), mod_text.size()
    );
    const mod_profile::ResolveResult resolved =
        mod_profile::resolve_profile(profile_bytes, "oamod.yaml");
    OA_CHECK(resolved.resolution.has_value());
    if (resolved.resolution) {
        OA_CHECK(
            text_of(member(*report, "sim_hash")) == hex_of(resolved.resolution->profile.sim_hash)
        );
        OA_CHECK(
            text_of(member(*report, "full_hash")) == hex_of(resolved.resolution->profile.full_hash)
        );
    }
    const install::PackageResult opened = install::open_package(path);
    OA_CHECK(opened.package && opened.package->profile);
    if (opened.package) {
        json::JsonWriter direct;
        direct.begin_object();
        oa::tool::describe_oamod(*opened.package, direct);
        direct.end_object();
        json::JsonError direct_error{};
        const std::optional<json::Json> described = json::parse_json(direct.text(), direct_error);
        OA_CHECK(described.has_value());
        if (described)
            OA_CHECK(
                text_of(member(*described, "sim_hash")) == text_of(member(*report, "sim_hash"))
            );
    }
    if (opened.package && opened.package->profile && resolved.resolution) {
        OA_CHECK(opened.package->profile->sim_hash != resolved.resolution->profile.sim_hash);
        OA_CHECK(text_of(member(*report, "sim_hash")) != hex_of(opened.package->profile->sim_hash));
    }

    const json::Json* hacks = member(*report, "hacks");
    OA_CHECK(hacks != nullptr);
    if (hacks != nullptr)
        expect_names(*hacks, {"sim", "view", "ids", "unimplemented"});
    OA_CHECK(hacks && member(*hacks, "sim") && member(*hacks, "sim")->integer() == 1);
    OA_CHECK(hacks && member(*hacks, "view") && member(*hacks, "view")->integer() == 1);
    expect_strings(
        hacks != nullptr ? member(*hacks, "ids") : nullptr,
        {"ai.squad5-factory-tick", "ui.whiteboard"}
    );
    OA_CHECK(strings_of(hacks ? member(*hacks, "unimplemented") : nullptr).empty());

    const json::Json* packaging = member(*report, "packaging");
    OA_CHECK(packaging != nullptr);
    if (packaging != nullptr)
        expect_names(*packaging, {"revision", "date", "packager"});
    OA_CHECK(packaging && member(*packaging, "revision")->integer() == 3);
    OA_CHECK(text_of(packaging ? member(*packaging, "date") : nullptr) == "2026-11-01");
    OA_CHECK(text_of(packaging ? member(*packaging, "packager") : nullptr) == "Open Annihilation");
    OA_CHECK(!contains(checked.out, "null"));
    OA_CHECK(!contains(checked.out, "result"));

    const Captured text = run({"check", path.string()});
    expect_status(text, oa::tool::exit_done);
    OA_CHECK(contains(text.out, "result: ok\n"));
    OA_CHECK(contains(text.out, "kind: oamod\n"));
    OA_CHECK(contains(text.out, "requires.engine: >= 0.0.1\n"));
    OA_CHECK(contains(text.out, "hacks.sim: 1\n"));
    OA_CHECK(contains(text.out, "hacks.ids: [\"ai.squad5-factory-tick\",\"ui.whiteboard\"]\n"));
}

void expect_refused_with_hash(
    const Scratch& scratch, std::string_view name, std::span<const uint8_t> bytes
) {
    const fs::path path = scratch.path / name;
    write_bytes(path, bytes);
    const Captured checked = run({"check", path.string(), "--json"});
    expect_status(checked, oa::tool::exit_failed);
    const std::optional<json::Json> report = parsed_object(checked);
    if (!report)
        return;
    OA_CHECK(member(*report, "ok") && member(*report, "ok")->boolean() == false);
    OA_CHECK(!strings_of(member(*report, "problems")).empty());
    OA_CHECK(
        member(*report, "size") &&
        member(*report, "size")->integer() == static_cast<int64_t>(bytes.size())
    );
    OA_CHECK(text_of(member(*report, "sha256")) == digest_of(bytes));
    OA_CHECK(text_of(member(*report, "sha256")).size() == 64);
}

void test_refusals(const Scratch& scratch) {
    const std::vector<uint8_t> damaged(500, static_cast<uint8_t>('x'));
    expect_refused_with_hash(scratch, "damaged.oamod", damaged);
    const Captured damaged_report =
        run({"check", (scratch.path / "damaged.oamod").string(), "--json"});
    const std::optional<json::Json> damaged_json = parsed_object(damaged_report);
    if (damaged_json)
        OA_CHECK(text_of(member(*damaged_json, "kind")) == "oamod");

    const std::vector<uint8_t> zip_bytes{'n', 'o', 't', 'a', 'z', 'i', 'p'};
    expect_refused_with_hash(scratch, "loose.zip", zip_bytes);
    const Captured zip_report = run({"check", (scratch.path / "loose.zip").string(), "--json"});
    const std::optional<json::Json> zip_json = parsed_object(zip_report);
    if (zip_json) {
        OA_CHECK(member(*zip_json, "kind") == nullptr);
        OA_CHECK(contains(zip_report.out, "not a kind of package"));
    }

    const raw::RawArchive unsafe = raw::build_raw_archive(
        {raw::stored_text("oamod.yaml", mod_text), raw::stored_text("../gone.txt", "x")}
    );
    expect_refused_with_hash(scratch, "unsafe.oamod", unsafe.bytes);

    const Captured missing = run({"check", (scratch.path / "nosuch.oamod").string(), "--json"});
    expect_status(missing, oa::tool::exit_failed);
    const std::optional<json::Json> missing_json = parsed_object(missing);
    if (missing_json) {
        OA_CHECK(text_of(member(*missing_json, "kind")) == "oamod");
        OA_CHECK(member(*missing_json, "size") == nullptr);
        OA_CHECK(member(*missing_json, "sha256") == nullptr);
        OA_CHECK(contains(missing.out, "It cannot be read."));
    }
}

void test_unimplemented(const Scratch& scratch) {
    const std::string hack = unimplemented_hack();
    if (hack.empty())
        return;
    const fs::path path = write_package(scratch, "unimplemented.oamod", profile_with_hack(hack));
    const Captured refused = run({"check", path.string(), "--json"});
    expect_status(refused, oa::tool::exit_failed);
    OA_CHECK(contains(refused.out, hack));
    OA_CHECK(contains(refused.out, "does not implement"));
    const Captured accepted =
        run({"check", "--accept-unimplemented-hacks", path.string(), "--json"});
    expect_status(accepted, oa::tool::exit_done);
    OA_CHECK(contains(accepted.out, hack));
    OA_CHECK(contains(accepted.out, "accepted for development"));
}

void test_usage(const Scratch& scratch) {
    const Captured none = run({"check"});
    expect_status(none, oa::tool::exit_usage);
    OA_CHECK(contains(none.err, "oa-tool check: "));
    OA_CHECK(contains(none.err, "run 'oa-tool help check'\n"));

    const fs::path path = write_package(scratch, "usage.oamod", mod_text);
    const Captured extra = run({"check", path.string(), "another.oamod"});
    expect_status(extra, oa::tool::exit_usage);
    const Captured unknown = run({"check", path.string(), "--nope"});
    expect_status(unknown, oa::tool::exit_usage);
    const Captured empty_dir = run({"check", path.string(), "--game-dir="});
    expect_status(empty_dir, oa::tool::exit_usage);
    const Captured flagged = run({"check", path.string(), "--accept-unimplemented-hacks"});
    expect_status(flagged, oa::tool::exit_done);
}

void expect_language_keys(const json::Json& language) {
    expect_names(
        language,
        {"tag", "name", "english-name", "word", "locales", "fallbacks", "text-needs", "unicode"}
    );
}

void test_oalang(const fs::path& manifest_path) {
    const std::vector<uint8_t> bytes = read_bytes(manifest_path);
    OA_CHECK(!bytes.empty());
    json::JsonWriter writer;
    writer.begin_object();
    oa::tool::describe_oalang(bytes, writer);
    writer.end_object();
    json::JsonError error{};
    const std::optional<json::Json> report = json::parse_json(writer.text(), error);
    OA_CHECK(report && report->type() == json::JsonType::object);
    if (!report)
        return;
    expect_names(*report, {"id", "name", "version", "revision", "language"});
    OA_CHECK(text_of(member(*report, "id")) == "en-XA");
    OA_CHECK(text_of(member(*report, "name")) == "［测Pseudo试］");
    OA_CHECK(text_of(member(*report, "version")) == "1");
    OA_CHECK(
        member(*report, "revision") && member(*report, "revision")->type() == json::JsonType::null
    );
    const json::Json* language = member(*report, "language");
    OA_CHECK(language != nullptr);
    if (language == nullptr)
        return;
    expect_language_keys(*language);
    OA_CHECK(text_of(member(*language, "tag")) == "en-XA");
    OA_CHECK(text_of(member(*language, "english-name")) == "Pseudo");
    OA_CHECK(text_of(member(*language, "word")) == "Pseudo");
    expect_strings(member(*language, "locales"), {"en-XA"});
    OA_CHECK(strings_of(member(*language, "fallbacks")).empty());
    OA_CHECK(text_of(member(*language, "text-needs")) == "modern-fonts");
    OA_CHECK(member(*language, "unicode") && member(*language, "unicode")->boolean() == true);
    OA_CHECK(member(*report, "homepage") == nullptr);
    OA_CHECK(member(*language, "requires") == nullptr);

    const auto revision_bytes = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(language_with_revision.data()),
        language_with_revision.size()
    );
    json::JsonWriter revised_writer;
    revised_writer.begin_object();
    oa::tool::describe_oalang(revision_bytes, revised_writer);
    revised_writer.end_object();
    const std::optional<json::Json> revised = json::parse_json(revised_writer.text(), error);
    OA_CHECK(
        revised && member(*revised, "revision") && member(*revised, "revision")->integer() == 4
    );
    OA_CHECK(revised && text_of(member(*revised, "id")) == "en-XB");
}

void test_help() {
    const Captured help = run({"help", "check"});
    expect_status(help, oa::tool::exit_done);
    OA_CHECK(contains(help.out, "sim_hash"));
    OA_CHECK(contains(help.out, "unpacked_size"));
    OA_CHECK(contains(help.out, "language"));
    OA_CHECK(contains(help.out, "full_hash"));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: oa-tool-check-test language.yaml\n");
        return 1;
    }
    const Scratch scratch;
    test_mod(scratch);
    test_refusals(scratch);
    test_unimplemented(scratch);
    test_usage(scratch);
    test_oalang(argv[1]);
    test_help();
    return oa::test::check_exit_status();
}
