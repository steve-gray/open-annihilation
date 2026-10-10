// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Matching installed packages to catalogue releases. The comparison is the
// release number recorded for the package: a higher release of the same
// registry and key is an update. A file the player installed is offered one
// only after its SHA-256 is a release a registry lists.

#include "oa/app/content/updates.hpp"

#include "oa/app/user_folder.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/mod_profile.hpp"
#include "oa/data/mod_profile/registry.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/formats/oamod.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>

#ifndef OA_ENGINE_VERSION
#error                                                                                             \
    "OA_ENGINE_VERSION names this build's release, which an update's engine requirement is checked against"
#endif

namespace oa::app::content {

namespace fs = std::filesystem;
namespace install = package_install;
namespace catalogue = data::catalogue;
namespace profile = data::mod_profile;

namespace {

/// A manifest's rules hash, remembered with the size and time it was read at.
struct RulesKey {
    fs::path folder{};
    uintmax_t size{};
    fs::file_time_type modified{};

    /// Orders two keys so a map can hold them.
    bool operator<(const RulesKey& other) const {
        if (folder != other.folder)
            return folder < other.folder;
        if (size != other.size)
            return size < other.size;
        return modified < other.modified;
    }
};

/// Returns a path's UTF-8 spelling, or empty when it cannot be converted.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8_of(const fs::path& path) {
    try {
        const auto text = path.u8string();
        return {text.begin(), text.end()};
    } catch (const std::exception&) {
        return {};
    }
}

/// Returns a text with its ASCII letters lowered.
///
/// @param text the text
/// @return the text
std::string folded(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered;
}

/// Tells whether a kind is a mod, the only kind whose rules hash matters.
///
/// @param kind the kind; null is not a mod
/// @return true for a mod
bool kind_has_rules(const install::PackageKind* kind) noexcept {
    return kind != nullptr && kind->name == "oamod";
}

/// The place of a kind in the update list: mods, map packs, language packs.
///
/// @param kind the kind; null sorts last
/// @return 0, 1, 2, or 3
int kind_rank(const install::PackageKind* kind) noexcept {
    if (kind == nullptr)
        return 3;
    if (kind->name == "oamod")
        return 0;
    if (kind->name == "oamap")
        return 1;
    if (kind->name == "oalang")
        return 2;
    return 3;
}

/// Tells whether an installed kind and a catalogue kind are the same.
///
/// @param kind the installed kind
/// @param listed the catalogue kind
/// @return true when their names agree
bool same_kind(const install::PackageKind* kind, catalogue::Kind listed) noexcept {
    return kind != nullptr && kind->name == catalogue::kind_name(listed);
}

/// Reads a manifest, refusing one larger than a profile may be.
///
/// @param file the manifest
/// @param[out] bytes the bytes; empty when the file is too large
/// @return true when the file was read, including when it is too large to use
bool read_manifest(const fs::path& file, std::vector<uint8_t>& bytes) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::array<char, 4096> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count <= 0)
            break;
        if (bytes.size() + static_cast<std::size_t>(count) > formats::oamod::max_input_bytes) {
            bytes.clear();
            return true;
        }
        bytes.insert(
            bytes.end(),
            reinterpret_cast<const uint8_t*>(buffer.data()),
            reinterpret_cast<const uint8_t*>(buffer.data()) + count
        );
    }
    return static_cast<bool>(in) || in.eof();
}

/// The rules hash of a mod folder, from the cache when the manifest is unchanged.
///
/// The map is function-local and guarded: a refresh resolves a mod again
/// only after its manifest's size or modification time changes. The resolve
/// uses a default-constructed options value, so player settings and
/// Developer Mode overrides are not part of the hash.
///
/// @param folder the mod folder
/// @return the hash; empty when the profile does not resolve
std::optional<base::sha256::Digest> cached_rules(const fs::path& folder) {
    static base::threads::Mutex mutex;
    static std::map<RulesKey, std::optional<base::sha256::Digest>> cache;

    const auto manifest = oa::app::entry_without_case(folder, "oamod.yaml");
    if (!manifest)
        return std::nullopt;
    std::error_code error;
    const auto size = fs::file_size(*manifest, error);
    if (error)
        return std::nullopt;
    const auto modified = fs::last_write_time(*manifest, error);
    if (error)
        return std::nullopt;
    const RulesKey key{folder, size, modified};
    {
        const base::threads::LockGuard guard(mutex);
        const auto found = cache.find(key);
        if (found != cache.end())
            return found->second;
    }

    std::vector<uint8_t> bytes;
    if (!read_manifest(*manifest, bytes))
        return std::nullopt;
    std::optional<base::sha256::Digest> rules;
    if (!bytes.empty()) {
        const profile::ResolveOptions options{};
        const profile::ResolveResult resolved =
            profile::resolve_profile(bytes, "oamod.yaml", options);
        if (resolved.resolution)
            rules = resolved.resolution->profile.sim_hash;
    }

    std::error_code after;
    const auto size_after = fs::file_size(*manifest, after);
    const auto modified_after =
        after ? fs::file_time_type{} : fs::last_write_time(*manifest, after);
    if (after || size_after != size || modified_after != modified)
        return rules;
    const base::threads::LockGuard guard(mutex);
    cache.insert_or_assign(RulesKey{folder, size_after, modified_after}, rules);
    return rules;
}

/// Writes a reason and returns false.
///
/// @param error the caller's reason; may be null
/// @param why the reason
/// @return false
bool refuse(std::string* error, std::string why) {
    if (error != nullptr)
        *error = std::move(why);
    return false;
}

/// The enabled entry a file install's SHA-256 equals, if one lists those bytes.
///
/// A built-in registry wins over an added one. Two of the same standing
/// keep the one listed first. A different kind is not the same package.
///
/// @param installed the package
/// @param digest the file's SHA-256
/// @param snapshot the catalogues in effect
/// @return the entry; null when none lists those bytes
const Entry* file_match(
    const InstalledPackage& installed, const base::sha256::Digest& digest, const Snapshot& snapshot
) {
    const Entry* chosen = nullptr;
    for (const Entry& entry : snapshot.entries) {
        if (entry.package == nullptr || entry.package->sha256 != digest)
            continue;
        if (!same_kind(installed.kind, entry.package->kind))
            continue;
        if (chosen == nullptr || (entry.reviewed && !chosen->reviewed))
            chosen = &entry;
    }
    return chosen;
}

/// Why an update cannot be installed, and the words that say so.
struct Block {
    Blocked blocked{Blocked::none};
    std::string detail{};
};

/// The first reason this build cannot install a release, in engine, base, hack order.
///
/// @param package the catalogue release
/// @param rules what this build can install
/// @return the reason; none when it can
Block block_for(const catalogue::Package& package, const UpdateRules& rules) {
    const formats::oamod::EngineRange* range = nullptr;
    std::optional<formats::oamod::EngineRange> parsed;
    if (package.requires_engine) {
        range = &*package.requires_engine;
    } else if (!package.requires_engine_text.empty()) {
        parsed = formats::oamod::parse_engine_range(package.requires_engine_text);
        if (parsed)
            range = &*parsed;
    }
    if (range != nullptr && !formats::oamod::engine_range_met(*range, rules.engine)) {
        return {Blocked::engine, formats::oamod::describe_engine_range(*range)};
    }
    if (!package.requires_base.empty() && package.requires_base != "ta-3.1c")
        return {Blocked::base, package.requires_base};

    std::vector<std::string> missing;
    for (const std::string& id : package.hack_ids) {
        const bool carried_out =
            static_cast<bool>(rules.hack_implemented) && rules.hack_implemented(id);
        if (!carried_out)
            missing.push_back(id);
    }
    if (missing.empty())
        return {};
    std::string detail;
    const std::size_t shown = std::min<std::size_t>(missing.size(), 3);
    for (std::size_t index = 0; index < shown; ++index) {
        if (index != 0)
            detail += ", ";
        detail += missing[index];
    }
    if (missing.size() > 3) {
        detail += " and ";
        detail += std::to_string(missing.size() - 3);
        detail += " more";
    }
    return {Blocked::hacks, std::move(detail)};
}

/// The catalogue release an update would install, when one is newer.
///
/// @param installed the package
/// @param match where it came from
/// @param snapshot the catalogues in effect
/// @return the newest higher release of that registry, kind and key; null for none
const Entry*
newer_release(const InstalledPackage& installed, const Match& match, const Snapshot& snapshot) {
    const Entry* newer = nullptr;
    for (const Entry& entry : snapshot.entries) {
        if (entry.package == nullptr || entry.registry != match.registry)
            continue;
        if (entry.package->id != match.key || !same_kind(installed.kind, entry.package->kind))
            continue;
        if (entry.package->release <= match.release)
            continue;
        if (newer == nullptr || entry.package->release > newer->package->release)
            newer = &entry;
    }
    return newer;
}

} // namespace

UpdateRules this_engine_rules() {
    UpdateRules rules;
    if (const std::optional<formats::oamod::EngineVersion> parsed =
            formats::oamod::parse_engine_version(OA_ENGINE_VERSION))
        rules.engine = *parsed;
    rules.hack_implemented = [](std::string_view id) {
        const profile::registry::Entry* entry = profile::registry::find_entry(id);
        return entry != nullptr && entry->implemented;
    };
    return rules;
}

std::vector<InstalledPackage> list_installed(const fs::path& user_folder) {
    std::vector<InstalledPackage> listed;
    for (const install::PackageKind& kind : install::package_kinds()) {
        if (kind.read_installed == nullptr || kind.root_folder.empty())
            continue;
        const fs::path root = user_folder / std::string(kind.root_folder);
        std::error_code error;
        if (!fs::is_directory(root, error) || error)
            continue;
        for (fs::directory_iterator
                 entry(root, fs::directory_options::skip_permission_denied, error),
             end;
             !error && entry != end;
             entry.increment(error)) {
            const std::string name = utf8_of(entry->path().filename());
            if (name.empty() || name.front() == '.')
                continue;
            std::error_code status;
            if (!entry->is_directory(status) || status)
                continue;
            const install::InstalledPackage held = kind.read_installed(entry->path());
            if (held.kind != install::FolderKind::package)
                continue;
            InstalledPackage package;
            package.kind = &kind;
            package.key = held.id;
            package.folder = entry->path();
            package.name = held.name;
            package.version = held.version;
            package.revision = held.revision;
            package.origin = install::read_origin(entry->path());
            if (kind.read_backup != nullptr) {
                if (const std::optional<install::InstalledPackage> backup =
                        kind.read_backup(entry->path())) {
                    package.has_backup = true;
                    package.backup_version = backup->version;
                    package.backup_revision = backup->revision;
                }
            }
            if (kind_has_rules(&kind))
                package.rules = cached_rules(entry->path());
            listed.push_back(std::move(package));
        }
    }
    return listed;
}

Match match_installed(const InstalledPackage& installed, const Snapshot& snapshot) {
    if (installed.origin && installed.origin->kind == install::OriginKind::catalogue) {
        Match match;
        match.provenance = Provenance::catalogue;
        match.registry = installed.origin->registry;
        match.key = installed.origin->catalogue_id;
        match.release = installed.origin->release;
        return match;
    }
    if (installed.origin && installed.origin->kind == install::OriginKind::file &&
        installed.origin->sha256) {
        if (const Entry* entry = file_match(installed, *installed.origin->sha256, snapshot)) {
            Match match;
            match.provenance = Provenance::matched;
            match.registry = entry->registry;
            match.key = entry->package->id;
            match.release = entry->package->release;
            return match;
        }
    }
    return {};
}

bool adopt_match(const InstalledPackage& installed, const Match& match, std::string* error) {
    if (match.provenance != Provenance::matched)
        return refuse(error, "the package was not matched to a catalogue release");
    if (installed.kind == nullptr)
        return refuse(error, "the package has no kind");
    if (!installed.origin || !installed.origin->sha256)
        return refuse(error, "the package has no SHA-256");
    if (!data::registry::valid_registry_id(match.registry) || match.key.empty() ||
        match.key.find('@') != std::string::npos || match.release < 1 || match.release > 2147483647)
        return refuse(error, "the catalogue release cannot be recorded");
    const std::string target = utf8_of(installed.folder.filename());
    if (target.empty() || target == "." || target == ".." ||
        target.find('/') != std::string::npos || target.find('\\') != std::string::npos)
        return refuse(error, "the package folder has no name");

    install::Origin origin;
    origin.kind = install::OriginKind::catalogue;
    origin.registry = match.registry;
    origin.catalogue_id = match.key;
    origin.release = match.release;
    origin.sha256 = installed.origin->sha256;
    origin.installed = install::utc_date_text(std::chrono::system_clock::now());
    return install::write_origin(
        *installed.kind, installed.folder.parent_path(), target, origin, error
    );
}

std::vector<Update> find_updates(
    std::span<const InstalledPackage> installed, const Snapshot& snapshot, const UpdateRules& rules
) {
    std::vector<Update> updates;
    for (std::size_t index = 0; index < installed.size(); ++index) {
        const InstalledPackage& package = installed[index];
        const Match match = match_installed(package, snapshot);
        if (match.provenance == Provenance::own)
            continue;
        const Entry* newer = newer_release(package, match, snapshot);
        if (newer == nullptr || newer->package == nullptr)
            continue;
        Update update;
        update.installed = index;
        update.registry = match.registry;
        update.key = match.key;
        update.from_release = match.release;
        update.to_release = newer->package->release;
        update.to_version = newer->package->version;
        update.to_revision = newer->package->revision;
        update.size = newer->package->size;
        if (kind_has_rules(package.kind)) {
            update.from_rules = package.rules;
            update.to_rules = newer->package->sim_hash;
            if (update.from_rules && update.to_rules && *update.from_rules == *update.to_rules)
                update.rules = RulesChange::same;
            else if (update.from_rules && update.to_rules)
                update.rules = RulesChange::changes;
            else
                update.rules = RulesChange::unknown;
        } else {
            update.rules = RulesChange::same;
        }
        const Block block = block_for(*newer->package, rules);
        update.blocked = block.blocked;
        update.blocked_detail = block.detail;
        updates.push_back(std::move(update));
    }
    std::stable_sort(updates.begin(), updates.end(), [&](const Update& left, const Update& right) {
        const InstalledPackage& left_package = installed[left.installed];
        const InstalledPackage& right_package = installed[right.installed];
        const int left_rank = kind_rank(left_package.kind);
        const int right_rank = kind_rank(right_package.kind);
        if (left_rank != right_rank)
            return left_rank < right_rank;
        return folded(left_package.name) < folded(right_package.name);
    });
    return updates;
}

std::size_t waiting_updates(std::span<const Update> updates) {
    std::size_t waiting = 0;
    for (const Update& update : updates)
        if (update.blocked == Blocked::none)
            ++waiting;
    return waiting;
}

} // namespace oa::app::content
