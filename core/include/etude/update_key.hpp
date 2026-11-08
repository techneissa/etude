// SPDX-License-Identifier: GPL-3.0-or-later
// The public key this build trusts for app updates, and the one door a downloaded release goes
// through. While the key is still the placeholder in update_key.cpp, nothing verifies and the
// updater must not run: ask update_key_ready() before offering an update at all.
#pragma once

#include "etude/minisign.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace etude {

// The built-in minisign public key (the "RW..." line of minisign.pub), or the placeholder text.
std::string_view update_public_key();

// True when `key` is the placeholder that stands in until the real key is pasted into the source.
bool update_key_is_placeholder(std::string_view key);

// False while the built-in key is the placeholder or not a well-formed minisign public key.
bool update_key_ready();

// verify_minisign against the built-in key; KeyNotSet while it is the placeholder.
SignatureCheck verify_release_signature(std::span<const std::uint8_t> data,
                                        std::string_view signature_file,
                                        std::string_view expected_trusted_comment);

// The same for a file that was hashed in pieces (FileDigest): prehashed signatures only.
SignatureCheck verify_release_digest(const Digest512& digest, std::string_view signature_file,
                                     std::string_view expected_trusted_comment);

} // namespace etude
