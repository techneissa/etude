// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/upload_network.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace etude {

namespace {

bool parse_v4(std::string_view text, std::uint8_t out[4]) {
    std::size_t part = 0;
    std::size_t digits = 0;
    unsigned value = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '.') {
            if (digits == 0 || part >= 4) {
                return false;
            }
            out[part++] = static_cast<std::uint8_t>(value);
            digits = 0;
            value = 0;
            continue;
        }
        if (text[i] < '0' || text[i] > '9' || ++digits > 3) {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(text[i] - '0');
        if (value > 255) {
            return false;
        }
    }
    return part == 4;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// The groups of an IPv6 address on one side of "::" (or all of it), each a 16-bit value; an
// embedded IPv4 tail counts as two groups. False on anything malformed.
bool parse_groups(std::string_view text, std::uint16_t* groups, std::size_t& count,
                  std::size_t limit) {
    count = 0;
    if (text.empty()) {
        return true;
    }
    std::size_t start = 0;
    for (;;) {
        const std::size_t colon = text.find(':', start);
        const std::string_view piece =
            text.substr(start, colon == std::string_view::npos ? colon : colon - start);
        if (piece.empty()) {
            return false;
        }
        if (piece.find('.') != std::string_view::npos) {
            std::uint8_t v4[4];
            if (colon != std::string_view::npos || count + 2 > limit || !parse_v4(piece, v4)) {
                return false; // an IPv4 tail ends the address
            }
            groups[count++] = static_cast<std::uint16_t>((v4[0] << 8) | v4[1]);
            groups[count++] = static_cast<std::uint16_t>((v4[2] << 8) | v4[3]);
            return true;
        }
        if (piece.size() > 4 || count >= limit) {
            return false;
        }
        unsigned value = 0;
        for (const char c : piece) {
            const int h = hex_value(c);
            if (h < 0) {
                return false;
            }
            value = value * 16 + static_cast<unsigned>(h);
        }
        groups[count++] = static_cast<std::uint16_t>(value);
        if (colon == std::string_view::npos) {
            return true;
        }
        start = colon + 1;
    }
}

bool parse_v6(std::string_view text, std::uint8_t out[16]) {
    const std::size_t zone = text.find('%');
    if (zone != std::string_view::npos) {
        text = text.substr(0, zone);
    }
    std::uint16_t head[8] = {};
    std::uint16_t tail[8] = {};
    std::size_t head_count = 0;
    std::size_t tail_count = 0;
    const std::size_t gap = text.find("::");
    if (gap == std::string_view::npos) {
        if (!parse_groups(text, head, head_count, 8) || head_count != 8) {
            return false;
        }
    } else {
        if (text.find("::", gap + 1) != std::string_view::npos ||
            !parse_groups(text.substr(0, gap), head, head_count, 7) ||
            !parse_groups(text.substr(gap + 2), tail, tail_count, 7) ||
            head_count + tail_count > 7) {
            return false;
        }
    }
    std::uint16_t all[8] = {};
    std::copy(head, head + head_count, all);
    std::copy(tail, tail + tail_count, all + (8 - tail_count));
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 2] = static_cast<std::uint8_t>(all[i] >> 8);
        out[i * 2 + 1] = static_cast<std::uint8_t>(all[i] & 0xFF);
    }
    return true;
}

bool local_v4(const std::uint8_t* a) {
    return a[0] == 127 || a[0] == 10 || (a[0] == 192 && a[1] == 168) ||
           (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 169 && a[1] == 254);
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool is_port_text(std::string_view text) {
    return !text.empty() && text.size() <= 5 &&
           std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

} // namespace

bool parse_address(std::string_view text, std::uint8_t out[16], std::size_t& length) {
    if (text.find(':') == std::string_view::npos) {
        length = 4;
        return parse_v4(text, out);
    }
    length = 16;
    return parse_v6(text, out);
}

bool is_local_address(std::span<const std::uint8_t> bytes) {
    if (bytes.size() == 4) {
        return local_v4(bytes.data());
    }
    if (bytes.size() != 16) {
        return false;
    }
    const std::uint8_t* a = bytes.data();
    const bool zeros = std::all_of(a, a + 10, [](std::uint8_t b) { return b == 0; });
    if (zeros && a[10] == 0xFF && a[11] == 0xFF) {
        return local_v4(a + 12); // ::ffff:a.b.c.d
    }
    const bool loopback =
        std::all_of(a, a + 15, [](std::uint8_t b) { return b == 0; }) && a[15] == 1;
    return loopback || (a[0] & 0xFE) == 0xFC || (a[0] == 0xFE && (a[1] & 0xC0) == 0x80);
}

bool host_header_ok(std::string_view host, std::span<const std::string_view> names) {
    std::string_view name = host;
    if (!host.empty() && host.front() == '[') {
        const std::size_t close = host.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        const std::string_view rest = host.substr(close + 1);
        if (!rest.empty() && (rest.front() != ':' || !is_port_text(rest.substr(1)))) {
            return false;
        }
        std::uint8_t bytes[16];
        std::size_t length = 0;
        const std::string_view inner = host.substr(1, close - 1);
        return inner.find(':') != std::string_view::npos && parse_address(inner, bytes, length) &&
               length == 16;
    }
    const std::size_t colon = host.find(':');
    if (colon != std::string_view::npos) {
        if (host.find(':', colon + 1) != std::string_view::npos ||
            !is_port_text(host.substr(colon + 1))) {
            return false;
        }
        name = host.substr(0, colon);
    }
    std::uint8_t bytes[16];
    std::size_t length = 0;
    if (parse_address(name, bytes, length)) {
        return true;
    }
    const std::string folded = lower(name);
    return folded == "localhost" ||
           std::any_of(names.begin(), names.end(),
                       [&](std::string_view n) { return folded == lower(n); });
}

bool origin_ok(std::string_view origin, std::string_view host) {
    if (origin.empty()) {
        return true;
    }
    constexpr std::string_view kScheme = "http://";
    if (origin.size() <= kScheme.size() || origin.substr(0, kScheme.size()) != kScheme) {
        return false; // "null" (a sandboxed page), https, or anything else
    }
    return !host.empty() && lower(origin.substr(kScheme.size())) == lower(host);
}

bool is_private_lan_address(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 4) {
        return false;
    }
    const std::uint8_t* a = bytes.data();
    return a[0] == 10 || (a[0] == 192 && a[1] == 168) ||
           (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 169 && a[1] == 254);
}

std::vector<std::string> choose_listen_addresses(std::span<const std::string> machine_addresses,
                                                 bool all_interfaces) {
    if (all_interfaces) {
        return {"0.0.0.0"};
    }
    std::vector<std::string> chosen;
    for (const std::string& text : machine_addresses) {
        std::uint8_t bytes[16];
        std::size_t length = 0;
        if (parse_address(text, bytes, length) && length == 4 &&
            is_private_lan_address({bytes, 4}) &&
            std::find(chosen.begin(), chosen.end(), text) == chosen.end()) {
            chosen.push_back(text);
        }
    }
    if (chosen.empty()) {
        chosen.push_back("127.0.0.1");
    }
    return chosen;
}

std::string_view cookie_value(std::string_view header, std::string_view name) {
    std::size_t at = 0;
    while (at < header.size()) {
        while (at < header.size() && (header[at] == ' ' || header[at] == ';')) {
            ++at;
        }
        const std::size_t end = std::min(header.find(';', at), header.size());
        const std::string_view pair = header.substr(at, end - at);
        const std::size_t equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == name) {
            std::string_view value = pair.substr(equals + 1);
            while (!value.empty() && value.back() == ' ') {
                value.remove_suffix(1);
            }
            return value;
        }
        at = end + 1;
    }
    return {};
}

} // namespace etude
