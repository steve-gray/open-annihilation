// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Download counts a registry publishes for its packages. The Library asks
// for them after a catalogue check and shows the last ones it could read.
#pragma once

#include "oa/app/content/service.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/http/client.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace oa::app::content {

/// The folder under the content folder that holds one registry's counts.
inline constexpr std::string_view counts_folder_name = "counts";

/// How many times each package was downloaded, as one counts document gives it.
struct CountsTable {
    /// Downloads by catalogue kind (`oalang`, `oamod`, `oamap`) and package key.
    std::map<std::pair<std::string, std::string>, uint64_t> by_kind_and_key{};
    std::string generated{}; ///< the document's UTC time, as it was written
};

/// Reads a counts document.
///
/// The document is at most 1 MiB. `counts` is 1, `generated` is a UTC time,
/// and each package's `downloads` is a whole number from 0 up. A package
/// whose kind is not a catalogue kind is left out. Members this reader does
/// not know are left out. Anything else is refused.
///
/// @param bytes the document
/// @param[out] error why it was refused; may be null
/// @return the table, or nothing when the document was refused
[[nodiscard]] std::optional<CountsTable>
parse_counts(std::span<const uint8_t> bytes, std::string* error);

/// Fetches one registry's counts.
///
/// GET of `api` with `/counts` added to its path. The request adds no header
/// and no query of its own. It waits at most 15 seconds and reads at most
/// 1 MiB. A response that is not 200, or a document `parse_counts` refuses,
/// fails and leaves any counts already stored alone.
///
/// @param client the caller's client
/// @param api the registry's download API
/// @param[out] error why the counts were not read; may be null
/// @return the table, or nothing when the fetch or the document failed
[[nodiscard]] std::optional<CountsTable>
fetch_counts(netgame::http::Client& client, const formats::url::Url& api, std::string* error);

/// Reports whether a registry's counts should be fetched for this check.
///
/// True for a registry that hands out download keys, names an API, and whose
/// catalogue check is newer than the check its counts were last fetched for.
/// Never true for a registry whose packages are fetched from the catalogue's
/// own address.
///
/// @param registry the registry as the latest snapshot lists it
/// @param fetched_for the check the counts were last fetched for; empty when none were
/// @return true when a fetch is due
[[nodiscard]] bool
counts_due(const RegistryView& registry, std::optional<int64_t> fetched_for) noexcept;

/// Writes a counts table as the bytes the counts folder keeps for one registry.
///
/// The file is `<counts folder>/<registry id>.json`, replaced whole. A registry
/// id that is not kebab-case is refused, so the name cannot leave the folder.
///
/// @param counts_folder `<data>/content/counts`
/// @param registry_id the registry's id
/// @param table the table a fetch just read
/// @param[out] error why the file was not written; may be null
/// @return true when the file holds the table
[[nodiscard]] bool save_counts(
    const std::filesystem::path& counts_folder,
    std::string_view registry_id,
    const CountsTable& table,
    std::string* error
);

/// Reads the counts file kept for one registry.
///
/// @param counts_folder `<data>/content/counts`
/// @param registry_id the registry's id
/// @param[out] error why the file was not a counts document; may be null
/// @return the table, or nothing when the file is missing or was refused
[[nodiscard]] std::optional<CountsTable> load_counts(
    const std::filesystem::path& counts_folder, std::string_view registry_id, std::string* error
);

} // namespace oa::app::content
