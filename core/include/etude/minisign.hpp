// SPDX-License-Identifier: GPL-3.0-or-later
// Checks a minisign signature (https://jedisct1.github.io/minisign/) over bytes the caller already
// has, against a public key given as text. Pure computation: no file system, no network, so the
// updater can hand it a downloaded file's bytes and a signature file's text and get a yes or a
// reason. Both signature kinds minisign writes are accepted: "ED" (signs the BLAKE2b-512 hash of
// the file, the default) and "Ed" (signs the file itself, `minisign -l`).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace etude {

// Why a file was not accepted. Everything but None means: do not install it.
enum class SignatureError : std::uint8_t {
    None,
    KeyNotSet,                  // the build's public key is still the placeholder (update_key.hpp)
    BadKey,                     // the public key text is not a minisign public key
    BadSignatureFile,           // the .minisig text is not four well-formed lines
    UnsupportedAlgorithm,       // neither "Ed" nor "ED"
    WrongKeyId,                 // signed by another key than ours
    BadSignature,               // the file is not what was signed (changed, cut, or forged)
    BadTrustedCommentSignature, // the trusted comment was edited after signing
    TrustedCommentMismatch,     // signed correctly, but for something else than expected
};

struct SignatureCheck {
    SignatureError error = SignatureError::None;
    // The signed trusted comment, set when the signature itself checked out.
    std::string trusted_comment;

    bool ok() const { return error == SignatureError::None; }
};

// `public_key` is the line `minisign -G` prints after "untrusted comment:" in minisign.pub (56
// characters starting "RW"); the whole two-line file is accepted too. `signature_file` is the
// text of the .minisig file. `expected_trusted_comment` is what the signer was to write with
// `minisign -t`: it must match exactly, which ties the signature to the release it announces (a
// genuine signature of an older release cannot pass as a newer one).
SignatureCheck verify_minisign(std::span<const std::uint8_t> data, std::string_view signature_file,
                               std::string_view public_key,
                               std::string_view expected_trusted_comment);

// A prehashed ("ED") signature covers the BLAKE2b-512 hash of the file, so a file too large to hold
// in memory is checked by hashing it in pieces: feed the pieces to a FileDigest, then hand the result
// to verify_minisign_digest. The legacy "Ed" kind signs the file itself and cannot be checked this way.
using Digest512 = std::array<std::uint8_t, 64>;

class FileDigest {
  public:
    FileDigest();
    ~FileDigest();
    FileDigest(const FileDigest&) = delete;
    FileDigest& operator=(const FileDigest&) = delete;

    void update(std::span<const std::uint8_t> piece);
    Digest512 finish();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

// True when `signature_file` is a well-formed minisign signature of the prehashed kind.
bool minisign_is_prehashed(std::string_view signature_file);

// The same checks as verify_minisign over a file whose BLAKE2b-512 hash is `digest`. A legacy "Ed"
// signature is UnsupportedAlgorithm here: it cannot be checked from a hash.
SignatureCheck verify_minisign_digest(const Digest512& digest, std::string_view signature_file,
                                      std::string_view public_key,
                                      std::string_view expected_trusted_comment);

} // namespace etude
