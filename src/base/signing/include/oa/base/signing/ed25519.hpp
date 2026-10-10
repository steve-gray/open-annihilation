// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Ed25519 as RFC 8032 defines it, with SHA-512, for the signature on a
// catalogue. A key and a signature also have a text form, and a public key
// has a fingerprint a player can compare when adding a registry. Nothing is
// allocated and nothing throws.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace oa::base::signing {

/// An Ed25519 public key, 32 bytes.
using PublicKey = std::array<uint8_t, 32>;

/// An Ed25519 secret key, 64 bytes: the 32-byte seed followed by the public key.
using SecretKey = std::array<uint8_t, 64>;

/// An Ed25519 signature, 64 bytes.
using Signature = std::array<uint8_t, 64>;

/// Reports whether a signature matches a message and a public key.
///
/// Any message length is accepted, including none. A signature that does not
/// match returns false.
///
/// @param public_key the public key named by the signature
/// @param message the bytes that were signed
/// @param signature the signature
/// @return true when the signature matches
[[nodiscard]] bool
verify(const PublicKey& public_key, std::span<const uint8_t> message, const Signature& signature);

/// Signs a message.
///
/// @param secret_key the signer's secret key
/// @param message the bytes to sign
/// @return the signature
[[nodiscard]] Signature sign(const SecretKey& secret_key, std::span<const uint8_t> message);

/// Derives a secret key and its public key from a seed.
///
/// The seed this function holds is wiped before it returns.
///
/// @param seed the 32-byte seed
/// @param[out] secret_key the secret key, the seed followed by the public key
/// @param[out] public_key the public key
void key_pair_from_seed(std::array<uint8_t, 32> seed, SecretKey& secret_key, PublicKey& public_key);

/// The text form of a public key, `ed25519:` and its base64.
struct KeyText {
    /// The 52 characters, with no terminating null.
    std::array<char, 52> chars{};

    /// Returns the characters.
    ///
    /// @return the 52 characters, with no terminating null
    [[nodiscard]] std::string_view view() const noexcept { return {chars.data(), chars.size()}; }
};

/// Writes a public key as `ed25519:` and its base64.
///
/// @param public_key the public key
/// @return 52 characters, with no terminating null
[[nodiscard]] KeyText public_key_text(const PublicKey& public_key) noexcept;

/// Reads a public key from `ed25519:` and its base64.
///
/// The text must be that prefix and 44 characters of strict base64, and
/// nothing else.
///
/// @param text the text
/// @return the public key; nullopt for any other text
[[nodiscard]] std::optional<PublicKey> parse_public_key(std::string_view text) noexcept;

/// The text form of a signature, its base64.
struct SignatureText {
    /// The 88 characters, with no terminating null.
    std::array<char, 88> chars{};

    /// Returns the characters.
    ///
    /// @return the 88 characters, with no terminating null
    [[nodiscard]] std::string_view view() const noexcept { return {chars.data(), chars.size()}; }
};

/// Writes a signature as base64.
///
/// @param signature the signature
/// @return 88 characters, with padding and no terminating null
[[nodiscard]] SignatureText signature_text(const Signature& signature) noexcept;

/// Reads a signature from its base64.
///
/// The text must be 88 characters of strict base64, and nothing else.
///
/// @param text the text
/// @return the signature; nullopt for any other text
[[nodiscard]] std::optional<Signature> parse_signature(std::string_view text) noexcept;

/// The fingerprint of a public key, shown when a player adds a registry.
struct FingerprintText {
    /// The 28 characters, with no terminating null.
    std::array<char, 28> chars{};

    /// Returns the characters.
    ///
    /// @return the 28 characters, with no terminating null
    [[nodiscard]] std::string_view view() const noexcept { return {chars.data(), chars.size()}; }
};

/// Writes a public key's fingerprint.
///
/// The fingerprint is the first 8 bytes of the key's SHA-256, as four groups
/// of four lower-case hexadecimal digits separated by a space, a middle dot
/// and a space.
///
/// @param public_key the public key
/// @return 28 characters, with no terminating null
[[nodiscard]] FingerprintText fingerprint(const PublicKey& public_key) noexcept;

} // namespace oa::base::signing
