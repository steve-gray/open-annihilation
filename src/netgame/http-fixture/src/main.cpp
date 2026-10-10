// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Serves one folder over HTTP/1.1 on 127.0.0.1 until a signal or a time limit.
// Scripts use it where a test would use the fixture library.

#include "oa/netgame/http_fixture/server.hpp"

#include "oa/base/threads.hpp"
#include "oa/platform/files.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::atomic<bool> g_stop{false};

/// Asks the process to stop. A signal handler may call it.
///
/// @param signal the signal number, unused
void on_signal(int signal) {
    (void)signal;
    g_stop.store(true);
}

/// Prints a usage error and returns the bad-argument status.
///
/// @param text the error
/// @return 2
int bad_argument(const char* text) {
    std::fprintf(stderr, "oa-http-fixture: %s\n", text);
    return 2;
}

/// Reads an unsigned decimal number with a maximum.
///
/// @param text the digits
/// @param max the largest value accepted
/// @param[out] value the number
/// @return false when the text is not such a number
bool parse_u32(const char* text, uint32_t max, uint32_t& value) {
    if (text == nullptr || text[0] < '0' || text[0] > '9')
        return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > max)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

/// Appends requests the log file does not yet hold.
///
/// @param server the server
/// @param log the log file; null skips the log
/// @param[in,out] written how many requests are already in the file
void write_log(oa::netgame::http_fixture::Server& server, std::FILE* log, std::size_t& written) {
    if (log == nullptr)
        return;
    const std::vector<oa::netgame::http_fixture::LoggedRequest> requests = server.requests();
    for (; written < requests.size(); ++written) {
        const oa::netgame::http_fixture::LoggedRequest& request = requests[written];
        const std::string* range = request.header("Range");
        std::fprintf(
            log,
            "%llu\t%s\t%s\t%s\n",
            static_cast<unsigned long long>(request.connection),
            request.method.c_str(),
            request.target.c_str(),
            range == nullptr ? "" : range->c_str()
        );
    }
    std::fflush(log);
}

} // namespace

int main(int argc, char** argv) {
    const char* root = nullptr;
    const char* port_file = nullptr;
    const char* log_file = nullptr;
    uint32_t port_value = 0;
    uint32_t seconds = 0;
    bool have_seconds = false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
        if (std::strcmp(arg, "--root") == 0) {
            if (value == nullptr || value[0] == '\0' || root != nullptr)
                return bad_argument("--root needs a folder");
            root = value;
            ++i;
        } else if (std::strcmp(arg, "--port") == 0) {
            if (value == nullptr || !parse_u32(value, 65535, port_value))
                return bad_argument("--port needs a number from 0 to 65535");
            ++i;
        } else if (std::strcmp(arg, "--port-file") == 0) {
            if (value == nullptr || value[0] == '\0' || port_file != nullptr)
                return bad_argument("--port-file needs a path");
            port_file = value;
            ++i;
        } else if (std::strcmp(arg, "--log") == 0) {
            if (value == nullptr || value[0] == '\0' || log_file != nullptr)
                return bad_argument("--log needs a path");
            log_file = value;
            ++i;
        } else if (std::strcmp(arg, "--seconds") == 0) {
            if (value == nullptr || have_seconds || !parse_u32(value, UINT32_MAX, seconds))
                return bad_argument("--seconds needs a number");
            have_seconds = true;
            ++i;
        } else {
            return bad_argument("unknown argument");
        }
    }
    if (root == nullptr)
        return bad_argument("--root is required");

    oa::netgame::http_fixture::Server server;
    std::string error;
    if (!server.start(static_cast<uint16_t>(port_value), &error)) {
        std::fprintf(stderr, "oa-http-fixture: %s\n", error.c_str());
        return 1;
    }
    if (port_file != nullptr) {
        std::FILE* file = oa::platform::open_file(port_file, "w");
        if (file == nullptr) {
            std::fprintf(stderr, "oa-http-fixture: could not write %s\n", port_file);
            return 1;
        }
        std::fprintf(file, "%u\n", static_cast<unsigned>(server.port()));
        std::fclose(file);
    }
    std::FILE* log = nullptr;
    if (log_file != nullptr) {
        log = oa::platform::open_file(log_file, "w");
        if (log == nullptr) {
            std::fprintf(stderr, "oa-http-fixture: could not write %s\n", log_file);
            return 1;
        }
    }
    server.serve_folder(root);
    std::printf(
        "oa-http-fixture: serving %s at http://127.0.0.1:%u/\n",
        root,
        static_cast<unsigned>(server.port())
    );
    std::fflush(stdout);

    std::signal(SIGINT, on_signal);
#ifdef SIGTERM
    std::signal(SIGTERM, on_signal);
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::size_t written = 0;
    while (!g_stop.load()) {
        if (have_seconds && std::chrono::steady_clock::now() >= deadline)
            break;
        write_log(server, log, written);
        oa::base::threads::sleep_ms(20);
    }
    server.stop();
    write_log(server, log, written);
    if (log != nullptr)
        std::fclose(log);
    return 0;
}
