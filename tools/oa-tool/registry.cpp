// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// registry init and mirror. init writes a direct registry with the descriptor
// writer and an empty catalogue signed as catalogue sign signs. mirror copies
// a registry through the HTTP client and, for a tokens registry, the download
// API. Catalogue bytes are copied, not rewritten. The sequence check is the
// catalogue check's own.
#include "arguments.hpp"
#include "command.hpp"
#include "files.hpp"
#include "passphrase.hpp"

#include "oa/app/content/download_api.hpp"
#include "oa/app/content/downloads.hpp"
#include "oa/app/content/settings.hpp"
#include "oa/base/sha256.hpp"
#include "oa/base/signing/ed25519.hpp"
#include "oa/base/signing/sealed_key.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/formats/json.hpp"
#include "oa/formats/url.hpp"
#include "oa/netgame/http/client.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

namespace oa::tool {
namespace {

namespace fs = std::filesystem;
namespace http = oa::netgame::http;
namespace url = oa::formats::url;
namespace json = oa::formats::json;
namespace sha = oa::base::sha256;
namespace signing = oa::base::signing;
namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;
namespace content = oa::app::content;

/// How many times a 403, or a key about to expire, asks again.
constexpr uint32_t rekey_limit = 3;

/// A key closer than this to expiry is asked for again, in seconds.
constexpr int64_t key_fresh_seconds = 30;

/// How many times a challenge is polled before it is given up.
constexpr uint32_t poll_limit = 1000;

/// How long a challenge may be polled, in seconds. Ten minutes.
constexpr int64_t challenge_limit_seconds = 600;

/// How many times a 429 is asked again.
constexpr uint32_t rate_reasks = 3;

/// How many times a failed key request is asked again.
constexpr uint32_t transport_reasks = 3;

/// The most bytes of one picture.
constexpr uint64_t picture_limit = 64ull << 20;

/// The wait when a 429 names none, in seconds.
constexpr uint32_t retry_after_default_seconds = 30;

void report_usage(Output& output, std::string_view command, std::string_view problem) {
    output.err << "oa-tool registry " << command << ": " << problem << '\n';
    output.err << "run 'oa-tool help registry " << command << "'\n";
}

void report_file(Output& output, std::string_view path, std::string_view problem) {
    output.err << "oa-tool registry mirror: " << path << ": " << problem << '\n';
}

fs::path path_from(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string utf8_of(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

std::vector<uint8_t> bytes_of(std::string_view text) {
    return {
        reinterpret_cast<const uint8_t*>(text.data()),
        reinterpret_cast<const uint8_t*>(text.data()) + text.size()
    };
}

int64_t current_time() {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    );
    return static_cast<int64_t>(seconds.count());
}

/// The civil date of a count of days since 1970-01-01.
///
/// Howard Hinnant's public-domain conversion. The system calendar is not used:
/// the Windows build marks gmtime unsafe.
void civil_from_days(int64_t z, int& year, unsigned& month, unsigned& day) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(y + (month <= 2));
}

void append_digits(std::string& out, int value, int width) {
    std::string digits(static_cast<std::size_t>(width), '0');
    for (int index = width - 1; index >= 0; --index) {
        digits[static_cast<std::size_t>(index)] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    out += digits;
}

/// A UTC time as `YYYY-MM-DDTHH:MM:SSZ`.
std::string format_utc(int64_t seconds) {
    constexpr int64_t day_seconds = 86400;
    int64_t days = seconds / day_seconds;
    int64_t time = seconds % day_seconds;
    if (time < 0) {
        time += day_seconds;
        --days;
    }
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, year, month, day);
    const int hour = static_cast<int>(time / 3600);
    const int minute = static_cast<int>((time % 3600) / 60);
    const int second = static_cast<int>(time % 60);
    std::string out;
    out.reserve(20);
    append_digits(out, year, 4);
    out.push_back('-');
    append_digits(out, static_cast<int>(month), 2);
    out.push_back('-');
    append_digits(out, static_cast<int>(day), 2);
    out.push_back('T');
    append_digits(out, hour, 2);
    out.push_back(':');
    append_digits(out, minute, 2);
    out.push_back(':');
    append_digits(out, second, 2);
    out.push_back('Z');
    return out;
}

const std::string* required_option(
    const Arguments& parsed, Output& output, std::string_view command, std::string_view name
) {
    const std::string* value = parsed.value(name);
    if (value == nullptr || value->empty()) {
        report_usage(output, command, "option '--" + std::string(name) + "' needs a value");
        return nullptr;
    }
    return value;
}

bool seek_file(std::FILE* file, uint64_t offset) noexcept {
#if defined(_WIN32)
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return ::fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool sync_file(std::FILE* file) noexcept {
    if (std::fflush(file) != 0)
        return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return ::fsync(::fileno(file)) == 0;
#endif
}

bool write_all(std::FILE* file, const uint8_t* data, std::size_t size) noexcept {
    while (size > 0) {
        const std::size_t wrote = std::fwrite(data, 1, size, file);
        if (wrote == 0)
            return false;
        data += wrote;
        size -= wrote;
    }
    return true;
}

void remove_file(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}

void ensure_parent(const fs::path& file) {
    const fs::path parent = file.parent_path();
    if (parent.empty())
        return;
    std::error_code error;
    fs::create_directories(parent, error);
    if (error)
        throw Failure("cannot write the file");
}

std::optional<sha::Digest> hash_file(const fs::path& path) {
    std::FILE* file = oa::platform::open_file(path, "rb");
    if (file == nullptr)
        return std::nullopt;

    struct Guard {
        std::FILE* file;

        ~Guard() { std::fclose(file); }
    } guard{file};

    sha::Hasher hasher;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (true) {
        const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
        if (read > 0)
            sha::update(hasher, std::span<const uint8_t>(buffer.data(), read));
        if (read < buffer.size()) {
            if (std::ferror(file) != 0)
                return std::nullopt;
            break;
        }
    }
    return sha::finish(hasher);
}

void replace_with(const fs::path& from, const fs::path& to) {
    std::error_code error;
    fs::remove(to, error);
    error.clear();
    fs::rename(from, to, error);
    if (error)
        throw Failure("cannot write the file");
}

/// Writes bytes through a temporary name, then renames it into place.
void publish_bytes(const fs::path& destination, std::span<const uint8_t> bytes) {
    ensure_parent(destination);
    fs::path temporary = destination;
    temporary += ".tmp";
    std::FILE* file = oa::platform::open_file(temporary, "wb");
    if (file == nullptr)
        throw Failure("cannot write the file");

    struct Guard {
        std::FILE* file;

        ~Guard() {
            if (file != nullptr)
                std::fclose(file);
        }
    } guard{file};

    if (!write_all(file, bytes.data(), bytes.size()) || !sync_file(file)) {
        remove_file(temporary);
        throw Failure("cannot write the file");
    }
    std::fclose(file);
    guard.file = nullptr;
    try {
        replace_with(temporary, destination);
    } catch (const Failure&) {
        remove_file(temporary);
        throw;
    }
}

/// The path of a URL below a folder. A `.` or `..` segment is refused.
std::optional<std::string> storage_path(const url::Url& address) {
    std::string path = address.target;
    const std::size_t query = path.find('?');
    if (query != std::string::npos)
        path.erase(query);
    if (path.size() < 2 || path.front() != '/' || path.back() == '/')
        return std::nullopt;
    std::size_t index = 0;
    bool any = false;
    while (index < path.size()) {
        if (path[index] == '/') {
            ++index;
            continue;
        }
        const std::size_t end = path.find('/', index);
        const std::string segment =
            path.substr(index, end == std::string::npos ? end : end - index);
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.find('\\') != std::string::npos)
            return std::nullopt;
        any = true;
        if (end == std::string::npos)
            break;
        index = end;
    }
    if (!any)
        return std::nullopt;
    return path;
}

fs::path placed_file(const fs::path& root, std::string_view url_path) {
    fs::path out = root;
    std::size_t index = 0;
    while (index < url_path.size()) {
        if (url_path[index] == '/') {
            ++index;
            continue;
        }
        const std::size_t end = url_path.find('/', index);
        const std::string segment(
            url_path.substr(index, end == std::string_view::npos ? end : end - index)
        );
        out /= segment;
        if (end == std::string_view::npos)
            break;
        index = end;
    }
    return out;
}

url::Url with_target(url::Url address, std::string target) {
    const std::size_t query = address.target.find('?');
    if (query != std::string::npos)
        target.append(address.target, query);
    address.target = std::move(target);
    return address;
}

std::vector<uint8_t>
fetch_bytes(http::Client& client, const std::string& address, uint64_t limit, int& status) {
    http::Request request;
    request.method = http::Method::get;
    request.url = address;
    request.accept_gzip = false;
    request.limits.max_body_bytes = limit;
    std::vector<uint8_t> body;
    const http::Response response = client.fetch(request, body);
    if (response.failure != http::Failure::none) {
        const std::string& detail = response.detail.empty()
                                        ? std::string(http::failure_text(response.failure))
                                        : response.detail;
        throw Failure(detail);
    }
    status = response.status;
    if (status != 200 && status != 404)
        throw Failure("the server answered " + std::to_string(status));
    return body;
}

/// Why a body stopped being written.
enum class Halt : uint8_t {
    none,
    differs,
    denied,
    missing,
    range,
    disk,
    other,
};

/// One file body, committed a step at a time. A cut drops the unfinished step.
class PartSink final : public http::BodySink {
  public:

    PartSink(fs::path path, uint64_t done, std::optional<uint64_t> package_size, bool ranged)
        : done_(done), path_(std::move(path)), package_size_(package_size), ranged_(ranged) {}

    ~PartSink() override { close_file(); }

    PartSink(const PartSink&) = delete;
    PartSink& operator=(const PartSink&) = delete;

    bool begin(const http::Response& head) override {
        status_ = head.status;
        if (head.status == 403) {
            halt_ = Halt::denied;
            return false;
        }
        if (head.status == 404) {
            halt_ = Halt::missing;
            return false;
        }
        if (head.status == 416) {
            halt_ = Halt::range;
            return false;
        }
        if (head.status == 200) {
            if (package_size_) {
                if (!head.content_length || *head.content_length != *package_size_) {
                    halt_ = Halt::differs;
                    return false;
                }
            } else if (head.content_length && *head.content_length > picture_limit) {
                halt_ = Halt::differs;
                return false;
            }
            if (ranged_)
                done_ = 0;
            total_ = package_size_ ? package_size_ : head.content_length;
        } else if (head.status == 206) {
            if (!head.content_range || head.content_range->first != done_) {
                halt_ = Halt::differs;
                return false;
            }
            if (package_size_) {
                if (!head.content_range->total || *head.content_range->total != *package_size_) {
                    halt_ = Halt::differs;
                    return false;
                }
            } else if (head.content_range->total && *head.content_range->total > picture_limit) {
                halt_ = Halt::differs;
                return false;
            }
            total_ = head.content_range && head.content_range->total ? head.content_range->total
                                                                     : package_size_;
        } else {
            halt_ = Halt::other;
            return false;
        }
        const char* mode = head.status == 200 ? "wb" : "r+b";
        file_ = oa::platform::open_file(path_, mode);
        if (file_ == nullptr && head.status == 206) {
            file_ = oa::platform::open_file(path_, "wb");
            done_ = 0;
        }
        if (file_ == nullptr) {
            halt_ = Halt::disk;
            return false;
        }
        opened_ = true;
        if (done_ > 0 && !seek_file(file_, done_)) {
            halt_ = Halt::disk;
            return false;
        }
        return true;
    }

    bool write(std::span<const uint8_t> bytes) override {
        if (halt_ != Halt::none)
            return false;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const uint64_t ceiling = total_ ? *total_ : picture_limit;
            if (done_ + static_cast<uint64_t>(buffer_.size()) > ceiling) {
                halt_ = Halt::differs;
                return false;
            }
            const uint64_t room = ceiling - done_ - static_cast<uint64_t>(buffer_.size());
            if (room == 0) {
                halt_ = Halt::differs;
                return false;
            }
            std::size_t take = bytes.size() - offset;
            const uint64_t step_room =
                content::download_step_bytes - static_cast<uint64_t>(buffer_.size());
            if (static_cast<uint64_t>(take) > step_room)
                take = static_cast<std::size_t>(step_room);
            if (static_cast<uint64_t>(take) > room)
                take = static_cast<std::size_t>(room);
            buffer_.insert(
                buffer_.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + take)
            );
            offset += take;
            const bool step_full = buffer_.size() == content::download_step_bytes;
            const bool file_full =
                total_ && done_ + static_cast<uint64_t>(buffer_.size()) == *total_;
            if ((step_full || file_full) && !flush_buffer())
                return false;
        }
        return true;
    }

    /// Writes a short tail only when the response ended cleanly and completes the file.
    bool finish_tail(bool clean) {
        if (buffer_.empty() || halt_ != Halt::none)
            return halt_ == Halt::none;
        if (!clean)
            return true;
        if (total_ && done_ + static_cast<uint64_t>(buffer_.size()) != *total_)
            return true;
        return flush_buffer();
    }

    void close_file() {
        if (file_ == nullptr)
            return;
        if (halt_ == Halt::none || halt_ == Halt::differs)
            sync_file(file_);
        std::fclose(file_);
        file_ = nullptr;
    }

    int status_ = 0;
    Halt halt_ = Halt::none;
    bool opened_ = false;
    uint64_t done_ = 0;

  private:

    bool flush_buffer() {
        if (buffer_.empty())
            return true;
        if (file_ == nullptr || !write_all(file_, buffer_.data(), buffer_.size()) ||
            !sync_file(file_)) {
            halt_ = Halt::disk;
            return false;
        }
        done_ += static_cast<uint64_t>(buffer_.size());
        buffer_.clear();
        return true;
    }

    fs::path path_;
    std::optional<uint64_t> package_size_;
    std::optional<uint64_t> total_;
    bool ranged_ = false;
    std::FILE* file_ = nullptr;
    std::vector<uint8_t> buffer_;
};

enum class Transfer : uint8_t { finished, partial, denied, missing, differs, range, failed };

Transfer transfer_body(
    http::Client& client,
    const url::Url& address,
    const fs::path& part,
    uint64_t have,
    const std::optional<uint64_t>& package_size
) {
    http::Request request;
    request.method = http::Method::get;
    request.url = url::url_text(address);
    request.accept_gzip = false;
    if (have > 0)
        request.range_from = have;
    request.limits.max_body_bytes = package_size ? *package_size : picture_limit;
    PartSink sink(part, have, package_size, have > 0);
    const http::Response response = client.fetch(request, sink);
    const bool clean = response.failure == http::Failure::none && sink.halt_ == Halt::none;
    sink.finish_tail(clean);
    sink.close_file();
    if (sink.halt_ == Halt::denied)
        return Transfer::denied;
    if (sink.halt_ == Halt::missing)
        return Transfer::missing;
    if (sink.halt_ == Halt::range)
        return Transfer::range;
    if (sink.halt_ == Halt::disk)
        return Transfer::failed;
    if (sink.halt_ == Halt::differs || sink.halt_ == Halt::other) {
        if (sink.opened_)
            remove_file(part);
        return Transfer::differs;
    }
    if (response.failure == http::Failure::too_large) {
        std::error_code error;
        const auto size = fs::is_regular_file(part, error) ? fs::file_size(part, error) : 0;
        if (package_size && !error && size == *package_size)
            return Transfer::finished;
        remove_file(part);
        return Transfer::differs;
    }
    if (response.failure == http::Failure::connection_lost ||
        response.failure == http::Failure::timed_out)
        return Transfer::partial;
    if (response.failure != http::Failure::none)
        return Transfer::partial;
    if (package_size) {
        std::error_code error;
        const auto size = fs::file_size(part, error);
        if (error || size != *package_size)
            return Transfer::partial;
    }
    return Transfer::finished;
}

enum class CopyEnd : uint8_t { ok, bad, partial, give_up };

struct Payload {
    std::string path;
    url::Url address;
    sha::Digest digest{};
    std::optional<uint64_t> size;
    bool picture = false;
    bool named = true;
};

struct Claim {
    url::Url address;
    bool document = false;
};

void claim_path(
    std::map<std::string, Claim, std::less<>>& claims,
    const std::string& path,
    const url::Url& address,
    bool document
) {
    const auto [it, inserted] = claims.emplace(path, Claim{address, document});
    if (inserted)
        return;
    if (!url::same_server(it->second.address, address))
        throw Failure("two files of one path come from different hosts");
    if (it->second.document != document)
        throw Failure("two files of one path are not the same file");
}

bool same_payload(
    const Payload& have, const sha::Digest& digest, const std::optional<uint64_t>& size
) {
    return have.digest == digest && have.size == size;
}

void add_payload(
    std::vector<Payload>& payloads,
    std::map<std::string, Claim, std::less<>>& claims,
    const url::Url& base,
    std::string_view reference,
    const sha::Digest& digest,
    std::optional<uint64_t> size,
    bool picture,
    bool named
) {
    url::UrlError error = url::UrlError::none;
    const std::optional<url::Url> resolved = url::resolve_reference(base, reference, &error);
    if (!resolved)
        throw Failure(url::url_error_text(error));
    const std::optional<std::string> path = storage_path(*resolved);
    if (!path)
        throw Failure("a path leaves the folder");
    claim_path(claims, *path, *resolved, false);
    for (const Payload& have : payloads) {
        if (have.path != *path)
            continue;
        if (!same_payload(have, digest, size))
            throw Failure("two files of one path are not the same file");
        return;
    }
    Payload payload;
    payload.path = *path;
    payload.address = *resolved;
    payload.digest = digest;
    payload.size = size;
    payload.picture = picture;
    payload.named = named;
    payloads.push_back(std::move(payload));
}

std::optional<sha::Digest> picture_digest(std::string_view reference) {
    const std::size_t slash = reference.rfind('/');
    const std::string_view name =
        slash == std::string_view::npos ? reference : reference.substr(slash + 1);
    constexpr std::string_view extension = ".png";
    if (name.size() <= extension.size() ||
        name.compare(name.size() - extension.size(), extension.size(), extension) != 0)
        return std::nullopt;
    return sha::parse_hex(name.substr(0, name.size() - extension.size()));
}

void add_picture(
    std::vector<Payload>& payloads,
    std::map<std::string, Claim, std::less<>>& claims,
    const url::Url& base,
    std::string_view reference
) {
    if (reference.empty())
        return;
    const std::optional<sha::Digest> digest = picture_digest(reference);
    add_payload(
        payloads,
        claims,
        base,
        reference,
        digest ? *digest : sha::Digest{},
        std::nullopt,
        true,
        digest.has_value()
    );
}

/// Polls a challenge the way the download queue does, and stops at expiry or ten minutes.
bool poll_until_solved(
    http::Client& client, const url::Url& api, const content::Challenge& challenge
) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(challenge_limit_seconds);
    uint32_t polls = 0;
    while (polls < poll_limit) {
        if (std::chrono::steady_clock::now() >= deadline || current_time() >= challenge.expires)
            return false;
        std::this_thread::sleep_for(std::chrono::seconds(challenge.poll_seconds));
        if (std::chrono::steady_clock::now() >= deadline || current_time() >= challenge.expires)
            return false;
        std::string why;
        const std::optional<content::ChallengeState> state =
            content::poll_challenge(client, api, challenge.code, nullptr, &why);
        ++polls;
        if (!state)
            continue;
        if (*state == content::ChallengeState::solved)
            return true;
        if (*state == content::ChallengeState::expired)
            return false;
    }
    return false;
}

enum class KeyHold : uint8_t { ready, refused, give_up };

struct HeldKey {
    KeyHold hold = KeyHold::refused;
    content::IssuedKey key;
};

/// Asks for one package key. A mirror posts no install result.
HeldKey obtain_key(
    http::Client& client, const url::Url& api, const content::KeyRequest& fields, Output& output
) {
    HeldKey held;
    uint32_t expiry_rekeys = 0;
    uint32_t transport = 0;
    uint32_t rates = 0;
    for (;;) {
        const content::KeyAnswer answer = content::ask_for_key(client, api, fields, nullptr);
        if (answer.kind == content::KeyAnswer::Kind::key && answer.key) {
            if (answer.key->expires < current_time() + key_fresh_seconds) {
                if (expiry_rekeys >= rekey_limit) {
                    held.hold = KeyHold::refused;
                    return held;
                }
                ++expiry_rekeys;
                output.out << "asking for a key again\n";
                continue;
            }
            held.hold = KeyHold::ready;
            held.key = *answer.key;
            return held;
        }
        if (answer.kind == content::KeyAnswer::Kind::challenge && answer.challenge) {
            output.out << "check: " << answer.challenge->code << '\n';
            output.out << "address: " << answer.challenge->address << '\n';
            if (!poll_until_solved(client, api, *answer.challenge)) {
                held.hold = KeyHold::give_up;
                return held;
            }
            continue;
        }
        if (answer.kind == content::KeyAnswer::Kind::rate_limited) {
            if (rates >= rate_reasks) {
                held.hold = KeyHold::refused;
                return held;
            }
            ++rates;
            const uint32_t wait = answer.error.retry_after_seconds == 0
                                      ? retry_after_default_seconds
                                      : std::min(answer.error.retry_after_seconds, 300u);
            std::this_thread::sleep_for(std::chrono::seconds(wait));
            continue;
        }
        if (answer.kind == content::KeyAnswer::Kind::retry) {
            if (transport >= transport_reasks) {
                held.hold = KeyHold::refused;
                return held;
            }
            ++transport;
            continue;
        }
        held.hold = KeyHold::refused;
        return held;
    }
}

content::KeyRequest
key_fields(const catalogue::Package& package, const std::optional<SecretText>& install) {
    content::KeyRequest request;
    if (install)
        request.install = install->text;
    request.package = package.id;
    request.kind = package.kind;
    request.release = package.release;
    request.sha256 = package.sha256;
    request.engine = std::string(content::engine_version());
    request.platform = std::string(content::platform_name());
    request.arch = std::string(content::arch_name());
    request.language.clear();
    request.reason = content::DownloadReason::mirror;
    return request;
}

CopyEnd store_part(const fs::path& part, const fs::path& destination, const sha::Digest& digest) {
    const std::optional<sha::Digest> actual = hash_file(part);
    if (!actual || *actual != digest) {
        remove_file(part);
        remove_file(destination);
        return CopyEnd::bad;
    }
    replace_with(part, destination);
    return CopyEnd::ok;
}

CopyEnd copy_payload(
    http::Client& client,
    const fs::path& root,
    const Payload& payload,
    bool tokens,
    const url::Url* api,
    const catalogue::Package* package,
    const std::optional<SecretText>& install,
    Output& output
) {
    if (payload.picture && !payload.named)
        return CopyEnd::bad;
    const fs::path destination = placed_file(root, payload.path);
    fs::path part = destination;
    part += content::part_suffix;
    std::error_code error;
    if (fs::is_regular_file(destination, error)) {
        const std::optional<sha::Digest> actual = hash_file(destination);
        if (actual && *actual == payload.digest)
            return CopyEnd::ok;
        remove_file(destination);
    }
    uint64_t have = 0;
    if (fs::is_regular_file(part, error)) {
        const auto size = fs::file_size(part, error);
        if (!error)
            have = static_cast<uint64_t>(size);
        if (payload.size && have > *payload.size) {
            remove_file(part);
            have = 0;
        } else if (payload.size && have == *payload.size) {
            return store_part(part, destination, payload.digest);
        } else if (!payload.size && have > picture_limit) {
            remove_file(part);
            have = 0;
        }
    }
    ensure_parent(destination);
    const bool keyed = tokens && !payload.picture;
    url::Url address = payload.address;
    uint32_t package_rekeys = 0;
    bool range_restarted = false;
    for (;;) {
        if (keyed) {
            if (api == nullptr || package == nullptr)
                return CopyEnd::bad;
            const HeldKey held = obtain_key(client, *api, key_fields(*package, install), output);
            if (held.hold == KeyHold::give_up)
                return CopyEnd::give_up;
            if (held.hold != KeyHold::ready)
                return CopyEnd::bad;
            address = held.key.url;
        }
        std::error_code size_error;
        have = 0;
        if (fs::is_regular_file(part, size_error)) {
            const auto size = fs::file_size(part, size_error);
            if (!size_error)
                have = static_cast<uint64_t>(size);
        }
        const Transfer transfer = transfer_body(
            client, address, part, have, payload.picture ? std::nullopt : payload.size
        );
        if (transfer == Transfer::denied && keyed) {
            if (package_rekeys >= rekey_limit)
                return CopyEnd::bad;
            ++package_rekeys;
            output.out << "asking for a key again\n";
            continue;
        }
        if (transfer == Transfer::range) {
            if (payload.size && have == *payload.size)
                return store_part(part, destination, payload.digest);
            if (!range_restarted) {
                remove_file(part);
                range_restarted = true;
                continue;
            }
            return CopyEnd::bad;
        }
        if (transfer == Transfer::partial)
            return CopyEnd::partial;
        if (transfer == Transfer::finished)
            return store_part(part, destination, payload.digest);
        if (transfer == Transfer::differs) {
            remove_file(part);
            remove_file(destination);
        }
        return CopyEnd::bad;
    }
}

std::string
sequence_sentence(const catalogue::Previous& previous, std::span<const uint8_t> served) {
    catalogue::Catalogue loaded;
    std::string error;
    // Both numbers come from the catalogue reader. This ticket does not compare them.
    if (!previous.sequence || !catalogue::read_catalogue(served, loaded, &error))
        throw Failure(error.empty() ? std::string("the catalogue was refused") : error);
    return "the folder holds sequence " + std::to_string(*previous.sequence) +
           "; the registry serves " + std::to_string(loaded.sequence);
}

void remove_tree_files(const fs::path& folder, bool remove_root) {
    remove_file(folder / "registry.yaml");
    remove_file(folder / "v1" / "catalogue.json");
    remove_file(folder / "v1" / "catalogue.json.sig");
    std::error_code error;
    fs::remove(folder / "v1" / "p", error);
    fs::remove(folder / "v1" / "i", error);
    fs::remove(folder / "v1", error);
    if (remove_root)
        fs::remove(folder, error);
}

} // namespace

int run_registry_init(std::span<const std::string> arguments, Output& output) {
    static constexpr OptionSpec options[] = {
        {"id", true, false},
        {"name", true, false},
        {"base-url", true, false},
        {"key", true, false},
        {"passphrase-file", true, false},
        {"homepage", true, false},
    };
    std::string problem;
    const std::optional<Arguments> parsed = parse_arguments(arguments, options, problem);
    if (!parsed) {
        report_usage(output, "init", problem);
        return exit_usage;
    }
    if (parsed->positional.size() != 1) {
        report_usage(
            output, "init", "expected one folder, got " + std::to_string(parsed->positional.size())
        );
        return exit_usage;
    }
    const std::string* id = required_option(*parsed, output, "init", "id");
    const std::string* name = required_option(*parsed, output, "init", "name");
    const std::string* base = required_option(*parsed, output, "init", "base-url");
    const std::string* key = required_option(*parsed, output, "init", "key");
    if (id == nullptr || name == nullptr || base == nullptr || key == nullptr)
        return exit_usage;
    const std::string* homepage = parsed->value("homepage");
    if (homepage != nullptr && homepage->empty()) {
        report_usage(output, "init", "option '--homepage' needs a value");
        return exit_usage;
    }
    const std::string* pass_text = parsed->value("passphrase-file");
    if (pass_text != nullptr && pass_text->empty()) {
        report_usage(output, "init", "option '--passphrase-file' needs a value");
        return exit_usage;
    }
    const std::optional<fs::path> pass_path =
        pass_text == nullptr ? std::nullopt : std::optional<fs::path>(path_from(*pass_text));

    const fs::path folder = path_from(parsed->positional[0]);
    std::error_code error;
    const bool existed = fs::exists(folder, error);
    if (error)
        throw Failure("cannot read the folder");
    if (existed) {
        if (!fs::is_directory(folder, error) || error)
            throw Failure("the folder is not a folder");
        if (!fs::is_empty(folder, error) || error)
            throw Failure("the folder is not empty");
    }
    if (!base->starts_with("http://")) {
        if (base->starts_with("https://"))
            throw Failure("a registry address is plain http");
        throw Failure("the address must be an http address");
    }
    if (base->ends_with('/'))
        throw Failure("the address must not end with a slash");
    url::UrlError url_error = url::UrlError::none;
    const std::optional<url::Url> base_url = url::parse_http_url(*base, &url_error);
    if (!base_url)
        throw Failure(url::url_error_text(url_error));
    if (url::has_query(*base_url))
        throw Failure("the address must not have a query");

    const std::vector<uint8_t> key_bytes = read_file(path_from(*key));
    signing::SealedKeyInfo info{};
    const signing::SealStatus read = signing::read_sealed_key_info(key_bytes, info);
    if (read != signing::SealStatus::ok)
        throw Failure(std::string(signing::seal_status_text(read)));

    registry::Descriptor descriptor;
    descriptor.id = *id;
    descriptor.name = *name;
    if (homepage != nullptr) {
        const std::optional<url::Url> page = url::parse_web_url(*homepage, &url_error);
        if (!page)
            throw Failure(url::url_error_text(url_error));
        descriptor.homepage = *page;
    }
    std::string catalogue_target = base_url->target;
    while (!catalogue_target.empty() && catalogue_target.back() == '/')
        catalogue_target.pop_back();
    catalogue_target += "/v1/catalogue.json";
    descriptor.catalogue = with_target(*base_url, catalogue_target);
    descriptor.keys.push_back(
        registry::RegistryKey{std::string(info.id.data(), info.id_size), info.public_key}
    );
    descriptor.mode = registry::DownloadMode::direct;
    descriptor.install_id = registry::InstallIdUse::none;
    const std::string descriptor_body = registry::descriptor_text(descriptor);
    registry::Descriptor read_back;
    std::string descriptor_error;
    if (!registry::read_descriptor(bytes_of(descriptor_body), read_back, &descriptor_error))
        throw Failure(descriptor_error);

    const int64_t generated = current_time();
    const int64_t expires = generated + 30 * 86400;
    json::JsonWriter writer;
    writer.begin_object();
    writer.key("catalogue");
    writer.integer(catalogue::catalogue_version);
    writer.key("registry");
    writer.string(*id);
    writer.key("sequence");
    writer.integer(1);
    writer.key("generated");
    writer.string(format_utc(generated));
    writer.key("expires");
    writer.string(format_utc(expires));
    writer.key("packages");
    writer.begin_array();
    writer.end_array();
    writer.end_object();
    std::string catalogue_text = writer.text();
    catalogue_text.push_back('\n');
    const std::vector<uint8_t> catalogue_bytes = bytes_of(catalogue_text);
    catalogue::Catalogue loaded;
    std::string catalogue_error;
    if (!catalogue::read_catalogue(catalogue_bytes, loaded, &catalogue_error))
        throw Failure(catalogue_error);

    signing::PublicKey public_key{};
    const signing::Signature signature =
        sign_with_sealed_key(key_bytes, pass_path, catalogue_bytes, public_key);
    const std::string signature_text = catalogue::signature_file_text(
        catalogue::SignatureLine{std::string(info.id.data(), info.id_size), signature}
    );
    const std::vector<uint8_t> signature_bytes = bytes_of(signature_text);
    const registry::RegistryKey trusted{std::string(info.id.data(), info.id_size), public_key};
    catalogue::CheckRequest request;
    request.catalogue = catalogue_bytes;
    request.signature = signature_bytes;
    request.registry_id = *id;
    request.trusted_keys = std::span<const registry::RegistryKey>(&trusted, 1);
    request.now = current_time();
    const catalogue::Checked checked = catalogue::check_catalogue(request);
    if (checked.verdict != catalogue::Verdict::accepted) {
        throw Failure(
            checked.detail.empty() ? catalogue::verdict_text(checked.verdict) : checked.detail
        );
    }

    bool created = false;
    try {
        fs::create_directories(folder / "v1" / "p", error);
        if (error)
            throw Failure("cannot write the folder");
        fs::create_directories(folder / "v1" / "i", error);
        if (error)
            throw Failure("cannot write the folder");
        created = true;
        const fs::path descriptor_path = folder / "registry.yaml";
        const fs::path catalogue_path = folder / "v1" / "catalogue.json";
        const fs::path signature_path = folder / "v1" / "catalogue.json.sig";
        publish_bytes(descriptor_path, bytes_of(descriptor_body));
        publish_bytes(signature_path, signature_bytes);
        publish_bytes(catalogue_path, catalogue_bytes);

        const std::vector<uint8_t> written_descriptor = read_file(descriptor_path);
        const std::vector<uint8_t> written_catalogue = read_file(catalogue_path);
        const std::vector<uint8_t> written_signature = read_file(signature_path);
        registry::Descriptor confirmed;
        if (!registry::read_descriptor(written_descriptor, confirmed, &descriptor_error))
            throw Failure(descriptor_error);
        catalogue::CheckRequest written;
        written.catalogue = written_catalogue;
        written.signature = written_signature;
        written.registry_id = confirmed.id;
        written.trusted_keys = confirmed.keys;
        written.now = current_time();
        const catalogue::Checked again = catalogue::check_catalogue(written);
        if (again.verdict != catalogue::Verdict::accepted) {
            throw Failure(
                again.detail.empty() ? catalogue::verdict_text(again.verdict) : again.detail
            );
        }
        output.out << "registry: " << utf8_of(descriptor_path) << '\n';
        for (const std::string& finger : registry::key_fingerprints(confirmed))
            output.out << "fingerprint: " << finger << '\n';
        return exit_done;
    } catch (const Failure&) {
        if (created)
            remove_tree_files(folder, !existed);
        throw;
    }
}

int run_registry_mirror(std::span<const std::string> arguments, Output& output) {
    static constexpr OptionSpec options[] = {
        {"key", true, false},
    };
    std::string problem;
    const std::optional<Arguments> parsed = parse_arguments(arguments, options, problem);
    if (!parsed) {
        report_usage(output, "mirror", problem);
        return exit_usage;
    }
    if (parsed->positional.size() != 2) {
        report_usage(
            output,
            "mirror",
            "expected a URL and a folder, got " + std::to_string(parsed->positional.size())
        );
        return exit_usage;
    }
    const std::string* key_text = parsed->value("key");
    if (key_text != nullptr && key_text->empty()) {
        report_usage(output, "mirror", "option '--key' needs a value");
        return exit_usage;
    }
    const fs::path folder = path_from(parsed->positional[1]);
    std::error_code error;
    if (fs::exists(folder, error) && (!fs::is_directory(folder, error) || error))
        throw Failure("the folder is not a folder");

    url::UrlError url_error = url::UrlError::none;
    const std::optional<url::Url> descriptor_url =
        url::parse_http_url(parsed->positional[0], &url_error);
    if (!descriptor_url) {
        if (parsed->positional[0].starts_with("https://"))
            throw Failure("a registry address is plain http");
        throw Failure(url::url_error_text(url_error));
    }
    const std::optional<std::string> descriptor_storage = storage_path(*descriptor_url);
    if (!descriptor_storage)
        throw Failure("a path leaves the folder");

    http::Client client;
    int status = 0;
    const std::vector<uint8_t> descriptor_bytes =
        fetch_bytes(client, parsed->positional[0], registry::max_descriptor_bytes, status);
    if (status != 200)
        throw Failure("the descriptor was not found");
    registry::Descriptor descriptor;
    std::string descriptor_error;
    if (!registry::read_descriptor(descriptor_bytes, descriptor, &descriptor_error))
        throw Failure(descriptor_error);

    std::vector<registry::RegistryKey> trusted = descriptor.keys;
    if (key_text != nullptr) {
        const std::optional<signing::PublicKey> pinned = signing::parse_public_key(*key_text);
        if (!pinned)
            throw Failure("a key must be ed25519: and its base64");
        const registry::RegistryKey* found = nullptr;
        for (const registry::RegistryKey& key : descriptor.keys) {
            if (key.key == *pinned) {
                found = &key;
                break;
            }
        }
        if (found == nullptr)
            throw Failure("the descriptor does not list that key");
        trusted.clear();
        trusted.push_back(*found);
    }

    output.out << "name: " << descriptor.name << '\n';
    output.out << "catalogue: " << url::url_text(descriptor.catalogue) << '\n';
    for (const std::string& finger : registry::key_fingerprints(descriptor))
        output.out << "fingerprint: " << finger << '\n';

    const std::optional<std::string> catalogue_storage = storage_path(descriptor.catalogue);
    if (!catalogue_storage)
        throw Failure("a path leaves the folder");
    url::Url signature_url = descriptor.catalogue;
    signature_url.target = *catalogue_storage + ".sig";
    const std::string signature_storage = *catalogue_storage + ".sig";

    const std::vector<uint8_t> catalogue_bytes = fetch_bytes(
        client, url::url_text(descriptor.catalogue), catalogue::max_catalogue_bytes, status
    );
    if (status != 200)
        throw Failure("the catalogue was not found");
    std::vector<uint8_t> signature_bytes = fetch_bytes(
        client, url::url_text(signature_url), catalogue::max_signature_file_bytes, status
    );
    const bool signature_present = status == 200;
    if (!signature_present)
        signature_bytes.clear();

    catalogue::Previous previous;
    const fs::path existing = placed_file(folder, *catalogue_storage);
    if (fs::is_regular_file(existing, error)) {
        const auto size = fs::file_size(existing, error);
        if (!error && size <= catalogue::max_catalogue_bytes) {
            try {
                const std::vector<uint8_t> held = read_file(existing);
                catalogue::Catalogue loaded;
                if (catalogue::read_catalogue(held, loaded, nullptr)) {
                    previous.sequence = loaded.sequence;
                    previous.digest = sha::digest_of(held);
                }
            } catch (const Failure&) {
            }
        }
    }

    catalogue::CheckRequest request;
    request.catalogue = catalogue_bytes;
    request.signature = signature_bytes;
    request.registry_id = descriptor.id;
    request.trusted_keys = trusted;
    request.previous = previous;
    request.now = current_time();
    const catalogue::Checked checked = catalogue::check_catalogue(request);
    if (checked.verdict == catalogue::Verdict::sequence_went_back ||
        checked.verdict == catalogue::Verdict::sequence_not_advanced) {
        throw Failure(sequence_sentence(previous, catalogue_bytes));
    }
    if (!checked.usable() || !checked.catalogue) {
        throw Failure(
            checked.detail.empty() ? catalogue::verdict_text(checked.verdict) : checked.detail
        );
    }

    std::map<std::string, Claim, std::less<>> claims;
    claim_path(claims, *descriptor_storage, *descriptor_url, true);
    claim_path(claims, *catalogue_storage, descriptor.catalogue, true);
    if (signature_present)
        claim_path(claims, signature_storage, signature_url, true);

    std::vector<Payload> payloads;
    for (const catalogue::Package& package : checked.catalogue->packages) {
        add_payload(
            payloads,
            claims,
            descriptor.catalogue,
            package.file,
            package.sha256,
            package.size,
            false,
            true
        );
        add_picture(payloads, claims, descriptor.catalogue, package.badge);
        for (const catalogue::MapEntry& map : package.maps)
            add_picture(payloads, claims, descriptor.catalogue, map.preview);
    }

    std::error_code make_error;
    fs::create_directories(folder, make_error);
    if (make_error)
        throw Failure("cannot write the folder");

    const bool tokens = descriptor.mode == registry::DownloadMode::tokens;
    std::optional<SecretText> install;
    if (tokens && descriptor.install_id != registry::InstallIdUse::none) {
        std::optional<std::string> made = content::make_install_id_text();
        if (!made)
            throw Failure("an install id could not be made");
        install.emplace(std::move(*made));
    }
    const url::Url* api = descriptor.api ? &*descriptor.api : nullptr;

    bool failed = false;
    for (const Payload& payload : payloads) {
        const catalogue::Package* package = nullptr;
        if (!payload.picture) {
            for (const catalogue::Package& candidate : checked.catalogue->packages) {
                if (candidate.sha256 == payload.digest && candidate.file.size() >= 1) {
                    url::UrlError ignored = url::UrlError::none;
                    const std::optional<url::Url> resolved =
                        url::resolve_reference(descriptor.catalogue, candidate.file, &ignored);
                    const std::optional<std::string> path =
                        resolved ? storage_path(*resolved) : std::nullopt;
                    if (path && *path == payload.path) {
                        package = &candidate;
                        break;
                    }
                }
            }
        }
        const CopyEnd end =
            copy_payload(client, folder, payload, tokens, api, package, install, output);
        if (end == CopyEnd::give_up)
            throw Failure("the check expired");
        if (end == CopyEnd::partial) {
            report_file(output, payload.path, "the download stopped before the file was complete");
            failed = true;
        } else if (end == CopyEnd::bad) {
            report_file(output, payload.path, "the file is not the one the catalogue named");
            failed = true;
        }
    }
    if (failed)
        return exit_failed;

    publish_bytes(placed_file(folder, *descriptor_storage), descriptor_bytes);
    if (signature_present)
        publish_bytes(placed_file(folder, signature_storage), signature_bytes);
    publish_bytes(placed_file(folder, *catalogue_storage), catalogue_bytes);
    return exit_done;
}

} // namespace oa::tool
