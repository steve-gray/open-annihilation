// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The kinds of package the game installs, and the names and folder reads
// each kind shares. One kind today: oamod.

#include "files.hpp"

#include "oa/app/package_install.hpp"
#include "oa/app/package_install/oamod.hpp"
#include "oa/app/package_install/prompts.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/base/threads.hpp"
#include "oa/formats/oamod.hpp"

#include <list>
#include <string>
#include <string_view>
#include <utility>

namespace oa::app::package_install {

namespace fs = std::filesystem;

namespace {

/// A kind remembered by its prefix, so a folder it left can be settled after
/// the kind's own object is gone. The list keeps each node where it is, and
/// the kind's prefix view points at the node's text.
struct Noted {
    std::string prefix{};
    PackageKind kind{};
};

base::threads::Mutex& noted_mutex() {
    static base::threads::Mutex mutex;
    return mutex;
}

std::list<Noted>& noted_kinds() {
    static std::list<Noted> kinds;
    return kinds;
}

constexpr KindPrompts kModPrompts{
    .installing = &oamod::installing_prompt,
    .question = &oamod::question_prompt,
    .installed = &oamod::installed_prompt,
    .updated = &oamod::updated_prompt,
    .refusal_text = &oamod::refusal_text,
    .refused = &oamod::refused_prompt,
};

constexpr PackageKind kKinds[] = {
    {
        .name = "oamod",
        .extension = ".oamod",
        .manifest = "oamod.yaml",
        .manifest_most_bytes = oa::formats::oamod::max_input_bytes,
        .root_folder = user_mods_folder_name,
        .prefix = ".oamod-",
        .read_manifest = &oamod::read_manifest,
        .read_installed = &oamod::read_installed_mod,
        .read_backup = &oamod::read_backup,
        .plan = &oamod::plan_install,
        .check_staged = nullptr,
        .prompts = &kModPrompts,
    },
};

/// The root a folder_hooks reads, kept with the hooks.
struct Binding {
    const PackageKind* kind{};
    fs::path root{};
};

} // namespace

namespace detail {

void note_kind(const PackageKind& kind) {
    const base::threads::LockGuard guard(noted_mutex());
    for (const Noted& noted : noted_kinds())
        if (noted.prefix == kind.prefix)
            return;
    Noted& noted = noted_kinds().emplace_back();
    noted.prefix = std::string(kind.prefix);
    noted.kind = kind;
    noted.kind.prefix = noted.prefix;
}

const PackageKind* kind_of_reserved(std::string_view name) {
    const PackageKind* found = nullptr;
    const auto consider = [&](const PackageKind& kind) {
        if (!kind.prefix.empty() && name.starts_with(kind.prefix) &&
            (found == nullptr || kind.prefix.size() > found->prefix.size()))
            found = &kind;
    };
    for (const PackageKind& kind : kKinds)
        consider(kind);
    const base::threads::LockGuard guard(noted_mutex());
    for (const Noted& noted : noted_kinds())
        consider(noted.kind);
    return found;
}

} // namespace detail

std::span<const PackageKind> package_kinds() noexcept {
    return kKinds;
}

const PackageKind* kind_for_file(const fs::path& file) noexcept {
    const std::string name = detail::folded(detail::utf8_of(file.filename()));
    const PackageKind* found = nullptr;
    for (const PackageKind& kind : kKinds) {
        const std::string extension = detail::folded(kind.extension);
        if (extension.empty() || name.size() <= extension.size() || !name.ends_with(extension))
            continue;
        if (found == nullptr || kind.extension.size() > found->extension.size())
            found = &kind;
    }
    return found;
}

const PackageKind* find_kind(std::string_view name) noexcept {
    for (const PackageKind& kind : kKinds)
        if (kind.name == name)
            return &kind;
    return nullptr;
}

const PackageKind& mod_kind() noexcept {
    return kKinds[0];
}

FolderNames folder_names(const PackageKind& kind) {
    const std::string prefix(kind.prefix);
    FolderNames names{};
    names.reserved = prefix;
    names.staging = prefix + "staging-";
    names.old = prefix + "old-";
    names.replaced = prefix + "replaced-";
    names.restore = prefix + "restore-";
    names.discard = prefix + "discard-";
    names.lock = prefix + "lock";
    return names;
}

FolderHooks folder_hooks(const PackageKind& kind, const fs::path& root) {
    const auto binding = std::make_shared<Binding>();
    binding->kind = &kind;
    binding->root = root;
    FolderHooks hooks{};
    hooks.owned = binding;
    hooks.context = binding.get();
    hooks.look = [](void* context, std::string_view folder) {
        const auto& bound = *static_cast<const Binding*>(context);
        if (bound.kind == nullptr || bound.kind->read_installed == nullptr)
            return InstalledPackage{};
        return bound.kind->read_installed(bound.root / detail::path_of(folder));
    };
    hooks.backup_of = [](void* context, std::string_view folder) {
        const auto& bound = *static_cast<const Binding*>(context);
        if (bound.kind == nullptr || bound.kind->read_backup == nullptr)
            return std::optional<InstalledPackage>{};
        return bound.kind->read_backup(bound.root / detail::path_of(folder));
    };
    return hooks;
}

} // namespace oa::app::package_install
