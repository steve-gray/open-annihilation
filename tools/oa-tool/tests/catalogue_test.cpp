// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-tool catalogue, run in-process. Keygen runs once, with the parameters a
// publisher key uses. The other cases reuse that key, or fail before a key
// is sealed. Passphrases come from files in a scratch folder, which is
// removed when the test ends. Nothing here is a key that signs a catalogue
// anyone ships.

#include "command.hpp"

#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

namespace fs = std::filesystem;

constexpr std::string_view catalogue_json =
    "{\"catalogue\":1,\"registry\":\"test-registry\",\"sequence\":1,"
    "\"generated\":\"2026-01-01T00:00:00Z\",\"expires\":\"2099-01-01T00:00:00Z\","
    "\"packages\":[]}";

/// A public key that is not the one keygen makes. RFC 8032's first vector.
constexpr std::string_view other_public = "ed25519:11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=";

struct Scratch {
    fs::path path;

    Scratch() : path(oa::test::make_scratch_directory("oa-tool-catalogue")) {}

    ~Scratch() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

struct Captured {
    int status = 0;
    std::string out;
    std::string err;
};

Captured run(std::vector<std::string> arguments) {
    std::ostringstream out;
    std::ostringstream err;
    oa::tool::Output output{out, err};
    const int status = oa::tool::run_tool(arguments, output);
    return {status, out.str(), err.str()};
}

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

void write_text(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    OA_CHECK(static_cast<bool>(out));
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string generic_utf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

std::string line_after(std::string_view text, std::string_view prefix) {
    const auto at = text.find(prefix);
    if (at == std::string_view::npos)
        return {};
    const auto start = at + prefix.size();
    const auto end = text.find('\n', start);
    if (end == std::string_view::npos)
        return std::string(text.substr(start));
    return std::string(text.substr(start, end - start));
}

/// Prints the tool's reply when a case did not exit as asked. The reply is
/// the public text and the error; a passphrase is not part of either.
void show(std::string_view label, const Captured& captured) {
    std::fprintf(
        stderr,
        "%.*s: status %d\n%.*s%.*s",
        static_cast<int>(label.size()),
        label.data(),
        captured.status,
        static_cast<int>(captured.out.size()),
        captured.out.data(),
        static_cast<int>(captured.err.size()),
        captured.err.data()
    );
}

bool exited(std::string_view label, const Captured& captured, int status) {
    if (captured.status == status)
        return true;
    show(label, captured);
    OA_CHECK(captured.status == status);
    return false;
}

std::string descriptor_text(std::string_view public_key) {
    std::string yaml = "registry: 1\n"
                       "id: test-registry\n"
                       "name: Test Registry\n"
                       "catalogue: http://downloads.example.net/v1/catalogue.json\n"
                       "keys:\n"
                       "  - {id: test-key, public: \"";
    yaml += public_key;
    yaml += "\"}\n"
            "downloads:\n"
            "  mode: direct\n";
    return yaml;
}

/// Refusals that have to happen before Argon2id runs.
void refuse_before_sealing(const fs::path& root) {
    const fs::path pass = root / "pass";
    write_text(pass, "correct horse battery\n");
    const fs::path missing = root / "no-such-pass";

    const auto leading_dash =
        run({"catalogue", "keygen", "--id", "-nope", "--out", (root / "dash").string()});
    exited("bad id dash", leading_dash, oa::tool::exit_failed);
    OA_CHECK(contains(leading_dash.err, "the key id is not allowed"));
    OA_CHECK(!contains(leading_dash.err, "passphrase"));

    const auto upper =
        run({"catalogue", "keygen", "--id", "Nope", "--out", (root / "upper").string()});
    exited("bad id upper", upper, oa::tool::exit_failed);
    OA_CHECK(contains(upper.err, "the key id is not allowed"));

    const fs::path short_pass = root / "short-pass";
    write_text(short_pass, "short\n");
    const auto short_key = run(
        {"catalogue",
         "keygen",
         "--id",
         "test-key",
         "--out",
         (root / "short-key").string(),
         "--passphrase-file",
         short_pass.string()}
    );
    exited("short passphrase", short_key, oa::tool::exit_failed);
    OA_CHECK(contains(short_key.err, "the passphrase must be at least 12 bytes"));
    OA_CHECK(!fs::exists(root / "short-key"));

    const fs::path crlf_pass = root / "crlf-pass";
    write_text(crlf_pass, "12345678901\r\n");
    const auto crlf = run(
        {"catalogue",
         "keygen",
         "--id",
         "test-key",
         "--out",
         (root / "crlf-key").string(),
         "--passphrase-file",
         crlf_pass.string()}
    );
    exited("crlf passphrase", crlf, oa::tool::exit_failed);
    OA_CHECK(contains(crlf.err, "the passphrase must be at least 12 bytes"));

    const fs::path long_pass = root / "long-pass";
    write_text(long_pass, std::string(1025, 'x') + "\n");
    const auto too_long = run(
        {"catalogue",
         "keygen",
         "--id",
         "test-key",
         "--out",
         (root / "long-key").string(),
         "--passphrase-file",
         long_pass.string()}
    );
    exited("long passphrase", too_long, oa::tool::exit_failed);
    OA_CHECK(contains(too_long.err, "the passphrase is longer than 1024 bytes"));
    OA_CHECK(!fs::exists(root / "long-key"));

    const fs::path kept = root / "kept-key";
    write_text(kept, "kept");
    const auto exists = run(
        {"catalogue",
         "keygen",
         "--id",
         "test-key",
         "--out",
         kept.string(),
         "--passphrase-file",
         missing.string()}
    );
    exited("existing key", exists, oa::tool::exit_failed);
    OA_CHECK(contains(exists.err, "the key file already exists"));
    OA_CHECK(!contains(exists.err, "passphrase"));
    OA_CHECK(read_text(kept) == "kept");

    const fs::path bad_catalogue = root / "bad.json";
    write_text(bad_catalogue, "{\"catalogue\":1}");
    const auto refused = run(
        {"catalogue",
         "sign",
         "--key",
         (root / "no-such-key").string(),
         bad_catalogue.string(),
         "--passphrase-file",
         missing.string()}
    );
    exited("bad catalogue", refused, oa::tool::exit_failed);
    OA_CHECK(contains(refused.err, "registry"));
    OA_CHECK(!contains(refused.err, "passphrase"));
    OA_CHECK(!fs::exists(root / "bad.json.sig"));

    const auto help = run({"help", "catalogue"});
    exited("help", help, oa::tool::exit_done);
    OA_CHECK(contains(help.out, "keygen"));
    OA_CHECK(contains(help.out, "verify"));
}

#ifndef _WIN32
bool mode_is_owner_only(const fs::path& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0)
        return false;
    return (st.st_mode & 0777) == 0600;
}
#endif

/// One real keygen, then the sign and verify cases on that key.
void seal_and_use(const fs::path& root) {
    const fs::path pass = root / "pass";
    const fs::path key = root / "test-key";
    const auto made = run(
        {"catalogue",
         "keygen",
         "--id",
         "test-key",
         "--out",
         key.string(),
         "--passphrase-file",
         pass.string()}
    );
    if (!exited("keygen", made, oa::tool::exit_done))
        return;
    OA_CHECK(made.err.empty());
    OA_CHECK(contains(made.out, "key: test-key\n"));
    const std::string public_key = line_after(made.out, "public: ");
    OA_CHECK(public_key.starts_with("ed25519:"));
    OA_CHECK(contains(made.out, "fingerprint: "));
    OA_CHECK(contains(made.out, " \xC2\xB7 "));
    const auto key_bytes = read_text(key);
    OA_CHECK(key_bytes.size() == 144 + std::string_view("test-key").size());
#ifndef _WIN32
    OA_CHECK(mode_is_owner_only(key));
#endif

    const auto shown = run({"catalogue", "public", key.string()});
    exited("public", shown, oa::tool::exit_done);
    OA_CHECK(shown.out == made.out);
    OA_CHECK(shown.err.empty());

    const fs::path catalogue = root / "cat.json";
    write_text(catalogue, catalogue_json);
    const auto signed_catalogue = run(
        {"catalogue",
         "sign",
         "--key",
         key.string(),
         catalogue.string(),
         "--passphrase-file",
         pass.string()}
    );
    if (!exited("sign", signed_catalogue, oa::tool::exit_done))
        return;
    const fs::path signature = root / "cat.json.sig";
    const std::string sig_text = read_text(signature);
    OA_CHECK(sig_text.starts_with("ed25519 test-key "));
    OA_CHECK(sig_text.find('\n') == sig_text.size() - 1);
    OA_CHECK(signed_catalogue.out == "signed " + generic_utf8(signature) + " with test-key\n");
    OA_CHECK(signed_catalogue.err.empty());

    const auto verified = run(
        {"catalogue",
         "verify",
         catalogue.string(),
         "--registry",
         "test-registry",
         "--key",
         public_key}
    );
    exited("verify", verified, oa::tool::exit_done);
    OA_CHECK(verified.out == "good signature by test-key\naccepted\n");
    OA_CHECK(verified.err.empty());

    const fs::path descriptor = root / "registry.yaml";
    write_text(descriptor, descriptor_text(public_key));
    const auto by_descriptor =
        run({"catalogue", "verify", catalogue.string(), "--descriptor", descriptor.string()});
    exited("verify descriptor", by_descriptor, oa::tool::exit_done);
    OA_CHECK(by_descriptor.out == "good signature by test-key\naccepted\n");

    std::string changed = std::string(catalogue_json);
    changed[0] = static_cast<char>(changed[0] ^ 0x01);
    const fs::path changed_catalogue = root / "changed.json";
    write_text(changed_catalogue, changed);
    write_text(root / "changed.json.sig", sig_text);
    const auto flipped = run(
        {"catalogue",
         "verify",
         changed_catalogue.string(),
         "--registry",
         "test-registry",
         "--key",
         public_key}
    );
    exited("changed byte", flipped, oa::tool::exit_failed);
    OA_CHECK(flipped.out == "the signature does not match the catalogue\n");
    OA_CHECK(flipped.err.empty());

    const auto other_key = run(
        {"catalogue",
         "verify",
         catalogue.string(),
         "--registry",
         "test-registry",
         "--key",
         std::string(other_public)}
    );
    exited("other key", other_key, oa::tool::exit_failed);
    OA_CHECK(other_key.out == "the signature does not match the catalogue\n");

    const auto other_registry = run(
        {"catalogue",
         "verify",
         catalogue.string(),
         "--registry",
         "other-registry",
         "--key",
         public_key}
    );
    exited("other registry", other_registry, oa::tool::exit_failed);
    OA_CHECK(other_registry.out == "the catalogue names another registry\n");

    std::string renamed = sig_text;
    const auto id_at = renamed.find("ed25519 test-key ");
    OA_CHECK(id_at == 0);
    if (id_at == 0)
        renamed.replace(0, std::string("ed25519 test-key ").size(), "ed25519 other-key ");
    const fs::path renamed_sig = root / "other-id.sig";
    write_text(renamed_sig, renamed);
    const auto other_id = run(
        {"catalogue",
         "verify",
         catalogue.string(),
         "--sig",
         renamed_sig.string(),
         "--descriptor",
         descriptor.string()}
    );
    exited("other id", other_id, oa::tool::exit_failed);
    OA_CHECK(other_id.out == "the signature names a key that is not trusted\n");

    const fs::path second = root / "second.json";
    write_text(second, catalogue_json);
    const fs::path wrong_pass = root / "wrong-pass";
    write_text(wrong_pass, "not-the-passphrase\n");
    const auto wrong = run(
        {"catalogue",
         "sign",
         "--key",
         key.string(),
         second.string(),
         "--passphrase-file",
         wrong_pass.string()}
    );
    exited("wrong passphrase", wrong, oa::tool::exit_failed);
    OA_CHECK(contains(wrong.err, "the passphrase does not open the key"));
    OA_CHECK(!fs::exists(root / "second.json.sig"));
    OA_CHECK(read_text(signature) == sig_text);
}

} // namespace

int main() {
    const Scratch scratch;
    refuse_before_sealing(scratch.path);
    seal_and_use(scratch.path);
    if (oa::test::check_exit_status() == 0)
        std::fputs("tools-oa-tool-catalogue: ok\n", stdout);
    return oa::test::check_exit_status();
}
