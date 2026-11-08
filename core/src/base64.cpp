// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/base64.hpp"

#include <cstddef>

namespace etude {
namespace {

int sextet(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    return c == '+' ? 62 : (c == '/' ? 63 : -1);
}

} // namespace

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::size_t padding = 0;
    while (padding < 2 && padding < text.size() && text[text.size() - 1 - padding] == '=') {
        ++padding;
    }
    const std::size_t data_chars = text.size() - padding;
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t bits = 0;
    int held = 0; // bits in `bits` not yet written out
    for (std::size_t i = 0; i < data_chars; ++i) {
        const int value = sextet(text[i]);
        if (value < 0) {
            return std::nullopt;
        }
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        held += 6;
        if (held >= 8) {
            held -= 8;
            out.push_back(static_cast<std::uint8_t>((bits >> held) & 0xFFU));
        }
    }
    if (held != 0 && (bits & ((1U << held) - 1U)) != 0) {
        return std::nullopt; // the unused bits of the last character must be zero
    }
    return out;
}

} // namespace etude
