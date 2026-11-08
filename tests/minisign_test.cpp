// SPDX-License-Identifier: GPL-3.0-or-later
// The fixtures below were made by the real minisign 0.11 with a key pair generated only for these
// tests (`minisign -G -W`). The secret half was thrown away; the key signs nothing but this payload,
// and it is not the app's update key.
#include "etude/minisign.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace etude;

namespace {

const std::string kPayload = "etude test payload: not a real release.\n";
const std::string kComment = "etude 1.2.3 bookworm arm64";

const std::string kTestKey = "RWQ6O+XM6TL4f2t5zvWbRDAA20Tmy0CrIHW0OhJ0RZqg/WDMeOM0R9ng";
const std::string kTestKeyFile =
    "untrusted comment: minisign public key 7FF832E9CCE53B3A\n" + kTestKey + "\n";

// The default kind ("ED": signs the BLAKE2b-512 hash of the file).
const std::string kSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUQ6O+XM6TL4f8RO94hZ3KhqNqNO5heUnbldGAMnwwQizVE1AhDnVH94E3I9PuOYHeZyoGnnFJHr5mnSQ6MsZGHDsV"
    "WqEu8WHwA=\n"
    "trusted comment: etude 1.2.3 bookworm arm64\n"
    "K2DQhXqR7AnTzkhdYN7+OubRJNd3xw3duEbymJCbMIGmVCfrKDY2Hv5ktCvFY7U20UD1TVn76d7MoN/2W/uUAw==\n";

// The legacy kind ("Ed", `minisign -l`: signs the file itself).
const std::string kLegacySignature =
    "untrusted comment: signature from minisign secret key\n"
    "RWQ6O+XM6TL4f3Z3nmx3FNQLd3N/ND/Bhd1mW/qBhCJxqbGdXTKZk27bFpTIjIPn7MjtSqoWds63afOa3xvmYxTJrM"
    "gmShstWQk=\n"
    "trusted comment: etude 1.2.3 bookworm arm64\n"
    "eVT2MCJZnz3ILihs/ba5CoSw2a6pthDWtwuunV/UJe+/c+jav7cgiixi7XMP7J2S0tkhoZOxop5N9gMcx/cBAA==\n";

// Signed correctly, by another key pair (key id 7A0991B94868E840).
const std::string kOtherKeySignature =
    "untrusted comment: signature from minisign secret key\n"
    "RURA6GhIuZEJevqlfmPkMDcmFZlTIWHxw3EVVlGQ/OwxbQhJsUFpfoAHmyMEDDgzNH2vzIdIBKKFhWfmTWR+njCq84"
    "NAEXqvEQk=\n"
    "trusted comment: etude 1.2.3 bookworm arm64\n"
    "mNd/wB5ApnURrkNDZvTZkJHN5I0MGyWbzxnmcm/G4mNcdKjpeh7PfJcWpIDa1Oa86dgWmfkXi02B1t457PxBBQ==\n";

std::vector<std::uint8_t> bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

SignatureError check(const std::string& data, const std::string& signature,
                     const std::string& key = kTestKey, const std::string& comment = kComment) {
    return verify_minisign(bytes(data), signature, key, comment).error;
}

// `text` with the first occurrence of `from` replaced by `to`.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

} // namespace

TEST_CASE("minisign: a genuine signature passes and hands back the signed trusted comment") {
    const SignatureCheck result = verify_minisign(bytes(kPayload), kSignature, kTestKey, kComment);
    CHECK(result.ok());
    CHECK(result.trusted_comment == kComment);
}

TEST_CASE("minisign: the legacy kind of signature passes too") {
    CHECK(check(kPayload, kLegacySignature) == SignatureError::None);
}

TEST_CASE("minisign: the key may be given as the whole minisign.pub file") {
    CHECK(check(kPayload, kSignature, kTestKeyFile) == SignatureError::None);
    CHECK(check(kPayload, kSignature, kTestKeyFile + "\n") == SignatureError::None);
}

TEST_CASE("minisign: a signature file with Windows line ends passes") {
    std::string crlf;
    for (const char c : kSignature) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    CHECK(check(kPayload, crlf) == SignatureError::None);
}

TEST_CASE("minisign: another key's signature is refused by its key id") {
    CHECK(check(kPayload, kOtherKeySignature) == SignatureError::WrongKeyId);
}

TEST_CASE("minisign: a signature whose key id was changed to ours does not verify") {
    // Same signature bytes, key id rewritten to the test key's: the id matches, the maths do not.
    std::string forged = kOtherKeySignature;
    forged = replaced(forged, "RURA6GhIuZEJev", "RUQ6O+XM6TL4f/");
    CHECK(check(kPayload, forged) == SignatureError::BadSignature);
}

TEST_CASE("minisign: a changed, cut or extended file is refused") {
    CHECK(check(kPayload + "x", kSignature) == SignatureError::BadSignature);
    CHECK(check(kPayload.substr(1), kSignature) == SignatureError::BadSignature);
    CHECK(check(replaced(kPayload, "real", "fake"), kSignature) == SignatureError::BadSignature);
    CHECK(check("", kSignature) == SignatureError::BadSignature);
    CHECK(check(kPayload + "x", kLegacySignature) == SignatureError::BadSignature);
}

TEST_CASE("minisign: a damaged signature is refused") {
    CHECK(check(kPayload, replaced(kSignature, "PuOYHeZyoGnn", "PuOYHeZyoGnA")) ==
          SignatureError::BadSignature);
}

TEST_CASE("minisign: a trusted comment edited after signing is refused") {
    const std::string edited = replaced(kSignature, "1.2.3", "9.9.9");
    CHECK(check(kPayload, edited, kTestKey, "etude 9.9.9 bookworm arm64") ==
          SignatureError::BadTrustedCommentSignature);
}

TEST_CASE("minisign: a trusted comment that is not the one expected is refused") {
    // A genuine, untouched signature of release 1.2.3 cannot pass as release 1.2.4.
    CHECK(check(kPayload, kSignature, kTestKey, "etude 1.2.4 bookworm arm64") ==
          SignatureError::TrustedCommentMismatch);
    CHECK(check(kPayload, kSignature, kTestKey, "") == SignatureError::TrustedCommentMismatch);
    CHECK(check(kPayload, kSignature, kTestKey, kComment + " ") ==
          SignatureError::TrustedCommentMismatch);
}

TEST_CASE("minisign: a malformed signature file is refused") {
    const std::string line1 = "untrusted comment: signature from minisign secret key\n";
    const std::string good_sig = kSignature.substr(line1.size(), 101); // the base64 line and its \n
    CHECK(check(kPayload, "") == SignatureError::BadSignatureFile);
    CHECK(check(kPayload, "\n\n\n\n") == SignatureError::BadSignatureFile);
    CHECK(check(kPayload, line1) == SignatureError::BadSignatureFile);
    CHECK(check(kPayload, kSignature.substr(0, kSignature.rfind("trusted"))) ==
          SignatureError::BadSignatureFile); // the last two lines are missing
    CHECK(check(kPayload, kSignature + "extra\n") == SignatureError::BadSignatureFile);
    CHECK(check(kPayload, replaced(kSignature, "untrusted comment:", "comment:")) ==
          SignatureError::BadSignatureFile);
    CHECK(check(kPayload, replaced(kSignature, "\ntrusted comment: ", "\ntrusted comment:")) ==
          SignatureError::BadSignatureFile);
    CHECK(check(kPayload, replaced(kSignature, "RUQ6O+", "RUQ!O+")) ==
          SignatureError::BadSignatureFile); // not base64
    CHECK(check(kPayload, replaced(kSignature, "HwA=\n", "Hw==\n")) ==
          SignatureError::BadSignatureFile); // shorter than a signature
    CHECK(check(kPayload, replaced(kSignature, "UAw==\n", "UAw=\n")) ==
          SignatureError::BadSignatureFile); // the global signature is cut
    CHECK(check(kPayload, good_sig) == SignatureError::BadSignatureFile);
}

TEST_CASE("minisign: an algorithm other than Ed or ED is refused") {
    // 'S' for 'R' turns the first byte of the signature from 'E' into 'I': "ID".
    CHECK(check(kPayload, replaced(kSignature, "RUQ6O+", "SUQ6O+")) ==
          SignatureError::UnsupportedAlgorithm);
}

TEST_CASE("minisign: a malformed public key is refused") {
    CHECK(check(kPayload, kSignature, "") == SignatureError::BadKey);
    CHECK(check(kPayload, kSignature, "not a key") == SignatureError::BadKey);
    CHECK(check(kPayload, kSignature, "MINISIGN-PUBLIC-KEY-NOT-SET") == SignatureError::BadKey);
    CHECK(check(kPayload, kSignature, kTestKey.substr(0, 40)) == SignatureError::BadKey);
    CHECK(check(kPayload, kSignature, kTestKey + "AAAA") == SignatureError::BadKey);
    CHECK(check(kPayload, kSignature, "RX" + kTestKey.substr(2)) == SignatureError::BadKey);
    CHECK(check(kPayload, kSignature, kTestKey + "\n" + kTestKey) == SignatureError::BadKey);
}

TEST_CASE("minisign: a key that is not the signer's refuses an otherwise good file") {
    // The other pair's public key against the test pair's signature: the key ids differ.
    CHECK(check(kPayload, kSignature, "RWRA6GhIuZEJen7FSbYr6hVJmYjH9Qo+Xa90akn3+KxuWn6RXbYsIOE/") ==
          SignatureError::WrongKeyId);
}

namespace {

// 2.5 MiB made by a formula and signed (prehashed, the default) by the real minisign 0.11 with a key
// pair made for this test alone, so the streaming check runs over a file of a real size.
constexpr std::size_t kBigBytes = 2 * 1024 * 1024 + 512 * 1024 + 17;
const std::string kBigKey = "RWTD6uY2Jl8jca0hCx/Wqe7nqjX9M+MGz/Oei21mhtOZFFWCvIpyxKPs";
const std::string kBigComment = "etude 2.0.0 trixie arm64";
const std::string kBigSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUTD6uY2Jl8jcdEhOAztMWZpMYaW3DZ6ItbpsX+tvhJ9SDx0AYwZ1uo4LTQJZ0dP7QXaDJOtQ4DK72HWbKSGSbmAQWgHFCDEJQ4=\n"
    "trusted comment: etude 2.0.0 trixie arm64\n"
    "y3YO3zXevBDZPu9rVNWPH7AttYBl5GhJUoTB+6dTmp5BmPCFLemqJxpJkLhFg19c2Sw9xyC0X90FgQoP7J0pDA==\n";

std::vector<std::uint8_t> big_payload() {
    std::vector<std::uint8_t> data(kBigBytes);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(((i * 31) ^ ((i >> 8) * 7)) & 0xff);
    }
    return data;
}

// The hash of `data` taken the way the helper does, `piece` bytes at a time.
Digest512 streamed(const std::vector<std::uint8_t>& data, std::size_t piece) {
    FileDigest hash;
    for (std::size_t at = 0; at < data.size(); at += piece) {
        hash.update({data.data() + at, std::min(piece, data.size() - at)});
    }
    return hash.finish();
}

} // namespace

TEST_CASE("minisign: a prehashed signature is verified by hashing in pieces, whatever the pieces") {
    const std::vector<std::uint8_t> data = big_payload();
    // The in-memory check agrees on the fixture.
    CHECK(verify_minisign(data, kBigSignature, kBigKey, kBigComment).ok());
    for (const std::size_t piece : {1u << 20, 256u * 1024, 65536u, 4099u, 7u}) {
        CAPTURE(piece);
        const SignatureCheck result =
            verify_minisign_digest(streamed(data, piece), kBigSignature, kBigKey, kBigComment);
        CHECK(result.ok());
        CHECK(result.trusted_comment == kBigComment);
    }
    // The small fixture too, a byte at a time.
    CHECK(verify_minisign_digest(streamed(bytes(kPayload), 1), kSignature, kTestKey, kComment).ok());
}

TEST_CASE("minisign: a byte changed in the middle of a file hashed in pieces is refused") {
    std::vector<std::uint8_t> data = big_payload();
    data[data.size() / 2] ^= 0x01;
    CHECK(verify_minisign_digest(streamed(data, 256 * 1024), kBigSignature, kBigKey, kBigComment).error ==
          SignatureError::BadSignature);
    data = big_payload();
    data.pop_back(); // cut short
    CHECK(verify_minisign_digest(streamed(data, 256 * 1024), kBigSignature, kBigKey, kBigComment).error ==
          SignatureError::BadSignature);
    data = big_payload();
    data.push_back(0); // extended
    CHECK(verify_minisign_digest(streamed(data, 256 * 1024), kBigSignature, kBigKey, kBigComment).error ==
          SignatureError::BadSignature);
}

TEST_CASE("minisign: a hash check keeps every other refusal of the in-memory check") {
    const Digest512 digest = streamed(big_payload(), 65536);
    CHECK(verify_minisign_digest(digest, kBigSignature, kBigKey, "etude 2.0.1 trixie arm64").error ==
          SignatureError::TrustedCommentMismatch);
    CHECK(verify_minisign_digest(digest, replaced(kBigSignature, "2.0.0", "9.9.9"), kBigKey,
                                 "etude 9.9.9 trixie arm64")
              .error == SignatureError::BadTrustedCommentSignature);
    CHECK(verify_minisign_digest(digest, kBigSignature, kTestKey, kBigComment).error ==
          SignatureError::WrongKeyId);
    CHECK(verify_minisign_digest(digest, kBigSignature, "not a key", kBigComment).error ==
          SignatureError::BadKey);
    CHECK(verify_minisign_digest(digest, "", kBigKey, kBigComment).error ==
          SignatureError::BadSignatureFile);
    CHECK(verify_minisign_digest(digest, kOtherKeySignature, kBigKey, kBigComment).error ==
          SignatureError::WrongKeyId);
}

TEST_CASE("minisign: the legacy kind cannot be checked from a hash, and is told apart") {
    CHECK(minisign_is_prehashed(kSignature));
    CHECK(minisign_is_prehashed(kBigSignature));
    CHECK_FALSE(minisign_is_prehashed(kLegacySignature));
    CHECK_FALSE(minisign_is_prehashed(""));
    CHECK_FALSE(minisign_is_prehashed("not a signature"));
    CHECK(verify_minisign_digest(streamed(bytes(kPayload), 16), kLegacySignature, kTestKey, kComment).error ==
          SignatureError::UnsupportedAlgorithm);
}
