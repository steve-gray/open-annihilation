// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/map_pack/manifest.hpp"

#include "oa/data/map_pack/map_name.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/oamod/package_keys.hpp"

#include <array>
#include <string>
#include <utility>

namespace oa::data::map_pack {

namespace {

namespace oamod = oa::formats::oamod;

/// The top-level keys a manifest may hold, in the order they are written.
constexpr std::array<std::string_view, 11> manifest_keys{
    "oamap",
    "id",
    "name",
    "version",
    "description",
    "homepage",
    "tags",
    "author",
    "packaging",
    "requires",
    "maps",
};

/// The keys of one map's index entry.
constexpr std::array<std::string_view, 7> map_keys{
    "stem",
    "name",
    "description",
    "players",
    "size",
    "preview",
    "files",
};

/// The keys of the author block. The name is required.
constexpr std::array<std::string_view, 2> author_keys{"name", "email"};

/// The keys of the packaging block. All three are required.
constexpr std::array<std::string_view, 3> packaging_keys{"revision", "date", "packager"};

/// The keys of the requires block.
constexpr std::array<std::string_view, 2> requires_keys{"base", "engine"};

/// The most characters of the pack's name. A map's title has its own bound.
constexpr std::size_t most_pack_name_characters = 64;

/// The most characters of the author's version text.
constexpr std::size_t most_version_characters = 32;

/// The most characters of the pack's description, as a mod's description allows.
constexpr std::size_t most_pack_description_characters = 120;

/// The most characters of the packager's name.
constexpr std::size_t most_packager_characters = 64;

/// The most bytes of an author's name, as a mod's author name allows.
constexpr std::size_t most_author_name_bytes = 128;

/// The most bytes of an e-mail address, as a mod's author e-mail allows.
constexpr std::size_t most_email_bytes = 254;

/// The range of a package revision, as a mod's revision allows.
constexpr int64_t least_revision = 1;
constexpr int64_t most_revision = 65535;

/// The fewest players a map lists.
constexpr int32_t least_players = 2;

/// The fewest paths a packed map lists: its own OTA and its own TNT.
constexpr std::size_t least_files_per_map = 2;

/// The game data every pack builds on, when it names a base.
constexpr std::string_view base_game = "ta-3.1c";

/// The first code point that is not a C0 control character.
constexpr char32_t first_printable = 0x20;

/// The delete character, and the last C1 control character.
constexpr char32_t delete_character = 0x7F;
constexpr char32_t last_c1_control = 0x9F;

/// The line separator and the paragraph separator.
constexpr char32_t line_separator = 0x2028;
constexpr char32_t paragraph_separator = 0x2029;

/// Appends one problem.
///
/// @param problems the problems collected so far
/// @param position where the value starts
/// @param key the path of the value
/// @param message what is wrong
void add_problem(
    std::vector<Problem>& problems,
    oamod::TextPosition position,
    std::string key,
    std::string message
) {
    problems.push_back(Problem{position.line, position.column, std::move(key), std::move(message)});
}

/// Tells whether a mapping key is one of a list.
///
/// @param key the key
/// @param allowed the keys that may appear
/// @return true when `key` is one of them
template <std::size_t count>
bool listed(std::string_view key, const std::array<std::string_view, count>& allowed) {
    for (const std::string_view known : allowed)
        if (known == key)
            return true;
    return false;
}

/// Reports every key of a mapping that is not one of a list.
///
/// @param node the mapping
/// @param allowed the keys that may appear
/// @param block the path of the mapping, or empty at the top
/// @param[in,out] problems the problems
template <std::size_t count>
void refuse_unknown_keys(
    const oamod::Node& node,
    const std::array<std::string_view, count>& allowed,
    std::string_view block,
    std::vector<Problem>& problems
) {
    for (const oamod::Node& entry : node.children) {
        const std::string& key = entry.key.text;
        if (entry.key.kind == oamod::KeyKind::string && listed(key, allowed))
            continue;
        std::string path = key;
        if (!block.empty())
            path = std::string{block} + "." + key;
        add_problem(problems, entry.key.position, std::move(path), "unknown key '" + key + "'");
    }
}

/// Decodes UTF-8 into its code points.
///
/// The manifest reader has already refused bytes that are not UTF-8.
///
/// @param text the text
/// @return its code points
std::u32string code_points(std::string_view text) {
    std::u32string characters;
    std::size_t at = 0;
    while (at < text.size()) {
        const auto lead = static_cast<unsigned char>(text[at]);
        char32_t code_point = lead;
        std::size_t length = 1;
        if (lead >= 0xF0) {
            code_point = lead & 0x07;
            length = 4;
        } else if (lead >= 0xE0) {
            code_point = lead & 0x0F;
            length = 3;
        } else if (lead >= 0xC0) {
            code_point = lead & 0x1F;
            length = 2;
        }
        for (std::size_t index = 1; index < length && at + index < text.size(); ++index) {
            code_point = (code_point << 6) | (static_cast<unsigned char>(text[at + index]) & 0x3F);
        }
        at += length;
        characters.push_back(code_point);
    }
    return characters;
}

/// Tells whether a character breaks a line of plain text.
///
/// A control character (C0, delete or C1), a line separator or a paragraph
/// separator, as a mod's description refuses them.
///
/// @param character the code point
/// @return true when a one-line text may not hold it
bool breaks_line(char32_t character) {
    return character < first_printable ||
           (character >= delete_character && character <= last_c1_control) ||
           character == line_separator || character == paragraph_separator;
}

/// Tells whether a text is one line of plain text.
///
/// @param text the text
/// @return true when no character breaks the line
bool one_line(std::string_view text) {
    const std::u32string characters = code_points(text);
    for (const char32_t character : characters)
        if (breaks_line(character))
            return false;
    return true;
}

/// Counts the characters of a text.
///
/// @param text the text
/// @return how many code points it holds
std::size_t character_count(std::string_view text) {
    return code_points(text).size();
}

/// Lowers ASCII letters.
///
/// @param character a byte
/// @return the byte, A-Z made a-z
char ascii_lower(unsigned char character) {
    if (character >= 'A' && character <= 'Z')
        return static_cast<char>(character - 'A' + 'a');
    return static_cast<char>(character);
}

/// Folds the ASCII letters of a text.
///
/// @param text the text
/// @return the text, A-Z made a-z
std::string folded(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char character : text)
        out.push_back(ascii_lower(character));
    return out;
}

/// Tells whether two texts are equal ignoring the case of ASCII letters.
///
/// @param left the first text
/// @param right the second text
/// @return true when they fold to the same bytes
bool same_ascii(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (ascii_lower(static_cast<unsigned char>(left[index])) !=
            ascii_lower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

/// Tells whether a text ends with a suffix, ignoring the case of ASCII letters.
///
/// @param text the text
/// @param suffix the suffix
/// @return true when `text` ends with `suffix`
bool ends_ascii(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size())
        return false;
    return same_ascii(text.substr(text.size() - suffix.size()), suffix);
}

/// Tells whether a path is under a top folder.
///
/// @param path the path
/// @param folder the folder, without a slash
/// @return true when the path's first part is that folder and more follows
bool under_folder(std::string_view path, std::string_view folder) {
    if (path.size() <= folder.size() || path[folder.size()] != '/')
        return false;
    return same_ascii(path.substr(0, folder.size()), folder);
}

/// Tells whether an e-mail address has the shape a mod's author e-mail has.
///
/// One @ with text on both sides, a dot in the part after it, and no white
/// space or control character, at most most_email_bytes.
///
/// @param text the address
/// @return true when it has that shape
bool email_address(std::string_view text) {
    if (text.empty() || text.size() > most_email_bytes)
        return false;
    for (const unsigned char character : text)
        if (character <= ' ' || character == 0x7F)
            return false;
    const std::size_t at = text.find('@');
    if (at == 0 || at == std::string_view::npos || text.find('@', at + 1) != std::string_view::npos)
        return false;
    const std::string_view domain = text.substr(at + 1);
    const std::size_t dot = domain.find('.');
    return dot != std::string_view::npos && dot != 0 && domain.back() != '.';
}

/// Tells whether a text is a calendar date, YYYY-MM-DD, that exists.
///
/// February has 29 days in a Gregorian leap year.
///
/// @param text the date
/// @return true when it is such a date
bool calendar_date(std::string_view text) {
    constexpr std::size_t year_digits = 4;
    constexpr std::size_t month_start = year_digits + 1;
    constexpr std::size_t day_start = month_start + 3;
    constexpr std::size_t date_length = day_start + 2;
    if (text.size() != date_length || text[month_start - 1] != '-' || text[day_start - 1] != '-')
        return false;
    const auto digits = [text](std::size_t start, std::size_t count, int& value) {
        value = 0;
        for (const char character : text.substr(start, count)) {
            if (character < '0' || character > '9')
                return false;
            value = value * 10 + (character - '0');
        }
        return true;
    };
    int year = 0;
    int month = 0;
    int day = 0;
    if (!digits(0, year_digits, year) || !digits(month_start, 2, month) ||
        !digits(day_start, 2, day))
        return false;
    if (month < 1 || month > 12 || day < 1)
        return false;
    constexpr std::array<int, 12> month_days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int days = month == 2 && leap ? 29 : month_days[static_cast<std::size_t>(month - 1)];
    return day <= days;
}

/// Reads one side of a map's size.
///
/// @param text the digits
/// @param[out] side the side, when the text is a whole number from 1 to most_map_side
/// @return true when `side` was set
bool map_side(std::string_view text, int& side) {
    if (text.empty() || text.size() > 3 || (text.size() > 1 && text.front() == '0'))
        return false;
    side = 0;
    for (const char character : text) {
        if (character < '0' || character > '9')
            return false;
        side = side * 10 + (character - '0');
    }
    return side >= 1 && side <= most_map_side;
}

/// Tells whether a size is `<width>x<height>` with each side in range.
///
/// @param text the size
/// @return true when both sides are whole numbers from 1 to most_map_side
bool map_size(std::string_view text) {
    const std::size_t split = text.find('x');
    if (split == std::string_view::npos || text.find('x', split + 1) != std::string_view::npos)
        return false;
    int width = 0;
    int height = 0;
    return map_side(text.substr(0, split), width) && map_side(text.substr(split + 1), height);
}

/// Reports a string that is not one line of at most a number of characters.
///
/// @param node the value
/// @param key its path
/// @param most the most characters
/// @param[in,out] problems the problems
/// @param[out] text the string, when the value is a string
/// @return true when the value is a string in those bounds
bool one_line_text(
    const oamod::Node& node,
    const std::string& key,
    std::size_t most,
    std::vector<Problem>& problems,
    std::string& text
) {
    if (node.kind != oamod::NodeKind::string) {
        add_problem(problems, node.position, key, key + " must be a string");
        return false;
    }
    text = node.text;
    bool ok = true;
    if (!one_line(node.text)) {
        add_problem(problems, node.position, key, key + " must be one line");
        ok = false;
    }
    const std::size_t count = character_count(node.text);
    if (count < 1 || count > most) {
        add_problem(
            problems,
            node.position,
            key,
            key + " must be 1 to " + std::to_string(most) + " characters (it has " +
                std::to_string(count) + ")"
        );
        ok = false;
    }
    return ok;
}

/// The shape problems of one path: a backslash, a colon, a control character,
/// or an empty, `.` or `..` part.
///
/// @param path the path
/// @param key the path of the value
/// @param position where the path is written
/// @param[in,out] problems the problems
void path_shape(
    std::string_view path,
    const std::string& key,
    oamod::TextPosition position,
    std::vector<Problem>& problems
) {
    bool backslash = false;
    bool colon = false;
    bool control = false;
    for (const unsigned char character : path) {
        if (character == '\\')
            backslash = true;
        else if (character == ':')
            colon = true;
        else if (character < 0x20 || character == 0x7F)
            control = true;
    }
    if (backslash)
        add_problem(problems, position, key, "a path contains a backslash");
    if (colon)
        add_problem(problems, position, key, "a path contains a colon");
    if (control)
        add_problem(problems, position, key, "a path contains a control character");
    bool bad_part = path.empty();
    std::size_t start = 0;
    while (!bad_part && start <= path.size()) {
        const std::size_t cut = path.find('/', start);
        const std::size_t end = cut == std::string_view::npos ? path.size() : cut;
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
            bad_part = true;
        if (cut == std::string_view::npos)
            break;
        start = cut + 1;
    }
    if (bad_part)
        add_problem(
            problems, position, key, "a path must be relative, with no empty, '.' or '..' part"
        );
}

/// Tells whether a path is one a map may list.
///
/// Its own `maps/<stem>.ota` or `maps/<stem>.tnt`, a feature TDF, a feature
/// animation or a 3DO model. Extensions are compared without regard to case.
///
/// @param path the path
/// @param stem the map's stem
/// @return true when the path is one of those
bool allowed_map_file(std::string_view path, std::string_view stem) {
    const std::string ota = std::string{"maps/"} + std::string{stem} + ".ota";
    const std::string tnt = std::string{"maps/"} + std::string{stem} + ".tnt";
    if (same_ascii(path, ota) || same_ascii(path, tnt))
        return true;
    if (under_folder(path, "features") && ends_ascii(path, ".tdf"))
        return true;
    if (under_folder(path, "anims") && ends_ascii(path, ".gaf"))
        return true;
    return under_folder(path, "objects3d") && ends_ascii(path, ".3do");
}

/// Reads a map's files, reporting every broken rule of every path.
///
/// @param node the files value
/// @param index the map's index
/// @param stem the map's stem, when it is valid
/// @param stem_known whether `stem` passed its rule
/// @param[out] files the paths, including those that broke a rule
/// @param[in,out] problems the problems
void read_files(
    const oamod::Node& node,
    std::size_t index,
    std::string_view stem,
    bool stem_known,
    std::vector<std::string>& files,
    std::vector<Problem>& problems
) {
    const std::string key = "maps[" + std::to_string(index) + "].files";
    if (node.kind != oamod::NodeKind::sequence) {
        add_problem(problems, node.position, key, "files must be a list of paths");
        return;
    }
    if (node.children.size() < least_files_per_map || node.children.size() > most_files_per_map) {
        add_problem(
            problems,
            node.position,
            key,
            "files must list " + std::to_string(least_files_per_map) + " to " +
                std::to_string(most_files_per_map) + " paths (it lists " +
                std::to_string(node.children.size()) + ")"
        );
    }
    bool saw_ota = false;
    bool saw_tnt = false;
    const std::string ota = std::string{"maps/"} + std::string{stem} + ".ota";
    const std::string tnt = std::string{"maps/"} + std::string{stem} + ".tnt";
    std::vector<std::string> seen;
    for (std::size_t file_index = 0; file_index < node.children.size(); ++file_index) {
        const oamod::Node& item = node.children[file_index];
        const std::string file_key = key + "[" + std::to_string(file_index) + "]";
        if (item.kind != oamod::NodeKind::string) {
            add_problem(problems, item.position, file_key, "a file must be a path");
            continue;
        }
        files.push_back(item.text);
        path_shape(item.text, file_key, item.position, problems);
        const bool features = under_folder(item.text, "features") && ends_ascii(item.text, ".tdf");
        const bool anims = under_folder(item.text, "anims") && ends_ascii(item.text, ".gaf");
        const bool models = under_folder(item.text, "objects3d") && ends_ascii(item.text, ".3do");
        const bool allowed = (stem_known && allowed_map_file(item.text, stem)) ||
                             (!stem_known && (features || anims || models));
        if (!allowed) {
            add_problem(
                problems,
                item.position,
                file_key,
                item.text + " is not this map's own OTA or TNT, a feature TDF, a feature animation "
                            "or a 3DO model"
            );
        }
        if (stem_known && same_ascii(item.text, ota))
            saw_ota = true;
        if (stem_known && same_ascii(item.text, tnt))
            saw_tnt = true;
        const std::string fold = folded(item.text);
        bool repeated = false;
        for (const std::string& earlier : seen) {
            if (earlier == fold) {
                repeated = true;
                break;
            }
        }
        if (repeated) {
            add_problem(
                problems, item.position, file_key, "a map lists this file twice, ignoring case"
            );
        } else {
            seen.push_back(fold);
        }
    }
    if (!stem_known)
        return;
    if (!saw_ota)
        add_problem(problems, node.position, key, "files must list " + ota);
    if (!saw_tnt)
        add_problem(problems, node.position, key, "files must list " + tnt);
}

/// Reads a preview path.
///
/// @param node the preview value
/// @param key its path
/// @param[out] preview the path
/// @param[in,out] problems the problems
void read_preview(
    const oamod::Node& node,
    const std::string& key,
    std::string& preview,
    std::vector<Problem>& problems
) {
    if (node.kind != oamod::NodeKind::string) {
        add_problem(problems, node.position, key, "preview must be a path");
        return;
    }
    preview = node.text;
    path_shape(node.text, key, node.position, problems);
    if (!under_folder(node.text, "previews") || !ends_ascii(node.text, ".png")) {
        add_problem(
            problems, node.position, key, "preview must be under previews/ and end in .png"
        );
    }
}

/// Reads one map entry, in source or package use.
///
/// @param entry the entry
/// @param index its index in maps
/// @param use whether the entry must be complete
/// @param id the pack's id, when it is a valid pack id
/// @param id_known whether `id` passed its rule
/// @param[in,out] manifest the manifest being filled
/// @param[in,out] stems the folded stems already accepted, so a repeat is reported
/// @param[in,out] problems the problems
void read_map(
    const oamod::Node& entry,
    std::size_t index,
    ManifestUse use,
    std::string_view id,
    bool id_known,
    Manifest& manifest,
    std::vector<std::string>& stems,
    std::vector<Problem>& problems
) {
    const std::string prefix = "maps[" + std::to_string(index) + "]";
    if (entry.kind != oamod::NodeKind::mapping) {
        add_problem(problems, entry.position, prefix, "a map must be a mapping");
        return;
    }
    refuse_unknown_keys(entry, map_keys, prefix, problems);
    const bool package = use == ManifestUse::package;
    MapEntry map;
    bool stem_known = false;
    const oamod::Node* stem = oamod::find_entry(entry, "stem");
    const std::string stem_key = prefix + ".stem";
    if (stem == nullptr || stem->kind != oamod::NodeKind::string || !valid_stem(stem->text)) {
        add_problem(
            problems,
            stem != nullptr ? stem->position : entry.position,
            stem_key,
            "stem must be 1 to " + std::to_string(most_stem_bytes) +
                " bytes of letters, digits, space and the marks _ - . ' ( ), not starting or "
                "ending with a space or a dot"
        );
    } else {
        map.stem = stem->text;
        stem_known = true;
        const std::string fold = folded(stem->text);
        bool repeated = false;
        for (const std::string& earlier : stems) {
            if (earlier == fold) {
                repeated = true;
                break;
            }
        }
        if (repeated) {
            add_problem(
                problems, stem->position, stem_key, "stem repeats " + stem->text + ", ignoring case"
            );
        } else {
            stems.push_back(fold);
        }
        if (id_known) {
            const std::string game_name = pack_map_name(stem->text, id);
            if (!map_name_fits(game_name)) {
                add_problem(
                    problems,
                    stem->position,
                    stem_key,
                    game_name + " is " + std::to_string(game_name.size()) +
                        " bytes; a map's name must fit in " + std::to_string(most_map_name_bytes) +
                        " bytes to travel in the battle room and in saved games"
                );
            }
        }
    }

    const auto require_or_read = [&](std::string_view name, bool required) -> const oamod::Node* {
        const oamod::Node* node = oamod::find_entry(entry, name);
        if (node == nullptr && required) {
            add_problem(
                problems,
                entry.position,
                prefix + "." + std::string{name},
                std::string{name} + " is required"
            );
        }
        return node;
    };

    if (const oamod::Node* title = require_or_read("name", package))
        one_line_text(*title, prefix + ".name", most_title_characters, problems, map.title);
    if (const oamod::Node* description = oamod::find_entry(entry, "description")) {
        const std::string key = prefix + ".description";
        if (description->kind != oamod::NodeKind::string) {
            add_problem(problems, description->position, key, "description must be a string");
        } else {
            map.description = description->text;
            if (!one_line(description->text))
                add_problem(problems, description->position, key, "description must be one line");
            if (description->text.size() > most_description_bytes) {
                add_problem(
                    problems,
                    description->position,
                    key,
                    "description must be at most " + std::to_string(most_description_bytes) +
                        " bytes (it has " + std::to_string(description->text.size()) + ")"
                );
            }
        }
    }
    if (const oamod::Node* players = require_or_read("players", package)) {
        const std::string key = prefix + ".players";
        int64_t number = 0;
        if (players->kind != oamod::NodeKind::number ||
            !oamod::integer_value(players->number, number) || number < least_players ||
            number > most_players) {
            add_problem(
                problems,
                players->position,
                key,
                "players must be an integer from " + std::to_string(least_players) + " to " +
                    std::to_string(most_players)
            );
        } else {
            map.players = static_cast<int32_t>(number);
        }
    }
    if (const oamod::Node* size = require_or_read("size", package)) {
        const std::string key = prefix + ".size";
        if (size->kind != oamod::NodeKind::string || !map_size(size->text)) {
            add_problem(
                problems,
                size->position,
                key,
                "size must be <width>x<height>, each side an integer from 1 to " +
                    std::to_string(most_map_side)
            );
        } else {
            map.size = size->text;
        }
    }
    if (const oamod::Node* preview = oamod::find_entry(entry, "preview"))
        read_preview(*preview, prefix + ".preview", map.preview, problems);
    if (const oamod::Node* files = require_or_read("files", package))
        read_files(*files, index, map.stem, stem_known, map.files, problems);
    manifest.maps.push_back(std::move(map));
}

/// Reads the author block.
///
/// @param node the author value
/// @param[out] author the author
/// @param[in,out] problems the problems
void read_author(const oamod::Node& node, Author& author, std::vector<Problem>& problems) {
    if (node.kind != oamod::NodeKind::mapping) {
        add_problem(
            problems,
            node.position,
            "author",
            "author must be an object with a name and an optional email"
        );
        return;
    }
    refuse_unknown_keys(node, author_keys, "author", problems);
    const oamod::Node* name = oamod::find_entry(node, "name");
    if (name == nullptr || name->kind != oamod::NodeKind::string || name->text.empty() ||
        name->text.size() > most_author_name_bytes) {
        add_problem(
            problems,
            name != nullptr ? name->position : node.position,
            "author.name",
            "author.name must be a string of 1 to " + std::to_string(most_author_name_bytes) +
                " bytes"
        );
    } else {
        author.name = name->text;
    }
    const oamod::Node* email = oamod::find_entry(node, "email");
    if (email == nullptr)
        return;
    if (email->kind != oamod::NodeKind::string || !email_address(email->text)) {
        add_problem(
            problems,
            email->position,
            "author.email",
            "author.email must be an e-mail address such as name@example.com"
        );
    } else {
        author.email = email->text;
    }
}

/// Reads the packaging block.
///
/// @param node the packaging value
/// @param[out] packaging the packaging
/// @param[in,out] problems the problems
void read_packaging(const oamod::Node& node, Packaging& packaging, std::vector<Problem>& problems) {
    if (node.kind != oamod::NodeKind::mapping) {
        add_problem(
            problems,
            node.position,
            "packaging",
            "packaging must be an object with a revision, a date and a packager"
        );
        return;
    }
    refuse_unknown_keys(node, packaging_keys, "packaging", problems);
    const oamod::Node* revision = oamod::find_entry(node, "revision");
    int64_t number = 0;
    if (revision == nullptr || revision->kind != oamod::NodeKind::number ||
        !oamod::integer_value(revision->number, number) || number < least_revision ||
        number > most_revision) {
        add_problem(
            problems,
            revision != nullptr ? revision->position : node.position,
            "packaging.revision",
            "packaging.revision must be an integer from " + std::to_string(least_revision) +
                " to " + std::to_string(most_revision)
        );
    } else {
        packaging.revision = number;
    }
    const oamod::Node* date = oamod::find_entry(node, "date");
    if (date == nullptr || date->kind != oamod::NodeKind::string || !calendar_date(date->text)) {
        add_problem(
            problems,
            date != nullptr ? date->position : node.position,
            "packaging.date",
            "packaging.date must be an ISO 8601 date, YYYY-MM-DD"
        );
    } else {
        packaging.date = date->text;
    }
    const oamod::Node* packager = oamod::find_entry(node, "packager");
    if (packager == nullptr || packager->kind != oamod::NodeKind::string ||
        character_count(packager->text) < 1 ||
        character_count(packager->text) > most_packager_characters) {
        const std::size_t count = packager != nullptr && packager->kind == oamod::NodeKind::string
                                      ? character_count(packager->text)
                                      : 0;
        add_problem(
            problems,
            packager != nullptr ? packager->position : node.position,
            "packaging.packager",
            "packaging.packager must be 1 to " + std::to_string(most_packager_characters) +
                " characters (it has " + std::to_string(count) + ")"
        );
    } else {
        packaging.packager = packager->text;
    }
}

/// Reads the requires block's base. The engine requirement is read apart, by
/// the shared package-key rules.
///
/// @param node the requires value
/// @param[out] base the base, when it is the one game data a pack builds on
/// @param[in,out] problems the problems
/// @return the block when it is a mapping, so the engine key can be read from it
const oamod::Node*
read_requires(const oamod::Node& node, std::string& base, std::vector<Problem>& problems) {
    if (node.kind != oamod::NodeKind::mapping) {
        add_problem(problems, node.position, "requires", "requires takes base and engine");
        return nullptr;
    }
    refuse_unknown_keys(node, requires_keys, "requires", problems);
    const oamod::Node* base_node = oamod::find_entry(node, "base");
    if (base_node == nullptr)
        return &node;
    if (base_node->kind != oamod::NodeKind::string || base_node->text != base_game) {
        add_problem(
            problems,
            base_node->position,
            "requires.base",
            "requires.base must be " + std::string{base_game}
        );
    } else {
        base = base_node->text;
    }
    return &node;
}

/// Reads the header and the maps.
///
/// @param root the document
/// @param use whether a map entry must be complete
/// @param[out] manifest the manifest
/// @param[in,out] problems the problems
void read_document_manifest(
    const oamod::Node& root, ManifestUse use, Manifest& manifest, std::vector<Problem>& problems
) {
    refuse_unknown_keys(root, manifest_keys, {}, problems);
    const oamod::Node* format = oamod::find_entry(root, "oamap");
    int64_t format_number = 0;
    if (format == nullptr || format->kind != oamod::NodeKind::number ||
        !oamod::integer_value(format->number, format_number) || format_number != format_version) {
        add_problem(
            problems,
            format != nullptr ? format->position : root.position,
            "oamap",
            "oamap must be " + std::to_string(format_version)
        );
    } else {
        manifest.format = format_number;
    }

    const oamod::Node* id = oamod::find_entry(root, "id");
    bool id_known = false;
    if (id == nullptr || id->kind != oamod::NodeKind::string || !valid_pack_id(id->text)) {
        add_problem(
            problems,
            id != nullptr ? id->position : root.position,
            "id",
            "id must be 1 to " + std::to_string(most_id_bytes) + " bytes of lower-case kebab-case"
        );
    } else {
        manifest.id = id->text;
        id_known = true;
    }

    if (const oamod::Node* name = oamod::find_entry(root, "name"))
        one_line_text(*name, "name", most_pack_name_characters, problems, manifest.name);
    else
        add_problem(problems, root.position, "name", "name is required");

    if (const oamod::Node* version = oamod::find_entry(root, "version"))
        one_line_text(*version, "version", most_version_characters, problems, manifest.version);
    else
        add_problem(problems, root.position, "version", "version is required");

    if (const oamod::Node* description = oamod::find_entry(root, "description")) {
        if (description->kind != oamod::NodeKind::string) {
            add_problem(
                problems, description->position, "description", "description must be a string"
            );
        } else {
            manifest.description = description->text;
            if (!one_line(description->text)) {
                add_problem(
                    problems, description->position, "description", "description must be one line"
                );
            }
            const std::size_t count = character_count(description->text);
            if (count > most_pack_description_characters) {
                add_problem(
                    problems,
                    description->position,
                    "description",
                    "description must be at most " +
                        std::to_string(most_pack_description_characters) + " characters (it has " +
                        std::to_string(count) + ")"
                );
            }
        }
    }

    if (const oamod::Node* author = oamod::find_entry(root, "author"))
        read_author(*author, manifest.author, problems);
    else
        add_problem(problems, root.position, "author", "author is required");

    if (const oamod::Node* packaging = oamod::find_entry(root, "packaging"))
        read_packaging(*packaging, manifest.packaging, problems);
    else
        add_problem(problems, root.position, "packaging", "packaging is required");

    const oamod::Node* requires_node = oamod::find_entry(root, "requires");
    const oamod::Node* requires_block = nullptr;
    if (requires_node != nullptr)
        requires_block = read_requires(*requires_node, manifest.requires_base, problems);

    oamod::PackageKeys keys;
    std::vector<oamod::KeyProblem> key_problems;
    oamod::read_package_keys(root, requires_block, keys, key_problems);
    for (const oamod::KeyProblem& problem : key_problems) {
        add_problem(problems, problem.position, problem.path, problem.message);
    }
    manifest.homepage = std::move(keys.homepage);
    manifest.tags = std::move(keys.tags);
    manifest.requires_engine = std::move(keys.requires_engine);

    const oamod::Node* maps = oamod::find_entry(root, "maps");
    if (maps == nullptr || maps->kind != oamod::NodeKind::sequence) {
        add_problem(
            problems,
            maps != nullptr ? maps->position : root.position,
            "maps",
            "maps must be a list of 1 to " + std::to_string(most_maps) + " maps"
        );
        return;
    }
    if (maps->children.empty() || maps->children.size() > most_maps) {
        add_problem(
            problems,
            maps->position,
            "maps",
            "maps must list 1 to " + std::to_string(most_maps) + " maps (it lists " +
                std::to_string(maps->children.size()) + ")"
        );
    }
    std::vector<std::string> stems;
    for (std::size_t index = 0; index < maps->children.size(); ++index) {
        read_map(
            maps->children[index], index, use, manifest.id, id_known, manifest, stems, problems
        );
    }
}

} // namespace

bool read_manifest(
    std::span<const uint8_t> bytes,
    ManifestUse use,
    Manifest& manifest,
    std::vector<Problem>& problems
) {
    oamod::Node root;
    oamod::ReadError error{};
    std::vector<Problem> found;
    if (!oamod::read_document(bytes, root, error)) {
        add_problem(found, error.position, "document", oamod::rule_message(error.rule));
    } else {
        Manifest read;
        read_document_manifest(root, use, read, found);
        if (found.empty()) {
            manifest = std::move(read);
            return true;
        }
    }
    problems.insert(problems.end(), found.begin(), found.end());
    return false;
}

std::string describe(const Problem& problem) {
    return std::string{manifest_file} + ":" + std::to_string(problem.line) + ":" +
           std::to_string(problem.column) + ": " + problem.key + ": " + problem.message;
}

} // namespace oa::data::map_pack
