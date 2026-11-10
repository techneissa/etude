// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/harmony.hpp"

#include "harmony_internal.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>

namespace etude {
namespace {

// ---- Cutting the song into bars

// A beat is a quarter note, or a dotted quarter in 6/8, 9/8, 12/8 and 3/8, whatever the written
// denominator, so that "three quarters of a beat" is the same length of music in every meter. 2/2
// and slower count their halves and wholes in quarters too.
std::vector<Frame> bars_of(const Song& song) {
    std::vector<Frame> frames;
    const std::int64_t tpq = std::max(song.tempo.ticks_per_quarter, 1);
    for (std::size_t i = 0; i < song.measures.size(); ++i) {
        const Measure& m = song.measures[i];
        const std::int64_t end =
            i + 1 < song.measures.size() ? song.measures[i + 1].start_tick : song.end_tick;
        if (end <= m.start_tick) {
            continue;
        }
        const bool compound = m.denominator == 8 && m.numerator % 3 == 0;
        const std::int64_t group = std::max<std::int64_t>(compound ? tpq * 3 / 2 : tpq, 1);
        const auto groups = static_cast<int>(std::max<std::int64_t>(
            std::llround(static_cast<double>(end - m.start_tick) / static_cast<double>(group)), 1));
        frames.push_back(Frame{m.start_tick, end, m.start_tick, group, groups});
    }
    return frames;
}

// The notes of the scope, by index into the song, sorted by start (the song's order).
std::vector<std::uint32_t> notes_of(const Song& song, HarmonyScope scope) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < song.notes.size(); ++i) {
        const Note& n = song.notes[i];
        if (n.drum) {
            continue;
        }
        const bool take = scope == HarmonyScope::All ||
                          (!n.accompaniment &&
                           n.hand == (scope == HarmonyScope::Left ? Hand::Left : Hand::Right));
        if (!take) {
            continue;
        }
        // A part that doubles another (the song's accompaniment copy of a hand) is one voice.
        if (scope == HarmonyScope::All && !out.empty()) {
            const Note& last = song.notes[out.back()];
            if (last.start_tick == n.start_tick && last.end_tick == n.end_tick &&
                last.pitch == n.pitch) {
                continue;
            }
        }
        out.push_back(static_cast<std::uint32_t>(i));
    }
    return out;
}

// For each frame, the notes that sound in it. Frames are sorted and do not overlap.
std::vector<std::vector<std::uint32_t>> bucket(const Song& song,
                                               const std::vector<std::uint32_t>& scoped,
                                               const std::vector<Frame>& frames) {
    std::vector<std::vector<std::uint32_t>> out(frames.size());
    if (frames.empty()) {
        return out;
    }
    for (const std::uint32_t index : scoped) {
        const Note& n = song.notes[index];
        // The first frame ending after the note starts.
        auto it = std::upper_bound(frames.begin(), frames.end(), n.start_tick,
                                   [](std::int64_t tick, const Frame& f) { return tick < f.t1; });
        for (; it != frames.end() && it->t0 < n.end_tick; ++it) {
            out[static_cast<std::size_t>(it - frames.begin())].push_back(index);
        }
    }
    return out;
}


// ---- Building stretches

HarmonyStretch make_stretch(const Song& song, std::int64_t t0, std::int64_t t1) {
    HarmonyStretch s;
    s.start_tick = t0;
    s.end_tick = t1;
    s.start_s = song.tempo.seconds_at(t0);
    s.end_s = song.tempo.seconds_at(t1);
    return s;
}

// The lowest and highest note of `notes` sounding in [t0, t1) whose pitch class is in `tones`,
// leaving out the tune; with `st`, only pitch classes it establishes.
void span_of(const Song& song, const std::vector<std::uint32_t>& notes, std::int64_t t0,
             std::int64_t t1, unsigned tones, const Stats* st, HarmonyStretch& out) {
    const std::vector<std::uint32_t> tune = tune_of(song, notes, t0, t1);
    for (const std::uint32_t index : notes) {
        const Note& n = song.notes[index];
        if (n.end_tick <= t0 || n.start_tick >= t1) {
            continue;
        }
        const int pc = pitch_class(n.pitch);
        if (!(tones & (1u << pc)) || std::binary_search(tune.begin(), tune.end(), index) ||
            (st != nullptr && !st->pc[static_cast<std::size_t>(pc)].established)) {
            continue;
        }
        out.low_pitch = out.low_pitch < 0 ? n.pitch : std::min<int>(out.low_pitch, n.pitch);
        out.high_pitch = std::max<int>(out.high_pitch, n.pitch);
    }
}

// Adjacent stretches with the same state and the same chord, spelled the same, become one.
std::vector<HarmonyStretch> merged(std::vector<HarmonyStretch> in) {
    std::vector<HarmonyStretch> out;
    for (HarmonyStretch& s : in) {
        if (!out.empty()) {
            HarmonyStretch& last = out.back();
            if (last.end_tick == s.start_tick && last.state == s.state && last.chord == s.chord &&
                (!s.chord.found || last.spelling == s.spelling) && last.from_file == s.from_file) {
                const double a = static_cast<double>(last.end_tick - last.start_tick);
                const double b = static_cast<double>(s.end_tick - s.start_tick);
                last.confidence =
                    static_cast<float>((last.confidence * a + s.confidence * b) / (a + b));
                last.end_tick = s.end_tick;
                last.end_s = s.end_s;
                last.hold_end_tick = std::max(last.hold_end_tick, s.hold_end_tick);
                last.hold_end_s = std::max(last.hold_end_s, s.hold_end_s);
                if (s.low_pitch >= 0) {
                    last.low_pitch =
                        last.low_pitch < 0 ? s.low_pitch : std::min(last.low_pitch, s.low_pitch);
                    last.high_pitch = std::max(last.high_pitch, s.high_pitch);
                }
                continue;
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

// An unclear stretch between two stretches of the same chord (a rest, a lone note) is made part of
// it, when it is no longer than `max_gap` ticks: the chord was held through it.
void fill_unclear_gaps(std::vector<HarmonyStretch>& stretches, std::int64_t max_gap) {
    for (std::size_t i = 1; i + 1 < stretches.size(); ++i) {
        HarmonyStretch& gap = stretches[i];
        const HarmonyStretch& before = stretches[i - 1];
        const HarmonyStretch& after = stretches[i + 1];
        if (gap.state != HarmonyState::Unclear || before.state != HarmonyState::Chord ||
            after.state != HarmonyState::Chord || gap.end_tick - gap.start_tick > max_gap ||
            before.chord.root_pc != after.chord.root_pc ||
            before.chord.quality != after.chord.quality) {
            continue;
        }
        gap.state = HarmonyState::Chord;
        gap.chord = before.chord;
        gap.spelling = before.spelling;
        gap.from_file = before.from_file && after.from_file;
        gap.confidence = std::min(before.confidence, after.confidence) * 0.8f;
        gap.low_pitch = before.low_pitch;
        gap.high_pitch = before.high_pitch;
        if (after.low_pitch >= 0) {
            gap.low_pitch =
                gap.low_pitch < 0 ? after.low_pitch : std::min(gap.low_pitch, after.low_pitch);
            gap.high_pitch = std::max(gap.high_pitch, after.high_pitch);
        }
    }
}

// A diminished seventh reads the same from each of its four tones. Where a chord follows, the root
// is the tone a semitone under that chord's root (C# dim7 into D minor); otherwise the one nearest
// the key was chosen already. A chord the file writes is left as written.
void root_diminished_on_resolution(const Song& song, std::vector<HarmonyStretch>& stretches) {
    for (std::size_t i = 0; i + 1 < stretches.size(); ++i) {
        HarmonyStretch& s = stretches[i];
        const HarmonyStretch& next = stretches[i + 1];
        if (s.from_file || !s.chord.found || s.chord.quality != ChordQuality::Diminished7 ||
            !next.chord.found || next.chord.quality == ChordQuality::Diminished7) {
            continue;
        }
        const int leading = ((next.chord.root_pc - 1) % 12 + 12) % 12;
        if (((leading - s.chord.root_pc) % 3 + 3) % 3 != 0) {
            continue; // not a tone of this chord: the tones are three semitones apart
        }
        s.chord.root_pc = leading;
        s.spelling = spell_chord(s.chord, key_at_tick(song.keys, s.start_tick));
    }
}

// `notes` without the sorted `drop`.
std::vector<std::uint32_t> without(const std::vector<std::uint32_t>& notes,
                                   const std::vector<std::uint32_t>& drop) {
    if (drop.empty()) {
        return notes;
    }
    std::vector<std::uint32_t> out;
    out.reserve(notes.size());
    for (const std::uint32_t index : notes) {
        if (!std::binary_search(drop.begin(), drop.end(), index)) {
            out.push_back(index);
        }
    }
    return out;
}

// A stretch's weights and its best chord. `notes` are the ones the weights come from: a hand's
// without its separate top voice.
struct Analysed {
    Stats stats;
    Fit fit;
    std::vector<std::uint32_t> notes;
};

Analysed analyse_unit(const Song& song, const std::vector<std::uint32_t>& notes, const Frame& bar,
                      std::int64_t t0, std::int64_t t1, bool hand) {
    Analysed a;
    // A name comes from the harmony, never from the tune: a hand that plays chords under a top
    // line of its own is named without that line, and a hand that plays only a line that moves by
    // step is named for nothing.
    a.notes = hand ? without(notes, top_voice_of(song, notes, t0, t1)) : notes;
    a.stats = stats_of(song, hand ? a.notes : notes, bar, t0, t1);
    a.fit = fit_chord(a.stats);
    if (hand && a.fit.found &&
        (!plays_full_chord(a.stats, a.fit.root, a.fit.quality) ||
         stepwise_line(song, a.notes, bar, t0, t1))) {
        a.fit = {};
    }
    return a;
}

std::vector<HarmonyStretch> detect(const Song& song, HarmonyScope scope) {
    // A hand is named only for a full chord of its own notes.
    const bool hand_scope = scope != HarmonyScope::All;
    const bool full_chords_only = hand_scope;
    const std::vector<std::uint32_t> scoped = notes_of(song, scope);
    const std::vector<Frame> bars = bars_of(song);
    const auto notes = bucket(song, scoped, bars);
    std::vector<HarmonyStretch> out;
    std::optional<Fit> held;

    for (std::size_t b = 0; b < bars.size(); ++b) {
        const Frame& bar = bars[b];
        const KeyChange* key = key_at_tick(song.keys, bar.t0);

        // The units of the bar: the whole bar, or its two halves when they clearly differ.
        struct Unit {
            std::int64_t t0;
            std::int64_t t1;
            bool half;
            Analysed analysed;
        };
        std::vector<Unit> units;
        Analysed whole = analyse_unit(song, notes[b], bar, bar.t0, bar.t1, full_chords_only);
        bool split = false;
        if (bar.groups >= 2 && bar.groups % 2 == 0) {
            const std::int64_t mid = bar.t0 + (bar.groups / 2) * bar.group_ticks;
            if (mid > bar.t0 && mid < bar.t1) {
                Analysed first = analyse_unit(song, notes[b], bar, bar.t0, mid, full_chords_only);
                Analysed second = analyse_unit(song, notes[b], bar, mid, bar.t1, full_chords_only);
                // The halves split when each is covered well by a chord of its own and the two
                // chords together fit better than one chord for the bar.
                const double whole_score = whole.fit.found ? whole.fit.score : -1.5;
                if (first.fit.found && second.fit.found && !same_chord(first.fit, second.fit) &&
                    std::min(first.fit.coverage, second.fit.coverage) >= kHarmonySplitCoverage &&
                    (first.fit.score + second.fit.score) / 2.0 - whole_score >= kHarmonySplitGain) {
                    units.push_back({bar.t0, mid, true, std::move(first)});
                    units.push_back({mid, bar.t1, true, std::move(second)});
                    split = true;
                }
            }
        }
        if (!split) {
            units.push_back({bar.t0, bar.t1, false, std::move(whole)});
        }

        for (const Unit& u : units) {
            const Stats& st = u.analysed.stats;
            Fit fit = u.analysed.fit;
            if (fit.found && held && !same_chord(*held, fit)) {
                const Fit old = fit_held(st, held->root, held->quality);
                const double margin = u.half ? kHarmonyHalfMargin : kHarmonyBarMargin;
                if (old.found && old.coverage >= kHarmonyMinCoverage &&
                    fit.score - old.score < margin &&
                    (!full_chords_only || plays_full_chord(st, old.root, old.quality))) {
                    fit = old; // not clearly better: the name stays
                    fit.margin = 0.0;
                }
            }
            HarmonyStretch s = make_stretch(song, u.t0, u.t1);
            if (fit.found) {
                Chord chord{true, fit.root, fit.quality, fit.bass_pc};
                chord = respell_symmetric(chord, key);
                held = fit;
                s.state = HarmonyState::Chord;
                s.chord = chord;
                s.spelling = spell_chord(chord, key);
                s.confidence = confidence_of(fit);
                s.hold_end_tick = std::max(bar.t1, u.t1 + bar.group_ticks);
                s.hold_end_s = song.tempo.seconds_at(s.hold_end_tick);
                span_of(song, hand_scope ? u.analysed.notes : notes[b], u.t0, u.t1, fit.tones, &st,
                        s);
            } else if (st.sounding >= kHarmonyMinSounding) {
                // Notes fill the stretch and make no chord: a tune, a unison.
                s.state = HarmonyState::NoChord;
                held.reset();
            } else {
                s.state = HarmonyState::Unclear; // too little to say; the held chord stays held
            }
            out.push_back(std::move(s));
        }
    }
    return merged(std::move(out));
}

// ---- Chord symbols

std::int64_t longest_bar(const Song& song) {
    std::int64_t longest = 0;
    for (std::size_t i = 0; i < song.measures.size(); ++i) {
        const std::int64_t end =
            i + 1 < song.measures.size() ? song.measures[i + 1].start_tick : song.end_tick;
        longest = std::max(longest, end - song.measures[i].start_tick);
    }
    return longest > 0 ? longest : std::int64_t{4} * std::max(song.tempo.ticks_per_quarter, 1);
}

// The end of the bar holding `tick`.
std::int64_t bar_end_after(const Song& song, std::int64_t tick) {
    const auto it =
        std::upper_bound(song.measures.begin(), song.measures.end(), tick,
                         [](std::int64_t t, const Measure& m) { return t < m.start_tick; });
    if (it != song.measures.end()) {
        return it->start_tick;
    }
    return std::max(song.end_tick, tick + 1);
}

// The stretches of time each symbol covers; see analyse_harmony.
struct SymbolSpan {
    std::int64_t t0;
    std::int64_t t1;
    std::size_t symbol;
};

std::vector<SymbolSpan> symbol_spans(const Song& song) {
    const auto& symbols = song.chord_symbols;
    std::int64_t typical = 1;
    const std::int64_t limit = kHarmonySymbolGapBars * longest_bar(song);
    for (std::size_t i = 0; i + 1 < symbols.size(); ++i) {
        const std::int64_t gap = symbols[i + 1].tick - symbols[i].tick;
        if (gap > 0 && gap <= limit) {
            typical = std::max(typical, gap);
        }
    }
    std::vector<SymbolSpan> spans;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const std::int64_t t0 = symbols[i].tick;
        if (!spans.empty() && t0 <= spans.back().t0) {
            continue; // two symbols at one tick: the first rules
        }
        std::int64_t t1 = bar_end_after(song, t0 + typical - 1);
        for (std::size_t j = i + 1; j < symbols.size(); ++j) {
            if (symbols[j].tick > t0) {
                t1 = std::min(t1, symbols[j].tick);
                break;
            }
        }
        spans.push_back({t0, std::max(t1, t0 + 1), i});
    }
    return spans;
}

std::vector<HarmonyStretch> symbol_stretches(const Song& song,
                                             const std::vector<SymbolSpan>& spans) {
    const std::vector<std::uint32_t> scoped = notes_of(song, HarmonyScope::All);
    const std::int64_t tpq = std::max(song.tempo.ticks_per_quarter, 1);
    std::vector<Frame> frames;
    for (const SymbolSpan& sp : spans) {
        frames.push_back(Frame{sp.t0, sp.t1, sp.t0, tpq, 4});
    }
    const auto notes = bucket(song, scoped, frames);
    std::vector<HarmonyStretch> out;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const ChordSymbol& sym = song.chord_symbols[spans[i].symbol];
        const Frame& frame = frames[i];
        HarmonyStretch s = make_stretch(song, frame.t0, frame.t1);
        s.from_file = true;
        s.state = HarmonyState::NoChord; // "N.C.", or a kind the app cannot name
        if (sym.chord.found) {
            const unsigned tones =
                rotate_mask(chord_quality_mask(sym.chord.quality), sym.chord.root_pc);
            s.state = HarmonyState::Chord;
            s.chord = sym.chord;
            s.spelling = sym.spelling;
            s.confidence = 1.0f;
            span_of(song, notes[i], frame.t0, frame.t1, tones, nullptr, s);
        }
        out.push_back(std::move(s));
    }
    return out;
}

// The detected stretches within [t0, t1), cut to it.
void append_detected(const Song& song, const std::vector<HarmonyStretch>& detected, std::int64_t t0,
                     std::int64_t t1, std::vector<HarmonyStretch>& out) {
    for (const HarmonyStretch& d : detected) {
        const std::int64_t a = std::max(d.start_tick, t0);
        const std::int64_t b = std::min(d.end_tick, t1);
        if (b <= a) {
            continue;
        }
        HarmonyStretch s = d;
        s.start_tick = a;
        s.end_tick = b;
        s.start_s = song.tempo.seconds_at(a);
        s.end_s = song.tempo.seconds_at(b);
        out.push_back(std::move(s));
    }
}

} // namespace

double harmony_beat_strength(double position, int beats_per_bar) {
    const double whole = std::floor(position + 1e-6);
    const double frac = std::max(position - whole, 0.0);
    double base = 0.65;
    if (whole < 0.5) {
        base = 1.0;
    } else if (beats_per_bar >= 4 && beats_per_bar % 2 == 0 && whole == beats_per_bar / 2) {
        base = 0.8;
    }
    constexpr double kTolerance = 0.06;
    if (frac < kTolerance) {
        return base;
    }
    return std::abs(frac - 0.5) < kTolerance ? 0.45 : 0.3;
}

double harmony_note_weight(double sounding_beats, double strength) {
    const double length = std::clamp(sounding_beats / 1.5, 0.0, 1.0);
    return strength * (0.35 + 0.65 * length);
}

std::vector<HarmonyStretch> analyse_harmony(const Song& song, HarmonyScope scope) {
    const std::vector<HarmonyStretch> detected = detect(song, scope);
    std::vector<HarmonyStretch> result;
    if (song.chord_symbols.empty() || scope != HarmonyScope::All) {
        result = detected; // a hand is named from its own notes, never from the file's symbols
    } else {
        // The symbols where the file has them, the detection in the bars between.
        const std::vector<SymbolSpan> spans = symbol_spans(song);
        const std::vector<HarmonyStretch> written = symbol_stretches(song, spans);
        std::int64_t cursor = song.measures.empty() ? 0 : song.measures.front().start_tick;
        for (std::size_t i = 0; i < spans.size(); ++i) {
            append_detected(song, detected, cursor, spans[i].t0, result);
            result.push_back(written[i]);
            cursor = spans[i].t1;
        }
        append_detected(song, detected, cursor, std::max(song.end_tick, cursor), result);
    }
    result = merged(std::move(result));
    if (scope == HarmonyScope::All) {
        // A hand's name is shown only while its notes form the chord: its rests are not filled.
        fill_unclear_gaps(result, longest_bar(song));
    }
    result = merged(std::move(result));
    root_diminished_on_resolution(song, result);
    return result;
}

HarmonyTimeline build_hand_harmony(const Song& song) {
    HarmonyTimeline timeline;
    timeline.left = analyse_harmony(song, HarmonyScope::Left);
    timeline.right = analyse_harmony(song, HarmonyScope::Right);
    return timeline;
}

} // namespace etude
