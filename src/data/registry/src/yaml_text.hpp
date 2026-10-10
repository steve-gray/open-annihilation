// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Quoting and reading helpers shared by the descriptor and the player's
// registry file. Not for callers outside this module.
#pragma once

#include "oa/data/registry/descriptor.hpp"
#include "oa/formats/oamod.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::registry {

/// Appends a double-quoted scalar, escaping quotes, backslashes and controls.
///
/// @param[in,out] out the text to append to
/// @param text the scalar's value
void append_quoted(std::string& out, std::string_view text);

/// The sentence for a YAML text that could not be read, with its line and column.
///
/// @param error the reader's refusal
/// @return the sentence
[[nodiscard]] std::string yaml_failure(const formats::oamod::ReadError& error);

/// Reports whether a day is YYYY-MM-DD, with a month and a day in range.
///
/// @param text the day
/// @return true when it is that form
[[nodiscard]] bool valid_date(std::string_view text) noexcept;

/// Reports whether text holds a control character.
///
/// @param text the text, as UTF-8
/// @return true when a character is in U+0000 to U+001F or U+007F to U+009F
[[nodiscard]] bool control_character(std::string_view text) noexcept;

/// Reads a registry name.
///
/// @param node the field's node
/// @param[out] out the name; unchanged on failure
/// @param[out] error why it was refused
/// @param where the field's path, for the refusal
/// @return true when the name was read
[[nodiscard]] bool take_name(
    const formats::oamod::Node& node, std::string& out, std::string& error, std::string_view where
);

/// Reads a registry id.
///
/// @param node the field's node
/// @param[out] out the id; unchanged on failure
/// @param[out] error why it was refused
/// @param where the field's path, for the refusal
/// @return true when the id was read
[[nodiscard]] bool take_registry_id(
    const formats::oamod::Node& node, std::string& out, std::string& error, std::string_view where
);

/// Reads an http address with no query.
///
/// @param node the field's node
/// @param[out] out the address; unchanged on failure
/// @param[out] error why it was refused
/// @param where the field's path, for the refusal
/// @return true when the address was read
[[nodiscard]] bool take_http_url(
    const formats::oamod::Node& node,
    formats::url::Url& out,
    std::string& error,
    std::string_view where
);

/// Reads an http or https address.
///
/// @param node the field's node
/// @param[out] out the address; unchanged on failure
/// @param[out] error why it was refused
/// @param where the field's path, for the refusal
/// @return true when the address was read
[[nodiscard]] bool take_web_url(
    const formats::oamod::Node& node,
    formats::url::Url& out,
    std::string& error,
    std::string_view where
);

/// Reads a list of http addresses with no query.
///
/// @param node the field's node
/// @param[out] out the addresses; unchanged on failure
/// @param[out] error why it was refused
/// @param where the field's path, for the refusal
/// @param max_count the most addresses the list may hold
/// @return true when the list was read
[[nodiscard]] bool take_mirrors(
    const formats::oamod::Node& node,
    std::vector<formats::url::Url>& out,
    std::string& error,
    std::string_view where,
    std::size_t max_count
);

/// Reads a list of signing keys, at most max_registry_keys.
///
/// Ids and public keys are each unique. An empty list is an unsigned registry.
///
/// @param node the field's node
/// @param[out] out the keys; unchanged on failure
/// @param[out] error why it was refused
/// @param where the field's path, for the refusal
/// @return true when the list was read
[[nodiscard]] bool take_keys(
    const formats::oamod::Node& node,
    std::vector<RegistryKey>& out,
    std::string& error,
    std::string_view where
);

/// Reads a downloads block onto a descriptor's mode, API and install id.
///
/// @param node the field's node
/// @param[in,out] descriptor the descriptor; its download fields change only on success
/// @param[out] error why it was refused
/// @param where the field's path, for the refusal
/// @return true when the block was read
[[nodiscard]] bool take_downloads(
    const formats::oamod::Node& node,
    Descriptor& descriptor,
    std::string& error,
    std::string_view where
);

/// Appends a block of addresses under a key, or an empty list.
///
/// @param[in,out] out the text to append to
/// @param key the field's name
/// @param urls the addresses
/// @param indent the spaces before the key
void append_urls(
    std::string& out,
    std::string_view key,
    const std::vector<formats::url::Url>& urls,
    std::string_view indent
);

/// Appends a block of keys under a key, or an empty list.
///
/// @param[in,out] out the text to append to
/// @param key the field's name
/// @param keys the keys
/// @param indent the spaces before the key
void append_keys(
    std::string& out,
    std::string_view key,
    const std::vector<RegistryKey>& keys,
    std::string_view indent
);

/// Appends a downloads block.
///
/// @param[in,out] out the text to append to
/// @param descriptor the descriptor
/// @param indent the spaces before the key
/// @param flow true to write the block on one line
void append_downloads(
    std::string& out, const Descriptor& descriptor, std::string_view indent, bool flow
);

/// Reports whether writing a descriptor and reading it back yields the same values.
///
/// @param descriptor the descriptor
/// @param[out] error why it does not store; unchanged when it does
/// @return true when the descriptor stores
[[nodiscard]] bool descriptor_stores(const Descriptor& descriptor, std::string& error);

/// What a read of a whole file did.
enum class FileRead : uint8_t {
    missing,    ///< there is no file
    read,       ///< the bytes were read
    unreadable, ///< the file is there and could not be read
};

/// Reads a whole file, refusing one longer than the limit.
///
/// @param file the file's path
/// @param[out] bytes the bytes; cleared when the file is missing or refused
/// @param[out] error why the file was refused; unchanged when it was read or missing
/// @param max_bytes the most bytes accepted
/// @return missing, read or unreadable
[[nodiscard]] FileRead read_file_bytes(
    const std::filesystem::path& file,
    std::vector<uint8_t>& bytes,
    std::string& error,
    std::size_t max_bytes
);

/// A path's UTF-8 spelling, for a refusal.
///
/// @param path the path
/// @return its UTF-8 spelling, or empty when it has none
[[nodiscard]] std::string path_text(const std::filesystem::path& path);

/// Joins a prefix and a field with a dot. An empty prefix is the field alone.
///
/// @param prefix the path so far; empty at the top
/// @param field the field's name
/// @return the path
[[nodiscard]] std::string field_path(std::string_view prefix, std::string_view field);

} // namespace oa::data::registry
