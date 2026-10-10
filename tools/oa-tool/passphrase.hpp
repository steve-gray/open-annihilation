// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A passphrase for a sealed publisher key. A file supplies the first line,
// for a test or a script. Otherwise the terminal is read with echo off, and
// the previous mode is put back, including when the read is interrupted.
// Signing with that key lives here too, so every command wipes the same way.
#pragma once

#include "oa/base/signing/ed25519.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace oa::tool {

/// Holds a passphrase or another short secret and wipes it when it goes away.
class SecretText {
  public:

    /// The secret bytes. Empty after a move.
    std::string text;

    /// Takes ownership of a secret.
    ///
    /// @param value the secret bytes
    explicit SecretText(std::string value);

    SecretText(const SecretText&) = delete;
    SecretText& operator=(const SecretText&) = delete;

    /// Moves a secret and wipes whatever the source still holds.
    ///
    /// @param other the secret to take
    SecretText(SecretText&& other) noexcept;

    SecretText& operator=(SecretText&&) = delete;

    /// Wipes the secret.
    ~SecretText();
};

/// Signs bytes with a sealed publisher key.
///
/// Reads the passphrase, unseals the key and signs. The passphrase and the
/// secret key are wiped on every path out, including when unsealing fails.
///
/// @param sealed the sealed key file bytes
/// @param passphrase_file the passphrase file, or nothing to read the terminal
/// @param bytes the bytes to sign
/// @param[out] public_key the public key unsealed from the key file
/// @return the signature
/// @throws Failure when the passphrase cannot be read or the key does not unseal
[[nodiscard]] oa::base::signing::Signature sign_with_sealed_key(
    std::span<const uint8_t> sealed,
    const std::optional<std::filesystem::path>& passphrase_file,
    std::span<const uint8_t> bytes,
    oa::base::signing::PublicKey& public_key
);

/// Reads a passphrase.
///
/// When `file` is set, the passphrase is that file's first line, without one
/// trailing CR or LF. A file with no line end is the whole file. More than
/// 1024 bytes is refused. When `file` is not set, the passphrase is read from
/// the terminal with echo off. No terminal asks for `--passphrase-file`
/// rather than reading with echo left on.
///
/// The caller wipes the returned string before it goes out of scope.
///
/// @param prompt the text shown on the terminal; unused when `file` is set
/// @param file the passphrase file, or nothing to read the terminal
/// @return the passphrase bytes
/// @throws Failure when the passphrase cannot be read
[[nodiscard]] std::string
read_passphrase(std::string_view prompt, const std::optional<std::filesystem::path>& file);

/// Reads a new passphrase and checks it.
///
/// A passphrase file is read once. A terminal is asked twice, and a short
/// passphrase is refused before the second prompt. The two replies have to
/// match, and the passphrase has to be at least 12 bytes.
///
/// The caller wipes the returned string before it goes out of scope.
///
/// @param file the passphrase file, or nothing to read the terminal
/// @return the passphrase bytes
/// @throws Failure when the passphrase is too short, the two replies differ,
///         or it cannot be read
[[nodiscard]] std::string read_new_passphrase(const std::optional<std::filesystem::path>& file);

} // namespace oa::tool
