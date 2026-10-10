// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A fetched catalogue: the signature over the exact bytes, the registry,
// the sequence, expiry and a published key set.

#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/test/check.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;
namespace sha = oa::base::sha256;
namespace signing = oa::base::signing;

std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

registry::RegistryKey make_key(std::string id, uint8_t fill) {
    std::array<uint8_t, 32> seed{};
    seed.fill(fill);
    signing::SecretKey secret{};
    registry::RegistryKey key;
    key.id = std::move(id);
    signing::key_pair_from_seed(seed, secret, key.key);
    secret.fill(0);
    return key;
}

std::string key_object(const registry::RegistryKey& key) {
    return "{\"id\":\"" + key.id + "\",\"public\":\"" +
           std::string(signing::public_key_text(key.key).view()) + "\"}";
}

std::string minimal(std::string_view registry_id, int sequence, std::string_view extra = {}) {
    std::string json = "{\"catalogue\":1,\"registry\":\"";
    json += registry_id;
    json += "\",\"sequence\":";
    json += std::to_string(sequence);
    json += ",\"generated\":\"2026-01-01T00:00:00Z\",\"expires\":\"2026-02-01T00:00:00Z\"";
    if (!extra.empty()) {
        json += ',';
        json += extra;
    }
    json += ",\"packages\":[]}";
    return json;
}

struct SignedBytes {
    std::vector<uint8_t> catalogue;
    std::vector<uint8_t> signature;
    signing::PublicKey key{};
};

SignedBytes sign_text(
    std::string_view json, std::string_view key_id, uint8_t fill, std::string_view ending = "\n"
) {
    SignedBytes out;
    out.catalogue = bytes_of(json);
    std::array<uint8_t, 32> seed{};
    seed.fill(fill);
    signing::SecretKey secret{};
    signing::key_pair_from_seed(seed, secret, out.key);
    seed.fill(0);
    const signing::Signature signature = signing::sign(secret, out.catalogue);
    secret.fill(0);
    catalogue::SignatureLine line{std::string(key_id), signature};
    std::string text = catalogue::signature_file_text(line);
    OA_CHECK(!text.empty() && text.back() == '\n');
    text.pop_back();
    text += ending;
    out.signature = bytes_of(text);
    return out;
}

catalogue::Checked check_of(
    const SignedBytes& signed_bytes,
    const std::vector<registry::RegistryKey>& keys,
    std::string_view registry_id = "coreprime"
) {
    catalogue::CheckRequest request;
    request.catalogue = signed_bytes.catalogue;
    request.signature = signed_bytes.signature;
    request.registry_id = registry_id;
    request.trusted_keys = keys;
    return catalogue::check_catalogue(request);
}

void test_signature_line() {
    const SignedBytes signed_bytes = sign_text(minimal("coreprime", 1), "release-2026", 0x11);
    std::string error = "none";
    const auto line = catalogue::parse_signature_file(signed_bytes.signature, &error);
    OA_CHECK(line.has_value());
    OA_CHECK(error == "none");
    if (line) {
        OA_CHECK(line->key_id == "release-2026");
        const std::string written = catalogue::signature_file_text(*line);
        OA_CHECK(!written.empty() && written.back() == '\n');
        const auto again = catalogue::parse_signature_file(bytes_of(written), &error);
        OA_CHECK(again.has_value());
        if (again) {
            OA_CHECK(again->key_id == line->key_id);
            OA_CHECK(again->signature == line->signature);
        }
    }
    const registry::RegistryKey key = make_key("release-2026", 0x11);
    const std::vector<registry::RegistryKey> keys = {key};
    OA_CHECK(
        check_of(sign_text(minimal("coreprime", 1), "release-2026", 0x11, ""), keys).verdict ==
        catalogue::Verdict::accepted
    );
    OA_CHECK(
        check_of(sign_text(minimal("coreprime", 1), "release-2026", 0x11, "\n"), keys).verdict ==
        catalogue::Verdict::accepted
    );
    OA_CHECK(
        check_of(sign_text(minimal("coreprime", 1), "release-2026", 0x11, "\r\n"), keys).verdict ==
        catalogue::Verdict::accepted
    );
}

void test_accepted_and_signature_failures() {
    const registry::RegistryKey first = make_key("release-2026", 0x11);
    const registry::RegistryKey second = make_key("release-2027", 0x22);
    const std::vector<registry::RegistryKey> both = {first, second};
    const SignedBytes signed_bytes = sign_text(minimal("coreprime", 4), "release-2027", 0x22);
    const catalogue::Checked accepted = check_of(signed_bytes, both);
    OA_CHECK(accepted.verdict == catalogue::Verdict::accepted);
    OA_CHECK(accepted.usable());
    OA_CHECK(accepted.catalogue != nullptr);
    OA_CHECK(accepted.signed_by == "release-2027");
    OA_CHECK(!accepted.expired);
    OA_CHECK(!accepted.new_keys);
    OA_CHECK(accepted.digest == sha::digest_of(signed_bytes.catalogue));
    if (accepted.catalogue) {
        OA_CHECK(accepted.catalogue->registry == "coreprime");
        OA_CHECK(accepted.catalogue->sequence == 4);
    }
    OA_CHECK(std::string(catalogue::verdict_text(catalogue::Verdict::accepted)) == "accepted");

    const SignedBytes other_name = sign_text(minimal("coreprime", 4), "other-key", 0x11);
    const catalogue::Checked unknown = check_of(other_name, {first});
    OA_CHECK(unknown.verdict == catalogue::Verdict::unknown_key);
    OA_CHECK(unknown.catalogue == nullptr);
    OA_CHECK(!unknown.usable());

    SignedBytes flipped_catalogue = signed_bytes;
    flipped_catalogue.catalogue[0] ^= 0x20;
    const catalogue::Checked bad_bytes = check_of(flipped_catalogue, both);
    OA_CHECK(bad_bytes.verdict == catalogue::Verdict::bad_signature);
    OA_CHECK(bad_bytes.catalogue == nullptr);

    auto line = catalogue::parse_signature_file(signed_bytes.signature, nullptr);
    OA_CHECK(line.has_value());
    if (line) {
        line->signature[0] ^= 0x01;
        SignedBytes flipped_signature = signed_bytes;
        flipped_signature.signature = bytes_of(catalogue::signature_file_text(*line));
        const catalogue::Checked bad_signature = check_of(flipped_signature, both);
        OA_CHECK(bad_signature.verdict == catalogue::Verdict::bad_signature);
    }

    const SignedBytes signed_other = sign_text("[]", "release-2026", 0x11);
    const std::vector<uint8_t> forged_bytes = bytes_of("{");
    catalogue::CheckRequest forged;
    forged.catalogue = forged_bytes;
    forged.signature = signed_other.signature;
    forged.registry_id = "coreprime";
    forged.trusted_keys = std::span<const registry::RegistryKey>(&first, 1);
    const catalogue::Checked forged_result = catalogue::check_catalogue(forged);
    OA_CHECK(forged_result.verdict == catalogue::Verdict::bad_signature);
    OA_CHECK(forged_result.verdict != catalogue::Verdict::malformed);

    const SignedBytes signed_malformed = sign_text("{", "release-2026", 0x11);
    const catalogue::Checked malformed = check_of(signed_malformed, {first});
    OA_CHECK(malformed.verdict == catalogue::Verdict::malformed);
    OA_CHECK(malformed.catalogue == nullptr);

    catalogue::CheckRequest missing;
    missing.catalogue = signed_bytes.catalogue;
    missing.registry_id = "coreprime";
    missing.trusted_keys = std::span<const registry::RegistryKey>(&first, 1);
    const catalogue::Checked no_signature = catalogue::check_catalogue(missing);
    OA_CHECK(no_signature.verdict == catalogue::Verdict::no_signature);
    OA_CHECK(no_signature.catalogue == nullptr);

    const std::string signature_body(88, 'A');
    const std::vector<std::string> bad_files = {
        "ed25519  release-2026 " + signature_body,
        "ed25519 release-2026 " + signature_body + " extra",
        "ed448 release-2026 " + signature_body,
        "ed25519 release-2026 " + std::string(88, '!'),
        std::string(257, 'x'),
    };
    for (const std::string& file : bad_files) {
        const std::vector<uint8_t> signature_bytes = bytes_of(file);
        catalogue::CheckRequest request;
        request.catalogue = signed_bytes.catalogue;
        request.signature = signature_bytes;
        request.registry_id = "coreprime";
        request.trusted_keys = std::span<const registry::RegistryKey>(&first, 1);
        const catalogue::Checked result = catalogue::check_catalogue(request);
        OA_CHECK(result.verdict == catalogue::Verdict::bad_signature_file);
        OA_CHECK(result.catalogue == nullptr);
    }
}

void test_registry_and_sequence() {
    const registry::RegistryKey key = make_key("release-2026", 0x11);
    const std::vector<registry::RegistryKey> keys = {key};
    const SignedBytes other_registry = sign_text(minimal("alpha", 1), "release-2026", 0x11);
    const catalogue::Checked wrong = check_of(other_registry, keys, "beta");
    OA_CHECK(wrong.verdict == catalogue::Verdict::wrong_registry);
    OA_CHECK(wrong.verdict != catalogue::Verdict::bad_signature);
    OA_CHECK(wrong.catalogue == nullptr);

    const std::string version_two = "{\"catalogue\":2,\"registry\":\"other\",\"sequence\":1,"
                                    "\"generated\":\"2026-01-01T00:00:00Z\","
                                    "\"expires\":\"2026-02-01T00:00:00Z\",\"packages\":[]}";
    const catalogue::Checked version = check_of(sign_text(version_two, "release-2026", 0x11), keys);
    OA_CHECK(version.verdict == catalogue::Verdict::wrong_version);
    OA_CHECK(version.catalogue == nullptr);

    const SignedBytes older = sign_text(minimal("coreprime", 4), "release-2026", 0x11);
    const SignedBytes same = sign_text(minimal("coreprime", 5), "release-2026", 0x11);
    const SignedBytes changed = sign_text(
        minimal("coreprime", 5, "\"downloads\":\"http://downloads.example.net/a\""),
        "release-2026",
        0x11
    );

    catalogue::CheckRequest back;
    back.catalogue = older.catalogue;
    back.signature = older.signature;
    back.registry_id = "coreprime";
    back.trusted_keys = keys;
    back.previous.sequence = 5;
    const catalogue::Checked went_back = catalogue::check_catalogue(back);
    OA_CHECK(went_back.verdict == catalogue::Verdict::sequence_went_back);
    OA_CHECK(went_back.catalogue == nullptr);
    OA_CHECK(!went_back.usable());

    catalogue::CheckRequest forward = back;
    forward.catalogue = same.catalogue;
    forward.signature = same.signature;
    forward.previous.sequence = 4;
    const catalogue::Checked advanced = catalogue::check_catalogue(forward);
    OA_CHECK(advanced.verdict == catalogue::Verdict::accepted);
    OA_CHECK(advanced.usable());

    catalogue::CheckRequest identical = forward;
    identical.previous.sequence = 5;
    identical.previous.digest = sha::digest_of(same.catalogue);
    const catalogue::Checked unchanged = catalogue::check_catalogue(identical);
    OA_CHECK(unchanged.verdict == catalogue::Verdict::unchanged);
    OA_CHECK(unchanged.usable());
    OA_CHECK(unchanged.catalogue != nullptr);
    OA_CHECK(unchanged.digest == sha::digest_of(same.catalogue));

    catalogue::CheckRequest moved = identical;
    moved.catalogue = changed.catalogue;
    moved.signature = changed.signature;
    const catalogue::Checked not_advanced = catalogue::check_catalogue(moved);
    OA_CHECK(not_advanced.verdict == catalogue::Verdict::sequence_not_advanced);
    OA_CHECK(not_advanced.catalogue == nullptr);

    identical.previous.digest = std::nullopt;
    const catalogue::Checked missing_digest = catalogue::check_catalogue(identical);
    OA_CHECK(missing_digest.verdict == catalogue::Verdict::sequence_not_advanced);
}

void test_expiry_rotation_and_size() {
    const registry::RegistryKey old_key = make_key("release-2026", 0x11);
    const registry::RegistryKey new_key = make_key("release-2027", 0x22);
    const SignedBytes signed_bytes = sign_text(minimal("coreprime", 1), "release-2026", 0x11);
    const auto expires = catalogue::parse_utc_time("2026-02-01T00:00:00Z");
    OA_CHECK(expires.has_value());
    catalogue::CheckRequest request;
    request.catalogue = signed_bytes.catalogue;
    request.signature = signed_bytes.signature;
    request.registry_id = "coreprime";
    request.trusted_keys = std::span<const registry::RegistryKey>(&old_key, 1);
    request.now = *expires;
    const catalogue::Checked expired = catalogue::check_catalogue(request);
    OA_CHECK(expired.verdict == catalogue::Verdict::accepted);
    OA_CHECK(expired.expired);
    OA_CHECK(expired.usable());
    OA_CHECK(expired.catalogue != nullptr);
    request.now = *expires - 1;
    const catalogue::Checked fresh = catalogue::check_catalogue(request);
    OA_CHECK(fresh.usable());
    OA_CHECK(!fresh.expired);

    const std::vector<uint8_t> bytes = bytes_of(minimal("coreprime", 1));
    catalogue::CheckRequest unsigned_request;
    unsigned_request.catalogue = bytes;
    unsigned_request.registry_id = "coreprime";
    const catalogue::Checked unsigned_catalogue = catalogue::check_catalogue(unsigned_request);
    OA_CHECK(unsigned_catalogue.verdict == catalogue::Verdict::accepted);
    OA_CHECK(unsigned_catalogue.usable());
    OA_CHECK(unsigned_catalogue.signed_by.empty());
    const std::vector<uint8_t> not_a_signature = bytes_of("not a signature");
    unsigned_request.signature = not_a_signature;
    const catalogue::Checked ignored = catalogue::check_catalogue(unsigned_request);
    OA_CHECK(ignored.verdict == catalogue::Verdict::accepted);

    const SignedBytes rotated = sign_text(
        minimal("coreprime", 2, "\"keys\":[" + key_object(new_key) + "]"), "release-2026", 0x11
    );
    const catalogue::Checked rotation = check_of(rotated, {old_key});
    OA_CHECK(rotation.verdict == catalogue::Verdict::accepted);
    OA_CHECK(rotation.new_keys.has_value());
    if (rotation.new_keys) {
        OA_CHECK(rotation.new_keys->size() == 1);
        if (rotation.new_keys->size() == 1) {
            OA_CHECK((*rotation.new_keys)[0].id == new_key.id);
            OA_CHECK((*rotation.new_keys)[0].key == new_key.key);
        }
    }
    catalogue::CheckRequest built_in;
    built_in.catalogue = rotated.catalogue;
    built_in.signature = rotated.signature;
    built_in.registry_id = "coreprime";
    built_in.trusted_keys = std::span<const registry::RegistryKey>(&old_key, 1);
    built_in.built_in = true;
    const catalogue::Checked ignored_keys = catalogue::check_catalogue(built_in);
    OA_CHECK(ignored_keys.verdict == catalogue::Verdict::accepted);
    OA_CHECK(!ignored_keys.new_keys);

    const std::string same_set =
        "\"keys\":[" + key_object(new_key) + "," + key_object(old_key) + "]";
    const SignedBytes reordered =
        sign_text(minimal("coreprime", 2, same_set), "release-2026", 0x11);
    const std::vector<registry::RegistryKey> trusted = {old_key, new_key};
    const catalogue::Checked same_keys = check_of(reordered, trusted);
    OA_CHECK(same_keys.verdict == catalogue::Verdict::accepted);
    OA_CHECK(!same_keys.new_keys);

    const SignedBytes empty_keys =
        sign_text(minimal("coreprime", 2, "\"keys\":[]"), "release-2026", 0x11);
    OA_CHECK(!check_of(empty_keys, {old_key}).new_keys);
    const SignedBytes absent = sign_text(minimal("coreprime", 2), "release-2026", 0x11);
    OA_CHECK(!check_of(absent, {old_key}).new_keys);

    const SignedBytes published = sign_text(
        minimal("coreprime", 8, "\"keys\":[" + key_object(new_key) + "]"), "release-2026", 0x11
    );
    catalogue::Previous previous;
    previous.sequence = 8;
    previous.digest = sha::digest_of(published.catalogue);
    catalogue::CheckRequest unchanged;
    unchanged.catalogue = published.catalogue;
    unchanged.signature = published.signature;
    unchanged.registry_id = "coreprime";
    unchanged.trusted_keys = std::span<const registry::RegistryKey>(&old_key, 1);
    unchanged.previous = previous;
    const catalogue::Checked unchanged_rotation = catalogue::check_catalogue(unchanged);
    OA_CHECK(unchanged_rotation.verdict == catalogue::Verdict::unchanged);
    OA_CHECK(unchanged_rotation.new_keys.has_value());

    std::vector<uint8_t> too_big(catalogue::max_catalogue_bytes + 1, '{');
    catalogue::CheckRequest huge;
    huge.catalogue = too_big;
    huge.registry_id = "coreprime";
    huge.trusted_keys = std::span<const registry::RegistryKey>(&old_key, 1);
    const catalogue::Checked oversized = catalogue::check_catalogue(huge);
    OA_CHECK(oversized.verdict == catalogue::Verdict::too_large);
    OA_CHECK(oversized.catalogue == nullptr);

    std::vector<uint8_t> exact(catalogue::max_catalogue_bytes, '{');
    huge.catalogue = exact;
    const catalogue::Checked at_limit = catalogue::check_catalogue(huge);
    OA_CHECK(at_limit.verdict == catalogue::Verdict::no_signature);
}

} // namespace

int main() {
    test_signature_line();
    test_accepted_and_signature_failures();
    test_registry_and_sequence();
    test_expiry_rotation_and_size();
    return oa::test::check_exit_status();
}
