// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace etude {

namespace {

const char* unit_suffix(Unit unit) {
    switch (unit) {
    case Unit::Percent:
        return "%";
    case Unit::Milliseconds:
        return "ms";
    case Unit::Seconds:
        return "s";
    case Unit::Bpm:
        return "BPM";
    case Unit::Times:
        return "\xC3\x97"; // U+00D7
    case Unit::Count:
        return "";
    }
    return "";
}

constexpr char kMinus[] = "\xE2\x88\x92"; // U+2212

} // namespace

UnitText format_unit(double value, Unit unit, int decimals, bool plus) {
    UnitText out;
    if (!std::isfinite(value) || std::fabs(value) >= 1e9) {
        out.text[0] = '-';
        out.length = 1;
        return out;
    }
    decimals = std::clamp(decimals, 0, 3);
    // Digits of the magnitude first, so a value that rounds to zero loses its sign.
    std::array<char, 24> digits{};
    std::snprintf(digits.data(), digits.size(), "%.*f", decimals, std::fabs(value));
    bool zero = true;
    for (const char* p = digits.data(); *p != '\0'; ++p) {
        if (*p >= '1' && *p <= '9') {
            zero = false;
        }
    }
    std::size_t n = 0;
    const auto put = [&](const char* s) {
        const std::size_t len = std::strlen(s);
        if (n + len < out.text.size()) {
            std::memcpy(out.text.data() + n, s, len);
            n += len;
        }
    };
    if (!zero && value < 0.0) {
        put(kMinus);
    } else if (!zero && plus) {
        put("+");
    }
    put(digits.data());
    put(unit_suffix(unit));
    out.text[n] = '\0';
    out.length = n;
    return out;
}

} // namespace etude
