// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_key.hpp"

namespace etude {
namespace {

constexpr std::string_view kPlaceholder = "MINISIGN-PUBLIC-KEY-NOT-SET";

// The public key of the release signing key, as `minisign -G` prints it in minisign.pub (the
// second line, 56 characters starting "RW"). Key id 0C91C57871701A2B.
constexpr std::string_view kUpdatePublicKey = "RWQrGnBxeMWRDOmNrv9me27OONZhPFpirjzTJ57C4RGsJDGNITQ+O96e";

} // namespace

std::string_view update_public_key() {
    return kUpdatePublicKey;
}

bool update_key_is_placeholder(std::string_view key) {
    return key == kPlaceholder;
}

bool update_key_ready() {
    if (update_key_is_placeholder(kUpdatePublicKey)) {
        return false;
    }
    // A key that cannot even check an empty signature file is malformed; a malformed signature
    // file is the answer for a good key, a bad key is the answer for a broken one.
    return verify_minisign({}, {}, kUpdatePublicKey, {}).error != SignatureError::BadKey;
}

SignatureCheck verify_release_signature(std::span<const std::uint8_t> data,
                                        std::string_view signature_file,
                                        std::string_view expected_trusted_comment) {
    if (update_key_is_placeholder(kUpdatePublicKey)) {
        SignatureCheck refused;
        refused.error = SignatureError::KeyNotSet;
        return refused;
    }
    return verify_minisign(data, signature_file, kUpdatePublicKey, expected_trusted_comment);
}

SignatureCheck verify_release_digest(const Digest512& digest, std::string_view signature_file,
                                     std::string_view expected_trusted_comment) {
    if (update_key_is_placeholder(kUpdatePublicKey)) {
        SignatureCheck refused;
        refused.error = SignatureError::KeyNotSet;
        return refused;
    }
    return verify_minisign_digest(digest, signature_file, kUpdatePublicKey, expected_trusted_comment);
}

} // namespace etude
