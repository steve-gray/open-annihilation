// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// catalogue keygen, public, sign and verify. The sealed key file is written
// only by creating it exclusively. The detached signature is F07's line, and
// a catalogue is accepted only by the same check the game uses.
#include "arguments.hpp"
#include "command.hpp"
#include "files.hpp"
#include "passphrase.hpp"

#include "oa/base/signing/ed25519.hpp"
#include "oa/base/signing/sealed_key.hpp"
#include "oa/data/catalogue/catalogue.hpp"
#include "oa/data/catalogue/check.hpp"
#include "oa/data/registry/descriptor.hpp"
#include "oa/platform/random.hpp"

#include <monocypher.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oa::tool {
namespace {

namespace fs = std::filesystem;
namespace signing = oa::base::signing;
namespace catalogue = oa::data::catalogue;
namespace registry = oa::data::registry;

void report_usage(Output& output, std::string_view command, std::string_view problem) {
    output.err << "oa-tool catalogue " << command << ": " << problem << '\n';
    output.err << "run 'oa-tool help catalogue " << command << "'\n";
}

fs::path path_from(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string utf8_of(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

std::span<const uint8_t> as_bytes(std::string_view text) {
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}

/// The passphrase, wiped when this goes away.
class SecretText {
  public:

    std::string text;

    explicit SecretText(std::string value) : text(std::move(value)) {}

    SecretText(const SecretText&) = delete;
    SecretText& operator=(const SecretText&) = delete;

    ~SecretText() { crypto_wipe(text.data(), text.size()); }
};

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

std::optional<fs::path>
passphrase_path(const Arguments& parsed, Output& output, std::string_view command, bool& usage) {
    const std::string* value = parsed.value("passphrase-file");
    if (value == nullptr)
        return std::nullopt;
    if (value->empty()) {
        report_usage(output, command, "option '--passphrase-file' needs a value");
        usage = true;
        return std::nullopt;
    }
    return path_from(*value);
}

void print_identity(Output& output, const signing::SealedKeyInfo& info) {
    const auto text = signing::public_key_text(info.public_key);
    const auto finger = signing::fingerprint(info.public_key);
    output.out << "key: ";
    output.out.write(info.id.data(), static_cast<std::streamsize>(info.id_size));
    output.out << "\npublic: ";
    output.out.write(text.view().data(), static_cast<std::streamsize>(text.view().size()));
    output.out << "\nfingerprint: ";
    output.out.write(finger.view().data(), static_cast<std::streamsize>(finger.view().size()));
    output.out << '\n';
}

std::vector<uint8_t>
read_at_most(const fs::path& path, uintmax_t most, const char* missing, const char* large) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error)
        throw Failure(missing);
    if (size > most)
        throw Failure(large);
    return read_file(path);
}

/// A key file created only when the name is new. A failure removes it.
class ExclusiveFile {
  public:

    explicit ExclusiveFile(const fs::path& path) : path_(path) { create(); }

    ExclusiveFile(const ExclusiveFile&) = delete;
    ExclusiveFile& operator=(const ExclusiveFile&) = delete;

    ~ExclusiveFile() { discard(); }

    void write(std::span<const uint8_t> bytes) {
#if defined(_WIN32)
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto remaining = bytes.size() - done;
            const DWORD chunk = static_cast<DWORD>(std::min(remaining, std::size_t{1} << 30));
            DWORD wrote = 0;
            if (WriteFile(handle_, bytes.data() + done, chunk, &wrote, nullptr) == 0 || wrote == 0)
                throw Failure("cannot write the key file");
            done += wrote;
        }
        if (FlushFileBuffers(handle_) == 0)
            throw Failure("cannot write the key file");
        if (CloseHandle(handle_) == 0) {
            handle_ = INVALID_HANDLE_VALUE;
            throw Failure("cannot write the key file");
        }
        handle_ = INVALID_HANDLE_VALUE;
#else
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto wrote = ::write(descriptor_, bytes.data() + done, bytes.size() - done);
            if (wrote < 0 && errno == EINTR)
                continue;
            if (wrote <= 0)
                throw Failure("cannot write the key file");
            done += static_cast<std::size_t>(wrote);
        }
        if (::fsync(descriptor_) != 0)
            throw Failure("cannot write the key file");
        if (::close(descriptor_) != 0) {
            descriptor_ = -1;
            throw Failure("cannot write the key file");
        }
        descriptor_ = -1;
#endif
        owned_ = false;
    }

  private:

    void create() {
#if defined(_WIN32)
        handle_ = CreateFileW(
            path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (handle_ == INVALID_HANDLE_VALUE) {
            const DWORD failure = GetLastError();
            if (failure == ERROR_FILE_EXISTS || failure == ERROR_ALREADY_EXISTS)
                throw Failure("the key file already exists");
            throw Failure("cannot write the key file");
        }
        owned_ = true;
#else
        descriptor_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (descriptor_ < 0) {
            if (errno == EEXIST)
                throw Failure("the key file already exists");
            throw Failure("cannot write the key file");
        }
        owned_ = true;
        if (::fchmod(descriptor_, 0600) != 0)
            throw Failure("cannot write the key file");
#endif
    }

    void discard() noexcept {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        if (owned_)
            DeleteFileW(path_.c_str());
#else
        if (descriptor_ >= 0)
            ::close(descriptor_);
        descriptor_ = -1;
        if (owned_)
            ::unlink(path_.c_str());
#endif
        owned_ = false;
    }

    fs::path path_;
    bool owned_ = false;
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

struct RandomMaterial {
    std::array<uint8_t, 32> seed{};
    std::array<uint8_t, 16> salt{};
    std::array<uint8_t, 24> nonce{};
    std::array<uint8_t, signing::sealed_key_most_bytes> sealed{};

    ~RandomMaterial() {
        crypto_wipe(seed.data(), seed.size());
        crypto_wipe(salt.data(), salt.size());
        crypto_wipe(nonce.data(), nonce.size());
        crypto_wipe(sealed.data(), sealed.size());
    }
};

int64_t current_time() {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    );
    return static_cast<int64_t>(seconds.count());
}

} // namespace

int run_catalogue_keygen(std::span<const std::string> arguments, Output& output) {
    static constexpr OptionSpec options[] = {
        {"id", true, false},
        {"out", true, false},
        {"passphrase-file", true, false},
    };
    std::string problem;
    const std::optional<Arguments> parsed = parse_arguments(arguments, options, problem);
    if (!parsed) {
        report_usage(output, "keygen", problem);
        return exit_usage;
    }
    if (!parsed->positional.empty()) {
        report_usage(
            output,
            "keygen",
            "expected no positional arguments, got " + std::to_string(parsed->positional.size())
        );
        return exit_usage;
    }
    const std::string* id = required_option(*parsed, output, "keygen", "id");
    if (id == nullptr)
        return exit_usage;
    const std::string* out = required_option(*parsed, output, "keygen", "out");
    if (out == nullptr)
        return exit_usage;
    bool usage = false;
    const std::optional<fs::path> pass_path = passphrase_path(*parsed, output, "keygen", usage);
    if (usage)
        return exit_usage;
    if (!registry::valid_key_id(*id))
        throw Failure(std::string(signing::seal_status_text(signing::SealStatus::bad_id)));

    const fs::path out_path = path_from(*out);
    std::error_code error;
    if (fs::exists(fs::symlink_status(out_path, error)))
        throw Failure("the key file already exists");

    SecretText passphrase(read_new_passphrase(pass_path));
    RandomMaterial random;
    if (!oa::platform::random::fill_random(random.seed) ||
        !oa::platform::random::fill_random(random.salt) ||
        !oa::platform::random::fill_random(random.nonce))
        throw Failure("the system's random bytes could not be read");

    std::size_t sealed_size = 0;
    const signing::SealStatus status = signing::seal_key(
        *id,
        random.seed,
        as_bytes(passphrase.text),
        random.salt,
        random.nonce,
        signing::SealParameters{},
        random.sealed,
        sealed_size
    );
    if (status != signing::SealStatus::ok)
        throw Failure(std::string(signing::seal_status_text(status)));

    {
        ExclusiveFile created(out_path);
        created.write(std::span<const uint8_t>(random.sealed.data(), sealed_size));
    }
    std::vector<uint8_t> written;
    try {
        written = read_at_most(
            out_path,
            signing::sealed_key_most_bytes,
            "cannot read the key file",
            signing::seal_status_text(signing::SealStatus::not_sealed_key).data()
        );
    } catch (const Failure&) {
        fs::remove(out_path, error);
        throw;
    }
    signing::SealedKeyInfo info{};
    if (written.size() != sealed_size ||
        signing::read_sealed_key_info(written, info) != signing::SealStatus::ok) {
        fs::remove(out_path, error);
        throw Failure(std::string(signing::seal_status_text(signing::SealStatus::not_sealed_key)));
    }
    print_identity(output, info);
    return exit_done;
}

int run_catalogue_public(std::span<const std::string> arguments, Output& output) {
    const std::vector<uint8_t> file = read_at_most(
        path_from(arguments[0]),
        signing::sealed_key_most_bytes,
        "cannot read the key file",
        signing::seal_status_text(signing::SealStatus::not_sealed_key).data()
    );
    signing::SealedKeyInfo info{};
    const signing::SealStatus status = signing::read_sealed_key_info(file, info);
    if (status != signing::SealStatus::ok)
        throw Failure(std::string(signing::seal_status_text(status)));
    print_identity(output, info);
    return exit_done;
}

int run_catalogue_sign(std::span<const std::string> arguments, Output& output) {
    static constexpr OptionSpec options[] = {
        {"key", true, false},
        {"out", true, false},
        {"passphrase-file", true, false},
    };
    std::string problem;
    const std::optional<Arguments> parsed = parse_arguments(arguments, options, problem);
    if (!parsed) {
        report_usage(output, "sign", problem);
        return exit_usage;
    }
    if (parsed->positional.size() != 1) {
        report_usage(
            output,
            "sign",
            "expected one catalogue, got " + std::to_string(parsed->positional.size())
        );
        return exit_usage;
    }
    const std::string* key = required_option(*parsed, output, "sign", "key");
    if (key == nullptr)
        return exit_usage;
    const std::string* out = parsed->value("out");
    if (out != nullptr && out->empty()) {
        report_usage(output, "sign", "option '--out' needs a value");
        return exit_usage;
    }
    bool usage = false;
    const std::optional<fs::path> pass_path = passphrase_path(*parsed, output, "sign", usage);
    if (usage)
        return exit_usage;

    const fs::path catalogue_path = path_from(parsed->positional[0]);
    const std::vector<uint8_t> catalogue_bytes = read_at_most(
        catalogue_path,
        catalogue::max_catalogue_bytes,
        "cannot read the catalogue",
        catalogue::verdict_text(catalogue::Verdict::too_large)
    );
    catalogue::Catalogue loaded;
    std::string error_text;
    if (!catalogue::read_catalogue(catalogue_bytes, loaded, &error_text))
        throw Failure(error_text);

    const std::vector<uint8_t> key_bytes = read_at_most(
        path_from(*key),
        signing::sealed_key_most_bytes,
        "cannot read the key file",
        signing::seal_status_text(signing::SealStatus::not_sealed_key).data()
    );
    signing::SealedKeyInfo info{};
    const signing::SealStatus read = signing::read_sealed_key_info(key_bytes, info);
    if (read != signing::SealStatus::ok)
        throw Failure(std::string(signing::seal_status_text(read)));

    signing::Signature signature{};
    signing::PublicKey public_key{};
    std::string key_id(info.id.data(), info.id_size);
    {
        SecretText passphrase(read_passphrase("Passphrase: ", pass_path));
        signing::SecretKey secret{};

        struct KeyGuard {
            signing::SecretKey& secret;

            ~KeyGuard() { crypto_wipe(secret.data(), secret.size()); }
        } guard{secret};

        const signing::SealStatus status =
            signing::unseal_key(key_bytes, as_bytes(passphrase.text), secret, public_key);
        if (status != signing::SealStatus::ok)
            throw Failure(std::string(signing::seal_status_text(status)));
        signature = signing::sign(secret, catalogue_bytes);
    }

    fs::path sig_path = out == nullptr ? catalogue_path : path_from(*out);
    if (out == nullptr)
        sig_path += ".sig";
    const std::string text =
        catalogue::signature_file_text(catalogue::SignatureLine{key_id, signature});
    const auto sig_bytes =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    try {
        write_file(sig_path, sig_bytes);
        const std::vector<uint8_t> written_catalogue = read_at_most(
            catalogue_path,
            catalogue::max_catalogue_bytes,
            "cannot read the catalogue",
            catalogue::verdict_text(catalogue::Verdict::too_large)
        );
        const std::vector<uint8_t> written_signature = read_at_most(
            sig_path,
            catalogue::max_signature_file_bytes,
            "cannot read the signature",
            catalogue::verdict_text(catalogue::Verdict::bad_signature_file)
        );
        const registry::RegistryKey trusted{key_id, public_key};
        catalogue::CheckRequest request;
        request.catalogue = written_catalogue;
        request.signature = written_signature;
        request.registry_id = loaded.registry;
        request.trusted_keys = std::span<const registry::RegistryKey>(&trusted, 1);
        request.now = current_time();
        const catalogue::Checked checked = catalogue::check_catalogue(request);
        if (!checked.usable()) {
            const char* verdict = catalogue::verdict_text(checked.verdict);
            throw Failure(checked.detail.empty() ? verdict : checked.detail);
        }
    } catch (const Failure&) {
        std::error_code ignored;
        fs::remove(sig_path, ignored);
        throw;
    }
    output.out << "signed " << utf8_of(sig_path) << " with " << key_id << '\n';
    return exit_done;
}

int run_catalogue_verify(std::span<const std::string> arguments, Output& output) {
    static constexpr OptionSpec options[] = {
        {"sig", true, false},
        {"descriptor", true, false},
        {"registry", true, false},
        {"key", true, true},
    };
    std::string problem;
    const std::optional<Arguments> parsed = parse_arguments(arguments, options, problem);
    if (!parsed) {
        report_usage(output, "verify", problem);
        return exit_usage;
    }
    if (parsed->positional.size() != 1) {
        report_usage(
            output,
            "verify",
            "expected one catalogue, got " + std::to_string(parsed->positional.size())
        );
        return exit_usage;
    }
    const std::string* sig = parsed->value("sig");
    const std::string* descriptor_path = parsed->value("descriptor");
    const std::string* registry_id = parsed->value("registry");
    if (sig != nullptr && sig->empty()) {
        report_usage(output, "verify", "option '--sig' needs a value");
        return exit_usage;
    }
    if (descriptor_path != nullptr && descriptor_path->empty()) {
        report_usage(output, "verify", "option '--descriptor' needs a value");
        return exit_usage;
    }
    if (registry_id != nullptr && registry_id->empty()) {
        report_usage(output, "verify", "option '--registry' needs a value");
        return exit_usage;
    }
    const auto key_values = parsed->values.find("key");
    const bool any_key = key_values != parsed->values.end() && !key_values->second.empty();
    const bool descriptor_mode = descriptor_path != nullptr && registry_id == nullptr && !any_key;
    const bool registry_mode = descriptor_path == nullptr && registry_id != nullptr && any_key;
    if (!descriptor_mode && !registry_mode) {
        report_usage(output, "verify", "pass a descriptor, or a registry and at least one key");
        return exit_usage;
    }

    const fs::path catalogue_path = path_from(parsed->positional[0]);
    std::error_code error;
    const auto catalogue_size = fs::file_size(catalogue_path, error);
    if (error)
        throw Failure("cannot read the catalogue");
    if (catalogue_size > catalogue::max_catalogue_bytes) {
        output.out << catalogue::verdict_text(catalogue::Verdict::too_large) << '\n';
        return exit_failed;
    }
    const std::vector<uint8_t> catalogue_bytes = read_file(catalogue_path);

    fs::path sig_path = sig == nullptr ? catalogue_path : path_from(*sig);
    if (sig == nullptr)
        sig_path += ".sig";
    const auto signature_size = fs::file_size(sig_path, error);
    if (error)
        throw Failure("cannot read the signature");
    std::vector<uint8_t> signature_bytes =
        signature_size > catalogue::max_signature_file_bytes
            ? std::vector<uint8_t>(catalogue::max_signature_file_bytes + 1)
            : read_file(sig_path);

    std::string registry_name;
    std::vector<registry::RegistryKey> trusted;
    if (descriptor_mode) {
        const std::vector<uint8_t> descriptor_bytes = read_at_most(
            path_from(*descriptor_path),
            registry::max_descriptor_bytes,
            "cannot read the descriptor",
            "the registry descriptor is longer than 64 KiB"
        );
        registry::Descriptor descriptor;
        std::string descriptor_error;
        if (!registry::read_descriptor(descriptor_bytes, descriptor, &descriptor_error))
            throw Failure(descriptor_error);
        if (descriptor.keys.empty())
            throw Failure("the registry names no signing key");
        registry_name = descriptor.id;
        trusted = std::move(descriptor.keys);
    } else {
        registry_name = *registry_id;
        std::string signature_error;
        const auto line = catalogue::parse_signature_file(signature_bytes, &signature_error);
        const std::string id = line ? line->key_id : "unknown";
        for (const std::string& text : key_values->second) {
            const std::optional<signing::PublicKey> key = signing::parse_public_key(text);
            if (!key)
                throw Failure("a key must be ed25519: and its base64");
            trusted.push_back(registry::RegistryKey{id, *key});
        }
    }

    catalogue::CheckRequest request;
    request.catalogue = catalogue_bytes;
    request.signature = signature_bytes;
    request.registry_id = registry_name;
    request.trusted_keys = trusted;
    request.now = current_time();
    const catalogue::Checked checked = catalogue::check_catalogue(request);
    if (!checked.usable()) {
        output.out << catalogue::verdict_text(checked.verdict) << '\n';
        return exit_failed;
    }
    output.out << "good signature by " << checked.signed_by << '\n';
    output.out << catalogue::verdict_text(checked.verdict) << '\n';
    return exit_done;
}

} // namespace oa::tool
