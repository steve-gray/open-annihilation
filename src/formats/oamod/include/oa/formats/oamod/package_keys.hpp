// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The homepage, tags and engine requirement shared by every package manifest:
// oamod.yaml, language.yaml and, later, oamap.yaml. One grammar, so a
// catalogue entry and a package agree on what a player may open, how a mod
// is filed, and which Open Annihilation the package runs on.
#pragma once

#include "oa/formats/oamod.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::formats::oamod {

/// One release of Open Annihilation, as major.minor.patch.
struct EngineVersion {
    uint16_t major{};
    uint16_t minor{};
    uint16_t patch{};

    /// Orders two releases by major, then minor, then patch.
    friend auto operator<=>(const EngineVersion&, const EngineVersion&) = default;
};

/// Reads MAJOR.MINOR.PATCH.
///
/// Each part is a decimal from 0 to 65535 with no leading zero; a lone 0 is
/// a part. The whole text must be the version.
///
/// @param text the version
/// @return the version, or nothing when the text is not one
[[nodiscard]] std::optional<EngineVersion> parse_engine_version(std::string_view text) noexcept;

/// Writes a release as MAJOR.MINOR.PATCH, with no leading zeros.
///
/// @param version the release
/// @return its text
[[nodiscard]] std::string engine_version_text(EngineVersion version);

/// How one comparison in an engine requirement relates this build to a release.
enum class EngineComparison : uint8_t {
    at_least, ///< this build is that release or a later one
    above,    ///< this build is later than that release
    at_most,  ///< this build is that release or an earlier one
    below,    ///< this build is earlier than that release
    exactly,  ///< this build is that release
};

/// An engine requirement: one to four comparisons, all of which must hold.
struct EngineRange {
    std::vector<std::pair<EngineComparison, EngineVersion>> terms{};
};

/// Reads an engine requirement.
///
/// One to four comparisons, separated by commas that spaces may surround.
/// Each comparison is an operator (`>=`, `>`, `<=`, `<` or `=`), optional
/// spaces, then a version. Nothing else may appear, and the whole text is
/// at most 64 bytes. A value that starts with `>` is a block scalar unless
/// it is quoted, and the reader of the manifest refuses it before this.
///
/// @param text the requirement, as written
/// @param[out] error what is wrong and the 1-based byte where, when the text
///        is not a requirement; may be null
/// @return the requirement, or nothing when the text is not one
[[nodiscard]] std::optional<EngineRange>
parse_engine_range(std::string_view text, std::string* error = nullptr);

/// Tells whether a release meets every comparison of a requirement.
///
/// @param range the requirement
/// @param version the release
/// @return true when every comparison holds
[[nodiscard]] bool engine_range_met(const EngineRange& range, EngineVersion version) noexcept;

/// Describes a requirement the way a player reads it.
///
/// Each comparison is "0.8.0 or later", "later than 0.8.0", "0.9.0 or
/// earlier", "before 0.9.0" or "exactly 0.8.0", and the comparisons are
/// joined with ", ".
///
/// @param range the requirement
/// @return that wording
[[nodiscard]] std::string describe_engine_range(const EngineRange& range);

/// Tells whether a homepage is an address Open Annihilation can open.
///
/// A string of 1 to 256 bytes that starts with `http://` or `https://`, the
/// scheme in either case, with at least one byte after the scheme and no
/// ASCII control character, delete or space.
///
/// @param text the address
/// @return true when it is such an address
[[nodiscard]] bool homepage_valid(std::string_view text) noexcept;

/// Tells whether a tag is lower-case kebab-case of 1 to 32 bytes.
///
/// Words of `a`-`z` and `0`-`9` joined by single hyphens, not starting or
/// ending with a hyphen.
///
/// @param text the tag
/// @return true when it is such a tag
[[nodiscard]] bool tag_valid(std::string_view text) noexcept;

/// The three shared keys, as written, when each one passed its rule.
struct PackageKeys {
    /// The homepage. Empty when the manifest has none, or the value was refused.
    std::string homepage{};
    /// The tags, in the order written. Empty when the manifest has none, or the list was refused.
    std::vector<std::string> tags{};
    /// The engine requirement, as written. Empty when the manifest has none, or it did not parse.
    std::string requires_engine{};
};

/// One broken rule of a shared key, where it is written.
struct KeyProblem {
    TextPosition position{}; ///< where the value starts
    std::string path{};      ///< homepage, tags, tags[2] or requires.engine
    std::string message{};   ///< what is wrong
};

/// Reads homepage and tags from a manifest, and engine from its requires block.
///
/// A key that is absent is left empty. A key that breaks a rule appends one
/// problem and is left empty; the other keys are still read. An empty tag
/// list is refused: the key is left out instead. `requires_block` is null
/// when the manifest has no requires mapping, and then no engine is read.
/// The caller decides whether an unmet requirement refuses the package.
///
/// @param root the manifest's top-level mapping
/// @param requires_block the requires mapping, or null
/// @param[in,out] keys the values that passed; a refused key is left as it was
/// @param[in,out] problems one entry appended for each broken rule
void read_package_keys(
    const Node& root,
    const Node* requires_block,
    PackageKeys& keys,
    std::vector<KeyProblem>& problems
);

} // namespace oa::formats::oamod
