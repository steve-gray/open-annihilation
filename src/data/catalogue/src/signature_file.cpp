// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/catalogue/check.hpp"

#include "oa/data/registry/descriptor.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace oa::data::catalogue {
namespace {

/// Sets `error` when the caller asked for one.
void set_error(std::string* error, std::string_view message) {
    if (error)
        *error = std::string(message);
}

} // namespace

std::optional<SignatureLine>
parse_signature_file(std::span<const uint8_t> bytes, std::string* error) {
    if (bytes.size() > max_signature_file_bytes) {
        set_error(error, "the signature file is larger than 256 bytes");
        return std::nullopt;
    }
    std::string text;
    text.reserve(bytes.size());
    for (const uint8_t byte : bytes) {
        if (byte > 0x7E || (byte < 0x20 && byte != '\n' && byte != '\r')) {
            set_error(error, "the signature file is not one line");
            return std::nullopt;
        }
        text.push_back(static_cast<char>(byte));
    }
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
        if (!text.empty() && text.back() == '\r')
            text.pop_back();
    }
    if (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos) {
        set_error(error, "the signature file is not one line");
        return std::nullopt;
    }
    const auto first = text.find(' ');
    const auto second = first == std::string::npos ? std::string::npos : text.find(' ', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        text.find(' ', second + 1) != std::string::npos) {
        set_error(error, "the signature file is not three fields");
        return std::nullopt;
    }
    const std::string_view algorithm(text.data(), first);
    const std::string_view key_id(text.data() + first + 1, second - first - 1);
    const std::string_view signature(text.data() + second + 1, text.size() - (second + 1));
    if (algorithm != "ed25519") {
        set_error(error, "the signature file is not ed25519");
        return std::nullopt;
    }
    if (!registry::valid_key_id(key_id)) {
        set_error(error, "the signature file names no key id");
        return std::nullopt;
    }
    const auto parsed = base::signing::parse_signature(signature);
    if (!parsed) {
        set_error(error, "the signature is not base64");
        return std::nullopt;
    }
    return SignatureLine{std::string(key_id), *parsed};
}

std::string signature_file_text(const SignatureLine& line) {
    std::string text = "ed25519 ";
    text += line.key_id;
    text += ' ';
    text.append(base::signing::signature_text(line.signature).view());
    text += '\n';
    return text;
}

const char* verdict_text(Verdict verdict) noexcept {
    switch (verdict) {
    case Verdict::accepted:
        return "accepted";
    case Verdict::unchanged:
        return "unchanged";
    case Verdict::too_large:
        return "the catalogue is larger than 16 MiB";
    case Verdict::no_signature:
        return "the catalogue has no signature";
    case Verdict::bad_signature_file:
        return "the signature file is not one line";
    case Verdict::unknown_key:
        return "the signature names a key that is not trusted";
    case Verdict::bad_signature:
        return "the signature does not match the catalogue";
    case Verdict::malformed:
        return "the catalogue is malformed";
    case Verdict::wrong_version:
        return "the catalogue is not version 1";
    case Verdict::wrong_registry:
        return "the catalogue names another registry";
    case Verdict::sequence_went_back:
        return "the sequence went backwards";
    case Verdict::sequence_not_advanced:
        return "the sequence did not advance";
    }
    return "";
}

} // namespace oa::data::catalogue
