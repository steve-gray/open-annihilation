// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A small HTTP/1.1 server for tests. It listens on 127.0.0.1 only, on a
// thread of its own, and can stall, cut or replace a response without
// holding up its other connections.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::netgame::http_fixture {

/// One HTTP header, as it arrived or as a response should send it.
struct Header {
    std::string name;  ///< the header name, without the colon
    std::string value; ///< the header value, without leading or trailing space
};

/// One request the server has accepted and logged.
struct LoggedRequest {
    uint64_t connection{};       ///< accepted connections, counting from 1
    std::string method;          ///< the request method, as it arrived
    std::string target;          ///< the whole target, query included
    std::string version;         ///< the request's HTTP version
    std::vector<Header> headers; ///< the request headers, in order
    std::vector<uint8_t> body;   ///< the request body

    /// Returns the value of the first header of this name.
    ///
    /// The match does not care about the case of the name.
    ///
    /// @param name the header name
    /// @return the value, or null when the request carries no such header
    [[nodiscard]] const std::string* header(std::string_view name) const;
};

/// The response to one request, and how to send it.
///
/// A delay here is a time at which bytes may leave. The server keeps serving
/// its other connections, and stop, while it waits.
struct Reply {
    int status{200};                        ///< the status code
    std::vector<Header> headers;            ///< headers sent ahead of the generated ones
    std::vector<uint8_t> body;              ///< the body, before gzip or chunking
    bool chunked{false};                    ///< send the body as chunked transfer coding
    std::size_t chunk_bytes{1024};          ///< the bytes of one chunk when chunked is set
    bool gzip{false};                       ///< compress the body and send Content-Encoding: gzip
    uint32_t head_delay_ms{0};              ///< milliseconds to wait before the status line
    std::size_t piece_bytes{0};             ///< body bytes between pauses; 0 sends the body at once
    uint32_t piece_delay_ms{0};             ///< milliseconds to wait between body pieces
    std::optional<std::size_t> cut_after{}; ///< close after this many body bytes
    bool close{false};                      ///< send Connection: close and close after the response
    bool no_length{false};                  ///< omit Content-Length and close to end the body
    std::optional<std::vector<uint8_t>> raw{}; ///< these bytes instead of a response, then close
};

/// Builds the reply for one logged request.
using Handler = std::function<Reply(const LoggedRequest&)>;

/// Serves HTTP/1.1 on 127.0.0.1 for tests and for the fixture tool.
///
/// Every method may be called from the test's thread while the server runs.
/// A handler runs on the server's thread and must not call stop.
class Server {
  public:

    /// Makes a server that is not listening.
    Server();

    /// Stops the server and joins its thread.
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Listens on 127.0.0.1 at `port` and starts the server thread.
    ///
    /// A port of 0 lets the system choose one. The server is stopped first
    /// when it was already running.
    ///
    /// @param port the port, in host order; 0 chooses one
    /// @param[out] error why it failed; null leaves the reason unwritten
    /// @return true when the server is listening
    [[nodiscard]] bool start(uint16_t port, std::string* error);

    /// Listens on 127.0.0.1 at a port the system chooses and starts the thread.
    ///
    /// @param[out] error why it failed; null leaves the reason unwritten
    /// @return true when the server is listening
    [[nodiscard]] bool start(std::string* error);

    /// Returns the port the server is listening on.
    ///
    /// @return the port, or 0 when the server has not started
    [[nodiscard]] uint16_t port() const;

    /// Builds the URL of a path on this server.
    ///
    /// @param path the path, including its leading slash when it has one
    /// @return `http://127.0.0.1:<port>` followed by `path`
    [[nodiscard]] std::string url(std::string_view path) const;

    /// Serves files from a folder at a URL prefix.
    ///
    /// A later call with the same prefix replaces the folder. GET and HEAD
    /// read a file; any other method is refused when the file is there.
    ///
    /// @param root the folder
    /// @param prefix the URL prefix; `/` serves the folder at the root
    void serve_folder(std::filesystem::path root, std::string prefix = "/");

    /// Serves one byte string at a path.
    ///
    /// A later call for the same path replaces the bytes. The path is matched
    /// without a query. GET and HEAD return the bytes; any other method is
    /// refused.
    ///
    /// @param path the URL path
    /// @param bytes the body
    /// @param content_type the Content-Type header
    void serve_bytes(
        std::string path,
        std::vector<uint8_t> bytes,
        std::string content_type = "application/octet-stream"
    );

    /// Handles one method and path with a function.
    ///
    /// A later call for the same method and path replaces the handler. The
    /// path is matched without a query.
    ///
    /// @param method the request method
    /// @param path the URL path
    /// @param handler the function; it runs on the server thread
    void handle(std::string method, std::string path, Handler handler);

    /// Answers a path with a redirect.
    ///
    /// A later call for the same path replaces the redirect. The path is
    /// matched without a query, for every method.
    ///
    /// @param from the URL path that redirects
    /// @param to the Location value
    /// @param status the status code, 302 unless another is given
    void redirect(std::string from, std::string to, int status = 302);

    /// Serves a fixed reply for the next requests to a path.
    ///
    /// The path is matched without a query, for every method. Each matching
    /// request uses one of `times`, and then the path is served as usual.
    ///
    /// @param path the URL path
    /// @param reply the reply to send
    /// @param times how many requests use the reply
    void override_next(std::string path, Reply reply, int times = 1);

    /// Turns single Range requests on or off for files and byte strings.
    ///
    /// @param honoured true to honour one `bytes` range; false to send the whole body
    void set_ranges(bool honoured);

    /// Sets how long a connection may sit idle before it is closed.
    ///
    /// The server starts at 5000 milliseconds.
    ///
    /// @param ms the idle time, in milliseconds
    void set_idle_close_ms(uint32_t ms);

    /// Returns a copy of the request log.
    ///
    /// @return every request accepted since the log was cleared, in order
    [[nodiscard]] std::vector<LoggedRequest> requests() const;

    /// Drops every request from the log.
    void clear_log();

    /// Returns how many connections the server has accepted.
    ///
    /// @return the count, from 0
    [[nodiscard]] uint64_t connections_accepted() const;

    /// Stops the server thread and closes its connections.
    ///
    /// Returns once the thread has finished. A delayed reply does not hold
    /// it: the wait is sliced, and this returns within a slice or two.
    void stop();

  private:

    struct State;
    std::unique_ptr<State> state_{};
};

} // namespace oa::netgame::http_fixture
