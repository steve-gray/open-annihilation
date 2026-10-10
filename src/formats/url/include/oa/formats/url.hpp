// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Web addresses the registries and the catalogue share: an absolute http or
// https address, read strictly, written back in one form, and resolved the
// way RFC 3986 section 5.2 resolves a reference. Nothing here fetches.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace oa::formats::url {

/// The longest address this reader accepts, in bytes.
inline constexpr std::size_t max_url_bytes = 2048;

/// Which kind of web address this is. https is only a page the system's
/// browser opens; a fetch uses http.
enum class Scheme : uint8_t {
    http,  ///< plain HTTP
    https, ///< a page the system's browser opens
};

/// One absolute web address, already in the form the readers agree on.
struct Url {
    Scheme scheme = Scheme::http;
    std::string host;   ///< lower case, no trailing dot: a name, or four decimal numbers
    uint16_t port = 80; ///< the scheme's default when the text gave none
    std::string target; ///< path and query, from '/', as a request line carries it

    /// True when both addresses name the same scheme, host, port and target.
    ///
    /// @param other the address to compare with
    /// @return true when every part matches
    bool operator==(const Url& other) const = default;
};

/// Why an address was refused. `none` is a successful read.
enum class UrlError : uint8_t {
    none,              ///< the address was read
    empty,             ///< no bytes
    too_long,          ///< more than max_url_bytes
    no_scheme,         ///< no http or https scheme
    unknown_scheme,    ///< a scheme that is neither http nor https
    https_not_fetched, ///< https, which a fetch does not use
    user_info,         ///< a user name or password in the authority
    ipv6_host,         ///< an IPv6 literal
    bad_host,          ///< a host that is not a domain or four decimal numbers
    bad_port,          ///< a port that is not a decimal number from 1 to 65535
    bad_character, ///< a byte this reader does not accept, or one outside the path and query sets
    bad_escape,    ///< a '%' not followed by two hex digits
};

/// The sentence a player of the tools would read for a refusal.
///
/// @param error the refusal, or none
/// @return a stable description; never null
[[nodiscard]] const char* url_error_text(UrlError error) noexcept;

/// Reads an absolute http or https address.
///
/// The text is 1 to max_url_bytes bytes of printable ASCII (0x21-0x7E). The
/// scheme is `http` or `https` in any case, followed by `://`. The host is
/// lower-cased and loses one trailing dot. A fragment is dropped. `.` and
/// `..` path segments are removed. Percent-escapes are kept as written.
///
/// @param text the address
/// @param error set to the refusal, or to none on success; may be null
/// @return the address, or nothing when it was refused
[[nodiscard]] std::optional<Url> parse_web_url(std::string_view text, UrlError* error = nullptr);

/// Reads an absolute http address that a fetch may use.
///
/// The same rules as parse_web_url. An https address is refused with
/// https_not_fetched: OA fetches over plain HTTP and checks what it gets
/// with signatures.
///
/// @param text the address
/// @param error set to the refusal, or to none on success; may be null
/// @return the address, or nothing when it was refused
[[nodiscard]] std::optional<Url> parse_http_url(std::string_view text, UrlError* error = nullptr);

/// Writes an address in the one form the readers agree on.
///
/// `<scheme>://<host>[:<port>]<target>`. The scheme's default port (80 for
/// http, 443 for https) is left out.
///
/// @param url an address this reader produced, or the same shape
/// @return the canonical text
[[nodiscard]] std::string url_text(const Url& url);

/// The Host header value for an address: the host, and the port when it is
/// not the scheme's default.
///
/// @param url an address this reader produced, or the same shape
/// @return `host` or `host:port`
[[nodiscard]] std::string host_header(const Url& url);

/// Tells whether the request target carries a query, including an empty one.
///
/// @param url an address this reader produced, or the same shape
/// @return true when `target` contains `?`
[[nodiscard]] bool has_query(const Url& url) noexcept;

/// Resolves a reference against a base as RFC 3986 section 5.2.2 does, then
/// applies the same rules as parse_web_url to the result.
///
/// A reference with its own scheme is read whole. `//` takes a new
/// authority, `/` a new path, `?` a new query, and anything else merges with
/// the base path. A fragment on the reference is dropped. `.` and `..`
/// segments are removed as section 5.2.4 describes.
///
/// @param base an address this reader produced
/// @param reference a reference of at most max_url_bytes printable ASCII bytes; empty keeps the base
/// @param error set to the refusal, or to none on success; may be null
/// @return the resolved address, or nothing when the reference or the result was refused
[[nodiscard]] std::optional<Url>
resolve_reference(const Url& base, std::string_view reference, UrlError* error = nullptr);

/// Tells whether a host is four decimal numbers from 0 to 255, with no
/// leading zeros.
///
/// @param host a host, compared as given (no case folding)
/// @return true for that spelling only
[[nodiscard]] bool is_ipv4_literal(std::string_view host) noexcept;

/// Tells whether a host is a domain or lies under it.
///
/// Both sides are compared after lower-casing and the removal of one
/// trailing dot. An IPv4 literal, as the host or as the domain, is within
/// only that same text. Any other host is within the domain when they are
/// equal or the host ends with `.` and the domain.
///
/// @param host the host
/// @param domain the domain
/// @return true when the host is the domain or a name under it
[[nodiscard]] bool host_within(std::string_view host, std::string_view domain) noexcept;

/// Tells whether two addresses are fetched from the same server.
///
/// @param a one address
/// @param b the other
/// @return true when scheme, host and port all match
[[nodiscard]] bool same_server(const Url& a, const Url& b) noexcept;

} // namespace oa::formats::url
