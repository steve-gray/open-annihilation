// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The oamap kind's own rules: its manifest in package use, the files a pack
// may hold, its previews, the engine it needs, what a folder of Maps holds,
// and where a map pack installs.

#include "files.hpp"

#include "oa/app/package_install/oamap.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/formats/oamod/package_keys.hpp"
#include "oa/formats/png.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef OA_ENGINE_VERSION
#error                                                                                             \
    "OA_ENGINE_VERSION names this build's version, which a map pack's engine requirement is checked against"
#endif

namespace oa::app::package_install::oamap {

namespace fs = std::filesystem;
namespace pack = oa::data::map_pack;
namespace png = oa::formats::png;
namespace keys = oa::formats::oamod;

namespace {

/// The most pixels on one side of a preview.
constexpr uint32_t most_preview_side = 1024;

/// The release this build is, parsed once from OA_ENGINE_VERSION.
///
/// @return that release; 0.0.0 when the macro is not a version
keys::EngineVersion this_build_version() {
    static const keys::EngineVersion version = [] {
        if (const std::optional<keys::EngineVersion> parsed =
                keys::parse_engine_version(OA_ENGINE_VERSION))
            return *parsed;
        return keys::EngineVersion{};
    }();
    return version;
}

/// Returns a problem.
///
/// @param refusal why
/// @param subject what it names
/// @param detail English, for the log and, where it is the whole sentence, the player
/// @return the problem
Problem problem_of(Refusal refusal, std::string subject = {}, std::string detail = {}) {
    Problem problem{};
    problem.refusal = refusal;
    problem.subject = std::move(subject);
    problem.detail = std::move(detail);
    return problem;
}

/// Tells whether a folder holds the incoming pack, at any version.
///
/// @param held what the folder holds
/// @param incoming the pack
/// @return true when it holds that id
bool holds(const InstalledPackage& held, const Incoming& incoming) noexcept {
    return held.kind == FolderKind::package && held.id == incoming.id;
}

/// Returns what a folder holds, through the hooks.
///
/// @param hooks the hooks
/// @param folder the folder's name
/// @return what it holds; missing without a hook
InstalledPackage look(const FolderHooks& hooks, std::string_view folder) {
    return hooks.look != nullptr ? hooks.look(hooks.context, folder) : InstalledPackage{};
}

/// Returns what a folder's .backup holds, through the hooks.
///
/// @param hooks the hooks
/// @param folder the folder's name
/// @return what it holds; nothing without a hook
std::optional<InstalledPackage> backup_of(const FolderHooks& hooks, std::string_view folder) {
    return hooks.backup_of != nullptr ? hooks.backup_of(hooks.context, folder) : std::nullopt;
}

/// Plans an update or a reinstall of a folder that holds the pack at the
/// incoming version.
///
/// @param hooks the hooks
/// @param incoming the pack
/// @param folder the folder
/// @param held what it holds
/// @return the plan
InstallPlan same_version(
    const FolderHooks& hooks,
    const Incoming& incoming,
    const std::string& folder,
    const InstalledPackage& held
) {
    InstallPlan plan{};
    plan.kind = held.revision == incoming.revision && held.revision != 0 ? PlanKind::ask_reinstall
                                                                         : PlanKind::ask_update;
    plan.target = folder;
    plan.installed = held;
    plan.backup = backup_of(hooks, folder);
    plan.older = plan.kind == PlanKind::ask_update && held.revision != 0 &&
                 incoming.revision < held.revision;
    return plan;
}

/// Tells whether a packaged name is a folder.
///
/// @param name the name
/// @return true when it ends in '/'
bool directory_name(std::string_view name) noexcept {
    return !name.empty() && name.back() == '/';
}

/// Tells whether a file sits outside the folders a map pack may hold.
///
/// @param name the file's name, below the package's top
/// @return true for a top-level file or a first folder other than maps,
///         features, anims, objects3d and previews
bool outside_folders(std::string_view name) {
    const auto slash = name.find('/');
    if (slash == std::string_view::npos)
        return true;
    const std::string top = detail::folded(name.substr(0, slash));
    return top != "maps" && top != "features" && top != "anims" && top != "objects3d" &&
           top != "previews";
}

/// Reads a folder's oamap.yaml, in package use.
///
/// @param folder the folder
/// @param[out] manifest the manifest, when it was read
/// @return true when the folder holds a readable manifest
bool read_folder_manifest(const fs::path& folder, pack::Manifest& manifest) {
    const auto found = entry_without_case(folder, pack::manifest_file);
    if (!found)
        return false;
    std::ifstream in(*found, std::ios::binary);
    if (!in)
        return false;
    std::vector<uint8_t> bytes;
    for (std::istreambuf_iterator<char> at{in}, end;
         at != end && bytes.size() <= oa::formats::oamod::max_input_bytes;
         ++at)
        bytes.push_back(static_cast<uint8_t>(*at));
    if (bytes.size() > oa::formats::oamod::max_input_bytes)
        return false;
    std::vector<pack::Problem> problems;
    return pack::read_manifest(bytes, pack::ManifestUse::package, manifest, problems);
}

/// Fills what a folder holds from a manifest it gave.
///
/// @param manifest the manifest
/// @return the folder, of kind package
InstalledPackage installed_of(const pack::Manifest& manifest) {
    InstalledPackage installed{};
    installed.kind = FolderKind::package;
    installed.id = manifest.id;
    installed.name = manifest.name;
    installed.version = manifest.version;
    installed.revision = manifest.packaging.revision;
    return installed;
}

/// The sentence for an engine requirement this build does not meet.
///
/// @param range the requirement
/// @return M01's wording
std::string unmet_engine(const keys::EngineRange& range) {
    return "needs Open Annihilation " + keys::describe_engine_range(range) +
           "; this is Open Annihilation " + keys::engine_version_text(this_build_version());
}

} // namespace

bool read_manifest(
    std::span<const uint8_t> manifest_bytes,
    const ManifestContext& context,
    Package& package,
    Problem& problem
) {
    pack::Manifest manifest;
    std::vector<pack::Problem> problems;
    if (!pack::read_manifest(manifest_bytes, pack::ManifestUse::package, manifest, problems)) {
        problem = problem_of(Refusal::manifest_errors);
        for (const pack::Problem& one : problems)
            problem.lines.push_back(pack::describe(one));
        return false;
    }
    if (windows_device_name(manifest.id)) {
        problem = problem_of(Refusal::reserved_id, manifest.id);
        return false;
    }
    package.incoming.id = manifest.id;
    package.incoming.name = manifest.name;
    package.incoming.version = manifest.version;
    package.incoming.revision = manifest.packaging.revision;
    package.incoming.maps = static_cast<uint32_t>(manifest.maps.size());

    std::set<std::string> held;
    for (const PackagedFile& file : package.files) {
        if (directory_name(file.name))
            continue;
        held.insert(detail::folded(file.name));
    }
    for (const pack::MapEntry& map : manifest.maps) {
        for (const std::string& name : map.files)
            if (!held.contains(detail::folded(name))) {
                problem = problem_of(Refusal::missing_entry, name);
                return false;
            }
        if (!map.preview.empty() && !held.contains(detail::folded(map.preview))) {
            problem = problem_of(Refusal::missing_entry, map.preview);
            return false;
        }
    }

    std::set<std::string> used;
    for (const pack::MapEntry& map : manifest.maps) {
        for (const std::string& name : map.files)
            used.insert(detail::folded(name));
        if (!map.preview.empty())
            used.insert(detail::folded(map.preview));
    }
    for (const PackagedFile& file : package.files) {
        if (directory_name(file.name) || detail::folded(file.name) == pack::manifest_file)
            continue;
        if (outside_folders(file.name)) {
            problem = problem_of(Refusal::outside_folder, file.name);
            return false;
        }
        if (!used.contains(detail::folded(file.name))) {
            problem = problem_of(Refusal::stray_file, file.name);
            return false;
        }
    }

    for (const pack::MapEntry& map : manifest.maps) {
        if (map.preview.empty())
            continue;
        if (context.read_entry == nullptr) {
            problem = problem_of(Refusal::preview_unreadable, map.preview);
            return false;
        }
        std::vector<uint8_t> bytes;
        const EntryRead read =
            context.read_entry(context.reader, map.preview, most_preview_bytes, bytes);
        if (read == EntryRead::missing) {
            problem = problem_of(Refusal::missing_entry, map.preview);
            return false;
        }
        if (read != EntryRead::read) {
            problem = problem_of(
                Refusal::preview_too_big,
                map.preview,
                "The preview " + map.preview + " is larger than 2 MiB."
            );
            return false;
        }
        if (!png::has_signature(bytes)) {
            problem = problem_of(Refusal::preview_unreadable, map.preview);
            return false;
        }
        png::Info info{};
        if (!png::read_info(bytes, png::Messages{}, &info)) {
            problem = problem_of(Refusal::preview_unreadable, map.preview);
            return false;
        }
        if (info.header.width > most_preview_side || info.header.height > most_preview_side) {
            problem = problem_of(
                Refusal::preview_too_big,
                map.preview,
                "The preview " + map.preview + " is larger than 1024 pixels on a side."
            );
            return false;
        }
    }

    if (!manifest.requires_engine.empty()) {
        const std::optional<keys::EngineRange> range =
            keys::parse_engine_range(manifest.requires_engine);
        if (!range || !keys::engine_range_met(*range, this_build_version())) {
            problem = problem_of(
                Refusal::engine_unmet,
                manifest.requires_engine,
                range ? unmet_engine(*range) : std::string("Its engine requirement cannot be read.")
            );
            return false;
        }
    }
    return true;
}

InstalledPackage read_installed(const fs::path& folder) {
    InstalledPackage installed{};
    try {
        std::error_code error;
        const fs::file_status status = fs::symlink_status(folder, error);
        if (error || !fs::exists(status))
            return installed;
        installed.kind = FolderKind::other;
        if (is_link_or_junction(folder) || !fs::is_directory(status))
            return installed;
        pack::Manifest manifest;
        if (!read_folder_manifest(folder, manifest) || manifest.id.empty())
            return installed;
        return installed_of(manifest);
    } catch (const std::exception&) {
        return installed;
    }
}

std::optional<InstalledPackage> read_backup(const fs::path& folder) {
    try {
        const auto kept = entry_without_case(folder, backup_folder_name);
        if (!kept)
            return std::nullopt;
        InstalledPackage backup = read_installed(*kept);
        if (backup.kind == FolderKind::missing)
            return std::nullopt;
        const InstalledPackage own = read_installed(folder);
        if (backup.kind == FolderKind::package &&
            (own.kind != FolderKind::package || own.id != backup.id))
            backup.kind = FolderKind::other;
        return backup;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

InstallPlan plan_install(const Incoming& incoming, const FolderHooks& hooks) {
    const std::string own = incoming.id;
    const InstalledPackage at_own = look(hooks, own);
    if (at_own.kind == FolderKind::missing) {
        InstallPlan plan{};
        plan.target = own;
        // A file the player opened asks before a new folder is made. The
        // Install button answers alongside, which the runtime installs into
        // `alongside`; that folder is this id, and it is missing.
        if (incoming.origin.kind == OriginKind::catalogue) {
            plan.kind = PlanKind::install;
            return plan;
        }
        plan.kind = PlanKind::ask_install;
        plan.alongside = own;
        return plan;
    }
    if (!holds(at_own, incoming)) {
        InstallPlan plan{};
        plan.kind = PlanKind::refuse;
        plan.refusal = Refusal::not_a_pack;
        plan.target = own;
        plan.installed = at_own;
        return plan;
    }
    const bool incoming_catalogue = incoming.origin.kind == OriginKind::catalogue;
    const bool held_catalogue =
        at_own.origin.has_value() && at_own.origin->kind == OriginKind::catalogue;
    if (incoming_catalogue && held_catalogue &&
        at_own.origin->registry == incoming.origin.registry) {
        InstallPlan plan{};
        plan.kind = PlanKind::install;
        plan.replacing = true;
        plan.target = own;
        plan.installed = at_own;
        plan.backup = backup_of(hooks, own);
        return plan;
    }
    if (incoming_catalogue && held_catalogue) {
        InstallPlan plan{};
        plan.kind = PlanKind::ask_version;
        plan.target = own;
        plan.installed = at_own;
        plan.backup = backup_of(hooks, own);
        return plan;
    }
    if (at_own.version == incoming.version)
        return same_version(hooks, incoming, own, at_own);
    InstallPlan plan{};
    plan.kind = PlanKind::ask_version;
    plan.target = own;
    plan.installed = at_own;
    plan.backup = backup_of(hooks, own);
    return plan;
}

bool check_staged(
    const fs::path& staging, const Package&, const PackageOptions& options, Problem& problem
) {
    const auto* hooks = static_cast<const FitHooks*>(options.context);
    if (hooks == nullptr || hooks->check_base_fit == nullptr)
        return true;
    try {
        pack::Manifest manifest;
        const auto found = entry_without_case(staging, pack::manifest_file);
        std::vector<uint8_t> bytes;
        if (found) {
            std::ifstream in(*found, std::ios::binary);
            if (in)
                for (std::istreambuf_iterator<char> at{in}, end;
                     at != end && bytes.size() <= oa::formats::oamod::max_input_bytes;
                     ++at)
                    bytes.push_back(static_cast<uint8_t>(*at));
        }
        std::vector<pack::Problem> problems;
        if (!found || bytes.size() > oa::formats::oamod::max_input_bytes ||
            !pack::read_manifest(bytes, pack::ManifestUse::package, manifest, problems)) {
            problem = problem_of(Refusal::manifest_errors);
            for (const pack::Problem& one : problems)
                problem.lines.push_back(pack::describe(one));
            if (problem.lines.empty())
                problem.lines.push_back("oamap.yaml:1:1: oamap.yaml: it cannot be read");
            return false;
        }
        std::vector<oa::data::map_fit::Failure> failures;
        for (const pack::MapEntry& map : manifest.maps) {
            const oa::data::map_fit::Fit fit =
                hooks->check_base_fit(hooks->context, staging, map, manifest.id);
            for (const oa::data::map_fit::Failure& failure : fit.failures)
                failures.push_back(failure);
        }
        if (failures.empty())
            return true;
        problem = problem_of(Refusal::unfit);
        for (const oa::data::map_fit::Failure& failure : failures) {
            const std::string sentence = oa::data::map_fit::describe(failure);
            problem.lines.push_back(sentence);
            if (!problem.detail.empty())
                problem.detail += ' ';
            problem.detail += sentence;
        }
        return false;
    } catch (const std::exception&) {
        problem = problem_of(Refusal::unfit, {}, "A map in the pack could not be checked.");
        return false;
    }
}

} // namespace oa::app::package_install::oamap
