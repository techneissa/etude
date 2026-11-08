// SPDX-License-Identifier: GPL-3.0-or-later
#include "harmony_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace etude {

namespace {

// ---- The shapes the analysis fits: the detection's, and the bare power chord.

const std::vector<ChordShape>& fit_shapes() {
    static const std::vector<ChordShape> shapes = [] {
        std::vector<ChordShape> all;
        for (const ChordShape& s : chord_shapes()) {
            all.push_back(s);
        }
        all.push_back(ChordShape{(1u << 0) | (1u << 7), ChordQuality::Power, 0});
        return all;
    }();
    return shapes;
}

// What claiming the quality costs: nothing for a triad, a little more for each step away from the
// plain chords a piano piece is made of. An extension must carry enough weight to pay for it.
double quality_cost(ChordQuality quality) {
    switch (quality) {
    case ChordQuality::Major:
    case ChordQuality::Minor:
        return 0.0;
    case ChordQuality::Dominant7:
    case ChordQuality::Minor7:
        return 0.04;
    case ChordQuality::Major7:
        return 0.05;
    case ChordQuality::Diminished:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Diminished7:
        return 0.06;
    case ChordQuality::Sus2:
    case ChordQuality::Sus4:
        return 0.08;
    case ChordQuality::Augmented:
    case ChordQuality::Dominant7Sus4:
    case ChordQuality::Major6:
    case ChordQuality::Minor6:
        return 0.10;
    case ChordQuality::MinorMajor7:
        return 0.12;
    case ChordQuality::Power:
        return 0.15;
    case ChordQuality::Dominant9:
    case ChordQuality::Major9:
    case ChordQuality::Minor9:
    case ChordQuality::Dominant7Flat9:
        return 0.30;
    case ChordQuality::Add9:
    case ChordQuality::Major69:
    case ChordQuality::Dominant13:
        return 0.45;
    case ChordQuality::None:
        break;
    }
    return 1.0;
}

// What leaving out a tone the shape needs costs, by its distance in semitones from the root.
double missing_cost(int semitones) {
    static constexpr std::array<double, 12> kCost{1.0,  0.25, 0.40, 0.45, 0.45, 0.60,
                                                  0.20, 0.10, 0.20, 0.25, 0.30, 0.30};
    return kCost[static_cast<std::size_t>(semitones)];
}

} // namespace

unsigned rotate_mask(unsigned shape, int root) {
    unsigned out = 0;
    for (int st = 0; st < 12; ++st) {
        if (shape & (1u << st)) {
            out |= 1u << ((st + root) % 12);
        }
    }
    return out;
}

namespace {

// Whether `semitones` above the root is a tone that makes the chord more than its triad (a
// seventh, a sixth, a ninth): the tones a passing note is most likely to be mistaken for.
bool is_extension(ChordQuality quality, int semitones) {
    switch (quality) {
    case ChordQuality::Dominant7:
    case ChordQuality::Major7:
    case ChordQuality::Minor7:
    case ChordQuality::MinorMajor7:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::Dominant7Sus4:
        return semitones == 10 || semitones == 11;
    case ChordQuality::Diminished7:
        return semitones == 9;
    case ChordQuality::Major6:
    case ChordQuality::Minor6:
        return semitones == 9;
    case ChordQuality::Add9:
        return semitones == 2;
    case ChordQuality::Dominant9:
    case ChordQuality::Major9:
    case ChordQuality::Minor9:
    case ChordQuality::Dominant7Flat9:
    case ChordQuality::Major69:
    case ChordQuality::Dominant13:
        return semitones == 1 || semitones == 2 || semitones == 9 || semitones == 10 ||
               semitones == 11;
    default:
        return false;
    }
}

// The score of `shape` on `root` over the stretch; `coverage` is the share it covers. An extension
// tone counts only when the stretch backs it: it starts again, or carries a good share of the
// weight; one short note is a passing note.
double score_shape(const Stats& st, int root, const ChordShape& shape, double& coverage) {
    double covered = 0.0;
    double missing = 0.0;
    for (int semitones = 0; semitones < 12; ++semitones) {
        if (!(shape.mask & (1u << semitones))) {
            continue;
        }
        const PcStat& p = st.pc[static_cast<std::size_t>((root + semitones) % 12)];
        const bool backed = !is_extension(shape.quality, semitones) ||
                            p.onsets >= kEstablishedOnsets || p.longest >= kHarmonyBackedBeats ||
                            (st.total > 0.0 && p.weight >= kHarmonyExtensionShare * st.total);
        if (p.established && backed) {
            covered += p.weight;
        } else if (!(shape.optional & (1u << semitones))) {
            missing += missing_cost(semitones);
        }
    }
    coverage = st.total > 0.0 ? covered / st.total : 0.0;
    double score = (1.0 + kHarmonyOutsideWeight) * coverage - kHarmonyOutsideWeight -
                   quality_cost(shape.quality) - missing;
    if (st.bass_pc >= 0) {
        const int from_root = ((st.bass_pc - root) % 12 + 12) % 12;
        if (from_root == 0) {
            score += kHarmonyBassRootBonus;
        } else if (!(shape.mask & (1u << from_root))) {
            score -= kHarmonyBassOutsidePenalty;
        }
    }
    return score;
}

// Whether the stretch sounds the tone that tells the chord's kind, even briefly: the third (minor
// or major), or for a suspended chord the second or fourth. A root and a fifth alone make a power
// chord, nothing else.
bool has_defining_tone(const Stats& st, int root, const ChordShape& shape) {
    unsigned wanted = shape.mask & ((1u << 3) | (1u << 4));
    if (wanted == 0) {
        wanted = shape.mask & ((1u << 2) | (1u << 5));
    }
    if (wanted == 0) {
        return true; // a power chord
    }
    for (int semitones = 0; semitones < 12; ++semitones) {
        if ((wanted & (1u << semitones)) &&
            st.pc[static_cast<std::size_t>((root + semitones) % 12)].weight > 0.0) {
            return true;
        }
    }
    return false;
}

// The tones a chord cannot lose and still be that chord, as semitones above its root: the root,
// the third (a suspended chord's second or fourth), the fifth, and the seventh of a seventh chord.
// A bare fifth has no third and is no chord a hand can be named for.
std::vector<int> required_tones(ChordQuality quality) {
    const unsigned mask = chord_quality_mask(quality);
    const auto first_of = [&](std::initializer_list<int> options) {
        for (const int semitones : options) {
            if ((mask & (1u << semitones)) != 0) {
                return semitones;
            }
        }
        return -1;
    };
    std::vector<int> need{0};
    need.push_back(first_of({3, 4, 2, 5}));
    need.push_back(first_of({7, 6, 8}));
    switch (quality) {
    case ChordQuality::Dominant7:
    case ChordQuality::Major7:
    case ChordQuality::Minor7:
    case ChordQuality::HalfDiminished7:
    case ChordQuality::MinorMajor7:
    case ChordQuality::Dominant9:
    case ChordQuality::Major9:
    case ChordQuality::Minor9:
    case ChordQuality::Dominant7Sus4:
    case ChordQuality::Dominant7Flat9:
    case ChordQuality::Dominant13:
        need.push_back(first_of({10, 11}));
        break;
    case ChordQuality::Diminished7:
        need.push_back(9);
        break;
    default:
        break;
    }
    return need;
}

// Whether the stretch's notes hold every tone the chord cannot lose, each of them established (it
// starts again or is held, so a passing note is not one): the rule a hand's chord must meet.

} // namespace

bool plays_full_chord(const Stats& st, int root, ChordQuality quality) {
    if (quality == ChordQuality::Power || quality == ChordQuality::None) {
        return false;
    }
    for (const int semitones : required_tones(quality)) {
        if (semitones < 0 ||
            !st.pc[static_cast<std::size_t>((root + semitones) % 12)].established) {
            return false;
        }
    }
    return true;
}

namespace {

// A sixth chord with its sixth in the bass (D6 over B) is the seventh chord rooted on that bass
// (Bm7) with its seventh left out: it is read as a triad on the bass unless the seventh sounds.
bool sixth_in_bass(const Stats& st, int root, const ChordShape& shape) {
    const bool sixth = shape.quality == ChordQuality::Major6 ||
                       shape.quality == ChordQuality::Minor6 ||
                       shape.quality == ChordQuality::Major69;
    return sixth && st.bass_pc >= 0 && st.bass_pc == (root + 9) % 12;
}

} // namespace

bool same_chord(const Fit& a, const Fit& b) {
    return a.quality == b.quality && a.tones == b.tones &&
           (a.root == b.root || a.quality == ChordQuality::Diminished7 ||
            a.quality == ChordQuality::Augmented);
}

namespace {

// Whether the stretch holds enough to name any chord: three pitch classes (two for a bare fifth,
// when they sound together), two of them sounding together or starting again, so that a bare tune
// names nothing.
bool holds_a_chord(const Stats& st) {
    if (st.established < kMinEstablished) {
        int together = 0;
        for (const PcStat& p : st.pc) {
            together += p.established && p.together ? 1 : 0;
        }
        if (st.established != 2 || together != 2) {
            return false;
        }
    }
    return st.stable >= kMinStable && st.total > 0.0;
}

} // namespace

Fit fit_chord(const Stats& st) {
    if (!holds_a_chord(st)) {
        return {};
    }
    Fit best;
    for (int root = 0; root < 12; ++root) {
        const PcStat& r = st.pc[static_cast<std::size_t>(root)];
        // A root is a pitch class the stretch backs: it starts again, is held, carries a share, or
        // is the bass.
        if (!r.established ||
            !(r.onsets >= kEstablishedOnsets || r.longest >= kHarmonyBackedBeats ||
              r.weight >= kHarmonyRootShare * st.total || root == st.bass_pc)) {
            continue;
        }
        for (const ChordShape& shape : fit_shapes()) {
            if (shape.quality == ChordQuality::Power && st.bass_pc != root) {
                continue; // a power chord has its root in the bass
            }
            if (!has_defining_tone(st, root, shape) || sixth_in_bass(st, root, shape)) {
                continue;
            }
            Fit f;
            f.score = score_shape(st, root, shape, f.coverage);
            f.root = root;
            f.quality = shape.quality;
            f.tones = rotate_mask(shape.mask, root);
            // A tie goes to the reading rooted on the bass, then to the earlier shape.
            if (f.score > best.score + 1e-9 || (std::abs(f.score - best.score) <= 1e-9 &&
                                                root == st.bass_pc && best.root != st.bass_pc)) {
                best = f;
                best.found = true;
            }
        }
    }
    if (!best.found) {
        return best;
    }
    // The lead over the best chord that is not this one.
    double runner_up = -1e9;
    for (int root = 0; root < 12; ++root) {
        if (!st.pc[static_cast<std::size_t>(root)].established) {
            continue;
        }
        for (const ChordShape& shape : fit_shapes()) {
            if ((shape.quality == ChordQuality::Power && st.bass_pc != root) ||
                !has_defining_tone(st, root, shape) || sixth_in_bass(st, root, shape)) {
                continue;
            }
            Fit f;
            f.quality = shape.quality;
            f.root = root;
            f.tones = rotate_mask(shape.mask, root);
            if (!same_chord(best, f)) {
                runner_up = std::max(runner_up, score_shape(st, root, shape, f.coverage));
            }
        }
    }
    // The chord must be built of tones the stretch really has.
    int present = 0;
    for (int pc = 0; pc < 12; ++pc) {
        present +=
            (best.tones & (1u << pc)) && st.pc[static_cast<std::size_t>(pc)].established ? 1 : 0;
    }
    const int needed = best.quality == ChordQuality::Power ? 2 : 3;
    if (best.coverage < kHarmonyMinCoverage || present < needed) {
        return {};
    }
    best.margin = std::min(best.score - runner_up, 1.0);
    best.bass_pc = st.bass_pc >= 0 && (best.tones & (1u << st.bass_pc)) ? st.bass_pc : best.root;
    return best;
}

namespace {

// How the chord `held` fits a stretch, for the hysteresis: its best shape of that quality.

} // namespace

Fit fit_held(const Stats& st, int root, ChordQuality quality) {
    Fit best;
    for (const ChordShape& shape : fit_shapes()) {
        if (shape.quality != quality || !has_defining_tone(st, root, shape)) {
            continue;
        }
        Fit f;
        f.score = score_shape(st, root, shape, f.coverage);
        if (f.score > best.score) {
            best = f;
            best.root = root;
            best.quality = quality;
            best.tones = rotate_mask(shape.mask, root);
        }
    }
    best.found = best.quality != ChordQuality::None;
    best.bass_pc = st.bass_pc >= 0 && (best.tones & (1u << st.bass_pc)) ? st.bass_pc : root;
    return best;
}

namespace {

} // namespace

float confidence_of(const Fit& f) {
    const double clear = std::clamp(f.margin / 0.25, 0.0, 1.0);
    return static_cast<float>(std::clamp(f.coverage * (0.7 + 0.3 * clear), 0.0, 1.0));
}

namespace {

} // namespace

} // namespace etude
