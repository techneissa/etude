// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: property (split from evaluator_test.cpp).
#include "etude/evaluator.hpp"
#include "alloc_count.hpp"
#include "etude/keyboard.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include "evaluator_test_support.hpp"

using namespace etude;
using OpKind = TransportOp::Kind;
using namespace evaluator_test_support;

// ---------------------------------------------------------------------------------------------
// Properties

namespace {

// A random song: single notes, chords of 2 to 4 keys, quick repeats of one key, trills, and both
// hands on one key at once. Two notes on one key either start together or at least 60 ms apart,
// as a real player needs to strike a key twice.
std::vector<N> random_song(std::mt19937& rng, int groups) {
    std::uniform_real_distribution<double> gap(0.05, 0.4);
    std::uniform_real_distribution<double> dur(0.05, 0.5);
    std::uniform_int_distribution<int> pitch(40, 90);
    std::uniform_int_distribution<int> pattern(0, 4);
    std::uniform_int_distribution<int> hand(0, 2);
    const Hand hands[] = {Hand::Left, Hand::Right, Hand::Unknown};
    std::vector<N> out;
    std::array<double, 128> last{};
    last.fill(-1.0);
    const auto add = [&](double t, int p, Hand h) {
        if (last[static_cast<std::size_t>(p)] >= 0.0 && t != last[static_cast<std::size_t>(p)] &&
            t - last[static_cast<std::size_t>(p)] < 0.060) {
            return;
        }
        last[static_cast<std::size_t>(p)] = t;
        out.push_back({t, p, h, dur(rng)});
    };
    double t = 0.5;
    for (int g = 0; g < groups; ++g) {
        const Hand h = hands[hand(rng)];
        const int p = pitch(rng);
        switch (pattern(rng)) {
        case 0:
            add(t, p, h);
            break;
        case 1: {
            std::uniform_int_distribution<int> size(2, 4);
            const int n = size(rng);
            for (int i = 0; i < n; ++i) {
                add(t, std::min(127, p + i * 4), h);
            }
            break;
        }
        case 2: // quick repeats
            add(t, p, h);
            add(t + 0.08, p, h);
            add(t + 0.16, p, h);
            t += 0.16;
            break;
        case 3: // trill
            for (int i = 0; i < 6; ++i) {
                add(t + i * 0.07, i % 2 == 0 ? p : p + 2, h);
            }
            t += 0.35;
            break;
        case 4: // both hands on one key
            add(t, p, Hand::Left);
            add(t, p, Hand::Right);
            break;
        }
        t += gap(rng);
    }
    return out;
}

struct Strike {
    double wall_s;
    int key;
};

// One press per key and start: what a perfect player plays, with jitter inside the Perfect window
// and chords struck in random order.
std::vector<Strike> perfect_strikes(std::mt19937& rng, const std::vector<N>& song, int semitones,
                                    double rate, double max_jitter_s) {
    std::uniform_real_distribution<double> jitter(-max_jitter_s, max_jitter_s);
    std::vector<Strike> out;
    std::vector<std::pair<double, int>> seen;
    for (const N& n : song) {
        const std::pair<double, int> id{n.start, n.pitch};
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
            continue;
        }
        seen.push_back(id);
        out.push_back({1.0 + n.start / rate + jitter(rng), n.pitch + semitones});
    }
    std::shuffle(out.begin(), out.end(), rng);
    return out;
}

} // namespace

TEST_CASE("property: a perfect player on random songs gets every note Perfect and no wrong") {
    for (unsigned seed = 1; seed <= 40; ++seed) {
        CAPTURE(seed);
        std::mt19937 rng(seed);
        const std::vector<N> notes = random_song(rng, 60);
        const int speeds[] = {25, 50, 75, 100, 125, 150};
        const int speed = speeds[seed % 6];
        const int semitones = static_cast<int>(seed % 25) - 12;
        FlowOptions o;
        // Pitches 40..102 shifted by up to an octave stay inside 0..127, so nothing folds.
        o.transposition = Transposition(semitones, KeyRange{0, 127});
        FlowRig rig(song_of(notes), o);
        rig.play_at(1.0, speed);
        // Inside the Perfect window (28 ms) with room for rounding.
        for (const Strike& st : perfect_strikes(rng, notes, semitones, speed / 100.0, 0.025)) {
            rig.send(note_on(st.wall_s, st.key));
        }
        rig.run_until(1.0 + rig.song.duration_s * 100.0 / speed + 1.0);
        rig.eval.finish();
        const EvaluationSummary& s = rig.eval.summary();
        CHECK(s.notes.size() == notes.size());
        CHECK(s.counts.perfect == static_cast<int>(notes.size()));
        CHECK(s.wrong == 0);
        CHECK(s.longest_combo == static_cast<int>(notes.size()));
        CHECK(rig.eval.pending_notes() == 0);
        CHECK(rig.eval.pending_presses() == 0);
    }
}

TEST_CASE("property: a sloppy player's extra notes never reduce the correct count") {
    for (unsigned seed = 1; seed <= 40; ++seed) {
        CAPTURE(seed);
        std::mt19937 rng(seed * 7919u);
        const std::vector<N> notes = random_song(rng, 60);
        const int speed = 50 + static_cast<int>(seed % 11) * 10;
        const double rate = speed / 100.0;
        FlowRig rig(song_of(notes), FlowOptions{});
        rig.play_at(1.0, speed);
        // Every note struck anywhere inside the window, plus extra presses: near misses on the
        // right key, neighbours, and random keys at random times.
        std::vector<Strike> strikes = perfect_strikes(rng, notes, 0, rate, 0.079);
        const std::size_t real = strikes.size();
        std::uniform_int_distribution<std::size_t> pick(0, real - 1);
        std::uniform_real_distribution<double> near(-0.079, 0.079);
        std::uniform_int_distribution<int> any_key(30, 100);
        std::uniform_real_distribution<double> any_time(1.0, 1.0 + rig.song.duration_s / rate);
        for (std::size_t i = 0; i < real / 2; ++i) {
            const Strike base = strikes[pick(rng)]; // a copy: push_back moves the vector
            strikes.push_back({base.wall_s + near(rng), base.key});
            strikes.push_back({base.wall_s + near(rng), base.key + 1});
            strikes.push_back({any_time(rng), any_key(rng)});
        }
        // Keys pressed early for a later note (up to 3.5 song seconds ahead, inside the 4 s
        // lookahead) where no note of that key is within a window: never a match, never wrong.
        int early = 0;
        std::uniform_real_distribution<double> ahead(0.2, 3.5);
        for (std::size_t i = 0; i < real / 4; ++i) {
            const N& n = notes[pick(rng) % notes.size()];
            const double song_s = n.start - ahead(rng);
            const bool near_same_key = std::any_of(notes.begin(), notes.end(), [&](const N& m) {
                return m.pitch == n.pitch && std::abs(m.start - song_s) <= 0.080 * rate + 0.01;
            });
            if (song_s < 0.01 || near_same_key) {
                continue;
            }
            strikes.push_back({1.0 + song_s / rate, n.pitch});
            ++early;
        }
        // Random keys no note of the song uses are always wrong.
        int foreign = 0;
        for (std::size_t i = real; i < strikes.size(); ++i) {
            const int key = strikes[i].key;
            if (std::none_of(notes.begin(), notes.end(),
                             [&](const N& m) { return m.pitch == key; })) {
                ++foreign;
            }
        }
        const int extras = static_cast<int>(strikes.size() - real);
        for (const Strike& st : strikes) {
            rig.send(note_on(st.wall_s, st.key));
        }
        rig.run_until(1.0 + rig.song.duration_s / rate + 1.0);
        rig.eval.finish();
        const EvaluationSummary& s = rig.eval.summary();
        CHECK(s.notes.size() == notes.size());
        CHECK(s.counts.missed == 0);
        CHECK(hits(s) == static_cast<int>(notes.size()));
        CHECK(early > 0);
        CHECK(s.wrong <= extras - early);
        CHECK(s.wrong >= foreign);
    }
}

TEST_CASE("property: a Wait Mode player who plays each chord when it stops never gets stuck") {
    for (unsigned seed = 1; seed <= 15; ++seed) {
        CAPTURE(seed);
        std::mt19937 rng(seed * 104729u);
        const std::vector<N> notes = random_song(rng, 30);
        WaitRig rig(song_of(notes), WaitOptions{});
        rig.play_at(1.0, 50 + static_cast<int>(seed % 3) * 50);
        std::vector<int> down;
        std::uniform_real_distribution<double> roll(0.0, 0.040);
        std::bernoulli_distribution lose(0.3); // a key-up lost on the way
        std::bernoulli_distribution peek(0.3); // a key of the next chord pressed early
        double struck = -1.0;                  // the chord already struck
        int guard = 0;
        while (!rig.transport.ended() && guard++ < 20000) {
            rig.step(rig.now + rig.frame);
            if (rig.transport.held() && rig.eval.waiting_for_s() != struck) {
                struck = rig.eval.waiting_for_s();
                const double t = static_cast<double>(rig.now) * 1e-9 + 0.010;
                for (int key : down) {
                    if (!lose(rng)) {
                        rig.send(note_off(t, key));
                    }
                }
                down = keys_of(rig.eval.waiting_keys());
                std::shuffle(down.begin(), down.end(), rng);
                for (int key : down) {
                    rig.send(note_on(t + 0.001 + roll(rng), key));
                }
                // The first key of the chord after this one, struck early and let go (or not).
                const auto later = std::find_if(notes.begin(), notes.end(), [&](const N& n) {
                    return n.start > struck + kChordSeconds + 1e-9;
                });
                if (later != notes.end() && peek(rng)) {
                    rig.send(note_on(t + 0.0005, later->pitch));
                    if (!lose(rng)) {
                        rig.send(note_off(t + 0.0008, later->pitch));
                    }
                }
            }
        }
        CHECK(rig.transport.ended());
        const EvaluationSummary& s = rig.eval.summary();
        CHECK(s.counts.played == static_cast<int>(notes.size()));
        CHECK(s.wrong == 0);
    }
}

TEST_CASE("flow: reset_results mid-loop keeps grading, with passes counted from 0 again") {
    FlowRig rig(song_of({{0.5, 60}, {1.5, 62}}), FlowOptions{});
    REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
    rig.transport.set_loop_enabled(0, true);
    rig.play_at(1.0);
    rig.send(note_on(1.5, 60));
    rig.send(note_on(2.5, 62));
    rig.send(note_on(3.5, 60));
    rig.send(note_on(4.5, 62));
    rig.run_until(4.51); // pass 1, its second note still undecided
    rig.eval.reset_results();
    rig.run_until(5.3);
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.notes.size() == 1);
    CHECK(s.notes[0].note_index == 1);
    CHECK(s.notes[0].pass == 0);
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    REQUIRE(passes.size() == 1);
    CHECK(passes[0].pass == 0);
    CHECK(passes[0].graded == 1);
    CHECK_FALSE(passes[0].clean); // it lost the results before the reset
}
