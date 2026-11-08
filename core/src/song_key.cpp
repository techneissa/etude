// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_key.hpp"

#include "etude/content_hash.hpp"

namespace etude {

std::string normalized_song_path(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') {
            c = '/';
        }
        if (c == '/' && !out.empty() && out.back() == '/') {
            continue;
        }
        out += c;
    }
    while (out.size() >= 2 && out[0] == '.' && out[1] == '/') {
        out.erase(0, 2);
    }
    while (!out.empty() && out.back() == '/') {
        out.pop_back();
    }
    return out == "." ? std::string() : out;
}

std::string song_key(std::uint64_t content_hash, std::string_view path) {
    if (content_hash != 0) {
        return "h:" + content_hash_hex(content_hash);
    }
    const std::string normalized = normalized_song_path(path);
    return normalized.empty() ? std::string() : "p:" + normalized;
}

} // namespace etude
