// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A sealed key against RFC 9106's Argon2id vector, the XChaCha20-Poly1305
// vector, a round trip, and the files this build refuses. The pinned bytes
// are a regression pin of a test pattern, not a key anyone uses.
#include "oa/base/signing/sealed_key.hpp"

#include "oa/test/check.hpp"

#include <monocypher.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace signing = oa::base::signing;

constexpr signing::SealParameters small_parameters{64, 1, 1};
constexpr signing::SealParameters wide_parameters{32, 3, 4};

int hex_digit(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

std::vector<uint8_t> bytes_from_hex(std::string_view hex) {
    OA_CHECK(hex.size() % 2 == 0);
    std::vector<uint8_t> bytes(hex.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const int high = hex_digit(hex[index * 2]);
        const int low = hex_digit(hex[index * 2 + 1]);
        OA_CHECK(high >= 0 && low >= 0);
        bytes[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return bytes;
}

void print_hex(std::span<const uint8_t> bytes) {
    for (const uint8_t byte : bytes)
        std::fprintf(stderr, "%02x", byte);
    std::fputc('\n', stderr);
}

void expect_bytes(
    const char* name, std::span<const uint8_t> got, std::span<const uint8_t> expected
) {
    if (got.size() == expected.size() && std::equal(got.begin(), got.end(), expected.begin()))
        return;
    std::fprintf(
        stderr, "%s mismatch (%zu bytes, expected %zu)\n", name, got.size(), expected.size()
    );
    print_hex(got);
    OA_CHECK(false);
}

std::span<const uint8_t> as_bytes(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

void wipe(std::span<uint8_t> bytes) {
    crypto_wipe(bytes.data(), bytes.size());
}

void store_u32(std::span<uint8_t> file, std::size_t at, uint32_t value) {
    file[at] = static_cast<uint8_t>(value);
    file[at + 1] = static_cast<uint8_t>(value >> 8);
    file[at + 2] = static_cast<uint8_t>(value >> 16);
    file[at + 3] = static_cast<uint8_t>(value >> 24);
}

/// RFC 9106 section 5.3, through Monocypher's crypto_argon2 with a secret
/// and associated data. The tag is the vector's, not a value read back from
/// this build.
void argon2id_rfc9106() {
    const std::vector<uint8_t> password(32, 0x01);
    const std::vector<uint8_t> salt(16, 0x02);
    const std::vector<uint8_t> secret(8, 0x03);
    const std::vector<uint8_t> associated(12, 0x04);
    std::vector<uint8_t> work(32u * 1024u + 8u);
    const auto address = reinterpret_cast<uintptr_t>(work.data());
    uint8_t* aligned = reinterpret_cast<uint8_t*>((address + 7u) & ~uintptr_t{7});
    std::array<uint8_t, 32> hash{};
    const crypto_argon2_config config{CRYPTO_ARGON2_ID, 32, 3, 4};
    const crypto_argon2_inputs inputs{password.data(), salt.data(), 32, 16};
    const crypto_argon2_extras extras{secret.data(), associated.data(), 8, 12};
    crypto_argon2(hash.data(), 32, aligned, config, inputs, extras);
    constexpr std::string_view tag =
        "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659";
    expect_bytes("argon2id", hash, bytes_from_hex(tag));
    wipe(hash);
}

/// draft-irtf-cfrg-xchacha-03 appendix A.3.1, through crypto_aead_lock.
/// The ciphertext and the tag are the draft's.
void xchacha_aead() {
    const auto plain = bytes_from_hex(
        "4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a20496620"
        "4920"
        "636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c20"
        "7375"
        "6e73637265656e20776f756c642062652069742e"
    );
    const auto associated = bytes_from_hex("50515253c0c1c2c3c4c5c6c7");
    const auto key =
        bytes_from_hex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    const auto nonce = bytes_from_hex("404142434445464748494a4b4c4d4e4f5051525354555657");
    const auto expected_cipher = bytes_from_hex(
        "bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb731c7f1b0b4aa6440bf3a82f4e"
        "da7e"
        "39ae64c6708c54c216cb96b72e1213b4522f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b23835"
        "65d3"
        "fff921f9664c97637da9768812f615c68b13b52e"
    );
    const auto expected_tag = bytes_from_hex("c0875924c1c7987947deafd8780acf49");
    OA_CHECK(key.size() == 32);
    OA_CHECK(nonce.size() == 24);
    OA_CHECK(expected_tag.size() == 16);
    OA_CHECK(expected_cipher.size() == plain.size());
    std::vector<uint8_t> cipher(plain.size());
    std::array<uint8_t, 16> tag{};
    crypto_aead_lock(
        cipher.data(),
        tag.data(),
        key.data(),
        nonce.data(),
        associated.data(),
        associated.size(),
        plain.data(),
        plain.size()
    );
    expect_bytes("xchacha ciphertext", cipher, expected_cipher);
    expect_bytes("xchacha tag", tag, expected_tag);

    std::vector<uint8_t> opened(plain.size());
    const int unlocked = crypto_aead_unlock(
        opened.data(),
        tag.data(),
        key.data(),
        nonce.data(),
        associated.data(),
        associated.size(),
        cipher.data(),
        cipher.size()
    );
    OA_CHECK(unlocked == 0);
    expect_bytes("xchacha plaintext", opened, plain);
    wipe(opened);
}

struct Materials {
    std::array<uint8_t, 32> seed{};
    std::array<uint8_t, 16> salt{};
    std::array<uint8_t, 24> nonce{};
    std::string passphrase;
    std::string id;

    Materials() = default;
    Materials(const Materials&) = delete;
    Materials& operator=(const Materials&) = delete;

    ~Materials() {
        wipe(seed);
        wipe(salt);
        wipe(nonce);
        if (!passphrase.empty())
            crypto_wipe(passphrase.data(), passphrase.size());
    }
};

std::vector<uint8_t>
seal_materials(const Materials& materials, signing::SealParameters parameters) {
    std::vector<uint8_t> file(signing::sealed_key_most_bytes);
    std::size_t size = 0;
    const auto status = signing::seal_key(
        materials.id,
        materials.seed,
        as_bytes(materials.passphrase),
        materials.salt,
        materials.nonce,
        parameters,
        file,
        size
    );
    OA_CHECK(status == signing::SealStatus::ok);
    OA_CHECK(size == 144 + materials.id.size());
    if (status != signing::SealStatus::ok)
        return {};
    file.resize(size);
    return file;
}

void expect_round_trip(const Materials& materials, signing::SealParameters parameters) {
    const std::vector<uint8_t> file = seal_materials(materials, parameters);
    OA_CHECK(file.size() == 144 + materials.id.size());
    signing::SealedKeyInfo info{};
    OA_CHECK(signing::read_sealed_key_info(file, info) == signing::SealStatus::ok);
    OA_CHECK(info.id_size == materials.id.size());
    OA_CHECK(std::string_view(info.id.data(), info.id_size) == materials.id);
    OA_CHECK(info.parameters.memory_kib == parameters.memory_kib);
    OA_CHECK(info.parameters.passes == parameters.passes);
    OA_CHECK(info.parameters.lanes == parameters.lanes);

    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    const auto status =
        signing::unseal_key(file, as_bytes(materials.passphrase), secret, public_key);
    OA_CHECK(status == signing::SealStatus::ok);
    OA_CHECK(std::equal(materials.seed.begin(), materials.seed.end(), secret.begin()));
    OA_CHECK(std::equal(public_key.begin(), public_key.end(), secret.begin() + 32));
    OA_CHECK(public_key == info.public_key);
    wipe(secret);
    wipe(public_key);
}

void fill_test(Materials& materials) {
    materials.seed.fill(0x42);
    materials.salt.fill(0x24);
    materials.nonce.fill(0x11);
    materials.passphrase = "pin-passphrase";
    materials.id = "pin-key";
}

void expect_unseal(
    std::span<const uint8_t> file, std::string_view passphrase, signing::SealStatus status
);

void round_trip() {
    Materials materials;
    fill_test(materials);
    expect_round_trip(materials, small_parameters);

    Materials dotted;
    dotted.seed.fill(0x07);
    dotted.salt.fill(0x08);
    dotted.nonce.fill(0x09);
    dotted.passphrase = "another-passphrase";
    dotted.id = "a.b-c";
    expect_round_trip(dotted, small_parameters);

    Materials full;
    full.seed.fill(0x03);
    full.salt.fill(0x04);
    full.nonce.fill(0x05);
    full.passphrase = "a-full-id-passphrase";
    full.id = std::string(64, 'a');
    expect_round_trip(full, small_parameters);
}

void wide_lanes_round_trip() {
    Materials materials;
    fill_test(materials);
    expect_round_trip(materials, wide_parameters);
}

/// The sealed bytes of a fixed test pattern. A change here means Argon2id
/// or the file layout changed. The pattern is not a key.
void pinned_bytes() {
    Materials materials;
    fill_test(materials);
    const std::vector<uint8_t> file = seal_materials(materials, small_parameters);
    OA_CHECK(file.size() == 144 + materials.id.size());
    // Regression pin of the pattern above. A mismatch prints the bytes.
    constexpr std::string_view pinned_hex =
        "4f41534b4559001a010700004000000001000000010000002424242424242424"
        "2424242424242424111111111111111111111111111111111111111111111111"
        "2152f8d19b791d24453242e15f2eab6cb7cffa7b6a5ed30097960e069881db12"
        "70696e2d6b65792cff949c05ccd10570c9020e81b20cbb837a8b665010274d57"
        "1e8e6ccf2c4939aecc852c8815a6f07de1e396e5478469";
    if (bytes_from_hex(pinned_hex) != file) {
        std::fputs("sealed pin:\n", stderr);
        print_hex(file);
        OA_CHECK(false);
    }
}

void expect_refused(std::span<const uint8_t> file, signing::SealStatus status) {
    signing::SealedKeyInfo info{};
    info.id[0] = 'Z';
    info.id_size = 3;
    info.parameters.memory_kib = 9;
    const auto read = signing::read_sealed_key_info(file, info);
    OA_CHECK(read == status);
    if (read != signing::SealStatus::ok) {
        OA_CHECK(info.id[0] == 'Z');
        OA_CHECK(info.id_size == 3);
        OA_CHECK(info.parameters.memory_kib == 9);
    }
    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    secret.fill(0xA5);
    public_key.fill(0xA5);
    const auto opened = signing::unseal_key(file, as_bytes("pin-passphrase"), secret, public_key);
    OA_CHECK(opened == status);
    OA_CHECK(secret == signing::SecretKey{});
    OA_CHECK(public_key == signing::PublicKey{});
}

void refusals() {
    Materials materials;
    fill_test(materials);
    const std::vector<uint8_t> file = seal_materials(materials, small_parameters);
    OA_CHECK(file.size() == 144 + materials.id.size());
    if (file.size() != 144 + materials.id.size())
        return;

    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    secret.fill(0xA5);
    const auto wrong =
        signing::unseal_key(file, as_bytes("not-the-passphrase"), secret, public_key);
    OA_CHECK(wrong == signing::SealStatus::wrong_passphrase);
    OA_CHECK(secret == signing::SecretKey{});
    OA_CHECK(public_key == signing::PublicKey{});

    for (std::size_t index = 0; index < 96 + materials.id.size(); ++index) {
        std::vector<uint8_t> flipped = file;
        flipped[index] = static_cast<uint8_t>(flipped[index] ^ 0x01);
        signing::SecretKey opened{};
        signing::PublicKey key{};
        const auto status =
            signing::unseal_key(flipped, as_bytes(materials.passphrase), opened, key);
        OA_CHECK(status != signing::SealStatus::ok);
        OA_CHECK(opened == signing::SecretKey{});
        wipe(opened);
        wipe(key);
    }

    std::vector<uint8_t> cipher = file;
    cipher[96 + materials.id.size()] =
        static_cast<uint8_t>(cipher[96 + materials.id.size()] ^ 0x01);
    expect_unseal(cipher, materials.passphrase, signing::SealStatus::wrong_passphrase);
    std::vector<uint8_t> tag = file;
    tag.back() = static_cast<uint8_t>(tag.back() ^ 0x01);
    expect_unseal(tag, materials.passphrase, signing::SealStatus::wrong_passphrase);

    std::vector<uint8_t> salt = file;
    salt[24] = static_cast<uint8_t>(salt[24] ^ 0x01);
    signing::SealedKeyInfo salt_info{};
    OA_CHECK(signing::read_sealed_key_info(salt, salt_info) == signing::SealStatus::ok);
    OA_CHECK(std::string_view(salt_info.id.data(), salt_info.id_size) == materials.id);
    expect_unseal(salt, materials.passphrase, signing::SealStatus::wrong_passphrase);

    expect_refused({}, signing::SealStatus::truncated);
    expect_refused(std::span<const uint8_t>(file.data(), 7), signing::SealStatus::truncated);
    expect_refused(std::span<const uint8_t>(file.data(), 8), signing::SealStatus::truncated);
    expect_refused(std::span<const uint8_t>(file.data(), 20), signing::SealStatus::truncated);
    expect_refused(
        std::span<const uint8_t>(file.data(), file.size() - 1), signing::SealStatus::truncated
    );

    std::vector<uint8_t> longer = file;
    longer.push_back(0);
    expect_refused(longer, signing::SealStatus::not_sealed_key);

    std::vector<uint8_t> bad_magic = file;
    bad_magic[0] = 0;
    expect_refused(bad_magic, signing::SealStatus::not_sealed_key);

    std::vector<uint8_t> version = file;
    version[8] = 2;
    expect_refused(version, signing::SealStatus::unknown_version);

    std::vector<uint8_t> reserved = file;
    reserved[10] = 1;
    expect_refused(reserved, signing::SealStatus::bad_parameters);

    auto patched = [&file](uint32_t memory, uint32_t passes, uint32_t lanes) {
        std::vector<uint8_t> out = file;
        store_u32(out, 12, memory);
        store_u32(out, 16, passes);
        store_u32(out, 20, lanes);
        return out;
    };
    expect_refused(patched(7, 1, 1), signing::SealStatus::bad_parameters);
    expect_refused(patched(1'048'577, 1, 1), signing::SealStatus::bad_parameters);
    expect_refused(patched(64, 0, 1), signing::SealStatus::bad_parameters);
    expect_refused(patched(64, 11, 1), signing::SealStatus::bad_parameters);
    expect_refused(patched(64, 1, 0), signing::SealStatus::bad_parameters);
    expect_refused(patched(64, 1, 5), signing::SealStatus::bad_parameters);
    expect_refused(patched(8, 1, 4), signing::SealStatus::bad_parameters);

    std::vector<uint8_t> empty_id = file;
    empty_id[9] = 0;
    expect_refused(empty_id, signing::SealStatus::bad_id);
    std::vector<uint8_t> huge_id = file;
    huge_id[9] = 65;
    expect_refused(huge_id, signing::SealStatus::bad_id);

    std::vector<uint8_t> dashed = file;
    dashed[96] = static_cast<uint8_t>('-');
    expect_refused(dashed, signing::SealStatus::bad_id);
    std::vector<uint8_t> upper = file;
    upper[96] = static_cast<uint8_t>('P');
    expect_refused(upper, signing::SealStatus::bad_id);

    std::array<uint8_t, 8> tiny{};
    tiny.fill(0x5A);
    std::size_t size = 4;
    const auto truncated = signing::seal_key(
        materials.id,
        materials.seed,
        as_bytes(materials.passphrase),
        materials.salt,
        materials.nonce,
        small_parameters,
        tiny,
        size
    );
    OA_CHECK(truncated == signing::SealStatus::truncated);
    OA_CHECK(size == 0);
    OA_CHECK(tiny[0] == 0x5A);

    size = 3;
    std::array<uint8_t, 4> unchanged{};
    unchanged.fill(0x11);
    OA_CHECK(
        signing::seal_key(
            "",
            materials.seed,
            as_bytes(materials.passphrase),
            materials.salt,
            materials.nonce,
            small_parameters,
            unchanged,
            size
        ) == signing::SealStatus::bad_id
    );
    OA_CHECK(size == 0);
    OA_CHECK(unchanged[0] == 0x11);
    const std::string too_long(65, 'b');
    OA_CHECK(
        signing::seal_key(
            too_long,
            materials.seed,
            as_bytes(materials.passphrase),
            materials.salt,
            materials.nonce,
            small_parameters,
            unchanged,
            size
        ) == signing::SealStatus::bad_id
    );
    OA_CHECK(
        signing::seal_key(
            "-pin-key",
            materials.seed,
            as_bytes(materials.passphrase),
            materials.salt,
            materials.nonce,
            small_parameters,
            unchanged,
            size
        ) == signing::SealStatus::bad_id
    );
    OA_CHECK(
        signing::seal_key(
            "Pin-key",
            materials.seed,
            as_bytes(materials.passphrase),
            materials.salt,
            materials.nonce,
            small_parameters,
            unchanged,
            size
        ) == signing::SealStatus::bad_id
    );
    signing::SealParameters bad{7, 1, 1};
    OA_CHECK(
        signing::seal_key(
            materials.id,
            materials.seed,
            as_bytes(materials.passphrase),
            materials.salt,
            materials.nonce,
            bad,
            unchanged,
            size
        ) == signing::SealStatus::bad_parameters
    );
    OA_CHECK(size == 0);
    OA_CHECK(unchanged[0] == 0x11);
}

void expect_unseal(
    std::span<const uint8_t> file, std::string_view passphrase, signing::SealStatus status
) {
    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    secret.fill(0xA5);
    const auto opened = signing::unseal_key(file, as_bytes(passphrase), secret, public_key);
    OA_CHECK(opened == status);
    OA_CHECK(secret == signing::SecretKey{});
    OA_CHECK(public_key == signing::PublicKey{});
}

/// A tag that matches a public key the seed did not produce. The failed tag
/// and this mismatch are the same status.
void public_key_mismatch() {
    Materials materials;
    fill_test(materials);
    const std::vector<uint8_t> file = seal_materials(materials, small_parameters);
    std::vector<uint8_t> forged = file;
    forged[64] = static_cast<uint8_t>(forged[64] ^ 0x01);

    std::vector<uint8_t> work(small_parameters.memory_kib * 1024u + 8u);
    const auto address = reinterpret_cast<uintptr_t>(work.data());
    uint8_t* aligned = reinterpret_cast<uint8_t*>((address + 7u) & ~uintptr_t{7});
    std::array<uint8_t, 32> derived{};
    const crypto_argon2_config config{
        CRYPTO_ARGON2_ID,
        small_parameters.memory_kib,
        small_parameters.passes,
        small_parameters.lanes
    };
    const crypto_argon2_inputs inputs{
        reinterpret_cast<const uint8_t*>(materials.passphrase.data()),
        materials.salt.data(),
        static_cast<uint32_t>(materials.passphrase.size()),
        16
    };
    crypto_argon2(derived.data(), 32, aligned, config, inputs, crypto_argon2_no_extras);
    const std::size_t ad_size = 96 + materials.id.size();
    crypto_aead_lock(
        forged.data() + ad_size,
        forged.data() + ad_size + 32,
        derived.data(),
        materials.nonce.data(),
        forged.data(),
        ad_size,
        materials.seed.data(),
        materials.seed.size()
    );
    wipe(derived);
    signing::SealedKeyInfo info{};
    OA_CHECK(signing::read_sealed_key_info(forged, info) == signing::SealStatus::ok);
    OA_CHECK(info.public_key[0] == forged[64]);
    expect_unseal(forged, materials.passphrase, signing::SealStatus::wrong_passphrase);
}

void read_without_passphrase() {
    Materials materials;
    fill_test(materials);
    const std::vector<uint8_t> file = seal_materials(materials, small_parameters);
    signing::SealedKeyInfo info{};
    OA_CHECK(signing::read_sealed_key_info(file, info) == signing::SealStatus::ok);
    OA_CHECK(std::string_view(info.id.data(), info.id_size) == materials.id);
    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    OA_CHECK(
        signing::unseal_key(file, as_bytes(materials.passphrase), secret, public_key) ==
        signing::SealStatus::ok
    );
    OA_CHECK(info.public_key == public_key);

    std::vector<uint8_t> tagged = file;
    tagged.back() = static_cast<uint8_t>(tagged.back() ^ 0x01);
    signing::SealedKeyInfo from_bad_tag{};
    OA_CHECK(signing::read_sealed_key_info(tagged, from_bad_tag) == signing::SealStatus::ok);
    OA_CHECK(from_bad_tag.public_key == info.public_key);
    OA_CHECK(std::string_view(from_bad_tag.id.data(), from_bad_tag.id_size) == materials.id);
    wipe(secret);
    wipe(public_key);
}

void status_text() {
    const signing::SealStatus statuses[] = {
        signing::SealStatus::ok,
        signing::SealStatus::not_sealed_key,
        signing::SealStatus::unknown_version,
        signing::SealStatus::bad_parameters,
        signing::SealStatus::bad_id,
        signing::SealStatus::truncated,
        signing::SealStatus::wrong_passphrase,
        signing::SealStatus::out_of_memory,
    };
    for (const signing::SealStatus status : statuses)
        OA_CHECK(!signing::seal_status_text(status).empty());
    OA_CHECK(signing::seal_status_text(static_cast<signing::SealStatus>(255)).empty());
}

} // namespace

int main() {
    argon2id_rfc9106();
    xchacha_aead();
    round_trip();
    wide_lanes_round_trip();
    pinned_bytes();
    refusals();
    public_key_mismatch();
    read_without_passphrase();
    status_text();
    const int status = oa::test::check_exit_status();
    if (status == 0)
        std::puts("base-signing-sealed-key: ok");
    return status;
}
