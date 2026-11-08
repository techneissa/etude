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

void PitchDetector::Impl::reset_note_search() {
    std::fill(delta.begin(), delta.end(), 0.0f);
    tentative_note = -1;
    tentative_period = 0.0f;
    tentative_conf = 0.0f;
    stable_note = -1;
    stable_count = 0;
    stable_conf = 0.0f;
    guess_note = -1;
    guess_score = 0.0f;
    verdict_same = 0;
    last_verdict = ExpectedVerdict::NotSure;
    last_present = last_absent = 0;
}

void PitchDetector::Impl::not_sure() {
    PitchEvent e;
    e.type = expected_count > 0 ? PitchEventType::Expected : PitchEventType::NotSure;
    e.verdict = ExpectedVerdict::NotSure;
    e.onset_sample = onset;
    e.sample = written;
    e.guess_note = expected_count > 0 ? expected[0] : guess_note;
    if (expected_count > 1) {
        e.chord_present = last_present;
        e.chord_absent = last_absent;
    }
    emit(e);
    state = State::Done;
    if (tentative_note >= 0) {
        last_note = tentative_note;
        last_period = tentative_period;
    }
}

// `refractory`: no further flux onset for kOnsetRefractoryMs.
// A gate opening sets none, so a real attack right after noise crept over the gate counts.
void PitchDetector::Impl::start_note(std::int64_t at, bool refractory) {
    const std::int64_t last = onset;
    if (state == State::Acquiring && tentative_note >= 0) {
        last_note = tentative_note;
        last_period = tentative_period;
    }
    // An onset this soon after the last one, before that one's note was confirmed, is most
    // likely the same attack still building (a bass note's energy keeps rising). What rang
    // before that attack still rings: keep it, and the spectrum from before it.
    const bool quick = state == State::Acquiring &&
                       static_cast<float>(at - last) <
                           kQuickOnsetSeconds * static_cast<float>(cfg.sample_rate);
    if (quick) {
        end_current_note();
        onset = at;
        if (refractory) {
            last_onset = written;
        }
        state = State::Acquiring;
        reset_note_search();
        return;
    }
    const bool ringing = state == State::Sounding && current_note >= 0;
    prev_note = ringing ? current_note : -1;
    prev_period = ringing ? current_period : 0.0f;
    end_current_note();
    onset = at;
    rang_before = false;
    before_ms = 0.0f;
    for (int i = 1; i <= kBeforeSamples; ++i) {
        const float v = sample_at(before_end() - i);
        before_ms += v * v / static_cast<float>(kBeforeSamples);
    }
    copy_recent(before_audio.data(), before_end(), kBeforeSpectrum);
    before_available = static_cast<int>(
        std::clamp<std::int64_t>(before_end() - last, kMinBeforeSpectrum, kBeforeSpectrum));
    before_length = 0;
    hear_ringing(last, state != State::Idle);
    if (refractory) {
        last_onset = written;
    }
    state = State::Acquiring;
    reset_note_search();
}

// The note ringing under a new onset may never have been confirmed (its pitch unsure, or its
// onset missed, so an older note still counts as sounding), or may have faded under the gate
// and still ring faintly under a quiet new note. Take its pitch from the audio between the
// last onset and this one, so it is filtered out and its subharmonics are treated with
// suspicion. The confirmed note stays when that audio gives no clear pitch. `sounding`: the
// gate was open up to this onset.
void PitchDetector::Impl::hear_ringing(std::int64_t last, bool sounding) {
    const std::int64_t end = before_end();
    const int available = static_cast<int>(std::min<std::int64_t>(
        {end - last, end - (written - kHistory + kMaxFrame), kMaxFrame}));
    if (available < kMinBeforeSearch) {
        return;
    }
    rang_before = sounding;
    // The periods over everything since the last onset, then each one's pitch from its most
    // recent four periods, which leaves out the attack and any note before it. The method's
    // own pick comes first (a multiple of the period scores about as high), the others by
    // strength after it.
    int length = available;
    copy_recent(frame.data(), end, length);
    analyzer.analyze(frame.data(), length, std::min(length / 2, max_lag));
    std::array<dsp::LagCandidate, dsp::kMaxLagCandidates> lags{};
    const int n = cfg.method == PitchMethod::Mpm
                      ? dsp::mpm_candidates(analyzer, min_lag, kMpmK, lags.data())
                      : dsp::yin_candidates(analyzer, min_lag, kYinThreshold, lags.data());
    std::sort(lags.begin() + std::min(n, 1), lags.begin() + n,
              [](const dsp::LagCandidate& a, const dsp::LagCandidate& b) {
                  return a.score > b.score;
              });
    for (int i = 0; i < n; ++i) {
        const float period = lags[static_cast<size_t>(i)].lag;
        length = std::clamp(static_cast<int>(std::ceil(kRingingPeriods * period)),
                            kMinBeforeSearch, available);
        copy_recent(frame.data(), end, length);
        analyzer.analyze(frame.data(), length, std::min(length / 2, max_lag));
        std::array<dsp::LagCandidate, dsp::kMaxLagCandidates> again{};
        const int m = cfg.method == PitchMethod::Mpm
                          ? dsp::mpm_candidates(analyzer, min_lag, kMpmK, again.data())
                          : dsp::yin_candidates(analyzer, min_lag, kYinThreshold, again.data());
        // The same period in the shorter frame, if it is still there.
        const dsp::LagCandidate* lag = nullptr;
        for (int k = 0; k < m; ++k) {
            const auto& a = again[static_cast<size_t>(k)];
            if (std::fabs(a.lag - period) <= kSamePeriod * period &&
                (lag == nullptr || a.score > lag->score)) {
                lag = &a;
            }
        }
        if (lag == nullptr) {
            continue;
        }
        const Candidate c = judge(*lag, length, analyzer, 0.0f);
        // Only to filter and to distrust, never to report: a clear period is enough even
        // when a quiet note's partials do not all stand out.
        // Several notes ringing together (a held pedal) also repeat at a long common period,
        // a "pitch" with nothing at its own lowest partials. Combing at it would cancel any
        // of those notes struck again, so only a single note's period is taken.
        const bool own_low = first_partials_heard(analyzer, c.hz);
        if (c.note >= 0 && own_low &&
            (c.confidence >= kRingingConfidence || lag->score >= kRingingClarity)) {
            prev_note = c.note;
            prev_period = c.period;
            return;
        }
    }
    // Too faint or blurred to name: the last note heard, if its period is still there.
    if (prev_period <= 0.0f && last_period > 0.0f) {
        length = std::clamp(static_cast<int>(std::ceil(kRingingPeriods * last_period)),
                            kMinBeforeSearch, available);
        copy_recent(frame.data(), end, length);
        analyzer.analyze(
            frame.data(), length,
            std::min(length / 2, static_cast<int>(std::ceil(last_period)) + kLagMargin));
        if (analyzer.nsdf_at(last_period) >= kTailClarity) {
            prev_note = last_note;
            prev_period = last_period;
        }
    }
}

} // namespace etude
