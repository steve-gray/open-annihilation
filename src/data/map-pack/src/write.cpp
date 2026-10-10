// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/map_pack/manifest.hpp"

#include <string>

namespace oa::data::map_pack {

namespace {

/// Appends a line at an indentation of two spaces per level.
///
/// @param[in,out] out the text
/// @param indent how many levels in the line sits
/// @param text the line, without its line break
void line(std::string& out, int indent, std::string_view text) {
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
    out.append(text);
    out.push_back('\n');
}

/// Writes a string as a double-quoted YAML scalar.
///
/// `"` and `\` are escaped, and a control character is written with a
/// backslash escape the manifest reader accepts.
///
/// @param text the string
/// @return the quoted scalar, quotes included
std::string quoted(std::string_view text) {
    std::string out;
    out.push_back('"');
    for (const unsigned char character : text) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\x";
                out.push_back(hex[character >> 4]);
                out.push_back(hex[character & 0x0F]);
            } else {
                out.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

/// Writes one `key: value` line.
///
/// @param[in,out] out the text
/// @param indent how many levels in the key sits
/// @param key the key
/// @param value the value, already in the form to write
void field(std::string& out, int indent, std::string_view key, std::string_view value) {
    line(out, indent, std::string{key} + ": " + std::string{value});
}

} // namespace

std::string write_manifest(const Manifest& manifest) {
    std::string out;
    field(out, 0, "oamap", std::to_string(manifest.format));
    field(out, 0, "id", quoted(manifest.id));
    field(out, 0, "name", quoted(manifest.name));
    field(out, 0, "version", quoted(manifest.version));
    if (!manifest.description.empty())
        field(out, 0, "description", quoted(manifest.description));
    if (!manifest.homepage.empty())
        field(out, 0, "homepage", quoted(manifest.homepage));
    if (!manifest.tags.empty()) {
        line(out, 0, "tags:");
        for (const std::string& tag : manifest.tags)
            line(out, 1, "- " + quoted(tag));
    }
    line(out, 0, "author:");
    field(out, 1, "name", quoted(manifest.author.name));
    if (!manifest.author.email.empty())
        field(out, 1, "email", quoted(manifest.author.email));
    line(out, 0, "packaging:");
    field(out, 1, "revision", std::to_string(manifest.packaging.revision));
    field(out, 1, "date", quoted(manifest.packaging.date));
    field(out, 1, "packager", quoted(manifest.packaging.packager));
    if (!manifest.requires_base.empty() || !manifest.requires_engine.empty()) {
        line(out, 0, "requires:");
        if (!manifest.requires_base.empty())
            field(out, 1, "base", quoted(manifest.requires_base));
        if (!manifest.requires_engine.empty())
            field(out, 1, "engine", quoted(manifest.requires_engine));
    }
    line(out, 0, "maps:");
    for (const MapEntry& map : manifest.maps) {
        line(out, 1, "- stem: " + quoted(map.stem));
        field(out, 2, "name", quoted(map.title));
        if (!map.description.empty())
            field(out, 2, "description", quoted(map.description));
        field(out, 2, "players", std::to_string(map.players));
        field(out, 2, "size", quoted(map.size));
        if (!map.preview.empty())
            field(out, 2, "preview", quoted(map.preview));
        line(out, 2, "files:");
        for (const std::string& path : map.files)
            line(out, 3, "- " + quoted(path));
    }
    return out;
}

} // namespace oa::data::map_pack
