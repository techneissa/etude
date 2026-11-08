// SPDX-License-Identifier: GPL-3.0-or-later
// Standard base64 (RFC 4648, with padding), strict: the form minisign writes its keys and
// signatures in.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace etude {

// The bytes of `text`, or nothing when it is not canonical base64: a character outside the
// alphabet, whitespace, a length that is not a multiple of 4, padding anywhere but the end, or
// non-zero bits in the last character's unused part.
std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text);

} // namespace etude
