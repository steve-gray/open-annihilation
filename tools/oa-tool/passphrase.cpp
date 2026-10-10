// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The passphrase is read here and nowhere else. Echo is off for the whole
// read, and a handler puts it back if the read is interrupted, so a stop at
// the prompt does not leave the terminal silent.
#include "passphrase.hpp"

#include "command.hpp"

#include <monocypher.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace oa::tool {
namespace {

constexpr std::size_t passphrase_most = 1024;

const char* const no_terminal =
    "no terminal is available; pass the passphrase with --passphrase-file";
const char* const too_long = "the passphrase is longer than 1024 bytes";
const char* const too_short = "the passphrase must be at least 12 bytes";
const char* const mismatch = "the passphrases do not match";

/// A passphrase held only until it is returned or discarded.
class SecretText {
  public:

    std::string text;

    SecretText() { text.reserve(passphrase_most); }

    SecretText(const SecretText&) = delete;
    SecretText& operator=(const SecretText&) = delete;

    ~SecretText() { crypto_wipe(text.data(), text.size()); }
};

void wipe_string(std::string& text) {
    crypto_wipe(text.data(), text.size());
    text.clear();
}

#if defined(_WIN32)

HANDLE console_input = INVALID_HANDLE_VALUE;
DWORD console_mode = 0;
volatile bool console_armed = false;

BOOL WINAPI restore_console(DWORD kind) {
    if (kind == CTRL_C_EVENT || kind == CTRL_BREAK_EVENT || kind == CTRL_CLOSE_EVENT ||
        kind == CTRL_LOGOFF_EVENT || kind == CTRL_SHUTDOWN_EVENT) {
        if (console_armed && console_input != INVALID_HANDLE_VALUE)
            SetConsoleMode(console_input, console_mode);
        console_armed = false;
    }
    return FALSE;
}

class ConsoleSession {
  public:

    ConsoleSession() = default;
    ConsoleSession(const ConsoleSession&) = delete;
    ConsoleSession& operator=(const ConsoleSession&) = delete;

    ~ConsoleSession() { restore(); }

    void open() {
        input_ = CreateFileA(
            "CONIN$",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        output_ = CreateFileA(
            "CONOUT$",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        if (input_ == INVALID_HANDLE_VALUE || output_ == INVALID_HANDLE_VALUE)
            throw Failure(no_terminal);
        if (GetConsoleMode(input_, &mode_) == 0)
            throw Failure(no_terminal);
        console_input = input_;
        console_mode = mode_;
        console_armed = true;
        if (SetConsoleCtrlHandler(restore_console, TRUE) == 0)
            throw Failure(no_terminal);
        handler_ = true;
        const DWORD hidden = mode_ & ~static_cast<DWORD>(ENABLE_ECHO_INPUT);
        if (SetConsoleMode(input_, hidden) == 0)
            throw Failure(no_terminal);
        hidden_ = true;
    }

    void restore() noexcept {
        if (hidden_ && input_ != INVALID_HANDLE_VALUE)
            SetConsoleMode(input_, mode_);
        hidden_ = false;
        console_armed = false;
        if (handler_)
            SetConsoleCtrlHandler(restore_console, FALSE);
        handler_ = false;
        if (input_ != INVALID_HANDLE_VALUE)
            CloseHandle(input_);
        if (output_ != INVALID_HANDLE_VALUE && output_ != input_)
            CloseHandle(output_);
        input_ = INVALID_HANDLE_VALUE;
        output_ = INVALID_HANDLE_VALUE;
        console_input = INVALID_HANDLE_VALUE;
    }

    void write(std::string_view text) const {
        DWORD written = 0;
        if (WriteConsoleA(
                output_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr
            ) != 0)
            return;
        if (WriteFile(output_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) !=
                0 &&
            written == text.size())
            return;
        throw Failure(no_terminal);
    }

    [[nodiscard]] HANDLE input() const noexcept { return input_; }

  private:

    HANDLE input_ = INVALID_HANDLE_VALUE;
    HANDLE output_ = INVALID_HANDLE_VALUE;
    DWORD mode_ = 0;
    bool handler_ = false;
    bool hidden_ = false;
};

using ReadConsoleWide = BOOL(WINAPI*)(HANDLE, LPWSTR, DWORD, LPDWORD, void*);

ReadConsoleWide read_console_wide() {
    const HMODULE kernel = GetModuleHandleA("kernel32.dll");
    if (kernel == nullptr)
        return nullptr;
    const FARPROC found = GetProcAddress(kernel, "ReadConsoleW");
    ReadConsoleWide read = nullptr;
    static_assert(sizeof(read) == sizeof(found));
    std::memcpy(&read, &found, sizeof read);
    return read;
}

void drain_wide(HANDLE input, ReadConsoleWide read_wide) {
    for (;;) {
        wchar_t junk[256]{};
        DWORD more = 0;
        const BOOL ok = read_wide(input, junk, 255, &more, nullptr);
        const bool ended = ok && more > 0 && junk[more - 1] == L'\n';
        crypto_wipe(junk, sizeof junk);
        if (!ok || more == 0 || ended)
            return;
    }
}

void drain_narrow(HANDLE input) {
    for (;;) {
        char junk[256]{};
        DWORD more = 0;
        const BOOL ok = ReadConsoleA(input, junk, 255, &more, nullptr);
        const bool ended = ok && more > 0 && junk[more - 1] == '\n';
        crypto_wipe(junk, sizeof junk);
        if (!ok || more == 0 || ended)
            return;
    }
}

std::string narrow_line(std::string_view bytes) {
    std::size_t length = bytes.size();
    while (length > 0 && (bytes[length - 1] == '\n' || bytes[length - 1] == '\r'))
        --length;
    if (length > passphrase_most)
        throw Failure(too_long);
    return std::string(bytes.data(), length);
}

std::string read_terminal(std::string_view prompt) {
    ConsoleSession console;
    console.open();
    console.write(prompt);
    const ReadConsoleWide read_wide = read_console_wide();
    bool use_narrow = read_wide == nullptr;
    if (!use_narrow) {
        wchar_t wide[1026]{};
        DWORD read = 0;
        const BOOL ok = read_wide(console.input(), wide, 1025, &read, nullptr);
        if (!ok && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) {
            crypto_wipe(wide, sizeof wide);
            use_narrow = true;
        } else if (!ok) {
            crypto_wipe(wide, sizeof wide);
            throw Failure("cannot read the passphrase");
        } else {
            const bool over = read == 1025 && wide[read - 1] != L'\n';
            if (over)
                drain_wide(console.input(), read_wide);
            DWORD length = read;
            while (length > 0 && (wide[length - 1] == L'\n' || wide[length - 1] == L'\r'))
                --length;
            std::string text;
            if (!over && length > 0) {
                // Flag 0: XP rejects WC_ERR_INVALID_CHARS for UTF-8.
                const int needed = WideCharToMultiByte(
                    CP_UTF8, 0, wide, static_cast<int>(length), nullptr, 0, nullptr, nullptr
                );
                if (needed <= 0 || static_cast<std::size_t>(needed) > passphrase_most) {
                    crypto_wipe(wide, sizeof wide);
                    console.write("\n");
                    throw Failure(
                        needed > static_cast<int>(passphrase_most) ? too_long
                                                                   : "cannot read the passphrase"
                    );
                }
                text.assign(static_cast<std::size_t>(needed), '\0');
                const int wrote = WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    wide,
                    static_cast<int>(length),
                    text.data(),
                    needed,
                    nullptr,
                    nullptr
                );
                crypto_wipe(wide, sizeof wide);
                console.write("\n");
                if (wrote != needed) {
                    wipe_string(text);
                    throw Failure("cannot read the passphrase");
                }
                return text;
            }
            crypto_wipe(wide, sizeof wide);
            console.write("\n");
            if (over)
                throw Failure(too_long);
            return text;
        }
    }
    char bytes[1026]{};
    DWORD read = 0;
    if (ReadConsoleA(console.input(), bytes, 1025, &read, nullptr) == 0) {
        crypto_wipe(bytes, sizeof bytes);
        throw Failure("cannot read the passphrase");
    }
    const bool over = read == 1025 && bytes[read - 1] != '\n';
    if (over)
        drain_narrow(console.input());
    std::string text = over ? std::string{} : narrow_line(std::string_view(bytes, read));
    crypto_wipe(bytes, sizeof bytes);
    console.write("\n");
    if (over) {
        wipe_string(text);
        throw Failure(too_long);
    }
    return text;
}

#else

int terminal_fd = -1;
termios saved_mode{};
volatile std::sig_atomic_t terminal_armed = 0;

void restore_echo() noexcept {
    if (terminal_armed == 0)
        return;
    const int fd = terminal_fd;
    terminal_armed = 0;
    if (fd >= 0)
        ::tcsetattr(fd, TCSANOW, &saved_mode);
}

void stop_handler(int signal_number) {
    restore_echo();
    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    ::sigaction(signal_number, &action, nullptr);
    ::raise(signal_number);
}

class TerminalSession {
  public:

    TerminalSession() = default;
    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    ~TerminalSession() { restore(); }

    void open() {
        fd_ = ::open("/dev/tty", O_RDWR | O_CLOEXEC);
        if (fd_ < 0)
            throw Failure(no_terminal);
        if (::tcgetattr(fd_, &saved_mode) != 0)
            throw Failure(no_terminal);
        terminal_fd = fd_;
        terminal_armed = 1;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        struct sigaction action{};
        action.sa_handler = stop_handler;
        sigemptyset(&action.sa_mask);
        if (::sigaction(SIGINT, &action, &old_int_) != 0)
            throw Failure(no_terminal);
        int_set_ = true;
        if (::sigaction(SIGTERM, &action, &old_term_) != 0)
            throw Failure(no_terminal);
        term_set_ = true;
        termios hidden = saved_mode;
        hidden.c_lflag = static_cast<tcflag_t>(hidden.c_lflag & ~static_cast<tcflag_t>(ECHO));
        if (::tcsetattr(fd_, TCSANOW, &hidden) != 0)
            throw Failure(no_terminal);
    }

    void restore() noexcept {
        restore_echo();
        if (int_set_)
            ::sigaction(SIGINT, &old_int_, nullptr);
        if (term_set_)
            ::sigaction(SIGTERM, &old_term_, nullptr);
        int_set_ = false;
        term_set_ = false;
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = -1;
        terminal_fd = -1;
    }

    void write(std::string_view text) const {
        std::size_t done = 0;
        while (done < text.size()) {
            const auto wrote = ::write(fd_, text.data() + done, text.size() - done);
            if (wrote < 0 && errno == EINTR)
                continue;
            if (wrote <= 0)
                throw Failure(no_terminal);
            done += static_cast<std::size_t>(wrote);
        }
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }

  private:

    int fd_ = -1;
    bool int_set_ = false;
    bool term_set_ = false;
    struct sigaction old_int_{};
    struct sigaction old_term_{};
};

void drain_line(int fd) {
    for (;;) {
        char byte = 0;
        const auto got = ::read(fd, &byte, 1);
        if (got < 0 && errno == EINTR)
            continue;
        const char eaten = byte;
        crypto_wipe(&byte, 1);
        if (got <= 0 || eaten == '\n')
            return;
    }
}

std::string read_terminal(std::string_view prompt) {
    TerminalSession terminal;
    terminal.open();
    terminal.write(prompt);
    SecretText secret;
    for (;;) {
        char byte = 0;
        const auto got = ::read(terminal.fd(), &byte, 1);
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0) {
            crypto_wipe(&byte, 1);
            throw Failure("cannot read the passphrase");
        }
        if (got == 0 || byte == '\n') {
            crypto_wipe(&byte, 1);
            break;
        }
        if (secret.text.size() == passphrase_most) {
            crypto_wipe(&byte, 1);
            drain_line(terminal.fd());
            throw Failure(too_long);
        }
        secret.text.push_back(byte);
        crypto_wipe(&byte, 1);
    }
    if (!secret.text.empty() && secret.text.back() == '\r') {
        secret.text.back() = '\0';
        secret.text.pop_back();
    }
    terminal.write("\n");
    return std::move(secret.text);
}

#endif

struct FileBuffer {
    std::array<char, passphrase_most + 1> bytes{};

    ~FileBuffer() { crypto_wipe(bytes.data(), bytes.size()); }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw Failure("cannot read the passphrase file");
    FileBuffer buffer;
    in.read(buffer.bytes.data(), static_cast<std::streamsize>(buffer.bytes.size()));
    const auto count = in.gcount();
    if (in.bad() || count < 0)
        throw Failure("cannot read the passphrase file");
    const auto got = static_cast<std::size_t>(count);
    std::size_t end = 0;
    bool newline = false;
    for (; end < got; ++end) {
        if (buffer.bytes[end] == '\n') {
            newline = true;
            break;
        }
    }
    if (!newline && got == buffer.bytes.size())
        throw Failure(too_long);
    std::size_t length = newline ? end : got;
    if (length > 0 && buffer.bytes[length - 1] == '\r')
        --length;
    return std::string(buffer.bytes.data(), length);
}

} // namespace

std::string
read_passphrase(std::string_view prompt, const std::optional<std::filesystem::path>& file) {
    if (file)
        return read_file(*file);
    return read_terminal(prompt);
}

std::string read_new_passphrase(const std::optional<std::filesystem::path>& file) {
    std::string first = read_passphrase("Passphrase: ", file);
    if (first.size() < 12) {
        wipe_string(first);
        throw Failure(too_short);
    }
    if (file)
        return first;
    std::string second = read_passphrase("Repeat passphrase: ", std::nullopt);
    const bool same = first == second;
    wipe_string(second);
    if (!same) {
        wipe_string(first);
        throw Failure(mismatch);
    }
    return first;
}

} // namespace oa::tool
