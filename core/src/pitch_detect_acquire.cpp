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

void PitchDetector::Impl::acquire_step() {
    const int length = frame_length();
    if (length < kMinFrame) {
        return;
    }
    copy_recent(frame.data(), written, length);
    const int lag_limit = std::min(length / 2, max_lag);
    analyzer.analyze(frame.data(), length, lag_limit);
    const int before_now = std::min(length, before_available);
    if (before_now != before_length) {
        before_analyzer.analyze(before_audio.data() + kBeforeSpectrum - before_now, before_now,
                                before_now / 2);
        before_length = before_now;
    }
    if (length > before_length) {
        after_analyzer.analyze(frame.data() + length - before_length, before_length,
                               before_length / 2);
    }

    // While the previous note still rings, its partials blur the new note's period and pull
    // in common subharmonics. Comb it out (the audio before the onset cancels it from the
    // first sample) and search the result too. Candidates come from both frames, each judged
    // on its own; the old note counts from the raw frame only if it was struck again (the
    // comb then leaves little).
    bool combed = false;
    bool restruck = false;
    const bool unknown_ringing = rang_before && prev_period <= 0.0f &&
                                 static_cast<float>(written - onset) <
                                     kCombSeconds * static_cast<float>(cfg.sample_rate);
    const bool ringing =
        prev_period > 0.0f && static_cast<float>(written - onset) <
                                  kCombSeconds * static_cast<float>(cfg.sample_rate);
    // Two notes blend into a false pitch only when the old one is not much quieter than the
    // new frame; a faint background tone leaves the new note's period alone.
    const bool loud = before_ms >= kLoudRinging * analyzer.rms() * analyzer.rms();
    if (ringing) {
        const std::int64_t start = written - length;
        const auto whole = static_cast<std::int64_t>(prev_period);
        const float frac = prev_period - static_cast<float>(whole);
        for (int j = 0; j < length; ++j) {
            const std::int64_t at = start + j - whole;
            const float delayed = sample_at(at) + (sample_at(at - 1) - sample_at(at)) * frac;
            comb[static_cast<size_t>(j)] = frame[static_cast<size_t>(j)] - delayed;
        }
        comb_analyzer.analyze(comb.data(), length, lag_limit);
        const float raw = analyzer.rms();
        const float residual =
            raw > 0.0f ? (comb_analyzer.rms() * comb_analyzer.rms()) / (raw * raw) : 0.0f;
        restruck = residual < kRestrikeResidual;
        combed = true;
    }

    // A pitch whose whole multiple is the ringing note: the new note itself (struck an
    // octave or more below), or the common subharmonic of the two notes.
    const float prev_hz = ringing ? static_cast<float>(cfg.sample_rate) / prev_period : 0.0f;
    const auto below_ringing = [&](float hz) {
        const float k = std::round(prev_hz / hz);
        return k >= 2.0f && std::fabs(prev_hz - k * hz) <= kSamePeriod * prev_hz;
    };

    std::array<Candidate, 2 * dsp::kMaxLagCandidates> cands{};
    int found = 0;
    const auto collect = [&](const dsp::FrameAnalyzer& source, float notch_hz) {
        std::array<dsp::LagCandidate, dsp::kMaxLagCandidates> lags{};
        const int n = cfg.method == PitchMethod::Mpm
                          ? dsp::mpm_candidates(source, min_lag, kMpmK, lags.data())
                          : dsp::yin_candidates(source, min_lag, kYinThreshold, lags.data());
        for (int i = 0; i < n; ++i) {
            Candidate c =
                judge(lags[static_cast<size_t>(i)], length, source, notch_hz,
                      combed && loud ? prev_hz : 0.0f, loud && (combed || unknown_ringing));
            // The old note still ringing, not a new strike. Only when it is really heard at
            // the pitch's partials: a ringing pitch estimated from a faint or misjudged tail
            // (a quiet tone an octave up taken for this key) must not silence a real strike.
            const bool same_key = prev_note >= 0 && c.note == prev_note;
            if (same_key && ringing && !restruck && old_sound_at(c)) {
                c.confidence = 0.0f;
            }
            // A soft neighbour a step away barely moves the comb residual under a louder old
            // note, so it can pass for the old key struck again. Struck again, the old key is
            // louder at its partials than just before the onset, and grows there, not a step
            // away. Until the frame separates a semitone at some partial, the old key waits.
            if (same_key && ringing && restruck && c.confidence > 0.0f) {
                const float b = dsp::piano_inharmonicity(c.note);
                const auto g =
                    dsp::neighbour_growth(before_analyzer, growth_after(), c.hz, b, max_hz);
                const auto e =
                    dsp::fresh_evidence(before_analyzer, growth_after(), c.hz, b, max_hz);
                if (e.after < kRestrikeLouder * e.before) {
                    c.confidence = 0.0f;
                } else if (g.separable == 0) {
                    c.ready = false;
                } else if (g.own < kRestrikeOwnGrowth * g.neighbours ||
                           g.neighbours >= kNeighbourRise * g.own_after) {
                    c.confidence = 0.0f;
                }
            }
            // The comb cancels exact multiples of the old period, but a piano's partials are
            // stretched: when the old note grows (struck again), its upper partials leak
            // through and look like another note. A new note's partials on the old series
            // are notched, so its energy lies off that series; a leak's lies on it.
            if (notch_hz > 0.0f && c.confidence > 0.0f) {
                const float b = dsp::piano_inharmonicity(c.note);
                const float cap = kPartialSearch * c.hz;
                const float all = dsp::partial_energy(source, c.hz, b, 1, 1, 16, max_hz, cap);
                const float off =
                    dsp::partial_energy(source, c.hz, b, 1, 1, 16, max_hz, cap, notch_hz);
                if (all - off > off) {
                    c.confidence = 0.0f;
                }
            }
            if (c.confidence > 0.0f && ringing && !restruck && loud && below_ringing(c.hz) &&
                !dsp::own_partials_present(analyzer, c.hz, dsp::piano_inharmonicity(c.note),
                                           max_hz, prev_hz)) {
                c.confidence = 0.0f; // the common subharmonic of the old and the new note
            }
            if (c.confidence > 0.0f) {
                check_fresh(c);
            }
            cands[static_cast<size_t>(found++)] = c;
        }
    };
    if (combed) {
        // Searched while the old key still counts as ringing, so only other pitches survive.
        const bool residual_small = restruck;
        restruck = false;
        collect(comb_analyzer, static_cast<float>(cfg.sample_rate) / prev_period);
        // A small residual means the same key struck again only when the frame without the
        // old note holds no other pitch: a soft neighbour under a louder old note also leaves
        // little after the comb.
        bool other = false;
        for (int i = 0; i < found; ++i) {
            const auto& c = cands[static_cast<size_t>(i)];
            other = other || (c.confidence > 0.0f && c.note != prev_note);
        }
        if (residual_small && !other) {
            restruck = true;
            combed = false;
            found = 0;
        }
    }
    const int clean_found = found;
    collect(analyzer, 0.0f);
    if (combed && loud) {
        // In a short frame two notes a step or two apart blur into one period between them.
        // A pitch from the raw frame counts only if the frame without the old note has it too.
        for (int i = clean_found; i < found; ++i) {
            auto& raw = cands[static_cast<size_t>(i)];
            bool seen = false;
            for (int j = 0; j < clean_found; ++j) {
                const auto& clean = cands[static_cast<size_t>(j)];
                seen = seen || (clean.note == raw.note && clean.confidence > 0.0f);
            }
            if (!seen) {
                raw.confidence = 0.0f;
            }
        }
    }

    for (int i = 0; i < found; ++i) {
        const auto& c = cands[static_cast<size_t>(i)];
        if (c.register_note >= cfg.low_note && c.register_note <= cfg.high_note &&
            c.score > guess_score) {
            guess_score = c.score;
            guess_note = c.register_note;
        }
    }

    // Observation log-likelihoods: candidates get their confidence, other notes a small
    // floor, and the unvoiced state whatever confidence is left.
    const float floor_log = std::log(1e-3f);
    std::fill(obs.begin(), obs.end(), floor_log);
    float voiced = 0.0f;
    for (int i = 0; i < found; ++i) {
        const auto& c = cands[static_cast<size_t>(i)];
        if (c.note < cfg.low_note || c.note > cfg.high_note || c.confidence <= 0.0f) {
            continue;
        }
        auto& o = obs[static_cast<size_t>(c.note - cfg.low_note)];
        o = std::max(o, std::log(std::max(c.confidence, 1e-3f)));
        voiced = std::max(voiced, c.confidence);
    }
    obs.back() = std::log(std::max(1.0f - voiced, 0.02f));

    // One step of the max-product (Viterbi) recursion with "stay" much likelier than "jump",
    // so a single odd frame cannot move the path.
    const float stay = std::log(0.95f);
    const float jump = std::log(0.05f / static_cast<float>(states));
    const float best_prev = *std::max_element(delta.begin(), delta.end());
    for (size_t s = 0; s < delta.size(); ++s) {
        next[s] = std::max(delta[s] + stay, best_prev + jump) + obs[s];
    }
    const auto top_it = std::max_element(next.begin(), next.end());
    const float top = *top_it;
    const int best = static_cast<int>(top_it - next.begin());
    for (size_t s = 0; s < next.size(); ++s) {
        delta[s] = next[s] - top;
    }

    const Candidate* support = nullptr;
    if (best < states - 1) {
        const int note = cfg.low_note + best;
        for (int i = 0; i < found; ++i) {
            const auto& c = cands[static_cast<size_t>(i)];
            if (c.note == note && (support == nullptr || c.confidence > support->confidence)) {
                support = &c;
            }
        }
    }
    if (support != nullptr && support->confidence > tentative_conf) {
        tentative_note = support->note;
        tentative_period = support->period;
        tentative_conf = support->confidence;
    }
    if (support != nullptr && support->ready && support->confidence >= cfg.min_confidence) {
        if (support->note == stable_note) {
            ++stable_count;
            stable_conf += support->confidence;
        } else {
            stable_note = support->note;
            stable_count = 1;
            stable_conf = support->confidence;
        }
    } else {
        stable_count = 0;
        stable_note = -1;
        stable_conf = 0.0f;
    }

    if (stable_count >= cfg.confirm_frames) {
        const float refined = support->hz;
        PitchEvent e;
        e.type = PitchEventType::NoteOn;
        e.note = stable_note;
        e.frequency_hz = refined;
        e.confidence = stable_conf / static_cast<float>(stable_count);
        e.onset_sample = onset;
        e.sample = written;
        emit(e);
        current_note = stable_note;
        current_period = support->period;
        last_note = current_note;
        last_period = current_period;
        state = State::Sounding;
        track_counter = 0;
        track_misses = 0;
        if (cfg.learn_tuning && e.confidence >= kLearnMinConfidence) {
            learn_tuning(stable_note, refined);
        }
        return;
    }
    if (past_deadline()) {
        not_sure();
    }
}

// While a note sounds, check now and then that its period is still there.
void PitchDetector::Impl::track_step() {
    if (++track_counter < kTrackEvery) {
        return;
    }
    track_counter = 0;
    const int length = std::clamp(
        pitch_window_samples(current_note, cfg.sample_rate, cfg.octave_check_periods), 512,
        kMaxFrame);
    const float period = static_cast<float>(cfg.sample_rate) / hz_of(current_note, a4);
    const int lag_max =
        std::min(length / 2, static_cast<int>(std::ceil(period * 1.04f)) + kLagMargin);
    if (static_cast<float>(lag_max) < period * 1.03f) {
        return;
    }
    copy_recent(frame.data(), written, length);
    analyzer.analyze(frame.data(), length, lag_max);
    float best = -1.0f;
    for (int t = static_cast<int>(period * 0.97f); t <= static_cast<int>(period * 1.03f) + 1;
         ++t) {
        best = std::max(best, analyzer.nsdf_at(static_cast<float>(t)));
    }
    track_misses = best < 0.4f ? track_misses + 1 : 0;
    if (track_misses >= kTrackMisses) {
        end_current_note();
        state = State::Done;
    }
}

void PitchDetector::Impl::expected_step() {
    const std::int64_t elapsed = written - onset;
    const int needed = expected_count == 1
                           ? ExpectedNoteChecker::window_samples(expected[0], cfg.sample_rate)
                           : ExpectedNoteChecker::chord_window_samples(
                                 expected.data(), expected_count, cfg.sample_rate);
    if (elapsed < needed) {
        return;
    }
    const int length = frame_length();
    copy_recent(frame.data(), written, length);
    PitchEvent e;
    e.type = PitchEventType::Expected;
    e.onset_sample = onset;
    e.sample = written;
    bool decided = false;
    if (expected_count == 1) {
        const auto r = checker.check(frame.data(), length, expected[0], a4);
        if (r.verdict != ExpectedVerdict::NotSure && r.verdict == last_verdict) {
            ++verdict_same;
        } else {
            verdict_same = r.verdict != ExpectedVerdict::NotSure ? 1 : 0;
        }
        last_verdict = r.verdict;
        if (verdict_same >= 2) {
            e.verdict = r.verdict;
            e.note = r.heard_note;
            e.confidence = r.confidence;
            decided = true;
        }
    } else {
        const auto r =
            checker.check_chord(frame.data(), length, expected.data(), expected_count, a4);
        std::uint16_t present = 0;
        std::uint16_t absent = 0;
        bool unsure = false;
        float conf = 1.0f;
        for (int i = 0; i < r.count; ++i) {
            if (r.states[i] == ChordNoteState::Present) {
                present = static_cast<std::uint16_t>(present | (1u << i));
            } else if (r.states[i] == ChordNoteState::Absent) {
                absent = static_cast<std::uint16_t>(absent | (1u << i));
            } else {
                unsure = true;
            }
            conf = std::min(conf, r.states[i] == ChordNoteState::Absent ? 1.0f - r.scores[i]
                                                                        : r.scores[i]);
        }
        if (!unsure && present == last_present && absent == last_absent) {
            ++verdict_same;
        } else {
            verdict_same = unsure ? 0 : 1;
        }
        last_present = present;
        last_absent = absent;
        if (verdict_same >= 2) {
            e.verdict = absent == 0 ? ExpectedVerdict::Expected : ExpectedVerdict::NotSure;
            e.chord_present = present;
            e.chord_absent = absent;
            e.confidence = conf;
            decided = true;
        }
    }
    if (decided) {
        emit(e);
        state = State::Done;
        return;
    }
    if (elapsed >= std::max<std::int64_t>(needed, kMaxFrame) + kDeadlineHops * kHop) {
        not_sure();
    }
}

void PitchDetector::Impl::learn_tuning(int note, float hz) {
    const float implied = hz / std::exp2(static_cast<float>(note - kA4) / 12.0f);
    tuning[static_cast<size_t>(tuning_next)] = implied;
    tuning_next = (tuning_next + 1) % kTuningNotes;
    tuning_count = std::min(tuning_count + 1, kTuningNotes);
    if (tuning_count < kTuningSettledNotes) {
        return;
    }
    std::array<float, kTuningNotes> sorted = tuning;
    std::sort(sorted.begin(), sorted.begin() + tuning_count);
    const float median = tuning_count % 2 == 1
                             ? sorted[static_cast<size_t>(tuning_count / 2)]
                             : 0.5f * (sorted[static_cast<size_t>(tuning_count / 2 - 1)] +
                                       sorted[static_cast<size_t>(tuning_count / 2)]);
    const float limit = std::exp2(50.0f / 1200.0f);
    a4 = std::clamp(median, a4_base / limit, a4_base * limit);
}

} // namespace etude
