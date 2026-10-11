// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The badge pictures' cache against a local fixture: a good picture, one
// whose bytes hash to another name, one too large, one the server does not
// have, one the folder already holds and is read with no request, and none
// fetched while a match runs. No request carries an install ID. A picture's
// checked file is handed over whole (take_bytes), and never for a picture
// refused.
#include "oa/app/content/pictures.hpp"

#include "oa/app/mod_summary.hpp"
#include "oa/base/sha256.hpp"
#include "oa/formats/png.hpp"
#include "oa/netgame/http/client.hpp"
#include "oa/netgame/http_fixture/server.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace content = oa::app::content;
namespace fixture = oa::netgame::http_fixture;
namespace http = oa::netgame::http;
namespace png = oa::formats::png;
namespace sha = oa::base::sha256;
namespace fs = std::filesystem;

/// The registry the pictures are asked for under.
constexpr std::string_view kRegistry = "fixture-core";

/// A badge picture's file: an 8 by 8 RGBA image, its colour given.
///
/// @param red the red of every pixel
/// @return the PNG file's bytes
std::vector<uint8_t> badge_file(uint8_t red) {
    constexpr uint32_t side = 8;
    const png::Header header{side, side, 8, png::ColorType::rgb_alpha, png::Interlace::none};
    std::vector<uint8_t> rows;
    for (uint32_t pixel = 0; pixel < side * side; ++pixel) {
        rows.push_back(red);
        rows.push_back(0x20);
        rows.push_back(0x40);
        rows.push_back(0xff);
    }
    std::vector<uint8_t> file;
    OA_CHECK(png::write(png::Image{header, {}, rows}, &file));
    return file;
}

/// The path a catalogue gives a picture: `/v1/i/<sha256>.png`.
///
/// @param digest the SHA-256 the name gives
/// @return the path
std::string picture_path(const sha::Digest& digest) {
    const std::array<char, sha::hex_size> hex = sha::to_hex(digest);
    return "/v1/i/" + std::string(hex.data(), hex.size()) + ".png";
}

/// Tells whether the fixture logged a request for a path.
///
/// @param server the fixture
/// @param path the path
/// @return true when it did
bool requested(const fixture::Server& server, const std::string& path) {
    for (const fixture::LoggedRequest& request : server.requests())
        if (request.target == path)
            return true;
    return false;
}

/// Tells whether a request named an install ID in any header.
///
/// @param request the request
/// @return true when a header's name or value speaks of one
bool names_install_id(const fixture::LoggedRequest& request) {
    for (const fixture::Header& header : request.headers) {
        std::string both = header.name + ":" + header.value;
        for (char& character : both)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (both.find("install") != std::string::npos)
            return true;
    }
    return false;
}

void test_names() {
    const sha::Digest digest = sha::digest_of(badge_file(1));
    const std::string path = picture_path(digest);
    OA_CHECK(content::picture_digest(path) == digest);
    OA_CHECK(content::picture_digest("http://127.0.0.1:9" + path) == digest);
    OA_CHECK(!content::picture_digest("/v1/i/badge.png"));
    OA_CHECK(!content::picture_digest(path.substr(0, path.size() - 4) + ".gif"));
    OA_CHECK(!content::picture_digest(path.substr(0, path.size() - 5) + ".png"));
}

void test_fetches() {
    const fs::path folder = oa::test::make_scratch_directory("app-content-pictures") / "pictures";
    fixture::Server server;
    std::string error;
    OA_CHECK(server.start(&error));

    // A good picture, served under its own SHA-256.
    const std::vector<uint8_t> good = badge_file(0x80);
    const std::string good_path = picture_path(sha::digest_of(good));
    server.serve_bytes(good_path, good, "image/png");
    // Bytes served under another picture's name.
    const std::vector<uint8_t> other = badge_file(0x81);
    const std::string wrong_path = picture_path(sha::digest_of(badge_file(0x82)));
    server.serve_bytes(wrong_path, other, "image/png");
    // A file over the largest picture, named by its own SHA-256.
    std::vector<uint8_t> large = badge_file(0x83);
    large.resize(static_cast<std::size_t>(content::max_picture_bytes) + 1, 0);
    const std::string large_path = picture_path(sha::digest_of(large));
    server.serve_bytes(large_path, large, "image/png");
    // A picture the server does not have.
    const std::string missing_path = picture_path(sha::digest_of(badge_file(0x84)));

    http::Client client;
    {
        content::PictureCache cache(folder);
        for (const std::string& path : {good_path, wrong_path, large_path, missing_path})
            cache.request(kRegistry, server.url(path));
        OA_CHECK(cache.waiting());
        OA_CHECK(!cache.take(kRegistry, server.url(good_path)));
        cache.fetch_waiting(client, false);
        OA_CHECK(!cache.waiting());
        OA_CHECK(cache.generation() == 1);

        const std::optional<oa::app::ModBadge> badge = cache.take(kRegistry, server.url(good_path));
        OA_CHECK(
            badge && badge->width == 8 && badge->height == 8 &&
            badge->pixels.size() == 8U * 8U * 4U && badge->pixels[0] == 0x80
        );
        OA_CHECK(fs::exists(folder / good_path.substr(6)));
        // Taken once, it is gone.
        OA_CHECK(!cache.take(kRegistry, server.url(good_path)));

        // The wrong hash, the large file and the missing one are refused,
        // kept nowhere, and not asked for again.
        for (const std::string& path : {wrong_path, large_path, missing_path}) {
            OA_CHECK(!cache.take(kRegistry, server.url(path)));
            OA_CHECK(!fs::exists(folder / path.substr(6)));
            OA_CHECK(requested(server, path));
        }
        server.clear_log();
        for (const std::string& path : {wrong_path, large_path, missing_path})
            cache.request(kRegistry, server.url(path));
        OA_CHECK(!cache.waiting());
        cache.fetch_waiting(client, false);
        OA_CHECK(server.requests().empty());
    }

    // A new cache over the same folder reads the good picture with no request.
    server.clear_log();
    {
        content::PictureCache cache(folder);
        cache.request(kRegistry, server.url(good_path));
        cache.fetch_waiting(client, false);
        const std::optional<oa::app::ModBadge> badge = cache.take(kRegistry, server.url(good_path));
        OA_CHECK(badge && badge->width == 8 && badge->pixels[0] == 0x80);
        OA_CHECK(server.requests().empty());
    }

    // A file in the folder whose bytes hash to another name is not used: the
    // picture is fetched, checked and written over it.
    server.clear_log();
    {
        const fs::path kept = folder / good_path.substr(6);
        {
            std::ofstream damaged(kept, std::ios::binary | std::ios::trunc);
            damaged.write(
                reinterpret_cast<const char*>(other.data()),
                static_cast<std::streamsize>(other.size())
            );
        }
        content::PictureCache cache(folder);
        cache.request(kRegistry, server.url(good_path));
        cache.fetch_waiting(client, false);
        const std::optional<oa::app::ModBadge> badge = cache.take(kRegistry, server.url(good_path));
        OA_CHECK(badge && badge->pixels[0] == 0x80);
        OA_CHECK(requested(server, good_path));
        std::vector<uint8_t> now(good.size() + 1, 0);
        std::ifstream reread(kept, std::ios::binary);
        reread.read(reinterpret_cast<char*>(now.data()), static_cast<std::streamsize>(now.size()));
        now.resize(static_cast<std::size_t>(reread.gcount()));
        OA_CHECK(now == good);
    }

    // While a match runs nothing is requested; once it ends the picture comes.
    const std::vector<uint8_t> later = badge_file(0x90);
    const std::string later_path = picture_path(sha::digest_of(later));
    server.serve_bytes(later_path, later, "image/png");
    server.clear_log();
    {
        content::PictureCache cache(folder);
        cache.request(kRegistry, server.url(later_path));
        cache.fetch_waiting(client, true);
        OA_CHECK(server.requests().empty());
        OA_CHECK(cache.waiting());
        OA_CHECK(!cache.take(kRegistry, server.url(later_path)));
        cache.fetch_waiting(client, false);
        OA_CHECK(requested(server, later_path));
        const std::optional<oa::app::ModBadge> badge =
            cache.take(kRegistry, server.url(later_path));
        OA_CHECK(badge && badge->pixels[0] == 0x90);
    }

    // drop_all forgets what waits; nothing is fetched for it.
    server.clear_log();
    {
        content::PictureCache cache(folder);
        const std::string fresh_path = picture_path(sha::digest_of(badge_file(0x91)));
        cache.request(kRegistry, server.url(fresh_path));
        cache.drop_all();
        OA_CHECK(!cache.waiting());
        cache.fetch_waiting(client, false);
        OA_CHECK(server.requests().empty());
    }

    // No request the cache made carried an install ID.
    server.clear_log();
    {
        content::PictureCache cache({});
        cache.request(kRegistry, server.url(good_path));
        cache.fetch_waiting(client, false);
        OA_CHECK(cache.take(kRegistry, server.url(good_path)).has_value());
        const std::vector<fixture::LoggedRequest> logged = server.requests();
        OA_CHECK(logged.size() == 1);
        for (const fixture::LoggedRequest& request : logged)
            OA_CHECK(!names_install_id(request));
    }
    server.stop();
}

void test_bytes() {
    const fs::path folder =
        oa::test::make_scratch_directory("app-content-pictures-bytes") / "pictures";
    fixture::Server server;
    std::string error;
    OA_CHECK(server.start(&error));
    const std::vector<uint8_t> good = badge_file(0xb0);
    const std::string good_path = picture_path(sha::digest_of(good));
    server.serve_bytes(good_path, good, "image/png");
    const std::vector<uint8_t> other = badge_file(0xb1);
    const std::string wrong_path = picture_path(sha::digest_of(badge_file(0xb2)));
    server.serve_bytes(wrong_path, other, "image/png");
    std::vector<uint8_t> large = badge_file(0xb3);
    large.resize(static_cast<std::size_t>(content::max_picture_bytes) + 1, 0);
    const std::string large_path = picture_path(sha::digest_of(large));
    server.serve_bytes(large_path, large, "image/png");

    http::Client client;
    {
        content::PictureCache cache(folder);
        for (const std::string& path : {good_path, wrong_path, large_path})
            cache.request(kRegistry, server.url(path));
        OA_CHECK(!cache.take_bytes(kRegistry, server.url(good_path)));
        cache.fetch_waiting(client, false);

        // The good picture's file, byte for byte, then nothing once taken.
        const std::optional<std::vector<uint8_t>> bytes =
            cache.take_bytes(kRegistry, server.url(good_path));
        OA_CHECK(bytes && *bytes == good);
        OA_CHECK(!cache.take_bytes(kRegistry, server.url(good_path)));
        OA_CHECK(!cache.take(kRegistry, server.url(good_path)));
        OA_CHECK(!cache.refused(kRegistry, server.url(good_path)));

        // The wrong hash and the file over the largest give nothing, and are refused.
        for (const std::string& path : {wrong_path, large_path}) {
            OA_CHECK(!cache.take_bytes(kRegistry, server.url(path)));
            OA_CHECK(cache.refused(kRegistry, server.url(path)));
        }
    }

    // Read again from the folder with no request: the same bytes.
    server.clear_log();
    {
        content::PictureCache cache(folder);
        cache.request(kRegistry, server.url(good_path));
        cache.fetch_waiting(client, false);
        const std::optional<std::vector<uint8_t>> bytes =
            cache.take_bytes(kRegistry, server.url(good_path));
        OA_CHECK(bytes && *bytes == good);
        OA_CHECK(server.requests().empty());
    }
    server.stop();
}

void test_cancel() {
    fixture::Server server;
    std::string error;
    OA_CHECK(server.start(&error));
    const std::vector<uint8_t> picture = badge_file(0xa0);
    const std::string path = picture_path(sha::digest_of(picture));
    server.serve_bytes(path, picture, "image/png");
    std::atomic<bool> cancel{true};
    http::Client client;
    content::PictureCache cache({}, &cancel);
    cache.request(kRegistry, server.url(path));
    cache.fetch_waiting(client, false);
    OA_CHECK(cache.waiting());
    OA_CHECK(server.requests().empty());
    cancel.store(false);
    cache.fetch_waiting(client, false);
    OA_CHECK(cache.take(kRegistry, server.url(path)).has_value());
    server.stop();
}

} // namespace

int main() {
    test_names();
    test_fetches();
    test_bytes();
    test_cancel();
    return oa::test::check_exit_status();
}
