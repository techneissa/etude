// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_opening.hpp"

#include <algorithm>

namespace etude {

SongOpening song_opening(const Song& song, double seconds) {
    SongOpening out;
    double first = -1.0;
    for (const Note& n : song.notes) {
        if (!n.drum && (first < 0.0 || n.start_s < first)) {
            first = n.start_s;
        }
    }
    if (first < 0.0) {
        return out;
    }
    int low = 127;
    int high = 0;
    for (const Note& n : song.notes) {
        const double at = n.start_s - first;
        if (n.drum || at >= seconds) {
            continue;
        }
        OpeningNote o;
        o.at = static_cast<float>(at);
        o.len = static_cast<float>(std::min(n.duration_s, seconds - at));
        o.key = n.pitch;
        o.velocity = n.velocity;
        o.hand = n.hand;
        out.notes.push_back(o);
        out.length_s = std::max(out.length_s, o.at + o.len);
        low = std::min(low, static_cast<int>(n.pitch));
        high = std::max(high, static_cast<int>(n.pitch));
    }
    std::stable_sort(out.notes.begin(), out.notes.end(),
                     [](const OpeningNote& a, const OpeningNote& b) { return a.at < b.at; });
    // Whole octaves round the notes, two at least, centred on them.
    int lo = (low / 12) * 12;
    int hi = (high / 12 + 1) * 12;
    while (hi - lo < 24) {
        if (lo >= 12 && (hi > 108 || (lo + hi) / 2 > (low + high) / 2)) {
            lo -= 12;
        } else {
            hi += 12;
        }
    }
    out.keys = KeyRange{std::max(0, lo), std::min(120, hi)};
    return out;
}

} // namespace etude
