// SPDX-License-Identifier: GPL-3.0-or-later
#include "pitch_detect_impl.hpp"

#include "pitch_dsp.hpp"
#include "etude/pitch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace etude {

// Turns a period estimate into a note, correcting octave errors from the spectrum and
// scaling its confidence by how clean and in-tune the evidence is. `clean` is the frame the
// period came from: the raw frame, or the combed one with the previous note (fundamental
// `notch_hz`) filtered out. Octave tests and pitch refinement read the raw spectrum, where no
// partial is notched; harmonicity, completeness and sub-periods read the clean frame.
// `ringing_hz` > 0: the old note still rings in the raw frame. Its partials are left out of
// the octave tests there too, or they could pass for the odd partials of an octave below.
// `under_ringing`: an old note rings under this onset, its pitch known or not; the checks
// against blends of two notes apply.
PitchDetector::Impl::Candidate PitchDetector::Impl::judge(const dsp::LagCandidate& lag, int length, const dsp::FrameAnalyzer& clean,
                float notch_hz, float ringing_hz, bool under_ringing) const {
    Candidate c;
    const float fs = static_cast<float>(cfg.sample_rate);
    float hz = fs / lag.lag;
    float period = lag.lag;
    float confidence = lag.score;
    const float low_hz = hz_of(cfg.low_note, a4) * 0.97f;
    const float high_hz = hz_of(cfg.high_note, a4) * 1.03f;
    // Stretched partials pull the period sharp (up to a semitone in the top octave). Once the
    // spectrum resolves the partials, move to their peaks: a semitone-wide search, then a
    // fine one.
    const auto refine = [&](float f) {
        const float b =
            dsp::piano_inharmonicity(static_cast<int>(std::lround(note_from_hz(f, a4))));
        if (2.0f * clean.resolution_hz() > 0.35f * f) {
            // Partials not resolved (the lowest notes): undo the average stretch instead.
            return f / (1.0f + dsp::stretch_bias(clean, f, b, max_hz));
        }
        f = dsp::refine_f0(clean, f, b, max_hz, 0.06f);
        return dsp::refine_f0(clean, f, b, max_hz);
    };
    hz = refine(hz);
    for (int pass = 0; pass < 3; ++pass) {
        const int n = static_cast<int>(std::lround(note_from_hz(hz, a4)));
        // A pitch on the ringing note's series (that note, or an octave or twelfth above)
        // would lose every partial to the notch; it is tested on all of them.
        const bool on_series = ringing_hz > 0.0f && dsp::in_notch(hz, ringing_hz);
        const auto ev = dsp::octave_evidence(clean, hz, dsp::piano_inharmonicity(n),
                                             dsp::piano_inharmonicity(n - 12), low_hz, max_hz,
                                             notch_hz > 0.0f ? notch_hz
                                             : on_series     ? 0.0f
                                                             : ringing_hz);
        if (ev.odd_resolvable && ev.odd_ratio < 0.02f && 2.0f * hz <= high_hz) {
            hz = refine(hz * 2.0f);
            period /= 2.0f;
            continue;
        }
        if (ev.odd_resolvable && ev.triple_ratio < 0.02f && 3.0f * hz <= high_hz) {
            hz = refine(hz * 3.0f);
            period /= 3.0f;
            continue;
        }
        if (ev.half_resolvable && ev.half_ratio > 0.35f) {
            hz = refine(hz * 0.5f);
            period *= 2.0f;
            continue;
        }
        if (ev.third_resolvable && ev.third_ratio > 0.6f) {
            hz = refine(hz / 3.0f);
            period *= 3.0f;
            continue;
        }
        if (ev.odd_resolvable && (ev.odd_ratio < 0.05f || ev.triple_ratio < 0.05f)) {
            confidence *= 0.5f; // between "octave above" and "this note"
        }
        if (ev.half_resolvable && ev.half_ratio > 0.15f) {
            confidence *= 0.5f;
        }
        break;
    }
    if (hz < low_hz || hz > high_hz) {
        return c;
    }
    const float exact = note_from_hz(hz, a4);
    const int note = static_cast<int>(std::lround(exact));
    const float cents = std::fabs(exact - static_cast<float>(note)) * 100.0f;
    c.register_note = note;
    c.score = lag.score;
    if (cents > kMaxCentsFromKey) {
        return c; // between keys: not a note, so the detector ends up "not sure"
    }
    const float b = dsp::piano_inharmonicity(note);
    const float harm = dsp::harmonicity(clean, hz, b, max_hz);
    confidence *= std::clamp((harm - 0.5f) / 0.35f, 0.0f, 1.0f);
    if (!dsp::partials_complete(clean, hz, b, max_hz, notch_hz)) {
        confidence = 0.0f; // most of its partials are missing: two notes' common subharmonic
    }
    // Nothing of its own at its lowest partials: the period several notes share. The
    // fundamental is measured in the raw frame: the comb's gain changes with frequency, so
    // it would weigh the partials unevenly, and it may have filtered the fundamental away.
    const float fundamental = dsp::fundamental_share(analyzer, hz, b, max_hz);
    // A weak fundamental may be broadband sound filling its place: it waits for a frame that
    // shows whether it stands out (or for the longest frame).
    const bool judge_later = fundamental < kWeakFundamentalShare &&
                             2.0f * analyzer.resolution_hz() > dsp::kBesideLobe * hz &&
                             length < kMaxFrame;
    if (!first_partials_heard(clean, hz) || fundamental < kOwnFundamentalShare ||
        !dsp::fundamental_stands_out(analyzer, hz, b, kFundamentalProminence)) {
        confidence = 0.0f;
    } else if (fundamental < kWeakFundamentalShare && before_length > 0) {
        const auto e = dsp::fresh_evidence(before_analyzer, growth_after(), hz, b, max_hz);
        if (e.grown_fundamental < kOwnFundamentalShare * e.grown_strongest) {
            confidence = 0.0f;
        }
    }
    // Two notes in a simple ratio (a new note over the last one still ringing) repeat
    // together at a common long period, but each also repeats at its own shorter one. A
    // single piano note scores near zero or below at a third of its period or less, so a
    // strong sub-period there means this pitch is not a note. (Half the period is left to
    // the odd/even octave test: a note with strong even partials repeats there too.)
    // Under a ringing note, before the spectrum resolves a pitch's partials, two notes a step
    // apart can make the mix repeat near a long, inexact multiple of the new note's period
    // (seven periods for a semitone). That "pitch" has nothing of its own low partials: a
    // real note keeps a tenth of its energy below its seventh partial.
    const bool unresolved = 2.0f * clean.resolution_hz() > kResolvedLobe * hz;
    if (under_ringing && unresolved &&
        clean.band_energy(0.5f * hz, kOwnLowTop * hz) <
            kOwnLowShare * clean.band_energy(0.5f * hz, max_hz)) {
        confidence = 0.0f;
    }
    // Such a blend can also repeat near a long multiple of the new note's period that is not
    // exact enough for the low-partial test, so sub-periods down to a sixteenth are searched,
    // a little wider.
    const bool wide = under_ringing && unresolved;
    const int sub_max = wide ? kWideSubPeriods : kSubPeriods;
    const float sub_tol = wide ? kWideSubTolerance : kSubTolerance;
    for (int k = 3; k <= sub_max && confidence > 0.0f; ++k) {
        const float sub = period / static_cast<float>(k);
        if (sub < min_lag) {
            break;
        }
        float best = -1.0f;
        for (float t = sub * (1.0f - sub_tol); t <= sub * (1.0f + sub_tol) + 1.0f; t += 1.0f) {
            best = std::max(best, clean.nsdf_at(t));
        }
        if (best >= 0.45f) {
            confidence = 0.0f;
        }
    }
    c.note = note;
    c.hz = hz;
    c.confidence = std::clamp(confidence, 0.0f, 1.0f);
    c.period = period;
    c.ready = length >= pitch_window_samples(note, cfg.sample_rate, cfg.octave_check_periods) &&
              !judge_later;
    // Under a ringing note a lag at twice the new note's period can win: the mix repeats
    // there too, and the other note's partials can pass for this pitch's odd partials in
    // the octave test. The pitch counts only if the waveform does not also repeat at half
    // its period.
    if (under_ringing) {
        float half = -1.0f;
        const float half_period = 0.5f * period;
        for (float t = half_period * (1.0f - kHalfTolerance);
             t <= half_period * (1.0f + kHalfTolerance) + 1.0f; t += 1.0f) {
            half = std::max(half, clean.nsdf_at(t));
        }
        if (half >= kHalfPeriodLimit) {
            c.ready = false;
        }
    }
    return c;
}

// Every pitch, whatever path it came from, must be new sound: its partials grew at the onset.
// A blend of notes already ringing (their common subharmonic, the old note itself, a note
// between them) grows nowhere; a note an octave above the pitch grows only at its even
// partials, and in general a pitch k times below the new note grows only at every k-th partial.
// Where nothing was sounding at the pitch's partials there is nothing to tell apart: all of
// it is new. That holds for a chord struck out of silence too, whose common subharmonic is
// new sound as well; judge() has already turned such a pitch down by its empty lowest
// partials, which needs no audio from before the onset. A frame that cannot separate the
// partials shows only whether they grew as a whole, and whether the odd ones did.
void PitchDetector::Impl::check_fresh(Candidate& c) const {
    const auto e = dsp::fresh_evidence(before_analyzer, growth_after(), c.hz,
                                       dsp::piano_inharmonicity(c.note), max_hz);
    if (e.before < kOldSoundShare * e.after) {
        return;
    }
    // Its partials sounded before the onset: struck, they are louder now. Quieter, they are the
    // old sound, beating or decaying, whatever estimate of what rang was made.
    if (e.after < e.before) {
        c.confidence = 0.0f;
        return;
    }
    // A pitch whose own fundamental was already sounding (the same pitch, still ringing or
    // held by another instrument) can seem to grow in a frame that cannot separate its
    // partials, from a new note a step or two away spreading into them: it waits for a frame
    // that can, or for the longest one (A0 to F#1 never get one that can).
    if (!e.resolved && e.before_fundamental >= kHeldFundamentalShare * e.after_fundamental &&
        analyzer.length() < kMaxFrame) {
        c.ready = false;
    }
    if (e.grown < kGrownShare * e.after) {
        c.confidence = 0.0f;
        return;
    }
    // A frame that blurs the partials only spreads the even ones' growth onto the odd ones,
    // so too little at the odd ones holds even then.
    if (e.grown_odd < kGrownOddShare * e.grown_even) {
        c.confidence = 0.0f;
        return;
    }
    if (e.resolved && e.off_series < kOffSeriesShare) {
        c.confidence = 0.0f;
    }
}

} // namespace etude
