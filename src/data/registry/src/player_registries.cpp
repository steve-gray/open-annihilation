// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/registry/player_registries.hpp"

#include "oa/platform/files.hpp"

#include "yaml_text.hpp"

#include <utility>

namespace oa::data::registry {
namespace {

namespace oamod = formats::oamod;

std::string at(std::string_view prefix, std::size_t index) {
    return std::string(prefix) + "[" + std::to_string(index) + "]";
}

/// Reads one added registry.
///
/// @param node the entry
/// @param[out] out the registry; unchanged on failure
/// @param[out] error why it was refused
/// @param where the entry's path
/// @return true when the entry was read
bool read_added(
    const oamod::Node& node, AddedRegistry& out, std::string& error, std::string_view where
) {
    if (node.kind != oamod::NodeKind::mapping) {
        error = std::string(where) + " must be a mapping";
        return false;
    }
    AddedRegistry read;
    bool saw_id = false;
    bool saw_name = false;
    bool saw_catalogue = false;
    bool saw_keys = false;
    bool saw_downloads = false;
    bool saw_added = false;
    bool saw_enabled = false;
    for (const oamod::Node& entry : node.children) {
        const std::string& key = entry.key.text;
        if (key == "id") {
            if (!take_registry_id(entry, read.descriptor.id, error, field_path(where, "id")))
                return false;
            saw_id = true;
        } else if (key == "name") {
            if (!take_name(entry, read.descriptor.name, error, field_path(where, "name")))
                return false;
            saw_name = true;
        } else if (key == "url") {
            formats::url::Url parsed;
            if (!take_web_url(entry, parsed, error, field_path(where, "url")))
                return false;
            read.url = formats::url::url_text(parsed);
        } else if (key == "homepage") {
            formats::url::Url homepage;
            if (!take_web_url(entry, homepage, error, field_path(where, "homepage")))
                return false;
            read.descriptor.homepage = std::move(homepage);
        } else if (key == "catalogue") {
            if (!take_http_url(
                    entry, read.descriptor.catalogue, error, field_path(where, "catalogue")
                ))
                return false;
            saw_catalogue = true;
        } else if (key == "mirrors") {
            if (!take_mirrors(
                    entry,
                    read.descriptor.mirrors,
                    error,
                    field_path(where, "mirrors"),
                    max_registry_mirrors
                ))
                return false;
        } else if (key == "player-mirrors") {
            if (!take_mirrors(
                    entry,
                    read.player_mirrors,
                    error,
                    field_path(where, "player-mirrors"),
                    oamod::max_node_count
                ))
                return false;
        } else if (key == "trusted-keys") {
            if (!take_keys(entry, read.descriptor.keys, error, field_path(where, "trusted-keys")))
                return false;
            saw_keys = true;
        } else if (key == "downloads") {
            if (!take_downloads(entry, read.descriptor, error, field_path(where, "downloads")))
                return false;
            saw_downloads = true;
        } else if (key == "added") {
            std::string day;
            if (entry.kind != oamod::NodeKind::string || !valid_date(entry.text)) {
                error = field_path(where, "added") + " must be a day, YYYY-MM-DD";
                return false;
            }
            day = entry.text;
            read.added = std::move(day);
            saw_added = true;
        } else if (key == "enabled") {
            if (entry.kind != oamod::NodeKind::boolean) {
                error = field_path(where, "enabled") + " must be true or false";
                return false;
            }
            read.enabled = entry.boolean;
            saw_enabled = true;
        } else {
            error = std::string(where) + " has no key " + key;
            return false;
        }
    }
    if (!saw_id)
        error = std::string(where) + " names no id";
    else if (!saw_name)
        error = std::string(where) + " names no name";
    else if (!saw_catalogue)
        error = std::string(where) + " names no catalogue";
    else if (!saw_keys)
        error = std::string(where) + " names no trusted-keys";
    else if (!saw_downloads)
        error = std::string(where) + " names no downloads";
    else if (!saw_added)
        error = std::string(where) + " names no added";
    else if (!saw_enabled)
        error = std::string(where) + " names no enabled";
    else {
        out = std::move(read);
        return true;
    }
    return false;
}

void append_line(std::string& out, std::string_view key, std::string_view value) {
    out += "    ";
    out.append(key);
    out += ": ";
    append_quoted(out, value);
    out.push_back('\n');
}

} // namespace

bool read_player_registries(
    std::span<const uint8_t> bytes, PlayerRegistries& out, std::string* error
) {
    std::string failure;
    PlayerRegistries read;
    if (bytes.size() > oamod::max_input_bytes) {
        failure = "Registries.yaml is longer than 256 KiB";
    } else {
        oamod::Node root;
        oamod::ReadError read_error{};
        if (!oamod::read_document(bytes, root, read_error)) {
            failure = yaml_failure(read_error);
        } else {
            for (const oamod::Node& entry : root.children) {
                if (entry.key.text == "registries") {
                    if (entry.kind != oamod::NodeKind::sequence) {
                        failure = "registries must be a list";
                        break;
                    }
                    for (std::size_t index = 0; index < entry.children.size(); ++index) {
                        AddedRegistry added;
                        if (!read_added(
                                entry.children[index], added, failure, at("registries", index)
                            ))
                            break;
                        for (const AddedRegistry& earlier : read.registries) {
                            if (earlier.descriptor.id == added.descriptor.id) {
                                failure = at("registries", index) + " repeats the id " +
                                          added.descriptor.id;
                                break;
                            }
                        }
                        if (!failure.empty())
                            break;
                        read.registries.push_back(std::move(added));
                    }
                    if (!failure.empty())
                        break;
                } else if (entry.key.text == "disabled") {
                    if (entry.kind != oamod::NodeKind::sequence) {
                        failure = "disabled must be a list";
                        break;
                    }
                    for (std::size_t index = 0; index < entry.children.size(); ++index) {
                        std::string id;
                        if (!take_registry_id(
                                entry.children[index], id, failure, at("disabled", index)
                            ))
                            break;
                        read.disabled.push_back(std::move(id));
                    }
                    if (!failure.empty())
                        break;
                } else {
                    failure = "Registries.yaml has no key " + entry.key.text;
                    break;
                }
            }
        }
    }
    if (!failure.empty()) {
        if (error != nullptr)
            *error = std::move(failure);
        return false;
    }
    out = std::move(read);
    return true;
}

std::string player_registries_text(const PlayerRegistries& registries) {
    std::string out = "# Open Annihilation's registries: the ones this player added, and the "
                      "built-in ones turned off.\n";
    if (registries.registries.empty()) {
        out += "registries: []\n";
    } else {
        out += "registries:\n";
        for (const AddedRegistry& entry : registries.registries) {
            out += "  - id: ";
            append_quoted(out, entry.descriptor.id);
            out.push_back('\n');
            append_line(out, "name", entry.descriptor.name);
            if (!entry.url.empty())
                append_line(out, "url", entry.url);
            if (entry.descriptor.homepage)
                append_line(out, "homepage", formats::url::url_text(*entry.descriptor.homepage));
            append_line(out, "catalogue", formats::url::url_text(entry.descriptor.catalogue));
            append_urls(out, "mirrors", entry.descriptor.mirrors, "    ");
            append_urls(out, "player-mirrors", entry.player_mirrors, "    ");
            append_keys(out, "trusted-keys", entry.descriptor.keys, "    ");
            append_downloads(out, entry.descriptor, "    ", true);
            append_line(out, "added", entry.added);
            out += "    enabled: ";
            out += entry.enabled ? "true\n" : "false\n";
        }
    }
    if (registries.disabled.empty()) {
        out += "disabled: []\n";
    } else {
        out += "disabled:\n";
        for (const std::string& id : registries.disabled) {
            out += "  - ";
            append_quoted(out, id);
            out.push_back('\n');
        }
    }
    return out;
}

LoadResult load_player_registries(
    const std::filesystem::path& file, PlayerRegistries& out, std::string* error
) {
    try {
        std::vector<uint8_t> bytes;
        std::string failure;
        const FileRead read = read_file_bytes(file, bytes, failure, oamod::max_input_bytes);
        if (read == FileRead::missing) {
            out = {};
            return LoadResult::missing;
        }
        if (read == FileRead::unreadable) {
            if (failure.find("longer than") != std::string::npos)
                failure = "Registries.yaml is longer than 256 KiB";
            if (error != nullptr)
                *error = std::move(failure);
            return LoadResult::unreadable;
        }
        if (!read_player_registries(bytes, out, error))
            return LoadResult::unreadable;
        return LoadResult::read;
    } catch (const std::exception& exception) {
        if (error != nullptr)
            *error = exception.what();
        return LoadResult::unreadable;
    } catch (...) {
        if (error != nullptr)
            *error = "Registries.yaml could not be read";
        return LoadResult::unreadable;
    }
}

bool save_player_registries(
    const std::filesystem::path& file, const PlayerRegistries& registries, std::string* error
) {
    try {
        PlayerRegistries existing;
        std::string failure;
        if (load_player_registries(file, existing, &failure) == LoadResult::unreadable) {
            if (error != nullptr)
                *error = failure.empty() ? std::string("Registries.yaml could not be read")
                                         : std::move(failure);
            return false;
        }
        const std::string text = player_registries_text(registries);
        const std::vector<uint8_t> bytes(text.begin(), text.end());
        return platform::replace_file(file, bytes, error);
    } catch (const std::exception& exception) {
        if (error != nullptr)
            *error = exception.what();
        return false;
    } catch (...) {
        if (error != nullptr)
            *error = "Registries.yaml could not be written";
        return false;
    }
}

} // namespace oa::data::registry
