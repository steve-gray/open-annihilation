// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A sealed key file, version 1. The layout is in the library README.
// The encryption key is Argon2id of the passphrase, and the seed is
// XChaCha20-Poly1305 under that key. The bytes before the ciphertext are
// the authentication data, so the id, the public key and the parameters
// cannot be changed without the passphrase.
#include "oa/base/signing/sealed_key.hpp"

#include <monocypher.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>

// The key id rule lives in the registry. This library does not include that
// header and does not link that library: the registry already links signing,
// and a base library does not link data. Callers that seal or open a key
// (the sealed-key test, and oa-tool) link the registry, which supplies this
// definition. A static library that never calls it does not need the symbol.
namespace oa::data::registry {
[[nodiscard]] bool valid_key_id(std::string_view text) noexcept;
} // namespace oa::data::registry

namespace oa::base::signing {
namespace {

constexpr std::array<uint8_t, 8> magic = {0x4F, 0x41, 0x53, 0x4B, 0x45, 0x59, 0x00, 0x1A};

constexpr std::size_t version_at = 8;
constexpr std::size_t id_length_at = 9;
constexpr std::size_t reserved_at = 10;
constexpr std::size_t memory_at = 12;
constexpr std::size_t passes_at = 16;
constexpr std::size_t lanes_at = 20;
constexpr std::size_t salt_at = 24;
constexpr std::size_t nonce_at = 40;
constexpr std::size_t public_key_at = 64;
constexpr std::size_t id_at = 96;
constexpr std::size_t salt_bytes = 16;
constexpr std::size_t nonce_bytes = 24;
constexpr std::size_t seed_bytes = 32;
constexpr std::size_t tag_bytes = 16;
constexpr std::size_t body_bytes = 144;
static_assert(body_bytes == id_at + seed_bytes + tag_bytes);
constexpr uint32_t memory_least = 8;
constexpr uint32_t memory_most = 1'048'576;
constexpr uint32_t passes_least = 1;
constexpr uint32_t passes_most = 10;
constexpr uint32_t lanes_least = 1;
constexpr uint32_t lanes_most = 4;
constexpr uint8_t version_1 = 1;

void store_u16(uint8_t* to, uint16_t value) {
    to[0] = static_cast<uint8_t>(value);
    to[1] = static_cast<uint8_t>(value >> 8);
}

void store_u32(uint8_t* to, uint32_t value) {
    to[0] = static_cast<uint8_t>(value);
    to[1] = static_cast<uint8_t>(value >> 8);
    to[2] = static_cast<uint8_t>(value >> 16);
    to[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t load_u16(const uint8_t* from) {
    return static_cast<uint16_t>(from[0] | (static_cast<uint16_t>(from[1]) << 8));
}

uint32_t load_u32(const uint8_t* from) {
    return static_cast<uint32_t>(from[0]) | (static_cast<uint32_t>(from[1]) << 8) |
           (static_cast<uint32_t>(from[2]) << 16) | (static_cast<uint32_t>(from[3]) << 24);
}

void copy_bytes(uint8_t* to, const uint8_t* from, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index)
        to[index] = from[index];
}

/// Argon2id parameters this build will run.
///
/// Lanes are checked before the product, so the product cannot wrap. A lane
/// count Monocypher accepts still needs eight blocks per lane: fewer makes
/// the last block index wrap.
bool parameters_allowed(const SealParameters& parameters) {
    if (parameters.lanes < lanes_least || parameters.lanes > lanes_most)
        return false;
    if (parameters.passes < passes_least || parameters.passes > passes_most)
        return false;
    if (parameters.memory_kib < memory_least || parameters.memory_kib > memory_most)
        return false;
    return parameters.memory_kib >= 8u * parameters.lanes;
}

/// The file, once the bytes are a sealed key of a known version. Pointers
/// address `file` and are not owned.
struct Parsed {
    SealStatus status = SealStatus::not_sealed_key;
    std::size_t id_size = 0;
    std::size_t ad_size = 0;
    SealParameters parameters{};
    const uint8_t* salt = nullptr;
    const uint8_t* nonce = nullptr;
    const uint8_t* public_key = nullptr;
    const uint8_t* id = nullptr;
    const uint8_t* cipher = nullptr;
    const uint8_t* tag = nullptr;
};

Parsed parse(std::span<const uint8_t> file) {
    Parsed parsed;
    if (file.size() < magic.size()) {
        parsed.status = SealStatus::truncated;
        return parsed;
    }
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (file[index] != magic[index]) {
            parsed.status = SealStatus::not_sealed_key;
            return parsed;
        }
    }
    if (file.size() < version_at + 1) {
        parsed.status = SealStatus::truncated;
        return parsed;
    }
    if (file[version_at] != version_1) {
        parsed.status = SealStatus::unknown_version;
        return parsed;
    }
    if (file.size() < sealed_key_header_bytes) {
        parsed.status = SealStatus::truncated;
        return parsed;
    }
    if (load_u16(file.data() + reserved_at) != 0) {
        parsed.status = SealStatus::bad_parameters;
        return parsed;
    }
    const uint8_t id_size = file[id_length_at];
    if (id_size == 0 || id_size > 64) {
        parsed.status = SealStatus::bad_id;
        return parsed;
    }
    parsed.parameters.memory_kib = load_u32(file.data() + memory_at);
    parsed.parameters.passes = load_u32(file.data() + passes_at);
    parsed.parameters.lanes = load_u32(file.data() + lanes_at);
    if (!parameters_allowed(parsed.parameters)) {
        parsed.status = SealStatus::bad_parameters;
        return parsed;
    }
    const std::size_t file_size = body_bytes + id_size;
    if (file.size() < file_size) {
        parsed.status = SealStatus::truncated;
        return parsed;
    }
    if (file.size() > file_size) {
        parsed.status = SealStatus::not_sealed_key;
        return parsed;
    }
    parsed.id_size = id_size;
    parsed.ad_size = id_at + id_size;
    parsed.salt = file.data() + salt_at;
    parsed.nonce = file.data() + nonce_at;
    parsed.public_key = file.data() + public_key_at;
    parsed.id = file.data() + id_at;
    parsed.cipher = file.data() + parsed.ad_size;
    parsed.tag = file.data() + parsed.ad_size + seed_bytes;
    const std::string_view key_id(reinterpret_cast<const char*>(parsed.id), parsed.id_size);
    if (!oa::data::registry::valid_key_id(key_id)) {
        parsed.status = SealStatus::bad_id;
        return parsed;
    }
    parsed.status = SealStatus::ok;
    return parsed;
}

/// Argon2id's work area. The allocation is wiped, including the padding that
/// puts the pointer Monocypher uses on an 8-byte boundary.
class WorkArea {
  public:

    WorkArea() = default;
    WorkArea(const WorkArea&) = delete;
    WorkArea& operator=(const WorkArea&) = delete;

    ~WorkArea() { release(); }

    [[nodiscard]] bool allocate(uint32_t memory_kib) {
        release();
        const uint64_t bytes = static_cast<uint64_t>(memory_kib) * 1024u;
        const uint64_t total = bytes + 8u;
        if (total > static_cast<uint64_t>(SIZE_MAX))
            return false;
        owned_.reset(new (std::nothrow) uint8_t[static_cast<std::size_t>(total)]);
        if (!owned_)
            return false;
        owned_size_ = static_cast<std::size_t>(total);
        const auto address = reinterpret_cast<uintptr_t>(owned_.get());
        aligned_ = reinterpret_cast<uint8_t*>((address + 7u) & ~uintptr_t{7});
        return aligned_ != nullptr;
    }

    [[nodiscard]] uint8_t* data() noexcept { return aligned_; }

    void release() noexcept {
        if (owned_)
            crypto_wipe(owned_.get(), owned_size_);
        owned_.reset();
        owned_size_ = 0;
        aligned_ = nullptr;
    }

  private:

    std::unique_ptr<uint8_t[]> owned_{};
    std::size_t owned_size_ = 0;
    uint8_t* aligned_ = nullptr;
};

struct DerivedKey {
    uint8_t bytes[32]{};

    ~DerivedKey() { crypto_wipe(bytes, sizeof bytes); }
};

struct SeedBuffer {
    std::array<uint8_t, 32> bytes{};

    ~SeedBuffer() { crypto_wipe(bytes.data(), bytes.size()); }
};

struct SecretBuffer {
    SecretKey secret{};
    PublicKey public_key{};

    ~SecretBuffer() {
        crypto_wipe(secret.data(), secret.size());
        crypto_wipe(public_key.data(), public_key.size());
    }
};

struct BuiltFile {
    std::array<uint8_t, sealed_key_most_bytes> bytes{};

    ~BuiltFile() { crypto_wipe(bytes.data(), bytes.size()); }
};

/// Derives the 32-byte encryption key. The work area is wiped before this
/// returns. `passphrase` may be empty; a null pointer is not passed on.
bool derive_key(
    uint8_t key[32],
    const SealParameters& parameters,
    std::span<const uint8_t> passphrase,
    const uint8_t salt[16]
) {
    WorkArea work;
    if (!work.allocate(parameters.memory_kib))
        return false;
    const uint8_t empty = 0;
    const uint8_t* pass = passphrase.empty() ? &empty : passphrase.data();
    const crypto_argon2_config config{
        CRYPTO_ARGON2_ID, parameters.memory_kib, parameters.passes, parameters.lanes
    };
    const crypto_argon2_inputs inputs{
        pass, salt, static_cast<uint32_t>(passphrase.size()), static_cast<uint32_t>(salt_bytes)
    };
    crypto_argon2(key, 32, work.data(), config, inputs, crypto_argon2_no_extras);
    return true;
}

} // namespace

SealStatus seal_key(
    std::string_view key_id,
    const std::array<uint8_t, 32>& seed,
    std::span<const uint8_t> passphrase,
    const std::array<uint8_t, 16>& salt,
    const std::array<uint8_t, 24>& nonce,
    const SealParameters& parameters,
    std::span<uint8_t> out,
    std::size_t& out_size
) {
    out_size = 0;
    if (!oa::data::registry::valid_key_id(key_id))
        return SealStatus::bad_id;
    if (!parameters_allowed(parameters))
        return SealStatus::bad_parameters;
    if (passphrase.size() > static_cast<std::size_t>(UINT32_MAX))
        return SealStatus::bad_parameters;
    const std::size_t file_size = body_bytes + key_id.size();
    if (out.size() < file_size)
        return SealStatus::truncated;

    BuiltFile built;
    SecretBuffer opened;
    key_pair_from_seed(seed, opened.secret, opened.public_key);
    for (std::size_t index = 0; index < magic.size(); ++index)
        built.bytes[index] = magic[index];
    built.bytes[version_at] = version_1;
    built.bytes[id_length_at] = static_cast<uint8_t>(key_id.size());
    store_u16(built.bytes.data() + reserved_at, 0);
    store_u32(built.bytes.data() + memory_at, parameters.memory_kib);
    store_u32(built.bytes.data() + passes_at, parameters.passes);
    store_u32(built.bytes.data() + lanes_at, parameters.lanes);
    copy_bytes(built.bytes.data() + salt_at, salt.data(), salt_bytes);
    copy_bytes(built.bytes.data() + nonce_at, nonce.data(), nonce_bytes);
    copy_bytes(
        built.bytes.data() + public_key_at, opened.public_key.data(), opened.public_key.size()
    );
    for (std::size_t index = 0; index < key_id.size(); ++index)
        built.bytes[id_at + index] = static_cast<uint8_t>(key_id[index]);
    // The secret holds the seed. The public key is already in the file.
    crypto_wipe(opened.secret.data(), opened.secret.size());

    DerivedKey derived;
    if (!derive_key(derived.bytes, parameters, passphrase, salt.data()))
        return SealStatus::out_of_memory;
    const std::size_t ad_size = id_at + key_id.size();
    crypto_aead_lock(
        built.bytes.data() + ad_size,
        built.bytes.data() + ad_size + seed_bytes,
        derived.bytes,
        nonce.data(),
        built.bytes.data(),
        ad_size,
        seed.data(),
        seed.size()
    );
    copy_bytes(out.data(), built.bytes.data(), file_size);
    out_size = file_size;
    return SealStatus::ok;
}

SealStatus read_sealed_key_info(std::span<const uint8_t> file, SealedKeyInfo& info) {
    const Parsed parsed = parse(file);
    if (parsed.status != SealStatus::ok)
        return parsed.status;
    SealedKeyInfo read;
    for (std::size_t index = 0; index < parsed.id_size; ++index)
        read.id[index] = static_cast<char>(parsed.id[index]);
    read.id_size = static_cast<uint8_t>(parsed.id_size);
    copy_bytes(read.public_key.data(), parsed.public_key, read.public_key.size());
    read.parameters = parsed.parameters;
    info = read;
    return SealStatus::ok;
}

SealStatus unseal_key(
    std::span<const uint8_t> file,
    std::span<const uint8_t> passphrase,
    SecretKey& secret,
    PublicKey& public_key
) {
    crypto_wipe(secret.data(), secret.size());
    crypto_wipe(public_key.data(), public_key.size());
    const Parsed parsed = parse(file);
    if (parsed.status != SealStatus::ok)
        return parsed.status;
    if (passphrase.size() > static_cast<std::size_t>(UINT32_MAX))
        return SealStatus::bad_parameters;

    DerivedKey derived;
    if (!derive_key(derived.bytes, parsed.parameters, passphrase, parsed.salt))
        return SealStatus::out_of_memory;

    SeedBuffer seed;
    SecretBuffer opened;
    if (crypto_aead_unlock(
            seed.bytes.data(),
            parsed.tag,
            derived.bytes,
            parsed.nonce,
            file.data(),
            parsed.ad_size,
            parsed.cipher,
            seed_bytes
        ) != 0)
        return SealStatus::wrong_passphrase;
    key_pair_from_seed(seed.bytes, opened.secret, opened.public_key);
    if (crypto_verify32(opened.public_key.data(), parsed.public_key) != 0)
        return SealStatus::wrong_passphrase;
    secret = opened.secret;
    public_key = opened.public_key;
    return SealStatus::ok;
}

std::string_view seal_status_text(SealStatus status) noexcept {
    switch (status) {
    case SealStatus::ok:
        return "the key is sealed";
    case SealStatus::not_sealed_key:
        return "the file is not a sealed key";
    case SealStatus::unknown_version:
        return "the sealed key has an unknown version";
    case SealStatus::bad_parameters:
        return "the sealed key names parameters that are not allowed";
    case SealStatus::bad_id:
        return "the key id is not allowed";
    case SealStatus::truncated:
        return "the sealed key is truncated";
    case SealStatus::wrong_passphrase:
        return "the passphrase does not open the key";
    case SealStatus::out_of_memory:
        return "the sealed key needs more memory than is available";
    }
    return "";
}

} // namespace oa::base::signing
