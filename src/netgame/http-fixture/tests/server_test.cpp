// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Drives the HTTP fixture on 127.0.0.1 and compares the bytes on the
// connection. Every wait blocks; the test thread is the only client.

#include "oa/netgame/http_fixture/server.hpp"
#include "oa/netgame/stream_socket.hpp"

#include <zlib.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace http = oa::netgame::http_fixture;
namespace sock = oa::netgame::sock;

namespace {

int failures = 0;
const char* current_test = "";

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ++failures;                                                                            \
            std::fprintf(stderr, "%s: %s:%d: %s\n", current_test, __FILE__, __LINE__, #condition); \
        }                                                                                          \
    } while (0)

constexpr uint32_t open_limit_ms = 5000;
constexpr uint32_t io_limit_ms = 3000;
constexpr uint8_t loopback_ip[4] = {127, 0, 0, 1};

/// A socket closed when the test leaves it.
struct Socket {
    intptr_t fd{sock::invalid_socket};

    Socket() = default;

    explicit Socket(intptr_t opened) : fd(opened) {}

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd(other.fd) { other.fd = sock::invalid_socket; }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            sock::stream_close(&fd);
            fd = other.fd;
            other.fd = sock::invalid_socket;
        }
        return *this;
    }

    ~Socket() { sock::stream_close(&fd); }
};

/// A folder removed when the test leaves it.
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static int serial = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("oa-http-fixture-" + std::to_string(ticks) + "-" + std::to_string(++serial));
        std::filesystem::create_directories(path);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

/// Returns how many milliseconds have passed since start.
///
/// @param start when the wait began
/// @return the elapsed milliseconds
int64_t milliseconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start
    )
        .count();
}

/// Builds an IPv4 address on 127.0.0.1.
///
/// @param port the port, in host order
/// @return the address
sock::Address loopback(uint16_t port) {
    sock::Address address{};
    std::memcpy(address.ip, loopback_ip, 4);
    address.port = port;
    return address;
}

/// Copies text into a byte buffer.
///
/// @param text the text
/// @return the bytes
std::vector<uint8_t> bytes_of(std::string_view text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

/// Prints a short form of a buffer after a mismatch.
///
/// @param bytes the buffer
void print_bytes(const std::vector<uint8_t>& bytes) {
    const std::size_t shown = bytes.size() < 240 ? bytes.size() : 240;
    for (std::size_t i = 0; i < shown; ++i) {
        const uint8_t byte = bytes[i];
        if (byte >= 32 && byte < 127 && byte != '\\')
            std::fputc(static_cast<int>(byte), stderr);
        else
            std::fprintf(stderr, "\\x%02x", byte);
    }
    if (bytes.size() > shown)
        std::fputs("...", stderr);
    std::fputc('\n', stderr);
}

/// Fails when two buffers differ.
///
/// @param got the bytes read
/// @param expected the bytes the server should have sent
void check_bytes(const std::vector<uint8_t>& got, std::string_view expected) {
    const std::vector<uint8_t> want = bytes_of(expected);
    if (got == want)
        return;
    ++failures;
    std::fprintf(
        stderr, "%s: bytes differ, got %zu, want %zu\n", current_test, got.size(), want.size()
    );
    std::fputs("got:  ", stderr);
    print_bytes(got);
    std::fputs("want: ", stderr);
    print_bytes(want);
}

/// Opens a connection to the server and waits until it has opened.
///
/// @param port the server's port
/// @return the connection, invalid when it did not open
Socket connect_open(uint16_t port) {
    char error[160]{};
    Socket outbound(sock::stream_connect(loopback(port), error, sizeof error));
    CHECK(outbound.fd != sock::invalid_socket);
    if (outbound.fd == sock::invalid_socket)
        return outbound;
    CHECK(sock::stream_wait_open(outbound.fd, open_limit_ms) == sock::StreamOpen::open);
    return outbound;
}

/// Writes every byte, waiting when the system takes nothing.
///
/// @param stream the connection
/// @param bytes the bytes
/// @param size how many
/// @return false when the write fails or the time runs out
bool write_all(intptr_t stream, const uint8_t* bytes, std::size_t size) {
    const auto start = std::chrono::steady_clock::now();
    std::size_t sent = 0;
    while (sent < size) {
        if (milliseconds_since(start) > io_limit_ms)
            return false;
        const std::ptrdiff_t wrote = sock::stream_write(stream, bytes + sent, size - sent);
        if (wrote == sock::stream_failed)
            return false;
        if (wrote > 0) {
            sent += static_cast<std::size_t>(wrote);
            continue;
        }
        sock::StreamWaitEntry wait{};
        wait.stream = stream;
        wait.write = true;
        if (sock::stream_wait(&wait, 1, 50) < 0)
            return false;
    }
    return true;
}

/// Writes a request.
///
/// @param stream the connection
/// @param text the request
/// @return false when the write fails
bool write_text(intptr_t stream, std::string_view text) {
    return write_all(stream, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

/// What one read of a single byte found.
enum class OneByte : uint8_t { got, ended, failed, timeout };

/// Reads one byte, or reports why it could not.
///
/// @param stream the connection
/// @param[out] byte the byte, when one arrived
/// @param limit_ms how long to wait
/// @return what happened
OneByte read_one(intptr_t stream, uint8_t& byte, uint32_t limit_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (milliseconds_since(start) <= static_cast<int64_t>(limit_ms)) {
        const std::ptrdiff_t arrived = sock::stream_read(stream, &byte, 1);
        if (arrived == 1)
            return OneByte::got;
        if (arrived == sock::stream_ended)
            return OneByte::ended;
        if (arrived == sock::stream_failed)
            return OneByte::failed;
        const auto used = milliseconds_since(start);
        if (used > static_cast<int64_t>(limit_ms))
            break;
        const auto left = static_cast<uint32_t>(limit_ms - static_cast<uint32_t>(used));
        sock::StreamWaitEntry wait{};
        wait.stream = stream;
        wait.read = true;
        const int ready = sock::stream_wait(&wait, 1, left > 50 ? 50 : left);
        if (ready < 0)
            return OneByte::failed;
    }
    return OneByte::timeout;
}

/// Reads an exact number of bytes.
///
/// @param stream the connection
/// @param[out] out the bytes
/// @param size how many
/// @param limit_ms how long to wait
/// @return false when the connection ends early or the time runs out
bool read_exact(intptr_t stream, std::vector<uint8_t>& out, std::size_t size, uint32_t limit_ms) {
    const auto start = std::chrono::steady_clock::now();
    out.clear();
    while (out.size() < size) {
        const auto used = milliseconds_since(start);
        if (used > static_cast<int64_t>(limit_ms))
            return false;
        uint8_t byte = 0;
        const auto left = static_cast<uint32_t>(limit_ms - static_cast<uint32_t>(used));
        const OneByte one = read_one(stream, byte, left);
        if (one != OneByte::got)
            return false;
        out.push_back(byte);
    }
    return true;
}

/// Reads until the server closes the connection.
///
/// @param stream the connection
/// @param[out] out the bytes
/// @param limit_ms how long to wait
/// @return false when the read fails or the time runs out
bool read_until_end(intptr_t stream, std::vector<uint8_t>& out, uint32_t limit_ms) {
    const auto start = std::chrono::steady_clock::now();
    out.clear();
    while (milliseconds_since(start) <= static_cast<int64_t>(limit_ms)) {
        const auto used = milliseconds_since(start);
        const auto left = static_cast<uint32_t>(limit_ms - static_cast<uint32_t>(used));
        uint8_t byte = 0;
        const OneByte one = read_one(stream, byte, left > 0 ? left : 0);
        if (one == OneByte::got) {
            out.push_back(byte);
            continue;
        }
        return one == OneByte::ended;
    }
    return false;
}

/// Starts the server on a port the system chooses.
///
/// @param server the server
/// @return false when it does not listen
bool listen(http::Server& server) {
    std::string error;
    const bool ok = server.start(&error);
    CHECK(ok);
    CHECK(server.port() != 0);
    if (!ok)
        std::fprintf(stderr, "%s: %s\n", current_test, error.c_str());
    return ok;
}

/// Reads one kept-alive response and compares it.
///
/// @param server the server
/// @param request the request
/// @param expected the whole response
void expect_open(http::Server& server, std::string_view request, std::string_view expected) {
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    CHECK(write_text(socket.fd, request));
    std::vector<uint8_t> got;
    CHECK(read_exact(socket.fd, got, expected.size(), io_limit_ms));
    check_bytes(got, expected);
}

/// Reads until the connection closes and compares the buffer.
///
/// @param server the server
/// @param request the request
/// @param expected the whole response, including a cut body
void expect_closed(http::Server& server, std::string_view request, std::string_view expected) {
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    CHECK(write_text(socket.fd, request));
    std::vector<uint8_t> got;
    CHECK(read_until_end(socket.fd, got, io_limit_ms));
    check_bytes(got, expected);
}

/// Writes a file under a folder.
///
/// @param path the file
/// @param text the bytes
/// @return false when the file cannot be written
bool write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

/// Returns the value of one response header.
///
/// @param response the response, headers and body
/// @param name the header name
/// @param[out] value the value
/// @return false when the header is absent
bool response_header(std::string_view response, std::string_view name, std::string& value) {
    const std::size_t end = response.find("\r\n\r\n");
    if (end == std::string_view::npos)
        return false;
    const std::string needle = std::string(name) + ": ";
    std::size_t at = 0;
    while (at < end) {
        const std::size_t next = response.find("\r\n", at);
        const std::size_t line_end = next == std::string_view::npos ? end : next;
        const std::string_view line = response.substr(at, line_end - at);
        if (line.size() >= needle.size()) {
            bool match = true;
            for (std::size_t i = 0; i < needle.size(); ++i) {
                const char left = line[i] >= 'A' && line[i] <= 'Z'
                                      ? static_cast<char>(line[i] - 'A' + 'a')
                                      : line[i];
                const char right = needle[i] >= 'A' && needle[i] <= 'Z'
                                       ? static_cast<char>(needle[i] - 'A' + 'a')
                                       : needle[i];
                if (left != right) {
                    match = false;
                    break;
                }
            }
            if (match) {
                value.assign(line.substr(needle.size()));
                return true;
            }
        }
        if (next == std::string_view::npos)
            break;
        at = next + 2;
    }
    return false;
}

/// Inflates a gzip body.
///
/// @param compressed the gzip bytes
/// @param[out] plain the inflated bytes
/// @return false when the bytes are not gzip
bool inflate_gzip(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& plain) {
    plain.clear();
    if (compressed.empty())
        return false;
    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK)
        return false;
    stream.next_in = const_cast<Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    uint8_t buffer[256];
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = buffer;
        stream.avail_out = sizeof buffer;
        result = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = sizeof buffer - stream.avail_out;
        plain.insert(plain.end(), buffer, buffer + produced);
    }
    inflateEnd(&stream);
    return result == Z_STREAM_END;
}

/// A file and a byte string answer with their length and type.
void files_and_byte_strings() {
    current_test = "files";
    TempDir temp;
    CHECK(write_file(temp.path / "a.txt", "abcdefgh"));
    CHECK(write_file(temp.path / "note.json", "{}"));
    CHECK(write_file(temp.path / "pic.PNG", "png"));
    CHECK(write_file(temp.path / "mark.sig", "sig"));
    CHECK(write_file(temp.path / "pack.yaml", "yaml"));
    CHECK(write_file(temp.path / "list.oareg", "reg"));
    CHECK(write_file(temp.path / "blob.bin", "bin"));
    http::Server server;
    if (!listen(server))
        return;
    server.serve_folder(temp.path);
    server.serve_bytes("/mem", bytes_of("xyz"));
    server.serve_bytes("/typed", bytes_of("ab"), "text/plain");
    expect_open(
        server,
        "GET /a.txt HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\nabcdefgh"
    );
    expect_open(
        server,
        "GET /note.json HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}"
    );
    expect_open(
        server,
        "GET /pic.PNG HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 3\r\n\r\npng"
    );
    expect_open(
        server,
        "GET /mark.sig HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\n\r\nsig"
    );
    expect_open(
        server,
        "GET /pack.yaml HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: application/yaml\r\nContent-Length: 4\r\n\r\nyaml"
    );
    expect_open(
        server,
        "GET /list.oareg HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: application/yaml\r\nContent-Length: 3\r\n\r\nreg"
    );
    expect_open(
        server,
        "GET /blob.bin HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: 3\r\n\r\nbin"
    );
    expect_open(
        server,
        "GET /mem HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: 3\r\n\r\nxyz"
    );
    expect_open(
        server,
        "GET /typed HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nab"
    );
    CHECK(server.url("/a.txt") == "http://127.0.0.1:" + std::to_string(server.port()) + "/a.txt");
}

/// A missing file is 404, and the wrong method on a file that is there is 405.
void missing_and_method() {
    current_test = "status";
    TempDir temp;
    CHECK(write_file(temp.path / "a.txt", "abcdefgh"));
    http::Server server;
    if (!listen(server))
        return;
    server.serve_folder(temp.path);
    server.serve_bytes("/mem", bytes_of("xyz"));
    expect_open(
        server,
        "GET /missing HTTP/1.1\r\n\r\n",
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
    );
    expect_open(
        server,
        "POST /a.txt HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nContent-Length: 0\r\n\r\n"
    );
    expect_open(
        server,
        "PUT /mem HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nContent-Length: 0\r\n\r\n"
    );
    expect_open(
        server,
        "POST /missing HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
    );
    expect_closed(
        server,
        "GET /\r\n\r\n",
        "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
}

/// HEAD carries the headers of the body it does not send.
void head_has_headers_and_no_body() {
    current_test = "head";
    TempDir temp;
    CHECK(write_file(temp.path / "a.txt", "abcdefgh"));
    http::Server server;
    if (!listen(server))
        return;
    server.serve_folder(temp.path);
    http::Reply paced;
    paced.body = bytes_of("abcdefgh");
    paced.chunked = true;
    paced.chunk_bytes = 4;
    server.override_next("/chunked", paced);
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    const std::string_view head = "HEAD /a.txt HTTP/1.1\r\n\r\n";
    const std::string_view head_response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\n";
    const std::string_view get = "GET /a.txt HTTP/1.1\r\n\r\n";
    const std::string_view get_response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\nabcdefgh";
    CHECK(write_text(socket.fd, head));
    std::vector<uint8_t> got;
    CHECK(read_exact(socket.fd, got, head_response.size(), io_limit_ms));
    check_bytes(got, head_response);
    CHECK(write_text(socket.fd, get));
    CHECK(read_exact(socket.fd, got, get_response.size(), io_limit_ms));
    check_bytes(got, get_response);

    Socket chunked = connect_open(server.port());
    if (chunked.fd == sock::invalid_socket)
        return;
    const std::string_view chunked_head = "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\n";
    CHECK(write_text(chunked.fd, "HEAD /chunked HTTP/1.1\r\n\r\n"));
    CHECK(read_exact(chunked.fd, got, chunked_head.size(), io_limit_ms));
    check_bytes(got, chunked_head);
    CHECK(write_text(chunked.fd, "GET /a.txt HTTP/1.1\r\n\r\n"));
    CHECK(read_exact(chunked.fd, got, get_response.size(), io_limit_ms));
    check_bytes(got, get_response);
    CHECK(server.connections_accepted() == 2);
}

/// Two requests share a connection, and Connection: close ends it.
void keep_alive_and_close() {
    current_test = "keep-alive";
    http::Server server;
    if (!listen(server))
        return;
    server.serve_bytes("/mem", bytes_of("xyz"), "text/plain");
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    const std::string_view response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\n\r\nxyz";
    std::vector<uint8_t> got;
    CHECK(write_text(socket.fd, "GET /mem HTTP/1.1\r\n\r\n"));
    CHECK(read_exact(socket.fd, got, response.size(), io_limit_ms));
    check_bytes(got, response);
    CHECK(write_text(socket.fd, "GET /mem HTTP/1.1\r\n\r\n"));
    CHECK(read_exact(socket.fd, got, response.size(), io_limit_ms));
    check_bytes(got, response);
    CHECK(server.connections_accepted() == 1);
    CHECK(server.requests().size() == 2);
    CHECK(server.requests()[0].connection == 1);
    CHECK(server.requests()[1].connection == 1);

    expect_closed(
        server,
        "GET /mem HTTP/1.1\r\nConnection: close\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: "
        "close\r\n\r\nxyz"
    );
    expect_closed(
        server,
        "GET /mem HTTP/1.0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: "
        "close\r\n\r\nxyz"
    );

    Socket kept = connect_open(server.port());
    if (kept.fd == sock::invalid_socket)
        return;
    const std::string_view kept_response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: "
        "keep-alive\r\n\r\nxyz";
    CHECK(write_text(kept.fd, "GET /mem HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
    CHECK(read_exact(kept.fd, got, kept_response.size(), io_limit_ms));
    check_bytes(got, kept_response);
    CHECK(write_text(kept.fd, "GET /mem HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
    CHECK(read_exact(kept.fd, got, kept_response.size(), io_limit_ms));
    check_bytes(got, kept_response);
}

/// One range is honoured, and a bad or disabled range is not.
void ranges() {
    current_test = "range";
    TempDir temp;
    CHECK(write_file(temp.path / "a.txt", "abcdefgh"));
    http::Server server;
    if (!listen(server))
        return;
    server.serve_folder(temp.path);
    server.serve_bytes("/mem", bytes_of("abcdefgh"), "text/plain");
    expect_open(
        server,
        "GET /a.txt HTTP/1.1\r\nRange: bytes=2-4\r\n\r\n",
        "HTTP/1.1 206 Partial Content\r\nContent-Type: text/plain\r\n"
        "Content-Range: bytes 2-4/8\r\nContent-Length: 3\r\n\r\ncde"
    );
    expect_open(
        server,
        "GET /a.txt HTTP/1.1\r\nRange: bytes=2-\r\n\r\n",
        "HTTP/1.1 206 Partial Content\r\nContent-Type: text/plain\r\n"
        "Content-Range: bytes 2-7/8\r\nContent-Length: 6\r\n\r\ncdefgh"
    );
    expect_open(
        server,
        "GET /mem HTTP/1.1\r\nRange: bytes=8-\r\n\r\n",
        "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Type: text/plain\r\n"
        "Content-Range: bytes */8\r\nContent-Length: 0\r\n\r\n"
    );
    expect_open(
        server,
        "GET /a.txt HTTP/1.1\r\nRange: bytes=100-200\r\n\r\n",
        "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Type: text/plain\r\n"
        "Content-Range: bytes */8\r\nContent-Length: 0\r\n\r\n"
    );
    const std::string_view whole =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\nabcdefgh";
    expect_open(server, "GET /a.txt HTTP/1.1\r\nRange: bytes=2-4,5-6\r\n\r\n", whole);
    expect_open(server, "GET /a.txt HTTP/1.1\r\nRange: bytes=-3\r\n\r\n", whole);
    expect_open(server, "GET /a.txt HTTP/1.1\r\nRange: bytes=4-2\r\n\r\n", whole);
    expect_open(server, "GET /a.txt HTTP/1.1\r\nRange: items=0-1\r\n\r\n", whole);
    server.set_ranges(false);
    expect_open(server, "GET /a.txt HTTP/1.1\r\nRange: bytes=2-4\r\n\r\n", whole);
}

/// Chunked framing uses hex sizes and a last chunk of zero.
void chunked_framing() {
    current_test = "chunked";
    http::Server server;
    if (!listen(server))
        return;
    http::Reply reply;
    reply.body = bytes_of("abcdefghij");
    reply.chunked = true;
    reply.chunk_bytes = 4;
    reply.close = true;
    server.override_next("/c", reply);
    expect_closed(
        server,
        "GET /c HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        "4\r\nabcd\r\n4\r\nefgh\r\n2\r\nij\r\n0\r\n\r\n"
    );

    http::Reply tiny;
    tiny.body = bytes_of("ab");
    tiny.chunked = true;
    tiny.chunk_bytes = 0;
    tiny.close = true;
    server.override_next("/z", tiny);
    expect_closed(
        server,
        "GET /z HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
        "1\r\na\r\n1\r\nb\r\n0\r\n\r\n"
    );
}

/// Gzip bytes inflate back to the body that was registered.
void gzip_inflates() {
    current_test = "gzip";
    http::Server server;
    if (!listen(server))
        return;
    const std::string_view plain = "hello gzip body!!";
    http::Reply reply;
    reply.body = bytes_of(plain);
    reply.gzip = true;
    server.override_next("/g", reply);
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    CHECK(write_text(socket.fd, "GET /g HTTP/1.1\r\n\r\n"));
    std::vector<uint8_t> header;
    bool saw_blank = false;
    while (!saw_blank && header.size() < 2048) {
        uint8_t byte = 0;
        CHECK(read_one(socket.fd, byte, io_limit_ms) == OneByte::got);
        header.push_back(byte);
        if (header.size() >= 4 &&
            std::memcmp(header.data() + header.size() - 4, "\r\n\r\n", 4) == 0)
            saw_blank = true;
    }
    CHECK(saw_blank);
    const std::string text(header.begin(), header.end());
    std::string encoding;
    std::string length_text;
    CHECK(response_header(text, "Content-Encoding", encoding));
    CHECK(encoding == "gzip");
    CHECK(response_header(text, "Content-Length", length_text));
    const std::size_t length =
        static_cast<std::size_t>(std::strtoul(length_text.c_str(), nullptr, 10));
    std::vector<uint8_t> compressed;
    CHECK(read_exact(socket.fd, compressed, length, io_limit_ms));
    std::vector<uint8_t> inflated;
    CHECK(inflate_gzip(compressed, inflated));
    check_bytes(inflated, plain);

    http::Reply head_reply;
    head_reply.body = bytes_of(plain);
    head_reply.gzip = true;
    head_reply.chunked = true;
    server.override_next("/g", head_reply);
    Socket head = connect_open(server.port());
    if (head.fd == sock::invalid_socket)
        return;
    CHECK(write_text(head.fd, "HEAD /g HTTP/1.1\r\n\r\n"));
    std::vector<uint8_t> head_bytes;
    CHECK(read_exact(head.fd, head_bytes, text.size(), io_limit_ms));
    const std::string head_text(head_bytes.begin(), head_bytes.end());
    CHECK(head_text == text);
    CHECK(head_text.find("Transfer-Encoding") == std::string::npos);
}

/// A redirect sends Location, and a handler sees the POST body.
void redirect_and_handler() {
    current_test = "handler";
    http::Server server;
    if (!listen(server))
        return;
    server.redirect("/old", "/new");
    server.redirect("/away", "/other", 301);
    std::atomic<int> seen_connection{-1};
    server.handle("POST", "/echo", [&](const http::LoggedRequest& request) {
        seen_connection.store(static_cast<int>(request.connection));
        (void)server.requests().size();
        (void)server.connections_accepted();
        http::Reply reply;
        http::Header seen;
        seen.name = "X-Seen";
        const std::string* mark = request.header("x-mark");
        seen.value = mark == nullptr ? "" : *mark;
        reply.headers.push_back(std::move(seen));
        reply.body = request.body;
        CHECK(request.header("no-such") == nullptr);
        return reply;
    });
    server.handle("GET", "/boom", [](const http::LoggedRequest&) -> http::Reply {
        throw std::runtime_error("handler failed");
    });
    server.handle("GET", "/whole", [](const http::LoggedRequest&) {
        http::Reply reply;
        reply.body = bytes_of("abcdefgh");
        return reply;
    });
    expect_open(
        server,
        "GET /old HTTP/1.1\r\n\r\n",
        "HTTP/1.1 302 Found\r\nLocation: /new\r\nContent-Length: 0\r\n\r\n"
    );
    expect_open(
        server,
        "POST /away HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 301 Moved Permanently\r\nLocation: /other\r\nContent-Length: 0\r\n\r\n"
    );
    expect_open(
        server,
        "POST /echo HTTP/1.1\r\nx-mark: mark\r\nContent-Length: 7\r\n\r\npayload",
        "HTTP/1.1 200 OK\r\nX-Seen: mark\r\nContent-Length: 7\r\n\r\npayload"
    );
    CHECK(seen_connection.load() == 3);
    const std::vector<http::LoggedRequest> log = server.requests();
    CHECK(log.size() == 3);
    CHECK(log[2].method == "POST");
    CHECK(log[2].body == bytes_of("payload"));
    CHECK(log[2].header("X-Mark") != nullptr);
    CHECK(*log[2].header("X-Mark") == "mark");
    expect_closed(
        server,
        "GET /boom HTTP/1.1\r\n\r\n",
        "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    );
    expect_open(
        server,
        "GET /whole HTTP/1.1\r\nRange: bytes=2-4\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nabcdefgh"
    );
}

/// An override is used for the asks it was given, then the path is served as usual.
void override_is_used_then_gone() {
    current_test = "override";
    http::Server server;
    if (!listen(server))
        return;
    server.serve_bytes("/q", bytes_of("FILE"), "text/plain");
    http::Reply first;
    first.body = bytes_of("ONCE");
    http::Reply second;
    second.body = bytes_of("TWICE");
    server.override_next("/q", first);
    server.override_next("/q", second);
    expect_open(
        server, "GET /q HTTP/1.1\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nONCE"
    );
    expect_open(
        server, "GET /q?x=1 HTTP/1.1\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nTWICE"
    );
    expect_open(
        server,
        "GET /q HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 4\r\n\r\nFILE"
    );
    const std::vector<http::LoggedRequest> log = server.requests();
    CHECK(log.size() == 3);
    CHECK(log[1].target == "/q?x=1");
    server.clear_log();
    CHECK(server.requests().empty());
}

/// The first byte waits out the head delay, and body pieces wait between them.
void delay_and_pacing() {
    current_test = "delay";
    http::Server server;
    if (!listen(server))
        return;
    http::Reply delayed;
    delayed.head_delay_ms = 200;
    delayed.body = bytes_of("Z");
    server.override_next("/d", delayed);
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    const auto start = std::chrono::steady_clock::now();
    CHECK(write_text(socket.fd, "GET /d HTTP/1.1\r\n\r\n"));
    uint8_t first = 0;
    CHECK(read_one(socket.fd, first, io_limit_ms) == OneByte::got);
    const int64_t first_at = milliseconds_since(start);
    CHECK(first_at >= 200);
    CHECK(first_at < 1500);
    CHECK(first == 'H');

    http::Reply paced;
    paced.body = bytes_of("abcd");
    paced.piece_bytes = 1;
    paced.piece_delay_ms = 80;
    server.override_next("/p", paced);
    Socket pace = connect_open(server.port());
    if (pace.fd == sock::invalid_socket)
        return;
    const std::string_view expected = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd";
    const auto pace_start = std::chrono::steady_clock::now();
    CHECK(write_text(pace.fd, "GET /p HTTP/1.1\r\n\r\n"));
    std::vector<uint8_t> got;
    std::vector<int64_t> at;
    while (got.size() < expected.size()) {
        uint8_t byte = 0;
        CHECK(read_one(pace.fd, byte, io_limit_ms) == OneByte::got);
        got.push_back(byte);
        at.push_back(milliseconds_since(pace_start));
        if (failures > 0 && got.size() != expected.size() && at.back() > 2000)
            break;
    }
    check_bytes(got, expected);
    const std::size_t body = expected.find("\r\n\r\n");
    CHECK(body != std::string_view::npos);
    if (body != std::string_view::npos && got.size() == expected.size()) {
        for (std::size_t i = body + 4 + 1; i < got.size(); ++i)
            CHECK(at[i] - at[i - 1] >= 65);
    }
}

/// A cut stops after that many body bytes, and a raw reply is those bytes alone.
void cut_and_raw() {
    current_test = "cut";
    http::Server server;
    if (!listen(server))
        return;
    http::Reply cut;
    cut.body = bytes_of("abcdefghij");
    cut.cut_after = 4;
    server.override_next("/cut", cut);
    expect_closed(
        server,
        "GET /cut HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nabcd"
    );

    http::Reply none;
    none.body = bytes_of("HELLO");
    none.cut_after = 0;
    server.override_next("/none", none);
    expect_closed(
        server,
        "GET /none HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n"
    );

    http::Reply shorter;
    shorter.body = bytes_of("ab");
    shorter.cut_after = 10;
    server.override_next("/short", shorter);
    expect_closed(
        server,
        "GET /short HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nab"
    );

    http::Reply framed;
    framed.body = bytes_of("abcdefghij");
    framed.chunked = true;
    framed.chunk_bytes = 4;
    framed.cut_after = 9;
    server.override_next("/framed", framed);
    expect_closed(
        server,
        "GET /framed HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n4\r\nabcd\r\n"
    );

    http::Reply raw;
    raw.raw = bytes_of("not-a-status-line");
    server.override_next("/raw", raw);
    expect_closed(server, "GET /raw HTTP/1.1\r\n\r\n", "not-a-status-line");

    http::Reply unlengthed;
    unlengthed.body = bytes_of("BODY");
    unlengthed.no_length = true;
    server.override_next("/nl", unlengthed);
    expect_closed(
        server, "GET /nl HTTP/1.1\r\n\r\n", "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nBODY"
    );

    http::Reply extra;
    extra.body = bytes_of("hi");
    http::Header length;
    length.name = "Content-Length";
    length.value = "999";
    extra.headers.push_back(length);
    http::Header mark;
    mark.name = "X-Extra";
    mark.value = "1";
    extra.headers.push_back(std::move(mark));
    server.override_next("/extra", extra);
    expect_open(
        server,
        "GET /extra HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX-Extra: 1\r\nContent-Length: 2\r\n\r\nhi"
    );
}

/// A quiet connection stays up until the idle time, then closes.
void idle_close() {
    current_test = "idle";
    http::Server server;
    server.set_idle_close_ms(400);
    if (!listen(server))
        return;
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    const auto start = std::chrono::steady_clock::now();
    uint8_t byte = 0;
    CHECK(read_one(socket.fd, byte, 150) == OneByte::timeout);
    const OneByte closed = read_one(socket.fd, byte, 1000);
    const int64_t elapsed = milliseconds_since(start);
    CHECK(closed == OneByte::ended);
    CHECK(elapsed >= 400);
    CHECK(elapsed < 1200);
}

/// The log keeps the connection number and the target, query included.
void request_log() {
    current_test = "log";
    TempDir temp;
    CHECK(write_file(temp.path / "a.txt", "abcdefgh"));
    http::Server server;
    if (!listen(server))
        return;
    server.serve_folder(temp.path, "/pkg");
    expect_open(
        server,
        "GET /pkg/a.txt?x=1 HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\nabcdefgh"
    );
    expect_open(
        server, "GET /a.txt HTTP/1.1\r\n\r\n", "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
    );
    expect_open(
        server,
        "GET /pkgx/a.txt HTTP/1.1\r\n\r\n",
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"
    );
    const std::vector<http::LoggedRequest> log = server.requests();
    CHECK(log.size() == 3);
    CHECK(log[0].connection == 1);
    CHECK(log[0].method == "GET");
    CHECK(log[0].target == "/pkg/a.txt?x=1");
    CHECK(log[0].version == "HTTP/1.1");
}

/// A folder path cannot climb out with `..`, a backslash or a NUL.
void folder_escape() {
    current_test = "escape";
    TempDir temp;
    const std::filesystem::path root = temp.path / "root";
    CHECK(write_file(root / "a.txt", "inside"));
    CHECK(write_file(temp.path / "secret.txt", "secret"));
    http::Server server;
    if (!listen(server))
        return;
    server.serve_folder(root);
    expect_open(
        server,
        "GET /a.txt HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 6\r\n\r\ninside"
    );
    const std::string_view missing = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    expect_open(server, "GET /../secret.txt HTTP/1.1\r\n\r\n", missing);
    expect_open(server, "GET /a/../../secret.txt HTTP/1.1\r\n\r\n", missing);
    expect_open(server, "GET /a\\b.txt HTTP/1.1\r\n\r\n", missing);

    std::string nul_request = "GET /a";
    nul_request.push_back('\0');
    nul_request += ".txt HTTP/1.1\r\n\r\n";
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    CHECK(write_all(
        socket.fd, reinterpret_cast<const uint8_t*>(nul_request.data()), nul_request.size()
    ));
    std::vector<uint8_t> got;
    CHECK(read_exact(socket.fd, got, missing.size(), io_limit_ms));
    check_bytes(got, missing);
}

/// A slow reply does not hold up another connection.
void slow_does_not_stall() {
    current_test = "stall";
    http::Server server;
    if (!listen(server))
        return;
    server.serve_bytes("/fast", bytes_of("OK"), "text/plain");
    http::Reply slow;
    slow.head_delay_ms = 700;
    slow.body = bytes_of("SLOW");
    server.override_next("/slow", slow);
    Socket slow_socket = connect_open(server.port());
    Socket fast_socket = connect_open(server.port());
    if (slow_socket.fd == sock::invalid_socket || fast_socket.fd == sock::invalid_socket)
        return;
    const auto start = std::chrono::steady_clock::now();
    CHECK(write_text(slow_socket.fd, "GET /slow HTTP/1.1\r\n\r\n"));
    CHECK(write_text(fast_socket.fd, "GET /fast HTTP/1.1\r\n\r\n"));
    const std::string_view fast_response =
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nOK";
    std::vector<uint8_t> got;
    CHECK(read_exact(fast_socket.fd, got, fast_response.size(), 400));
    CHECK(milliseconds_since(start) < 400);
    check_bytes(got, fast_response);
    uint8_t first = 0;
    CHECK(read_one(slow_socket.fd, first, io_limit_ms) == OneByte::got);
    CHECK(milliseconds_since(start) >= 680);
    CHECK(first == 'H');
}

/// stop returns while a delayed reply has not been sent.
void stop_while_delayed() {
    current_test = "stop";
    http::Server server;
    if (!listen(server))
        return;
    http::Reply delayed;
    delayed.head_delay_ms = 8000;
    delayed.body = bytes_of("Z");
    server.override_next("/slow", delayed);
    Socket socket = connect_open(server.port());
    if (socket.fd == sock::invalid_socket)
        return;
    CHECK(write_text(socket.fd, "GET /slow HTTP/1.1\r\n\r\n"));
    const auto wait_start = std::chrono::steady_clock::now();
    while (server.requests().empty() && milliseconds_since(wait_start) < 2000) {
        sock::StreamWaitEntry wait{};
        wait.stream = socket.fd;
        wait.read = true;
        if (sock::stream_wait(&wait, 1, 20) < 0)
            break;
    }
    CHECK(server.requests().size() == 1);
    const auto stop_start = std::chrono::steady_clock::now();
    server.stop();
    const int64_t elapsed = milliseconds_since(stop_start);
    CHECK(elapsed < 1000);
    CHECK(server.port() == 0);

    std::string error;
    CHECK(server.start(&error));
    CHECK(server.port() != 0);
    server.serve_bytes("/mem", bytes_of("Z"), "text/plain");
    expect_open(
        server,
        "GET /mem HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 1\r\n\r\nZ"
    );
    CHECK(server.connections_accepted() == 2);
}

/// The longest prefix wins, and a later folder of that prefix replaces it.
void folder_prefix() {
    current_test = "prefix";
    TempDir temp;
    CHECK(write_file(temp.path / "short" / "a.txt", "SHORT"));
    CHECK(write_file(temp.path / "long" / "a.txt", "LONG!"));
    CHECK(write_file(temp.path / "later" / "a.txt", "LATER"));
    http::Server server;
    if (!listen(server))
        return;
    server.serve_folder(temp.path / "short", "/");
    server.serve_folder(temp.path / "long", "/pkg");
    server.serve_folder(temp.path / "short", "/pkg");
    server.serve_folder(temp.path / "later", "/pkg");
    expect_open(
        server,
        "GET /a.txt HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nSHORT"
    );
    expect_open(
        server,
        "GET /pkg/a.txt HTTP/1.1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nLATER"
    );
}

} // namespace

int main() {
    files_and_byte_strings();
    missing_and_method();
    head_has_headers_and_no_body();
    keep_alive_and_close();
    ranges();
    chunked_framing();
    gzip_inflates();
    redirect_and_handler();
    override_is_used_then_gone();
    delay_and_pacing();
    cut_and_raw();
    idle_close();
    request_log();
    folder_escape();
    slow_does_not_stall();
    stop_while_delayed();
    folder_prefix();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("http fixture: all tests passed");
    return 0;
}
