// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Ed25519 against RFC 8032 section 7.1, the text forms of a key and a
// signature, RFC 4648 base64, and a fingerprint computed independently.
#include "oa/base/signing/ed25519.hpp"

#include "../src/base64.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace signing = oa::base::signing;

int hex_digit(char character) {
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

std::vector<uint8_t> message_from_hex(std::string_view hex) {
    OA_CHECK(hex.size() % 2 == 0);
    std::vector<uint8_t> bytes(hex.size() / 2);
    for (size_t index = 0; index < bytes.size(); ++index) {
        const int high = hex_digit(hex[index * 2]);
        const int low = hex_digit(hex[index * 2 + 1]);
        OA_CHECK(high >= 0 && low >= 0);
        bytes[index] = static_cast<uint8_t>(high << 4 | low);
    }
    return bytes;
}

template <size_t N>
std::array<uint8_t, N> bytes_from_hex(std::string_view hex) {
    const auto bytes = message_from_hex(hex);
    std::array<uint8_t, N> out{};
    OA_CHECK(bytes.size() == N);
    if (bytes.size() == N)
        for (size_t index = 0; index < N; ++index)
            out[index] = bytes[index];
    return out;
}

std::span<const uint8_t> as_bytes(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

void check_vector(
    std::string_view seed_hex,
    std::string_view public_hex,
    std::string_view message_hex,
    std::string_view signature_hex
) {
    const auto seed = bytes_from_hex<32>(seed_hex);
    const auto expected_public = bytes_from_hex<32>(public_hex);
    const auto expected_signature = bytes_from_hex<64>(signature_hex);
    const auto message = message_from_hex(message_hex);

    signing::SecretKey secret{};
    signing::PublicKey public_key{};
    signing::key_pair_from_seed(seed, secret, public_key);
    OA_CHECK(public_key == expected_public);
    // The secret holds the seed and then the public key.
    OA_CHECK(std::equal(seed.begin(), seed.end(), secret.begin()));
    OA_CHECK(std::equal(public_key.begin(), public_key.end(), secret.begin() + 32));

    const auto signature = signing::sign(secret, message);
    OA_CHECK(signature == expected_signature);
    OA_CHECK(signing::verify(public_key, message, signature));

    if (!message.empty()) {
        auto flipped_message = message;
        flipped_message[0] ^= 0x01;
        OA_CHECK(!signing::verify(public_key, flipped_message, signature));
        // A span shorter than the signed message does not verify.
        OA_CHECK(
            !signing::verify(public_key, std::span<const uint8_t>(message.data(), 0), signature)
        );
        if (message.size() > 1)
            OA_CHECK(
                !signing::verify(public_key, std::span<const uint8_t>(message.data(), 1), signature)
            );
    }

    auto flipped_signature = signature;
    flipped_signature[0] ^= 0x01;
    OA_CHECK(!signing::verify(public_key, message, flipped_signature));

    auto flipped_key = public_key;
    flipped_key[0] ^= 0x01;
    OA_CHECK(!signing::verify(flipped_key, message, signature));

    const auto key_text = signing::public_key_text(public_key);
    OA_CHECK(key_text.view().size() == 52);
    OA_CHECK(key_text.view().substr(0, 8) == "ed25519:");
    const auto parsed_key = signing::parse_public_key(key_text.view());
    OA_CHECK(parsed_key.has_value() && *parsed_key == public_key);

    const auto signature_text = signing::signature_text(signature);
    OA_CHECK(signature_text.view().size() == 88);
    const auto parsed_signature = signing::parse_signature(signature_text.view());
    OA_CHECK(parsed_signature.has_value() && *parsed_signature == signature);
}

// RFC 8032 section 7.1. TEST 1024's message is 1023 bytes.
constexpr std::string_view message_1024 =
    "08b8b2b733424243760fe426a4b54908632110a66c2f6591eabd3345e3e4eb98"
    "fa6e264bf09efe12ee50f8f54e9f77b1e355f6c50544e23fb1433ddf73be84d8"
    "79de7c0046dc4996d9e773f4bc9efe5738829adb26c81b37c93a1b270b20329d"
    "658675fc6ea534e0810a4432826bf58c941efb65d57a338bbd2e26640f89ffbc"
    "1a858efcb8550ee3a5e1998bd177e93a7363c344fe6b199ee5d02e82d522c4fe"
    "ba15452f80288a821a579116ec6dad2b3b310da903401aa62100ab5d1a36553e"
    "06203b33890cc9b832f79ef80560ccb9a39ce767967ed628c6ad573cb116dbef"
    "efd75499da96bd68a8a97b928a8bbc103b6621fcde2beca1231d206be6cd9ec7"
    "aff6f6c94fcd7204ed3455c68c83f4a41da4af2b74ef5c53f1d8ac70bdcb7ed1"
    "85ce81bd84359d44254d95629e9855a94a7c1958d1f8ada5d0532ed8a5aa3fb2"
    "d17ba70eb6248e594e1a2297acbbb39d502f1a8c6eb6f1ce22b3de1a1f40cc24"
    "554119a831a9aad6079cad88425de6bde1a9187ebb6092cf67bf2b13fd65f270"
    "88d78b7e883c8759d2c4f5c65adb7553878ad575f9fad878e80a0c9ba63bcbcc"
    "2732e69485bbc9c90bfbd62481d9089beccf80cfe2df16a2cf65bd92dd597b07"
    "07e0917af48bbb75fed413d238f5555a7a569d80c3414a8d0859dc65a46128ba"
    "b27af87a71314f318c782b23ebfe808b82b0ce26401d2e22f04d83d1255dc51a"
    "ddd3b75a2b1ae0784504df543af8969be3ea7082ff7fc9888c144da2af58429e"
    "c96031dbcad3dad9af0dcbaaaf268cb8fcffead94f3c7ca495e056a9b47acdb7"
    "51fb73e666c6c655ade8297297d07ad1ba5e43f1bca32301651339e22904cc8c"
    "42f58c30c04aafdb038dda0847dd988dcda6f3bfd15c4b4c4525004aa06eeff8"
    "ca61783aacec57fb3d1f92b0fe2fd1a85f6724517b65e614ad6808d6f6ee34df"
    "f7310fdc82aebfd904b01e1dc54b2927094b2db68d6f903b68401adebf5a7e08"
    "d78ff4ef5d63653a65040cf9bfd4aca7984a74d37145986780fc0b16ac451649"
    "de6188a7dbdf191f64b5fc5e2ab47b57f7f7276cd419c17a3ca8e1b939ae49e4"
    "88acba6b965610b5480109c8b17b80e1b7b750dfc7598d5d5011fd2dcc5600a3"
    "2ef5b52a1ecc820e308aa342721aac0943bf6686b64b2579376504ccc493d97e"
    "6aed3fb0f9cd71a43dd497f01f17c0e2cb3797aa2a2f256656168e6c496afc5f"
    "b93246f6b1116398a346f1a641f3b041e989f7914f90cc2c7fff357876e506b5"
    "0d334ba77c225bc307ba537152f3f1610e4eafe595f6d9d90d11faa933a15ef1"
    "369546868a7f3a45a96768d40fd9d03412c091c6315cf4fde7cb68606937380d"
    "b2eaaa707b4c4185c32eddcdd306705e4dc1ffc872eeee475a64dfac86aba41c"
    "0618983f8741c5ef68d3a101e8a3b8cac60c905c15fc910840b94c00a0b9d0";

void rfc_8032_vectors() {
    check_vector(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "",
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
    );
    check_vector(
        "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "72",
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
    );
    check_vector(
        "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
        "af82",
        "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
        "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"
    );
    check_vector(
        "f5e5767cf153319517630f226876b86c8160cc583bc013744c6bf255f5cc0ee5",
        "278117fc144c72340f67d0f2316e8386ceffbf2b2428c9c51fef7c597f1d426e",
        message_1024,
        "0aab4c900501b3e24d7cdf4663326a3a87df5e4843b2cbdb67cbf6e460fec350"
        "aa5371b1508f9f4528ecea23c436d94b5e8fcd4f681e30a6ac00a9704a188a03"
    );
}

void empty_and_short_spans() {
    const auto public_key =
        bytes_from_hex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const auto signature =
        bytes_from_hex<64>("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                           "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    const std::span<const uint8_t> empty;
    OA_CHECK(signing::verify(public_key, empty, signature));
    const signing::Signature zeros{};
    OA_CHECK(!signing::verify(public_key, empty, zeros));
    const uint8_t one = 0x00;
    OA_CHECK(!signing::verify(public_key, std::span<const uint8_t>(&one, 1), signature));
    OA_CHECK(!signing::verify(public_key, std::span<const uint8_t>(&one, 1), zeros));
}

void text_forms() {
    const auto public_key =
        bytes_from_hex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const auto signature =
        bytes_from_hex<64>("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                           "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    constexpr std::string_view key_text = "ed25519:11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=";
    constexpr std::string_view signature_text =
        "5VZDAMNgrHKQhuLMgG6CioSHfx645dl02HPgZSJJAVVfuIIVkKM7rMYeOXAc+bRr0lv18FlbviRlUUFDjnoQCw==";
    OA_CHECK(signing::public_key_text(public_key).view() == key_text);
    OA_CHECK(signing::signature_text(signature).view() == signature_text);

    const auto parsed_key = signing::parse_public_key(key_text);
    OA_CHECK(parsed_key.has_value() && *parsed_key == public_key);
    const auto parsed_signature = signing::parse_signature(signature_text);
    OA_CHECK(parsed_signature.has_value() && *parsed_signature == signature);

    const std::string key = std::string(key_text);
    OA_CHECK(!signing::parse_public_key("ed25518:" + key.substr(8)));
    OA_CHECK(!signing::parse_public_key(key.substr(0, key.size() - 1)));
    OA_CHECK(!signing::parse_public_key(key + "x"));
    std::string bad_key = key;
    bad_key[10] = '!';
    OA_CHECK(!signing::parse_public_key(bad_key));
    OA_CHECK(!signing::parse_public_key(""));
    OA_CHECK(!signing::parse_public_key("ed25519:"));
    OA_CHECK(!signing::parse_public_key(key.substr(8)));

    const std::string sig = std::string(signature_text);
    OA_CHECK(!signing::parse_signature(sig.substr(0, sig.size() - 1)));
    OA_CHECK(!signing::parse_signature(sig + "x"));
    std::string bad_sig = sig;
    bad_sig[0] = '!';
    OA_CHECK(!signing::parse_signature(bad_sig));
    OA_CHECK(!signing::parse_signature(""));
    OA_CHECK(!signing::parse_signature("ed25519:" + sig));
}

void base64_vectors() {
    struct Example {
        std::string_view raw;
        std::string_view encoded;
    };

    // RFC 4648 section 10.
    constexpr Example examples[] = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };
    for (const Example& example : examples) {
        const auto bytes = as_bytes(example.raw);
        std::vector<char> encoded(signing::base64_encoded_size(bytes.size()));
        OA_CHECK(signing::base64_encode(bytes, encoded));
        OA_CHECK(std::string_view(encoded.data(), encoded.size()) == example.encoded);
        std::vector<uint8_t> decoded(bytes.size());
        const auto written = signing::base64_decode(example.encoded, decoded);
        OA_CHECK(written.has_value() && *written == bytes.size());
        OA_CHECK(std::equal(bytes.begin(), bytes.end(), decoded.begin()));
        if (!encoded.empty()) {
            encoded.pop_back();
            OA_CHECK(!signing::base64_encode(bytes, encoded));
        }
    }

    std::array<uint8_t, 8> buffer{};
    for (const char* bad :
         {"Zg=",
          "Zg===",
          "Zm9vYg",
          "Zm9vYg=",
          "====",
          "Z===",
          "Zm9vYmFy=",
          "@@@@",
          "Zg== ",
          "Zm9vYmF*",
          "\nZm9v",
          "Zm9v====",
          "Zg==Zm9v",
          "Zh=="}) {
        OA_CHECK(!signing::base64_decode(bad, buffer));
    }
}

void fingerprint_of_test_1() {
    // python3 -c 'import hashlib; print(hashlib.sha256(bytes.fromhex(
    // "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")).hexdigest())'
    // The first 16 hex digits are 21fe31dfa154a261, grouped by four.
    const auto public_key =
        bytes_from_hex<32>("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    constexpr std::string_view expected = "21fe \xC2\xB7 31df \xC2\xB7 a154 \xC2\xB7 a261";
    OA_CHECK(expected.size() == 28);
    const auto text = signing::fingerprint(public_key);
    OA_CHECK(text.view() == expected);
    OA_CHECK(text.view().size() == 28);
}

} // namespace

int main() {
    rfc_8032_vectors();
    empty_and_short_spans();
    text_forms();
    base64_vectors();
    fingerprint_of_test_1();
    const int status = oa::test::check_exit_status();
    if (status == 0)
        std::puts("base-signing: ok");
    return status;
}
