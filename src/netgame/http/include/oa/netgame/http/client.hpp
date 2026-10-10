// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A blocking HTTP/1.1 client for a worker thread. It fetches catalogues and
// packages over plain HTTP. One client is used by one thread; two clients on
// two threads share nothing. fetch is never called on the game's main thread.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::netgame::http {

/// Why a fetch did not read a whole response. `none` is a response read whole.
enum class Failure : uint8_t {
    none,              ///< a whole final response was read
    bad_url,           ///< the address could not be read
    https_unsupported, ///< the address is https, which a fetch does not use
    bad_request,       ///< the request is one the client will not send
    cannot_resolve,    ///< the name did not resolve to an IPv4 address
    cannot_connect,    ///< the connection could not be opened
    timed_out,         ///< a time limit ran out
    cancelled,         ///< the cancel flag was set
    connection_lost,   ///< the connection closed before the response ended
    bad_response,      ///< the response could not be read
    too_large,         ///< the response passed a size limit
    redirect_refused,  ///< a redirect was not followed
    sink_stopped,      ///< the body sink stopped the fetch
    decode_failed,     ///< a gzip body could not be decoded
};

/// The short description of a failure, for a log line.
///
/// @param failure the failure, or none
/// @return a stable English phrase; never null
[[nodiscard]] const char* failure_text(Failure failure) noexcept;

/// The method a fetch sends.
enum class Method : uint8_t {
    get,  ///< GET
    head, ///< HEAD, which has no body
    post, ///< POST, with the request body
};

/// One header, as the caller named it or as the response sent it.
struct Header {
    std::string name;  ///< the header name, without the colon
    std::string value; ///< the header value
};

/// How much of a response a fetch will read.
struct Limits {
    /// The most bytes of one response's status line and headers.
    std::size_t max_header_bytes = 64 * 1024;
    /// The most body bytes given to the sink, after decoding.
    uint64_t max_body_bytes = 64ull << 20;
    /// The most redirects one fetch follows.
    uint32_t max_redirects = 5;
};

/// How long a fetch waits. A zero total is no total limit.
struct Timeouts {
    /// Milliseconds for the name lookup and the connection together.
    uint32_t connect_ms = 10000;
    /// Milliseconds any wait for a byte to be sent or to arrive may take.
    uint32_t idle_ms = 30000;
    /// Milliseconds for the whole fetch, redirects included. Zero means none.
    uint32_t total_ms = 0;
};

/// The range a 206 response names, in bytes of the whole resource.
struct ContentRange {
    uint64_t first = 0;            ///< the first byte, counting from 0
    uint64_t last = 0;             ///< the last byte, counting from 0
    std::optional<uint64_t> total; ///< the whole resource, when the server named it
};

/// The outcome of one fetch.
///
/// `failure` is none when a final response was read whole. `status` is that
/// response's status, or 0 when none was read. A 200 answer to a range request
/// is the whole resource from byte 0, which the caller can tell from a 206.
struct Response {
    Failure failure = Failure::none; ///< none when a final response was read whole
    std::string detail;              ///< what went wrong, in English, for a log
    int status = 0;                  ///< the final response's status; 0 without one
    std::vector<Header> headers;     ///< the final response's headers, as sent
    std::string final_url;           ///< the address after redirects
    /// The Content-Length the response sent, before decoding.
    std::optional<uint64_t> content_length;
    /// The Content-Range a 206 sent. Empty for every other status.
    std::optional<ContentRange> content_range;
    /// Bytes given to the sink, after decoding.
    uint64_t body_bytes = 0;

    /// Returns the value of the first header of this name.
    ///
    /// The match does not care about the case of the name.
    ///
    /// @param name the header name
    /// @return the value, or null when the response carries no such header
    [[nodiscard]] const std::string* header(std::string_view name) const;
};

/// Receives the body of the final response, one piece at a time.
///
/// begin is called once, before any body byte. write is called for each piece
/// in order, after gzip decoding when the response was gzip. Either returning
/// false ends the fetch with sink_stopped. A redirect's body is not given here.
class BodySink {
  public:

    /// Releases the sink.
    virtual ~BodySink() = default;

    /// Accepts the final response's head, before any body byte.
    ///
    /// @param head the status, headers and address; body_bytes is still 0
    /// @return false to stop the fetch before the body
    virtual bool begin(const Response& head) = 0;

    /// Accepts the next piece of the decoded body.
    ///
    /// @param bytes the piece, in order
    /// @return false to stop the fetch
    virtual bool write(std::span<const uint8_t> bytes) = 0;
};

/// Keeps a response body in memory, up to the limit it was given.
class MemorySink final : public BodySink {
  public:

    /// Keeps at most `limit` bytes and then stops.
    ///
    /// @param limit the most bytes `bytes` will hold
    explicit MemorySink(uint64_t limit);

    /// Clears `bytes` and accepts the head.
    ///
    /// @param head the final response's head
    /// @return true
    bool begin(const Response& head) override;

    /// Appends a piece that still fits in the limit.
    ///
    /// @param incoming the piece
    /// @return false when the piece would pass the limit
    bool write(std::span<const uint8_t> incoming) override;

    /// The body received so far.
    std::vector<uint8_t> bytes;

  private:

    uint64_t limit_ = 0;
};

/// How many body bytes have been given to the sink, and how many are expected.
struct Progress {
    uint64_t received = 0;            ///< decoded bytes given to the sink so far
    std::optional<uint64_t> expected; ///< the decoded length, when it is known
};

/// Called after each piece is given to the sink.
///
/// A gzip body has no expected length. A null callback is not called.
using ProgressCallback = void (*)(void* context, const Progress& progress);

/// Which hosts a redirect may name, besides the address being fetched.
///
/// An entry is one host exactly, or a leading dot and a domain for that
/// domain and every name under it. A redirect to the request's own host and
/// port is allowed without an entry.
struct RedirectPolicy {
    /// Hosts a redirect may name. "name" is that host only; ".domain" is that
    /// domain and every name under it.
    std::vector<std::string> allowed_hosts;
};

/// One fetch. The client's own headers are sent ahead of `headers`.
struct Request {
    Method method = Method::get;        ///< GET, HEAD or POST
    std::string url;                    ///< an absolute http address
    std::vector<Header> headers;        ///< added after the client's own headers
    std::vector<uint8_t> body;          ///< the POST body; empty for GET and HEAD
    std::string content_type;           ///< the POST Content-Type; empty sends the generic type
    std::optional<uint64_t> range_from; ///< sends Range bytes from this byte to the end
    /// Sends Accept-Encoding gzip. Never set together with range_from.
    bool accept_gzip = false;
    RedirectPolicy redirects; ///< hosts a redirect may name
    Timeouts timeouts;        ///< the fetch's time limits
    Limits limits;            ///< the fetch's size limits
    /// Read at least every 50 milliseconds, including while a name is looked up.
    /// Null means the fetch cannot be cancelled.
    const std::atomic<bool>* cancel = nullptr;
    ProgressCallback progress = nullptr; ///< called after each body piece; null skips it
    void* progress_context = nullptr;    ///< passed back to progress
};

/// Resolves one host name to one IPv4 address.
///
/// @param host the host, as the address names it
/// @param ip the four address bytes, written on success
/// @return false when the name has no IPv4 address
using Resolver = bool (*)(const char* host, uint8_t ip[4]);

/// How a client names itself and how it keeps connections.
struct ClientOptions {
    /// The User-Agent header. Empty sends OpenAnnihilation and the engine version.
    std::string user_agent;
    /// How many idle connections one host and port may have waiting.
    uint32_t idle_connections_per_server = 2;
    /// Milliseconds an idle pooled connection may wait before it is closed.
    uint32_t keep_alive_ms = 30000;
    /// Resolves names. Null uses the sockets' own IPv4 resolver.
    Resolver resolve = nullptr;
};

/// Fetches one address at a time on the thread that calls it.
///
/// fetch blocks that thread until the response has been read, a limit has
/// been reached, or cancel has been noticed. It is never called on the game's
/// main thread. One client serves one thread at a time. Two clients on two
/// threads share nothing: each has its own connections.
class Client {
  public:

    /// Makes a client with the given options.
    ///
    /// @param options the user agent, the idle pool and the resolver
    explicit Client(ClientOptions options = {});

    /// Closes every pooled connection.
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    /// Fetches the address and gives the body to the sink.
    ///
    /// Blocks the calling thread. The cancel flag is read at least every 50
    /// milliseconds, including while a name is looked up. A GET or HEAD whose
    /// reused connection fails before the first response byte is sent once
    /// more on a new connection. A POST is never sent twice.
    ///
    /// @param request the address, method and limits
    /// @param sink receives the final response only
    /// @return the outcome; failure is none when the final response was read whole
    [[nodiscard]] Response fetch(const Request& request, BodySink& sink);

    /// Fetches the address and keeps the body in `body`.
    ///
    /// The same rules as the fetch that takes a sink. `body` is cleared first
    /// and then holds what the sink was given, up to the request's body limit.
    ///
    /// @param request the address, method and limits
    /// @param[out] body the decoded body; cleared first
    /// @return the outcome; failure is none when the final response was read whole
    [[nodiscard]] Response fetch(const Request& request, std::vector<uint8_t>& body);

    /// Closes every pooled connection.
    void close_idle();

  private:

    struct State;
    ClientOptions options_{};
    std::unique_ptr<State> state_{};
};

} // namespace oa::netgame::http
