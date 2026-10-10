// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/registry/descriptor.hpp"

#include "yaml_text.hpp"

namespace oa::data::registry {
namespace {

namespace oamod = formats::oamod;

bool word_byte(char letter) noexcept {
    return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9');
}

/// Reads one descriptor mapping.
///
/// @param root the mapping
/// @param[out] out the descriptor; unchanged on failure
/// @param[out] error why it was refused
/// @param where the mapping's path, for the refusal
/// @return true when the mapping was read
bool read_mapping(
    const oamod::Node& root, Descriptor& out, std::string& error, std::string_view where
) {
    if (root.kind != oamod::NodeKind::mapping) {
        error = std::string(where) + " must be a mapping";
        return false;
    }
    Descriptor read;
    bool saw_registry = false;
    bool saw_id = false;
    bool saw_name = false;
    bool saw_catalogue = false;
    bool saw_downloads = false;
    for (const oamod::Node& entry : root.children) {
        const std::string& key = entry.key.text;
        if (key == "registry") {
            int64_t value = 0;
            if (entry.kind != oamod::NodeKind::number ||
                !oamod::integer_value(entry.number, value) || value != descriptor_version) {
                error = field_path(where, "registry") + " must be 1";
                return false;
            }
            saw_registry = true;
        } else if (key == "id") {
            if (!take_registry_id(entry, read.id, error, field_path(where, "id")))
                return false;
            saw_id = true;
        } else if (key == "name") {
            if (!take_name(entry, read.name, error, field_path(where, "name")))
                return false;
            saw_name = true;
        } else if (key == "homepage") {
            formats::url::Url homepage;
            if (!take_web_url(entry, homepage, error, field_path(where, "homepage")))
                return false;
            read.homepage = std::move(homepage);
        } else if (key == "catalogue") {
            if (!take_http_url(entry, read.catalogue, error, field_path(where, "catalogue")))
                return false;
            saw_catalogue = true;
        } else if (key == "mirrors") {
            if (!take_mirrors(
                    entry, read.mirrors, error, field_path(where, "mirrors"), max_registry_mirrors
                ))
                return false;
        } else if (key == "keys") {
            if (!take_keys(entry, read.keys, error, field_path(where, "keys")))
                return false;
        } else if (key == "downloads") {
            if (!take_downloads(entry, read, error, field_path(where, "downloads")))
                return false;
            saw_downloads = true;
        } else {
            error = std::string(where) + " has no key " + key;
            return false;
        }
    }
    if (!saw_registry)
        error = std::string(where) + " names no registry";
    else if (!saw_id)
        error = std::string(where) + " names no id";
    else if (!saw_name)
        error = std::string(where) + " names no name";
    else if (!saw_catalogue)
        error = std::string(where) + " names no catalogue";
    else if (!saw_downloads)
        error = std::string(where) + " names no downloads";
    else {
        out = std::move(read);
        return true;
    }
    return false;
}

} // namespace

bool valid_registry_id(std::string_view text) noexcept {
    if (text.empty() || text.size() > 32)
        return false;
    bool hyphen = true;
    for (char letter : text) {
        if (word_byte(letter))
            hyphen = false;
        else if (letter == '-' && !hyphen)
            hyphen = true;
        else
            return false;
    }
    return !hyphen;
}

bool valid_key_id(std::string_view text) noexcept {
    if (text.empty() || text.size() > 64 || !word_byte(text.front()))
        return false;
    for (char letter : text.substr(1)) {
        if (!word_byte(letter) && letter != '.' && letter != '-')
            return false;
    }
    return true;
}

bool read_descriptor(std::span<const uint8_t> bytes, Descriptor& out, std::string* error) {
    std::string failure;
    Descriptor read;
    if (bytes.size() > max_descriptor_bytes) {
        failure = "the registry descriptor is longer than 64 KiB";
    } else {
        oamod::Node root;
        oamod::ReadError read_error{};
        if (!oamod::read_document(bytes, root, read_error))
            failure = yaml_failure(read_error);
        else if (!read_mapping(root, read, failure, "the descriptor")) {
            // failure already says which key broke which rule
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

std::string descriptor_text(const Descriptor& descriptor) {
    std::string out;
    out += "registry: ";
    out += std::to_string(descriptor_version);
    out.push_back('\n');
    out += "id: ";
    append_quoted(out, descriptor.id);
    out.push_back('\n');
    out += "name: ";
    append_quoted(out, descriptor.name);
    out.push_back('\n');
    if (descriptor.homepage) {
        out += "homepage: ";
        append_quoted(out, formats::url::url_text(*descriptor.homepage));
        out.push_back('\n');
    }
    out += "catalogue: ";
    append_quoted(out, formats::url::url_text(descriptor.catalogue));
    out.push_back('\n');
    append_urls(out, "mirrors", descriptor.mirrors, "");
    append_keys(out, "keys", descriptor.keys, "");
    append_downloads(out, descriptor, "", false);
    return out;
}

std::vector<std::string> key_fingerprints(const Descriptor& descriptor) {
    std::vector<std::string> fingerprints;
    fingerprints.reserve(descriptor.keys.size());
    for (const RegistryKey& key : descriptor.keys)
        fingerprints.emplace_back(base::signing::fingerprint(key.key).view());
    return fingerprints;
}

std::string address_text(const Descriptor& descriptor) {
    std::string text = descriptor.catalogue.host;
    if (descriptor.catalogue.port != 80) {
        text.push_back(':');
        text += std::to_string(descriptor.catalogue.port);
    }
    return text;
}

} // namespace oa::data::registry
