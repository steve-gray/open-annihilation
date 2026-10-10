// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Pushes arrived bytes through one response: the status line, the headers,
// and a body framed by length, chunks, or the close.

#include "response_reader.hpp"

#include <string_view>

namespace oa::netgame::http {
namespace {

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
/// @return false when the text is empty, not decimal, or too large
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

/// Reads a hexadecimal chunk size.
///
/// @param text the chunk-size line
/// @param[out] value the size
/// @param[out] used how many bytes were digits
/// @return false when there is no digit or the size does not fit
bool parse_hex(std::string_view text, uint64_t& value, std::size_t& used) {
    value = 0;
    used = 0;
    for (; used < text.size(); ++used) {
        const unsigned char byte = static_cast<unsigned char>(text[used]);
        uint64_t digit = 0;
        if (byte >= '0' && byte <= '9')
            digit = byte - '0';
        else if (byte >= 'a' && byte <= 'f')
            digit = byte - 'a' + 10;
        else if (byte >= 'A' && byte <= 'F')
            digit = byte - 'A' + 10;
        else
            break;
        if (value > (UINT64_MAX >> 4))
            return false;
        value = (value << 4) | digit;
    }
    return used > 0;
}

/// The lesser of a buffer count and a remaining length.
///
/// @param available bytes still in the buffer
/// @param want the length still wanted
/// @return how many bytes to take
std::size_t take_count(std::size_t available, uint64_t want) {
    if (static_cast<uint64_t>(available) <= want)
        return available;
    return static_cast<std::size_t>(want);
}

} // namespace

ResponseReader::ResponseReader(Limits limits, Method method, bool accept_gzip)
    : limits_(limits), method_(method), accept_gzip_(accept_gzip) {
}

int ResponseReader::status() const {
    return status_;
}

const std::vector<Header>& ResponseReader::headers() const {
    return headers_;
}

std::optional<uint64_t> ResponseReader::content_length() const {
    return content_length_;
}

bool ResponseReader::chunked() const {
    return chunked_;
}

bool ResponseReader::gzip() const {
    return gzip_;
}

bool ResponseReader::reusable() const {
    return complete_ && keep_alive_ && !close_delimited_ && failure_ == Failure::none;
}

Failure ResponseReader::failure() const {
    return failure_;
}

const std::string& ResponseReader::detail() const {
    return detail_;
}

ResponseReader::LineTake ResponseReader::take_line(
    std::span<const uint8_t> incoming, std::size_t& taken, std::size_t& counted, std::size_t budget
) {
    while (taken < incoming.size()) {
        if (counted >= budget)
            return LineTake::too_large;
        const uint8_t byte = incoming[taken];
        ++counted;
        ++taken;
        if (byte == '\n') {
            if (!line_.empty() && line_.back() == '\r')
                line_.pop_back();
            return LineTake::ready;
        }
        line_.push_back(static_cast<char>(byte));
    }
    return LineTake::need_more;
}

ReadPiece ResponseReader::fail(Failure failure, std::string detail) {
    if (phase_ != Phase::failed) {
        phase_ = Phase::failed;
        failure_ = failure;
        detail_ = std::move(detail);
        complete_ = false;
    }
    return ReadPiece{ReadKind::failed, {}};
}

ReadPiece ResponseReader::succeed() {
    phase_ = Phase::done;
    complete_ = true;
    failure_ = Failure::none;
    return ReadPiece{ReadKind::finished, {}};
}

ReadPiece ResponseReader::finish_headers() {
    if (status_ >= 100 && status_ <= 199) {
        headers_.clear();
        content_length_.reset();
        length_conflict_ = false;
        length_malformed_ = false;
        saw_length_ = false;
        chunked_ = false;
        bad_transfer_ = false;
        gzip_ = false;
        bad_encoding_ = false;
        saw_encoding_ = false;
        connection_close_ = false;
        connection_keep_alive_ = false;
        keep_alive_ = false;
        close_delimited_ = false;
        complete_ = false;
        body_remaining_ = 0;
        chunk_remaining_ = 0;
        body_emitted_ = 0;
        section_bytes_ = 0;
        line_.clear();
        phase_ = Phase::status_line;
        return ReadPiece{ReadKind::interim, {}};
    }

    for (const Header& header : headers_) {
        const std::string_view value = trim_ows(header.value);
        if (same_name(header.name, "content-length")) {
            uint64_t length = 0;
            if (!parse_u64(value, length)) {
                length_malformed_ = true;
            } else if (saw_length_ && content_length_ && *content_length_ != length) {
                length_conflict_ = true;
            } else if (!saw_length_) {
                saw_length_ = true;
                content_length_ = length;
            }
        } else if (same_name(header.name, "transfer-encoding")) {
            if (chunked_ || !same_name(value, "chunked"))
                bad_transfer_ = true;
            else
                chunked_ = true;
        } else if (same_name(header.name, "content-encoding")) {
            if (saw_encoding_)
                bad_encoding_ = true;
            saw_encoding_ = true;
            if (same_name(value, "identity")) {
                // An identity coding is the body as it stands.
            } else if (same_name(value, "gzip")) {
                if (!accept_gzip_)
                    bad_encoding_ = true;
                else
                    gzip_ = true;
            } else {
                bad_encoding_ = true;
            }
        } else if (same_name(header.name, "connection")) {
            std::string_view rest = value;
            while (!rest.empty()) {
                const std::size_t comma = rest.find(',');
                const std::string_view token =
                    trim_ows(comma == std::string_view::npos ? rest : rest.substr(0, comma));
                if (same_name(token, "close"))
                    connection_close_ = true;
                else if (same_name(token, "keep-alive"))
                    connection_keep_alive_ = true;
                if (comma == std::string_view::npos)
                    break;
                rest.remove_prefix(comma + 1);
            }
        }
    }

    if (length_conflict_)
        return fail(Failure::bad_response, "the response has two content lengths");
    if (length_malformed_)
        return fail(Failure::bad_response, "the content length could not be read");
    if (bad_transfer_)
        return fail(Failure::bad_response, "the transfer coding is not chunked");
    if (bad_encoding_)
        return fail(Failure::bad_response, "the content coding is not accepted");

    keep_alive_ =
        version_ >= 11 ? !connection_close_ : connection_keep_alive_ && !connection_close_;
    const bool no_body = method_ == Method::head || status_ == 204 || status_ == 304;
    if (no_body) {
        phase_ = Phase::done;
        complete_ = true;
        return ReadPiece{ReadKind::head, {}};
    }
    if (!chunked_ && saw_length_ && !gzip_ && content_length_ &&
        *content_length_ > limits_.max_body_bytes)
        return fail(Failure::too_large, "the body is larger than the limit");
    if (chunked_) {
        phase_ = Phase::chunk_size;
        section_bytes_ = 0;
        line_.clear();
        return ReadPiece{ReadKind::head, {}};
    }
    if (saw_length_) {
        body_remaining_ = content_length_.value_or(0);
        if (body_remaining_ == 0) {
            phase_ = Phase::done;
            complete_ = true;
        } else {
            phase_ = Phase::counted;
        }
        return ReadPiece{ReadKind::head, {}};
    }
    phase_ = Phase::until_close;
    close_delimited_ = true;
    keep_alive_ = false;
    return ReadPiece{ReadKind::head, {}};
}

ReadPiece ResponseReader::feed(std::span<const uint8_t> incoming, std::size_t& taken) {
    taken = 0;
    if (phase_ == Phase::failed)
        return ReadPiece{ReadKind::failed, {}};
    if (phase_ == Phase::done)
        return succeed();

    while (true) {
        switch (phase_) {
        case Phase::status_line:
        case Phase::headers: {
            const LineTake got =
                take_line(incoming, taken, section_bytes_, limits_.max_header_bytes);
            if (got == LineTake::need_more)
                return ReadPiece{ReadKind::need_more, {}};
            if (got == LineTake::too_large)
                return fail(Failure::too_large, "the headers are larger than the limit");
            if (phase_ == Phase::status_line) {
                const std::string_view line = line_;
                const bool http10 = line.starts_with("HTTP/1.0");
                const bool http11 = line.starts_with("HTTP/1.1");
                std::string_view rest;
                if (http10)
                    rest = line.substr(8);
                else if (http11)
                    rest = line.substr(8);
                int code = 0;
                const bool digits = rest.size() >= 4 && rest[0] == ' ' && rest[1] >= '0' &&
                                    rest[1] <= '9' && rest[2] >= '0' && rest[2] <= '9' &&
                                    rest[3] >= '0' && rest[3] <= '9' &&
                                    (rest.size() == 4 || rest[4] == ' ');
                if ((!http10 && !http11) || !digits) {
                    line_.clear();
                    return fail(Failure::bad_response, "the status line could not be read");
                }
                code = (rest[1] - '0') * 100 + (rest[2] - '0') * 10 + (rest[3] - '0');
                status_ = code;
                version_ = http11 ? 11 : 10;
                line_.clear();
                phase_ = Phase::headers;
                continue;
            }
            if (line_.empty())
                return finish_headers();
            if (line_[0] == ' ' || line_[0] == '\t')
                return fail(Failure::bad_response, "a header line is folded");
            const std::size_t colon = line_.find(':');
            if (colon == std::string::npos || colon == 0) {
                line_.clear();
                return fail(Failure::bad_response, "a header could not be read");
            }
            Header header;
            header.name = line_.substr(0, colon);
            for (const char byte : header.name) {
                const unsigned char value = static_cast<unsigned char>(byte);
                if (value <= 32 || value == 127 || value == ':') {
                    line_.clear();
                    return fail(Failure::bad_response, "a header could not be read");
                }
            }
            header.value = std::string(trim_ows(std::string_view(line_).substr(colon + 1)));
            line_.clear();
            if (headers_.size() >= 100)
                return fail(Failure::bad_response, "the response has too many headers");
            headers_.push_back(std::move(header));
            continue;
        }
        case Phase::counted: {
            if (body_remaining_ == 0) {
                phase_ = Phase::done;
                complete_ = true;
                return succeed();
            }
            if (taken >= incoming.size())
                return ReadPiece{ReadKind::need_more, {}};
            if (body_emitted_ >= limits_.max_body_bytes)
                return fail(Failure::too_large, "the body is larger than the limit");
            const uint64_t room = limits_.max_body_bytes - body_emitted_;
            const uint64_t want = body_remaining_ < room ? body_remaining_ : room;
            const std::size_t n = take_count(incoming.size() - taken, want);
            if (n == 0)
                return fail(Failure::too_large, "the body is larger than the limit");
            emit_.assign(
                incoming.begin() + static_cast<std::ptrdiff_t>(taken),
                incoming.begin() + static_cast<std::ptrdiff_t>(taken + n)
            );
            taken += n;
            body_remaining_ -= n;
            body_emitted_ += n;
            if (body_remaining_ == 0) {
                phase_ = Phase::done;
                complete_ = true;
            }
            return ReadPiece{ReadKind::body, std::span<const uint8_t>(emit_.data(), emit_.size())};
        }
        case Phase::chunk_size: {
            const LineTake got =
                take_line(incoming, taken, section_bytes_, limits_.max_header_bytes);
            if (got == LineTake::need_more)
                return ReadPiece{ReadKind::need_more, {}};
            if (got == LineTake::too_large)
                return fail(Failure::too_large, "the headers are larger than the limit");
            uint64_t size = 0;
            std::size_t used = 0;
            const bool digits = parse_hex(line_, size, used);
            const bool tail = digits && (used == line_.size() || line_[used] == ';');
            line_.clear();
            section_bytes_ = 0;
            if (!tail)
                return fail(Failure::bad_response, "the chunk size could not be read");
            if (size == 0) {
                phase_ = Phase::trailers;
                continue;
            }
            if (size > limits_.max_body_bytes || body_emitted_ > limits_.max_body_bytes - size)
                return fail(Failure::too_large, "the body is larger than the limit");
            chunk_remaining_ = size;
            phase_ = Phase::chunk_data;
            continue;
        }
        case Phase::chunk_data: {
            if (chunk_remaining_ == 0) {
                phase_ = Phase::chunk_cr;
                continue;
            }
            if (taken >= incoming.size())
                return ReadPiece{ReadKind::need_more, {}};
            if (body_emitted_ >= limits_.max_body_bytes)
                return fail(Failure::too_large, "the body is larger than the limit");
            const uint64_t room = limits_.max_body_bytes - body_emitted_;
            const uint64_t want = chunk_remaining_ < room ? chunk_remaining_ : room;
            const std::size_t n = take_count(incoming.size() - taken, want);
            if (n == 0)
                return fail(Failure::too_large, "the body is larger than the limit");
            emit_.assign(
                incoming.begin() + static_cast<std::ptrdiff_t>(taken),
                incoming.begin() + static_cast<std::ptrdiff_t>(taken + n)
            );
            taken += n;
            chunk_remaining_ -= n;
            body_emitted_ += n;
            if (chunk_remaining_ == 0)
                phase_ = Phase::chunk_cr;
            return ReadPiece{ReadKind::body, std::span<const uint8_t>(emit_.data(), emit_.size())};
        }
        case Phase::chunk_cr: {
            if (taken >= incoming.size())
                return ReadPiece{ReadKind::need_more, {}};
            const uint8_t byte = incoming[taken++];
            if (byte == '\n') {
                phase_ = Phase::chunk_size;
                continue;
            }
            if (byte == '\r') {
                phase_ = Phase::chunk_lf;
                continue;
            }
            return fail(Failure::bad_response, "the chunk size could not be read");
        }
        case Phase::chunk_lf: {
            if (taken >= incoming.size())
                return ReadPiece{ReadKind::need_more, {}};
            const uint8_t byte = incoming[taken++];
            if (byte != '\n')
                return fail(Failure::bad_response, "the chunk size could not be read");
            phase_ = Phase::chunk_size;
            continue;
        }
        case Phase::trailers: {
            const LineTake got =
                take_line(incoming, taken, section_bytes_, limits_.max_header_bytes);
            if (got == LineTake::need_more)
                return ReadPiece{ReadKind::need_more, {}};
            if (got == LineTake::too_large)
                return fail(Failure::too_large, "the headers are larger than the limit");
            if (!line_.empty() && (line_[0] == ' ' || line_[0] == '\t'))
                return fail(Failure::bad_response, "a header line is folded");
            if (line_.empty()) {
                line_.clear();
                return succeed();
            }
            const std::size_t colon = line_.find(':');
            line_.clear();
            if (colon == std::string::npos || colon == 0)
                return fail(Failure::bad_response, "a header could not be read");
            continue;
        }
        case Phase::until_close: {
            if (taken >= incoming.size())
                return ReadPiece{ReadKind::need_more, {}};
            if (body_emitted_ >= limits_.max_body_bytes)
                return fail(Failure::too_large, "the body is larger than the limit");
            const uint64_t room = limits_.max_body_bytes - body_emitted_;
            const std::size_t n = take_count(incoming.size() - taken, room);
            if (n == 0)
                return fail(Failure::too_large, "the body is larger than the limit");
            emit_.assign(
                incoming.begin() + static_cast<std::ptrdiff_t>(taken),
                incoming.begin() + static_cast<std::ptrdiff_t>(taken + n)
            );
            taken += n;
            body_emitted_ += n;
            return ReadPiece{ReadKind::body, std::span<const uint8_t>(emit_.data(), emit_.size())};
        }
        case Phase::done:
            return succeed();
        case Phase::failed:
            return ReadPiece{ReadKind::failed, {}};
        }
    }
}

ReadPiece ResponseReader::finish() {
    if (phase_ == Phase::failed)
        return ReadPiece{ReadKind::failed, {}};
    if (phase_ == Phase::done)
        return succeed();
    if (phase_ == Phase::until_close) {
        close_delimited_ = true;
        keep_alive_ = false;
        return succeed();
    }
    const bool in_body = phase_ == Phase::counted || phase_ == Phase::chunk_size ||
                         phase_ == Phase::chunk_data || phase_ == Phase::chunk_cr ||
                         phase_ == Phase::chunk_lf || phase_ == Phase::trailers;
    if (in_body)
        return fail(Failure::connection_lost, "the connection closed before the body ended");
    return fail(Failure::connection_lost, "the connection closed before the response");
}

} // namespace oa::netgame::http
