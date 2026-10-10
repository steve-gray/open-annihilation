// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The oalang kind's own rules: its manifest, the engine it needs, the tables,
// fonts and warm-up the game reads, what a folder of Languages holds, and
// where a language pack installs.

#include "files.hpp"

#include "oa/app/package_install/oalang.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/languages/language_pack.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/oamod/package_keys.hpp"
#include "oa/platform/text_font.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef OA_ENGINE_VERSION
#error                                                                                             \
    "OA_ENGINE_VERSION names this build's version, which a language pack's engine requirement is checked against"
#endif

namespace oa::app::package_install::oalang {

namespace fs = std::filesystem;
namespace languages = oa::data::languages;
namespace keys = oa::formats::oamod;
namespace text_font = oa::platform::text_font;

namespace {

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

/// Tells whether a packaged name is a folder.
///
/// @param name the name
/// @return true when it ends in '/'
bool directory_name(std::string_view name) noexcept {
    return !name.empty() && name.back() == '/';
}

/// Tells whether the package holds a file, matched without case.
///
/// @param package the package
/// @param relative the name below the pack's folder
/// @return true when an entry has that name
bool holds_file(const Package& package, std::string_view relative) {
    const std::string wanted = detail::folded(relative);
    for (const PackagedFile& file : package.files) {
        if (directory_name(file.name))
            continue;
        if (detail::folded(file.name) == wanted)
            return true;
    }
    return false;
}

/// Reads one packaged file through the manifest context.
///
/// @param context the context
/// @param name the file, below the pack's folder
/// @param most_bytes the most bytes read
/// @param[out] bytes the file, when it was read
/// @return how the read fared; missing when no reader was given
EntryRead read_pack_file(
    const ManifestContext& context,
    std::string_view name,
    uint64_t most_bytes,
    std::vector<uint8_t>& bytes
) {
    if (context.read_entry == nullptr)
        return EntryRead::missing;
    return context.read_entry(context.reader, name, most_bytes, bytes);
}

/// Returns bytes as text.
///
/// @param bytes the bytes
/// @return the text
std::string text_of(const std::vector<uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// The sentence for an engine requirement this build does not meet.
///
/// @param range the requirement
/// @return M01's wording
std::string unmet_engine(const keys::EngineRange& range) {
    return "needs Open Annihilation " + keys::describe_engine_range(range) +
           "; this is Open Annihilation " + keys::engine_version_text(this_build_version());
}

/// Reads a folder's language.yaml.
///
/// @param folder the folder
/// @param[out] manifest the manifest, when it was read
/// @return true when the folder holds a readable manifest
bool read_folder_manifest(const fs::path& folder, languages::PackManifest& manifest) {
    const auto found = entry_without_case(folder, languages::pack_manifest_file);
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
    return languages::read_manifest(bytes, manifest);
}

/// Fills what a folder holds from a manifest it gave.
///
/// @param manifest the manifest
/// @return the folder, of kind package
InstalledPackage installed_of(const languages::PackManifest& manifest) {
    InstalledPackage installed{};
    installed.kind = FolderKind::package;
    installed.id = manifest.tag;
    installed.name = manifest.name;
    installed.version = manifest.version;
    installed.revision = manifest.packaging ? manifest.packaging->revision : 0;
    return installed;
}

/// Plans a catalogue install that asks nothing, or the question a file asks.
///
/// @param incoming the pack
/// @param ask the question a file the player opened asks
/// @param held what the folder holds
/// @param backup what its .backup holds
/// @param reinstalling the same version and revision is installed again
/// @param replacing the folder is replaced
/// @return the plan
InstallPlan decided(
    const Incoming& incoming,
    PlanKind ask,
    const InstalledPackage& held,
    std::optional<InstalledPackage> backup,
    bool reinstalling,
    bool replacing
) {
    InstallPlan plan{};
    plan.target = incoming.id;
    plan.installed = held;
    plan.backup = std::move(backup);
    if (incoming.origin.kind == OriginKind::catalogue) {
        plan.kind = PlanKind::install;
        plan.reinstalling = reinstalling;
        plan.replacing = replacing;
        return plan;
    }
    plan.kind = ask;
    if (ask == PlanKind::ask_install)
        plan.alongside = incoming.id;
    if (ask == PlanKind::ask_update && held.kind == FolderKind::package && held.revision != 0 &&
        incoming.revision < held.revision)
        plan.older = true;
    return plan;
}

} // namespace

bool read_manifest(
    std::span<const uint8_t> manifest_bytes,
    const ManifestContext& context,
    Package& package,
    Problem& problem
) {
    languages::PackManifest manifest;
    std::string error;
    if (!languages::read_manifest(manifest_bytes, manifest, &error)) {
        problem = problem_of(Refusal::manifest_errors, {}, std::move(error));
        return false;
    }
    if (windows_device_name(manifest.tag)) {
        problem = problem_of(Refusal::reserved_id, manifest.tag);
        return false;
    }
    package.incoming.id = manifest.tag;
    package.incoming.name = manifest.name;
    package.incoming.english_name = manifest.english_name;
    package.incoming.version = manifest.version;
    package.incoming.revision = manifest.packaging ? manifest.packaging->revision : 0;

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

    for (const languages::PackFont& font : manifest.fonts) {
        const std::string packaged = std::string(languages::pack_fonts_folder) + "/" + font.file;
        if (!holds_file(package, packaged)) {
            problem = problem_of(Refusal::font_missing, packaged);
            return false;
        }
    }

    if (!manifest.warmup.empty()) {
        std::vector<uint8_t> bytes;
        const EntryRead read =
            read_pack_file(context, manifest.warmup, languages::most_warmup_bytes, bytes);
        if (read == EntryRead::missing) {
            problem =
                problem_of(Refusal::warmup_invalid, manifest.warmup, "its warm-up file is missing");
            return false;
        }
        if (read != EntryRead::read) {
            problem = problem_of(
                Refusal::warmup_invalid,
                manifest.warmup,
                "its warm-up file is larger than " + std::to_string(languages::most_warmup_bytes) +
                    " bytes"
            );
            return false;
        }
        if (!text_font::decode_utf8(text_of(bytes))) {
            problem = problem_of(
                Refusal::warmup_invalid, manifest.warmup, "its warm-up file is not UTF-8"
            );
            return false;
        }
    }

    languages::LanguagePack tables(manifest);
    for (const languages::PackTable table : languages::pack_tables) {
        const std::string file(languages::pack_table_file(table));
        std::vector<uint8_t> bytes;
        const EntryRead read =
            read_pack_file(context, file, languages::most_pack_table_bytes, bytes);
        if (read == EntryRead::missing)
            continue;
        std::string table_error;
        if (read != EntryRead::read || !tables.add(table, text_of(bytes), &table_error)) {
            if (table_error.empty())
                table_error = "it is larger than " +
                              std::to_string(languages::most_pack_table_bytes) + " bytes";
            problem = problem_of(Refusal::table_errors, file, file + ": " + table_error);
            return false;
        }
    }

    const std::string interface_file(languages::pack_interface_file);
    std::vector<uint8_t> interface_bytes;
    const EntryRead interface_read =
        read_pack_file(context, interface_file, languages::most_catalogue_bytes, interface_bytes);
    if (interface_read == EntryRead::unreadable) {
        problem = problem_of(
            Refusal::table_errors,
            interface_file,
            interface_file + ": it is larger than " +
                std::to_string(languages::most_catalogue_bytes) + " bytes"
        );
        return false;
    }
    if (interface_read == EntryRead::read && !interface_bytes.empty()) {
        languages::InterfaceText catalogue;
        std::string interface_error;
        if (!catalogue.add(text_of(interface_bytes), &interface_error)) {
            problem = problem_of(
                Refusal::table_errors, interface_file, interface_file + ": " + interface_error
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
        languages::PackManifest manifest;
        if (!read_folder_manifest(folder, manifest) || manifest.tag.empty())
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
    const InstalledPackage held = look(hooks, incoming.id);
    const std::optional<InstalledPackage> backup = backup_of(hooks, incoming.id);
    if (held.kind == FolderKind::missing)
        return decided(incoming, PlanKind::ask_install, {}, std::nullopt, false, false);
    const bool same_pack = held.kind == FolderKind::package && held.id == incoming.id;
    if (!same_pack)
        return decided(incoming, PlanKind::ask_version, held, backup, false, true);
    if (held.version == incoming.version && held.revision == incoming.revision &&
        held.revision != 0)
        return decided(incoming, PlanKind::ask_reinstall, held, backup, true, false);
    if (held.version == incoming.version)
        return decided(incoming, PlanKind::ask_update, held, backup, false, true);
    return decided(incoming, PlanKind::ask_version, held, backup, false, true);
}

bool check_staged(
    const fs::path& staging, const Package&, const PackageOptions&, Problem& problem
) {
    try {
        languages::PackManifest manifest;
        if (!read_folder_manifest(staging, manifest)) {
            problem = problem_of(Refusal::manifest_errors, {}, "Its language.yaml cannot be read.");
            return false;
        }
        for (const languages::PackFont& font : manifest.fonts) {
            const fs::path file = staging / detail::path_of(languages::pack_fonts_folder) /
                                  detail::path_of(font.file);
            if (!text_font::FontStack::face_file_opens(file)) {
                const std::string packaged =
                    std::string(languages::pack_fonts_folder) + "/" + font.file;
                problem = problem_of(Refusal::font_unreadable, packaged);
                return false;
            }
        }
        return true;
    } catch (const std::exception&) {
        problem =
            problem_of(Refusal::font_unreadable, {}, "A font in the pack could not be opened.");
        return false;
    }
}

} // namespace oa::app::package_install::oalang
