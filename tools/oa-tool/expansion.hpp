// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Packing and checking an expansion package. The game's commands pass a null
// table and use the built-in one. A test passes its own table, whose bytes
// the built-in table does not know; the game never does.
#pragma once

#include "command.hpp"

#include "oa/data/content_bundles.hpp"

#include <filesystem>
#include <string>

namespace oa::tool {

/// Packs a folder whose top holds oaexp.yaml.
///
/// `files` null checks the built-in table. The manifest is still read
/// against that table, so the id, the name, extends and needs are the
/// expansion's. Every file is stored. The manifest is written first.
///
/// @param folder the folder
/// @param files the table the bytes must match; null for the built-in table
/// @param out_path the --out path; empty for `<id>.oaexp` in the current folder
/// @param force an existing output may be replaced
/// @param[in,out] output receives the packed line, and the reason on failure
/// @return exit_done, or exit_failed when the folder cannot be packed
[[nodiscard]] int pack_expansion(
    const std::filesystem::path& folder,
    const oa::data::content_bundles::Bundle* files,
    const std::string& out_path,
    bool force,
    Output& output
);

/// Checks a package against `files`.
///
/// `files` null uses the built-in table. A package whose bytes that table
/// does not know is refused. This does not ask the generic installer to
/// unpack the archive.
///
/// @param package the .oaexp file
/// @param files the table the bytes must match; null for the built-in table
/// @param[in,out] output receives result: ok, or the reason
/// @return exit_done when the package matches, otherwise exit_failed
[[nodiscard]] int check_expansion_package(
    const std::filesystem::path& package,
    const oa::data::content_bundles::Bundle* files,
    Output& output
);

} // namespace oa::tool
