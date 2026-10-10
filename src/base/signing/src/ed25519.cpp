// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Standard Ed25519 (RFC 8032, SHA-512) over Monocypher's
// crypto_ed25519_check, crypto_ed25519_sign and crypto_ed25519_key_pair,
// and the text form of a key, a signature and a fingerprint.
#include "oa/base/signing/ed25519.hpp"

#include "base64.hpp"
#include "oa/base/sha256.hpp"

#include <monocypher-ed25519.h>

namespace oa::base::signing {
namespace {

constexpr std::string_view key_prefix = "ed25519:";
constexpr std::string_view fingerprint_separator = " \xC2\xB7 ";

static_assert(key_prefix.size() == 8);
static_assert(base64_encoded_size(32) == 44);
static_assert(key_prefix.size() + 44 == 52);
static_assert(base64_encoded_size(64) == 88);
static_assert(fingerprint_separator.size() == 4);
static_assert(4 * 4 + 3 * fingerprint_separator.size() == 28);

} // namespace

bool verify(
    const PublicKey& public_key, std::span<const uint8_t> message, const Signature& signature
) {
    // A message may be empty. A zero length is no bytes, whatever pointer the
    // span holds.
    return crypto_ed25519_check(
               signature.data(), public_key.data(), message.data(), message.size()
           ) == 0;
}

Signature sign(const SecretKey& secret_key, std::span<const uint8_t> message) {
    Signature signature{};
    crypto_ed25519_sign(signature.data(), secret_key.data(), message.data(), message.size());
    return signature;
}

void key_pair_from_seed(
    std::array<uint8_t, 32> seed, SecretKey& secret_key, PublicKey& public_key
) {
    crypto_ed25519_key_pair(secret_key.data(), public_key.data(), seed.data());
    // This function's copy of the seed. The call above wipes the buffer it
    // reads; wipe it again so the copy is clear when this function returns.
    crypto_wipe(seed.data(), seed.size());
}

KeyText public_key_text(const PublicKey& public_key) noexcept {
    KeyText text{};
    for (size_t index = 0; index < key_prefix.size(); ++index)
        text.chars[index] = key_prefix[index];
    const std::span<char> encoded{text.chars.data() + key_prefix.size(), 44};
    if (!base64_encode(public_key, encoded))
        text = {};
    return text;
}

std::optional<PublicKey> parse_public_key(std::string_view text) noexcept {
    if (text.size() != 52 || !text.starts_with(key_prefix))
        return std::nullopt;
    PublicKey public_key{};
    const auto written = base64_decode(text.substr(key_prefix.size()), public_key);
    if (!written || *written != public_key.size())
        return std::nullopt;
    return public_key;
}

SignatureText signature_text(const Signature& signature) noexcept {
    SignatureText text{};
    if (!base64_encode(signature, text.chars))
        text = {};
    return text;
}

std::optional<Signature> parse_signature(std::string_view text) noexcept {
    if (text.size() != 88)
        return std::nullopt;
    Signature signature{};
    const auto written = base64_decode(text, signature);
    if (!written || *written != signature.size())
        return std::nullopt;
    return signature;
}

FingerprintText fingerprint(const PublicKey& public_key) noexcept {
    const oa::base::sha256::Digest digest = oa::base::sha256::digest_of(public_key);
    FingerprintText text{};
    constexpr char digits[] = "0123456789abcdef";
    size_t written = 0;
    for (size_t group = 0; group < 4; ++group) {
        if (group != 0) {
            for (const char character : fingerprint_separator)
                text.chars[written++] = character;
        }
        const uint8_t first = digest[group * 2];
        const uint8_t second = digest[group * 2 + 1];
        text.chars[written++] = digits[first >> 4];
        text.chars[written++] = digits[first & 0x0f];
        text.chars[written++] = digits[second >> 4];
        text.chars[written++] = digits[second & 0x0f];
    }
    return text;
}

} // namespace oa::base::signing
