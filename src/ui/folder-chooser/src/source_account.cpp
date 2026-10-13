// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What a folder or a file is, for the locator's column and its side panel
// (folder_chooser.hpp). Installers are recognised by the size the built-in
// table records. A folder's bundles are the ones whose files match by name
// and size. Nothing is hashed here.
#include "oa/ui/folder_chooser.hpp"

#include "oa/data/content_bundles.hpp"
#include "oa/data/languages/interface_text.hpp"

#include <span>
#include <string>
#include <string_view>

namespace oa::ui::folder_chooser {

namespace {

namespace bundles = oa::data::content_bundles;

/// Returns one of the locator's words in the installed language.
///
/// @param english the words as the source writes them
/// @return their translation, or the words themselves
std::string tr(std::string_view english) {
    return std::string(oa::data::languages::interface_text(english));
}

/// Returns the name a bundle is shown under.
///
/// @param bundle the bundle
/// @return the name players read
std::string bundle_name(const bundles::Bundle& bundle) {
    if (bundle.id == "totala")
        return tr("Total Annihilation");
    if (bundle.id == "totala-demo")
        return tr("Total Annihilation demo");
    return tr(bundle.english_name);
}

/// Tells whether a bundle is part of the Commander Pack (every bundle but the demo).
///
/// @param bundle the bundle
/// @return true for the retail game and its expansions
bool commander_pack(const bundles::Bundle& bundle) noexcept {
    return bundle.id != "totala-demo";
}

/// Folds ASCII letters to lower case.
///
/// @param text the text
/// @return the folded text
std::string folded(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char character : text) {
        if (character >= 'A' && character <= 'Z')
            out.push_back(static_cast<char>(character - 'A' + 'a'));
        else
            out.push_back(static_cast<char>(character));
    }
    return out;
}

/// Returns the characters after the last dot, folded; empty when there is no dot.
///
/// @param name a file name
/// @return the extension, without the dot
std::string extension_of(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size())
        return {};
    return folded(name.substr(dot + 1));
}

/// Tells whether a file name is an archive the column calls an archive.
///
/// @param name the file name
/// @return true for the game's archive extensions
bool archive_name(std::string_view name) {
    const std::string extension = extension_of(name);
    return extension == "hpi" || extension == "ufo" || extension == "ccx" || extension == "gp3" ||
           extension == "gpf";
}

/// Joins bundle names with commas.
///
/// @param names the names
/// @return the names in one line
std::string join_names(const std::vector<std::string>& names) {
    std::string line;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0)
            line += ", ";
        line += names[index];
    }
    return line;
}

/// The bundles a folder holds, in the built-in order, from files matched by name and size.
///
/// @param children the folder's files
/// @param[out] base_game a base game's file matched
/// @param[out] demo the demo's archive matched
/// @return the bundle names
std::vector<std::string>
matched_bundles(const std::vector<ChildFile>& children, bool& base_game, bool& demo) {
    base_game = false;
    demo = false;
    std::vector<uint8_t> hit(bundles::bundles().size(), 0);
    for (const ChildFile& child : children) {
        const bundles::FileMatch match = bundles::match_file(child.name, child.size);
        if (!match)
            continue;
        const bundles::Bundle* first = bundles::bundles().data();
        const auto slot = static_cast<std::size_t>(match.bundle - first);
        if (slot < hit.size())
            hit[slot] = 1;
        if (match.bundle->id == "totala")
            base_game = true;
        if (match.bundle->id == "totala-demo")
            demo = true;
    }
    std::vector<std::string> names;
    const std::span<const bundles::Bundle> known = bundles::bundles();
    for (std::size_t slot = 0; slot < known.size(); ++slot) {
        if (hit[slot] == 0)
            continue;
        names.push_back(bundle_name(known[slot]));
    }
    return names;
}

/// Fills an account for one of the Commander Pack's installers.
///
/// @param kind which installer
/// @return the account
SourceAccount installer_account(bundles::InstallerKind kind) {
    SourceAccount account;
    account.recognised = true;
    account.choosable = true;
    account.account =
        tr("Total Annihilation: Commander Pack. Recognised by its size. The game is read out of "
           "it; nothing is installed.");
    for (const bundles::Bundle& bundle : bundles::bundles()) {
        if (commander_pack(bundle))
            account.bundles.push_back(bundle_name(bundle));
    }
    switch (kind) {
    case bundles::InstallerKind::windows:
        account.what = tr("GOG installer (Windows)");
        account.title = tr("GOG installer for Windows");
        break;
    case bundles::InstallerKind::macos:
        account.what = tr("GOG installer (macOS)");
        account.title = tr("GOG installer for macOS");
        break;
    case bundles::InstallerKind::older_mac:
        account.what = tr("GOG installer (older Macs)");
        account.title = tr("GOG installer for older Macs");
        break;
    }
    return account;
}

/// Fills an account for the 1997 demo's installer.
///
/// @return the account
SourceAccount demo_account() {
    SourceAccount account;
    account.what = tr("TA demo installer");
    account.title = tr("Total Annihilation demo (1997)");
    account.account = tr("The installer of the Total Annihilation demo (1997).");
    account.bundles.push_back(tr("Total Annihilation demo"));
    account.recognised = true;
    account.choosable = true;
    return account;
}

} // namespace

SourceAccount describe_source(
    std::string_view name,
    uint64_t size,
    bool directory,
    const std::vector<ChildFile>& children,
    uint64_t demo_installer_bytes
) {
    SourceAccount account;
    account.directory = directory;
    account.title = std::string(name);
    if (!directory) {
        if (const bundles::Installer* installer = bundles::find_installer(size))
            return installer_account(installer->kind);
        if (demo_installer_bytes != 0 && size == demo_installer_bytes)
            return demo_account();
        account.what = archive_name(name) ? tr("archive") : tr("file");
        return account;
    }
    bool base_game = false;
    bool demo = false;
    account.bundles = matched_bundles(children, base_game, demo);
    if (account.bundles.empty()) {
        account.what = tr("folder");
        return account;
    }
    account.recognised = true;
    account.choosable = base_game || demo;
    if (base_game) {
        account.what = tr("Total Annihilation");
        account.title = tr("Total Annihilation");
    } else if (demo) {
        account.what = tr("Total Annihilation demo");
        account.title = tr("Total Annihilation demo");
    } else {
        account.what = account.bundles.front();
        account.title = account.bundles.front();
    }
    std::string held = tr("Holds {bundles}.");
    const std::string names = join_names(account.bundles);
    if (const auto at = held.find("{bundles}"); at != std::string::npos)
        held.replace(at, std::string_view("{bundles}").size(), names);
    account.account = std::move(held);
    return account;
}

} // namespace oa::ui::folder_chooser
