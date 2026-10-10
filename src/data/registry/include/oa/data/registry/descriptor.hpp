// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A registry descriptor: where a catalogue lives, which keys sign it, and
// how its packages are downloaded. The same text is an .oareg file. Nothing
// here fetches, and nothing here opens a file.
#pragma once

#include "oa/base/signing/ed25519.hpp"
#include "oa/formats/url.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::registry {

/// The descriptor format this build reads. A file names it as registry.
inline constexpr int64_t descriptor_version = 1;

/// The extension of a registry descriptor handed out as a file.
inline constexpr std::string_view descriptor_extension = ".oareg";

/// The most bytes of a descriptor this build reads.
inline constexpr std::size_t max_descriptor_bytes = 64 * 1024;

/// The most signing keys one registry names.
inline constexpr std::size_t max_registry_keys = 8;

/// The most catalogue mirrors one descriptor names.
inline constexpr std::size_t max_registry_mirrors = 8;

/// How a registry's packages are downloaded.
enum class DownloadMode : uint8_t {
    direct, ///< the catalogue's own address, checked by its digest
    tokens, ///< the registry's download API hands out one key per download
};

/// Whether a download from this registry sends an install id.
enum class InstallIdUse : uint8_t {
    none,     ///< downloads carry no install id
    required, ///< the registry refuses a download that has none
};

/// One key that may sign the registry's catalogue.
struct RegistryKey {
    std::string id{};               ///< the id the signature file names
    base::signing::PublicKey key{}; ///< the Ed25519 public key
};

/// One registry: its catalogue, its keys and how its packages are fetched.
struct Descriptor {
    std::string id{};                            ///< kebab-case, 1 to 32 bytes
    std::string name{};                          ///< the name a player sees, 1 to 64 bytes
    std::optional<formats::url::Url> homepage{}; ///< a page opened in a browser; absent when unset
    formats::url::Url catalogue{};               ///< the catalogue, http, with no query
    std::vector<formats::url::Url> mirrors{};    ///< other copies of the catalogue, at most 8
    std::vector<RegistryKey> keys{}; ///< the signing keys; empty when the registry is unsigned
    DownloadMode mode = DownloadMode::direct;     ///< how packages are downloaded
    std::optional<formats::url::Url> api{};       ///< the download API; tokens only
    InstallIdUse install_id = InstallIdUse::none; ///< whether downloads send an install id

    /// Reports whether the registry names a signing key.
    ///
    /// An empty key list is an unsigned registry.
    ///
    /// @return true when keys is not empty
    [[nodiscard]] bool is_signed() const noexcept { return !keys.empty(); }
};

/// Reports whether a registry id is kebab-case of 1 to 32 bytes.
///
/// Words of a-z and 0-9 joined by single hyphens, not starting or ending
/// with a hyphen, and with no hyphen doubled.
///
/// @param text the id
/// @return true when the id is of that form
[[nodiscard]] bool valid_registry_id(std::string_view text) noexcept;

/// Reports whether a key id is 1 to 64 bytes of a key id.
///
/// The first byte is a-z or 0-9. Each byte after it is a-z, 0-9, a dot or
/// a hyphen.
///
/// @param text the id
/// @return true when the id is of that form
[[nodiscard]] bool valid_key_id(std::string_view text) noexcept;

/// Reads a registry descriptor.
///
/// registry must be 1. id, name, catalogue and downloads are required.
/// homepage, mirrors and keys are optional; absent mirrors and keys read as
/// empty, and empty keys are an unsigned registry. A catalogue, mirror or
/// download API is an http address with no query. A homepage is an http or
/// https address. downloads.mode is direct or tokens. direct refuses an API
/// and refuses install-id required, and defaults install-id to none. tokens
/// requires an API and defaults install-id to required. A key id and a
/// public key are each unique. At most 8 keys and 8 mirrors. A key this
/// build does not know is refused, so a misspelt one is noticed. A refusal
/// names the key and the rule.
///
/// @param bytes the descriptor's bytes, at most max_descriptor_bytes
/// @param[out] out the descriptor; left unchanged when the bytes are refused
/// @param[out] error why the bytes were refused; may be null
/// @return true when the descriptor was read
[[nodiscard]] bool
read_descriptor(std::span<const uint8_t> bytes, Descriptor& out, std::string* error);

/// Writes a registry descriptor.
///
/// Every string is double-quoted. Reading the text back yields the same
/// values.
///
/// @param descriptor the descriptor
/// @return the descriptor's text
[[nodiscard]] std::string descriptor_text(const Descriptor& descriptor);

/// The fingerprints of a descriptor's keys, in the keys' order.
///
/// @param descriptor the descriptor
/// @return one fingerprint for each key, as shown when a player adds a registry
[[nodiscard]] std::vector<std::string> key_fingerprints(const Descriptor& descriptor);

/// The address shown for a registry: its catalogue's host, and the port when
/// the port is not 80.
///
/// @param descriptor the descriptor
/// @return the host, or the host, a colon and the port
[[nodiscard]] std::string address_text(const Descriptor& descriptor);

} // namespace oa::data::registry
