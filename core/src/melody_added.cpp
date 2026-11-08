// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/melody_added.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace etude {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

bool step_ok(int step) { return step >= kMelodyFirstStep && step <= kMelodyLastStep; }

bool listed(std::span<const std::uint64_t> hashes, std::uint64_t hash) {
    return std::find(hashes.begin(), hashes.end(), hash) != hashes.end();
}

// A path as one word: a space, a percent sign and a control byte written as %XX.
void write_path(std::string& out, std::string_view path) {
    for (const char c : path) {
        const auto b = static_cast<unsigned char>(c);
        if (b <= 0x20 || b == 0x7F || c == '%') {
            char escaped[4];
            std::snprintf(escaped, sizeof escaped, "%%%02X", b);
            out += escaped;
        } else {
            out += c;
        }
    }
}

// The path written by `write_path`; false when a % is not followed by two hex digits.
bool read_path(std::string_view word, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (word[i] != '%') {
            out += word[i];
            continue;
        }
        const bool room = i + 2 < word.size(); // two digits follow
        const int hi = room ? hex_value(static_cast<char>(std::tolower(word[i + 1]))) : -1;
        const int lo = room ? hex_value(static_cast<char>(std::tolower(word[i + 2]))) : -1;
        if (hi < 0 || lo < 0) {
            return false;
        }
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
    }
    return out.size() <= kMaxMelodyPath;
}

// One `HASH:STEP` or `HASH:STEP:PATH` word; false when it is not that.
bool read_word(std::string_view word, MelodyAddedSong& out) {
    if (word.size() < 18 || word[16] != ':' || (word.size() > 18 && word[18] != ':')) {
        return false;
    }
    std::uint64_t hash = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        const int d = hex_value(word[i]);
        if (d < 0) {
            return false;
        }
        hash = (hash << 4) | static_cast<std::uint64_t>(d);
    }
    const int step = word[17] - '0';
    if (hash == 0 || !step_ok(step)) {
        return false;
    }
    out = {hash, step, {}};
    return word.size() == 18 || read_path(word.substr(19), out.path);
}

} // namespace

MelodyAdded MelodyAdded::parse(std::string_view text) {
    MelodyAdded list;
    std::size_t at = 0;
    while (at < text.size() && list.songs_.size() < kMaxMelodyStored) {
        std::size_t end = text.find(' ', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        MelodyAddedSong song;
        if (end > at && read_word(text.substr(at, end - at), song) && list.step_of(song.hash) == 0) {
            list.songs_.push_back(std::move(song));
        }
        at = end + 1;
    }
    return list;
}

std::string MelodyAdded::text() const {
    std::string out;
    for (const MelodyAddedSong& s : songs_) {
        char head[24];
        std::snprintf(head, sizeof head, "%016llx:%d", static_cast<unsigned long long>(s.hash), s.step);
        if (!out.empty()) {
            out += ' ';
        }
        out += head;
        if (!s.path.empty()) {
            out += ':';
            write_path(out, s.path);
        }
    }
    return out;
}

int MelodyAdded::step_of(std::uint64_t hash, std::string_view path) const {
    for (const MelodyAddedSong& s : songs_) {
        if (s.hash == hash) {
            return s.step;
        }
    }
    if (!path.empty()) {
        for (const MelodyAddedSong& s : songs_) {
            if (s.path == path) {
                return s.step;
            }
        }
    }
    return 0;
}

std::size_t MelodyAdded::visible(std::span<const std::uint64_t> hidden) const {
    return static_cast<std::size_t>(std::count_if(
        songs_.begin(), songs_.end(), [&](const MelodyAddedSong& s) { return !listed(hidden, s.hash); }));
}

bool MelodyAdded::set(std::uint64_t hash, int step, std::string_view path,
                      std::span<const std::uint64_t> hidden) {
    if (hash == 0 || !step_ok(step)) {
        return false;
    }
    const std::string kept = path.size() <= kMaxMelodyPath ? std::string(path) : std::string();
    for (MelodyAddedSong& s : songs_) {
        if (s.hash == hash) {
            s.step = step;
            if (!kept.empty()) {
                s.path = kept;
            }
            return true;
        }
    }
    if (!kept.empty()) {
        for (MelodyAddedSong& s : songs_) {
            if (s.path == kept) {
                s.hash = hash; // the same file, edited
                s.step = step;
                return true;
            }
        }
    }
    if (visible(hidden) >= kMaxMelodyAdded) {
        return false;
    }
    if (songs_.size() >= kMaxMelodyStored) {
        // Room: the oldest entry the library does not have goes.
        const auto gone = std::find_if(songs_.begin(), songs_.end(),
                                       [&](const MelodyAddedSong& s) { return listed(hidden, s.hash); });
        if (gone == songs_.end()) {
            return false;
        }
        songs_.erase(gone);
    }
    songs_.push_back({hash, step, kept});
    return true;
}

bool MelodyAdded::remove(std::uint64_t hash, std::string_view path) {
    const auto it = std::find_if(songs_.begin(), songs_.end(), [&](const MelodyAddedSong& s) {
        return s.hash == hash || (!path.empty() && s.path == path);
    });
    if (it == songs_.end()) {
        return false;
    }
    songs_.erase(it);
    return true;
}

bool MelodyAdded::rebind(std::uint64_t old_hash, std::uint64_t new_hash, std::string_view path) {
    const auto it = std::find_if(songs_.begin(), songs_.end(),
                                 [&](const MelodyAddedSong& s) { return s.hash == old_hash; });
    if (it == songs_.end()) {
        return false;
    }
    if (new_hash != old_hash && step_of(new_hash) != 0) {
        songs_.erase(it); // the new hash is an entry of its own already
        return true;
    }
    it->hash = new_hash;
    if (path.size() <= kMaxMelodyPath) {
        it->path = std::string(path);
    }
    return true;
}

} // namespace etude
