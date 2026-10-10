// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Fetches one address at a time: the pool, the time limits and the body sink.

#include "oa/netgame/http/client.hpp"

#include "gzip.hpp"
#include "lookup.hpp"
#include "response_reader.hpp"
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
    if (target.host == origin.host && target.port == origin.port)
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
/// @return none when the request was written
Failure write_all(intptr_t stream, std::span<const uint8_t> bytes, Budget& budget) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const Budget::Hit hit = budget.hit(false);
        if (hit != Budget::Hit::ok)
            return hit == Budget::Hit::cancel ? Failure::cancelled : Failure::timed_out;
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

/// What reading one response decided.
struct ReadEnd {
    bool retry = false;
    bool follow = false;
    bool reusable = false;
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
    bool allow_retry
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
            const std::ptrdiff_t got = sock::stream_read(stream, buffer, sizeof buffer);
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
                if (followed >= request.limits.max_redirects) {
                    fail(Failure::redirect_refused, "the redirect was refused");
                    return end;
                }
                if (method == Method::post &&
                    (end.response.status == 301 || end.response.status == 302)) {
                    fail(Failure::redirect_refused, "a POST redirect was refused");
                    return end;
                }
                const std::string* location = end.response.header("location");
                if (location == nullptr) {
                    fail(Failure::redirect_refused, "the redirect has no location");
                    return end;
                }
                url::UrlError url_error = url::UrlError::none;
                const std::optional<url::Url> next =
                    url::resolve_reference(current, trim_ows(*location), &url_error);
                if (!next || next->scheme != url::Scheme::http) {
                    fail(
                        Failure::redirect_refused,
                        next ? "the redirect is not plain HTTP" : url::url_error_text(url_error)
                    );
                    return end;
                }
                if (!host_allowed(request.redirects, *next, origin)) {
                    fail(
                        Failure::redirect_refused, "the redirect names a host that is not allowed"
                    );
                    return end;
                }
                pending_follow = true;
                end.location = *next;
                next_method = end.response.status == 303 ? Method::get : method;
                if (end.response.status == 303)
                    next_body.clear();
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

/// Closes a connection unless it has already been released.
struct StreamGuard {
    intptr_t fd = sock::invalid_socket;

    StreamGuard() = default;

    ~StreamGuard() {
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
        return "OA fetches over plain HTTP";
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
    url::UrlError url_error = url::UrlError::none;
    const std::optional<url::Url> parsed = url::parse_http_url(request.url, &url_error);
    if (!parsed) {
        Response response;
        if (url_error == url::UrlError::https_not_fetched) {
            response.failure = Failure::https_unsupported;
            response.detail = url::url_error_text(url_error);
        } else {
            response.failure = Failure::bad_url;
            response.detail = url::url_error_text(url_error);
        }
        return response;
    }

    const std::string user_agent = options_.user_agent.empty()
                                       ? std::string("OpenAnnihilation/") + OA_ENGINE_VERSION
                                       : options_.user_agent;
    Budget budget;
    budget.timeouts = request.timeouts;
    budget.cancel = request.cancel;
    const url::Url origin = *parsed;
    url::Url current = origin;
    Method method = request.method;
    std::vector<uint8_t> body = request.body;
    uint32_t followed = 0;

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
            if (!retried) {
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
                    return response;
                }
            }

            // Lookup and the TCP connect are not idle time. The idle clock
            // starts as the request is written.
            budget.touch();
            const Failure wrote = write_all(guard.fd, prepared.bytes, budget);
            if (wrote != Failure::none) {
                if (wrote == Failure::connection_lost && pooled && !retried &&
                    method != Method::post) {
                    retried = true;
                    continue;
                }
                Response response;
                response.failure = wrote;
                response.detail = io_detail(wrote);
                response.final_url = final_url;
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
                pooled && !retried && method != Method::post
            );
            if (end.retry) {
                retried = true;
                continue;
            }
            if (end.follow) {
                if (end.reusable)
                    state_->keep(current.host, current.port, guard.fd);
                current = std::move(end.location);
                method = end.method;
                body = std::move(end.body);
                ++followed;
                break;
            }
            if (end.reusable)
                state_->keep(current.host, current.port, guard.fd);
            return end.response;
        }
    }
}

} // namespace oa::netgame::http
