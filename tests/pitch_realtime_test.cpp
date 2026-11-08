// SPDX-License-Identifier: GPL-3.0-or-later
// The pitch detector runs on the real-time analysis thread, so process() must never allocate.
#include "alloc_count.hpp"

#include "etude/pitch_detect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr int kRate = 48000;

// Harmonic note: eight partials of amplitude 0.3/n, `cents` from the equal-tempered `note`, from
// `start` to `end`.
void add_note(std::vector<float>& audio, int note, double cents, int start, int end) {
    const double f = 440.0 * std::exp2((note - 69) / 12.0 + cents / 1200.0);
    for (int i = start; i < end; ++i) {
        const double t = static_cast<double>(i - start) / kRate;
        double v = 0.0;
        for (int n = 1; n <= 8; ++n) {
            v += 0.3 / n * std::sin(6.283185307 * f * n * t + 0.3 * (n - 1));
        }
        audio[static_cast<size_t>(i)] = static_cast<float>(v);
    }
}

// Runs `audio` through the detector in 256-sample blocks with the counter on, and returns the
// number of events; `allocations` gets the allocations made inside process().
// `kept`, when given, receives the events (reserved beforehand, so keeping them allocates nothing
// while the counter is on).
int run_counting(PitchDetector& d, const std::vector<float>& audio, int& allocations,
                 std::vector<PitchEvent>* kept = nullptr) {
    PitchEvent events[8];
    int got = 0;
    if (kept != nullptr) {
        kept->reserve(audio.size() / 256 * 8);
    }
    test::start_counting_allocations();
    for (size_t i = 0; i + 256 <= audio.size(); i += 256) {
        const int n = d.process(audio.data() + i, 256, events, 8);
        got += n;
        for (int k = 0; k < n && kept != nullptr; ++k) {
            kept->push_back(events[k]);
        }
    }
    allocations = test::stop_counting_allocations();
    return got;
}

// The key scheduled at `sample`: the last one struck at or before it (20 ms early allowed, as an
// onset may be placed a little early).
int key_at(const int* notes, const int* starts, int count, std::int64_t sample) {
    int key = -1;
    for (int i = 0; i < count; ++i) {
        if (starts[i] <= sample + kRate / 50) {
            key = notes[i];
        }
    }
    return key;
}

} // namespace

TEST_CASE("pitch detector: process() does not allocate, in open and expected-note mode") {
    // Two notes and a gap, so every path runs: gate, onset, acquisition, tracking, note-off.
    std::vector<float> audio(static_cast<size_t>(kRate), 0.0f);
    add_note(audio, 57, 0.0, 2400, kRate / 2);
    add_note(audio, 64, 0.0, kRate / 2, kRate * 3 / 4);
    // The counter itself works.
    test::start_counting_allocations();
    // Direct calls: an optimiser may drop a new-expression whose pointer is never used.
    void* probe = ::operator new(sizeof(int));
    const int probes = test::stop_counting_allocations();
    ::operator delete(probe);
    REQUIRE(probes == 1);

    for (int pass = 0; pass < 4; ++pass) {
        CAPTURE(pass);
        PitchDetectorConfig cfg;
        cfg.method = pass == 1 ? PitchMethod::Yin : PitchMethod::Mpm;
        PitchDetector d(cfg);
        if (pass == 2) {
            const int chord[] = {57, 64};
            d.set_expected_notes(chord, 2);
        } else if (pass == 3) {
            const int single[] = {57};
            d.set_expected_notes(single, 1);
        }
        int allocations = -1;
        std::vector<PitchEvent> events;
        const int got = run_counting(d, audio, allocations, &events);
        CHECK(allocations == 0);
        CHECK(got > 0);
        constexpr int kNotes[] = {57, 64};
        constexpr int kStarts[] = {2400, kRate / 2};
        for (const auto& e : events) {
            if (e.type == PitchEventType::NoteOn) {
                CHECK(e.note == key_at(kNotes, kStarts, 2, e.onset_sample));
            }
        }
    }
}

TEST_CASE("pitch detector: process() does not allocate while it learns the tuning") {
    // Six notes, the whole instrument 20 cents sharp, so the learned A4 moves.
    std::vector<float> audio(static_cast<size_t>(kRate * 2 + 4800), 0.0f);
    int start = 2400;
    for (int note : {57, 60, 64, 67, 62, 65}) {
        add_note(audio, note, 20.0, start, start + kRate / 3);
        start += kRate / 3;
    }
    PitchDetectorConfig cfg;
    cfg.learn_tuning = true;
    PitchDetector d(cfg);
    int allocations = -1;
    const int got = run_counting(d, audio, allocations);
    CHECK(allocations == 0);
    CHECK(got > 0);
    // The learning path ran: the tuning left 440 Hz for about 20 cents above it.
    CHECK(d.a4_hz() > 443.0f);
}

TEST_CASE("pitch detector: process() does not allocate when notes ring into each other") {
    // Each note keeps sounding under the next (a fifth below, a step, the same key again), so
    // the ringing-note paths run: the audio before the onset, the comb, the restrike check.
    // Every note reported must be the key struck at its onset; "not sure" is allowed.
    constexpr int kNotes[] = {57, 50, 52, 52, 45};
    int starts[5] = {};
    std::vector<float> audio(static_cast<size_t>(kRate * 2), 0.0f);
    std::vector<float> one(audio.size(), 0.0f);
    int start = 2400;
    for (int i = 0; i < 5; ++i) {
        starts[i] = start;
        std::fill(one.begin(), one.end(), 0.0f);
        add_note(one, kNotes[i], 0.0, start, static_cast<int>(audio.size()));
        for (size_t k = static_cast<size_t>(start); k < audio.size(); ++k) {
            audio[k] = 0.6f * audio[k] + one[k]; // the old note, quieter, under the new one
        }
        start += kRate / 4;
    }
    for (auto method : {PitchMethod::Mpm, PitchMethod::Yin}) {
        CAPTURE(static_cast<int>(method));
        PitchDetectorConfig cfg;
        cfg.method = method;
        PitchDetector d(cfg);
        int allocations = -1;
        std::vector<PitchEvent> events;
        const int got = run_counting(d, audio, allocations, &events);
        CHECK(allocations == 0);
        CHECK(got > 0);
        for (const auto& e : events) {
            if (e.type == PitchEventType::NoteOn) {
                CAPTURE(e.onset_sample);
                CHECK(e.note == key_at(kNotes, starts, 5, e.onset_sample));
            }
        }
    }
}
