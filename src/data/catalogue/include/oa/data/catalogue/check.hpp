// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The check a fetched catalogue goes through before anything is installed
// from it: the detached signature over the exact bytes, the registry it
// names, a sequence that never goes back, and whether it is past expires.
// Reading the entries is catalogue.hpp. Nothing here fetches.
#pragma once

#include "oa/base/signing/ed25519.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/registry/descriptor.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::catalogue {

/// One line of a detached signature file.
struct SignatureLine {
    std::string key_id;                   ///< the id the line names
    base::signing::Signature signature{}; ///< the Ed25519 signature
};

/// Reads a detached signature file.
///
/// The file is ASCII, at most max_signature_file_bytes, and one line:
/// `ed25519`, the key id and the signature, separated by single spaces. A
/// line end, LF or CRLF, may follow, and nothing else may.
///
/// @param bytes the file's bytes
/// @param[out] error why the file was refused; may be null
/// @return the line, or nothing when the file is not one
[[nodiscard]] std::optional<SignatureLine>
parse_signature_file(std::span<const uint8_t> bytes, std::string* error);

/// Writes a detached signature file, with a trailing line end.
///
/// @param line the line
/// @return the ed25519 line and a trailing line end
[[nodiscard]] std::string signature_file_text(const SignatureLine& line);

/// The last catalogue the caller kept, when it has kept one.
struct Previous {
    std::optional<int64_t> sequence;            ///< that catalogue's sequence
    std::optional<base::sha256::Digest> digest; ///< the SHA-256 of its exact bytes
};

/// Why a catalogue was kept or refused.
enum class Verdict : uint8_t {
    accepted,              ///< the catalogue is the one to keep
    unchanged,             ///< the same bytes as the last catalogue that was kept
    too_large,             ///< more than max_catalogue_bytes
    no_signature,          ///< a signed registry sent no signature
    bad_signature_file,    ///< the signature file is not the one line
    unknown_key,           ///< the signature names a key that is not trusted
    bad_signature,         ///< the signature does not match the bytes
    malformed,             ///< the bytes are not a catalogue
    wrong_version,         ///< catalogue is not 1
    wrong_registry,        ///< the catalogue names another registry
    sequence_went_back,    ///< the sequence is lower than the last one kept
    sequence_not_advanced, ///< the sequence did not move on for new bytes
};

/// The verdict as a short sentence.
///
/// @param verdict the verdict
/// @return a stable description; never null
[[nodiscard]] const char* verdict_text(Verdict verdict) noexcept;

/// What the caller knows when it asks for a catalogue to be checked.
struct CheckRequest {
    std::span<const uint8_t> catalogue; ///< the catalogue's exact bytes
    std::span<const uint8_t> signature; ///< the signature file; empty when there is none
    std::string_view registry_id;       ///< the registry the bytes were fetched for
    std::span<const registry::RegistryKey>
        trusted_keys;      ///< the keys that may sign it; empty when unsigned
    bool built_in = false; ///< the registry's keys come from the engine build
    Previous previous;     ///< the last catalogue kept for this registry
    int64_t now = 0;       ///< the caller's clock, seconds since 1970
};

/// The result of checking one catalogue.
struct Checked {
    Verdict verdict = Verdict::malformed; ///< why the bytes were kept or refused
    std::string detail;                   ///< the particular reason, when there is one
    /// The catalogue. Set only when the bytes are usable.
    std::shared_ptr<const Catalogue> catalogue;
    base::sha256::Digest digest{}; ///< the SHA-256 of the exact bytes, after the signature
    std::string signed_by;         ///< the trusted key the signature named; empty when unsigned
    bool expired = false;          ///< `now` is at or after expires; the catalogue is still usable
    /// The keys an added registry published, when they differ from the trusted set.
    std::optional<std::vector<registry::RegistryKey>> new_keys;

    /// Reports whether the catalogue may be shown and installed from.
    ///
    /// An expired catalogue is still usable. A refused one is not.
    ///
    /// @return true for accepted and unchanged
    [[nodiscard]] bool usable() const noexcept {
        return verdict == Verdict::accepted || verdict == Verdict::unchanged;
    }
};

/// Checks a fetched catalogue.
///
/// The size is checked first. For a registry that names a key, the signature
/// file is read and verified over the exact bytes before those bytes are
/// parsed. An unsigned registry skips the signature. The version, the
/// registry id and the sequence come next, then the entries. A lower sequence
/// is refused. The same sequence with the same bytes is unchanged, and with
/// other bytes is refused. A catalogue whose expires is at or before `now`
/// is still returned, with expired set. An added registry whose keys are
/// present, not empty and different from the trusted set reports that set.
/// A built-in registry's keys are ignored.
///
/// @param request the bytes, the registry and the caller's clock
/// @return the verdict. catalogue is set only when the bytes are usable
[[nodiscard]] Checked check_catalogue(const CheckRequest& request);

} // namespace oa::data::catalogue
