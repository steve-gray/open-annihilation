// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// HTTP/1.1 on 127.0.0.1 for tests. One thread waits on the listener and the
// open connections. A delayed or paced reply is a time at which that
// connection's bytes may be written, so it never sleeps the thread and never
// holds up another connection, or stop.

#include "oa/netgame/http_fixture/server.hpp"

#include "oa/base/threads.hpp"
#include "oa/netgame/stream_socket.hpp"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <utility>

namespace oa::netgame::http_fixture {
namespace {

namespace sock = oa::netgame::sock;
namespace threads = oa::base::threads;

constexpr std::size_t max_header_bytes = 16 * 1024;
constexpr std::size_t max_body_bytes = 4 * 1024 * 1024;
constexpr uint32_t wait_slice_ms = 20;
constexpr uint32_t default_idle_ms = 5000;
constexpr std::size_t max_connections = sock::stream_wait_most - 1;

/// One folder served under a URL prefix.
struct FolderRoute {
    std::filesystem::path root;
    std::string prefix;
};

/// One byte string served at a path.
struct BytesRoute {
    std::string path;
    std::vector<uint8_t> bytes;
    std::string content_type;
};

/// One function served at a method and path.
struct HandlerRoute {
    std::string method;
    std::string path;
    Handler handler;
};

/// One redirect.
struct RedirectRoute {
    std::string from;
    std::string to;
    int status{302};
};

/// A reply queued for the next requests to a path.
struct OverrideRoute {
    std::string path;
    Reply reply;
    int times{0};
};

/// Bytes waiting to be written on one connection, and when they may go.
struct Outgoing {
    std::vector<uint8_t> bytes;
    std::size_t sent{0};
    std::size_t limit{0};
    std::size_t header_bytes{0};
    std::size_t piece_bytes{0};
    std::size_t piece_sent{0};
    uint32_t piece_delay_ms{0};
    uint64_t ready_at{0};
    bool close_after{false};
    bool active{false};
};

/// One accepted connection.
struct Conn {
    intptr_t fd{sock::invalid_socket};
    uint64_t id{0};
    std::vector<uint8_t> inbound;
    Outgoing outgoing{};
    uint64_t last_ms{0};
    bool reading{true};
    bool peer_done{false};
    bool dead{false};
};

/// What the tables say to do with a request.
enum class PlanKind : uint8_t {
    reply,
    handler,
    read_file,
    method_not_allowed,
    missing,
};

/// The reply, or the file still to be read, for one request.
struct Plan {
    PlanKind kind{PlanKind::missing};
    Reply reply{};
    Handler handler{};
    std::filesystem::path file;
    std::string content_type;
    bool ranges{true};
};

/// Appends characters to a byte buffer.
///
/// @param[in,out] out the buffer
/// @param text the characters
void append_text(std::vector<uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

/// Returns a steady-clock reading in milliseconds.
///
/// @return milliseconds since the clock's epoch
uint64_t now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()
    )
                                     .count());
}

/// Folds one ASCII letter to lower case.
///
/// @param c the character
/// @return the lower-case letter, or `c` when it is not an ASCII letter
char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c - 'A' + 'a');
    return c;
}

/// Compares two strings without regard to ASCII case.
///
/// @param left one string
/// @param right the other
/// @return true when they match
bool ascii_ieq(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (ascii_lower(left[i]) != ascii_lower(right[i]))
            return false;
    }
    return true;
}

/// Drops leading and trailing spaces and tabs.
///
/// @param text the text
/// @return the text inside the spaces
std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

/// Finds a header by name, ignoring ASCII case.
///
/// @param headers the headers
/// @param name the name
/// @return the value, or null when none matches
const std::string* find_header(const std::vector<Header>& headers, std::string_view name) {
    for (const Header& header : headers) {
        if (ascii_ieq(header.name, name))
            return &header.value;
    }
    return nullptr;
}

/// Tells whether a header's value lists a token.
///
/// @param headers the headers
/// @param name the header name
/// @param token the token
/// @return true when a comma-separated part matches the token
bool header_has_token(
    const std::vector<Header>& headers, std::string_view name, std::string_view token
) {
    const std::string* value = find_header(headers, name);
    if (value == nullptr)
        return false;
    std::size_t begin = 0;
    while (begin <= value->size()) {
        const std::size_t comma = value->find(',', begin);
        const std::string_view part(*value);
        const std::string_view item = trim(
            part.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin)
        );
        if (ascii_ieq(item, token))
            return true;
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return false;
}

/// The reason phrase sent with a status code.
///
/// @param status the status code
/// @return the phrase
std::string_view reason_phrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 206:
        return "Partial Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 307:
        return "Temporary Redirect";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 416:
        return "Range Not Satisfiable";
    case 428:
        return "Precondition Required";
    case 500:
        return "Internal Server Error";
    default:
        return "Response";
    }
}

/// How a decimal number was read.
enum class NumberRead : uint8_t { ok, bad, overflow };

/// Reads an unsigned decimal number.
///
/// @param text the digits
/// @param[out] value the number; unchanged unless the result is ok
/// @return ok, bad when a character is not a digit, or overflow
NumberRead parse_u64(std::string_view text, uint64_t& value) {
    if (text.empty())
        return NumberRead::bad;
    uint64_t parsed = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            return NumberRead::bad;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (parsed > (UINT64_MAX - digit) / 10)
            return NumberRead::overflow;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return NumberRead::ok;
}

/// Returns the target without its query.
///
/// @param target the request target
/// @return the path
std::string path_without_query(std::string_view target) {
    const std::size_t query = target.find('?');
    return std::string(target.substr(0, query));
}

/// Splits a URL path into the part under a prefix.
///
/// @param path the URL path
/// @param prefix the prefix
/// @param[out] relative the path after the prefix, without a leading slash
/// @return true when `path` is under the prefix
bool split_prefix(std::string_view path, std::string_view prefix, std::string_view& relative) {
    if (prefix.empty())
        prefix = "/";
    if (prefix.back() == '/' && prefix.size() > 1)
        prefix.remove_suffix(1);
    if (prefix == "/") {
        if (path.empty() || path.front() != '/')
            return false;
        relative = path.substr(1);
        return true;
    }
    if (!path.starts_with(prefix))
        return false;
    if (path.size() == prefix.size()) {
        relative = {};
        return true;
    }
    if (path[prefix.size()] != '/')
        return false;
    relative = path.substr(prefix.size() + 1);
    return true;
}

/// Tells whether a folder-relative path must be refused.
///
/// @param relative the path under the folder
/// @return true when it contains `..`, a backslash or a NUL
bool folder_path_rejected(std::string_view relative) {
    return relative.find("..") != std::string_view::npos ||
           relative.find('\\') != std::string_view::npos ||
           relative.find('\0') != std::string_view::npos;
}

/// Tells whether a path stays inside a folder after dot segments are removed.
///
/// @param root the folder
/// @param full the joined path
/// @return true when `full` is inside `root`
bool inside_root(const std::filesystem::path& root, const std::filesystem::path& full) {
    const std::filesystem::path relative =
        full.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty())
        return false;
    for (const std::filesystem::path& part : relative) {
        if (part == "..")
            return false;
    }
    return true;
}

/// The Content-Type a file name selects.
///
/// @param name the URL path or file name
/// @return the media type
std::string content_type_for(std::string_view name) {
    const std::size_t slash = name.rfind('/');
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return "application/octet-stream";
    std::string ext;
    ext.reserve(name.size() - dot);
    for (const char c : name.substr(dot))
        ext.push_back(ascii_lower(c));
    if (ext == ".json")
        return "application/json";
    if (ext == ".png")
        return "image/png";
    if (ext == ".sig" || ext == ".txt")
        return "text/plain";
    if (ext == ".yaml" || ext == ".oareg")
        return "application/yaml";
    return "application/octet-stream";
}

/// Reads a whole file.
///
/// @param path the file
/// @param[out] out the bytes; cleared on failure
/// @return true when the file was read
bool read_file_bytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    out.clear();
    try {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            return false;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        in.seekg(0, std::ios::end);
        const auto end = in.tellg();
        if (end < 0)
            return false;
        in.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(end));
        if (end > 0 && !in.read(reinterpret_cast<char*>(out.data()), end)) {
            out.clear();
            return false;
        }
        return true;
    } catch (const std::exception&) {
        out.clear();
        return false;
    }
}

/// Compresses bytes as a gzip stream.
///
/// @param bytes the data
/// @return the gzip stream, empty when compression failed
std::vector<uint8_t> gzip_bytes(const std::vector<uint8_t>& bytes) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK)
        return {};
    if (bytes.size() > static_cast<std::size_t>(UINT_MAX)) {
        deflateEnd(&stream);
        return {};
    }
    const uLong bound = deflateBound(&stream, static_cast<uLong>(bytes.size()));
    if (bound > static_cast<uLong>(UINT_MAX)) {
        deflateEnd(&stream);
        return {};
    }
    std::vector<uint8_t> out(bound);
    Bytef empty = 0;
    stream.next_in = bytes.empty() ? &empty : const_cast<Bytef*>(bytes.data());
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    if (result != Z_STREAM_END)
        return {};
    return out;
}

/// Appends a hexadecimal size with no leading zeros.
///
/// @param[in,out] out the buffer
/// @param value the size
void append_hex(std::vector<uint8_t>& out, std::size_t value) {
    if (value == 0) {
        out.push_back('0');
        return;
    }
    char digits[sizeof(std::size_t) * 2];
    int count = 0;
    while (value > 0) {
        const std::size_t nibble = value & 0xfu;
        digits[count++] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
        value >>= 4;
    }
    while (count > 0)
        out.push_back(static_cast<uint8_t>(digits[--count]));
}

/// Wraps a body in chunked transfer coding.
///
/// @param body the body
/// @param chunk_bytes the most bytes of one chunk
/// @return the chunked body, including the last chunk
std::vector<uint8_t> chunk_body(const std::vector<uint8_t>& body, std::size_t chunk_bytes) {
    if (chunk_bytes == 0)
        chunk_bytes = 1;
    std::vector<uint8_t> out;
    std::size_t offset = 0;
    while (offset < body.size()) {
        const std::size_t n = std::min(chunk_bytes, body.size() - offset);
        append_hex(out, n);
        append_text(out, "\r\n");
        out.insert(
            out.end(),
            body.begin() + static_cast<std::ptrdiff_t>(offset),
            body.begin() + static_cast<std::ptrdiff_t>(offset + n)
        );
        append_text(out, "\r\n");
        offset += n;
    }
    append_text(out, "0\r\n\r\n");
    return out;
}

/// What one Range header asked for.
enum class RangeKind : uint8_t { ignore, unsatisfiable, slice };

/// The bytes one Range header selects.
struct RangeDecision {
    RangeKind kind{RangeKind::ignore};
    std::vector<uint8_t> slice;
    std::string content_range;
};

/// Applies one `bytes` range to a body.
///
/// Several ranges, a suffix range and a malformed header are ignored. A start
/// at or past the end cannot be satisfied.
///
/// @param header the Range value
/// @param body the whole body
/// @return what to send
RangeDecision apply_range(std::string_view header, const std::vector<uint8_t>& body) {
    RangeDecision decision;
    const std::string_view value = trim(header);
    if (value.find(',') != std::string_view::npos)
        return decision;
    constexpr std::string_view unit = "bytes=";
    if (!value.starts_with(unit))
        return decision;
    const std::string_view spec = value.substr(unit.size());
    const std::size_t dash = spec.find('-');
    if (dash == std::string_view::npos || spec.find('-', dash + 1) != std::string_view::npos)
        return decision;
    const std::string_view start_text = spec.substr(0, dash);
    const std::string_view end_text = spec.substr(dash + 1);
    if (start_text.empty())
        return decision;
    uint64_t start = 0;
    const NumberRead start_read = parse_u64(start_text, start);
    if (start_read == NumberRead::bad)
        return decision;
    if (start_read == NumberRead::overflow || start >= body.size()) {
        decision.kind = RangeKind::unsatisfiable;
        decision.content_range = "bytes */" + std::to_string(body.size());
        return decision;
    }
    const std::size_t from = static_cast<std::size_t>(start);
    std::size_t to = body.size() - 1;
    if (!end_text.empty()) {
        uint64_t end = 0;
        const NumberRead end_read = parse_u64(end_text, end);
        if (end_read == NumberRead::bad)
            return decision;
        if (end_read == NumberRead::ok && end < start)
            return decision;
        if (end_read == NumberRead::ok && end < body.size())
            to = static_cast<std::size_t>(end);
    }
    decision.kind = RangeKind::slice;
    decision.slice.assign(
        body.begin() + static_cast<std::ptrdiff_t>(from),
        body.begin() + static_cast<std::ptrdiff_t>(to + 1)
    );
    decision.content_range = "bytes " + std::to_string(from) + "-" + std::to_string(to) + "/" +
                             std::to_string(body.size());
    return decision;
}

/// Builds a file or byte-string reply, honouring one range when asked.
///
/// @param body the whole body
/// @param content_type the Content-Type
/// @param request the request
/// @param ranges whether a single range is honoured
/// @return the reply, still carrying its body when the method is HEAD
Reply reply_for_bytes(
    std::vector<uint8_t> body, std::string content_type, const LoggedRequest& request, bool ranges
) {
    Reply reply;
    Header type;
    type.name = "Content-Type";
    type.value = std::move(content_type);
    reply.headers.push_back(std::move(type));
    if (ranges) {
        if (const std::string* range = request.header("Range")) {
            RangeDecision decision = apply_range(*range, body);
            if (decision.kind == RangeKind::slice) {
                reply.status = 206;
                Header content_range;
                content_range.name = "Content-Range";
                content_range.value = std::move(decision.content_range);
                reply.headers.push_back(std::move(content_range));
                body = std::move(decision.slice);
            } else if (decision.kind == RangeKind::unsatisfiable) {
                reply.status = 416;
                Header content_range;
                content_range.name = "Content-Range";
                content_range.value = "bytes */" + std::to_string(body.size());
                reply.headers.push_back(std::move(content_range));
                body.clear();
            }
        }
    }
    reply.body = std::move(body);
    return reply;
}

/// A fixed reply with an empty body.
///
/// @param status the status code
/// @param close_after true to close after the response
/// @return the reply
Reply empty_reply(int status, bool close_after) {
    Reply reply;
    reply.status = status;
    reply.close = close_after;
    return reply;
}

/// The reply that refuses a method on a file that is there.
///
/// @return status 405
Reply method_not_allowed() {
    Reply reply;
    reply.status = 405;
    Header allow;
    allow.name = "Allow";
    allow.value = "GET, HEAD";
    reply.headers.push_back(std::move(allow));
    return reply;
}

/// Tells whether the request asks the connection to end.
///
/// @param request the request
/// @return true for `Connection: close`, and for HTTP/1.0 without keep-alive
bool request_closes(const LoggedRequest& request) {
    if (header_has_token(request.headers, "Connection", "close"))
        return true;
    return request.version == "HTTP/1.0" &&
           !header_has_token(request.headers, "Connection", "keep-alive");
}

/// The bytes to write for one reply, and how to pace them.
struct Wire {
    std::vector<uint8_t> bytes;
    std::size_t header_bytes{0};
    std::size_t limit{0};
    std::size_t piece_bytes{0};
    uint32_t piece_delay_ms{0};
    uint32_t head_delay_ms{0};
    bool close_after{false};
};

/// Turns a reply into the bytes on the connection.
///
/// @param reply the reply
/// @param request the request it answers
/// @return the bytes, the header size and the send limit
Wire make_wire(Reply reply, const LoggedRequest& request) {
    if (reply.raw) {
        Wire wire;
        wire.bytes = std::move(*reply.raw);
        wire.header_bytes = 0;
        wire.limit = wire.bytes.size();
        wire.piece_bytes = reply.piece_bytes;
        wire.piece_delay_ms = reply.piece_delay_ms;
        wire.head_delay_ms = reply.head_delay_ms;
        wire.close_after = true;
        return wire;
    }
    if (reply.gzip) {
        std::vector<uint8_t> compressed = gzip_bytes(reply.body);
        if (compressed.empty() && !reply.body.empty())
            reply = empty_reply(500, true);
        else
            reply.body = std::move(compressed);
    }
    const bool head = request.method == "HEAD";
    const bool gzip = reply.gzip && reply.status != 500;
    std::vector<uint8_t> body = std::move(reply.body);
    const bool use_chunked = reply.chunked && !head && reply.status != 500;
    if (use_chunked)
        body = chunk_body(body, reply.chunk_bytes);

    const bool closing = request_closes(request) || reply.close || reply.no_length ||
                         reply.cut_after.has_value() ||
                         header_has_token(reply.headers, "Connection", "close");
    std::vector<Header> headers;
    for (const Header& header : reply.headers) {
        if (ascii_ieq(header.name, "Content-Length") ||
            ascii_ieq(header.name, "Transfer-Encoding") || ascii_ieq(header.name, "Connection"))
            continue;
        if (gzip && ascii_ieq(header.name, "Content-Encoding"))
            continue;
        headers.push_back(header);
    }
    if (gzip) {
        Header encoding;
        encoding.name = "Content-Encoding";
        encoding.value = "gzip";
        headers.push_back(std::move(encoding));
    }
    if (use_chunked) {
        Header transfer;
        transfer.name = "Transfer-Encoding";
        transfer.value = "chunked";
        headers.push_back(std::move(transfer));
    }
    if (!use_chunked && !reply.no_length) {
        Header length;
        length.name = "Content-Length";
        length.value = std::to_string(body.size());
        headers.push_back(std::move(length));
    }
    if (closing) {
        Header connection;
        connection.name = "Connection";
        connection.value = "close";
        headers.push_back(std::move(connection));
    } else if (request.version == "HTTP/1.0") {
        Header connection;
        connection.name = "Connection";
        connection.value = "keep-alive";
        headers.push_back(std::move(connection));
    }

    Wire wire;
    append_text(
        wire.bytes,
        "HTTP/1.1 " + std::to_string(reply.status) + " " +
            std::string(reason_phrase(reply.status)) + "\r\n"
    );
    for (const Header& header : headers) {
        append_text(wire.bytes, header.name);
        append_text(wire.bytes, ": ");
        append_text(wire.bytes, header.value);
        append_text(wire.bytes, "\r\n");
    }
    append_text(wire.bytes, "\r\n");
    wire.header_bytes = wire.bytes.size();
    if (!head)
        wire.bytes.insert(wire.bytes.end(), body.begin(), body.end());
    wire.limit = head ? wire.header_bytes : wire.bytes.size();
    if (!head && reply.cut_after.has_value()) {
        const std::size_t cut = *reply.cut_after;
        const std::size_t body_bytes = wire.bytes.size() - wire.header_bytes;
        wire.limit = wire.header_bytes + std::min(cut, body_bytes);
    }
    wire.piece_bytes = reply.piece_bytes;
    wire.piece_delay_ms = reply.piece_delay_ms;
    wire.head_delay_ms = reply.head_delay_ms;
    wire.close_after = closing || reply.cut_after.has_value();
    return wire;
}

/// How much of a request the buffer holds.
enum class Take : uint8_t { need_more, ready, bad };

/// Takes one request from a connection's buffer.
///
/// @param[in,out] inbound the bytes read so far; a taken request is removed
/// @param[out] request the request, when one was complete
/// @return ready, bad, or need_more
Take take_request(std::vector<uint8_t>& inbound, LoggedRequest& request) {
    const std::string_view bytes(reinterpret_cast<const char*>(inbound.data()), inbound.size());
    const std::size_t end = bytes.find("\r\n\r\n");
    if (end == std::string_view::npos) {
        if (bytes.size() > max_header_bytes)
            return Take::bad;
        return Take::need_more;
    }
    if (end + 4 > max_header_bytes)
        return Take::bad;
    const std::string_view block = bytes.substr(0, end);
    std::vector<std::string_view> lines;
    std::size_t line_at = 0;
    while (line_at <= block.size()) {
        const std::size_t next = block.find("\r\n", line_at);
        if (next == std::string_view::npos) {
            lines.push_back(block.substr(line_at));
            break;
        }
        lines.push_back(block.substr(line_at, next - line_at));
        line_at = next + 2;
    }
    if (lines.empty())
        return Take::bad;
    const std::string_view request_line = lines[0];
    const std::size_t first_space = request_line.find(' ');
    const std::size_t second_space = first_space == std::string_view::npos
                                         ? std::string_view::npos
                                         : request_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
        request_line.find(' ', second_space + 1) != std::string_view::npos)
        return Take::bad;
    LoggedRequest parsed;
    parsed.method = std::string(request_line.substr(0, first_space));
    parsed.target =
        std::string(request_line.substr(first_space + 1, second_space - first_space - 1));
    parsed.version = std::string(request_line.substr(second_space + 1));
    if (parsed.method.empty() || parsed.target.empty() ||
        (parsed.version != "HTTP/1.0" && parsed.version != "HTTP/1.1"))
        return Take::bad;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (line.empty())
            return Take::bad;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0)
            return Take::bad;
        const std::string_view name = line.substr(0, colon);
        if (name.find(' ') != std::string_view::npos || name.find('\t') != std::string_view::npos)
            return Take::bad;
        Header header;
        header.name = std::string(name);
        header.value = std::string(trim(line.substr(colon + 1)));
        if (ascii_ieq(header.name, "Content-Length") &&
            find_header(parsed.headers, "Content-Length") != nullptr)
            return Take::bad;
        parsed.headers.push_back(std::move(header));
    }
    if (find_header(parsed.headers, "Transfer-Encoding") != nullptr)
        return Take::bad;
    uint64_t length = 0;
    if (const std::string* header = find_header(parsed.headers, "Content-Length")) {
        const NumberRead read = parse_u64(trim(*header), length);
        if (read != NumberRead::ok || length > max_body_bytes)
            return Take::bad;
    }
    const std::size_t total = end + 4 + static_cast<std::size_t>(length);
    if (inbound.size() < total)
        return Take::need_more;
    parsed.body.assign(
        inbound.begin() + static_cast<std::ptrdiff_t>(end + 4),
        inbound.begin() + static_cast<std::ptrdiff_t>(total)
    );
    inbound.erase(inbound.begin(), inbound.begin() + static_cast<std::ptrdiff_t>(total));
    request = std::move(parsed);
    return Take::ready;
}

/// How many bytes may be written now.
///
/// @param out the outgoing bytes
/// @return the bytes of this write; headers stop at the body, and a paced body stops at a piece
std::size_t send_budget(const Outgoing& out) {
    if (out.sent >= out.limit)
        return 0;
    if (out.sent < out.header_bytes)
        return std::min(out.header_bytes - out.sent, out.limit - out.sent);
    const std::size_t remain = out.limit - out.sent;
    if (out.piece_bytes == 0)
        return remain;
    if (out.piece_sent >= out.piece_bytes)
        return 0;
    return std::min(out.piece_bytes - out.piece_sent, remain);
}

/// Queues a wire on a connection.
///
/// @param[in,out] conn the connection
/// @param wire the bytes to send
/// @param now the time, in milliseconds
void arm(Conn& conn, Wire wire, uint64_t now) {
    conn.reading = false;
    Outgoing outgoing;
    outgoing.bytes = std::move(wire.bytes);
    outgoing.header_bytes = std::min(wire.header_bytes, outgoing.bytes.size());
    outgoing.limit = std::min(wire.limit, outgoing.bytes.size());
    outgoing.piece_bytes = wire.piece_bytes;
    outgoing.piece_delay_ms = wire.piece_delay_ms;
    outgoing.ready_at = now + wire.head_delay_ms;
    outgoing.close_after = wire.close_after;
    outgoing.active = true;
    conn.outgoing = std::move(outgoing);
}

/// Closes a connection's socket.
///
/// @param[in,out] conn the connection
void drop_conn(Conn& conn) {
    sock::stream_close(&conn.fd);
    conn.dead = true;
}

} // namespace

struct Server::State {
    /// Runs until stop is set, then closes the listener and the connections.
    void run();

    /// Chooses the reply for a request. The lock is not held on return.
    ///
    /// @param request the request
    /// @return the plan
    Plan plan_for(const LoggedRequest& request);

    /// Parses a buffered request and queues its reply.
    ///
    /// @param[in,out] conn the connection
    /// @param now the time, in milliseconds
    void take_and_answer(Conn& conn, uint64_t now);

    /// The thread entry: runs the server until stop.
    ///
    /// @param argument the server state
    static void run_server(void* argument);

    mutable threads::Mutex mu{};
    std::atomic<bool> stop{false};
    threads::Thread thread{};
    bool thread_started{false};
    intptr_t listener{sock::invalid_socket};
    uint16_t bound_port{0};
    std::atomic<uint64_t> accepted{0};
    std::vector<FolderRoute> folders;
    std::vector<BytesRoute> files;
    std::vector<HandlerRoute> handlers;
    std::vector<RedirectRoute> redirects;
    std::vector<OverrideRoute> overrides;
    std::vector<LoggedRequest> log;
    bool ranges{true};
    uint32_t idle_ms{default_idle_ms};
};

/// Chooses the reply for a request from the tables.
///
/// An override comes first, then a handler, a redirect, a byte string, a
/// file, and otherwise the request is missing. The lock is not held on return.
///
/// @param request the request
/// @return the plan
Plan Server::State::plan_for(const LoggedRequest& request) {
    const std::string path = path_without_query(request.target);
    Plan plan;
    std::string relative;
    std::filesystem::path root;
    {
        threads::LockGuard guard(mu);
        plan.ranges = ranges;
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            if (it->path == path && it->times > 0) {
                plan.kind = PlanKind::reply;
                plan.reply = it->reply;
                if (--it->times <= 0)
                    overrides.erase(it);
                return plan;
            }
        }
        for (const HandlerRoute& route : handlers) {
            if (route.method == request.method && route.path == path) {
                plan.kind = PlanKind::handler;
                plan.handler = route.handler;
                return plan;
            }
        }
        for (const RedirectRoute& route : redirects) {
            if (route.from == path) {
                plan.kind = PlanKind::reply;
                plan.reply.status = route.status;
                Header location;
                location.name = "Location";
                location.value = route.to;
                plan.reply.headers.push_back(std::move(location));
                return plan;
            }
        }
        for (const BytesRoute& route : files) {
            if (route.path != path)
                continue;
            if (request.method != "GET" && request.method != "HEAD") {
                plan.kind = PlanKind::method_not_allowed;
                return plan;
            }
            plan.kind = PlanKind::reply;
            plan.reply = reply_for_bytes(route.bytes, route.content_type, request, ranges);
            return plan;
        }
        const FolderRoute* best = nullptr;
        std::string_view best_relative;
        for (const FolderRoute& route : folders) {
            std::string_view part;
            if (!split_prefix(path, route.prefix, part))
                continue;
            if (best != nullptr && route.prefix.size() < best->prefix.size())
                continue;
            best = &route;
            best_relative = part;
        }
        if (best == nullptr) {
            plan.kind = PlanKind::missing;
            return plan;
        }
        if (folder_path_rejected(best_relative) || best_relative.empty()) {
            plan.kind = PlanKind::missing;
            return plan;
        }
        relative = std::string(best_relative);
        root = best->root;
        plan.content_type = content_type_for(best_relative);
        plan.ranges = ranges;
    }
    try {
        const std::filesystem::path full = root / std::filesystem::path(relative);
        if (!inside_root(root, full)) {
            plan.kind = PlanKind::missing;
            return plan;
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(full, error)) {
            plan.kind = PlanKind::missing;
            return plan;
        }
        if (request.method != "GET" && request.method != "HEAD") {
            plan.kind = PlanKind::method_not_allowed;
            return plan;
        }
        plan.kind = PlanKind::read_file;
        plan.file = full;
        return plan;
    } catch (const std::exception&) {
        plan.kind = PlanKind::missing;
        return plan;
    }
}

namespace {

/// Builds the wire for a planned request.
///
/// @param plan the plan
/// @param request the request
/// @return the wire
Wire wire_for(Plan plan, const LoggedRequest& request) {
    if (plan.kind == PlanKind::handler) {
        Reply reply = empty_reply(500, true);
        if (plan.handler) {
            try {
                reply = plan.handler(request);
            } catch (const std::exception&) {
                reply = empty_reply(500, true);
            }
        }
        return make_wire(std::move(reply), request);
    }
    if (plan.kind == PlanKind::read_file) {
        std::vector<uint8_t> body;
        if (!read_file_bytes(plan.file, body))
            return make_wire(empty_reply(404, false), request);
        return make_wire(
            reply_for_bytes(std::move(body), std::move(plan.content_type), request, plan.ranges),
            request
        );
    }
    if (plan.kind == PlanKind::method_not_allowed)
        return make_wire(method_not_allowed(), request);
    if (plan.kind == PlanKind::missing)
        return make_wire(empty_reply(404, false), request);
    return make_wire(std::move(plan.reply), request);
}

} // namespace

/// Parses a buffered request and queues its reply.
///
/// @param state the server
/// @param[in,out] conn the connection
/// @param now the time, in milliseconds
void Server::State::take_and_answer(Conn& conn, uint64_t now) {
    if (!conn.reading || conn.outgoing.active || conn.dead)
        return;
    LoggedRequest request;
    const Take taken = take_request(conn.inbound, request);
    if (taken == Take::need_more)
        return;
    if (taken == Take::bad) {
        arm(conn, make_wire(empty_reply(400, true), LoggedRequest{}), now);
        return;
    }
    request.connection = conn.id;
    {
        threads::LockGuard guard(mu);
        log.push_back(request);
    }
    arm(conn, wire_for(plan_for(request), request), now);
}

namespace {

/// Writes whatever of a connection's reply may go now.
///
/// A piece that the system will not take yet is postponed by one wait slice.
/// The thread is not slept for the reply's own delay.
///
/// @param[in,out] conn the connection
/// @param now the time, in milliseconds
void write_outgoing(Conn& conn, uint64_t now) {
    Outgoing& out = conn.outgoing;
    if (!out.active || conn.dead || now < out.ready_at)
        return;
    if (out.limit == 0 || out.sent >= out.limit) {
        out.active = false;
        if (out.close_after || conn.peer_done)
            drop_conn(conn);
        else {
            conn.reading = true;
            conn.last_ms = now;
        }
        return;
    }
    while (out.active && out.sent < out.limit && now >= out.ready_at) {
        const std::size_t budget = send_budget(out);
        if (budget == 0)
            return;
        const std::ptrdiff_t wrote =
            sock::stream_write(conn.fd, out.bytes.data() + out.sent, budget);
        if (wrote == sock::stream_failed) {
            drop_conn(conn);
            return;
        }
        if (wrote == 0) {
            out.ready_at = now_ms() + wait_slice_ms;
            return;
        }
        const std::size_t sent_before = out.sent;
        out.sent += static_cast<std::size_t>(wrote);
        conn.last_ms = now_ms();
        if (out.piece_bytes > 0 && out.sent > out.header_bytes) {
            const std::size_t body_before =
                sent_before > out.header_bytes ? sent_before : out.header_bytes;
            out.piece_sent += out.sent - body_before;
            if (out.piece_sent >= out.piece_bytes && out.sent < out.limit) {
                out.piece_sent = 0;
                out.ready_at = now_ms() + out.piece_delay_ms;
                return;
            }
        }
        now = now_ms();
        if (out.sent >= out.limit)
            break;
    }
    if (out.sent >= out.limit) {
        out.active = false;
        if (out.close_after || conn.peer_done)
            drop_conn(conn);
        else {
            conn.reading = true;
            conn.last_ms = now_ms();
        }
    }
}

/// Reads what has arrived on a connection.
///
/// @param[in,out] conn the connection
void read_incoming(Conn& conn) {
    if (conn.dead)
        return;
    for (;;) {
        uint8_t buffer[4096];
        const std::ptrdiff_t got = sock::stream_read(conn.fd, buffer, sizeof buffer);
        if (got == 0)
            return;
        if (got == sock::stream_ended) {
            conn.peer_done = true;
            return;
        }
        if (got == sock::stream_failed) {
            drop_conn(conn);
            return;
        }
        if (conn.inbound.size() + static_cast<std::size_t>(got) >
            max_header_bytes + max_body_bytes) {
            drop_conn(conn);
            return;
        }
        conn.inbound.insert(conn.inbound.end(), buffer, buffer + got);
        conn.last_ms = now_ms();
    }
}

} // namespace

/// The thread entry: runs the server until stop.
///
/// @param argument the server state
void Server::State::run_server(void* argument) {
    static_cast<Server::State*>(argument)->run();
}

void Server::State::run() {
    std::vector<Conn> connections;
    while (!stop.load()) {
        uint32_t idle = default_idle_ms;
        {
            threads::LockGuard guard(mu);
            idle = idle_ms;
        }
        const uint64_t now = now_ms();
        sock::StreamWaitEntry entries[sock::stream_wait_most]{};
        int which[sock::stream_wait_most]{};
        std::size_t count = 0;
        if (connections.size() < max_connections && listener != sock::invalid_socket) {
            entries[count].stream = listener;
            entries[count].read = true;
            which[count] = -1;
            ++count;
        }
        for (std::size_t i = 0; i < connections.size() && count < sock::stream_wait_most; ++i) {
            Conn& conn = connections[i];
            if (conn.dead || conn.fd == sock::invalid_socket)
                continue;
            entries[count].stream = conn.fd;
            entries[count].read = true;
            entries[count].write = conn.outgoing.active && now >= conn.outgoing.ready_at &&
                                   conn.outgoing.sent < conn.outgoing.limit;
            which[count] = static_cast<int>(i);
            ++count;
        }
        if (count > 0) {
            const int ready = sock::stream_wait(entries, count, wait_slice_ms);
            if (ready < 0) {
                // The wait failed, so take the same slice without spinning.
                threads::sleep_ms(wait_slice_ms);
                continue;
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (which[i] != -1)
                    continue;
                if (!entries[i].readable)
                    continue;
                while (connections.size() < max_connections) {
                    const intptr_t fd = sock::stream_accept(listener);
                    if (fd == sock::invalid_socket)
                        break;
                    Conn conn;
                    conn.fd = fd;
                    conn.id = accepted.fetch_add(1) + 1;
                    conn.last_ms = now_ms();
                    connections.push_back(std::move(conn));
                }
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (which[i] < 0 || static_cast<std::size_t>(which[i]) >= connections.size())
                    continue;
                Conn& conn = connections[static_cast<std::size_t>(which[i])];
                if (entries[i].failed && !entries[i].readable && !entries[i].writable) {
                    drop_conn(conn);
                    continue;
                }
                if (entries[i].readable)
                    read_incoming(conn);
                if (entries[i].writable)
                    write_outgoing(conn, now_ms());
            }
        }
        for (Conn& conn : connections) {
            if (conn.dead)
                continue;
            const uint64_t after = now_ms();
            take_and_answer(conn, after);
            const uint64_t sent_at = now_ms();
            if (conn.outgoing.active && sent_at >= conn.outgoing.ready_at)
                write_outgoing(conn, sent_at);
            if (conn.dead)
                continue;
            if (conn.reading && !conn.outgoing.active && conn.peer_done) {
                drop_conn(conn);
                continue;
            }
            const uint64_t idle_at = now_ms();
            if (conn.reading && !conn.outgoing.active && idle_at >= conn.last_ms &&
                idle_at - conn.last_ms >= idle)
                drop_conn(conn);
        }
        std::erase_if(connections, [](const Conn& conn) {
            return conn.dead || conn.fd == sock::invalid_socket;
        });
    }
    for (Conn& conn : connections)
        drop_conn(conn);
    connections.clear();
    sock::stream_close(&listener);
}

const std::string* LoggedRequest::header(std::string_view name) const {
    return find_header(headers, name);
}

Server::Server() : state_(std::make_unique<State>()) {
}

Server::~Server() {
    stop();
}

bool Server::start(std::string* error) {
    return start(0, error);
}

bool Server::start(uint16_t port, std::string* error) {
    stop();
    threads::LockGuard guard(state_->mu);
    state_->stop.store(false);
    char reason[160]{};
    uint16_t bound = 0;
    const intptr_t listener =
        sock::stream_listen(sock::Loopback::ipv4, port, bound, reason, sizeof reason);
    if (listener == sock::invalid_socket) {
        if (error != nullptr)
            *error = reason[0] == '\0' ? "could not listen" : reason;
        return false;
    }
    state_->listener = listener;
    state_->bound_port = bound;
    if (!threads::start_thread(state_->thread, &Server::State::run_server, state_.get())) {
        sock::stream_close(&state_->listener);
        state_->bound_port = 0;
        if (error != nullptr)
            *error = "could not start the server thread";
        return false;
    }
    state_->thread_started = true;
    return true;
}

uint16_t Server::port() const {
    threads::LockGuard guard(state_->mu);
    return state_->bound_port;
}

std::string Server::url(std::string_view path) const {
    return "http://127.0.0.1:" + std::to_string(port()) + std::string(path);
}

void Server::serve_folder(std::filesystem::path root, std::string prefix) {
    if (prefix.empty())
        prefix = "/";
    if (prefix.front() != '/')
        prefix.insert(prefix.begin(), '/');
    threads::LockGuard guard(state_->mu);
    for (FolderRoute& route : state_->folders) {
        if (route.prefix == prefix) {
            route.root = std::move(root);
            return;
        }
    }
    FolderRoute route;
    route.root = std::move(root);
    route.prefix = std::move(prefix);
    state_->folders.push_back(std::move(route));
}

void Server::serve_bytes(std::string path, std::vector<uint8_t> bytes, std::string content_type) {
    threads::LockGuard guard(state_->mu);
    for (BytesRoute& route : state_->files) {
        if (route.path == path) {
            route.bytes = std::move(bytes);
            route.content_type = std::move(content_type);
            return;
        }
    }
    BytesRoute route;
    route.path = std::move(path);
    route.bytes = std::move(bytes);
    route.content_type = std::move(content_type);
    state_->files.push_back(std::move(route));
}

void Server::handle(std::string method, std::string path, Handler handler) {
    threads::LockGuard guard(state_->mu);
    for (HandlerRoute& route : state_->handlers) {
        if (route.method == method && route.path == path) {
            route.handler = std::move(handler);
            return;
        }
    }
    HandlerRoute route;
    route.method = std::move(method);
    route.path = std::move(path);
    route.handler = std::move(handler);
    state_->handlers.push_back(std::move(route));
}

void Server::redirect(std::string from, std::string to, int status) {
    threads::LockGuard guard(state_->mu);
    for (RedirectRoute& route : state_->redirects) {
        if (route.from == from) {
            route.to = std::move(to);
            route.status = status;
            return;
        }
    }
    RedirectRoute route;
    route.from = std::move(from);
    route.to = std::move(to);
    route.status = status;
    state_->redirects.push_back(std::move(route));
}

void Server::override_next(std::string path, Reply reply, int times) {
    if (times <= 0)
        return;
    threads::LockGuard guard(state_->mu);
    OverrideRoute route;
    route.path = std::move(path);
    route.reply = std::move(reply);
    route.times = times;
    state_->overrides.push_back(std::move(route));
}

void Server::set_ranges(bool honoured) {
    threads::LockGuard guard(state_->mu);
    state_->ranges = honoured;
}

void Server::set_idle_close_ms(uint32_t ms) {
    threads::LockGuard guard(state_->mu);
    state_->idle_ms = ms;
}

std::vector<LoggedRequest> Server::requests() const {
    threads::LockGuard guard(state_->mu);
    return state_->log;
}

void Server::clear_log() {
    threads::LockGuard guard(state_->mu);
    state_->log.clear();
}

uint64_t Server::connections_accepted() const {
    return state_->accepted.load();
}

void Server::stop() {
    if (!state_)
        return;
    {
        threads::LockGuard guard(state_->mu);
        state_->stop.store(true);
    }
    threads::join_thread(state_->thread);
    threads::LockGuard guard(state_->mu);
    state_->thread_started = false;
    state_->bound_port = 0;
}

} // namespace oa::netgame::http_fixture
