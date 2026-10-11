// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Download counts: the specification's example, each refusal, a fetch that
// carries nothing identifying the player, a saved table kept when a fetch
// fails, and when a fetch is due.
#include "oa/app/content/counts.hpp"

#include "oa/data/registry/descriptor.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/http/client.hpp"
#include "oa/netgame/http_fixture/server.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef OA_COUNTS_EXAMPLE
#error OA_COUNTS_EXAMPLE is the counts example's path
#endif

namespace {

namespace content = oa::app::content;
namespace fixture = oa::netgame::http_fixture;
namespace http = oa::netgame::http;
namespace registry = oa::data::registry;
namespace url = oa::formats::url;
namespace fs = std::filesystem;

/// The bytes of a text.
///
/// @param text the text
/// @return its bytes
std::vector<uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

/// A counts document with one package, for a refusal built around it.
///
/// @param downloads the downloads member, as JSON
/// @return the document
std::string one_package(std::string_view downloads) {
    return std::string(
               "{\"counts\":1,\"generated\":\"2026-11-02T04:10:00Z\",\"packages\":"
               "[{\"kind\":\"oamod\",\"id\":\"ridge\",\"downloads\":"
           ) +
           std::string(downloads) + "}]}";
}

/// Reads the specification's example.
///
/// @return its bytes
std::vector<uint8_t> example_bytes() {
    std::ifstream in(OA_COUNTS_EXAMPLE, std::ios::binary);
    OA_CHECK(static_cast<bool>(in));
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Tells whether a request's path, query or headers name an install.
///
/// @param request the request
/// @return true when they do
bool names_player(const fixture::LoggedRequest& request) {
    std::string text = request.target;
    for (const fixture::Header& header : request.headers)
        text += " " + header.name + " " + header.value;
    for (char& character : text)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text.find("install") != std::string::npos;
}

void test_example() {
    std::string error = "untouched";
    const std::optional<content::CountsTable> table =
        content::parse_counts(example_bytes(), &error);
    OA_CHECK(table.has_value());
    if (!table)
        return;
    OA_CHECK(error == "untouched");
    OA_CHECK(table->generated == "2026-11-02T04:10:00Z");
    OA_CHECK(table->by_kind_and_key.size() == 3);
    OA_CHECK(table->by_kind_and_key.at({"oalang", "xx-Test"}) == 4);
    OA_CHECK(table->by_kind_and_key.at({"oamap", "example-maps"}) == 2);
    OA_CHECK(table->by_kind_and_key.at({"oamod", "example-mod"}) == 7);
}

void refuses(std::string_view document, std::string_view needle) {
    std::string error;
    const std::optional<content::CountsTable> table =
        content::parse_counts(bytes_of(document), &error);
    OA_CHECK(!table);
    OA_CHECK(error.find(needle) != std::string::npos);
}

void test_refusals() {
    const std::vector<uint8_t> huge((static_cast<std::size_t>(1) << 20) + 1, ' ');
    std::string error;
    OA_CHECK(!content::parse_counts(huge, &error));
    OA_CHECK(error.find("1 MiB") != std::string::npos);

    refuses(
        "{\"counts\":2,\"generated\":\"2026-11-02T04:10:00Z\",\"packages\":[]}", "counts is not 1"
    );
    refuses(
        "{\"counts\":1.5,\"generated\":\"2026-11-02T04:10:00Z\",\"packages\":[]}", "counts is not 1"
    );
    refuses(
        "{\"counts\":\"1\",\"generated\":\"2026-11-02T04:10:00Z\",\"packages\":[]}",
        "counts is not 1"
    );
    refuses("{\"generated\":\"2026-11-02T04:10:00Z\",\"packages\":[]}", "counts is not 1");
    refuses("{\"counts\":1,\"generated\":\"yesterday\",\"packages\":[]}", "generated");
    refuses("{\"counts\":1,\"generated\":\"2026-11-02\",\"packages\":[]}", "generated");
    refuses("{\"counts\":1,\"packages\":[]}", "generated");
    refuses("{\"counts\":1,\"generated\":\"2026-11-02T04:10:00Z\"}", "packages");
    refuses("not json", "JSON");
    refuses(one_package("-1"), "downloads");
    refuses(one_package("1.5"), "downloads");
    refuses(one_package("\"12\""), "downloads");
    refuses(
        "{\"counts\":1,\"generated\":\"2026-11-02T04:10:00Z\",\"packages\":[{\"kind\":\"oamod\","
        "\"id\":\"ridge\"}]}",
        "downloads"
    );

    const std::string skipped =
        "{\"counts\":1,\"generated\":\"2026-11-02T04:10:00Z\",\"note\":true,\"packages\":["
        "{\"kind\":\"widget\",\"id\":\"other\",\"downloads\":9},"
        "{\"kind\":\"oamod\",\"id\":\"ridge\",\"downloads\":3,\"extra\":1}]}";
    const std::optional<content::CountsTable> kept =
        content::parse_counts(bytes_of(skipped), &error);
    OA_CHECK(kept && kept->by_kind_and_key.size() == 1);
    if (kept)
        OA_CHECK(kept->by_kind_and_key.at({"oamod", "ridge"}) == 3);
}

void test_fetch_names_nobody() {
    fixture::Server server;
    std::string error;
    OA_CHECK(server.start(&error));
    const std::vector<uint8_t> example = example_bytes();
    server.serve_bytes("/api/v1/counts", example, "application/json");
    const std::optional<url::Url> api = url::parse_http_url(server.url("/api/v1"));
    OA_CHECK(api.has_value());
    if (!api)
        return;
    http::Client client;
    const std::optional<content::CountsTable> table = content::fetch_counts(client, *api, &error);
    OA_CHECK(table && table->by_kind_and_key.at({"oamod", "example-mod"}) == 7);
    const std::vector<fixture::LoggedRequest> logged = server.requests();
    OA_CHECK(logged.size() == 1);
    if (logged.empty())
        return;
    OA_CHECK(logged[0].method == "GET");
    OA_CHECK(logged[0].target == "/api/v1/counts");
    OA_CHECK(!names_player(logged[0]));
}

void test_failed_fetch_keeps_saved() {
    const fs::path folder = oa::test::make_scratch_directory("app-content-counts");
    content::CountsTable saved;
    saved.generated = "2026-11-02T04:10:00Z";
    saved.by_kind_and_key[{"oamod", "ridge"}] = 12400;
    std::string error;
    OA_CHECK(content::save_counts(folder, "fixture-core", saved, &error));

    fixture::Server server;
    OA_CHECK(server.start(&error));
    server.handle("GET", "/api/v1/counts", [](const fixture::LoggedRequest&) {
        fixture::Reply reply;
        reply.status = 500;
        reply.body = {'n', 'o'};
        return reply;
    });
    const std::optional<url::Url> api = url::parse_http_url(server.url("/api/v1"));
    OA_CHECK(api.has_value());
    http::Client client;
    if (api)
        OA_CHECK(!content::fetch_counts(client, *api, &error));

    const std::optional<content::CountsTable> loaded =
        content::load_counts(folder, "fixture-core", &error);
    OA_CHECK(loaded.has_value());
    if (loaded) {
        OA_CHECK(loaded->generated == saved.generated);
        OA_CHECK(loaded->by_kind_and_key.at({"oamod", "ridge"}) == 12400);
    }
    fs::remove_all(folder);
}

content::RegistryView tokens_registry(int64_t checked) {
    content::RegistryView registry;
    registry.registry.descriptor.mode = registry::DownloadMode::tokens;
    registry.registry.descriptor.api = url::parse_http_url("http://127.0.0.1/api/v1");
    registry.checked = checked;
    return registry;
}

void test_counts_due() {
    content::RegistryView direct;
    direct.registry.descriptor.mode = registry::DownloadMode::direct;
    direct.registry.descriptor.api = url::parse_http_url("http://127.0.0.1/api/v1");
    direct.checked = 10;
    OA_CHECK(!content::counts_due(direct, std::nullopt));

    const content::RegistryView tokens = tokens_registry(10);
    OA_CHECK(content::counts_due(tokens, std::nullopt));
    OA_CHECK(!content::counts_due(tokens, 10));
    OA_CHECK(content::counts_due(tokens, 9));

    content::RegistryView no_api = tokens;
    no_api.registry.descriptor.api.reset();
    OA_CHECK(!content::counts_due(no_api, std::nullopt));

    content::RegistryView unchecked = tokens;
    unchecked.checked.reset();
    OA_CHECK(!content::counts_due(unchecked, std::nullopt));
}

} // namespace

int main() {
    test_example();
    test_refusals();
    test_fetch_names_nobody();
    test_failed_fetch_keeps_saved();
    test_counts_due();
    return oa::test::check_exit_status();
}
