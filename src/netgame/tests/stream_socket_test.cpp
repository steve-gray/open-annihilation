// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Outbound TCP connections on 127.0.0.1: opening, refusal, reading, writing,
// ending and timing out, and the loopback listener a program on this machine
// uses to reach the game. Every wait blocks; nothing spins.

#include "oa/netgame/stream_socket.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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
constexpr std::size_t transfer_bytes = std::size_t{1} << 20;
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

/// A loopback listener and the port it was given.
struct Listener {
    Socket socket;
    uint16_t port{};
};

/// Returns how many milliseconds have passed since start.
///
/// @param start when the wait began
/// @return the elapsed milliseconds
uint32_t milliseconds_since(std::chrono::steady_clock::time_point start) {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start
    )
                                     .count());
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

/// Tells whether a failure message was written into the buffer.
///
/// @param error the buffer
/// @param size the bytes `error` holds
/// @return true when a zero-ended message was written
bool has_message(const char* error, std::size_t size) {
    return error[0] != '\0' && std::memchr(error, '\0', size) != nullptr;
}

/// Opens an IPv4 listener on a port the system chooses.
///
/// @return the listener; its socket is invalid when the system refused
Listener listen_loopback() {
    Listener listener;
    char error[160]{};
    listener.socket.fd =
        sock::stream_listen(sock::Loopback::ipv4, 0, listener.port, error, sizeof error);
    CHECK(listener.socket.fd != sock::invalid_socket);
    CHECK(listener.port != 0);
    return listener;
}

/// Accepts the connection waiting at a listener.
///
/// @param listener the listener
/// @return the connection, invalid when none was accepted
Socket accept_one(const Listener& listener) {
    sock::StreamWaitEntry wait{};
    wait.stream = listener.socket.fd;
    wait.read = true;
    CHECK(sock::stream_wait(&wait, 1, open_limit_ms) == 1);
    CHECK(wait.readable);
    Socket accepted(sock::stream_accept(listener.socket.fd));
    CHECK(accepted.fd != sock::invalid_socket);
    return accepted;
}

/// Opens a connection to a listener and waits until it has opened.
///
/// @param port the listener's port
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

/// Copies bytes from one connection to the other, waiting whenever the system takes nothing.
///
/// @param from the connection written
/// @param to the connection read
/// @param bytes the bytes to copy
/// @param size how many
/// @param[out] received what arrived; `size` bytes
/// @return false when the copy did not finish within the time limit
bool transfer(
    intptr_t from, intptr_t to, const uint8_t* bytes, std::size_t size, uint8_t* received
) {
    std::size_t sent = 0;
    std::size_t got = 0;
    const auto start = std::chrono::steady_clock::now();
    while (got < size) {
        const uint32_t used = milliseconds_since(start);
        if (used >= open_limit_ms)
            return false;
        if (sent < size) {
            const std::ptrdiff_t wrote = sock::stream_write(from, bytes + sent, size - sent);
            if (wrote == sock::stream_failed || wrote < 0)
                return false;
            if (wrote > 0)
                sent += static_cast<std::size_t>(wrote);
        }
        const std::ptrdiff_t arrived = sock::stream_read(to, received + got, size - got);
        if (arrived == sock::stream_failed || arrived == sock::stream_ended || arrived < 0)
            return false;
        if (arrived > 0) {
            got += static_cast<std::size_t>(arrived);
            continue;
        }
        sock::StreamWaitEntry entries[2]{};
        entries[0].stream = to;
        entries[0].read = true;
        std::size_t count = 1;
        if (sent < size) {
            entries[1].stream = from;
            entries[1].write = true;
            count = 2;
        }
        const int ready = sock::stream_wait(entries, count, open_limit_ms - used);
        if (ready <= 0 || entries[0].failed || (count == 2 && entries[1].failed))
            return false;
    }
    return sent == size;
}

/// Waits until the other end has finished sending.
///
/// @param stream the connection to read
/// @return false when a byte arrives or the end does not
bool read_ended(intptr_t stream) {
    const auto start = std::chrono::steady_clock::now();
    while (milliseconds_since(start) < open_limit_ms) {
        const uint32_t used = milliseconds_since(start);
        if (used >= open_limit_ms)
            return false;
        uint8_t byte = 0;
        const std::ptrdiff_t arrived = sock::stream_read(stream, &byte, 1);
        if (arrived == sock::stream_ended)
            return true;
        if (arrived != 0)
            return false;
        sock::StreamWaitEntry wait{};
        wait.stream = stream;
        wait.read = true;
        const int ready = sock::stream_wait(&wait, 1, open_limit_ms - used);
        if (ready <= 0 || (!wait.readable && !wait.failed))
            return false;
    }
    return false;
}

/// Fills a buffer with a pattern that depends on a seed.
///
/// @param[out] bytes the buffer
/// @param seed the first byte
void fill(std::vector<uint8_t>& bytes, uint8_t seed) {
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i * 131u));
}

/// A listener on a chosen port reports that port, and an empty connection reads nothing.
void loopback_listener_reports_its_port() {
    current_test = "loopback_listener_reports_its_port";
    Listener listener = listen_loopback();
    if (listener.socket.fd == sock::invalid_socket)
        return;
    Socket outbound = connect_open(listener.port);
    if (outbound.fd == sock::invalid_socket)
        return;
    Socket accepted = accept_one(listener);
    if (accepted.fd == sock::invalid_socket)
        return;
    uint8_t byte = 0x3c;
    CHECK(sock::stream_read(accepted.fd, &byte, 1) == 0);
    CHECK(byte == 0x3c);
    CHECK(sock::stream_read(outbound.fd, &byte, 1) == 0);
}

/// A connection to a listener opens, the listener is readable, and the connection is accepted.
void connection_opens_and_is_accepted() {
    current_test = "connection_opens_and_is_accepted";
    Listener listener = listen_loopback();
    if (listener.socket.fd == sock::invalid_socket)
        return;
    Socket outbound = connect_open(listener.port);
    if (outbound.fd == sock::invalid_socket)
        return;
    Socket accepted = accept_one(listener);
    CHECK(accepted.fd != sock::invalid_socket);
}

/// A megabyte written each way arrives whole, and finishing one side ends the other.
void bytes_arrive_each_way_and_the_end_follows() {
    current_test = "bytes_arrive_each_way_and_the_end_follows";
    Listener listener = listen_loopback();
    if (listener.socket.fd == sock::invalid_socket)
        return;
    Socket outbound = connect_open(listener.port);
    Socket accepted = accept_one(listener);
    if (outbound.fd == sock::invalid_socket || accepted.fd == sock::invalid_socket)
        return;

    std::vector<uint8_t> out_to_in(transfer_bytes);
    std::vector<uint8_t> in_to_out(transfer_bytes);
    std::vector<uint8_t> received(transfer_bytes);
    fill(out_to_in, 17);
    fill(in_to_out, 29);
    CHECK(transfer(outbound.fd, accepted.fd, out_to_in.data(), transfer_bytes, received.data()));
    CHECK(std::memcmp(received.data(), out_to_in.data(), transfer_bytes) == 0);
    CHECK(transfer(accepted.fd, outbound.fd, in_to_out.data(), transfer_bytes, received.data()));
    CHECK(std::memcmp(received.data(), in_to_out.data(), transfer_bytes) == 0);

    sock::stream_finish(outbound.fd);
    CHECK(read_ended(accepted.fd));
}

/// A connection to a port nobody listens on fails, and does not open.
void closed_port_fails() {
    current_test = "closed_port_fails";
    Listener listener = listen_loopback();
    const uint16_t port = listener.port;
    sock::stream_close(&listener.socket.fd);
    if (port == 0)
        return;
    char error[160]{};
    Socket outbound(sock::stream_connect(loopback(port), error, sizeof error));
    CHECK(outbound.fd != sock::invalid_socket);
    if (outbound.fd == sock::invalid_socket)
        return;
    const sock::StreamOpen opened = sock::stream_wait_open(outbound.fd, open_limit_ms);
    CHECK(opened == sock::StreamOpen::failed);
    CHECK(opened != sock::StreamOpen::open);
}

/// A wait for bytes that never arrive runs out, after the time asked and before two seconds more.
void read_wait_times_out() {
    current_test = "read_wait_times_out";
    Listener listener = listen_loopback();
    if (listener.socket.fd == sock::invalid_socket)
        return;
    Socket outbound = connect_open(listener.port);
    Socket accepted = accept_one(listener);
    if (outbound.fd == sock::invalid_socket || accepted.fd == sock::invalid_socket)
        return;
    sock::StreamWaitEntry wait{};
    wait.stream = accepted.fd;
    wait.read = true;
    constexpr uint32_t wait_ms = 300;
    const auto start = std::chrono::steady_clock::now();
    const int ready = sock::stream_wait(&wait, 1, wait_ms);
    const uint32_t elapsed = milliseconds_since(start);
    CHECK(ready == 0);
    CHECK(!wait.readable);
    CHECK(!wait.writable);
    CHECK(!wait.failed);
    CHECK(elapsed >= wait_ms);
    CHECK(elapsed < wait_ms + 2000);
}

/// Of two connections, only the one written to is readable.
void only_the_written_connection_is_readable() {
    current_test = "only_the_written_connection_is_readable";
    Listener listener = listen_loopback();
    if (listener.socket.fd == sock::invalid_socket)
        return;
    Socket first = connect_open(listener.port);
    Socket first_accepted = accept_one(listener);
    Socket second = connect_open(listener.port);
    Socket second_accepted = accept_one(listener);
    if (first.fd == sock::invalid_socket || first_accepted.fd == sock::invalid_socket ||
        second.fd == sock::invalid_socket || second_accepted.fd == sock::invalid_socket)
        return;
    const uint8_t byte = 0x5a;
    CHECK(sock::stream_write(first.fd, &byte, 1) == 1);
    sock::StreamWaitEntry entries[2]{};
    entries[0].stream = first_accepted.fd;
    entries[0].read = true;
    entries[1].stream = second_accepted.fd;
    entries[1].read = true;
    CHECK(sock::stream_wait(entries, 2, open_limit_ms) == 1);
    CHECK(entries[0].readable);
    CHECK(!entries[0].failed);
    CHECK(!entries[1].readable);
    CHECK(!entries[1].writable);
    CHECK(!entries[1].failed);
    uint8_t got = 0;
    CHECK(sock::stream_read(first_accepted.fd, &got, 1) == 1);
    CHECK(got == byte);
}

/// Addresses that name no server, an empty wait and an invalid socket are refused.
void refusals() {
    current_test = "refusals";
    char error[160]{};
    sock::Address unspecified{};
    unspecified.port = 9;
    CHECK(sock::stream_connect(unspecified, error, sizeof error) == sock::invalid_socket);
    CHECK(has_message(error, sizeof error));

    std::memset(error, 0, sizeof error);
    CHECK(sock::stream_connect(loopback(0), error, sizeof error) == sock::invalid_socket);
    CHECK(has_message(error, sizeof error));

    std::memset(error, 0, sizeof error);
    sock::Address broadcast{};
    broadcast.ip[0] = broadcast.ip[1] = broadcast.ip[2] = broadcast.ip[3] = 255;
    broadcast.port = 9;
    CHECK(sock::stream_connect(broadcast, error, sizeof error) == sock::invalid_socket);
    CHECK(has_message(error, sizeof error));

    sock::StreamWaitEntry entries[sock::stream_wait_most + 1]{};
    CHECK(sock::stream_wait(entries, 0, 0) == -1);
    CHECK(sock::stream_wait(entries, sock::stream_wait_most + 1, 0) == -1);
    CHECK(sock::stream_wait_open(sock::invalid_socket, 0) == sock::StreamOpen::failed);
}

} // namespace

int main() {
    loopback_listener_reports_its_port();
    connection_opens_and_is_accepted();
    bytes_arrive_each_way_and_the_end_follows();
    closed_port_fails();
    read_wait_times_out();
    only_the_written_connection_is_readable();
    refusals();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("stream socket: all tests passed");
    return 0;
}
