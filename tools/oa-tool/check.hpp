// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The facts `check` reports for one kind of package, written into an object
// a caller has already started. The command writes the keys every kind
// shares, then one of these. A test calls them on their own.

#pragma once

#include "oa/app/package_install.hpp"
#include "oa/formats/json.hpp"

#include <cstdint>
#include <span>

namespace oa::tool {

/// Writes a mod's catalogue facts into an object that is already open.
///
/// Resolves the packaged profile again with no player settings and with
/// hacks this build does not carry out yet accepted, which is the resolution
/// a catalogue compares. From that resolution it writes summary, homepage,
/// tags, author, requires, sim_hash, full_hash, hacks and packaging. A key
/// the profile does not have is left out. requires.catalogue is left out.
///
/// @param package a mod package `open_package` accepted
/// @param[in,out] writer the object the facts are written into
void describe_oamod(
    const oa::app::package_install::Package& package, oa::formats::json::JsonWriter& writer
);

/// Writes a language pack's catalogue facts into an object that is already open.
///
/// Reads the manifest's own tree: id is the tag, name and version are the
/// manifest's, and revision is packaging.revision when that is a whole
/// number, otherwise null. Then the language object, whose keys are tag,
/// name, english-name, word, locales, fallbacks, text-needs and unicode.
/// A key the manifest does not have is left out, apart from revision.
///
/// @param manifest the language.yaml bytes
/// @param[in,out] writer the object the facts are written into
void describe_oalang(std::span<const uint8_t> manifest, oa::formats::json::JsonWriter& writer);

} // namespace oa::tool
