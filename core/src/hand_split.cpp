// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hand_split.hpp"

#include "part_index.hpp"

#include "etude/instrument.hpp"
#include "etude/staff_plan.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>

namespace etude {

namespace {

constexpr double kMinClusterShare = 0.15;
constexpr double kMinClusterSeparation = 8.0;
constexpr double kSideBias = 1.0;
constexpr double kInitialSpread = 6.0;
constexpr int kLloydIterations = 6;

// The cost model of the smart split, in semitones of "how unlikely": the way through the chords
// that costs least wins (see hand_split.hpp).
constexpr double kSingleRegisterNear = 4.0;       // one register: within this many semitones of the
constexpr double kSingleRegisterNearSlope = 0.25; // split point a note costs this much per semitone
constexpr double kSingleRegisterSlope = 3.0;      // and beyond it this much,
constexpr double kSingleRegisterRange = 6.0;      // up to this many more semitones
constexpr int kComfortableSpan = 12;              // a hand spans an octave without effort
constexpr double kSpanCost = 3.0;                 // per semitone beyond it
constexpr double kBeyondReachCost = 30.0;         // more than kHandReachSemitones
constexpr int kHandNotes = 5;                     // a hand has five fingers
constexpr double kExtraNoteCost = 1.0;            // per note beyond them
constexpr int kNaturalGap = 6;                    // a chord falls apart at a gap of a tritone
constexpr double kCutCost = 1.0;                  // per semitone a cut falls short of it
constexpr double kContinueCost = 1.0;       // how much a hand playing on from its last notes counts
constexpr double kFreshStartCost = 3.0;     // a note the hand has not been near: a fresh start
constexpr double kRepeatBonus = 6.0;        // a note it just played again
constexpr double kBeamCost = 20.0;          // a way this much dearer than the cheapest is dropped
constexpr double kFixedHandCost = 1000.0;   // a note with a hand on the other side of the cut

// Notes that start together: indices into song.notes, sorted by pitch.
struct SimultaneousNotes {
    std::vector<std::size_t> notes;
    std::int64_t start_tick = 0;
};

// Pitch histogram of the notes inside a window of the song.
struct Histogram {
    std::array<int, 128> count{};
    std::array<std::uint64_t, 2> present{}; // bit p: some note of pitch p is in the window
    int total = 0;
    void add(int pitch) {
        const auto p = static_cast<std::size_t>(pitch);
        if (count[p]++ == 0) {
            present[p >> 6] |= std::uint64_t{1} << (p & 63);
        }
        ++total;
    }
    void remove(int pitch) {
        const auto p = static_cast<std::size_t>(pitch);
        if (--count[p] == 0) {
            present[p >> 6] &= ~(std::uint64_t{1} << (p & 63));
        }
        --total;
    }
    // Calls f(pitch, count) for every pitch in the window, low to high.
    template <class F> void each(F&& f) const {
        for (std::size_t word = 0; word < present.size(); ++word) {
            for (std::uint64_t bits = present[word]; bits != 0; bits &= bits - 1) {
                const std::size_t p = word * 64 + static_cast<std::size_t>(std::countr_zero(bits));
                f(static_cast<int>(p), count[p]);
            }
        }
    }
};

// The two registers the hands play in: mean pitch of the low and of the high group.
struct Registers {
    double low = 0.0;
    double high = 0.0;
};

// True when `pitch` belongs to the high register: it is nearer to the high centre, with a small
// pull toward the side of the split point it lies on, so the split point still counts for a note
// that sits between the centres. A tie goes by the split point.
bool in_high_register(double pitch, const Registers& r, int split) {
    double to_low = std::abs(pitch - r.low) + (pitch >= split ? kSideBias : 0.0);
    double to_high = std::abs(pitch - r.high) + (pitch < split ? kSideBias : 0.0);
    return to_high < to_low || (to_high == to_low && pitch >= split);
}

// Fits two registers to the window by k-means (Lloyd's iterations), warm-started from the
// registers of the previous chord, so a passage keeps the groups it already had instead of jumping
// to a different grouping of the same pitches. Returns false when the window does not look like
// two registers (see hand_split.hpp); `fit` is then untouched.
bool fit_registers(const Histogram& h, const Registers& start, int split, Registers& fit) {
    if (h.total < 4) {
        return false;
    }
    Registers r = start;
    int low_n = 0;
    int high_n = 0;
    for (int iteration = 0; iteration < kLloydIterations; ++iteration) {
        double low_sum = 0.0;
        double high_sum = 0.0;
        low_n = 0;
        high_n = 0;
        h.each([&](int p, int c) {
            if (in_high_register(p, r, split)) {
                high_sum += static_cast<double>(c) * p;
                high_n += c;
            } else {
                low_sum += static_cast<double>(c) * p;
                low_n += c;
            }
        });
        Registers next{low_n > 0 ? low_sum / low_n : r.low,
                       high_n > 0 ? high_sum / high_n : r.high};
        bool same = next.low == r.low && next.high == r.high;
        r = next;
        if (same) {
            break;
        }
    }
    int min_side = std::max(2, static_cast<int>(std::ceil(kMinClusterShare * h.total)));
    if (low_n < min_side || high_n < min_side || r.high - r.low < kMinClusterSeparation) {
        return false;
    }
    fit = r;
    return true;
}

// The last few notes a hand played: pitch and start, newest first.
constexpr std::size_t kRecentNotes = 6;
struct Recent {
    std::array<std::int16_t, kRecentNotes> pitch{};
    std::array<std::int64_t, kRecentNotes> tick{};
    std::size_t count = 0;
    // Puts the notes pitch[a..b) of a chord that started at `at` in front.
    void push(const std::vector<int>& pitches, std::size_t a, std::size_t b, std::int64_t at) {
        std::array<std::int16_t, kRecentNotes> p2{};
        std::array<std::int64_t, kRecentNotes> t2{};
        std::size_t n = 0;
        for (std::size_t k = b; k > a && n < kRecentNotes; --k) {
            p2[n] = static_cast<std::int16_t>(pitches[k - 1]);
            t2[n] = at;
            ++n;
        }
        for (std::size_t k = 0; k < count && n < kRecentNotes; ++k) {
            p2[n] = pitch[k];
            t2[n] = tick[k];
            ++n;
        }
        pitch = p2;
        tick = t2;
        count = n;
    }
};

// Where to cut a chord wider than a hand: the first note of the right hand part.
std::size_t cut_chord(const Song& song, const std::vector<std::size_t>& sorted_notes,
                      double boundary) {
    auto pitch = [&](std::size_t k) { return static_cast<int>(song.notes[sorted_notes[k]].pitch); };
    std::size_t n = sorted_notes.size();
    std::size_t best = 1;
    double best_cost = 0.0;
    bool best_reachable = false;
    bool have = false;
    for (std::size_t j = 1; j < n; ++j) { // lower part: [0, j), upper part: [j, n)
        bool reachable = pitch(j - 1) - pitch(0) <= kHandReachSemitones &&
                         pitch(n - 1) - pitch(j) <= kHandReachSemitones;
        double midpoint = (pitch(j - 1) + pitch(j)) / 2.0;
        double cost = std::abs(midpoint - boundary);
        bool better = !have || (reachable && !best_reachable) ||
                      (reachable == best_reachable && cost < best_cost);
        if (better) {
            have = true;
            best = j;
            best_cost = cost;
            best_reachable = reachable;
        }
    }
    return best;
}

double median_pitch(const Song& song, const std::vector<std::size_t>& sorted_notes) {
    std::size_t n = sorted_notes.size();
    auto p = [&](std::size_t k) { return static_cast<double>(song.notes[sorted_notes[k]].pitch); };
    return n % 2 == 1 ? p(n / 2) : (p(n / 2 - 1) + p(n / 2)) / 2.0;
}

// Per note of the song: true when it belongs to an accompaniment part (or already carries the
// flag).
std::vector<bool> accompaniment_mask(const Song& song) {
    std::vector<bool> mask(song.notes.size(), false);
    std::map<std::pair<std::uint16_t, std::uint8_t>, bool> by_part;
    const bool keyboard = has_keyboard_part(song);
    for (const Part& p : song.parts) {
        by_part[{p.track, p.channel}] = p.accompaniment || is_accompaniment_part(p, keyboard);
    }
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        const Note& n = song.notes[i];
        if (n.drum) {
            continue;
        }
        auto it = by_part.find({n.track, n.channel});
        mask[i] = n.accompaniment || (it != by_part.end() && it->second);
    }
    return mask;
}

// True when `idx` (the notes of one part, sorted by start) is one melodic line:
// - at most kSingleLineChordFraction of its onsets are chords (an onset is the notes starting
//   within kChordWindowSeconds of its first note; a chord has two or more). A note held into the
//   next one, with onsets further apart than the chord window, is legato or pedal and never
//   counts, however long it is held;
// - and it does not look like two hands woven into one part: no more than kInterleavedLeapFraction
//   of the steps from one onset to the next leap further than an octave (an Alberti bass or a
//   stride pattern alternates between registers), and no more than kHeldAwayFraction of the onsets
//   sound over a note held since two onsets earlier that lies more than an octave from them.
bool is_single_line(const Song& song, const std::vector<std::size_t>& idx) {
    if (idx.empty()) {
        return false;
    }
    struct Onset {
        std::size_t notes = 0;
        std::vector<int> pitches;
        double start = 0.0;
        int first_pitch = 0; // the first note starting here, and when it ends
        double first_end = 0.0;
    };
    std::vector<Onset> onsets;
    double first_start = 0.0;
    for (std::size_t i : idx) {
        const Note& n = song.notes[i];
        if (onsets.empty() || n.start_s - first_start > kChordWindowSeconds) {
            onsets.emplace_back();
            onsets.back().start = n.start_s;
            onsets.back().first_pitch = n.pitch;
            onsets.back().first_end = n.start_s + n.duration_s;
            first_start = n.start_s;
        }
        ++onsets.back().notes;
        onsets.back().pitches.push_back(n.pitch);
    }
    const auto count = static_cast<double>(onsets.size());
    std::size_t chords = 0;
    std::vector<int> middles;
    middles.reserve(onsets.size());
    for (Onset& o : onsets) {
        chords += o.notes >= 2 ? 1 : 0;
        std::sort(o.pitches.begin(), o.pitches.end());
        middles.push_back(o.pitches[o.pitches.size() / 2]);
    }
    constexpr double kEpsilon = 1e-9; // 10% of 20 onsets is exactly two
    if (static_cast<double>(chords) > kSingleLineChordFraction * count + kEpsilon) {
        return false;
    }
    std::size_t leaps = 0;
    for (std::size_t k = 1; k < middles.size(); ++k) {
        leaps += std::abs(middles[k] - middles[k - 1]) > 12 ? 1 : 0;
    }
    const auto steps = static_cast<double>(middles.size() - 1);
    if (static_cast<double>(leaps) > kInterleavedLeapFraction * steps + kEpsilon) {
        return false;
    }
    // A note that still sounds two onsets later and lies over an octave from the notes it sounds
    // under is another line, a held bass under an arpeggio. (A note held into the next never
    // counts, nor do notes held on stepwise: that is pedal.) Only the latest few are looked at.
    constexpr std::size_t kHeldLook = 16;
    std::array<std::pair<int, double>, kHeldLook> older{};
    std::size_t older_count = 0;
    std::size_t away = 0;
    for (std::size_t k = 2; k < onsets.size(); ++k) {
        if (older_count == kHeldLook) {
            std::copy(older.begin() + 1, older.end(), older.begin());
            --older_count;
        }
        older[older_count++] = {onsets[k - 2].first_pitch, onsets[k - 2].first_end};
        const double now = onsets[k].start + kChordWindowSeconds;
        std::size_t kept = 0;
        bool apart = false;
        for (std::size_t h = 0; h < older_count; ++h) {
            if (older[h].second > now) {
                older[kept++] = older[h];
                apart = apart || std::abs(older[h].first - middles[k]) > 12;
            }
        }
        older_count = kept;
        away += apart ? 1 : 0;
    }
    return static_cast<double>(away) <= kHeldAwayFraction * count + kEpsilon;
}

// The staves of the song's parts (staff_plan.hpp, read for the hands) over the notes `playable` (those not in an
// accompaniment part). `part_of` gets the part of each of them; a note with no part makes the plan
// decide nothing.
StaffPlan plan_of_parts(const Song& song, const std::vector<std::size_t>& playable,
                        std::vector<std::size_t>& part_of) {
    const PartIndex part_at(song.parts);
    std::vector<StaffTrack> tracks(song.parts.size());
    for (std::size_t i = 0; i < song.parts.size(); ++i) {
        tracks[i].hand = song.parts[i].hand;
    }
    part_of.clear();
    part_of.reserve(playable.size());
    for (const std::size_t i : playable) {
        const Note& n = song.notes[i];
        const std::size_t at = part_at.find(n.track, n.channel);
        if (at == PartIndex::kNone) {
            StaffPlan none;
            return none;
        }
        part_of.push_back(at);
        tracks[at].pitch_sum += n.pitch;
        ++tracks[at].notes;
    }
    StaffOptions options;
    options.for_hands = true;
    return plan_staves(tracks, options);
}

// Gives the notes of an unnamed track the hand of its staff when the tracks alone make two staves
// (`StaffPlan::decides`): the lower tracks are the left hand and the others the right. Returns
// false, touching nothing, when they do not (one track, tracks all on one side, a stray track).
// The notes of `candidates` that got a hand are dropped from it.
bool assign_by_staves(const Song& song, const std::vector<std::size_t>& playable,
                      std::vector<std::size_t>& candidates, std::vector<Hand>& hands) {
    std::vector<std::size_t> part_of; // along `playable`
    const StaffPlan plan = plan_of_parts(song, playable, part_of);
    if (!plan.decides) {
        return false;
    }
    // `candidates` is a sublist of `playable`, both in note order.
    std::size_t at = 0;
    for (const std::size_t i : candidates) {
        while (playable[at] != i) {
            ++at;
        }
        hands[i] = hand_of_staff(plan.staff[part_of[at]]);
    }
    std::erase_if(candidates, [&](std::size_t i) { return hands[i] != Hand::Unknown; });
    return true;
}

// Gives every single-line part among the notes `candidates` its one hand, and drops those notes
// from `candidates`.
void assign_single_lines(const Song& song, std::vector<std::size_t>& candidates,
                         std::vector<Hand>& hands) {
    std::map<std::pair<std::uint16_t, std::uint8_t>, std::vector<std::size_t>> parts;
    for (std::size_t i : candidates) {
        parts[{song.notes[i].track, song.notes[i].channel}].push_back(i);
    }
    bool changed = false;
    for (const auto& [key, idx] : parts) {
        if (!is_single_line(song, idx)) {
            continue;
        }
        std::vector<int> pitches;
        pitches.reserve(idx.size());
        for (std::size_t i : idx) {
            pitches.push_back(song.notes[i].pitch);
        }
        std::sort(pitches.begin(), pitches.end());
        const std::size_t m = pitches.size();
        const double median =
            m % 2 == 1 ? pitches[m / 2] : (pitches[m / 2 - 1] + pitches[m / 2]) / 2.0;
        const Hand hand = median >= kSingleLineRightMedian ? Hand::Right : Hand::Left;
        for (std::size_t i : idx) {
            hands[i] = hand;
        }
        changed = true;
    }
    if (changed) {
        std::erase_if(candidates, [&](std::size_t i) { return hands[i] != Hand::Unknown; });
    }
}

// The two registers around a chord, when the notes near it look like two hands.
struct ChordRegisters {
    bool two = false;
    Registers r;
};

// The registers around each chord: a window over the notes of `stream` from kSplitWindowBeatsBefore
// beats before the chord to kSplitWindowBeatsAfter after it, advanced chord by chord.
std::vector<ChordRegisters> registers_around_chords(const Song& song,
                                                    const std::vector<std::size_t>& stream,
                                                    const std::vector<SimultaneousNotes>& chords,
                                                    int split) {
    const double beat = static_cast<double>(std::max(1, song.tempo.ticks_per_quarter));
    std::vector<ChordRegisters> out(chords.size());
    Histogram window;
    std::size_t win_lo = 0; // first note in the window
    std::size_t win_hi = 0; // one past the last note in the window
    const auto before_ticks = static_cast<std::int64_t>(kSplitWindowBeatsBefore * beat);
    const auto after_ticks = static_cast<std::int64_t>(kSplitWindowBeatsAfter * beat);
    Registers registers{split - kInitialSpread, split + kInitialSpread};
    for (std::size_t c = 0; c < chords.size(); ++c) {
        const SimultaneousNotes& chord = chords[c];
        while (win_hi < stream.size() &&
               song.notes[stream[win_hi]].start_tick <= chord.start_tick + after_ticks) {
            window.add(song.notes[stream[win_hi]].pitch);
            ++win_hi;
        }
        while (win_lo < win_hi &&
               song.notes[stream[win_lo]].start_tick < chord.start_tick - before_ticks) {
            window.remove(song.notes[stream[win_lo]].pitch);
            ++win_lo;
        }
        Registers fit;
        if (fit_registers(window, registers, split, fit)) {
            registers = fit;
            out[c] = {true, registers};
        }
    }
    return out;
}

// The smart split: gives a hand to every note of `chords` that has none (hands[i] is Unknown).
// `stream` holds all the playable notes the chords were made from, sorted by start; the ones with a
// hand stay as they are and count as the context of the others.
void split_by_cost(const Song& song, const std::vector<std::size_t>& stream,
                   const std::vector<SimultaneousNotes>& chords, int split,
                   std::vector<Hand>& hands) {
    const double beat = static_cast<double>(std::max(1, song.tempo.ticks_per_quarter));
    const std::vector<ChordRegisters> chord_registers =
        registers_around_chords(song, stream, chords, split);

    // Each chord is cut once, or not at all: the notes below the cut are the left hand, the ones
    // from it up the right. The cheapest way through all the chords is found by dynamic
    // programming over the cuts (see the cost terms in hand_split.hpp).
    struct Cand {
        std::size_t cut = 0;
        double cost = 0.0;
        std::int32_t back = -1; // the candidate of the chord before, on the cheapest way here
        std::int32_t slot = -1; // ... and its place among the ones that still count
    };
    std::vector<Cand> table;
    table.reserve(chords.size() * 2 + 1);
    table.emplace_back();               // before the first chord: nothing played yet
    std::vector<std::int32_t> alive{0}; // the candidates of the chord before that still count
    std::vector<std::int32_t> next_alive;
    std::vector<Recent> alive_left(1); // what each hand last played, on the way to each of them
    std::vector<Recent> alive_right(1);
    std::vector<Recent> next_left;
    std::vector<Recent> next_right;
    std::vector<int> pitch;
    std::vector<double> wrong_l; // wrong_l[k]: cost of the notes below k being left hand
    std::vector<double> wrong_r; // wrong_r[k]: cost of the notes below k being right hand
    std::vector<double> near_l;  // per alive candidate: the same for how far the notes below
    std::vector<double> near_r;  // k are from what that hand just played
    std::vector<std::size_t> cuts;
    std::vector<std::pair<int, std::size_t>> gaps;
    for (std::size_t c = 0; c < chords.size(); ++c) {
        const SimultaneousNotes& chord = chords[c];
        const std::vector<std::size_t>& notes = chord.notes;
        const std::size_t n = notes.size();
        pitch.resize(n);
        wrong_l.assign(n + 1, 0.0);
        wrong_r.assign(n + 1, 0.0);
        const ChordRegisters& cr = chord_registers[c];
        for (std::size_t i = 0; i < n; ++i) {
            pitch[i] = song.notes[notes[i]].pitch;
            const double p = pitch[i];
            // d > 0: the note is nearer the right hand's register than the left's.
            double d;
            const Hand fixed = hands[notes[i]];
            if (fixed != Hand::Unknown) {
                d = fixed == Hand::Right ? kFixedHandCost : -kFixedHandCost;
            } else if (cr.two) {
                d = (std::abs(p - cr.r.low) + (pitch[i] >= split ? kSideBias : 0.0)) -
                    (std::abs(p - cr.r.high) + (pitch[i] < split ? kSideBias : 0.0));
            } else {
                const double off = p - split + 0.5;
                const double far = std::max(0.0, std::abs(off) - kSingleRegisterNear);
                d = std::copysign(std::min(std::abs(off), kSingleRegisterNear) *
                                          kSingleRegisterNearSlope +
                                      std::min(far, kSingleRegisterRange) * kSingleRegisterSlope,
                                  off);
            }
            wrong_l[i + 1] = wrong_l[i] + std::max(0.0, d);
            wrong_r[i + 1] = wrong_r[i] + std::max(0.0, -d);
        }
        // The cuts worth trying: all of them, or for a very big chord the widest gaps.
        cuts.clear();
        cuts.push_back(0);
        if (n <= 12) {
            for (std::size_t k = 1; k <= n; ++k) {
                cuts.push_back(k);
            }
        } else {
            gaps.clear();
            for (std::size_t k = 1; k < n; ++k) {
                gaps.emplace_back(-(pitch[k] - pitch[k - 1]), k);
            }
            std::partial_sort(gaps.begin(), gaps.begin() + 8, gaps.end());
            for (std::size_t g = 0; g < 8; ++g) {
                cuts.push_back(gaps[g].second);
            }
            cuts.push_back(n);
            std::sort(cuts.begin(), cuts.end());
        }
        // How well each note continues what each hand played just before, on each way here.
        const std::size_t stride = n + 1;
        near_l.assign(alive.size() * stride, 0.0);
        near_r.assign(alive.size() * stride, 0.0);
        for (std::size_t a = 0; a < alive.size(); ++a) {
            for (int side = 0; side < 2; ++side) {
                const Recent& recent = side == 0 ? alive_left[a] : alive_right[a];
                std::array<double, kRecentNotes> fresh{};
                for (std::size_t r = 0; r < recent.count; ++r) {
                    const double beats =
                        static_cast<double>(chord.start_tick - recent.tick[r]) / beat;
                    fresh[r] = std::clamp((beats - 1.0) / (kFollowBeats - 1.0), 0.0, 1.0);
                }
                double* out = (side == 0 ? near_l : near_r).data() + a * stride;
                for (std::size_t i = 0; i < n; ++i) {
                    double best = kFreshStartCost;
                    for (std::size_t r = 0; r < recent.count; ++r) {
                        const int gap = std::abs(pitch[i] - recent.pitch[r]);
                        const double d =
                            gap == 0 ? -kRepeatBonus : std::min<double>(gap, kFreshStartCost);
                        best = std::min(best, (1.0 - fresh[r]) * d + fresh[r] * kFreshStartCost);
                    }
                    out[i + 1] = out[i] + kContinueCost * best;
                }
            }
        }
        auto penalty = [&](std::size_t a, std::size_t b) {
            if (b <= a) {
                return 0.0;
            }
            const int span = pitch[b - 1] - pitch[a];
            double extra = 0.0;
            if (span > kComfortableSpan) {
                extra += kSpanCost * (span - kComfortableSpan);
            }
            if (span > kHandReachSemitones) {
                extra += kBeyondReachCost;
            }
            if (b - a > static_cast<std::size_t>(kHandNotes)) {
                extra += kExtraNoteCost *
                         static_cast<double>((b - a) - static_cast<std::size_t>(kHandNotes));
            }
            return extra;
        };
        const std::size_t first = table.size();
        double cheapest = std::numeric_limits<double>::infinity();
        for (std::size_t cut : cuts) {
            double local = wrong_l[cut] + (wrong_r[n] - wrong_r[cut]);
            local += penalty(0, cut) + penalty(cut, n);
            if (cut > 0 && cut < n) {
                local += kCutCost * std::max(0, kNaturalGap - (pitch[cut] - pitch[cut - 1]));
            }
            Cand best;
            best.cut = cut;
            best.cost = std::numeric_limits<double>::infinity();
            for (std::size_t a = 0; a < alive.size(); ++a) {
                const double total = table[static_cast<std::size_t>(alive[a])].cost + local +
                                     near_l[a * stride + cut] +
                                     (near_r[a * stride + n] - near_r[a * stride + cut]);
                if (total < best.cost) {
                    best.cost = total;
                    best.back = alive[a];
                    best.slot = static_cast<std::int32_t>(a);
                }
            }
            cheapest = std::min(cheapest, best.cost);
            table.push_back(best);
        }
        // Keep the cheap ways, and give each the notes the hands just played.
        next_alive.clear();
        next_left.clear();
        next_right.clear();
        for (std::size_t q = first; q < table.size(); ++q) {
            const Cand& cand = table[q];
            if (cand.cost > cheapest + kBeamCost) {
                continue;
            }
            next_left.push_back(alive_left[static_cast<std::size_t>(cand.slot)]);
            next_right.push_back(alive_right[static_cast<std::size_t>(cand.slot)]);
            if (cand.cut > 0) {
                next_left.back().push(pitch, 0, cand.cut, chord.start_tick);
            }
            if (cand.cut < n) {
                next_right.back().push(pitch, cand.cut, n, chord.start_tick);
            }
            next_alive.push_back(static_cast<std::int32_t>(q));
        }
        alive.swap(next_alive);
        alive_left.swap(next_left);
        alive_right.swap(next_right);
    }
    // The cheapest end.
    std::int32_t at = alive.front();
    for (std::int32_t q : alive) {
        if (table[static_cast<std::size_t>(q)].cost < table[static_cast<std::size_t>(at)].cost) {
            at = q;
        }
    }
    for (std::size_t c = chords.size(); c-- > 0;) {
        const Cand& cand = table[static_cast<std::size_t>(at)];
        const std::vector<std::size_t>& notes = chords[c].notes;
        for (std::size_t k = 0; k < notes.size(); ++k) {
            if (hands[notes[k]] == Hand::Unknown) {
                hands[notes[k]] = k < cand.cut ? Hand::Left : Hand::Right;
            }
        }
        at = cand.back;
    }
}

} // namespace

bool needs_hand_split(const Song& song) {
    const std::vector<bool> accompaniment = accompaniment_mask(song);
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        const Note& n = song.notes[i];
        if (n.drum) {
            continue;
        }
        if (accompaniment[i] ? (!n.accompaniment || n.hand != Hand::Unknown)
                             : n.hand == Hand::Unknown) {
            return true;
        }
    }
    return false;
}

bool hand_split_decides(const Song& song) {
    const std::vector<bool> accompaniment = accompaniment_mask(song);
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        const Note& n = song.notes[i];
        if (!n.drum && !accompaniment[i] && n.hand == Hand::Unknown) {
            return true;
        }
    }
    return false;
}

int sanitized_split_point(int split_point) {
    return split_point < 1 ? kAutoSplit : std::min(split_point, kMaxSplitPoint);
}

HandSplitOptions options_for_split_point(int split_point) {
    HandSplitOptions options;
    const int point = sanitized_split_point(split_point);
    if (point != kAutoSplit) {
        options.method = SplitMethod::Fixed;
        options.split_point = point;
        options.single_lines_whole = false;
        options.by_pitch = true;
    }
    return options;
}

bool hands_from_staves(const Song& song) {
    const std::vector<bool> accompaniment = accompaniment_mask(song);
    std::vector<std::size_t> playable;
    bool unnamed = false;
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        const Note& n = song.notes[i];
        if (!n.drum && !accompaniment[i]) {
            playable.push_back(i);
            unnamed = unnamed || n.hand == Hand::Unknown;
        }
    }
    std::vector<std::size_t> part_of;
    return unnamed && plan_of_parts(song, playable, part_of).decides;
}

int suggested_split_point(const Song& song) {
    std::array<int, 128> left{};
    std::array<int, 128> right{};
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment || n.pitch > 127) {
            continue;
        }
        if (n.hand == Hand::Left) {
            ++left[static_cast<std::size_t>(n.pitch)];
        } else if (n.hand == Hand::Right) {
            ++right[static_cast<std::size_t>(n.pitch)];
        }
    }
    // cost(p): right-hand notes below p plus left-hand notes from p up.
    int left_from = 0;
    for (const int c : left) {
        left_from += c;
    }
    int right_below = 0;
    std::array<int, 128> cost{}; // index p = 1..127
    for (int p = 1; p <= kMaxSplitPoint; ++p) {
        const auto below = static_cast<std::size_t>(p - 1);
        right_below += right[below];
        left_from -= left[below];
        cost[static_cast<std::size_t>(p)] = right_below + left_from;
    }
    const int best = *std::min_element(cost.begin() + 1, cost.end());
    int chosen = kMiddleC;
    int chosen_distance = std::numeric_limits<int>::max();
    for (int p = 1; p <= kMaxSplitPoint;) {
        if (cost[static_cast<std::size_t>(p)] != best) {
            ++p;
            continue;
        }
        int end = p;
        while (end + 1 <= kMaxSplitPoint && cost[static_cast<std::size_t>(end + 1)] == best) {
            ++end;
        }
        // A run between the two hands: its middle. A run open at an end of the keyboard (a song
        // with one hand's notes only): the point of it nearest middle C.
        const int mid =
            p == 1 || end == kMaxSplitPoint ? std::clamp(kMiddleC, p, end) : (p + end) / 2;
        // The run nearest middle C; a tie goes to the lower run, which is the left hand's side.
        if (std::abs(mid - kMiddleC) < chosen_distance) {
            chosen = mid;
            chosen_distance = std::abs(mid - kMiddleC);
        }
        p = end + 1;
    }
    return chosen;
}

std::vector<Hand> split_hands(const Song& song, const HandSplitOptions& options) {
    std::vector<Hand> hands(song.notes.size(), Hand::Unknown);
    std::vector<std::size_t> candidates; // the notes without a hand
    std::vector<std::size_t> playable;   // those and the ones that have one
    const std::vector<bool> accompaniment = accompaniment_mask(song);
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        const Note& n = song.notes[i];
        if (n.drum || accompaniment[i]) {
            continue;
        }
        hands[i] = n.hand;
        playable.push_back(i);
        if (n.hand == Hand::Unknown) {
            candidates.push_back(i);
        }
    }
    if (options.by_pitch) {
        const int point = std::clamp(options.split_point, 1, 127);
        for (const std::size_t i : candidates) {
            hands[i] = song.notes[i].pitch >= point ? Hand::Right : Hand::Left;
        }
        return hands;
    }
    if (options.staves_decide && assign_by_staves(song, playable, candidates, hands)) {
        return hands;
    }
    if (options.single_lines_whole) {
        assign_single_lines(song, candidates, hands);
    }
    if (candidates.empty()) {
        return hands;
    }
    const bool smart = options.method == SplitMethod::Smart;
    const int split = std::clamp(options.split_point, 1, 127);

    // Chords: notes starting within the chord window of the chord's first note. The smart split
    // takes the notes that already have a hand along, as the context of the others (a melody that
    // the single-line rule gave to the right hand tells where the chords under it belong); the
    // fixed split looks at the others only.
    const std::vector<std::size_t>& stream = smart ? playable : candidates;
    std::vector<SimultaneousNotes> chords;
    for (std::size_t i : stream) {
        const Note& n = song.notes[i];
        if (chords.empty() ||
            n.start_s - song.notes[chords.back().notes.front()].start_s > kChordWindowSeconds) {
            chords.push_back(SimultaneousNotes{{}, n.start_tick});
        }
        chords.back().notes.push_back(i);
    }
    for (SimultaneousNotes& c : chords) {
        std::stable_sort(c.notes.begin(), c.notes.end(), [&](std::size_t a, std::size_t b) {
            return song.notes[a].pitch < song.notes[b].pitch;
        });
    }

    if (!smart) {
        // The split point decides, chord by chord.
        for (const SimultaneousNotes& chord : chords) {
            const std::vector<std::size_t>& notes = chord.notes;
            const int lowest = song.notes[notes.front()].pitch;
            const int highest = song.notes[notes.back()].pitch;
            std::size_t cut = 0; // notes before `cut` are the left hand
            if (highest - lowest > kHandReachSemitones) {
                cut = cut_chord(song, notes, split);
            } else if (median_pitch(song, notes) < split) {
                cut = notes.size();
            }
            for (std::size_t k = 0; k < notes.size(); ++k) {
                hands[notes[k]] = k < cut ? Hand::Left : Hand::Right;
            }
        }
        return hands;
    }

    split_by_cost(song, stream, chords, split, hands);
    return hands;
}

namespace {
std::atomic<std::uint64_t> g_split_runs{0};
} // namespace

HandSplit compute_hand_split(const Song& song, const HandSplitOptions& options) {
    g_split_runs.fetch_add(1, std::memory_order_relaxed);
    HandSplit out;
    out.fingerprint = song_fingerprint(song);
    out.note_hands = split_hands(song, options);
    const std::vector<bool> accompaniment = accompaniment_mask(song);
    out.note_accompaniment = accompaniment;
    // One pass over the notes, not one per part: a song can have thousands of parts.
    struct Seen {
        Hand common = Hand::Unknown;
        bool mixed = false;
    };
    const PartIndex part_at(song.parts);
    std::vector<Seen> seen(song.parts.size());
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        const Note& n = song.notes[i];
        const std::size_t at = n.drum ? PartIndex::kNone : part_at.find(n.track, n.channel);
        if (at == PartIndex::kNone) {
            continue;
        }
        Seen& s = seen[at];
        const Hand hand = out.note_hands[i];
        if (s.common == Hand::Unknown) {
            s.common = hand;
        } else if (hand != s.common) {
            s.mixed = true;
        }
    }
    const bool keyboard = has_keyboard_part(song);
    out.part_hands.assign(song.parts.size(), Hand::Unknown);
    out.part_accompaniment.assign(song.parts.size(), false);
    for (std::size_t i = 0; i < song.parts.size(); ++i) {
        const Part& part = song.parts[i];
        out.part_accompaniment[i] =
            !part.drum && (part.accompaniment || is_accompaniment_part(part, keyboard));
        out.part_hands[i] = part.drum ? part.hand : seen[i].mixed ? Hand::Unknown : seen[i].common;
    }
    return out;
}

std::uint64_t hand_split_runs() { return g_split_runs.load(std::memory_order_relaxed); }

} // namespace etude
