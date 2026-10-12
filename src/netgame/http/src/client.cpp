// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Fetches one address at a time: the pool, the time limits and the body sink.

#include "oa/netgame/http/client.hpp"

#include "gzip.hpp"
#include "lookup.hpp"
#include "response_reader.hpp"
#include "tls.hpp"
#include "url_request.hpp"

#include "oa/formats/url.hpp"
#include "oa/netgame/stream_socket.hpp"

#include <chrono>
#include <cstring>
#include <utility>

namespace oa::netgame::http {
namespace {

namespace url = oa::formats::url;
namespace sock = oa::netgame::sock;

/// Compares two texts without regard to case.
///
/// @param left one text
/// @param right the other
/// @return true when they are the same letters
bool same_name(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(left[i]);
        unsigned char b = static_cast<unsigned char>(right[i]);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

/// Drops spaces and tabs from both ends.
///
/// @param text the text
/// @return the text inside the spaces
std::string_view trim_ows(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

/// Reads a decimal length.
///
/// @param text the digits
/// @param[out] value the length
/// @return false when the text is not a decimal length
bool parse_u64(std::string_view text, uint64_t& value) {
    if (text.empty())
        return false;
    uint64_t number = 0;
    for (const char byte : text) {
        if (byte < '0' || byte > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(byte - '0');
        if (number > (UINT64_MAX - digit) / 10)
            return false;
        number = number * 10 + digit;
    }
    value = number;
    return true;
}

/// Reads a Content-Range value.
///
/// @param text the header value
/// @return the range, or nothing when it is not `bytes first-last/total`
std::optional<ContentRange> parse_content_range(std::string_view text) {
    text = trim_ows(text);
    if (text.size() < 6 || !same_name(text.substr(0, 5), "bytes") || text[5] != ' ')
        return std::nullopt;
    text.remove_prefix(6);
    const std::size_t dash = text.find('-');
    const std::size_t slash = text.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos || slash < dash ||
        dash == 0)
        return std::nullopt;
    ContentRange range;
    if (!parse_u64(text.substr(0, dash), range.first))
        return std::nullopt;
    if (!parse_u64(text.substr(dash + 1, slash - dash - 1), range.last) || range.last < range.first)
        return std::nullopt;
    const std::string_view total = text.substr(slash + 1);
    if (total == "*")
        return range;
    uint64_t whole = 0;
    if (!parse_u64(total, whole) || range.last >= whole)
        return std::nullopt;
    range.total = whole;
    return range;
}

/// True for a redirect status.
///
/// @param status the response status
/// @return true for 301, 302, 303, 307 and 308
bool redirect_status(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/// True when a redirect may name this host.
///
/// The request's own host and port are always allowed. An allow-list entry is
/// one host, or a leading dot and a domain for that domain and every name under it.
///
/// @param policy the allow-list
/// @param target the address the redirect names
/// @param origin the address the fetch started with
/// @return true when the redirect may be followed
bool host_allowed(const RedirectPolicy& policy, const url::Url& target, const url::Url& origin) {
    // The same host on the same port, or on the port the target scheme uses
    // when an address names none: http to https on 443, and the reverse.
    const uint16_t scheme_port = target.scheme == url::Scheme::https ? uint16_t{443} : uint16_t{80};
    if (target.host == origin.host && (target.port == origin.port || target.port == scheme_port))
        return true;
    for (const std::string& entry : policy.allowed_hosts) {
        if (entry.empty())
            continue;
        if (entry.front() == '.') {
            if (url::host_within(target.host, std::string_view(entry).substr(1)))
                return true;
        } else if (same_name(entry, target.host)) {
            return true;
        }
    }
    return false;
}

/// Milliseconds from a start to now.
///
/// @param from the start
/// @param now the current time
/// @return the elapsed milliseconds, never negative
int64_t
elapsed_ms(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point now) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - from).count();
    return elapsed < 0 ? 0 : elapsed;
}

/// How a fetch's clocks stand.
struct Budget {
    enum class Hit : uint8_t { ok, cancel, total, connect, idle };

    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point hop = started;
    std::chrono::steady_clock::time_point idle_at = started;
    Timeouts timeouts{};
    const std::atomic<bool>* cancel = nullptr;

    /// Starts the connect clock for one address.
    void begin_hop() {
        hop = std::chrono::steady_clock::now();
        idle_at = hop;
    }

    /// A byte was sent or arrived.
    void touch() { idle_at = std::chrono::steady_clock::now(); }

    /// Which limit, if any, has run out.
    ///
    /// @param connecting true while the name is looked up or the connection opens
    /// @return ok, or the limit that ran out
    [[nodiscard]] Hit hit(bool connecting) const {
        if (cancel != nullptr && cancel->load())
            return Hit::cancel;
        const auto now = std::chrono::steady_clock::now();
        if (timeouts.total_ms > 0 &&
            elapsed_ms(started, now) >= static_cast<int64_t>(timeouts.total_ms))
            return Hit::total;
        if (connecting && elapsed_ms(hop, now) >= static_cast<int64_t>(timeouts.connect_ms))
            return Hit::connect;
        if (!connecting && elapsed_ms(idle_at, now) >= static_cast<int64_t>(timeouts.idle_ms))
            return Hit::idle;
        return Hit::ok;
    }

    /// Milliseconds left of one clock, or zero when it has run out.
    ///
    /// @param from the clock's start
    /// @param limit the clock's limit
    /// @param now the current time
    /// @return the remaining milliseconds
    [[nodiscard]] static uint32_t remaining(
        std::chrono::steady_clock::time_point from,
        uint32_t limit,
        std::chrono::steady_clock::time_point now
    ) {
        const int64_t elapsed = elapsed_ms(from, now);
        if (elapsed >= static_cast<int64_t>(limit))
            return 0;
        const int64_t left = static_cast<int64_t>(limit) - elapsed;
        if (left > static_cast<int64_t>(UINT32_MAX))
            return UINT32_MAX;
        return static_cast<uint32_t>(left);
    }

    /// The next wait, at most 50 milliseconds, and never past a clock.
    ///
    /// @param connecting true while the connection opens
    /// @return the wait, in milliseconds
    [[nodiscard]] uint32_t slice(bool connecting) const {
        uint32_t left = 50;
        const auto now = std::chrono::steady_clock::now();
        const auto cap = [&](std::chrono::steady_clock::time_point from, uint32_t limit) {
            const uint32_t remain = remaining(from, limit, now);
            if (remain < left)
                left = remain;
        };
        if (timeouts.total_ms > 0)
            cap(started, timeouts.total_ms);
        if (connecting)
            cap(hop, timeouts.connect_ms);
        else
            cap(idle_at, timeouts.idle_ms);
        return left;
    }

    /// How long a name lookup may wait.
    ///
    /// @return the remaining connect time, also bounded by the total
    [[nodiscard]] uint32_t lookup_wait() const {
        const auto now = std::chrono::steady_clock::now();
        uint32_t left = remaining(hop, timeouts.connect_ms, now);
        if (timeouts.total_ms > 0) {
            const uint32_t total_left = remaining(started, timeouts.total_ms, now);
            if (total_left < left)
                left = total_left;
        }
        return left;
    }
};

/// A failure from a clock.
///
/// @param hit which clock ran out
/// @param final_url the address being fetched
/// @return the outcome
Response failed_clock(Budget::Hit hit, std::string final_url) {
    Response response;
    response.final_url = std::move(final_url);
    if (hit == Budget::Hit::cancel) {
        response.failure = Failure::cancelled;
        response.detail = "the fetch was cancelled";
    } else {
        response.failure = Failure::timed_out;
        response.detail = "the time limit ran out";
    }
    return response;
}

/// The detail for a failure that is not a clock.
///
/// @param failure the failure
/// @return a short English phrase
const char* io_detail(Failure failure) {
    switch (failure) {
    case Failure::cancelled:
        return "the fetch was cancelled";
    case Failure::timed_out:
        return "the time limit ran out";
    case Failure::connection_lost:
        return "the connection closed early";
    case Failure::cannot_connect:
        return "the connection could not be opened";
    case Failure::too_large:
        return "the body is larger than the limit";
    case Failure::decode_failed:
        return "the body could not be decoded";
    case Failure::sink_stopped:
        return "the body was stopped";
    case Failure::certificate:
        return "the server's certificate was rejected";
    case Failure::https_needs_tls:
        return "This registry needs HTTPS, which this system cannot use";
    default:
        return "the fetch failed";
    }
}

/// Opens a connection inside the connect clock.
///
/// @param address the IPv4 address and port
/// @param budget the clocks
/// @param[out] stream the connection; invalid on failure
/// @param[out] failure why it did not open
/// @param[out] detail the English detail
/// @return true when the connection is open
bool open_connection(
    const sock::Address& address,
    Budget& budget,
    intptr_t& stream,
    Failure& failure,
    std::string& detail
) {
    char error[256] = {};
    stream = sock::stream_connect(address, error, sizeof error);
    if (stream == sock::invalid_socket) {
        failure = Failure::cannot_connect;
        detail = "the connection could not be opened";
        return false;
    }
    while (true) {
        const Budget::Hit hit = budget.hit(true);
        if (hit != Budget::Hit::ok) {
            sock::stream_close(&stream);
            failure = hit == Budget::Hit::cancel ? Failure::cancelled : Failure::timed_out;
            detail = io_detail(failure);
            return false;
        }
        const sock::StreamOpen opened = sock::stream_wait_open(stream, budget.slice(true));
        if (opened == sock::StreamOpen::open) {
            budget.touch();
            return true;
        }
        if (opened == sock::StreamOpen::failed) {
            sock::stream_close(&stream);
            failure = Failure::cannot_connect;
            detail = "the connection could not be opened";
            return false;
        }
    }
}

/// Writes every request byte, waiting at most one slice at a time.
///
/// @param stream the connection
/// @param bytes the request
/// @param budget the clocks
/// @param tls the TLS session, or null for plain HTTP
/// @param[out] detail a certificate or handshake failure's sentence
/// @return none when the request was written
Failure write_all(
    intptr_t stream,
    std::span<const uint8_t> bytes,
    Budget& budget,
    TlsSession* tls,
    std::string& detail
) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const Budget::Hit hit = budget.hit(false);
        if (hit != Budget::Hit::ok)
            return hit == Budget::Hit::cancel ? Failure::cancelled : Failure::timed_out;
        if (tls != nullptr) {
            const TlsIoResult result = tls_write(tls, bytes.data() + sent, bytes.size() - sent);
            if (result.kind == TlsIoResult::Kind::ready && result.count > 0) {
                sent += static_cast<std::size_t>(result.count);
                budget.touch();
                continue;
            }
            if (result.kind == TlsIoResult::Kind::failed ||
                result.kind == TlsIoResult::Kind::closed) {
                detail =
                    result.detail.empty() ? std::string(io_detail(result.failure)) : result.detail;
                return result.failure == Failure::none ? Failure::connection_lost : result.failure;
            }
            sock::StreamWaitEntry entry{};
            entry.stream = stream;
            entry.read = result.kind == TlsIoResult::Kind::want_read;
            entry.write = result.kind != TlsIoResult::Kind::want_read;
            const int ready = sock::stream_wait(&entry, 1, budget.slice(false));
            if (ready < 0 || entry.failed)
                return Failure::connection_lost;
            continue;
        }
        sock::StreamWaitEntry entry{};
        entry.stream = stream;
        entry.write = true;
        const int ready = sock::stream_wait(&entry, 1, budget.slice(false));
        if (ready < 0 || entry.failed)
            return Failure::connection_lost;
        if (!entry.writable)
            continue;
        const std::ptrdiff_t wrote =
            sock::stream_write(stream, bytes.data() + sent, bytes.size() - sent);
        if (wrote == sock::stream_failed)
            return Failure::connection_lost;
        if (wrote > 0) {
            sent += static_cast<std::size_t>(wrote);
            budget.touch();
        }
    }
    return Failure::none;
}

/// Finishes a TLS handshake on the connect clock. A rejected certificate is
/// returned as certificate and is not turned into HTTP.
///
/// @param stream the connection
/// @param tls the session
/// @param budget the clocks
/// @param[out] failure why the handshake stopped
/// @param[out] detail the sentence
/// @return true when the server's certificate was accepted
bool drive_handshake(
    intptr_t stream, TlsSession* tls, Budget& budget, Failure& failure, std::string& detail
) {
    while (true) {
        const Budget::Hit hit = budget.hit(true);
        if (hit != Budget::Hit::ok) {
            failure = hit == Budget::Hit::cancel ? Failure::cancelled : Failure::timed_out;
            detail = io_detail(failure);
            return false;
        }
        const TlsIoResult result = tls_handshake(tls);
        if (result.kind == TlsIoResult::Kind::ready) {
            budget.touch();
            return true;
        }
        if (result.kind == TlsIoResult::Kind::failed || result.kind == TlsIoResult::Kind::closed) {
            failure = result.failure == Failure::none ? Failure::connection_lost : result.failure;
            detail =
                result.detail.empty() ? std::string("the secure connection failed") : result.detail;
            return false;
        }
        sock::StreamWaitEntry entry{};
        entry.stream = stream;
        entry.read = result.kind != TlsIoResult::Kind::want_write;
        entry.write = result.kind == TlsIoResult::Kind::want_write;
        const int ready = sock::stream_wait(&entry, 1, budget.slice(true));
        if (ready < 0 || entry.failed) {
            failure = Failure::connection_lost;
            detail = "the secure connection failed";
            return false;
        }
    }
}

/// Where a redirect may go, and whether an https location was read as http.
struct RedirectChoice {
    bool ok = false;
    std::string detail;
    url::Url location;
    Method method = Method::get;
    std::vector<uint8_t> body;
    bool rewritten = false;
};

/// Decides whether a redirect is followed. An absolute https location is read
/// as http when TLS is off, with the same port rule as the first address.
///
/// @param request the caller's limits and allow-list
/// @param current the address of this hop
/// @param origin the address the fetch opened first
/// @param followed how many redirects have been followed
/// @param method the method on this hop
/// @param body the POST body on this hop
/// @param status the redirect status
/// @param location_text the Location value
/// @param tls_on true when https is fetched with TLS
/// @return the next hop, or a refusal
RedirectChoice decide_redirect(
    const Request& request,
    const url::Url& current,
    const url::Url& origin,
    uint32_t followed,
    Method method,
    const std::vector<uint8_t>& body,
    int status,
    std::string_view location_text,
    bool tls_on
) {
    RedirectChoice choice;
    if (followed >= request.limits.max_redirects) {
        choice.detail = "the redirect was refused";
        return choice;
    }
    if (method == Method::post && (status == 301 || status == 302)) {
        choice.detail = "a POST redirect was refused";
        return choice;
    }
    location_text = trim_ows(location_text);
    if (location_text.empty()) {
        choice.detail = "the redirect has no location";
        return choice;
    }
    url::UrlError url_error = url::UrlError::none;
    std::optional<url::Url> next;
    const bool absolute = location_text.find("://") != std::string_view::npos;
    if (absolute && !tls_on) {
        const std::optional<url::FetchAddress> decided =
            url::address_for_fetch(location_text, false, &url_error);
        if (!decided) {
            choice.detail = url::url_error_text(url_error);
            return choice;
        }
        next = decided->url;
        choice.rewritten = decided->rewritten_to_http;
    } else {
        next = url::resolve_reference(current, location_text, &url_error);
        if (!next) {
            choice.detail = url::url_error_text(url_error);
            return choice;
        }
    }
    if (next->scheme != url::Scheme::http && next->scheme != url::Scheme::https) {
        choice.detail = "the redirect is not an http or https address";
        return choice;
    }
    if (!tls_on && next->scheme == url::Scheme::https) {
        choice.detail = "the redirect is not plain HTTP";
        return choice;
    }
    if (!host_allowed(request.redirects, *next, origin)) {
        choice.detail = "the redirect names a host that is not allowed";
        return choice;
    }
    choice.ok = true;
    choice.location = *next;
    choice.method = status == 303 ? Method::get : method;
    if (status != 303)
        choice.body = body;
    return choice;
}

/// A rewritten https hop that never got an HTTP status is the system's way
/// of saying it cannot speak HTTPS and the host did not speak HTTP.
///
/// @param[in,out] response the hop's outcome
/// @param rewritten true when this hop was an https address read as http
void note_needs_https(Response& response, bool rewritten) {
    if (!rewritten || response.status != 0)
        return;
    if (response.failure != Failure::cannot_connect &&
        response.failure != Failure::connection_lost && response.failure != Failure::bad_response)
        return;
    response.failure = Failure::https_needs_tls;
    response.detail = failure_text(Failure::https_needs_tls);
}

/// What reading one response decided.
struct ReadEnd {
    bool retry = false;
    bool follow = false;
    bool reusable = false;
    bool rewritten = false;
    Response response;
    url::Url location;
    Method method = Method::get;
    std::vector<uint8_t> body;
};

/// Reads one response and either delivers it, follows a redirect, or fails.
///
/// @param stream the connection
/// @param budget the clocks
/// @param sink the body sink
/// @param request the caller's request
/// @param method the method on this hop
/// @param body the POST body on this hop
/// @param current the address of this hop
/// @param origin the address the fetch started with
/// @param followed how many redirects have been followed
/// @param final_url the text of this hop
/// @param allow_retry true when a dead pooled connection may be tried once more
/// @param tls the TLS session, or null for plain HTTP
/// @param tls_on true when an https redirect is fetched with TLS
/// @return what the hop decided
ReadEnd read_response(
    intptr_t stream,
    Budget& budget,
    BodySink& sink,
    const Request& request,
    Method method,
    const std::vector<uint8_t>& body,
    const url::Url& current,
    const url::Url& origin,
    uint32_t followed,
    const std::string& final_url,
    bool allow_retry,
    TlsSession* tls,
    bool tls_on
) {
    ReadEnd end;
    end.response.final_url = final_url;
    end.method = method;
    ResponseReader reader(request.limits, method, request.accept_gzip);
    GzipDecoder decoder;
    std::vector<uint8_t> stash;
    std::optional<ReadPiece> forced;
    bool delivering = false;
    bool gzip_body = false;
    bool saw_byte = false;
    bool pending_follow = false;
    bool next_rewritten = false;
    Method next_method = method;
    std::vector<uint8_t> next_body = body;
    std::optional<uint64_t> expected;

    const auto fill_head = [&]() {
        if (end.response.status == 0)
            end.response.status = reader.status();
        if (end.response.headers.empty())
            end.response.headers = reader.headers();
        if (!end.response.content_length)
            end.response.content_length = reader.content_length();
    };
    const auto fail = [&](Failure failure, std::string detail) {
        fill_head();
        end.response.failure = failure;
        end.response.detail = std::move(detail);
        if (failure == Failure::connection_lost && !saw_byte && allow_retry)
            end.retry = true;
        return true;
    };
    const auto give = [&](std::span<const uint8_t> bytes) {
        const uint64_t have = end.response.body_bytes;
        const uint64_t room =
            have >= request.limits.max_body_bytes ? 0 : request.limits.max_body_bytes - have;
        std::span<const uint8_t> piece = bytes;
        if (static_cast<uint64_t>(piece.size()) > room)
            piece = piece.first(static_cast<std::size_t>(room));
        if (!piece.empty()) {
            if (!sink.write(piece))
                return !fail(Failure::sink_stopped, "the body was stopped");
            end.response.body_bytes += static_cast<uint64_t>(piece.size());
            if (request.progress != nullptr) {
                Progress progress;
                progress.received = end.response.body_bytes;
                progress.expected = expected;
                request.progress(request.progress_context, progress);
            }
        }
        if (piece.size() != bytes.size())
            return !fail(Failure::too_large, "the body is larger than the limit");
        return true;
    };
    const auto decode = [&](std::span<const uint8_t> compressed, bool finishing) {
        std::vector<uint8_t> decoded;
        const Failure decoded_failure =
            finishing
                ? decoder.finish(decoded, end.response.body_bytes, request.limits.max_body_bytes)
                : decoder.push(
                      compressed, decoded, end.response.body_bytes, request.limits.max_body_bytes
                  );
        if (!give(decoded))
            return false;
        if (decoded_failure == Failure::none)
            return true;
        return !fail(decoded_failure, io_detail(decoded_failure));
    };

    while (true) {
        if (end.response.failure != Failure::none)
            return end;
        ReadPiece piece;
        if (forced.has_value()) {
            piece = *forced;
            forced.reset();
        } else {
            std::size_t taken = 0;
            piece = reader.feed(stash, taken);
            if (taken > stash.size())
                taken = stash.size();
            if (taken > 0)
                stash.erase(stash.begin(), stash.begin() + static_cast<std::ptrdiff_t>(taken));
        }

        if (piece.kind == ReadKind::need_more) {
            if (!stash.empty()) {
                fail(Failure::bad_response, "the response could not be read");
                return end;
            }
            if (request.cancel != nullptr && request.cancel->load()) {
                fail(Failure::cancelled, "the fetch was cancelled");
                return end;
            }
            if (budget.timeouts.total_ms > 0 && budget.hit(false) == Budget::Hit::total) {
                fail(Failure::timed_out, "the time limit ran out");
                return end;
            }
            uint8_t buffer[8192];
            std::ptrdiff_t got = 0;
            if (tls == nullptr) {
                got = sock::stream_read(stream, buffer, sizeof buffer);
            } else {
                while (true) {
                    const TlsIoResult result = tls_read(tls, buffer, sizeof buffer);
                    if (result.kind == TlsIoResult::Kind::ready && result.count > 0) {
                        got = result.count;
                        break;
                    }
                    if (result.kind == TlsIoResult::Kind::closed) {
                        got = sock::stream_ended;
                        break;
                    }
                    if (result.kind == TlsIoResult::Kind::failed) {
                        fail(
                            result.failure == Failure::none ? Failure::connection_lost
                                                            : result.failure,
                            result.detail.empty() ? std::string("the secure connection failed")
                                                  : result.detail
                        );
                        return end;
                    }
                    const Budget::Hit tls_hit = budget.hit(false);
                    if (tls_hit != Budget::Hit::ok) {
                        fail(
                            tls_hit == Budget::Hit::cancel ? Failure::cancelled
                                                           : Failure::timed_out,
                            io_detail(
                                tls_hit == Budget::Hit::cancel ? Failure::cancelled
                                                               : Failure::timed_out
                            )
                        );
                        return end;
                    }
                    sock::StreamWaitEntry tls_entry{};
                    tls_entry.stream = stream;
                    tls_entry.read = result.kind != TlsIoResult::Kind::want_write;
                    tls_entry.write = result.kind == TlsIoResult::Kind::want_write;
                    const int tls_ready = sock::stream_wait(&tls_entry, 1, budget.slice(false));
                    if (tls_ready < 0 || tls_entry.failed) {
                        got = sock::stream_failed;
                        break;
                    }
                }
            }
            if (got > 0) {
                saw_byte = true;
                budget.touch();
                stash.insert(stash.end(), buffer, buffer + got);
                continue;
            }
            if (got == sock::stream_ended || got == sock::stream_failed) {
                forced = reader.finish();
                continue;
            }
            const Budget::Hit hit = budget.hit(false);
            if (hit != Budget::Hit::ok) {
                fail(
                    hit == Budget::Hit::cancel ? Failure::cancelled : Failure::timed_out,
                    io_detail(hit == Budget::Hit::cancel ? Failure::cancelled : Failure::timed_out)
                );
                return end;
            }
            sock::StreamWaitEntry entry{};
            entry.stream = stream;
            entry.read = true;
            const int ready = sock::stream_wait(&entry, 1, budget.slice(false));
            if (ready < 0 || (entry.failed && !entry.readable)) {
                forced = reader.finish();
                continue;
            }
            continue;
        }

        if (piece.kind == ReadKind::interim)
            continue;

        if (piece.kind == ReadKind::head) {
            end.response.status = reader.status();
            end.response.headers = reader.headers();
            end.response.content_length = reader.content_length();
            if (redirect_status(end.response.status)) {
                const std::string* location = end.response.header("location");
                const RedirectChoice choice = decide_redirect(
                    request,
                    current,
                    origin,
                    followed,
                    method,
                    body,
                    end.response.status,
                    location == nullptr ? std::string_view{} : std::string_view(*location),
                    tls_on
                );
                if (!choice.ok) {
                    fail(Failure::redirect_refused, choice.detail);
                    return end;
                }
                pending_follow = true;
                end.location = choice.location;
                next_method = choice.method;
                next_body = choice.body;
                next_rewritten = choice.rewritten;
                continue;
            }
            if (request.range_from.has_value() && end.response.status == 206) {
                const std::string* header = end.response.header("content-range");
                const std::optional<ContentRange> range =
                    header == nullptr ? std::nullopt : parse_content_range(*header);
                const bool length_ok = !reader.content_length().has_value() || reader.chunked() ||
                                       reader.gzip() ||
                                       (range && range->last >= range->first &&
                                        range->last - range->first != UINT64_MAX &&
                                        *reader.content_length() == range->last - range->first + 1);
                if (!range || range->first != *request.range_from || !length_ok) {
                    fail(Failure::bad_response, "the content range does not match");
                    return end;
                }
                end.response.content_range = range;
            }
            if (!reader.gzip() && !reader.chunked() && reader.content_length())
                expected = reader.content_length();
            Response head = end.response;
            head.failure = Failure::none;
            head.body_bytes = 0;
            head.detail.clear();
            if (!sink.begin(head)) {
                fail(Failure::sink_stopped, "the body was stopped");
                return end;
            }
            delivering = true;
            gzip_body = reader.gzip();
            continue;
        }

        if (piece.kind == ReadKind::body) {
            if (delivering) {
                if (gzip_body) {
                    if (!decode(piece.bytes, false))
                        return end;
                } else if (!give(piece.bytes)) {
                    return end;
                }
            }
            continue;
        }

        if (piece.kind == ReadKind::finished) {
            if (delivering && gzip_body && !decode({}, true))
                return end;
            if (pending_follow) {
                end.follow = true;
                end.method = next_method;
                end.body = std::move(next_body);
                end.rewritten = next_rewritten;
                end.reusable = reader.reusable() && stash.empty();
                end.response.failure = Failure::none;
                return end;
            }
            end.reusable = reader.reusable() && stash.empty() && delivering;
            end.response.failure = Failure::none;
            end.response.detail.clear();
            return end;
        }

        fill_head();
        end.response.failure = reader.failure();
        end.response.detail =
            reader.detail().empty() ? io_detail(reader.failure()) : reader.detail();
        if (end.response.failure == Failure::connection_lost && !saw_byte && allow_retry)
            end.retry = true;
        return end;
    }
}

/// Copies one system-stack hop into the response the caller sees.
///
/// @param exchange the hop
/// @param final_url the address of this hop
/// @return the outcome
Response response_from_exchange(const HttpsExchange& exchange, const std::string& final_url) {
    Response response;
    response.failure = exchange.failure;
    if (exchange.detail.empty() && exchange.failure != Failure::none)
        response.detail = failure_text(exchange.failure);
    else
        response.detail = exchange.detail;
    response.status = exchange.status;
    response.headers = exchange.headers;
    response.final_url = final_url;
    response.content_length = exchange.content_length;
    response.content_range = exchange.content_range;
    response.body_bytes = exchange.body_bytes;
    return response;
}

/// Closes a connection unless it has already been released.
struct StreamGuard {
    intptr_t fd = sock::invalid_socket;
    TlsSession* tls = nullptr;

    StreamGuard() = default;

    ~StreamGuard() {
        if (tls != nullptr)
            tls_close(tls);
        if (fd != sock::invalid_socket)
            sock::stream_close(&fd);
    }

    StreamGuard(const StreamGuard&) = delete;
    StreamGuard& operator=(const StreamGuard&) = delete;
};

} // namespace

const char* failure_text(Failure failure) noexcept {
    switch (failure) {
    case Failure::none:
        return "ok";
    case Failure::bad_url:
        return "the address is not one OA can fetch";
    case Failure::https_unsupported:
        return "This system cannot use HTTPS";
    case Failure::certificate:
        return "the server's certificate was rejected";
    case Failure::https_needs_tls:
        return "This registry needs HTTPS, which this system cannot use";
    case Failure::bad_request:
        return "the request cannot be sent";
    case Failure::cannot_resolve:
        return "the name could not be resolved";
    case Failure::cannot_connect:
        return "the connection could not be opened";
    case Failure::timed_out:
        return "the time limit ran out";
    case Failure::cancelled:
        return "the fetch was cancelled";
    case Failure::connection_lost:
        return "the connection closed early";
    case Failure::bad_response:
        return "the response could not be read";
    case Failure::too_large:
        return "the response is larger than the limit";
    case Failure::redirect_refused:
        return "the redirect was refused";
    case Failure::sink_stopped:
        return "the body was stopped";
    case Failure::decode_failed:
        return "the body could not be decoded";
    }
    return "the fetch failed";
}

const std::string* Response::header(std::string_view name) const {
    for (const Header& header : headers) {
        if (same_name(header.name, name))
            return &header.value;
    }
    return nullptr;
}

MemorySink::MemorySink(uint64_t limit) : limit_(limit) {
}

bool MemorySink::begin(const Response&) {
    bytes.clear();
    return true;
}

bool MemorySink::write(std::span<const uint8_t> incoming) {
    if (static_cast<uint64_t>(bytes.size()) > limit_)
        return false;
    const uint64_t room = limit_ - static_cast<uint64_t>(bytes.size());
    if (static_cast<uint64_t>(incoming.size()) > room)
        return false;
    bytes.insert(bytes.end(), incoming.begin(), incoming.end());
    return true;
}

struct Client::State {
    struct Idle {
        std::string host;
        uint16_t port = 0;
        intptr_t stream = sock::invalid_socket;
        std::chrono::steady_clock::time_point since{};
    };

    std::vector<Idle> idle;
    uint32_t cap = 2;
    uint32_t keep_ms = 30000;

    /// Closes every pooled connection.
    void close_all() {
        for (Idle& item : idle) {
            if (item.stream != sock::invalid_socket)
                sock::stream_close(&item.stream);
        }
        idle.clear();
    }

    /// Closes connections that have sat idle too long.
    void reap() {
        const auto now = std::chrono::steady_clock::now();
        std::vector<Idle> kept;
        kept.reserve(idle.size());
        for (Idle& item : idle) {
            const int64_t age = elapsed_ms(item.since, now);
            if (age >= static_cast<int64_t>(keep_ms) || item.stream == sock::invalid_socket) {
                if (item.stream != sock::invalid_socket)
                    sock::stream_close(&item.stream);
                continue;
            }
            kept.push_back(std::move(item));
        }
        idle = std::move(kept);
    }

    /// Takes one idle connection for a host and port.
    ///
    /// @param host the host
    /// @param port the port
    /// @return the connection, or invalid_socket when none is waiting
    [[nodiscard]] intptr_t take(const std::string& host, uint16_t port) {
        reap();
        for (auto it = idle.begin(); it != idle.end(); ++it) {
            if (it->host == host && it->port == port) {
                const intptr_t stream = it->stream;
                it->stream = sock::invalid_socket;
                idle.erase(it);
                return stream;
            }
        }
        return sock::invalid_socket;
    }

    /// Puts a connection back, or closes it when the pool is full.
    ///
    /// @param host the host
    /// @param port the port
    /// @param[in,out] stream the connection; invalid afterwards
    void keep(std::string host, uint16_t port, intptr_t& stream) {
        reap();
        if (stream == sock::invalid_socket)
            return;
        if (cap == 0) {
            sock::stream_close(&stream);
            return;
        }
        uint32_t count = 0;
        for (const Idle& item : idle) {
            if (item.host == host && item.port == port)
                ++count;
        }
        if (count >= cap) {
            sock::stream_close(&stream);
            return;
        }
        Idle item;
        item.host = std::move(host);
        item.port = port;
        item.stream = stream;
        item.since = std::chrono::steady_clock::now();
        stream = sock::invalid_socket;
        idle.push_back(std::move(item));
    }

    ~State() { close_all(); }
};

Client::Client(ClientOptions options)
    : options_(std::move(options)), state_(std::make_unique<State>()) {
    state_->cap = options_.idle_connections_per_server;
    state_->keep_ms = options_.keep_alive_ms;
}

Client::~Client() = default;

void Client::close_idle() {
    state_->close_all();
}

Response Client::fetch(const Request& request, std::vector<uint8_t>& body) {
    body.clear();
    MemorySink sink(request.limits.max_body_bytes);
    Response response = fetch(request, sink);
    body = std::move(sink.bytes);
    return response;
}

Response Client::fetch(const Request& request, BodySink& sink) {
    state_->reap();
    // Rewrite https to http only before any connection, and only when TLS is
    // off. A failed handshake or a rejected certificate is never tried again
    // as HTTP.
    const bool tls_on = options_.tls.value_or(tls_available());
    url::UrlError url_error = url::UrlError::none;
    const std::optional<url::FetchAddress> decided =
        url::address_for_fetch(request.url, tls_on, &url_error);
    if (!decided) {
        Response response;
        response.failure = Failure::bad_url;
        response.detail = url::url_error_text(url_error);
        return response;
    }

    const std::string user_agent = options_.user_agent.empty()
                                       ? std::string("OpenAnnihilation/") + OA_ENGINE_VERSION
                                       : options_.user_agent;
    Budget budget;
    budget.timeouts = request.timeouts;
    budget.cancel = request.cancel;
    const url::Url origin = decided->url;
    url::Url current = origin;
    Method method = request.method;
    std::vector<uint8_t> body = request.body;
    uint32_t followed = 0;
    bool hop_rewritten = decided->rewritten_to_http;

    while (true) {
        budget.begin_hop();
        const std::string final_url = url::url_text(current);
        const Budget::Hit started = budget.hit(true);
        if (started != Budget::Hit::ok)
            return failed_clock(started, final_url);

        Request hop = request;
        hop.method = method;
        hop.body = body;
        const PreparedRequest prepared = prepare_request(hop, current, user_agent);
        if (prepared.failure != Failure::none) {
            Response response;
            response.failure = prepared.failure;
            response.detail = prepared.detail;
            response.final_url = final_url;
            return response;
        }

        const bool secure = current.scheme == url::Scheme::https;
        if (secure) {
            const HttpsTransport transport = https_transport();
            if (transport == HttpsTransport::none) {
                Response response;
                response.failure = Failure::https_unsupported;
                response.detail = failure_text(Failure::https_unsupported);
                response.final_url = final_url;
                return response;
            }
            if (transport == HttpsTransport::platform) {
                FetchClock clock;
                clock.started = budget.started;
                clock.hop = budget.hop;
                clock.timeouts = budget.timeouts;
                clock.cancel = budget.cancel;
                const HttpsExchange exchange =
                    fetch_https_hop(request, current, method, body, user_agent, sink, clock);
                if (exchange.failure == Failure::none && redirect_status(exchange.status)) {
                    const std::string* location = nullptr;
                    for (const Header& header : exchange.headers) {
                        if (same_name(header.name, "location")) {
                            location = &header.value;
                            break;
                        }
                    }
                    RedirectChoice choice = decide_redirect(
                        request,
                        current,
                        origin,
                        followed,
                        method,
                        body,
                        exchange.status,
                        location == nullptr ? std::string_view{} : std::string_view(*location),
                        tls_on
                    );
                    if (!choice.ok) {
                        Response response = response_from_exchange(exchange, final_url);
                        response.failure = Failure::redirect_refused;
                        response.detail = std::move(choice.detail);
                        return response;
                    }
                    current = std::move(choice.location);
                    method = choice.method;
                    body = std::move(choice.body);
                    hop_rewritten = choice.rewritten;
                    ++followed;
                    continue;
                }
                Response response = response_from_exchange(exchange, final_url);
                if (response.failure == Failure::certificate)
                    return response;
                note_needs_https(response, hop_rewritten);
                return response;
            }
        }

        const LookupResult looked =
            lookup_ipv4(current.host, options_.resolve, request.cancel, budget.lookup_wait());
        if (looked.failure != Failure::none) {
            Response response;
            response.failure = looked.failure;
            response.detail = looked.detail;
            response.final_url = final_url;
            return response;
        }
        const Budget::Hit after_lookup = budget.hit(true);
        if (after_lookup != Budget::Hit::ok)
            return failed_clock(after_lookup, final_url);

        sock::Address address{};
        std::memcpy(address.ip, looked.ip, sizeof address.ip);
        address.port = current.port;

        bool retried = false;
        while (true) {
            StreamGuard guard;
            bool pooled = false;
            // A TLS socket is never pooled. The pool key is host and port, so a
            // later plain fetch would be handed a session it cannot speak.
            if (!secure && !retried) {
                guard.fd = state_->take(current.host, current.port);
                pooled = guard.fd != sock::invalid_socket;
            }
            if (guard.fd == sock::invalid_socket) {
                Failure failure = Failure::none;
                std::string detail;
                if (!open_connection(address, budget, guard.fd, failure, detail)) {
                    Response response;
                    response.failure = failure;
                    response.detail = std::move(detail);
                    response.final_url = final_url;
                    note_needs_https(response, hop_rewritten && !secure);
                    return response;
                }
            }
            if (secure) {
                Failure opened = Failure::none;
                std::string opened_detail;
                guard.tls = tls_open(
                    guard.fd,
                    current.host,
                    url::is_ipv4_literal(current.host),
                    opened,
                    opened_detail
                );
                if (guard.tls == nullptr ||
                    !drive_handshake(guard.fd, guard.tls, budget, opened, opened_detail)) {
                    Response response;
                    response.failure = opened == Failure::none ? Failure::connection_lost : opened;
                    response.detail = opened_detail.empty()
                                          ? std::string(io_detail(response.failure))
                                          : std::move(opened_detail);
                    response.final_url = final_url;
                    return response;
                }
            }

            // Lookup, the TCP connect and the handshake are not idle time.
            // The idle clock starts as the request is written.
            budget.touch();
            std::string write_detail;
            const Failure wrote =
                write_all(guard.fd, prepared.bytes, budget, guard.tls, write_detail);
            if (wrote != Failure::none) {
                if (wrote == Failure::connection_lost && pooled && !retried &&
                    method != Method::post) {
                    retried = true;
                    continue;
                }
                Response response;
                response.failure = wrote;
                response.detail =
                    write_detail.empty() ? std::string(io_detail(wrote)) : std::move(write_detail);
                response.final_url = final_url;
                if (response.failure != Failure::certificate)
                    note_needs_https(response, hop_rewritten && !secure);
                return response;
            }

            ReadEnd end = read_response(
                guard.fd,
                budget,
                sink,
                request,
                method,
                body,
                current,
                origin,
                followed,
                final_url,
                pooled && !retried && method != Method::post && !secure,
                guard.tls,
                tls_on
            );
            if (end.retry) {
                retried = true;
                continue;
            }
            if (end.follow) {
                if (end.reusable && !secure)
                    state_->keep(current.host, current.port, guard.fd);
                current = std::move(end.location);
                method = end.method;
                body = std::move(end.body);
                hop_rewritten = end.rewritten;
                ++followed;
                break;
            }
            if (end.reusable && !secure)
                state_->keep(current.host, current.port, guard.fd);
            if (end.response.failure != Failure::certificate)
                note_needs_https(end.response, hop_rewritten && !secure);
            return end.response;
        }
    }
}

} // namespace oa::netgame::http
