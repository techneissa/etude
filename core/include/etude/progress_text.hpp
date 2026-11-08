// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

namespace etude {

// Words for the Progress page's numbers.

// "45 min", "1 h 05 min", "3 h 52 min"; a whole number of hours is "2 h".
inline std::string minutes_words(int minutes) {
    if (minutes < 0) {
        minutes = 0;
    }
    if (minutes < 60) {
        return std::to_string(minutes) + " min";
    }
    const int h = minutes / 60;
    const int m = minutes % 60;
    if (m == 0) {
        return std::to_string(h) + " h";
    }
    return std::to_string(h) + " h " + (m < 10 ? "0" : "") + std::to_string(m) + " min";
}

// "1st", "2nd", "3rd", "4th", "11th", "12th", "13th", "21st".
inline std::string ordinal_words(int n) {
    const int tens = (n % 100) / 10;
    const int last = n % 10;
    const char* suffix = "th";
    if (tens != 1) {
        suffix = last == 1 ? "st" : last == 2 ? "nd" : last == 3 ? "rd" : "th";
    }
    return std::to_string(n) + suffix;
}

// "bar 8", "bars 3 to 5" for Song bar numbers; a pickup bar is 0 and is called "the pickup".
inline std::string bars_words(int first, int last) {
    if (last <= first) {
        return first == 0 ? "the pickup" : "bar " + std::to_string(first);
    }
    return first == 0 ? "the pickup to bar " + std::to_string(last)
                      : "bars " + std::to_string(first) + " to " + std::to_string(last);
}

// A number with thousands separators: 1051 is "1,051".
inline std::string thousands(int n) {
    std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(digits[i]);
    }
    return (n < 0 ? "-" : "") + out;
}

} // namespace etude
