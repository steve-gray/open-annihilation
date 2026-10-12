// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A second host that asks for an enumeration port the first already holds is
// refused. One line on standard error names that port and the system's
// reason, and the host's error text is those same words.

#include "oa/netgame/socket_host.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

/// Standard error diverted into a temporary file for one hosting attempt.
struct StderrCapture {
    FILE* file = nullptr;
    int saved = -1;

    /// Sends what is written to standard error into a temporary file.
    ///
    /// @return false when the file or the diversion could not be made
    bool begin() {
#ifdef _MSC_VER
        // This compiler deprecates tmpfile; tmpfile_s makes the same unnamed file.
        if (tmpfile_s(&file) != 0)
            file = nullptr;
#else
        file = std::tmpfile();
#endif
        if (file == nullptr)
            return false;
#ifdef _WIN32
        const int from = _fileno(stderr);
        const int into = _fileno(file);
        saved = _dup(from);
        if (saved < 0 || _dup2(into, from) != 0) {
#else
        const int from = fileno(stderr);
        const int into = fileno(file);
        saved = dup(from);
        // POSIX dup2 returns the descriptor it made, not 0.
        if (saved < 0 || dup2(into, from) < 0) {
#endif
            if (saved >= 0) {
#ifdef _WIN32
                _close(saved);
#else
                close(saved);
#endif
                saved = -1;
            }
            std::fclose(file);
            file = nullptr;
            return false;
        }
        return true;
    }

    /// Puts standard error back, after anything still buffered has been written.
    void end() {
        if (saved < 0)
            return;
        std::fflush(stderr);
#ifdef _WIN32
        _dup2(saved, _fileno(stderr));
        _close(saved);
#else
        dup2(saved, fileno(stderr));
        close(saved);
#endif
        saved = -1;
    }

    /// The text written since begin, including its line breaks.
    ///
    /// @return the captured text; empty when begin did not succeed
    std::string read() {
        std::fflush(stderr);
        if (file == nullptr)
            return {};
        std::fflush(file);
        std::rewind(file);
        std::string text;
        char buffer[256];
        while (std::fgets(buffer, static_cast<int>(sizeof buffer), file) != nullptr)
            text += buffer;
        return text;
    }

    ~StderrCapture() {
        end();
        if (file != nullptr)
            std::fclose(file);
    }
};

/// How many lines of text contain needle.
///
/// @param text the captured standard error
/// @param needle the words a line must contain
/// @return the number of such lines
int lines_containing(const std::string& text, const char* needle) {
    int count = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::size_t stop = end == std::string::npos ? text.size() : end;
        if (text.substr(at, stop - at).find(needle) != std::string::npos)
            ++count;
        if (end == std::string::npos)
            break;
        at = end + 1;
    }
    return count;
}

/// A host bound to this machine, with stream and datagram ports the system chooses.
///
/// @param enumeration enumeration port; 0 asks the system for one
/// @return the configuration
sock::HostConfig loopback_ports(uint16_t enumeration) {
    sock::HostConfig config{};
    config.bind_ip[0] = 127;
    config.bind_ip[3] = 1;
    config.stream_port_first = 0;
    config.stream_port_last = 0;
    config.datagram_port_first = 0;
    config.datagram_port_last = 0;
    config.enum_port = enumeration;
    return config;
}

/// A held enumeration port is refused, and the refusal names it once.
void refused_enumeration_names_the_port() {
    current_test = "refused enumeration names the port";
    auto held = std::make_unique<sock::Host>();
    const bool held_open = sock::host_open(held.get(), loopback_ports(0));
    const bool held_enum = held_open && sock::host_listen_enumeration(held.get());
    const unsigned port = held->enum_port_bound;

    StderrCapture captured;
    const bool diverted = captured.begin();
    auto refused = std::make_unique<sock::Host>();
    bool opened = false;
    bool listened = false;
    if (diverted) {
        opened = sock::host_open(refused.get(), loopback_ports(static_cast<uint16_t>(port)));
        listened = opened && sock::host_listen_enumeration(refused.get());
    }
    const std::string logged = captured.read();
    const std::string reason = refused->error;
    captured.end();

    CHECK(held_open);
    CHECK(held_enum);
    CHECK(port != 0);
    CHECK(diverted);
    CHECK(opened);
    CHECK(!listened);
    CHECK(lines_containing(logged, "multiplayer: hosting failed") == 1);
    char expect[96];
    std::snprintf(
        expect,
        sizeof expect,
        "multiplayer: hosting failed while binding the enumeration port %u: ",
        port
    );
    CHECK(logged.find(expect) != std::string::npos);
    CHECK(!reason.empty());
    CHECK(logged.find(reason) != std::string::npos);

    if (opened)
        sock::host_close(refused.get());
    if (held_open)
        sock::host_close(held.get());
}

} // namespace

int main() {
    refused_enumeration_names_the_port();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
