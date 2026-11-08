// SPDX-License-Identifier: GPL-3.0-or-later
#include "harmony_internal.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace etude {

namespace {

// A stretch must hold this many beats of a pitch class, or sound it this many times, before the
// pitch class counts at all: one short note is a passing note.
constexpr double kEstablishedBeats = 0.75; // a played quarter note is a little under a beat
// The tune is searched in stretches of up to this many notes, and stands this far above the voice
// below it (semitones).
constexpr std::size_t kMaxTuneSearch = 600;
constexpr int kTuneGap = 12;

} // namespace

// The stretch's notes weighed. Linear in the notes of the stretch (and twelve times the notes for
// the simultaneity sweep).
Stats stats_of(const Song& song, const std::vector<std::uint32_t>& notes, const Frame& bar,
               std::int64_t t0, std::int64_t t1) {
    struct Span {
        std::int64_t s;
        std::int64_t e;
        std::int64_t start; // the note's own start, before the stretch clips it
        int pc;
        int pitch;
    };
    Stats st;
    std::vector<Span> spans;
    spans.reserve(notes.size());
    for (const std::uint32_t index : notes) {
        const Note& n = song.notes[index];
        const std::int64_t s = std::max(n.start_tick, t0);
        const std::int64_t e = std::min(n.end_tick, t1);
        if (e <= s) {
            continue;
        }
        spans.push_back({s, e, n.start_tick, pitch_class(n.pitch), n.pitch});
    }
    std::array<double, 128> pitch_weight{};
    std::array<std::int64_t, 12> last_attack;
    last_attack.fill(-1);
    const double group = static_cast<double>(bar.group_ticks);
    std::int64_t covered = 0;
    std::int64_t reach = t0;
    for (const Span& a : spans) {
        // Time in which some note sounds: spans start in order, so a running end is enough.
        covered += std::max<std::int64_t>(a.e - std::max(a.s, reach), 0);
        reach = std::max(reach, a.e);
        const double beats = static_cast<double>(a.e - a.s) / group;
        const double strength =
            a.start >= t0 ? harmony_beat_strength(static_cast<double>(a.start - bar.bar_t0) / group,
                                                  bar.groups)
                          : kHarmonyHeldOverStrength;
        const double w = harmony_note_weight(beats, strength);
        PcStat& p = st.pc[static_cast<std::size_t>(a.pc)];
        p.weight += w;
        p.longest = std::max(p.longest, beats);
        // An attack counts once, however many octaves of the pitch class it strikes.
        if (a.start >= t0 && a.start != last_attack[static_cast<std::size_t>(a.pc)]) {
            ++p.onsets;
            last_attack[static_cast<std::size_t>(a.pc)] = a.start;
        }
        pitch_weight[static_cast<std::size_t>(a.pitch & 127)] += w;
    }
    st.sounding = t1 > t0 ? static_cast<double>(covered) / static_cast<double>(t1 - t0) : 0.0;
    // Pitch classes that sound together for a while. Two spans overlap by at least `m` when each
    // starts `m` before the other ends; going through the spans in order of start, a span is paired
    // with every earlier pitch class whose latest end is at least `m` past its start.
    const auto m = static_cast<std::int64_t>(kHarmonyMinSimultaneous * group);
    std::array<std::int64_t, 12> latest_end;
    latest_end.fill(std::numeric_limits<std::int64_t>::min());
    for (const Span& b : spans) {
        if (b.e - b.s >= m) {
            for (int q = 0; q < 12; ++q) {
                if (q != b.pc && latest_end[static_cast<std::size_t>(q)] >= b.s + m) {
                    st.pc[static_cast<std::size_t>(q)].together = true;
                    st.pc[static_cast<std::size_t>(b.pc)].together = true;
                }
            }
        }
        latest_end[static_cast<std::size_t>(b.pc)] =
            std::max(latest_end[static_cast<std::size_t>(b.pc)], b.e);
    }
    for (PcStat& p : st.pc) {
        p.established =
            p.weight > 0.0 && (p.onsets >= kEstablishedOnsets || p.longest >= kEstablishedBeats);
        p.stable = p.established && (p.together || p.onsets >= kEstablishedOnsets);
        if (p.established) {
            st.total += p.weight;
            ++st.established;
        }
        st.stable += p.stable ? 1 : 0;
    }
    // The bass: the lowest note of real weight among the established pitch classes.
    double heaviest = 0.0;
    for (int pitch = 0; pitch < 128; ++pitch) {
        if (st.pc[static_cast<std::size_t>(pitch % 12)].established) {
            heaviest = std::max(heaviest, pitch_weight[static_cast<std::size_t>(pitch)]);
        }
    }
    for (int pitch = 0; pitch < 128 && heaviest > 0.0; ++pitch) {
        if (st.pc[static_cast<std::size_t>(pitch % 12)].established &&
            pitch_weight[static_cast<std::size_t>(pitch)] >= kHarmonyBassShare * heaviest) {
            st.bass_pc = pitch % 12;
            break;
        }
    }
    return st;
}

// The notes of a stretch that are its tune, sorted by index: the highest note sounding when it
// starts, the only start of its pitch, with the next voice below it at least an octave down. They
// do not place the label. A stretch of more than `kMaxTuneSearch` notes is searched no further
// (the search compares every pair, so it costs at most that number squared for a stretch).
std::vector<std::uint32_t> tune_of(const Song& song, const std::vector<std::uint32_t>& notes,
                                   std::int64_t t0, std::int64_t t1) {
    std::vector<std::uint32_t> tune;
    std::vector<std::uint32_t> in;
    for (const std::uint32_t index : notes) {
        const Note& n = song.notes[index];
        if (std::min(n.end_tick, t1) > std::max(n.start_tick, t0)) {
            in.push_back(index);
        }
    }
    if (in.size() > kMaxTuneSearch) {
        return tune;
    }
    for (const std::uint32_t ia : in) {
        const Note& a = song.notes[ia];
        if (a.start_tick < t0) {
            continue;
        }
        int below = -1; // the highest note sounding under it
        bool top = true;
        int pitch_starts = 0;
        for (const std::uint32_t ib : in) {
            const Note& b = song.notes[ib];
            pitch_starts += b.pitch == a.pitch && b.start_tick >= t0 ? 1 : 0;
            if (ib == ia || !(b.start_tick <= a.start_tick && b.end_tick > a.start_tick)) {
                continue;
            }
            if (b.pitch > a.pitch) {
                top = false;
                break;
            }
            below = std::max<int>(below, b.pitch);
        }
        if (top && pitch_starts == 1 && below >= 0 && a.pitch - below >= kTuneGap) {
            tune.push_back(ia);
        }
    }
    return tune; // in index order already
}

// The notes of a hand's stretch that are a separate top voice over its chords: a note that is the
// highest sounding when it starts and stands `kTuneGap` or more above the highest note sounding
// under it (a repeated or held top line counts, unlike `tune_of`'s single start). They are the tune
// of a hand that plays chords, and a name never comes from the tune. Sorted by index; a stretch of
// more than `kMaxTuneSearch` notes is not searched.
std::vector<std::uint32_t> top_voice_of(const Song& song, const std::vector<std::uint32_t>& notes,
                                        std::int64_t t0, std::int64_t t1) {
    std::vector<std::uint32_t> out;
    std::vector<std::uint32_t> in;
    for (const std::uint32_t index : notes) {
        const Note& n = song.notes[index];
        if (std::min(n.end_tick, t1) > std::max(n.start_tick, t0)) {
            in.push_back(index);
        }
    }
    if (in.size() > kMaxTuneSearch) {
        return out;
    }
    for (const std::uint32_t ia : in) {
        const Note& a = song.notes[ia];
        const std::int64_t at = std::max(a.start_tick, t0);
        int below = -1;
        bool top = true;
        for (const std::uint32_t ib : in) {
            const Note& b = song.notes[ib];
            if (ib == ia || !(b.start_tick <= at && b.end_tick > at)) {
                continue;
            }
            if (b.pitch > a.pitch) {
                top = false;
                break;
            }
            below = std::max<int>(below, b.pitch);
        }
        if (top && below >= 0 && a.pitch - below >= kTuneGap) {
            out.push_back(ia);
        }
    }
    return out; // in index order already
}

// Whether the stretch is one line (no two notes sound together for `kHarmonyMinSimultaneous` of a
// beat) that moves mostly by step, a second or less: a tune, whatever pitch classes it touches. A
// line that moves by the intervals of a chord (a broken chord, an Alberti bass) is harmony and not
// this.
bool stepwise_line(const Song& song, const std::vector<std::uint32_t>& notes, const Frame& bar,
                   std::int64_t t0, std::int64_t t1) {
    const auto overlap =
        static_cast<std::int64_t>(kHarmonyMinSimultaneous * static_cast<double>(bar.group_ticks));
    std::int64_t reach = std::numeric_limits<std::int64_t>::min();
    int previous = -1;
    int moves = 0;
    int steps = 0;
    for (const std::uint32_t index : notes) {
        const Note& n = song.notes[index];
        const std::int64_t s = std::max(n.start_tick, t0);
        const std::int64_t e = std::min(n.end_tick, t1);
        if (e <= s) {
            continue;
        }
        if (reach != std::numeric_limits<std::int64_t>::min() && s < reach - overlap) {
            return false; // two notes sound together: more than a line
        }
        reach = std::max(reach, e);
        if (n.start_tick < t0) {
            continue; // held in from before: not an attack of this stretch
        }
        if (previous >= 0 && n.pitch != previous) {
            ++moves;
            steps += std::abs(n.pitch - previous) <= 2 ? 1 : 0;
        }
        previous = n.pitch;
    }
    return moves >= 2 && steps * 2 > moves;
}

} // namespace etude
