// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A streaming reader for the portable ASCII cpio layouts. The caller keeps
// every byte; a file is handed on as its header and its data arrive.

#include "oa/formats/cpio.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace oa::formats::cpio {
namespace {

constexpr std::size_t k_odc_header = 76;
constexpr std::size_t k_newc_header = 110;

[[nodiscard]] bool
digits(const uint8_t* bytes, std::size_t count, int base, uint64_t& value) noexcept {
    value = 0;
    if (count == 0)
        return false;
    for (std::size_t index = 0; index < count; ++index) {
        const unsigned char byte = bytes[index];
        unsigned digit = 0;
        if (base == 8) {
            if (byte < '0' || byte > '7')
                return false;
            digit = static_cast<unsigned>(byte - '0');
        } else {
            if (byte >= '0' && byte <= '9')
                digit = static_cast<unsigned>(byte - '0');
            else if (byte >= 'a' && byte <= 'f')
                digit = static_cast<unsigned>(byte - 'a' + 10);
            else if (byte >= 'A' && byte <= 'F')
                digit = static_cast<unsigned>(byte - 'A' + 10);
            else
                return false;
        }
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / static_cast<unsigned>(base))
            return false;
        value = value * static_cast<unsigned>(base) + digit;
    }
    return true;
}

[[nodiscard]] bool magic_is(const uint8_t* header, const char* text) noexcept {
    return std::memcmp(header, text, 6) == 0;
}

} // namespace

bool name_is_safe(std::string_view name) noexcept {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string_view::npos ||
        name.find('\0') != std::string_view::npos)
        return false;
    if (name == "." || name == "./")
        return true;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = name.find('/', start);
        const std::string_view part = name.substr(
            start, slash == std::string_view::npos ? std::string_view::npos : slash - start
        );
        if (part.empty() || part == "..")
            return false;
        if (slash == std::string_view::npos)
            return true;
        start = slash + 1;
    }
    return false;
}

bool Reader::fail(std::string* error, const char* text) {
    failed_ = true;
    if (error != nullptr)
        *error = text;
    return false;
}

bool Reader::open_header(const Visit&, std::string* error) {
    uint64_t mode = 0;
    uint64_t modified = 0;
    uint64_t namesize = 0;
    uint64_t filesize = 0;
    uint64_t checksum = 0;
    uint64_t device = 0;
    uint64_t inode = 0;
    if (format_ == HeaderFormat::odc) {
        if (!digits(header_ + 6, 6, 8, device) || !digits(header_ + 12, 6, 8, inode) ||
            !digits(header_ + 18, 6, 8, mode) || !digits(header_ + 48, 11, 8, modified) ||
            !digits(header_ + 59, 6, 8, namesize) || !digits(header_ + 65, 11, 8, filesize))
            return fail(error, "the archive is broken");
    } else {
        uint64_t major = 0;
        uint64_t minor = 0;
        if (!digits(header_ + 6, 8, 16, inode) || !digits(header_ + 6 + 8, 8, 16, mode) ||
            !digits(header_ + 6 + 5 * 8, 8, 16, modified) ||
            !digits(header_ + 6 + 6 * 8, 8, 16, filesize) ||
            !digits(header_ + 6 + 7 * 8, 8, 16, major) ||
            !digits(header_ + 6 + 8 * 8, 8, 16, minor) ||
            !digits(header_ + 6 + 11 * 8, 8, 16, namesize) ||
            !digits(header_ + 6 + 12 * 8, 8, 16, checksum))
            return fail(error, "the archive is broken");
        if (major > 0xffffffffu || minor > 0xffffffffu)
            return fail(error, "the archive is broken");
        device = (major << 32) | minor;
    }
    if (namesize == 0 || namesize > max_name_bytes)
        return fail(error, namesize == 0 ? "the archive is broken" : "the file name is too long");
    if (modified > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return fail(error, "the archive is broken");
    file_ = {};
    file_.bytes = filesize;
    file_.modified = static_cast<int64_t>(modified);
    file_.mode = static_cast<uint32_t>(mode);
    file_.device = device;
    file_.inode = inode;
    const uint32_t kind = file_.mode & 0170000u;
    file_.directory = kind == 0040000u;
    file_.regular = kind == 0100000u;
    file_.symbolic_link = kind == 0120000u;
    checksum_expected_ = static_cast<uint32_t>(checksum);
    checksum_got_ = 0;
    name_.clear();
    name_left_ = static_cast<std::size_t>(namesize);
    phase_ = Phase::name;
    return true;
}

bool Reader::open_file(const Visit& visit, std::string* error) {
    file_.name = name_;
    file_.trailer = name_ == "TRAILER!!!";
    taking_ = false;
    in_file_ = false;
    if (!file_.trailer && visit.begin != nullptr) {
        bool take = false;
        if (!visit.begin(visit.context, file_, take)) {
            halted_ = true;
            return false;
        }
        taking_ = take && file_.bytes > 0;
    }
    if (!file_.trailer)
        in_file_ = true;
    const uint64_t pad = format_ == HeaderFormat::newc ? (4 - (accepted_ % 4)) % 4 : 0;
    if (pad > 0) {
        phase_ = Phase::name_pad;
        data_left_ = pad;
        return true;
    }
    phase_ = Phase::data;
    data_left_ = file_.bytes;
    if (data_left_ == 0)
        return finish_file(visit, error);
    return true;
}

bool Reader::feed(std::span<const uint8_t> input, const Visit& visit, std::string* error) {
    if (failed_ || halted_)
        return false;
    if (input.empty())
        return true;
    std::size_t at = 0;
    while (at < input.size() && !failed_ && !halted_) {
        if (phase_ == Phase::done) {
            if (input[at] != 0)
                return fail(error, "the archive has bytes after its trailer");
            ++at;
            ++accepted_;
            continue;
        }
        if (phase_ == Phase::header) {
            const std::size_t need = header_need_ - header_got_;
            const std::size_t step = std::min(need, input.size() - at);
            std::memcpy(header_ + header_got_, input.data() + at, step);
            header_got_ += step;
            at += step;
            accepted_ += step;
            if (header_need_ == 6 && header_got_ == 6) {
                HeaderFormat next = HeaderFormat::unknown;
                if (magic_is(header_, "070707"))
                    next = HeaderFormat::odc;
                else if (magic_is(header_, "070701") || magic_is(header_, "070702"))
                    next = HeaderFormat::newc;
                else
                    return fail(error, "the archive is not a cpio this reader reads");
                if (format_ != HeaderFormat::unknown && next != format_)
                    return fail(error, "the archive is broken");
                format_ = next;
                check_checksum_ = magic_is(header_, "070702");
                header_need_ = format_ == HeaderFormat::odc ? k_odc_header : k_newc_header;
            }
            if (header_got_ == header_need_ && header_need_ > 6) {
                if (!open_header(visit, error))
                    return false;
            }
            continue;
        }
        if (phase_ == Phase::name) {
            const uint8_t byte = input[at];
            ++at;
            ++accepted_;
            if (name_left_ == 0)
                return fail(error, "the archive is broken");
            if (name_left_ == 1) {
                if (byte != 0)
                    return fail(error, "the file name is broken");
            } else if (byte == 0) {
                return fail(error, "the file name is broken");
            } else {
                name_.push_back(static_cast<char>(byte));
            }
            --name_left_;
            if (name_left_ == 0 && !open_file(visit, error))
                return false;
            continue;
        }
        if (phase_ == Phase::name_pad || phase_ == Phase::data_pad) {
            const std::size_t step =
                static_cast<std::size_t>(std::min<uint64_t>(data_left_, input.size() - at));
            at += step;
            accepted_ += step;
            data_left_ -= step;
            if (data_left_ == 0) {
                if (phase_ == Phase::name_pad) {
                    phase_ = Phase::data;
                    data_left_ = file_.bytes;
                    checksum_got_ = 0;
                    if (data_left_ == 0 && !finish_file(visit, error))
                        return false;
                } else if (!next_header()) {
                    return fail(error, "the archive is broken");
                }
            }
            continue;
        }
        if (phase_ == Phase::data) {
            const std::size_t step =
                static_cast<std::size_t>(std::min<uint64_t>(data_left_, input.size() - at));
            const auto chunk = input.subspan(at, step);
            if (check_checksum_) {
                for (uint8_t byte : chunk)
                    checksum_got_ += byte;
            }
            if (taking_ && visit.data != nullptr && !visit.data(visit.context, chunk)) {
                halted_ = true;
                return false;
            }
            at += step;
            accepted_ += step;
            data_left_ -= step;
            if (data_left_ == 0 && !finish_file(visit, error))
                return false;
            continue;
        }
        return fail(error, "the archive is broken");
    }
    return !failed_ && !halted_;
}

bool Reader::finish(std::string* error) const {
    if (halted_ || failed_)
        return false;
    if (phase_ != Phase::done) {
        if (error != nullptr)
            *error = "the archive ended early";
        return false;
    }
    return true;
}

bool Reader::finish_file(const Visit& visit, std::string* error) {
    if (check_checksum_ && checksum_got_ != checksum_expected_)
        return fail(error, "the checksum does not match");
    if (in_file_ && visit.end != nullptr && !visit.end(visit.context)) {
        halted_ = true;
        return false;
    }
    in_file_ = false;
    taking_ = false;
    const uint64_t pad = format_ == HeaderFormat::newc ? (4 - (accepted_ % 4)) % 4 : 0;
    if (pad > 0) {
        phase_ = Phase::data_pad;
        data_left_ = pad;
        return true;
    }
    return next_header();
}

bool Reader::next_header() {
    if (file_.trailer) {
        phase_ = Phase::done;
        return true;
    }
    phase_ = Phase::header;
    header_got_ = 0;
    header_need_ = 6;
    return true;
}

} // namespace oa::formats::cpio
