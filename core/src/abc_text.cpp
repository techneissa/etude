// SPDX-License-Identifier: GPL-3.0-or-later
#include "abc_text.hpp"

namespace etude::abc_detail {

bool equals_nocase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_of(a[i]) != lower_of(b[i])) {
            return false;
        }
    }
    return true;
}

LineSpan line_at(const char* text, std::size_t size, std::size_t begin) {
    LineSpan l;
    l.begin = begin;
    std::size_t i = begin;
    while (i < size && text[i] != '\n' && text[i] != '\r') {
        ++i;
    }
    l.end = i;
    if (i < size) {
        if (text[i] == '\r' && i + 1 < size && text[i + 1] == '\n') {
            ++i;
        }
        ++i;
    }
    l.next = i;
    return l;
}

std::string_view field_value(std::string_view s) {
    std::string_view v = s.substr(2);
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '%' && (i == 0 || is_space(v[i - 1]))) {
            v = v.substr(0, i);
            break;
        }
    }
    return trim(v);
}

} // namespace etude::abc_detail
