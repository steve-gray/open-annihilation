// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The download API's bodies and readers, with no network.
#include "oa/app/content/download_api.hpp"

#include "oa/base/sha256.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/test/check.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace {

namespace content = oa::app::content;
namespace sha = oa::base::sha256;

/// The bytes of a text, for a reader.
std::span<const uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

/// A digest whose hex is 64 `a`s, so the body is obvious.
sha::Digest digest_a() {
    const auto parsed = sha::parse_hex(std::string(64, 'a'));
    OA_CHECK(parsed.has_value());
    return parsed ? *parsed : sha::Digest{};
}

/// One key request, with an install ID when `install` is not empty.
content::KeyRequest request_for(std::string_view install) {
    content::KeyRequest request;
    if (!install.empty())
        request.install = std::string(install);
    request.package = "ridge";
    request.kind = oa::data::catalogue::Kind::oamod;
    request.release = 28;
    request.sha256 = digest_a();
    request.engine = "0.8.0";
    request.platform = std::string(content::platform_name());
    request.arch = std::string(content::arch_name());
    request.language = "de";
    request.reason = content::DownloadReason::update;
    return request;
}

void test_key_body() {
    const std::string with_id =
        content::key_request_body(request_for("7f3a90d2-4c1b-4e8a-9d07-c91e00000001"));
    const std::string wanted =
        std::string(
            "{\"install\":\"7f3a90d2-4c1b-4e8a-9d07-c91e00000001\",\"package\":\"ridge\","
            "\"kind\":\"oamod\",\"release\":28,\"sha256\":\""
        ) +
        std::string(64, 'a') + "\",\"engine\":\"0.8.0\",\"platform\":\"" +
        std::string(content::platform_name()) + "\",\"arch\":\"" +
        std::string(content::arch_name()) + "\",\"language\":\"de\",\"reason\":\"update\"}";
    if (with_id != wanted)
        std::fprintf(stderr, "body:\n%s\nwanted:\n%s\n", with_id.c_str(), wanted.c_str());
    OA_CHECK(with_id == wanted);

    const std::string without = content::key_request_body(request_for(""));
    const std::string wanted_without =
        std::string("{\"package\":\"ridge\",\"kind\":\"oamod\",\"release\":28,\"sha256\":\"") +
        std::string(64, 'a') + "\",\"engine\":\"0.8.0\",\"platform\":\"" +
        std::string(content::platform_name()) + "\",\"arch\":\"" +
        std::string(content::arch_name()) + "\",\"language\":\"de\",\"reason\":\"update\"}";
    OA_CHECK(without == wanted_without);
    OA_CHECK(without.find("install") == std::string::npos);
}

void test_issued_key() {
    std::string why = "untouched";
    const auto key = content::read_issued_key(
        bytes_of(
            "{\"download\":\"ridge-dl-1\",\"expires\":\"2026-02-01T00:00:00Z\","
            "\"url\":\"http://downloads.example.net/p/ridge.oamod\"}"
        ),
        &why
    );
    OA_CHECK(key.has_value());
    if (!key)
        return;
    OA_CHECK(key->download == "ridge-dl-1");
    const auto when = oa::data::catalogue::parse_utc_time("2026-02-01T00:00:00Z");
    OA_CHECK(when.has_value());
    OA_CHECK(key->expires == *when);
    OA_CHECK(key->url.host == "downloads.example.net");

    OA_CHECK(!content::read_issued_key(
        bytes_of(
            "{\"expires\":\"2026-02-01T00:00:00Z\","
            "\"url\":\"http://downloads.example.net/p\"}"
        ),
        &why
    ));
    OA_CHECK(!content::read_issued_key(
        bytes_of(
            "{\"download\":\"has space\",\"expires\":\"2026-02-01T00:00:00Z\","
            "\"url\":\"http://downloads.example.net/p\"}"
        ),
        &why
    ));
    OA_CHECK(!content::read_issued_key(
        bytes_of(
            "{\"download\":\"ridge-dl-1\",\"expires\":\"2026-02-01T00:00:00Z\","
            "\"url\":\"https://downloads.example.net/p\"}"
        ),
        &why
    ));
    OA_CHECK(!content::read_issued_key(
        bytes_of(
            "{\"download\":\"ridge-dl-1\",\"expires\":\"tomorrow\","
            "\"url\":\"http://downloads.example.net/p\"}"
        ),
        &why
    ));
    OA_CHECK(!content::read_issued_key(bytes_of("{"), &why));
}

void test_challenge() {
    std::string why;
    const auto challenge = content::read_challenge(
        bytes_of(
            "{\"error\":\"challenge_required\",\"message\":\"confirm you are a person\","
            "\"challenge\":{\"code\":\"HXQ7-4KP2\","
            "\"verify\":\"https://downloads.example.net/verify?c=HXQ7-4KP2\","
            "\"expires\":\"2026-02-01T00:00:00Z\",\"poll\":3}}"
        ),
        &why
    );
    OA_CHECK(challenge.has_value());
    if (!challenge)
        return;
    OA_CHECK(challenge->code == "HXQ7-4KP2");
    OA_CHECK(challenge->address == "downloads.example.net/verify");
    OA_CHECK(challenge->poll_seconds == 3);
    OA_CHECK(challenge->verify.scheme == oa::formats::url::Scheme::https);

    const auto low = content::read_challenge(
        bytes_of(
            "{\"challenge\":{\"code\":\"HXQ7-4KP2\","
            "\"verify\":\"https://downloads.example.net/verify?c=HXQ7-4KP2\","
            "\"expires\":\"2026-02-01T00:00:00Z\",\"poll\":1}}"
        ),
        &why
    );
    OA_CHECK(low.has_value());
    if (low)
        OA_CHECK(low->poll_seconds == 2);
    const auto high = content::read_challenge(
        bytes_of(
            "{\"challenge\":{\"code\":\"HXQ7-4KP2\","
            "\"verify\":\"https://downloads.example.net/verify?c=HXQ7-4KP2\","
            "\"expires\":\"2026-02-01T00:00:00Z\",\"poll\":99}}"
        ),
        &why
    );
    OA_CHECK(high.has_value());
    if (high)
        OA_CHECK(high->poll_seconds == 30);
    const auto absent = content::read_challenge(
        bytes_of(
            "{\"challenge\":{\"code\":\"HXQ7-4KP2\","
            "\"verify\":\"https://downloads.example.net/verify?c=HXQ7-4KP2\","
            "\"expires\":\"2026-02-01T00:00:00Z\"}}"
        ),
        &why
    );
    OA_CHECK(absent.has_value());
    if (absent)
        OA_CHECK(absent->poll_seconds == 3);

    OA_CHECK(!content::read_challenge(bytes_of("{\"error\":\"challenge_required\"}"), &why));
    OA_CHECK(!content::read_challenge(
        bytes_of(
            "{\"challenge\":{\"code\":\"bad code\","
            "\"verify\":\"https://downloads.example.net/verify\","
            "\"expires\":\"2026-02-01T00:00:00Z\"}}"
        ),
        &why
    ));
    OA_CHECK(!content::read_challenge(
        bytes_of(
            "{\"challenge\":{\"code\":\"HXQ7-4KP2\","
            "\"verify\":\"https://downloads.example.net/verify\","
            "\"expires\":\"not-a-time\"}}"
        ),
        &why
    ));
}

void test_challenge_state() {
    std::string why;
    const auto pending = content::read_challenge_state(
        bytes_of(
            "{\"code\":\"HXQ7-4KP2\",\"state\":\"pending\",\"expires\":\"2026-02-01T00:00:00Z\"}"
        ),
        &why
    );
    OA_CHECK(pending.has_value());
    if (pending)
        OA_CHECK(*pending == content::ChallengeState::pending);
    const auto solved = content::read_challenge_state(
        bytes_of(
            "{\"code\":\"HXQ7-4KP2\",\"state\":\"solved\",\"expires\":\"2026-02-01T00:00:00Z\"}"
        ),
        &why
    );
    OA_CHECK(solved.has_value());
    if (solved)
        OA_CHECK(*solved == content::ChallengeState::solved);
    const auto expired = content::read_challenge_state(
        bytes_of(
            "{\"code\":\"HXQ7-4KP2\",\"state\":\"expired\",\"expires\":\"2026-02-01T00:00:00Z\"}"
        ),
        &why
    );
    OA_CHECK(expired.has_value());
    if (expired)
        OA_CHECK(*expired == content::ChallengeState::expired);
    OA_CHECK(!content::read_challenge_state(
        bytes_of("{\"code\":\"HXQ7-4KP2\",\"expires\":\"2026-02-01T00:00:00Z\"}"), &why
    ));
    OA_CHECK(!content::read_challenge_state(
        bytes_of(
            "{\"code\":\"HXQ7-4KP2\",\"state\":\"later\",\"expires\":\"2026-02-01T00:00:00Z\"}"
        ),
        &why
    ));
    OA_CHECK(!content::read_challenge_state(bytes_of("{"), &why));
}

void test_errors() {
    const std::string retry = "12";
    const content::ApiError known = content::read_api_error(
        400,
        bytes_of(
            "{\"error\":\"bad_request\",\"message\":\"the package is missing\","
            "\"field\":\"package\"}"
        ),
        nullptr
    );
    OA_CHECK(known.status == 400);
    OA_CHECK(known.error == "bad_request");
    OA_CHECK(known.message == "the package is missing");
    OA_CHECK(known.field == "package");
    OA_CHECK(known.retry_after_seconds == 0);

    const content::ApiError limited = content::read_api_error(
        429, bytes_of("{\"error\":\"rate_limited\",\"message\":\"wait\"}"), &retry
    );
    OA_CHECK(limited.retry_after_seconds == 12);
    const std::string huge = "9999";
    OA_CHECK(content::read_api_error(429, bytes_of("{"), &huge).retry_after_seconds == 300);
    const std::string words = "soon";
    OA_CHECK(content::read_api_error(429, bytes_of("not json"), &words).retry_after_seconds == 0);
    const content::ApiError bare = content::read_api_error(503, bytes_of(""), nullptr);
    OA_CHECK(bare.status == 503);
    OA_CHECK(bare.error.empty());
    OA_CHECK(bare.message.empty());
}

void test_results_and_names() {
    OA_CHECK(
        content::result_body(content::KeyResult::installed, 43011223, 38) ==
        "{\"result\":\"installed\",\"bytes\":43011223,\"seconds\":38}"
    );
    OA_CHECK(
        content::result_body(content::KeyResult::refused, 0, 1) ==
        "{\"result\":\"refused\",\"bytes\":0,\"seconds\":1}"
    );
    OA_CHECK(
        content::result_body(content::KeyResult::failed, 12, 0) ==
        "{\"result\":\"failed\",\"bytes\":12,\"seconds\":0}"
    );
    OA_CHECK(
        content::result_body(content::KeyResult::cancelled, 3, 2) ==
        "{\"result\":\"cancelled\",\"bytes\":3,\"seconds\":2}"
    );
    OA_CHECK(content::reason_text(content::DownloadReason::install) == "install");
    OA_CHECK(content::reason_text(content::DownloadReason::update) == "update");
    OA_CHECK(content::reason_text(content::DownloadReason::repair) == "repair");
    OA_CHECK(content::reason_text(content::DownloadReason::offered_in_lobby) == "offered-in-lobby");
    OA_CHECK(content::reason_text(content::DownloadReason::mirror) == "mirror");

#if defined(_WIN32)
    OA_CHECK(content::platform_name() == "windows");
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE
    OA_CHECK(content::platform_name() == "ios");
#else
    OA_CHECK(content::platform_name() == "macos");
#endif
#elif defined(__ANDROID__)
    OA_CHECK(content::platform_name() == "android");
#elif defined(__linux__)
    OA_CHECK(content::platform_name() == "linux");
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    OA_CHECK(content::arch_name() == "arm64");
#elif defined(__x86_64__) || defined(_M_X64)
    OA_CHECK(content::arch_name() == "x64");
#elif defined(__i386__) || defined(_M_IX86)
    OA_CHECK(content::arch_name() == "x86");
#elif defined(__arm__) || defined(_M_ARM)
    OA_CHECK(content::arch_name() == "arm");
#endif
    OA_CHECK(!content::platform_name().empty());
    OA_CHECK(!content::arch_name().empty());
    OA_CHECK(content::engine_version() == "0.8.0");
}

} // namespace

int main() {
    test_key_body();
    test_issued_key();
    test_challenge();
    test_challenge_state();
    test_errors();
    test_results_and_names();
    return oa::test::check_exit_status();
}
