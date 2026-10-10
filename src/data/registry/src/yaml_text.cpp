// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "yaml_text.hpp"

#include "oa/platform/files.hpp"

#include <cstdio>
#include <system_error>

namespace oa::data::registry {
namespace {

namespace oamod = formats::oamod;
namespace url = formats::url;

constexpr std::size_t max_name_bytes = 64;

/// Appends two hex digits.
///
/// @param[in,out] out the text to append to
/// @param byte the byte
void append_hex(std::string& out, unsigned char byte) {
    constexpr char digits[] = "0123456789abcdef";
    out.push_back(digits[byte >> 4]);
    out.push_back(digits[byte & 0x0f]);
}

/// Tells whether a byte continues a UTF-8 sequence.
///
/// @param byte the byte
/// @return true when it is a continuation byte
bool continuation(unsigned char byte) noexcept {
    return (byte & 0xc0) == 0x80;
}

std::string join_index(std::string_view where, std::size_t index) {
    return std::string(where) + "[" + std::to_string(index) + "]";
}

bool take_string(
    const oamod::Node& node, std::string& out, std::string& error, std::string_view where
) {
    if (node.kind != oamod::NodeKind::string) {
        error = std::string(where) + " must be text";
        return false;
    }
    out = node.text;
    return true;
}

bool same_descriptor(const Descriptor& left, const Descriptor& right) {
    if (left.id != right.id || left.name != right.name || left.catalogue != right.catalogue ||
        left.mode != right.mode || left.install_id != right.install_id ||
        left.homepage != right.homepage || left.api != right.api || left.mirrors != right.mirrors ||
        left.keys.size() != right.keys.size())
        return false;
    for (std::size_t index = 0; index < left.keys.size(); ++index) {
        if (left.keys[index].id != right.keys[index].id ||
            left.keys[index].key != right.keys[index].key)
            return false;
    }
    return true;
}

const char* mode_name(DownloadMode mode) noexcept {
    return mode == DownloadMode::tokens ? "tokens" : "direct";
}

const char* install_name(InstallIdUse install) noexcept {
    return install == InstallIdUse::required ? "required" : "none";
}

} // namespace

void append_quoted(std::string& out, std::string_view text) {
    out.push_back('"');
    for (unsigned char byte : text) {
        switch (byte) {
        case '"':
        case '\\':
            out.push_back('\\');
            out.push_back(static_cast<char>(byte));
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
        default:
            if (byte < 0x20) {
                out += "\\x";
                append_hex(out, byte);
            } else {
                out.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    out.push_back('"');
}

std::string yaml_failure(const formats::oamod::ReadError& error) {
    return std::string(oamod::rule_message(error.rule)) + " at line " +
           std::to_string(error.position.line) + ", column " +
           std::to_string(error.position.column);
}

bool valid_date(std::string_view text) noexcept {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        return false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == 4 || index == 7)
            continue;
        if (text[index] < '0' || text[index] > '9')
            return false;
    }
    const int month = (text[5] - '0') * 10 + (text[6] - '0');
    const int day = (text[8] - '0') * 10 + (text[9] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool control_character(std::string_view text) noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = bytes[index];
        if (lead <= 0x1f || (lead >= 0x7f && lead < 0xc2))
            return true;
        std::size_t width = 1;
        uint32_t code = lead;
        if (lead < 0x80) {
            ++index;
            continue;
        }
        if ((lead & 0xe0) == 0xc0) {
            width = 2;
            code = lead & 0x1f;
        } else if ((lead & 0xf0) == 0xe0) {
            width = 3;
            code = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0 && lead <= 0xf4) {
            width = 4;
            code = lead & 0x07;
        } else {
            return true;
        }
        if (index + width > text.size())
            return true;
        for (std::size_t part = 1; part < width; ++part) {
            if (!continuation(bytes[index + part]))
                return true;
            code = (code << 6) | (bytes[index + part] & 0x3f);
        }
        if ((width == 3 && code < 0x800) || (width == 4 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff) || (code >= 0x7f && code <= 0x9f))
            return true;
        index += width;
    }
    return false;
}

bool take_name(
    const oamod::Node& node, std::string& out, std::string& error, std::string_view where
) {
    std::string text;
    if (!take_string(node, text, error, where))
        return false;
    if (text.empty() || text.size() > max_name_bytes || control_character(text)) {
        error = std::string(where) + " must be 1 to 64 bytes with no control character";
        return false;
    }
    out = std::move(text);
    return true;
}

bool take_registry_id(
    const oamod::Node& node, std::string& out, std::string& error, std::string_view where
) {
    std::string text;
    if (!take_string(node, text, error, where))
        return false;
    if (!valid_registry_id(text)) {
        error = std::string(where) + " must be kebab-case of 1 to 32 bytes";
        return false;
    }
    out = std::move(text);
    return true;
}

bool take_http_url(
    const oamod::Node& node, url::Url& out, std::string& error, std::string_view where
) {
    std::string text;
    if (!take_string(node, text, error, where)) {
        error = std::string(where) + " must be an http address with no query";
        return false;
    }
    const auto parsed = url::parse_http_url(text);
    if (!parsed || url::has_query(*parsed)) {
        error = std::string(where) + " must be an http address with no query";
        return false;
    }
    out = *parsed;
    return true;
}

bool take_web_url(
    const oamod::Node& node, url::Url& out, std::string& error, std::string_view where
) {
    std::string text;
    if (!take_string(node, text, error, where)) {
        error = std::string(where) + " must be an http or https address";
        return false;
    }
    const auto parsed = url::parse_web_url(text);
    if (!parsed) {
        error = std::string(where) + " must be an http or https address";
        return false;
    }
    out = *parsed;
    return true;
}

bool take_mirrors(
    const oamod::Node& node,
    std::vector<url::Url>& out,
    std::string& error,
    std::string_view where,
    std::size_t max_count
) {
    if (node.kind != oamod::NodeKind::sequence) {
        error = std::string(where) + " must be a list";
        return false;
    }
    if (node.children.size() > max_count) {
        error = std::string(where) + " holds more than " + std::to_string(max_count) + " addresses";
        return false;
    }
    std::vector<url::Url> read;
    read.reserve(node.children.size());
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        url::Url one;
        if (!take_http_url(node.children[index], one, error, join_index(where, index)))
            return false;
        read.push_back(std::move(one));
    }
    out = std::move(read);
    return true;
}

bool take_keys(
    const oamod::Node& node,
    std::vector<RegistryKey>& out,
    std::string& error,
    std::string_view where
) {
    if (node.kind != oamod::NodeKind::sequence) {
        error = std::string(where) + " must be a list";
        return false;
    }
    if (node.children.size() > max_registry_keys) {
        error =
            std::string(where) + " holds more than " + std::to_string(max_registry_keys) + " keys";
        return false;
    }
    std::vector<RegistryKey> read;
    read.reserve(node.children.size());
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        const oamod::Node& item = node.children[index];
        const std::string path = join_index(where, index);
        if (item.kind != oamod::NodeKind::mapping) {
            error = path + " must be a mapping";
            return false;
        }
        bool saw_id = false;
        bool saw_public = false;
        RegistryKey key;
        for (const oamod::Node& field : item.children) {
            if (field.key.text == "id") {
                std::string id;
                if (!take_string(field, id, error, field_path(path, "id")))
                    return false;
                if (!valid_key_id(id)) {
                    error = field_path(path, "id") + " must be 1 to 64 bytes of a key id";
                    return false;
                }
                key.id = std::move(id);
                saw_id = true;
            } else if (field.key.text == "public") {
                std::string text;
                if (!take_string(field, text, error, field_path(path, "public")))
                    return false;
                const auto parsed = base::signing::parse_public_key(text);
                if (!parsed) {
                    error = field_path(path, "public") + " is not a public key";
                    return false;
                }
                key.key = *parsed;
                saw_public = true;
            } else {
                error = path + " has no key " + field.key.text;
                return false;
            }
        }
        if (!saw_id) {
            error = path + " names no id";
            return false;
        }
        if (!saw_public) {
            error = path + " names no public";
            return false;
        }
        for (const RegistryKey& earlier : read) {
            if (earlier.id == key.id) {
                error = path + " repeats the id " + key.id;
                return false;
            }
            if (earlier.key == key.key) {
                error = path + " repeats a public key";
                return false;
            }
        }
        read.push_back(std::move(key));
    }
    out = std::move(read);
    return true;
}

bool take_downloads(
    const oamod::Node& node, Descriptor& descriptor, std::string& error, std::string_view where
) {
    if (node.kind != oamod::NodeKind::mapping) {
        error = std::string(where) + " must be a mapping";
        return false;
    }
    bool saw_mode = false;
    bool saw_api = false;
    bool saw_install = false;
    DownloadMode mode = DownloadMode::direct;
    InstallIdUse install = InstallIdUse::none;
    std::optional<url::Url> api;
    for (const oamod::Node& field : node.children) {
        if (field.key.text == "mode") {
            std::string text;
            if (!take_string(field, text, error, field_path(where, "mode")) ||
                (text != "direct" && text != "tokens")) {
                error = field_path(where, "mode") + " must be direct or tokens";
                return false;
            }
            mode = text == "tokens" ? DownloadMode::tokens : DownloadMode::direct;
            saw_mode = true;
        } else if (field.key.text == "api") {
            url::Url parsed;
            if (!take_http_url(field, parsed, error, field_path(where, "api")))
                return false;
            api = std::move(parsed);
            saw_api = true;
        } else if (field.key.text == "install-id") {
            std::string text;
            if (!take_string(field, text, error, field_path(where, "install-id")) ||
                (text != "none" && text != "required")) {
                error = field_path(where, "install-id") + " must be required or none";
                return false;
            }
            install = text == "required" ? InstallIdUse::required : InstallIdUse::none;
            saw_install = true;
        } else {
            error = std::string(where) + " has no key " + field.key.text;
            return false;
        }
    }
    if (!saw_mode) {
        error = std::string(where) + " names no mode";
        return false;
    }
    if (mode == DownloadMode::direct) {
        if (saw_api) {
            error = field_path(where, "api") + " is refused when downloads are direct";
            return false;
        }
        if (saw_install && install == InstallIdUse::required) {
            error =
                field_path(where, "install-id") + " required is refused when downloads are direct";
            return false;
        }
        if (!saw_install)
            install = InstallIdUse::none;
        api.reset();
    } else {
        if (!saw_api) {
            error = std::string(where) + " names no api";
            return false;
        }
        if (!saw_install)
            install = InstallIdUse::required;
    }
    descriptor.mode = mode;
    descriptor.api = std::move(api);
    descriptor.install_id = install;
    return true;
}

void append_urls(
    std::string& out,
    std::string_view key,
    const std::vector<url::Url>& urls,
    std::string_view indent
) {
    out.append(indent);
    out.append(key);
    if (urls.empty()) {
        out += ": []\n";
        return;
    }
    out += ":\n";
    const std::string item = std::string(indent) + "  ";
    for (const url::Url& one : urls) {
        out.append(item);
        out += "- ";
        append_quoted(out, url::url_text(one));
        out.push_back('\n');
    }
}

void append_keys(
    std::string& out,
    std::string_view key,
    const std::vector<RegistryKey>& keys,
    std::string_view indent
) {
    out.append(indent);
    out.append(key);
    if (keys.empty()) {
        out += ": []\n";
        return;
    }
    out += ":\n";
    const std::string item = std::string(indent) + "  ";
    for (const RegistryKey& key_entry : keys) {
        out.append(item);
        out += "- {id: ";
        append_quoted(out, key_entry.id);
        out += ", public: ";
        append_quoted(out, base::signing::public_key_text(key_entry.key).view());
        out += "}\n";
    }
}

void append_downloads(
    std::string& out, const Descriptor& descriptor, std::string_view indent, bool flow
) {
    out.append(indent);
    out += "downloads:";
    if (flow) {
        out += " {mode: ";
        append_quoted(out, mode_name(descriptor.mode));
        if (descriptor.api) {
            out += ", api: ";
            append_quoted(out, url::url_text(*descriptor.api));
        }
        out += ", install-id: ";
        append_quoted(out, install_name(descriptor.install_id));
        out += "}\n";
        return;
    }
    out.push_back('\n');
    const std::string child = std::string(indent) + "  ";
    out.append(child);
    out += "mode: ";
    append_quoted(out, mode_name(descriptor.mode));
    out.push_back('\n');
    if (descriptor.api) {
        out.append(child);
        out += "api: ";
        append_quoted(out, url::url_text(*descriptor.api));
        out.push_back('\n');
    }
    out.append(child);
    out += "install-id: ";
    append_quoted(out, install_name(descriptor.install_id));
    out.push_back('\n');
}

bool descriptor_stores(const Descriptor& descriptor, std::string& error) {
    const std::string text = descriptor_text(descriptor);
    const std::vector<uint8_t> bytes(text.begin(), text.end());
    Descriptor read;
    if (!read_descriptor(bytes, read, &error))
        return false;
    if (!same_descriptor(descriptor, read)) {
        error = "the registry descriptor does not store";
        return false;
    }
    return true;
}

FileRead read_file_bytes(
    const std::filesystem::path& file,
    std::vector<uint8_t>& bytes,
    std::string& error,
    std::size_t max_bytes
) {
    std::error_code failure;
    const auto status = std::filesystem::status(file, failure);
    if (status.type() == std::filesystem::file_type::not_found ||
        failure == std::errc::no_such_file_or_directory) {
        bytes.clear();
        return FileRead::missing;
    }
    if (failure || !std::filesystem::is_regular_file(status)) {
        bytes.clear();
        error = path_text(file) + " could not be read";
        return FileRead::unreadable;
    }
    failure.clear();
    const auto size = std::filesystem::file_size(file, failure);
    if (failure) {
        bytes.clear();
        error = path_text(file) + " could not be read";
        return FileRead::unreadable;
    }
    if (size > max_bytes) {
        bytes.clear();
        error = path_text(file) + " is longer than " + std::to_string(max_bytes) + " bytes";
        return FileRead::unreadable;
    }
    std::FILE* stream = platform::open_file(file, "rb");
    if (stream == nullptr) {
        bytes.clear();
        error = path_text(file) + " could not be read";
        return FileRead::unreadable;
    }
    bytes.resize(static_cast<std::size_t>(size));
    std::size_t got = 0;
    while (got < bytes.size()) {
        const std::size_t read = std::fread(bytes.data() + got, 1, bytes.size() - got, stream);
        if (read == 0)
            break;
        got += read;
    }
    const bool failed = got != bytes.size() || std::ferror(stream) != 0;
    std::fclose(stream);
    if (failed) {
        bytes.clear();
        error = path_text(file) + " could not be read";
        return FileRead::unreadable;
    }
    return FileRead::read;
}

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::string field_path(std::string_view prefix, std::string_view field) {
    if (prefix.empty())
        return std::string(field);
    std::string out;
    out.reserve(prefix.size() + 1 + field.size());
    out.append(prefix);
    out.push_back('.');
    out.append(field);
    return out;
}

} // namespace oa::data::registry
