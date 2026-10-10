// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Language packs read from their folders (oa/data/languages/language_pack.hpp):
// the engine's own, in the languages folder beside the game's other files;
// the player's, in Languages in their own folder; and a mod's, in its
// languages folder. Each pack is a folder named by its tag, holding
// language.yaml and its tables.

#pragma once

#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/languages/language_pack.hpp"
#include "oa/platform/text_font.hpp"

#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

/// The folder beside the game's other files that holds the engine's
/// interface catalogue files and its language packs.
inline constexpr std::string_view engine_languages_folder = "languages";

/// The folder of the player's own folder that holds their language packs.
inline constexpr std::string_view player_languages_folder = "Languages";

/// A language pack read from its folder.
struct LoadedLanguagePack {
    /// The pack's tables.
    oa::data::languages::LanguagePack pack;
    /// The folder it was read from; its files folder answers the game
    /// data's language folders.
    std::filesystem::path folder{};
    /// The warm-up text, drawn into the glyph store when the language is
    /// shown; empty when the manifest names none.
    std::string warmup{};
};

/// One font file of a pack, at its path in the pack's fonts folder.
struct PackFontFile {
    std::filesystem::path file{};             ///< the font file
    oa::platform::text_font::FaceRole role{}; ///< how the face is drawn
};

/// Returns a pack's font files, in the manifest's order.
///
/// Each file is <folder>/fonts/<file>. A name the manifest accepted is
/// used as it is; reading the pack has already required the file to be there.
///
/// @param pack the pack
/// @return the files
[[nodiscard]] std::vector<PackFontFile> pack_fonts(const LoadedLanguagePack& pack);

/// Reads every language pack in a folder: each folder in it that holds
/// language.yaml. A pack whose manifest or a table does not read is
/// reported and left out whole; tables larger than
/// oa::data::languages::most_pack_table_bytes are refused.
///
/// @param root the folder that holds the packs' folders; one that is not
///     there holds none
/// @param[in,out] packs the packs read are added at the end, in the order
///     of their folders' names
/// @param[in,out] catalogue each pack's interface.tdf is added to it; null
///     checks that the file is one the catalogue reads and adds nothing
void read_language_packs(
    const std::filesystem::path& root,
    std::vector<std::unique_ptr<LoadedLanguagePack>>& packs,
    oa::data::languages::InterfaceText* catalogue
);

/// Returns the registry entries of the packs, in the order of the groups
/// and of the packs in each group.
///
/// @param packs the groups; a null group is skipped
/// @return one entry per pack, as its manifest describes it
[[nodiscard]] std::vector<oa::data::languages::LanguageEntry> installed_entries(
    std::initializer_list<const std::vector<std::unique_ptr<LoadedLanguagePack>>*> packs
);

/// Adds each pack's interface.tdf to a catalogue, in the packs' order.
///
/// A file that does not read is reported and skipped. read_language_packs
/// has already left out a pack whose interface.tdf the catalogue refuses,
/// so this adds the texts after the catalogue's own files.
///
/// @param packs the packs, in the order their texts replace earlier ones
/// @param catalogue the catalogue
void add_pack_interface_texts(
    const std::vector<std::unique_ptr<LoadedLanguagePack>>& packs,
    oa::data::languages::InterfaceText& catalogue
);

/// Reads a file of a pack's files folder, of at most the bytes the TDF
/// reader takes.
///
/// @param file the file
/// @param[out] failure why it was not read
/// @return its bytes; nothing when it is not there, is larger or does not read
[[nodiscard]] std::optional<std::string>
read_pack_file(const std::filesystem::path& file, std::string& failure);

/// Finds a file of the game data's language folders in a pack's files
/// folder, matching each name without regard to case.
///
/// @param pack the pack
/// @param path the file's path in the game data, its names parted by '/'
///     or '\\', as "camps/briefs-Chinese/arm01.txt"
/// @return the file on this machine; empty when the pack has none, or when
///     no folder of the path is one of the pack's word's language folders
///     (<folder>-<word>)
[[nodiscard]] std::filesystem::path
language_pack_file(const LoadedLanguagePack& pack, std::string_view path);

} // namespace oa::app
