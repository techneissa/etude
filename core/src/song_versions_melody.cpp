// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_versions.hpp"

#include "song_versions_internal.hpp"

#include "etude/difficulty.hpp"

#include <algorithm>
#include <array>

namespace etude {

// ---- The melody's fingerprint

std::vector<int> pitches_of(const MelodyContour& c) {
    std::vector<int> out;
    out.reserve(c.steps.size() + 1);
    int p = 0;
    out.push_back(p);
    for (const std::int8_t s : c.steps) {
        p += s;
        out.push_back(p);
    }
    return out;
}

namespace {

// Alignment scores, in units: a note that lines up, a wrong note, a note on one side only (an
// ornament, a passing note) and a note left over before the first match or after the last, which
// costs more, so a short melody that matches only a stretch of a longer one scores low.
constexpr int kMatch = 8;
constexpr int kMismatch = -6;
constexpr int kGap = -2;
constexpr int kEndGap = -4;
// Offsets (key changes) tried per pair, the commonest ones between the two melodies' notes.
constexpr int kOffsetsTried = 3;

int distinct_pitches(const std::vector<int>& p) {
    std::vector<int> sorted = p;
    std::sort(sorted.begin(), sorted.end());
    return static_cast<int>(std::unique(sorted.begin(), sorted.end()) - sorted.begin());
}

// The best alignment of `a` (moved by `offset`) with `b` that starts and ends on a note that lines
// up: each note of either melody before the first match or after the last costs `kEndGap`, a note
// between matches that has no partner `kGap`, a wrong pair `kMismatch`.
int align(const std::vector<int>& a, const std::vector<int>& b, int offset, Scratch& scratch) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    constexpr int kNone = -1'000'000;          // no alignment reaches the cell
    std::vector<int>& h_prev = scratch.h_prev; // row i - 1
    std::vector<int>& h_cur = scratch.h_cur;   // row i: best score of an alignment ending at (i, j)
    h_prev.assign(m + 1, kNone);
    h_cur.assign(m + 1, kNone);
    int best = 0;
    for (std::size_t i = 1; i <= n; ++i) {
        h_cur[0] = kNone;
        for (std::size_t j = 1; j <= m; ++j) {
            const int diag = h_prev[j - 1];
            int cell = kNone;
            if (a[i - 1] + offset == b[j - 1]) {
                // A match starts an alignment here, with the notes before it left over, or goes on
                // from the cell before.
                const int start = kEndGap * static_cast<int>((i - 1) + (j - 1));
                const int score = kMatch + std::max(start, diag);
                cell = score;
                best = std::max(best, score + kEndGap * static_cast<int>((n - i) + (m - j)));
            } else if (diag > kNone / 2) {
                cell = diag + kMismatch;
            }
            if (h_cur[j - 1] > kNone / 2) {
                cell = std::max(cell, h_cur[j - 1] + kGap);
            }
            if (h_prev[j] > kNone / 2) {
                cell = std::max(cell, h_prev[j] + kGap);
            }
            h_cur[j] = cell;
        }
        std::swap(h_prev, h_cur);
    }
    return best;
}

} // namespace

// The similarity of two melodies given as pitches (the first at 0), usable or not.
double similarity_of(const std::vector<int>& pa, const std::vector<int>& pb, Scratch& scratch) {
    // The key change that lines the most notes up is the likeliest one; try the commonest few.
    std::array<int, 2 * kMaxShift + 1>& votes = scratch.votes;
    votes.fill(0);
    for (const int x : pa) {
        for (const int y : pb) {
            ++votes[static_cast<std::size_t>(y - x + kMaxShift)];
        }
    }
    std::array<int, kOffsetsTried> offsets{};
    int tried = 0;
    for (int t = 0; t < kOffsetsTried; ++t) {
        int best = 0;
        std::size_t at = votes.size();
        for (std::size_t d = 0; d < votes.size(); ++d) {
            if (votes[d] > best) {
                best = votes[d];
                at = d;
            }
        }
        if (at == votes.size() || best < 2) {
            break;
        }
        offsets[static_cast<std::size_t>(tried++)] = static_cast<int>(at) - kMaxShift;
        votes[at] = 0;
    }
    int best = 0;
    for (int t = 0; t < tried; ++t) {
        best = std::max(best, align(pa, pb, offsets[static_cast<std::size_t>(t)], scratch));
    }
    const double most =
        static_cast<double>(kMatch) * static_cast<double>(std::min(pa.size(), pb.size()));
    return std::clamp(static_cast<double>(best) / most, 0.0, 1.0);
}

bool MelodyContour::usable() const {
    if (steps.size() + 1 < kMelodyMinNotes) {
        return false;
    }
    return static_cast<std::size_t>(distinct_pitches(pitches_of(*this))) >= kMelodyMinPitches;
}

MelodyContour melody_contour(const Song& song) {
    MelodyContour out;
    const Note* first = nullptr;
    for (const Note& n : song.notes) {
        if (!n.drum && !n.accompaniment) {
            first = &n;
            break;
        }
    }
    if (first == nullptr) {
        return out;
    }
    // Bars from the first note's bar; a pickup bar (0) comes before them and is not counted.
    const int last_bar = first->measure + kMelodyBars - (first->measure == 0 ? 0 : 1);
    const std::int64_t tpq = std::max(1, song.tempo.ticks_per_quarter);
    const std::int64_t same_onset = std::max<std::int64_t>(1, tpq / 8); // a 32nd note
    const std::int64_t held = std::max<std::int64_t>(1, tpq / 4);       // a 16th note
    struct Sounding {
        std::int64_t end = 0;
        int pitch = 0;
    };
    std::vector<Sounding> sounding;
    std::vector<int> line;
    std::size_t i = 0;
    const std::size_t count = song.notes.size();
    while (i < count && line.size() < kMelodyMaxNotes) {
        const Note& head = song.notes[i];
        if (head.drum || head.accompaniment) {
            ++i;
            continue;
        }
        if (head.measure > last_bar) {
            break;
        }
        // The notes that start together: the top one is the melody's, unless a higher note that
        // started earlier is still held (this one is then an inner voice).
        const std::int64_t at = head.start_tick;
        int top = head.pitch;
        std::size_t j = i;
        for (; j < count && song.notes[j].start_tick - at < same_onset; ++j) {
            const Note& n = song.notes[j];
            if (n.drum || n.accompaniment) {
                continue;
            }
            top = std::max<int>(top, n.pitch);
        }
        std::erase_if(sounding, [&](const Sounding& s) { return s.end <= at; });
        bool inner = false;
        for (const Sounding& s : sounding) {
            if (s.pitch > top && s.end - at > held) {
                inner = true;
                break;
            }
        }
        for (std::size_t k = i; k < j; ++k) {
            const Note& n = song.notes[k];
            if (!n.drum && !n.accompaniment) {
                sounding.push_back({n.end_tick, n.pitch});
            }
        }
        if (!inner && (line.empty() || line.back() != top)) {
            line.push_back(top);
        }
        i = j;
    }
    if (line.size() < 2) {
        return out;
    }
    out.steps.reserve(line.size() - 1);
    for (std::size_t k = 1; k < line.size(); ++k) {
        out.steps.push_back(static_cast<std::int8_t>(std::clamp(line[k] - line[k - 1], -127, 127)));
    }
    return out;
}

double melody_similarity(const MelodyContour& a, const MelodyContour& b) {
    if (!a.usable() || !b.usable()) {
        return 0.0;
    }
    Scratch scratch;
    return similarity_of(pitches_of(a), pitches_of(b), scratch);
}

} // namespace etude
