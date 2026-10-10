// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A sealed key file: an Ed25519 seed encrypted with XChaCha20-Poly1305 under
// a key that Argon2id derives from a passphrase. The key id and the public
// key are stored in the clear and are covered by the authentication tag.
// Nothing here opens a file, asks for a passphrase or keeps a secret after
// it returns. The bytes of the file are described in the library's README.

#include "oa/base/signing/ed25519.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace oa::base::signing {

/// How hard it is to derive the encryption key from a passphrase.
///
/// The defaults are the ones a new key is sealed with: 256 MiB of memory,
/// three passes and one lane, which takes a second or two on a laptop.
struct SealParameters {
    uint32_t memory_kib{262'144}; ///< Argon2id memory, in KiB
    uint32_t passes{3};           ///< Argon2id passes
    uint32_t lanes{1};            ///< Argon2id lanes
};

/// The bytes of a sealed key that come before the key id.
inline constexpr std::size_t sealed_key_header_bytes = 96;

/// The most bytes a sealed key file can be: the header, a 64-byte id, the
/// encrypted seed and the tag.
inline constexpr std::size_t sealed_key_most_bytes = 144 + 64;

/// What a sealed key file says without being opened by a passphrase.
struct SealedKeyInfo {
    /// The key id. The bytes after `id_size` are zero, so a shorter id reads
    /// as text. An id of 64 bytes fills the array and has no terminating zero.
    std::array<char, 64> id{};
    uint8_t id_size{};           ///< how many bytes of `id` are the key id
    PublicKey public_key{};      ///< the Ed25519 public key, in the clear
    SealParameters parameters{}; ///< the Argon2id parameters the file names
};

/// Why a sealed key was or was not read.
enum class SealStatus : uint8_t {
    ok,               ///< the key was sealed or opened
    not_sealed_key,   ///< the bytes are not a sealed key file
    unknown_version,  ///< the file names a version this build does not read
    bad_parameters,   ///< the Argon2id parameters are outside the allowed range
    bad_id,           ///< the key id is not one this build accepts
    truncated,        ///< the file ends before a sealed key of its id length
    wrong_passphrase, ///< the tag failed, or the seed is not the recorded key
    out_of_memory,    ///< the Argon2id work area could not be allocated
};

/// Seals an Ed25519 seed into a key file.
///
/// The encryption key is Argon2id of the passphrase and the salt, with the
/// parameters and with no secret or associated data of Argon2's own. The
/// authentication covers everything before the ciphertext, so the id, the
/// public key and the parameters cannot be changed without the passphrase.
/// A copy of the seed, the derived key and the work area are wiped before
/// this returns. The caller's seed is left as it was.
///
/// @param key_id the key id, 1 to 64 bytes, as a registry key id
/// @param seed the 32-byte Ed25519 seed
/// @param passphrase the passphrase, as bytes
/// @param salt 16 random bytes, stored in the file
/// @param nonce 24 random bytes, stored in the file
/// @param parameters the Argon2id parameters to store and to use
/// @param[out] out room for the file; unchanged when the status is not ok
/// @param[out] out_size the file's size; zero when the status is not ok
/// @return ok, or why the key was not sealed
[[nodiscard]] SealStatus seal_key(
    std::string_view key_id,
    const std::array<uint8_t, 32>& seed,
    std::span<const uint8_t> passphrase,
    const std::array<uint8_t, 16>& salt,
    const std::array<uint8_t, 24>& nonce,
    const SealParameters& parameters,
    std::span<uint8_t> out,
    std::size_t& out_size
);

/// Reads a sealed key's id, public key and parameters.
///
/// No passphrase is used, and the authentication tag is not checked. A file
/// whose tag was changed still reports the id and the public key it stores.
///
/// @param file the file's bytes
/// @param[out] info the id, the public key and the parameters; unchanged when
///        the status is not ok
/// @return ok, or why the file is not a sealed key
[[nodiscard]] SealStatus read_sealed_key_info(std::span<const uint8_t> file, SealedKeyInfo& info);

/// Opens a sealed key with a passphrase.
///
/// A failed tag and a seed that does not produce the public key in the file
/// are both reported as the wrong passphrase. On any other status, `secret`
/// and `public_key` are wiped.
///
/// @param file the file's bytes
/// @param passphrase the passphrase, as bytes
/// @param[out] secret the secret key, the seed followed by the public key
/// @param[out] public_key the public key
/// @return ok, or why the key was not opened
[[nodiscard]] SealStatus unseal_key(
    std::span<const uint8_t> file,
    std::span<const uint8_t> passphrase,
    SecretKey& secret,
    PublicKey& public_key
);

/// The status as a short sentence.
///
/// @param status the status
/// @return a stable description
[[nodiscard]] std::string_view seal_status_text(SealStatus status) noexcept;

} // namespace oa::base::signing
