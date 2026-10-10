// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The web-address reader: addresses it accepts, every refusal, reference
// resolution against RFC 3986 sections 5.4.1 and 5.4.2, and host comparison.

#include "oa/formats/url.hpp"
#include "oa/test/check.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using namespace oa::formats::url;

/// Reads an address that must be accepted, and checks its parts and canonical text.
///
/// @param text the address as written
/// @param scheme the scheme it names
/// @param host the canonical host
/// @param port the port, including a scheme default the text left out
/// @param target the request target
/// @param canonical the one form url_text writes
void expect_url(
    std::string_view text,
    Scheme scheme,
    std::string_view host,
    uint16_t port,
    std::string_view target,
    std::string_view canonical
) {
    UrlError error = UrlError::bad_host;
    const std::optional<Url> url = parse_web_url(text, &error);
    if (!url || error != UrlError::none || url->scheme != scheme || url->host != host ||
        url->port != port || url->target != target || url_text(*url) != canonical) {
        std::fprintf(
            stderr,
            "address %.*s: error %s host '%s' port %u target '%s' text '%s'\n",
            static_cast<int>(text.size()),
            text.data(),
            url_error_text(error),
            url ? url->host.c_str() : "",
            url ? url->port : 0,
            url ? url->target.c_str() : "",
            url ? url_text(*url).c_str() : ""
        );
    }
    OA_CHECK(url.has_value());
    OA_CHECK(error == UrlError::none);
    if (!url)
        return;
    OA_CHECK(url->scheme == scheme);
    OA_CHECK(url->host == host);
    OA_CHECK(url->port == port);
    OA_CHECK(url->target == target);
    OA_CHECK(url_text(*url) == canonical);
    OA_CHECK(has_query(*url) == (std::string_view{url->target}.find('?') != std::string::npos));
}

/// Reads an address that must be refused.
///
/// @param text the address
/// @param expected the refusal
/// @param http_only true to use the fetch reader
void expect_refused(std::string_view text, UrlError expected, bool http_only = false) {
    UrlError error = UrlError::none;
    const std::optional<Url> url =
        http_only ? parse_http_url(text, &error) : parse_web_url(text, &error);
    if (url || error != expected) {
        std::fprintf(
            stderr,
            "refusal of %.*s: got %s, expected %s\n",
            static_cast<int>(text.size()),
            text.data(),
            url ? "acceptance" : url_error_text(error),
            url_error_text(expected)
        );
    }
    OA_CHECK(!url.has_value());
    OA_CHECK(error == expected);
    OA_CHECK(url_error_text(expected) != nullptr);
    OA_CHECK(url_error_text(expected)[0] != '\0');
}

/// Resolves a reference and checks the canonical result. A fragment on the
/// result is absent, because this reader drops fragments.
///
/// @param reference the reference
/// @param canonical the resolved address, with no fragment
void expect_resolved(std::string_view reference, std::string_view canonical) {
    UrlError base_error = UrlError::none;
    const std::optional<Url> base = parse_web_url("http://a/b/c/d;p?q", &base_error);
    OA_CHECK(base.has_value());
    if (!base)
        return;
    UrlError error = UrlError::none;
    const std::optional<Url> resolved = resolve_reference(*base, reference, &error);
    const std::string written = resolved ? url_text(*resolved) : std::string{};
    if (!resolved || written != canonical) {
        std::fprintf(
            stderr,
            "resolve %.*s: got '%s' (%s), expected '%.*s'\n",
            static_cast<int>(reference.size()),
            reference.data(),
            written.c_str(),
            url_error_text(error),
            static_cast<int>(canonical.size()),
            canonical.data()
        );
    }
    OA_CHECK(resolved.has_value());
    OA_CHECK(error == UrlError::none);
    OA_CHECK(written == canonical);
}

/// Accepted addresses: case, default ports, an empty path, dot segments, a
/// fragment dropped and a query kept.
void test_accepted() {
    expect_url(
        "http://example.org/maps",
        Scheme::http,
        "example.org",
        80,
        "/maps",
        "http://example.org/maps"
    );
    expect_url(
        "HTTP://Example.ORG/Maps",
        Scheme::http,
        "example.org",
        80,
        "/Maps",
        "http://example.org/Maps"
    );
    expect_url("http://example.org", Scheme::http, "example.org", 80, "/", "http://example.org/");
    expect_url(
        "http://example.org:80/a", Scheme::http, "example.org", 80, "/a", "http://example.org/a"
    );
    expect_url(
        "http://example.org:8080/a",
        Scheme::http,
        "example.org",
        8080,
        "/a",
        "http://example.org:8080/a"
    );
    expect_url(
        "https://Example.ORG./page",
        Scheme::https,
        "example.org",
        443,
        "/page",
        "https://example.org/page"
    );
    expect_url(
        "https://example.org:443/", Scheme::https, "example.org", 443, "/", "https://example.org/"
    );
    expect_url(
        "https://example.org:8443/a",
        Scheme::https,
        "example.org",
        8443,
        "/a",
        "https://example.org:8443/a"
    );
    expect_url(
        "http://example.org/a/./b/../c",
        Scheme::http,
        "example.org",
        80,
        "/a/c",
        "http://example.org/a/c"
    );
    expect_url(
        "http://example.org/a/b?x=1#frag",
        Scheme::http,
        "example.org",
        80,
        "/a/b?x=1",
        "http://example.org/a/b?x=1"
    );
    expect_url(
        "http://example.org/a%2F%2e%2e",
        Scheme::http,
        "example.org",
        80,
        "/a%2F%2e%2e",
        "http://example.org/a%2F%2e%2e"
    );
    expect_url("http://127.0.0.1/", Scheme::http, "127.0.0.1", 80, "/", "http://127.0.0.1/");
    expect_url("http://127.0.0.1.:9/a", Scheme::http, "127.0.0.1", 9, "/a", "http://127.0.0.1:9/a");
    expect_url(
        "http://example.org/a/b?",
        Scheme::http,
        "example.org",
        80,
        "/a/b?",
        "http://example.org/a/b?"
    );
    expect_url("http://a-b.example/", Scheme::http, "a-b.example", 80, "/", "http://a-b.example/");

    UrlError error = UrlError::bad_host;
    const std::optional<Url> fetched = parse_http_url("http://example.org/a", &error);
    OA_CHECK(fetched.has_value());
    OA_CHECK(error == UrlError::none);
    OA_CHECK(fetched && url_text(*fetched) == "http://example.org/a");

    const std::optional<Url> page = parse_web_url("https://example.org/", &error);
    OA_CHECK(page.has_value());
    OA_CHECK(page && page->scheme == Scheme::https && page->port == 443);
}

/// Each refusal, with at least two texts. The odd IPv4 spellings are here
/// because a rule that names 127.0.0.1 must not be fooled by another spelling.
void test_refused() {
    expect_refused("", UrlError::empty);
    expect_refused(std::string_view{}, UrlError::empty);

    expect_refused(std::string(max_url_bytes + 1, 'a'), UrlError::too_long);
    expect_refused("http://" + std::string(max_url_bytes, 'a'), UrlError::too_long);

    expect_refused("host/path", UrlError::no_scheme);
    expect_refused("/a/b", UrlError::no_scheme);
    expect_refused("://example.org/", UrlError::no_scheme);

    expect_refused("ftp://host/", UrlError::unknown_scheme);
    expect_refused("file://host/a", UrlError::unknown_scheme);

    expect_refused("https://example.org/", UrlError::https_not_fetched, true);
    expect_refused("HTTPS://example.org/a", UrlError::https_not_fetched, true);

    expect_refused("http://user@host/", UrlError::user_info);
    expect_refused("http://user:secret@host/a", UrlError::user_info);

    expect_refused("http://[::1]/", UrlError::ipv6_host);
    expect_refused("http://[::1]:80/", UrlError::ipv6_host);

    expect_refused("http://127.1/", UrlError::bad_host);
    expect_refused("http://0x7f.0.0.1/", UrlError::bad_host);
    expect_refused("http://2130706433/", UrlError::bad_host);
    expect_refused("http://010.0.0.1/", UrlError::bad_host);
    expect_refused("http://-host/", UrlError::bad_host);
    expect_refused("http://host-/", UrlError::bad_host);
    expect_refused("http://host..example/", UrlError::bad_host);
    expect_refused("http://256.0.0.1/", UrlError::bad_host);
    expect_refused("http://1.2.3/", UrlError::bad_host);

    expect_refused("http://host:0/", UrlError::bad_port);
    expect_refused("http://host:65536/", UrlError::bad_port);
    expect_refused("http://host:/", UrlError::bad_port);
    expect_refused("http://host:80a/", UrlError::bad_port);

    expect_refused("http://ho st/", UrlError::bad_character);
    expect_refused("http://host/a b", UrlError::bad_character);
    expect_refused("http://host/a|b", UrlError::bad_character);

    expect_refused("http://host/a%zz", UrlError::bad_escape);
    expect_refused("http://host/a%", UrlError::bad_escape);
    expect_refused("http://host/a%2", UrlError::bad_escape);
    expect_refused("http://host/q%zz", UrlError::bad_escape);

    OA_CHECK(!parse_web_url("http://127.1/").has_value());
    OA_CHECK(!parse_http_url("https://example.org/").has_value());
    UrlError untouched = UrlError::none;
    OA_CHECK(!parse_web_url("ftp://host/"));
    OA_CHECK(untouched == UrlError::none);
}

/// RFC 3986 section 5.4 against the base http://a/b/c/d;p?q.
///
/// Fragments are dropped, and an empty path is written with a slash, so the
/// canonical text differs from the RFC's where the RFC keeps a fragment or
/// leaves the path empty. g:h is an unknown scheme. The strict reading of
/// http:g has no host, so it is not an address this reader keeps.
void test_resolution() {
    // Section 5.4.1, normal examples, except g:h.
    expect_resolved("g", "http://a/b/c/g");
    expect_resolved("./g", "http://a/b/c/g");
    expect_resolved("g/", "http://a/b/c/g/");
    expect_resolved("/g", "http://a/g");
    expect_resolved("//g", "http://g/");
    expect_resolved("?y", "http://a/b/c/d;p?y");
    expect_resolved("g?y", "http://a/b/c/g?y");
    expect_resolved("#s", "http://a/b/c/d;p?q");
    expect_resolved("g#s", "http://a/b/c/g");
    expect_resolved("g?y#s", "http://a/b/c/g?y");
    expect_resolved(";x", "http://a/b/c/;x");
    expect_resolved("g;x", "http://a/b/c/g;x");
    expect_resolved("g;x?y#s", "http://a/b/c/g;x?y");
    expect_resolved("", "http://a/b/c/d;p?q");
    expect_resolved(".", "http://a/b/c/");
    expect_resolved("./", "http://a/b/c/");
    expect_resolved("..", "http://a/b/");
    expect_resolved("../", "http://a/b/");
    expect_resolved("../g", "http://a/b/g");
    expect_resolved("../..", "http://a/");
    expect_resolved("../../", "http://a/");
    expect_resolved("../../g", "http://a/g");

    UrlError error = UrlError::none;
    const std::optional<Url> base = parse_web_url("http://a/b/c/d;p?q");
    OA_CHECK(base.has_value());
    if (base) {
        OA_CHECK(!resolve_reference(*base, "g:h", &error).has_value());
        OA_CHECK(error == UrlError::unknown_scheme);
    }

    // Section 5.4.2, the abnormal examples whose result stays an http address.
    expect_resolved("../../../g", "http://a/g");
    expect_resolved("../../../../g", "http://a/g");
    expect_resolved("/./g", "http://a/g");
    expect_resolved("/../g", "http://a/g");
    expect_resolved("g.", "http://a/b/c/g.");
    expect_resolved(".g", "http://a/b/c/.g");
    expect_resolved("g..", "http://a/b/c/g..");
    expect_resolved("..g", "http://a/b/c/..g");
    expect_resolved("./../g", "http://a/b/g");
    expect_resolved("./g/.", "http://a/b/c/g/");
    expect_resolved("g/./h", "http://a/b/c/g/h");
    expect_resolved("g/../h", "http://a/b/c/h");
    expect_resolved("g;x=1/./y", "http://a/b/c/g;x=1/y");
    expect_resolved("g;x=1/../y", "http://a/b/c/y");
    expect_resolved("g?y/./x", "http://a/b/c/g?y/./x");
    expect_resolved("g?y/../x", "http://a/b/c/g?y/../x");
    expect_resolved("g#s/./x", "http://a/b/c/g");
    expect_resolved("g#s/../x", "http://a/b/c/g");

    const std::optional<Url> replaced = base ? resolve_reference(*base, "http://other/x") : base;
    OA_CHECK(replaced.has_value());
    OA_CHECK(replaced && url_text(*replaced) == "http://other/x");
    const std::optional<Url> secured = base ? resolve_reference(*base, "https://other/x") : base;
    OA_CHECK(secured.has_value());
    OA_CHECK(secured && secured->scheme == Scheme::https && secured->host == "other");
}

/// A host under a domain, and an IPv4 literal only under itself.
void test_hosts() {
    OA_CHECK(host_within("a.example.org", "example.org"));
    OA_CHECK(host_within("A.Example.ORG.", "example.org"));
    OA_CHECK(!host_within("badexample.org", "example.org"));
    OA_CHECK(host_within("example.org", "example.org"));
    OA_CHECK(host_within("example.org.", "Example.ORG"));
    OA_CHECK(!host_within("example.org.extra", "example.org"));
    OA_CHECK(is_ipv4_literal("127.0.0.1"));
    OA_CHECK(!is_ipv4_literal("127.1"));
    OA_CHECK(!is_ipv4_literal("010.0.0.1"));
    OA_CHECK(!is_ipv4_literal("example.org"));
    OA_CHECK(host_within("127.0.0.1", "127.0.0.1"));
    OA_CHECK(!host_within("127.0.0.1", "0.0.1"));
    OA_CHECK(!host_within("127.0.0.1", "example.org"));
    OA_CHECK(!host_within("not.127.0.0.1", "127.0.0.1"));

    const Url plain = *parse_web_url("http://example.org/a");
    const Url same = *parse_web_url("http://Example.ORG:80/b?q");
    const Url other_port = *parse_web_url("http://example.org:8080/");
    const Url secure = *parse_web_url("https://example.org/");
    OA_CHECK(same_server(plain, same));
    OA_CHECK(!same_server(plain, other_port));
    OA_CHECK(!same_server(plain, secure));
    OA_CHECK(host_header(plain) == "example.org");
    OA_CHECK(host_header(other_port) == "example.org:8080");
    OA_CHECK(host_header(secure) == "example.org");
    OA_CHECK(host_header(*parse_web_url("https://example.org:444/")) == "example.org:444");
    OA_CHECK(has_query(same));
    OA_CHECK(!has_query(plain));
}

} // namespace

int main() {
    test_accepted();
    test_refused();
    test_resolution();
    test_hosts();
    return oa::test::check_exit_status();
}
