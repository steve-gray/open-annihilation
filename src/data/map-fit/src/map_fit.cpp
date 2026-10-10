// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The four rules a map must meet. Each pass reports every break it finds;
// the game's names are the caller's and are not collected again here.

#include "oa/data/map_fit/map_fit.hpp"

#include "oa/data/map_pack/map_name.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::data::map_fit {

namespace map_pack = oa::data::map_pack;
namespace defs = oa::data::defs;

namespace {

/// Folds ASCII letters to lower case. Anything else is kept.
///
/// @param text the text
/// @return the folded text
std::string fold_name(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        result.push_back(
            byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a')
                                       : static_cast<char>(byte)
        );
    }
    return result;
}

/// Folds a resource path the way the asset store keys one.
///
/// @param path the path
/// @return the folded path
std::string fold_path(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    bool previous_slash = true;
    for (const char raw : path) {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte == '\\' || byte == '/') {
            if (!previous_slash)
                result.push_back('/');
            previous_slash = true;
            continue;
        }
        result.push_back(
            byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte - 'A' + 'a')
                                       : static_cast<char>(byte)
        );
        previous_slash = false;
    }
    if (!result.empty() && result.back() == '/')
        result.pop_back();
    return result;
}

/// Returns a host path's UTF-8 spelling.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// Views file bytes as text. Empty bytes are an empty view.
///
/// @param bytes the bytes
/// @return the same bytes as text
std::string_view text_of(const std::vector<uint8_t>& bytes) {
    if (bytes.empty())
        return {};
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Tells whether a path is one of the map's feature TDFs.
///
/// @param path the path
/// @return true for a file under features/ whose name ends in .tdf
bool is_feature_tdf(std::string_view path) {
    const auto folded = fold_path(path);
    return folded.starts_with("features/") && folded.ends_with(".tdf");
}

/// The stem of a listed unit file, with its extension removed.
///
/// @param path the listed path, already folded
/// @param suffix the folded extension, including the dot
/// @return the stem, or empty when the file has none
std::string stem_of(std::string_view path, std::string_view suffix) {
    const auto slash = path.find_last_of('/');
    auto file = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (!suffix.empty() && file.size() > suffix.size() && file.ends_with(suffix))
        file.remove_suffix(suffix.size());
    return std::string(file);
}

/// One feature section the map defines, in the spelling it was written with.
struct DefinedSection {
    std::string name{};
    const formats::tdf::Block* block{};
};

/// The map's feature documents and the first section of each folded name.
struct MapDefinitions {
    std::vector<formats::tdf::OwnedDocument> documents{};
    std::map<std::string, DefinedSection> defined{};
};

/// Reads one game TDF, or records it as unreadable.
///
/// @param store the game
/// @param path the resource
/// @param[in,out] names receives the path when the file cannot be used
/// @return the document, empty when the file was skipped
formats::tdf::OwnedDocument
read_document(const oa::AssetStore& store, const std::string& path, GameNames& names) {
    formats::tdf::OwnedDocument document;
    std::vector<uint8_t> bytes;
    try {
        bytes = store.read(path).bytes;
    } catch (const std::exception&) {
        names.unreadable.push_back(path);
        return document;
    }
    formats::tdf::ParseError error{};
    if (!document.parse(text_of(bytes), &error) || document.root() == nullptr)
        names.unreadable.push_back(path);
    return document;
}

/// Records the first occurrence of each top-level feature section.
///
/// @param store the game
/// @param path the feature TDF
/// @param[in,out] names receives the sections, or the path when it did not parse
void read_feature_file(const oa::AssetStore& store, const std::string& path, GameNames& names) {
    auto document = read_document(store, path, names);
    if (document.root() == nullptr)
        return;
    for (uint32_t index = 0; index < formats::tdf::child_count(document.root()); ++index) {
        const auto* section = formats::tdf::child_at(document.root(), index);
        if (section == nullptr || section->name == nullptr || section->name[0] == '\0')
            continue;
        const auto folded = fold_name(section->name);
        if (!names.feature_sections.contains(folded))
            names.feature_sections.emplace(folded, path);
    }
}

/// Records the top-level weapon sections of one file.
///
/// @param store the game
/// @param path the weapon TDF
/// @param[in,out] names receives the weapons, or the path when it did not parse
void read_weapon_file(const oa::AssetStore& store, const std::string& path, GameNames& names) {
    auto document = read_document(store, path, names);
    if (document.root() == nullptr)
        return;
    for (uint32_t index = 0; index < formats::tdf::child_count(document.root()); ++index) {
        const auto* section = formats::tdf::child_at(document.root(), index);
        if (section == nullptr || section->name == nullptr || section->name[0] == '\0')
            continue;
        names.weapons.insert(fold_name(section->name));
    }
}

/// Names where a file the game already provides comes from.
///
/// The detail is never empty: the providing archive or loose file, else the
/// provider's identity, else a word that still says the game has it.
///
/// @param store the game
/// @param path the resource
/// @return the provider, as text
std::string provider_detail(const oa::AssetStore& store, std::string_view path) {
    try {
        const auto provider = store.provider(path);
        auto text = utf8_text(provider.path);
        if (!text.empty())
            return text;
        if (!provider.identity.empty())
            return provider.identity;
    } catch (const std::exception& error) {
        if (error.what() != nullptr && error.what()[0] != '\0')
            return error.what();
    }
    return "the game";
}

/// Reports Isolated when the mounted layer is not exactly this map's files.
///
/// Paths are compared folded, so spelling and order do not matter. A check
/// that is not reading the layer does not ask what is mounted.
///
/// @param store the game
/// @param map the map
/// @param id the pack id
/// @param files the map's files
/// @param[in,out] failures receives the failure
void check_isolated(
    const oa::AssetStore& store,
    const map_pack::MapEntry& map,
    std::string_view id,
    const MapFiles& files,
    std::vector<Failure>& failures
) {
    if (!files.from_layer())
        return;
    std::set<std::string> mounted;
    for (const auto& path : store.pack_layer_paths())
        mounted.insert(fold_path(path));
    std::set<std::string> expected;
    for (const auto& file : layer_files(map, id))
        expected.insert(fold_path(file.path));
    if (mounted == expected)
        return;
    std::string subject = "pack layer";
    if (const auto label = store.pack_layer_label())
        if (!label->empty())
            subject = *label;
    failures.push_back({Rule::isolated, std::move(subject), "another map's files are mounted"});
}

/// Reports Adds only for each of the map's paths the game already provides.
///
/// @param store the game
/// @param files the map's files
/// @param[in,out] failures receives one failure per path
void check_adds_only(
    const oa::AssetStore& store, const MapFiles& files, std::vector<Failure>& failures
) {
    for (const auto& path : files.paths()) {
        if (path.empty())
            continue;
        bool provided = false;
        try {
            provided = store.provided_above_pack_layer(path);
        } catch (const std::exception& error) {
            failures.push_back(
                {Rule::complete, path, std::string("could not be read: ") + error.what()}
            );
            continue;
        }
        if (provided)
            failures.push_back({Rule::adds_only, path, provider_detail(store, path)});
    }
}

/// Reads the map's feature TDFs: new names, parse failures and definitions.
///
/// A section the game already names is a New names failure. The map's own
/// first section of a name is what Complete follows. A file that does not
/// parse names nothing and is a Complete failure.
///
/// @param game the game's names
/// @param files the map's files
/// @param[in,out] failures receives New names and parse failures
/// @return the sections the map defines
MapDefinitions
read_map_features(const GameNames& game, const MapFiles& files, std::vector<Failure>& failures) {
    MapDefinitions definitions;
    definitions.documents.reserve(files.paths().size());
    for (const auto& path : files.paths()) {
        if (!is_feature_tdf(path))
            continue;
        const auto bytes = files.read(path);
        if (!bytes) {
            failures.push_back({Rule::complete, path, "is missing"});
            continue;
        }
        formats::tdf::OwnedDocument document;
        formats::tdf::ParseError error{};
        if (!document.parse(text_of(*bytes), &error) || document.root() == nullptr) {
            failures.push_back(
                {Rule::complete, path, "could not be read: " + formats::tdf::describe(error)}
            );
            continue;
        }
        definitions.documents.push_back(std::move(document));
        const auto* root = definitions.documents.back().root();
        for (uint32_t index = 0; index < formats::tdf::child_count(root); ++index) {
            const auto* section = formats::tdf::child_at(root, index);
            if (section == nullptr || section->name == nullptr || section->name[0] == '\0')
                continue;
            const std::string spelling(section->name);
            const auto folded = fold_name(spelling);
            const auto known = game.feature_sections.find(folded);
            if (known != game.feature_sections.end())
                failures.push_back({Rule::new_names, spelling, known->second});
            if (!definitions.defined.contains(folded))
                definitions.defined.emplace(folded, DefinedSection{spelling, section});
        }
    }
    return definitions;
}

/// A feature still to follow, and where its name was written.
struct PendingFeature {
    std::string name{};
    std::string where{};
    bool link{false};
};

/// Reports Complete for the features and units the map places.
///
/// A name the game defines is satisfied, and the game's own references are
/// not followed: the game's section is the one that loads. A name the map
/// defines is followed, including its links. A name defined nowhere fails
/// once, at the first place it was used.
///
/// @param store the game
/// @param game the game's names
/// @param files the map's files
/// @param map the map
/// @param id the pack id
/// @param layout the data layout
/// @param defined the map's own sections
/// @param[in,out] failures receives Complete failures
void check_complete(
    const oa::AssetStore& store,
    const GameNames& game,
    const MapFiles& files,
    const map_pack::MapEntry& map,
    std::string_view id,
    const defs::DataLayout& layout,
    const MapDefinitions& defined,
    std::vector<Failure>& failures
) {
    const auto ota_path = "maps/" + map_pack::pack_map_name(map.stem, id) + ".ota";
    const auto tnt_path = "maps/" + map_pack::pack_map_name(map.stem, id) + ".tnt";
    const auto ota_bytes = files.read(ota_path);
    const auto tnt_bytes = files.read(tnt_path);
    if (!ota_bytes)
        failures.push_back({Rule::complete, ota_path, "is missing"});
    if (!tnt_bytes)
        failures.push_back({Rule::complete, tnt_path, "is missing"});
    if (!ota_bytes || !tnt_bytes)
        return;

    MapUses uses;
    std::string error;
    const std::span<const uint8_t> ota_span(ota_bytes->data(), ota_bytes->size());
    const std::span<const uint8_t> tnt_span(tnt_bytes->data(), tnt_bytes->size());
    if (!uses_of(ota_span, tnt_span, layout, uses, &error)) {
        constexpr std::string_view ota_tag = "OTA: ";
        constexpr std::string_view tnt_tag = "TNT: ";
        std::string subject = ota_path;
        std::string detail = error;
        if (error.starts_with(ota_tag))
            detail = error.substr(ota_tag.size());
        else if (error.starts_with(tnt_tag)) {
            subject = tnt_path;
            detail = error.substr(tnt_tag.size());
        }
        if (detail.empty())
            detail = "the map could not be read";
        failures.push_back({Rule::complete, std::move(subject), "could not be read: " + detail});
        return;
    }

    std::set<std::string> listed;
    for (const auto& path : files.paths())
        listed.insert(fold_path(path));

    auto need_file = [&](const std::string& path, const std::string& section) {
        if (path.empty() || listed.contains(fold_path(path)))
            return;
        bool provided = false;
        try {
            provided = store.provided_above_pack_layer(path);
        } catch (const std::exception& caught) {
            failures.push_back(
                {Rule::complete, path, std::string("could not be read: ") + caught.what()}
            );
            return;
        }
        if (!provided) {
            failures.push_back(
                {Rule::complete,
                 path,
                 "is needed by the feature '" + section + "' and is not in the map or the game"}
            );
        }
    };

    std::vector<PendingFeature> pending;
    pending.reserve(uses.features.size());
    for (std::size_t index = 0; index < uses.features.size(); ++index)
        pending.push_back({uses.features[index], uses.features_from[index], false});

    std::set<std::string> visited;
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const PendingFeature item = pending[index];
        const auto folded = fold_name(item.name);
        if (folded.empty() || !visited.insert(folded).second)
            continue;
        // The game's section wins, so a map that defines the same name does
        // not supply the sprite, the model or the links that load.
        if (game.feature_sections.contains(folded))
            continue;
        const auto found = defined.defined.find(folded);
        if (found == defined.defined.end()) {
            const auto detail =
                item.link ? "is needed by the feature '" + item.where + "' and is defined nowhere"
                          : "is used in " + item.where + " and is defined nowhere";
            failures.push_back({Rule::complete, item.name, detail});
            continue;
        }
        const auto& section = found->second;
        const auto references = references_of(*section.block);
        need_file(references.gaf, section.name);
        need_file(references.model, section.name);
        if (!references.burn_weapon.empty() &&
            !game.weapons.contains(fold_name(references.burn_weapon))) {
            failures.push_back(
                {Rule::complete,
                 references.burn_weapon,
                 "is used as a burn weapon by the feature '" + section.name +
                     "' and is not in the game"}
            );
        }
        for (const auto& link : references.features)
            pending.push_back({link, section.name, true});
    }

    for (std::size_t index = 0; index < uses.units.size(); ++index) {
        const auto& unit = uses.units[index];
        if (unit.empty() || game.units.contains(fold_name(unit)))
            continue;
        const auto where =
            index < uses.units_from.size() ? uses.units_from[index] : std::string("a schema");
        failures.push_back(
            {Rule::complete, unit, "is placed in " + where + " and is not in the game"}
        );
    }
}

/// Orders failures and drops a repeated rule, subject and detail.
///
/// @param[in,out] failures the failures
void sort_unique(std::vector<Failure>& failures) {
    std::sort(failures.begin(), failures.end(), [](const Failure& left, const Failure& right) {
        if (left.rule != right.rule)
            return left.rule < right.rule;
        if (left.subject != right.subject)
            return left.subject < right.subject;
        return left.detail < right.detail;
    });
    failures.erase(
        std::unique(
            failures.begin(),
            failures.end(),
            [](const Failure& left, const Failure& right) {
                return left.rule == right.rule && left.subject == right.subject &&
                       left.detail == right.detail;
            }
        ),
        failures.end()
    );
}

} // namespace

std::string_view rule_name(Rule rule) noexcept {
    switch (rule) {
    case Rule::isolated:
        return "Isolated";
    case Rule::adds_only:
        return "Adds only";
    case Rule::new_names:
        return "New names";
    case Rule::complete:
        return "Complete";
    }
    return "an unknown rule";
}

std::string describe(const Failure& failure) {
    const auto name = std::string(rule_name(failure.rule));
    switch (failure.rule) {
    case Rule::isolated:
        return name + ": " + failure.detail + " ('" + failure.subject + "')";
    case Rule::adds_only:
        return name + ": '" + failure.subject + "' is already provided by " + failure.detail;
    case Rule::new_names:
        return name + ": the feature '" + failure.subject + "' is defined already by " +
               failure.detail;
    case Rule::complete:
        return name + ": '" + failure.subject + "' " + failure.detail;
    }
    return name + ": '" + failure.subject + "' " + failure.detail;
}

GameNames collect_game_names(const oa::AssetStore& store, const defs::DataLayout& layout) {
    GameNames names;
    const auto weapons = static_cast<std::size_t>(defs::DataDirectory::weapons);
    const auto units = static_cast<std::size_t>(defs::DataDirectory::units);
    const std::string& weapons_directory = layout.directories[weapons];
    const std::string& units_directory = layout.directories[units];
    const auto unit_suffix = "." + layout.unit_extension;

    std::vector<std::string> feature_paths;
    try {
        feature_paths = store.list_effective_recursive("features", ".tdf", false);
    } catch (const std::exception&) {
        names.unreadable.emplace_back("features");
    }
    for (const auto& path : feature_paths)
        read_feature_file(store, path, names);

    std::vector<std::string> weapon_paths;
    try {
        weapon_paths = store.list_effective(weapons_directory, ".tdf", false);
    } catch (const std::exception&) {
        names.unreadable.push_back(weapons_directory);
    }
    for (const auto& path : weapon_paths)
        read_weapon_file(store, path, names);

    std::vector<std::string> unit_paths;
    try {
        unit_paths = store.list_effective(units_directory, unit_suffix, false);
    } catch (const std::exception&) {
        names.unreadable.push_back(units_directory);
    }
    const auto folded_suffix = fold_name(unit_suffix);
    for (const auto& path : unit_paths) {
        auto stem = stem_of(path, folded_suffix);
        if (!stem.empty())
            names.units.insert(std::move(stem));
    }
    return names;
}

Fit check_map(
    const oa::AssetStore& store,
    const GameNames& game,
    const MapFiles& files,
    const map_pack::MapEntry& map,
    std::string_view id,
    const defs::DataLayout& layout
) {
    try {
        std::vector<Failure> failures;
        check_isolated(store, map, id, files, failures);
        check_adds_only(store, files, failures);
        const auto defined = read_map_features(game, files, failures);
        check_complete(store, game, files, map, id, layout, defined, failures);
        sort_unique(failures);
        return Fit{std::move(failures)};
    } catch (const std::exception& error) {
        const std::string subject = map.stem.empty() ? std::string(id) : map.stem;
        const char* what = error.what();
        Fit fit;
        fit.failures.push_back(
            {Rule::complete,
             subject.empty() ? std::string("map") : subject,
             std::string("could not be read: ") + (what == nullptr ? "" : what)}
        );
        return fit;
    }
}

} // namespace oa::data::map_fit
