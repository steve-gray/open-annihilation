// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Updates from origin records: a higher release of the same registry and
// key, a file install matched by SHA-256, rules hashes, a release this
// build cannot install, and a roll back that brings the older record back.
// Temporary folders and in-memory catalogues. No network.

#include "oa/app/content/updates.hpp"

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/app/package_install/origin.hpp"
#include "oa/base/sha256.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/mod_profile.hpp"
#include "oa/formats/oamod/package_keys.hpp"
#include "oa/test/check.hpp"
#include "oa/test/raw_zip.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace content = oa::app::content;
namespace install = oa::app::package_install;
namespace catalogue = oa::data::catalogue;
namespace profile = oa::data::mod_profile;
namespace sha = oa::base::sha256;
namespace raw = oa::test::raw_zip;
using install::Change;

/// A scratch folder, deleted when the test ends.
class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-content-updates-")) {}

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
void write_text(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

/// A profile. `resolves` false leaves out the fields a profile needs.
///
/// @param id the mod's id
/// @param name the name a player sees
/// @param revision packaging.revision
/// @param resolves true for a profile the resolver accepts
/// @return the manifest
std::string profile_text(
    std::string_view id,
    std::string_view name,
    int revision,
    bool resolves,
    std::string_view extra = {}
) {
    std::string text = "oamod: 1\nid: ";
    text.append(id);
    text.append("\nname: \"");
    text.append(name);
    text.append("\"\nversion: \"4.8\"\n");
    if (!resolves)
        return text;
    text.append("requires: {base: ta-3.1c, catalogue: 1}\n");
    text.append("author: {name: unknown}\n");
    text.append("packaging: {revision: ");
    text.append(std::to_string(revision));
    text.append(", date: 2026-10-04, packager: Open Annihilation}\n");
    text.append(extra);
    return text;
}

/// Writes a mod folder and, when given, its origin record.
///
/// @param user the player's folder
/// @param id the folder and the manifest's id
/// @param name the name a player sees
/// @param revision packaging.revision
/// @param origin the record; nothing writes none
/// @param resolves true for a profile the resolver accepts
void place_mod(
    const fs::path& user,
    std::string_view id,
    std::string_view name,
    int revision,
    const std::optional<install::Origin>& origin,
    bool resolves
) {
    const fs::path folder = user / "Mods" / std::string(id);
    write_text(folder / "oamod.yaml", profile_text(id, name, revision, resolves));
    if (origin)
        write_text(folder / std::string(install::origin_file_name), install::origin_text(*origin));
}

/// A catalogue origin at a release, with the given SHA-256.
///
/// @param registry the registry id
/// @param id the package key
/// @param release the release
/// @param digest the package file's SHA-256
/// @return the origin
install::Origin
catalogue_origin(std::string registry, std::string id, int64_t release, sha::Digest digest) {
    install::Origin origin;
    origin.kind = install::OriginKind::catalogue;
    origin.registry = std::move(registry);
    origin.catalogue_id = std::move(id);
    origin.release = release;
    origin.sha256 = digest;
    origin.installed = "2026-10-01";
    return origin;
}

/// A file origin with the given SHA-256.
///
/// @param digest the package file's SHA-256
/// @return the origin
install::Origin file_origin(sha::Digest digest) {
    install::Origin origin;
    origin.kind = install::OriginKind::file;
    origin.sha256 = digest;
    origin.installed = "2026-10-01";
    return origin;
}

/// The SHA-256 of a short text, so two tests can share a known digest.
///
/// @param text the text
/// @return its digest
sha::Digest digest_of(std::string_view text) {
    return sha::digest_of({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

/// Catalogues kept alive for the entries that point into them.
struct Library {
    std::vector<std::shared_ptr<catalogue::Catalogue>> catalogues{};
    content::Snapshot snapshot{};

    /// Adds one registry's packages, in the order given.
    ///
    /// @param registry the registry id
    /// @param reviewed true for a built-in registry
    /// @param packages the entries
    void add(std::string registry, bool reviewed, std::vector<catalogue::Package> packages) {
        auto loaded = std::make_shared<catalogue::Catalogue>();
        loaded->registry = registry;
        loaded->packages = std::move(packages);
        for (const catalogue::Package& package : loaded->packages) {
            content::Entry entry;
            entry.registry = registry;
            entry.package = &package;
            entry.reviewed = reviewed;
            snapshot.entries.push_back(std::move(entry));
        }
        catalogues.push_back(std::move(loaded));
    }
};

/// A mod entry at a release.
///
/// @param id the package key
/// @param release the release
/// @param shown the version text the update copies
/// @param bytes the package's size
/// @param digest the package file's SHA-256
/// @return the entry
catalogue::Package mod_release(
    std::string id, int64_t release, std::string shown, uint64_t bytes, sha::Digest digest
) {
    catalogue::Package package;
    package.kind = catalogue::Kind::oamod;
    package.id = std::move(id);
    package.release = release;
    package.version = std::move(shown);
    package.revision = 4;
    package.size = bytes;
    package.sha256 = digest;
    return package;
}

/// The installed package with this key, or null.
///
/// @param listed the list
/// @param key the key
/// @return the package
const content::InstalledPackage*
find_key(const std::vector<content::InstalledPackage>& listed, std::string_view key) {
    for (const content::InstalledPackage& package : listed)
        if (package.key == key)
            return &package;
    return nullptr;
}

/// The update for this key, or null.
///
/// @param updates the updates
/// @param key the key
/// @return the update
const content::Update*
find_update(const std::vector<content::Update>& updates, std::string_view key) {
    for (const content::Update& update : updates)
        if (update.key == key)
            return &update;
    return nullptr;
}

/// Prints why a package was refused.
///
/// @param problem the refusal
void print_problem(const install::Problem& problem) {
    std::fprintf(
        stderr,
        "package refused: %s\n",
        problem.detail.empty() ? problem.subject.c_str() : problem.detail.c_str()
    );
    for (const std::string& line : problem.lines)
        std::fprintf(stderr, "  %s\n", line.c_str());
}

/// Deletes discard folders to the end.
///
/// @param folders the folders
void discard_all(const std::vector<fs::path>& folders) {
    install::Discarder discarder;
    discarder.add(folders);
    for (int step = 0; step < 100000 && discarder.step(); ++step) {
    }
}

/// Installs a package the way the game does, with the origin given.
///
/// @param file the package
/// @param mods the Mods folder
/// @param change the change
/// @param origin where it came from
/// @return true when the files are in place
bool install_package(
    const fs::path& file, const fs::path& mods, Change change, const install::Origin& origin
) {
    const auto opened = install::open_package(file);
    if (!opened.package) {
        print_problem(opened.problem);
        OA_CHECK(opened.package.has_value());
        return false;
    }
    const auto incoming = install::oamod::incoming_of(*opened.package->profile);
    const install::InstallPlan plan =
        install::oamod::plan_install(incoming, install::folder_hooks(install::mod_kind(), mods));
    const std::string target = plan.target.empty() ? std::string("ridge") : plan.target;
    install::Problem problem;
    install::Unpacking unpacking;
    if (!unpacking.start(*opened.package, mods, target, plan.installed, origin, problem)) {
        print_problem(problem);
        discard_all(unpacking.discards());
        OA_CHECK(false);
        return false;
    }
    auto step = oa::formats::zip::StreamStep::more;
    while (step == oa::formats::zip::StreamStep::more)
        step = unpacking.step(uint64_t{1} << 20, problem);
    if (step != oa::formats::zip::StreamStep::done) {
        print_problem(problem);
        discard_all(unpacking.discards());
        OA_CHECK(false);
        return false;
    }
    install::ChangeOptions options;
    options.expected = plan.installed;
    options.first_retry_ms = 0;
    const install::ChangeResult result =
        install::commit_change(install::mod_kind(), mods, target, change, options);
    discard_all(result.discards);
    if (!result.changed)
        std::fprintf(stderr, "change refused: %s\n", result.detail.c_str());
    OA_CHECK(result.changed);
    return result.changed;
}

/// Writes a package of Ridge at a packaging revision.
///
/// @param folder where it goes
/// @param revision the revision
/// @return the package
fs::path write_package(const fs::path& folder, int revision) {
    const fs::path file = folder / ("ridge-" + std::to_string(revision) + ".oamod");
    const auto archive = raw::build_raw_archive({
        raw::stored_text("oamod.yaml", profile_text("ridge", "Ridge", revision, true)),
        raw::stored_text("units/", ""),
        raw::stored_text("units/unit.fbi", "revision " + std::to_string(revision)),
    });
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(archive.bytes.data()),
        static_cast<std::streamsize>(archive.bytes.size())
    );
    return file;
}

/// One file's bytes and the time they were written.
struct Stamp {
    std::string relative{};
    std::uintmax_t size{};
    fs::file_time_type modified{};
    sha::Digest digest{};

    /// Orders two stamps by their path.
    bool operator<(const Stamp& other) const { return relative < other.relative; }

    /// Tells whether two stamps are the same bytes written at the same time.
    bool operator==(const Stamp& other) const {
        return relative == other.relative && size == other.size && modified == other.modified &&
               digest == other.digest;
    }
};

/// Reads every file under a folder.
///
/// @param root the folder
/// @return one stamp per file, by relative path
std::vector<Stamp> stamp_tree(const fs::path& root) {
    std::vector<Stamp> stamps;
    std::error_code error;
    for (fs::recursive_directory_iterator
             entry(root, fs::directory_options::skip_permission_denied, error),
         end;
         !error && entry != end;
         entry.increment(error)) {
        std::error_code status;
        if (!entry->is_regular_file(status) || status)
            continue;
        std::ifstream in(entry->path(), std::ios::binary);
        const std::vector<uint8_t> bytes{
            std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}
        };
        Stamp stamp;
        stamp.relative = fs::relative(entry->path(), root, status).generic_string();
        stamp.size = bytes.size();
        stamp.modified = fs::last_write_time(entry->path(), status);
        stamp.digest = sha::digest_of(bytes);
        stamps.push_back(std::move(stamp));
    }
    std::sort(stamps.begin(), stamps.end());
    return stamps;
}

// 1. Record at release 27, catalogue at release 28: one update 27 to 28.
void test_higher_release() {
    Scratch scratch;
    const sha::Digest digest = digest_of("ridge-27");
    place_mod(
        scratch.path(),
        "ridge",
        "Ridge",
        3,
        catalogue_origin("coreprime", "ridge", 27, digest),
        true
    );
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    OA_CHECK(listed.size() == 1);
    if (listed.size() != 1)
        return;
    const content::Match match = content::match_installed(listed[0], content::Snapshot{});
    OA_CHECK(match.provenance == content::Provenance::catalogue);
    OA_CHECK(match.registry == "coreprime" && match.key == "ridge" && match.release == 27);

    Library library;
    library.add(
        "coreprime", true, {mod_release("ridge", 28, "4.9", 3200000, digest_of("ridge-28"))}
    );
    const content::UpdateRules rules = content::this_engine_rules();
    const std::vector<content::Update> updates =
        content::find_updates(listed, library.snapshot, rules);
    OA_CHECK(updates.size() == 1);
    OA_CHECK(content::waiting_updates(updates) == 1);
    if (updates.size() != 1)
        return;
    OA_CHECK(updates[0].installed == 0);
    OA_CHECK(updates[0].from_release == 27 && updates[0].to_release == 28);
    OA_CHECK(updates[0].to_version == "4.9");
    OA_CHECK(updates[0].to_revision == 4);
    OA_CHECK(updates[0].size == 3200000);
    OA_CHECK(updates[0].blocked == content::Blocked::none);
}

// 2. Equal release, and lower release: no update.
void test_equal_and_lower() {
    Scratch scratch;
    const sha::Digest digest = digest_of("held");
    place_mod(
        scratch.path(),
        "equal",
        "Equal",
        1,
        catalogue_origin("coreprime", "equal", 27, digest),
        true
    );
    place_mod(
        scratch.path(),
        "lower",
        "Lower",
        1,
        catalogue_origin("coreprime", "lower", 27, digest),
        true
    );
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    OA_CHECK(listed.size() == 2);
    Library library;
    library.add(
        "coreprime",
        true,
        {mod_release("equal", 27, "4.8", 10, digest), mod_release("lower", 20, "4.0", 10, digest)}
    );
    const std::vector<content::Update> updates =
        content::find_updates(listed, library.snapshot, content::this_engine_rules());
    OA_CHECK(updates.empty());
}

// 3. The same key at a higher release in another registry: no update.
void test_other_registry() {
    Scratch scratch;
    place_mod(
        scratch.path(),
        "ridge",
        "Ridge",
        3,
        catalogue_origin("coreprime", "ridge", 27, digest_of("ridge")),
        true
    );
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    OA_CHECK(listed.size() == 1);
    if (listed.empty())
        return;
    Library library;
    library.add("example", false, {mod_release("ridge", 40, "9.0", 10, digest_of("other"))});
    const content::Match match = content::match_installed(listed[0], library.snapshot);
    OA_CHECK(match.provenance == content::Provenance::catalogue);
    OA_CHECK(match.registry == "coreprime" && match.release == 27);
    const std::vector<content::Update> updates =
        content::find_updates(listed, library.snapshot, content::this_engine_rules());
    OA_CHECK(updates.empty());
}

// 4. File install whose SHA equals the entry: matched. Adopting writes a catalogue record.
void test_file_match_and_adopt() {
    Scratch scratch;
    const sha::Digest digest = digest_of("same-bytes");
    place_mod(scratch.path(), "hand-copy", "Hand Copy", 1, file_origin(digest), true);
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    const content::InstalledPackage* installed = find_key(listed, "hand-copy");
    OA_CHECK(installed != nullptr);
    if (installed == nullptr)
        return;

    Library language_only;
    catalogue::Package language = mod_release("words", 4, "1", 10, digest);
    language.kind = catalogue::Kind::oalang;
    language_only.add("coreprime", true, {language});
    const content::Match wrong_kind = content::match_installed(*installed, language_only.snapshot);
    OA_CHECK(wrong_kind.provenance == content::Provenance::own);

    Library both;
    both.add("example", false, {mod_release("first-key", 3, "1", 10, digest)});
    both.add("coreprime", true, {mod_release("ridge", 28, "4.8", 100, digest)});
    const content::Match match = content::match_installed(*installed, both.snapshot);
    OA_CHECK(match.provenance == content::Provenance::matched);
    OA_CHECK(match.registry == "coreprime" && match.key == "ridge" && match.release == 28);

    Library added_first;
    added_first.add("example", false, {mod_release("from-example", 9, "1", 10, digest)});
    added_first.add("later", false, {mod_release("from-later", 9, "1", 10, digest)});
    const content::Match first = content::match_installed(*installed, added_first.snapshot);
    OA_CHECK(first.provenance == content::Provenance::matched && first.key == "from-example");

    std::string error;
    OA_CHECK(content::adopt_match(*installed, match, &error));
    const auto recorded = install::read_origin(installed->folder);
    OA_CHECK(recorded.has_value());
    if (!recorded)
        return;
    OA_CHECK(recorded->kind == install::OriginKind::catalogue);
    OA_CHECK(recorded->registry == "coreprime" && recorded->catalogue_id == "ridge");
    OA_CHECK(recorded->release == 28 && recorded->sha256 == digest);
    const std::string today = install::utc_date_text(std::chrono::system_clock::now());
    OA_CHECK(recorded->installed == today);

    const std::vector<content::InstalledPackage> again = content::list_installed(scratch.path());
    const content::InstalledPackage* adopted = find_key(again, "hand-copy");
    OA_CHECK(adopted != nullptr);
    if (adopted == nullptr)
        return;
    Library later;
    later.add("coreprime", true, {mod_release("ridge", 29, "4.10", 200, digest_of("newer-bytes"))});
    const std::vector<content::Update> updates =
        content::find_updates(again, later.snapshot, content::this_engine_rules());
    OA_CHECK(updates.size() == 1);
    if (updates.size() != 1)
        return;
    OA_CHECK(updates[0].from_release == 28 && updates[0].to_release == 29);
    OA_CHECK(
        updates[0].key == "ridge" && updates[0].to_version == "4.10" && updates[0].size == 200
    );
}

// 5. File install with an unknown SHA: own, and no update ever.
void test_unknown_sha() {
    Scratch scratch;
    place_mod(scratch.path(), "ridge", "Ridge", 3, file_origin(digest_of("not-listed")), true);
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    OA_CHECK(listed.size() == 1);
    if (listed.empty())
        return;
    Library library;
    library.add("coreprime", true, {mod_release("ridge", 50, "9.0", 10, digest_of("catalogue"))});
    const content::Match match = content::match_installed(listed[0], library.snapshot);
    OA_CHECK(match.provenance == content::Provenance::own);
    OA_CHECK(match.registry.empty() && match.key.empty() && match.release == 0);
    OA_CHECK(content::find_updates(listed, library.snapshot, content::this_engine_rules()).empty());
}

// 6. A mod folder with no record: own.
void test_no_record() {
    Scratch scratch;
    place_mod(scratch.path(), "ridge", "Ridge", 1, std::nullopt, true);
    write_text(
        scratch.path() / "Mods" / ".hidden" / "oamod.yaml",
        profile_text("hidden", "Hidden", 1, true)
    );
    write_text(scratch.path() / "Mods" / "notes.txt", "not a package\n");
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    OA_CHECK(listed.size() == 1);
    if (listed.size() != 1)
        return;
    OA_CHECK(!listed[0].origin.has_value());
    OA_CHECK(listed[0].key == "ridge");
    Library library;
    library.add("coreprime", true, {mod_release("ridge", 30, "5.0", 10, digest_of("catalogue"))});
    const content::Match match = content::match_installed(listed[0], library.snapshot);
    OA_CHECK(match.provenance == content::Provenance::own);
    OA_CHECK(content::find_updates(listed, library.snapshot, content::this_engine_rules()).empty());
}

/// The rules hash list_installed keeps, resolved the same way, for a profile.
///
/// @param text the profile
/// @return the hash; empty when it does not resolve
std::optional<sha::Digest> rules_of(std::string_view text, bool report) {
    const profile::ResolveOptions options{};
    const profile::ResolveResult resolved = profile::resolve_profile(
        {reinterpret_cast<const uint8_t*>(text.data()), text.size()}, "oamod.yaml", options
    );
    if (!resolved.resolution) {
        if (report)
            for (const profile::Diagnostic& diagnostic : resolved.errors)
                std::fprintf(stderr, "%s\n", profile::format_diagnostic(diagnostic).c_str());
        return std::nullopt;
    }
    return resolved.resolution->profile.sim_hash;
}

// 7. Rules: equal hashes, differing hashes, a missing catalogue hash, a profile that does not resolve.
void test_rules() {
    Scratch scratch;
    const std::string same_text = profile_text("same", "Same", 1, true);
    // A sim-scoped hack, so the rules hash is not the one `same` resolves to.
    const std::string changed_text =
        profile_text("changed", "Changed", 1, true, "hacks: {ai.difficulty-names: true}\n");
    const std::string missing_text = profile_text("missing", "Missing", 1, true);
    const std::string broken_text = profile_text("broken", "Broken", 1, false);
    write_text(scratch.path() / "Mods" / "same" / "oamod.yaml", same_text);
    write_text(scratch.path() / "Mods" / "changed" / "oamod.yaml", changed_text);
    write_text(scratch.path() / "Mods" / "missing" / "oamod.yaml", missing_text);
    write_text(scratch.path() / "Mods" / "broken" / "oamod.yaml", broken_text);
    const sha::Digest digest = digest_of("rules");
    for (const char* id : {"same", "changed", "missing", "broken"})
        write_text(
            scratch.path() / "Mods" / id / std::string(install::origin_file_name),
            install::origin_text(catalogue_origin("coreprime", id, 1, digest))
        );

    const std::optional<sha::Digest> same_rules = rules_of(same_text, true);
    const std::optional<sha::Digest> changed_rules = rules_of(changed_text, true);
    OA_CHECK(same_rules.has_value() && changed_rules.has_value());
    OA_CHECK(!rules_of(broken_text, false).has_value());
    if (!same_rules || !changed_rules)
        return;

    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    const content::InstalledPackage* same = find_key(listed, "same");
    const content::InstalledPackage* changed = find_key(listed, "changed");
    const content::InstalledPackage* missing = find_key(listed, "missing");
    const content::InstalledPackage* broken = find_key(listed, "broken");
    OA_CHECK(same && changed && missing && broken);
    if (!same || !changed || !missing || !broken)
        return;
    OA_CHECK(same->rules == same_rules);
    OA_CHECK(changed->rules == changed_rules);
    OA_CHECK(!broken->rules.has_value());

    catalogue::Package same_entry = mod_release("same", 2, "4.9", 10, digest);
    same_entry.sim_hash = same_rules;
    catalogue::Package changed_entry = mod_release("changed", 2, "4.9", 10, digest);
    changed_entry.sim_hash = same_rules;
    OA_CHECK(changed_rules != same_rules);
    catalogue::Package missing_entry = mod_release("missing", 2, "4.9", 10, digest);
    catalogue::Package broken_entry = mod_release("broken", 2, "4.9", 10, digest);
    broken_entry.sim_hash = same_rules;
    Library library;
    library.add("coreprime", true, {same_entry, changed_entry, missing_entry, broken_entry});
    const std::vector<content::Update> updates =
        content::find_updates(listed, library.snapshot, content::this_engine_rules());
    const content::Update* same_update = find_update(updates, "same");
    const content::Update* changed_update = find_update(updates, "changed");
    const content::Update* missing_update = find_update(updates, "missing");
    const content::Update* broken_update = find_update(updates, "broken");
    OA_CHECK(same_update && changed_update && missing_update && broken_update);
    if (!same_update || !changed_update || !missing_update || !broken_update)
        return;
    OA_CHECK(same_update->rules == content::RulesChange::same);
    OA_CHECK(same_update->from_rules == same_rules && same_update->to_rules == same_rules);
    OA_CHECK(changed_update->rules == content::RulesChange::changes);
    OA_CHECK(missing_update->rules == content::RulesChange::unknown && !missing_update->to_rules);
    OA_CHECK(broken_update->rules == content::RulesChange::unknown && !broken_update->from_rules);
}

// 8. Blocked: an engine requirement, another base, and a hack this build does not implement.
void test_blocked() {
    Scratch scratch;
    const sha::Digest digest = digest_of("blocked");
    place_mod(
        scratch.path(),
        "by-engine",
        "Zulu",
        1,
        catalogue_origin("coreprime", "by-engine", 1, digest),
        true
    );
    place_mod(
        scratch.path(),
        "by-base",
        "alpha",
        1,
        catalogue_origin("coreprime", "by-base", 1, digest),
        true
    );
    place_mod(
        scratch.path(),
        "by-hack",
        "Mike",
        1,
        catalogue_origin("coreprime", "by-hack", 1, digest),
        true
    );
    place_mod(
        scratch.path(), "open", "bravo", 1, catalogue_origin("coreprime", "open", 1, digest), true
    );

    catalogue::Package engine = mod_release("by-engine", 2, "1", 10, digest);
    const auto range = oa::formats::oamod::parse_engine_range(">= 9999.0.0");
    OA_CHECK(range.has_value());
    engine.requires_engine = range;
    catalogue::Package base = mod_release("by-base", 2, "1", 10, digest);
    base.requires_base = "other";
    catalogue::Package hacks = mod_release("by-hack", 2, "1", 10, digest);
    hacks.hack_ids = {"no-such-hack", "also-missing", "third-missing", "fourth-missing"};
    catalogue::Package open = mod_release("open", 2, "1", 11, digest);

    Library library;
    library.add("coreprime", true, {engine, base, std::move(hacks), open});
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    const std::vector<content::Update> updates =
        content::find_updates(listed, library.snapshot, content::this_engine_rules());
    OA_CHECK(updates.size() == 4);
    OA_CHECK(content::waiting_updates(updates) == 1);
    if (updates.size() != 4)
        return;
    OA_CHECK(updates[0].key == "by-base");
    OA_CHECK(updates[1].key == "open");
    OA_CHECK(updates[2].key == "by-hack");
    OA_CHECK(updates[3].key == "by-engine");

    const content::Update* engine_update = find_update(updates, "by-engine");
    const content::Update* base_update = find_update(updates, "by-base");
    const content::Update* hack_update = find_update(updates, "by-hack");
    OA_CHECK(engine_update && base_update && hack_update);
    if (!engine_update || !base_update || !hack_update)
        return;
    OA_CHECK(engine_update->blocked == content::Blocked::engine);
    OA_CHECK(engine_update->blocked_detail == "9999.0.0 or later");
    OA_CHECK(base_update->blocked == content::Blocked::base);
    OA_CHECK(base_update->blocked_detail == "other");
    OA_CHECK(hack_update->blocked == content::Blocked::hacks);
    OA_CHECK(hack_update->blocked_detail == "no-such-hack, also-missing, third-missing and 1 more");

    // A language pack sorts after a mod, whatever their names.
    install::PackageKind language = install::mod_kind();
    language.name = "oalang";
    content::InstalledPackage language_pack;
    language_pack.kind = &language;
    language_pack.key = "words";
    language_pack.name = "Aaa";
    language_pack.origin = catalogue_origin("coreprime", "words", 1, digest);
    content::InstalledPackage mod;
    mod.kind = &install::mod_kind();
    mod.key = "ridge";
    mod.name = "Zzz";
    mod.origin = catalogue_origin("coreprime", "ridge", 1, digest);
    const content::InstalledPackage packs[] = {language_pack, mod};
    catalogue::Package words = mod_release("words", 2, "1", 10, digest);
    words.kind = catalogue::Kind::oalang;
    Library ordered;
    ordered.add("coreprime", true, {words, mod_release("ridge", 2, "1", 10, digest)});
    const std::vector<content::Update> ordered_updates =
        content::find_updates(packs, ordered.snapshot, content::this_engine_rules());
    OA_CHECK(ordered_updates.size() == 2);
    if (ordered_updates.size() == 2) {
        OA_CHECK(ordered_updates[0].key == "ridge" && ordered_updates[1].key == "words");
        OA_CHECK(ordered_updates[1].rules == content::RulesChange::same);
    }
}

// 9. Roll back restores the older origin record, so the update is offered again.
void test_roll_back() {
    Scratch scratch;
    const fs::path incoming = scratch.path() / "incoming";
    const fs::path mods = scratch.path() / "Mods";
    fs::create_directories(incoming);
    const fs::path first = write_package(incoming, 3);
    const fs::path second = write_package(incoming, 4);
    const fs::path folder = mods / "ridge";

    install::Origin release_27;
    release_27.kind = install::OriginKind::catalogue;
    release_27.registry = "coreprime";
    release_27.catalogue_id = "ridge";
    release_27.release = 27;
    release_27.installed = "2026-10-01";
    install::Origin release_28 = release_27;
    release_28.release = 28;

    OA_CHECK(install_package(first, mods, Change::install, release_27));
    const auto installed = install::read_origin(folder);
    OA_CHECK(
        installed && installed->release == 27 && installed->kind == install::OriginKind::catalogue
    );

    OA_CHECK(install_package(second, mods, Change::replace, release_28));
    const auto replaced = install::read_origin(folder);
    const auto kept = install::read_origin(folder / std::string(install::backup_folder_name));
    OA_CHECK(replaced && replaced->release == 28);
    OA_CHECK(kept && kept->release == 27);

    Library library;
    library.add("coreprime", true, {mod_release("ridge", 28, "4.8", 64, digest_of("ridge-28"))});
    const content::UpdateRules rules = content::this_engine_rules();
    const std::vector<content::InstalledPackage> at_28 = content::list_installed(scratch.path());
    OA_CHECK(content::find_updates(at_28, library.snapshot, rules).empty());

    install::ChangeOptions options;
    options.first_retry_ms = 0;
    const install::ChangeResult back =
        install::commit_change(install::mod_kind(), mods, "ridge", Change::roll_back, options);
    OA_CHECK(back.changed);
    const std::vector<content::InstalledPackage> at_27 = content::list_installed(scratch.path());
    OA_CHECK(at_27.size() == 1);
    if (at_27.size() == 1) {
        OA_CHECK(at_27[0].origin && at_27[0].origin->release == 27);
        OA_CHECK(at_27[0].has_backup);
    }
    const std::vector<content::Update> offered =
        content::find_updates(at_27, library.snapshot, rules);
    OA_CHECK(offered.size() == 1);
    if (offered.size() == 1)
        OA_CHECK(offered[0].from_release == 27 && offered[0].to_release == 28);

    const install::ChangeResult forward =
        install::commit_change(install::mod_kind(), mods, "ridge", Change::roll_back, options);
    OA_CHECK(forward.changed);
    const std::vector<content::InstalledPackage> restored = content::list_installed(scratch.path());
    OA_CHECK(restored.size() == 1 && restored[0].origin && restored[0].origin->release == 28);
    OA_CHECK(content::find_updates(restored, library.snapshot, rules).empty());
}

// 10. list_installed changes no file's bytes or modification time.
void test_list_changes_nothing() {
    Scratch scratch;
    place_mod(
        scratch.path(),
        "ridge",
        "Ridge",
        3,
        catalogue_origin("coreprime", "ridge", 27, digest_of("ridge")),
        true
    );
    write_text(scratch.path() / "Mods" / "ridge" / "units" / "unit.fbi", "untouched\n");
    const std::vector<Stamp> before = stamp_tree(scratch.path());
    OA_CHECK(!before.empty());
    const std::vector<content::InstalledPackage> listed = content::list_installed(scratch.path());
    OA_CHECK(listed.size() == 1);
    const std::vector<content::InstalledPackage> again = content::list_installed(scratch.path());
    OA_CHECK(again.size() == 1);
    const std::vector<Stamp> after = stamp_tree(scratch.path());
    OA_CHECK(before == after);
}

} // namespace

int main() {
    test_higher_release();
    test_equal_and_lower();
    test_other_registry();
    test_file_match_and_adopt();
    test_unknown_sha();
    test_no_record();
    test_rules();
    test_blocked();
    test_roll_back();
    test_list_changes_nothing();
    return oa::test::check_exit_status();
}
