// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// TCP streams: a listener on this machine's loopback address, the
// connections it accepts, and an outbound connection to any IPv4 address.
// All are non-blocking, and read and written without waiting. They are
// served by socket_host.cpp, the one source that calls the system's socket
// functions, with the same sockets as network play's: Winsock 1.1 on
// Windows 95, Winsock 2 on Windows from Windows XP on, and the system's
// sockets elsewhere.
#pragma once

#include "oa/netgame/socket_host.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::netgame::sock {

/// What stream_read and stream_write return when the connection failed.
inline constexpr std::ptrdiff_t stream_failed = -1;
/// What stream_read returns once the other end has said it will send nothing
/// more; what it sent before has all been read, and writing may still work.
inline constexpr std::ptrdiff_t stream_ended = -2;

/// The loopback address a stream listener is bound to.
enum class Loopback : uint8_t {
    ipv4, ///< 127.0.0.1
    ipv6, ///< ::1
};

/// Opens a non-blocking TCP listener on a loopback address.
///
/// @param loopback the address
/// @param port the port; 0 lets the system choose one
/// @param[out] bound_port the port the listener is bound to; unchanged on failure
/// @param[out] error why it failed, ending in a zero byte
/// @param error_size the bytes `error` holds
/// @return the listener, or invalid_socket when the system refused it
[[nodiscard]] intptr_t stream_listen(
    Loopback loopback, uint16_t port, uint16_t& bound_port, char* error, std::size_t error_size
) noexcept;

/// Accepts a connection waiting at a listener, without waiting for one.
///
/// The connection is non-blocking and sends what it is given at once.
///
/// @param listener the listener
/// @return the connection, or invalid_socket when none waits
[[nodiscard]] intptr_t stream_accept(intptr_t listener) noexcept;

/// Reads what has arrived on a connection, without waiting.
///
/// @param stream the connection
/// @param[out] bytes receives what was read
/// @param size the bytes `bytes` holds
/// @return the bytes read; 0 when nothing has arrived; stream_ended once
///         the other end will send nothing more; stream_failed when the
///         connection failed
[[nodiscard]] std::ptrdiff_t
stream_read(intptr_t stream, uint8_t* bytes, std::size_t size) noexcept;

/// Writes what the system takes now to a connection, without waiting.
///
/// @param stream the connection
/// @param bytes the bytes to write
/// @param size how many
/// @return the bytes the system took, which may be fewer than `size` or 0;
///         stream_failed when the connection failed
[[nodiscard]] std::ptrdiff_t
stream_write(intptr_t stream, const uint8_t* bytes, std::size_t size) noexcept;

/// Tells the other end that nothing more will be sent, once what was written goes out.
///
/// @param stream the connection
void stream_finish(intptr_t stream) noexcept;

/// Closes a listener or a connection.
///
/// @param[in,out] stream the socket; invalid_socket afterwards
void stream_close(intptr_t* stream) noexcept;

/// Starts a non-blocking TCP connection to an IPv4 address and returns at once.
///
/// The socket is prepared as one stream_accept returns: non-blocking, never
/// ending the program when the other end has gone, and sending what it is
/// given at once. The connection may still be opening when this returns;
/// stream_wait_open waits for it.
///
/// @param to the IPv4 address and port, the port in host order
/// @param[out] error why it failed, ending in a zero byte; unchanged when a connection was started
/// @param error_size the bytes `error` holds
/// @return the connection, which may still be opening, or invalid_socket when
///         the system refused at once, including 0.0.0.0, 255.255.255.255 and port 0
[[nodiscard]] intptr_t
stream_connect(const Address& to, char* error, std::size_t error_size) noexcept;

/// How far a connection stream_connect started has got.
enum class StreamOpen : uint8_t {
    open,    ///< writable, and the system reports no error
    opening, ///< the time ran out while the connection was still opening
    failed,  ///< an error, a refusal or an invalid socket
};

/// Waits up to a time limit for a connection stream_connect started to open.
///
/// @param stream the connection
/// @param wait_ms how long to wait, in milliseconds; 0 only looks
/// @return open once the connection is writable and the system reports no
///         error; failed on an error, a refusal or an invalid socket; opening
///         when the time ran out
[[nodiscard]] StreamOpen stream_wait_open(intptr_t stream, uint32_t wait_ms) noexcept;

/// One stream stream_wait watches, and what it found.
///
/// `stream`, `read` and `write` are what was asked. `readable`, `writable`
/// and `failed` are what was found.
struct StreamWaitEntry {
    intptr_t stream{invalid_socket};
    bool read{};     ///< wait for a connection, bytes or the other end's end
    bool write{};    ///< wait until bytes can be written
    bool readable{}; ///< a connection, bytes or the other end's end is ready
    bool writable{}; ///< bytes can be written
    bool failed{};   ///< the stream failed, or it is not a socket
};

/// The most streams one stream_wait watches.
inline constexpr std::size_t stream_wait_most = 16;

/// Waits until one stream is ready or the time runs out.
///
/// A listener is readable when a connection waits to be accepted. A stream
/// is readable when bytes or the other end's end have arrived.
///
/// @param[in,out] entries the streams and what to wait for on each; the found flags are written
/// @param count entries at `entries`, from 1 to stream_wait_most
/// @param wait_ms how long to wait, in milliseconds; 0 only looks
/// @return the entries with something found; 0 when the time ran out; -1 when
///         `count` is 0 or above stream_wait_most, or the wait itself failed
[[nodiscard]] int
stream_wait(StreamWaitEntry* entries, std::size_t count, uint32_t wait_ms) noexcept;

} // namespace oa::netgame::sock
