// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/map_pack/map_name.hpp"

namespace oa::data::map_pack {

bool valid_pack_id(std::string_view text) {
    if (text.empty() || text.size() > most_id_bytes || text.front() == '-' || text.back() == '-')
        return false;
    char previous = 0;
    for (const char character : text) {
        const bool word =
            (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (!word && character != '-')
            return false;
        if (character == '-' && previous == '-')
            return false;
        previous = character;
    }
    return true;
}

bool valid_stem(std::string_view text) {
    if (text.empty() || text.size() > most_stem_bytes)
        return false;
    const unsigned char first = static_cast<unsigned char>(text.front());
    const unsigned char last = static_cast<unsigned char>(text.back());
    if (first == ' ' || first == '.' || last == ' ' || last == '.')
        return false;
    for (const unsigned char character : text) {
        const bool letter =
            (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        const bool digit = character >= '0' && character <= '9';
        const bool mark = character == ' ' || character == '_' || character == '-' ||
                          character == '.' || character == '\'' || character == '(' ||
                          character == ')';
        if (!letter && !digit && !mark)
            return false;
    }
    return true;
}

std::string pack_map_name(std::string_view stem, std::string_view id) {
    std::string name;
    name.reserve(stem.size() + 1 + id.size());
    name.append(stem);
    name.push_back(map_name_separator);
    name.append(id);
    return name;
}

std::optional<PackMapName> split_pack_map_name(std::string_view name) {
    const std::size_t split = name.rfind(map_name_separator);
    if (split == std::string_view::npos)
        return std::nullopt;
    const std::string_view stem = name.substr(0, split);
    const std::string_view id = name.substr(split + 1);
    if (!valid_stem(stem) || !valid_pack_id(id))
        return std::nullopt;
    return PackMapName{stem, id};
}

bool map_name_fits(std::string_view name) {
    return name.size() <= most_map_name_bytes;
}

} // namespace oa::data::map_pack
