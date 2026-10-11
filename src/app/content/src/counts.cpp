// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Download counts: the document, the one request that fetches it, and the
// file the Library keeps so the last counts still show with no network.
#include "oa/app/content/counts.hpp"

#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/formats/json.hpp"
#include "oa/platform/files.hpp"

#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace oa::app::content {
namespace {

namespace json = oa::formats::json;
namespace url = oa::formats::url;
namespace http = oa::netgame::http;
namespace catalogue = oa::data::catalogue;

/// The most bytes of a counts document this reader accepts.
constexpr std::size_t max_counts_bytes = std::size_t{1} << 20;

/// How long one counts fetch may take, in milliseconds.
constexpr uint32_t counts_timeout_ms = 15000;

/// The shape version a counts document names.
constexpr int64_t counts_shape = 1;

/// Writes a reason into `error` when it is not null.
///
/// @param error the caller's reason; may be null
/// @param text the reason
void set_error(std::string* error, std::string text) {
    if (error != nullptr)
        *error = std::move(text);
}

/// Returns an object's member, or null.
///
/// @param value a JSON value
/// @param name the member
/// @return the member, or null when this is not an object or it has none
const json::Json* member(const json::Json& value, std::string_view name) noexcept {
    if (value.type() != json::JsonType::object)
        return nullptr;
    return value.find(name);
}

/// Reports whether a kind is one a catalogue lists.
///
/// @param kind the kind as the document writes it
/// @return true for `oalang`, `oamod` or `oamap`
bool known_kind(std::string_view kind) noexcept {
    return kind == "oalang" || kind == "oamod" || kind == "oamap";
}

/// Appends `/counts` to an API address's path, keeping a query the API had.
///
/// @param api the download API
/// @return the counts address
url::Url counts_url(const url::Url& api) {
    url::Url out = api;
    std::string path = api.target;
    std::string query;
    const std::size_t mark = path.find('?');
    if (mark != std::string::npos) {
        query = path.substr(mark);
        path.erase(mark);
    }
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    path.append("/counts");
    path += query;
    out.target = std::move(path);
    return out;
}

/// Writes a table as one counts document.
///
/// @param table the table
/// @return the document, packages ordered by kind and then key
std::string counts_json(const CountsTable& table) {
    std::string text = "{\"counts\":1,\"generated\":";
    json::append_json_string(text, table.generated);
    text += ",\"packages\":[";
    bool first = true;
    for (const auto& entry : table.by_kind_and_key) {
        if (!first)
            text.push_back(',');
        first = false;
        text += "{\"kind\":";
        json::append_json_string(text, entry.first.first);
        text += ",\"id\":";
        json::append_json_string(text, entry.first.second);
        text += ",\"downloads\":";
        text += std::to_string(entry.second);
        text.push_back('}');
    }
    text += "]}";
    return text;
}

/// The file one registry's counts are kept in.
///
/// @param counts_folder `<data>/content/counts`
/// @param registry_id a kebab-case registry id
/// @return `<counts folder>/<registry id>.json`
std::filesystem::path
counts_file(const std::filesystem::path& counts_folder, std::string_view registry_id) {
    return counts_folder / (std::string(registry_id) + ".json");
}

} // namespace

std::optional<CountsTable> parse_counts(std::span<const uint8_t> bytes, std::string* error) {
    if (bytes.size() > max_counts_bytes) {
        set_error(error, "the counts document is larger than 1 MiB");
        return std::nullopt;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    json::JsonError json_error;
    const std::optional<json::Json> root = json::parse_json(text, json_error);
    if (!root || root->type() != json::JsonType::object) {
        set_error(error, "the counts document is not a JSON object");
        return std::nullopt;
    }
    const json::Json* shape = member(*root, "counts");
    const std::optional<int64_t> shape_number = shape ? shape->integer() : std::nullopt;
    if (!shape_number || *shape_number != counts_shape) {
        set_error(error, "counts is not 1");
        return std::nullopt;
    }
    const json::Json* generated_value = member(*root, "generated");
    const std::string* generated = generated_value ? generated_value->string() : nullptr;
    if (!generated || !catalogue::parse_utc_time(*generated)) {
        set_error(error, "generated is not a UTC time");
        return std::nullopt;
    }
    const json::Json* packages_value = member(*root, "packages");
    if (packages_value == nullptr || packages_value->type() != json::JsonType::array) {
        set_error(error, "packages is not a list");
        return std::nullopt;
    }
    CountsTable table;
    table.generated = *generated;
    for (const json::Json& package : packages_value->elements()) {
        if (package.type() != json::JsonType::object) {
            set_error(error, "a package is not an object");
            return std::nullopt;
        }
        const json::Json* kind_value = member(package, "kind");
        const std::string* kind = kind_value ? kind_value->string() : nullptr;
        if (!kind) {
            set_error(error, "kind is not text");
            return std::nullopt;
        }
        if (!known_kind(*kind))
            continue;
        const json::Json* id_value = member(package, "id");
        const std::string* id = id_value ? id_value->string() : nullptr;
        if (!id || id->empty()) {
            set_error(error, "id is empty");
            return std::nullopt;
        }
        const json::Json* downloads_value = member(package, "downloads");
        const std::optional<int64_t> downloads =
            downloads_value ? downloads_value->integer() : std::nullopt;
        if (!downloads || *downloads < 0) {
            set_error(error, "downloads is not a whole number");
            return std::nullopt;
        }
        table.by_kind_and_key[{*kind, *id}] = static_cast<uint64_t>(*downloads);
    }
    return table;
}

std::optional<CountsTable>
fetch_counts(http::Client& client, const url::Url& api, std::string* error) {
    http::Request request;
    request.method = http::Method::get;
    request.url = url::url_text(counts_url(api));
    request.timeouts.total_ms = counts_timeout_ms;
    request.limits.max_body_bytes = max_counts_bytes;
    std::vector<uint8_t> body;
    const http::Response response = client.fetch(request, body);
    if (response.failure != http::Failure::none) {
        set_error(
            error, response.detail.empty() ? http::failure_text(response.failure) : response.detail
        );
        return std::nullopt;
    }
    if (response.status != 200) {
        set_error(error, "the counts answer was " + std::to_string(response.status));
        return std::nullopt;
    }
    return parse_counts(body, error);
}

bool counts_due(const RegistryView& registry, std::optional<int64_t> fetched_for) noexcept {
    const data::registry::Descriptor& descriptor = registry.registry.descriptor;
    if (descriptor.mode != data::registry::DownloadMode::tokens || !descriptor.api ||
        !registry.checked)
        return false;
    if (!fetched_for)
        return true;
    return *registry.checked > *fetched_for;
}

bool save_counts(
    const std::filesystem::path& counts_folder,
    std::string_view registry_id,
    const CountsTable& table,
    std::string* error
) {
    if (counts_folder.empty()) {
        set_error(error, "there is no counts folder");
        return false;
    }
    if (!data::registry::valid_registry_id(registry_id)) {
        set_error(error, "the registry id is not kebab-case");
        return false;
    }
    const std::string text = counts_json(table);
    const std::span<const uint8_t> bytes(
        reinterpret_cast<const uint8_t*>(text.data()), text.size()
    );
    return oa::platform::replace_file(counts_file(counts_folder, registry_id), bytes, error);
}

std::optional<CountsTable> load_counts(
    const std::filesystem::path& counts_folder, std::string_view registry_id, std::string* error
) {
    if (counts_folder.empty() || !data::registry::valid_registry_id(registry_id)) {
        set_error(error, "there is no counts file");
        return std::nullopt;
    }
    const std::filesystem::path file = counts_file(counts_folder, registry_id);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        set_error(error, "there is no counts file");
        return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) {
        set_error(error, "the counts file could not be read");
        return std::nullopt;
    }
    if (static_cast<uint64_t>(end) > max_counts_bytes) {
        set_error(error, "the counts document is larger than 1 MiB");
        return std::nullopt;
    }
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty())
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in) {
        set_error(error, "the counts file could not be read");
        return std::nullopt;
    }
    return parse_counts(bytes, error);
}

} // namespace oa::app::content
