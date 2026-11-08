// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/semver.hpp"

#include <algorithm>
#include <charconv>
#include <vector>

namespace etude {
namespace {

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_identifier_char(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool all_digits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), is_digit);
}

// A number with no leading zero that fits 32 bits.
bool parse_number(std::string_view s, std::uint32_t& out) {
    if (!all_digits(s) || (s.size() > 1 && s.front() == '0')) {
        return false;
    }
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), out);
    return error == std::errc() && end == s.data() + s.size();
}

std::vector<std::string_view> split(std::string_view s, char separator) {
    std::vector<std::string_view> parts;
    std::size_t from = 0;
    for (;;) {
        const std::size_t at = s.find(separator, from);
        if (at == std::string_view::npos) {
            parts.push_back(s.substr(from));
            return parts;
        }
        parts.push_back(s.substr(from, at - from));
        from = at + 1;
    }
}

// Every dot-separated identifier is non-empty, made of [0-9A-Za-z-], and a numeric one has no
// leading zero. `allow_leading_zero` is for build metadata, which the standard lets have them.
bool valid_identifiers(std::string_view s, bool allow_leading_zero) {
    for (const std::string_view id : split(s, '.')) {
        if (id.empty() || !std::all_of(id.begin(), id.end(), is_identifier_char)) {
            return false;
        }
        if (!allow_leading_zero && all_digits(id) && id.size() > 1 && id.front() == '0') {
            return false;
        }
    }
    return true;
}

int compare_identifiers(std::string_view a, std::string_view b) {
    const bool a_number = all_digits(a);
    const bool b_number = all_digits(b);
    if (a_number && b_number) { // no leading zeros, so length then text is the numeric order
        if (a.size() != b.size()) {
            return a.size() < b.size() ? -1 : 1;
        }
    } else if (a_number != b_number) {
        return a_number ? -1 : 1;
    }
    const int c = a.compare(b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

int compare_prerelease(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) { // a release is newer than its own prerelease
        return a.empty() == b.empty() ? 0 : (a.empty() ? 1 : -1);
    }
    const std::vector<std::string_view> left = split(a, '.');
    const std::vector<std::string_view> right = split(b, '.');
    const std::size_t common = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (const int c = compare_identifiers(left[i], right[i]); c != 0) {
            return c;
        }
    }
    if (left.size() == right.size()) {
        return 0;
    }
    return left.size() < right.size() ? -1 : 1;
}

} // namespace

std::optional<SemVer> parse_semver(std::string_view text) {
    if (const std::size_t plus = text.find('+'); plus != std::string_view::npos) {
        if (!valid_identifiers(text.substr(plus + 1), true)) {
            return std::nullopt;
        }
        text = text.substr(0, plus);
    }
    SemVer v;
    if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
        const std::string_view pre = text.substr(dash + 1);
        if (!valid_identifiers(pre, false)) {
            return std::nullopt;
        }
        v.prerelease = std::string(pre);
        text = text.substr(0, dash);
    }
    const std::vector<std::string_view> numbers = split(text, '.');
    if (numbers.size() != 3 || !parse_number(numbers[0], v.major) ||
        !parse_number(numbers[1], v.minor) || !parse_number(numbers[2], v.patch)) {
        return std::nullopt;
    }
    return v;
}

int compare_semver(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch) {
        return a.patch < b.patch ? -1 : 1;
    }
    return compare_prerelease(a.prerelease, b.prerelease);
}

VersionOrder compare_versions(std::string_view offered, std::string_view installed) {
    const std::optional<SemVer> a = parse_semver(offered);
    const std::optional<SemVer> b = parse_semver(installed);
    if (!a || !b) {
        return VersionOrder::Malformed;
    }
    const int c = compare_semver(*a, *b);
    return c > 0 ? VersionOrder::Newer : (c < 0 ? VersionOrder::Older : VersionOrder::Same);
}

} // namespace etude
