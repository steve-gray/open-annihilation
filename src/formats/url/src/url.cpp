// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The strict reader for the web addresses registries use. Hosts are made
// canonical before anything compares them, and an IPv4 host is accepted
// only as four plain decimal numbers, so another spelling of the same
// address cannot slip past a rule written for one spelling.

#include "oa/formats/url.hpp"

#include <cstdint>

namespace oa::formats::url {
namespace {

/// Records a refusal and returns an empty result.
///
/// @param error where the caller asked to hear the reason; may be null
/// @param code the refusal
/// @return an empty address
std::optional<Url> fail(UrlError* error, UrlError code) {
    if (error != nullptr)
        *error = code;
    return std::nullopt;
}

/// Records success.
///
/// @param error where the caller asked to hear the reason; may be null
void succeed(UrlError* error) {
    if (error != nullptr)
        *error = UrlError::none;
}

/// The port a scheme uses when the address names none.
///
/// @param scheme http or https
/// @return 80 or 443
uint16_t default_port(Scheme scheme) noexcept {
    return scheme == Scheme::https ? uint16_t{443} : uint16_t{80};
}

/// True for a printable ASCII byte, excluding space.
///
/// @param byte the byte
/// @return true for 0x21-0x7E
bool printable_ascii(unsigned char byte) noexcept {
    return byte >= 0x21 && byte <= 0x7E;
}

/// True for an ASCII letter.
///
/// @param byte the byte
/// @return true for A-Z or a-z
bool is_alpha(unsigned char byte) noexcept {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

/// True for a decimal digit.
///
/// @param byte the byte
/// @return true for 0-9
bool is_digit(unsigned char byte) noexcept {
    return byte >= '0' && byte <= '9';
}

/// True for a hex digit.
///
/// @param byte the byte
/// @return true for 0-9, A-F or a-f
bool is_hex(unsigned char byte) noexcept {
    return is_digit(byte) || (byte >= 'A' && byte <= 'F') || (byte >= 'a' && byte <= 'f');
}

/// Lower-cases one ASCII letter and leaves every other byte as it is.
///
/// @param byte the byte
/// @return the lower-case letter, or the same byte
char lower_ascii(unsigned char byte) noexcept {
    if (byte >= 'A' && byte <= 'Z')
        return static_cast<char>(byte - 'A' + 'a');
    return static_cast<char>(byte);
}

/// The scheme name, when the text has one before any slash, question mark or hash.
///
/// @param text the reference or address
/// @param scheme set to the bytes before the colon
/// @return true when those bytes are a scheme name
bool take_scheme(std::string_view text, std::string_view& scheme) noexcept {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0)
        return false;
    const auto cut = text.find_first_of("/?#");
    if (cut != std::string_view::npos && cut < colon)
        return false;
    if (!is_alpha(static_cast<unsigned char>(text.front())))
        return false;
    for (std::size_t index = 1; index < colon; ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if (!is_alpha(byte) && !is_digit(byte) && byte != '+' && byte != '-' && byte != '.')
            return false;
    }
    scheme = text.substr(0, colon);
    return true;
}

/// True when two scheme names are the same letters ignoring case.
///
/// @param scheme the name as written
/// @param literal the lower-case name to match
/// @return true when they match
bool scheme_is(std::string_view scheme, std::string_view literal) noexcept {
    if (scheme.size() != literal.size())
        return false;
    for (std::size_t index = 0; index < scheme.size(); ++index) {
        if (lower_ascii(static_cast<unsigned char>(scheme[index])) != literal[index])
            return false;
    }
    return true;
}

/// True when every byte of a label is a decimal digit.
///
/// @param label one host label
/// @return true when it is non-empty and all digits
bool all_digits(std::string_view label) noexcept {
    if (label.empty())
        return false;
    for (char byte : label) {
        if (!is_digit(static_cast<unsigned char>(byte)))
            return false;
    }
    return true;
}

/// Reads one decimal octet, refusing a leading zero and a value above 255.
///
/// @param label the text of one number
/// @param value set to the number when it is an octet
/// @return true when the label is one octet in plain decimal
bool decimal_octet(std::string_view label, int& value) noexcept {
    if (!all_digits(label) || label.size() > 3)
        return false;
    if (label.size() > 1 && label.front() == '0')
        return false;
    int number = 0;
    for (char byte : label)
        number = number * 10 + (byte - '0');
    if (number > 255)
        return false;
    value = number;
    return true;
}

/// True for four plain decimal octets and nothing else.
///
/// @param host the host, already lower-cased and without a trailing dot
/// @return true for that spelling only
bool four_decimal_octets(std::string_view host) noexcept {
    int octet = 0;
    int count = 0;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= host.size(); ++index) {
        if (index != host.size() && host[index] != '.')
            continue;
        if (!decimal_octet(host.substr(start, index - start), octet))
            return false;
        ++count;
        start = index + 1;
    }
    return count == 4;
}

/// Checks a host that has already been lower-cased and has lost one trailing dot.
///
/// @param host the canonical host text
/// @return the refusal, or none
UrlError check_host(std::string_view host) noexcept {
    if (host.empty() || host.size() > 253)
        return UrlError::bad_host;
    std::size_t label_start = 0;
    std::string_view last{};
    for (std::size_t index = 0; index <= host.size(); ++index) {
        if (index != host.size() && host[index] != '.')
            continue;
        const std::string_view label = host.substr(label_start, index - label_start);
        if (label.empty() || label.size() > 63)
            return UrlError::bad_host;
        if (label.front() == '-' || label.back() == '-')
            return UrlError::bad_host;
        for (char byte : label) {
            const auto value = static_cast<unsigned char>(byte);
            if (!is_digit(value) && !(value >= 'a' && value <= 'z') && value != '-')
                return UrlError::bad_host;
        }
        last = label;
        label_start = index + 1;
    }
    if (all_digits(last) && !four_decimal_octets(host))
        return UrlError::bad_host;
    return UrlError::none;
}

/// Characters a path may hold, aside from a percent-escape. Query also allows '?'.
///
/// @param byte the byte
/// @param query true when the byte sits in the query
/// @return true when the byte is in the set
bool target_byte(unsigned char byte, bool query) noexcept {
    if (is_alpha(byte) || is_digit(byte))
        return true;
    switch (byte) {
    case '-':
    case '.':
    case '_':
    case '~':
    case '!':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '+':
    case ',':
    case ';':
    case '=':
    case ':':
    case '@':
    case '/':
        return true;
    case '?':
        return query;
    default:
        return false;
    }
}

/// Checks a path or a query. Percent-escapes stay as written.
///
/// @param text the path, including its leading slash, or the query without '?'
/// @param query true for a query, which may also hold '?'
/// @return bad_escape, bad_character, or none
UrlError check_component(std::string_view text, bool query) noexcept {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if (byte == '%') {
            if (index + 2 >= text.size() || !is_hex(static_cast<unsigned char>(text[index + 1])) ||
                !is_hex(static_cast<unsigned char>(text[index + 2])))
                return UrlError::bad_escape;
            index += 2;
            continue;
        }
        if (!target_byte(byte, query))
            return UrlError::bad_character;
    }
    return UrlError::none;
}

/// Drops the last path segment, including the slash that introduces it.
///
/// @param output the path built so far
void pop_segment(std::string& output) {
    const auto slash = output.rfind('/');
    if (slash == std::string::npos)
        output.clear();
    else
        output.erase(slash);
}

/// Removes `.` and `..` segments as RFC 3986 section 5.2.4 describes.
///
/// @param path the path, which may be relative
/// @return the path with those segments removed
std::string remove_dot_segments(std::string path) {
    std::string output;
    while (!path.empty()) {
        if (path.starts_with("../")) {
            path.erase(0, 3);
        } else if (path.starts_with("./")) {
            path.erase(0, 2);
        } else if (path.starts_with("/./")) {
            path.replace(0, 3, "/");
        } else if (path == "/.") {
            path = "/";
        } else if (path.starts_with("/../")) {
            path.replace(0, 4, "/");
            pop_segment(output);
        } else if (path == "/..") {
            path = "/";
            pop_segment(output);
        } else if (path == "." || path == "..") {
            path.clear();
        } else {
            const std::size_t from = path.front() == '/' ? 1 : 0;
            const auto next = path.find('/', from);
            if (next == std::string::npos) {
                output += path;
                path.clear();
            } else {
                output.append(path, 0, next);
                path.erase(0, next);
            }
        }
    }
    return output;
}

/// Joins a base path with a relative path as RFC 3986 section 5.2.3 describes.
///
/// The base is an absolute address, so its path is empty or begins with '/'.
/// An empty base path (only possible before this reader stores '/') is merged
/// as a path under the root.
///
/// @param base_path the base path, without its query
/// @param reference_path the reference path, without a leading slash
/// @return the merged path, before dot segments are removed
std::string merge_paths(std::string_view base_path, std::string_view reference_path) {
    if (base_path.empty())
        return std::string{"/"} + std::string{reference_path};
    const auto slash = base_path.rfind('/');
    return std::string{base_path.substr(0, slash + 1)} + std::string{reference_path};
}

/// A reference split into the parts section 5.2.2 transforms. Absent parts
/// stay unset so they can be told from empty ones.
struct Reference {
    bool has_scheme = false;
    Scheme scheme = Scheme::http;
    bool has_authority = false;
    std::string host;
    bool port_given = false;
    uint16_t port = 0;
    std::string path;
    bool has_query = false;
    std::string query;
};

/// Reads the authority of an absolute address or a scheme-relative reference.
///
/// @param authority the bytes after `//`, up to the path, query or fragment
/// @param host set to the canonical host
/// @param port_given set when the text named a port
/// @param port set to that port
/// @return the refusal, or none
UrlError
read_authority(std::string_view authority, std::string& host, bool& port_given, uint16_t& port) {
    if (authority.find('@') != std::string_view::npos)
        return UrlError::user_info;
    if (authority.find('[') != std::string_view::npos)
        return UrlError::ipv6_host;
    const auto colon = authority.rfind(':');
    std::string_view host_text = authority;
    port_given = false;
    port = 0;
    if (colon != std::string_view::npos) {
        host_text = authority.substr(0, colon);
        const std::string_view port_text = authority.substr(colon + 1);
        if (port_text.empty() || port_text.size() > 5)
            return UrlError::bad_port;
        int number = 0;
        for (char byte : port_text) {
            if (!is_digit(static_cast<unsigned char>(byte)))
                return UrlError::bad_port;
            number = number * 10 + (byte - '0');
        }
        if (number < 1 || number > 65535)
            return UrlError::bad_port;
        port_given = true;
        port = static_cast<uint16_t>(number);
    }
    std::string canonical;
    canonical.reserve(host_text.size());
    for (char byte : host_text)
        canonical.push_back(lower_ascii(static_cast<unsigned char>(byte)));
    if (!canonical.empty() && canonical.back() == '.')
        canonical.pop_back();
    if (const UrlError host_error = check_host(canonical); host_error != UrlError::none)
        return host_error;
    host = std::move(canonical);
    return UrlError::none;
}

/// Splits the path, query and fragment that follow an authority.
///
/// The fragment is dropped. The path is left as written, without dot removal.
///
/// @param rest bytes from the first slash, question mark or hash, or empty
/// @param reference filled in for the path and the query
/// @return the refusal, or none
UrlError read_path_query(std::string_view rest, Reference& reference) {
    const auto hash = rest.find('#');
    if (hash != std::string_view::npos)
        rest = rest.substr(0, hash);
    const auto question = rest.find('?');
    std::string_view path = rest;
    if (question != std::string_view::npos) {
        path = rest.substr(0, question);
        reference.has_query = true;
        reference.query = std::string{rest.substr(question + 1)};
        if (const UrlError query_error = check_component(reference.query, true);
            query_error != UrlError::none)
            return query_error;
    }
    reference.path = std::string{path};
    return check_component(reference.path, false);
}

/// Reads an absolute http or https address into parts, without dot removal.
///
/// @param text the whole address, already bounded and printable
/// @param reference filled in on success
/// @param http_only true when https is a refusal of its own
/// @return the refusal, or none
UrlError read_absolute(std::string_view text, Reference& reference, bool http_only) {
    std::string_view scheme;
    if (!take_scheme(text, scheme))
        return UrlError::no_scheme;
    const bool http = scheme_is(scheme, "http");
    const bool https = scheme_is(scheme, "https");
    if (!http && !https)
        return UrlError::unknown_scheme;
    if (https && http_only)
        return UrlError::https_not_fetched;
    if (text.size() < scheme.size() + 3 || text[scheme.size()] != ':' ||
        text[scheme.size() + 1] != '/' || text[scheme.size() + 2] != '/')
        return UrlError::bad_host;
    reference.has_scheme = true;
    reference.scheme = https ? Scheme::https : Scheme::http;
    const std::string_view after = text.substr(scheme.size() + 3);
    const auto path_at = after.find_first_of("/?#");
    const std::string_view authority =
        path_at == std::string_view::npos ? after : after.substr(0, path_at);
    const std::string_view rest =
        path_at == std::string_view::npos ? std::string_view{} : after.substr(path_at);
    reference.has_authority = true;
    if (const UrlError authority_error =
            read_authority(authority, reference.host, reference.port_given, reference.port);
        authority_error != UrlError::none)
        return authority_error;
    return read_path_query(rest, reference);
}

/// Builds an address from parts, removing dot segments and supplying a root path.
///
/// @param reference parts that have already passed the character rules
/// @return the address in canonical form
Url url_from_parts(Reference reference) {
    Url url;
    url.scheme = reference.scheme;
    url.host = std::move(reference.host);
    url.port = reference.port_given ? reference.port : default_port(reference.scheme);
    std::string path = remove_dot_segments(std::move(reference.path));
    if (path.empty())
        path = "/";
    if (reference.has_query) {
        path.push_back('?');
        path += reference.query;
    }
    url.target = std::move(path);
    return url;
}

/// Refuses a text that is empty, too long, or not printable ASCII.
///
/// @param text the address or reference
/// @param allow_empty true for a reference, where an empty text keeps the base
/// @return the refusal, or none
UrlError check_text(std::string_view text, bool allow_empty) noexcept {
    if (text.empty())
        return allow_empty ? UrlError::none : UrlError::empty;
    if (text.size() > max_url_bytes)
        return UrlError::too_long;
    for (char byte : text) {
        if (!printable_ascii(static_cast<unsigned char>(byte)))
            return UrlError::bad_character;
    }
    return UrlError::none;
}

/// Reads a whole address for a fetch or for a page the browser opens.
///
/// @param text the address
/// @param error set on the way out
/// @param http_only true when https is refused
/// @return the address, or nothing
std::optional<Url> parse_absolute(std::string_view text, UrlError* error, bool http_only) {
    if (const UrlError text_error = check_text(text, false); text_error != UrlError::none)
        return fail(error, text_error);
    Reference reference;
    if (const UrlError read_error = read_absolute(text, reference, http_only);
        read_error != UrlError::none)
        return fail(error, read_error);
    succeed(error);
    return url_from_parts(std::move(reference));
}

/// Splits a base target into its path and its query.
///
/// @param target the stored target
/// @param path set to the path, from '/'
/// @param has_query set when a query is present
/// @param query set to the query without '?'
void split_target(std::string_view target, std::string& path, bool& has_query, std::string& query) {
    const auto question = target.find('?');
    if (question == std::string_view::npos) {
        path = std::string{target};
        has_query = false;
        query.clear();
        return;
    }
    path = std::string{target.substr(0, question)};
    has_query = true;
    query = std::string{target.substr(question + 1)};
}

/// Reads a reference that has no scheme of its own.
///
/// @param text the reference, without a fragment
/// @param reference filled in on success
/// @return the refusal, or none
UrlError read_relative(std::string_view text, Reference& reference) {
    if (text.starts_with("//")) {
        reference.has_authority = true;
        const std::string_view after = text.substr(2);
        const auto path_at = after.find_first_of("/?#");
        const std::string_view authority =
            path_at == std::string_view::npos ? after : after.substr(0, path_at);
        const std::string_view rest =
            path_at == std::string_view::npos ? std::string_view{} : after.substr(path_at);
        if (const UrlError authority_error =
                read_authority(authority, reference.host, reference.port_given, reference.port);
            authority_error != UrlError::none)
            return authority_error;
        return read_path_query(rest, reference);
    }
    return read_path_query(text, reference);
}

} // namespace

const char* url_error_text(UrlError error) noexcept {
    switch (error) {
    case UrlError::none:
        return "address accepted";
    case UrlError::empty:
        return "address is empty";
    case UrlError::too_long:
        return "address is longer than 2048 bytes";
    case UrlError::no_scheme:
        return "address has no scheme";
    case UrlError::unknown_scheme:
        return "address scheme is not http or https";
    case UrlError::https_not_fetched:
        return "OA fetches over plain HTTP and checks what it gets with signatures";
    case UrlError::user_info:
        return "address carries user info";
    case UrlError::ipv6_host:
        return "address names an IPv6 host";
    case UrlError::bad_host:
        return "address host is not a domain or four decimal numbers";
    case UrlError::bad_port:
        return "address port is not a number from 1 to 65535";
    case UrlError::bad_character:
        return "address holds a character it cannot";
    case UrlError::bad_escape:
        return "address has a percent escape that is not two hex digits";
    }
    return "address was refused";
}

std::optional<Url> parse_web_url(std::string_view text, UrlError* error) {
    return parse_absolute(text, error, false);
}

std::optional<Url> parse_http_url(std::string_view text, UrlError* error) {
    return parse_absolute(text, error, true);
}

std::string url_text(const Url& url) {
    std::string text = url.scheme == Scheme::https ? "https://" : "http://";
    text += url.host;
    if (url.port != default_port(url.scheme)) {
        text.push_back(':');
        text += std::to_string(url.port);
    }
    text += url.target;
    return text;
}

std::string host_header(const Url& url) {
    std::string text = url.host;
    if (url.port != default_port(url.scheme)) {
        text.push_back(':');
        text += std::to_string(url.port);
    }
    return text;
}

bool has_query(const Url& url) noexcept {
    return url.target.find('?') != std::string::npos;
}

std::optional<Url> resolve_reference(const Url& base, std::string_view reference, UrlError* error) {
    if (const UrlError text_error = check_text(reference, true); text_error != UrlError::none)
        return fail(error, text_error);

    std::string_view scheme;
    if (take_scheme(reference, scheme)) {
        if (!scheme_is(scheme, "http") && !scheme_is(scheme, "https"))
            return fail(error, UrlError::unknown_scheme);
        return parse_absolute(reference, error, false);
    }

    const auto hash = reference.find('#');
    const std::string_view body =
        hash == std::string_view::npos ? reference : reference.substr(0, hash);

    Reference relative;
    if (const UrlError relative_error = read_relative(body, relative);
        relative_error != UrlError::none)
        return fail(error, relative_error);

    Reference result;
    result.has_scheme = true;
    result.scheme = base.scheme;
    std::string base_path;
    bool base_query = false;
    std::string base_query_text;
    split_target(base.target, base_path, base_query, base_query_text);

    if (relative.has_authority) {
        result.has_authority = true;
        result.host = std::move(relative.host);
        result.port_given = relative.port_given;
        result.port = relative.port;
        result.path = std::move(relative.path);
        result.has_query = relative.has_query;
        result.query = std::move(relative.query);
    } else {
        result.has_authority = true;
        result.host = base.host;
        result.port_given = true;
        result.port = base.port;
        if (relative.path.empty()) {
            result.path = std::move(base_path);
            if (relative.has_query) {
                result.has_query = true;
                result.query = std::move(relative.query);
            } else {
                result.has_query = base_query;
                result.query = std::move(base_query_text);
            }
        } else {
            if (relative.path.front() == '/')
                result.path = std::move(relative.path);
            else
                result.path = merge_paths(base_path, relative.path);
            result.has_query = relative.has_query;
            result.query = std::move(relative.query);
        }
    }

    // Dot removal runs inside url_from_parts. The base path was already
    // removed of its own dot segments when the base was read, and merging
    // or replacing the path runs the removal once on the result.
    succeed(error);
    return url_from_parts(std::move(result));
}

bool is_ipv4_literal(std::string_view host) noexcept {
    return four_decimal_octets(host);
}

bool host_within(std::string_view host, std::string_view domain) noexcept {
    auto canonical = [](std::string_view text) {
        std::string value;
        value.reserve(text.size());
        for (char byte : text)
            value.push_back(lower_ascii(static_cast<unsigned char>(byte)));
        if (!value.empty() && value.back() == '.')
            value.pop_back();
        return value;
    };
    const std::string host_text = canonical(host);
    const std::string domain_text = canonical(domain);
    if (host_text.empty() || domain_text.empty())
        return false;
    // A literal is a single address, so it is within only that same text,
    // whether it is the host or the domain being asked about.
    if (is_ipv4_literal(host_text) || is_ipv4_literal(domain_text))
        return host_text == domain_text;
    if (host_text == domain_text)
        return true;
    if (host_text.size() <= domain_text.size())
        return false;
    const std::size_t suffix = host_text.size() - domain_text.size();
    return host_text[suffix - 1] == '.' &&
           host_text.compare(suffix, domain_text.size(), domain_text) == 0;
}

bool same_server(const Url& a, const Url& b) noexcept {
    return a.scheme == b.scheme && a.host == b.host && a.port == b.port;
}

} // namespace oa::formats::url
