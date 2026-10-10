// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What a map places and what one feature section uses. Names keep the
// spelling they were written with; a caller folds ASCII case to compare them.

#include "oa/data/map_fit/map_fit.hpp"

#include "oa/formats/tdf.hpp"
#include "oa/formats/tnt.hpp"

#include <string>
#include <string_view>

namespace oa::data::map_fit {

namespace defs = oa::data::defs;

namespace {

/// The feature links a section is followed through, in the order the game loads them.
constexpr const char* link_keys[] = {"featuredead", "featurereclamate", "featureburnt"};

/// Returns a field's text, or empty when the field is absent or empty.
///
/// @param section the feature section
/// @param key the field
/// @return the value, or empty
std::string_view field(const formats::tdf::Block& section, const char* key) {
    const char* value = formats::tdf::find_value(&section, key);
    if (value == nullptr || value[0] == '\0')
        return {};
    return value;
}

/// Writes a reason and reports that the map's uses could not be read.
///
/// @param error receives the reason, if not null
/// @param reason the reason
/// @return false
bool fail(std::string* error, std::string reason) {
    if (error != nullptr)
        *error = std::move(reason);
    return false;
}

/// Tells whether a section name is Schema N for this number, ignoring ASCII case.
///
/// Schema 00 is not Schema 0: the digits must be the canonical decimal.
///
/// @param name the section name
/// @param number the schema number
/// @return true when the name is that schema
bool is_schema(std::string_view name, int number) {
    constexpr std::string_view prefix = "Schema ";
    if (name.size() != prefix.size() + std::to_string(number).size())
        return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const auto left = static_cast<unsigned char>(name[index]);
        const auto right = static_cast<unsigned char>(prefix[index]);
        const auto folded_left = left >= 'A' && left <= 'Z' ? left - 'A' + 'a' : left;
        const auto folded_right = right >= 'A' && right <= 'Z' ? right - 'A' + 'a' : right;
        if (folded_left != folded_right)
            return false;
    }
    const auto digits = std::to_string(number);
    return name.substr(prefix.size()) == digits;
}

/// Finds Schema N among a header's sections.
///
/// @param header the GlobalHeader section
/// @param number the schema number
/// @return the section, or null when this schema is not written
const formats::tdf::Block* find_schema(const formats::tdf::Block* header, int number) {
    for (uint32_t index = 0; index < formats::tdf::child_count(header); ++index) {
        const auto* child = formats::tdf::child_at(header, index);
        if (child != nullptr && child->name != nullptr && is_schema(child->name, number))
            return child;
    }
    return nullptr;
}

/// Appends the feature names one schema places.
///
/// A feature with no coordinates, or with XPos or ZPos below zero, is not
/// placed: a mission load drops it the same way.
///
/// @param schema the schema section
/// @param[in,out] uses receives the names
/// @param where the text that names this schema's features
void append_schema_features(
    const formats::tdf::Block* schema, MapUses& uses, const std::string& where
) {
    const auto* features = formats::tdf::find_child(schema, "features");
    for (uint32_t index = 0; index < formats::tdf::child_count(features); ++index) {
        const auto* entry = formats::tdf::child_at(features, index);
        const auto* name = formats::tdf::find_value(entry, "Featurename");
        if (name == nullptr || name[0] == '\0')
            continue;
        if (formats::tdf::get_int(entry, "XPos", -1) < 0 ||
            formats::tdf::get_int(entry, "ZPos", -1) < 0)
            continue;
        uses.features.emplace_back(name);
        uses.features_from.push_back(where);
    }
}

/// Appends the unit names one schema places.
///
/// Only Unitname places a unit. An empty name is not a unit.
///
/// @param schema the schema section
/// @param section_name the layout's unit section
/// @param[in,out] uses receives the names
/// @param where the text that names this schema's units
void append_schema_units(
    const formats::tdf::Block* schema,
    const std::string& section_name,
    MapUses& uses,
    const std::string& where
) {
    const auto* units = formats::tdf::find_child(schema, section_name.c_str());
    for (uint32_t index = 0; index < formats::tdf::child_count(units); ++index) {
        const auto* entry = formats::tdf::child_at(units, index);
        const auto* name = formats::tdf::find_value(entry, "Unitname");
        if (name == nullptr || name[0] == '\0')
            continue;
        uses.units.emplace_back(name);
        uses.units_from.push_back(where);
    }
}

} // namespace

SectionReferences references_of(const formats::tdf::Block& section) {
    SectionReferences references;
    const auto object = field(section, "object");
    if (object.empty()) {
        const auto filename = field(section, "filename");
        if (!filename.empty())
            references.gaf = "anims/" + std::string(filename) + ".gaf";
    } else {
        references.model = "objects3d/" + std::string(object) + ".3do";
    }
    const auto weapon = field(section, "burnweapon");
    if (!weapon.empty())
        references.burn_weapon = std::string(weapon);
    for (const char* key : link_keys) {
        const auto linked = field(section, key);
        if (!linked.empty())
            references.features.emplace_back(linked);
    }
    return references;
}

bool uses_of(
    std::span<const uint8_t> ota,
    std::span<const uint8_t> tnt,
    const defs::DataLayout& layout,
    MapUses& uses,
    std::string* error
) {
    formats::tdf::OwnedDocument document;
    formats::tdf::ParseError parse_error{};
    const std::string_view ota_text(
        ota.empty() ? "" : reinterpret_cast<const char*>(ota.data()), ota.size()
    );
    if (!document.parse(ota_text, &parse_error))
        return fail(error, "OTA: " + formats::tdf::describe(parse_error));

    const auto parsed = formats::tnt::parse(tnt);
    if (!parsed.ok()) {
        std::string message = "the map could not be read";
        if (parsed.error && !parsed.error->message.empty())
            message = parsed.error->message;
        return fail(error, "TNT: " + message);
    }

    MapUses found;
    for (const auto& feature : parsed.map->features) {
        if (feature.name.empty())
            continue;
        found.features.push_back(feature.name);
        found.features_from.emplace_back("the TNT");
    }

    // Schema 0, Schema 1, ... and stop at the first number that is not written.
    // A schema past that gap is never played, so its names are not uses. The
    // scan is not limited to the schema a player count would pick.
    const auto* header = formats::tdf::find_child(document.root(), "GlobalHeader");
    for (int number = 0; header != nullptr; ++number) {
        const auto* schema = find_schema(header, number);
        if (schema == nullptr)
            break;
        const auto label = std::to_string(number);
        append_schema_features(schema, found, "Schema " + label + "'s features");
        append_schema_units(
            schema, layout.map_units_section, found, "Schema " + label + "'s units"
        );
    }

    uses.features.insert(uses.features.end(), found.features.begin(), found.features.end());
    uses.features_from.insert(
        uses.features_from.end(), found.features_from.begin(), found.features_from.end()
    );
    uses.units.insert(uses.units.end(), found.units.begin(), found.units.end());
    uses.units_from.insert(uses.units_from.end(), found.units_from.begin(), found.units_from.end());
    return true;
}

} // namespace oa::data::map_fit
