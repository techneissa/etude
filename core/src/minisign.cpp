// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/minisign.hpp"

#include "etude/base64.hpp"

#include <monocypher-ed25519.h>
#include <monocypher.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace etude {
namespace {

constexpr std::size_t kIdBytes = 8;
constexpr std::size_t kKeyBytes = 32;
constexpr std::size_t kSigBytes = 64;
constexpr std::string_view kUntrusted = "untrusted comment:";
constexpr std::string_view kTrusted = "trusted comment: ";

struct PublicKey {
    std::array<std::uint8_t, kIdBytes> id{};
    std::array<std::uint8_t, kKeyBytes> key{};
};

struct SignatureFile {
    std::array<std::uint8_t, 2> algorithm{};
    std::array<std::uint8_t, kIdBytes> id{};
    std::array<std::uint8_t, kSigBytes> signature{};
    std::string trusted_comment;
    std::array<std::uint8_t, kSigBytes> global{};
};

// The lines of a text, without their line ends; empty lines at the end (the trailing newline) are
// dropped.
std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t from = 0;
    while (from < text.size()) {
        std::size_t at = text.find('\n', from);
        if (at == std::string_view::npos) {
            at = text.size();
        }
        std::string_view line = text.substr(from, at - from);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        from = at + 1;
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}

std::optional<PublicKey> read_public_key(std::string_view text) {
    std::vector<std::string_view> lines = lines_of(text);
    if (lines.size() == 2 && starts_with(lines[0], kUntrusted)) {
        lines.erase(lines.begin());
    }
    if (lines.size() != 1) {
        return std::nullopt;
    }
    const auto bytes = base64_decode(lines[0]);
    constexpr std::size_t kTotal = 2 + kIdBytes + kKeyBytes;
    if (!bytes || bytes->size() != kTotal || (*bytes)[0] != 'E' || (*bytes)[1] != 'd') {
        return std::nullopt;
    }
    PublicKey key;
    std::copy_n(bytes->begin() + 2, kIdBytes, key.id.begin());
    std::copy_n(bytes->begin() + 2 + kIdBytes, kKeyBytes, key.key.begin());
    return key;
}

// The four lines: untrusted comment, signature, "trusted comment: ...", global signature.
std::optional<SignatureFile> read_signature_file(std::string_view text) {
    const std::vector<std::string_view> lines = lines_of(text);
    if (lines.size() != 4 || !starts_with(lines[0], kUntrusted) || !starts_with(lines[2], kTrusted)) {
        return std::nullopt;
    }
    const auto sig = base64_decode(lines[1]);
    const auto global = base64_decode(lines[3]);
    constexpr std::size_t kTotal = 2 + kIdBytes + kSigBytes;
    if (!sig || sig->size() != kTotal || !global || global->size() != kSigBytes) {
        return std::nullopt;
    }
    SignatureFile file;
    std::copy_n(sig->begin(), 2, file.algorithm.begin());
    std::copy_n(sig->begin() + 2, kIdBytes, file.id.begin());
    std::copy_n(sig->begin() + 2 + kIdBytes, kSigBytes, file.signature.begin());
    std::copy_n(global->begin(), kSigBytes, file.global.begin());
    file.trusted_comment = std::string(lines[2].substr(kTrusted.size()));
    return file;
}

// What the first signature covers: the file itself ("Ed") or its BLAKE2b-512 hash ("ED").
bool signature_matches(const SignatureFile& file, const PublicKey& key,
                       std::span<const std::uint8_t> data) {
    if (file.algorithm[1] == 'D') {
        std::array<std::uint8_t, 64> hash{};
        crypto_blake2b(hash.data(), hash.size(), data.data(), data.size());
        return crypto_ed25519_check(file.signature.data(), key.key.data(), hash.data(),
                                    hash.size()) == 0;
    }
    return crypto_ed25519_check(file.signature.data(), key.key.data(), data.data(),
                                data.size()) == 0;
}

// The second signature covers the first one and the trusted comment.
bool trusted_comment_matches(const SignatureFile& file, const PublicKey& key) {
    std::vector<std::uint8_t> signed_bytes(file.signature.begin(), file.signature.end());
    signed_bytes.insert(signed_bytes.end(), file.trusted_comment.begin(),
                        file.trusted_comment.end());
    return crypto_ed25519_check(file.global.data(), key.key.data(), signed_bytes.data(),
                                signed_bytes.size()) == 0;
}

// The checks both entry points share. `digest` is set for the prehashed-only path (a file too large to
// hold), where the legacy kind is not accepted; else `data` is the file.
SignatureCheck check_minisign(const Digest512* digest, std::span<const std::uint8_t> data,
                              std::string_view signature_file, std::string_view public_key,
                              std::string_view expected_trusted_comment) {
    SignatureCheck result;
    const std::optional<PublicKey> key = read_public_key(public_key);
    if (!key) {
        result.error = SignatureError::BadKey;
        return result;
    }
    const std::optional<SignatureFile> file = read_signature_file(signature_file);
    const bool prehashed = file && file->algorithm[1] == 'D';
    if (!file) {
        result.error = SignatureError::BadSignatureFile;
    } else if (file->algorithm[0] != 'E' || (file->algorithm[1] != 'd' && !prehashed) ||
               (digest != nullptr && !prehashed)) {
        result.error = SignatureError::UnsupportedAlgorithm;
    } else if (file->id != key->id) {
        result.error = SignatureError::WrongKeyId;
    } else if (digest != nullptr
                   ? crypto_ed25519_check(file->signature.data(), key->key.data(), digest->data(),
                                          digest->size()) != 0
                   : !signature_matches(*file, *key, data)) {
        result.error = SignatureError::BadSignature;
    } else if (!trusted_comment_matches(*file, *key)) {
        result.error = SignatureError::BadTrustedCommentSignature;
    } else if (file->trusted_comment != expected_trusted_comment) {
        result.error = SignatureError::TrustedCommentMismatch;
    } else {
        result.trusted_comment = file->trusted_comment;
    }
    return result;
}

} // namespace

SignatureCheck verify_minisign(std::span<const std::uint8_t> data, std::string_view signature_file,
                               std::string_view public_key,
                               std::string_view expected_trusted_comment) {
    return check_minisign(nullptr, data, signature_file, public_key, expected_trusted_comment);
}

SignatureCheck verify_minisign_digest(const Digest512& digest, std::string_view signature_file,
                                      std::string_view public_key,
                                      std::string_view expected_trusted_comment) {
    return check_minisign(&digest, {}, signature_file, public_key, expected_trusted_comment);
}

bool minisign_is_prehashed(std::string_view signature_file) {
    const std::optional<SignatureFile> file = read_signature_file(signature_file);
    return file && file->algorithm[0] == 'E' && file->algorithm[1] == 'D';
}

struct FileDigest::State {
    crypto_blake2b_ctx ctx;
};

FileDigest::FileDigest() : state_(std::make_unique<State>()) {
    crypto_blake2b_init(&state_->ctx, 64);
}

FileDigest::~FileDigest() = default;

void FileDigest::update(std::span<const std::uint8_t> piece) {
    crypto_blake2b_update(&state_->ctx, piece.data(), piece.size());
}

Digest512 FileDigest::finish() {
    Digest512 hash{};
    crypto_blake2b_final(&state_->ctx, hash.data());
    return hash;
}

} // namespace etude
