// SPDX-License-Identifier: GPL-3.0-or-later
// The score of a run: the formula's boundaries (run_score.hpp).
#include "etude/run_score.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using namespace etude;

namespace {

// A run of `n` notes, every one perfect and in one unbroken combo, the two hands sharing them.
RunRecord perfect_run(int n = 100, int speed = 100, HandFilter hands = HandFilter::Both) {
    RunRecord r;
    r.song_hash = 7;
    r.speed_percent = speed;
    r.hands = hands;
    r.counts.perfect = n;
    r.longest_combo = n;
    r.all = {n, n};
    if (hands == HandFilter::Both) {
        r.left = {n / 2, n / 2};
        r.right = {n - n / 2, n - n / 2};
    } else if (hands == HandFilter::Left) {
        r.left = r.all;
    } else {
        r.right = r.all;
    }
    return r;
}

} // namespace

TEST_CASE("score: every note perfect at full speed on both hands is 1000") {
    const ScoreParts p = score_parts(perfect_run());
    CHECK(p.score == 1000);
    CHECK(p.credit == doctest::Approx(1.0));
    CHECK(p.flow == doctest::Approx(1.0));
    CHECK(p.balance == doctest::Approx(1.0));
    CHECK(p.coverage == doctest::Approx(1.0));
}

TEST_CASE("score: slower earns less but never nothing") {
    CHECK(score_run(perfect_run(100, 50)) == 625);
    CHECK(score_run(perfect_run(100, 75)) == 813); // 812.5 rounds up
    CHECK(score_run(perfect_run(100, 25)) == 438); // 437.5 rounds up
    int last = 0;
    for (int speed = 25; speed <= 200; speed += 5) {
        const int score = score_run(perfect_run(100, speed));
        CHECK(score > 0);
        CHECK(score >= last); // never less for a faster run of the same playing
        last = score;
    }
    CHECK(speed_factor(25) == doctest::Approx(0.4375));
    CHECK(speed_factor(100) == doctest::Approx(1.0));
    CHECK(speed_factor(150) == doctest::Approx(1.1));
}

TEST_CASE("score: faster than the song earns a bonus, to 200%") {
    CHECK(score_run(perfect_run(100, 125)) == 1050);
    CHECK(score_run(perfect_run(100, 200)) == 1200);
    CHECK(speed_factor(500) == doctest::Approx(1.2)); // a speed past the range counts as 200%
    CHECK(speed_factor(0) == doctest::Approx(0.4375)); // and one below as 25%
}

TEST_CASE("score: all missed is nothing, and so is a run that graded no note") {
    RunRecord missed;
    missed.song_hash = 7;
    missed.counts.missed = 100;
    missed.all = {100, 0};
    missed.left = {50, 0};
    missed.right = {50, 0};
    CHECK(score_run(missed) == 0);
    CHECK(score_run(RunRecord{}) == 0);
    CHECK(scored(RunRecord{}, {}).covered_percent == 0);
}

TEST_CASE("score: one hand only earns 0.8 of the same playing on both hands") {
    CHECK(score_run(perfect_run(100, 100, HandFilter::Right)) == 800);
    CHECK(score_run(perfect_run(100, 100, HandFilter::Left)) == 800);
    CHECK(score_run(perfect_run(100, 50, HandFilter::Right)) == 500);
    // Its accuracy is the hand's own: half the notes missed on the one hand is not 100%.
    RunRecord r = perfect_run(100, 100, HandFilter::Right);
    r.counts.perfect = 50;
    r.counts.missed = 50;
    r.longest_combo = 50;
    r.all = {100, 50};
    r.right = r.all;
    CHECK(score_run(r) == 400); // 1000 x (0.7 x 0.5 + 0.2 x 0.5 + 0.1 x 0.5) x 0.8
    CHECK(score_run(r) < score_run(perfect_run(100, 100, HandFilter::Right)));
}

TEST_CASE("score: grades are worth what they say") {
    RunRecord r = perfect_run();
    r.counts = {.perfect = 50, .good = 50};
    CHECK(score_parts(r).credit == doctest::Approx(0.9));
    CHECK(score_run(r) == 930); // 1000 x (0.7 x 0.9 + 0.2 + 0.1)
    r.counts = {.perfect = 0, .good = 0, .early = 30, .late = 70};
    CHECK(score_parts(r).credit == doctest::Approx(0.6));
    r.counts = {.good = 100};
    CHECK(score_run(r) == 860);
    CHECK(score_run(perfect_run()) > score_run(r));
}

TEST_CASE("score: Wait Mode grades no timing, so it tops out under Flow Mode") {
    RunRecord r = perfect_run();
    r.mode = RunMode::Wait;
    r.counts = {.played = 100};
    CHECK(score_run(r) == 895); // 1000 x (0.7 x 0.85 + 0.2 + 0.1)
    CHECK(score_run(r) < score_run(perfect_run()));
    CHECK(score_run(r) > 0);
}

TEST_CASE("score: a wrong press costs a quarter of a note, never below nothing") {
    RunRecord r = perfect_run();
    r.wrong = 20;
    CHECK(score_parts(r).credit == doctest::Approx(0.95));
    CHECK(score_run(r) == 965);
    r.wrong = 5000;
    CHECK(score_parts(r).credit == doctest::Approx(0.0));
    CHECK(score_run(r) == 300); // the combo and the accuracy still count: 1000 x (0.2 + 0.1)
}

TEST_CASE("score: the combo counts, a broken run earns less than an unbroken one") {
    RunRecord whole = perfect_run();
    RunRecord broken = perfect_run();
    broken.longest_combo = 30;
    CHECK(score_parts(broken).flow == doctest::Approx(0.3));
    CHECK(score_run(broken) == 860); // 1000 x (0.7 + 0.2 x 0.3 + 0.1)
    CHECK(score_run(broken) < score_run(whole));
    broken.longest_combo = 5000; // more than the notes is no more than all of them
    CHECK(score_run(broken) == 1000);
}

TEST_CASE("score: the weaker hand counts when both were played") {
    RunRecord balanced = perfect_run();
    balanced.counts = {.perfect = 75, .missed = 25};
    balanced.longest_combo = 50;
    balanced.all = {100, 75};
    balanced.left = {50, 38};
    balanced.right = {50, 37};
    RunRecord lopsided = balanced;
    lopsided.left = {50, 50};
    lopsided.right = {50, 25};
    CHECK(score_parts(lopsided).balance == doctest::Approx(0.5));
    CHECK(score_parts(balanced).balance == doctest::Approx(0.74));
    CHECK(score_run(balanced) > score_run(lopsided));
}

TEST_CASE("score: the song's rating moves it between 0.8 and 1.2") {
    const RunRecord r = perfect_run();
    CHECK(score_run(r, {.difficulty_half_stars = 0}) == 1000); // not rated
    CHECK(score_run(r, {.difficulty_half_stars = 2}) == 800);  // one star
    CHECK(score_run(r, {.difficulty_half_stars = 6}) == 1000); // three stars
    CHECK(score_run(r, {.difficulty_half_stars = 7}) == 1050);
    CHECK(score_run(r, {.difficulty_half_stars = 10}) == 1200);
    CHECK(score_run(r, {.difficulty_half_stars = 1}) == 800);   // below one star counts as one
    CHECK(score_run(r, {.difficulty_half_stars = 99}) == 1200); // above five as five
}

TEST_CASE("score: the most a run can earn is the top of the range") {
    const RunRecord best = perfect_run(100, 200);
    CHECK(score_run(best, {.difficulty_half_stars = 10}) == kMaxRunScore);
    CHECK(kMaxRunScore == 1440);
}

TEST_CASE("score: a run stopped half way earns part of a pass") {
    RunRecord half = perfect_run(50); // 50 of the song's 100 notes, all perfect
    for (int bar = 1; bar <= 10; ++bar) {
        half.measures.push_back({bar, 5, 0, 0});
    }
    const ScoreContext song{.song_notes = 100, .song_bars = 20};
    const ScoreParts p = score_parts(half, song);
    CHECK(p.coverage == doctest::Approx(0.5));
    CHECK(p.score == 600); // 1000 x (0.2 + 0.8 x 0.5)
    CHECK(scored(half, song).covered_percent == 50);
    CHECK(scored(half, song).score == 600);
    // The whole song is the whole score.
    RunRecord whole = perfect_run(100);
    for (int bar = 1; bar <= 20; ++bar) {
        whole.measures.push_back({bar, 5, 0, 0});
    }
    CHECK(score_run(whole, song) == 1000);
    CHECK(scored(whole, song).covered_percent == 100);
}

TEST_CASE("score: a loop cannot be repeated into a whole pass") {
    // Four bars of a 20-bar, 100-note song, 20 times over: 400 notes graded, all perfect.
    RunRecord loop = perfect_run(400);
    for (int bar = 1; bar <= 4; ++bar) {
        loop.measures.push_back({bar, 100, 0, 0});
    }
    loop.clean_passes = 20;
    const ScoreContext song{.song_notes = 100, .song_bars = 20};
    const ScoreParts p = score_parts(loop, song);
    CHECK(p.coverage == doctest::Approx(0.2)); // the bars, not the notes graded
    CHECK(p.score == 360);                     // 1000 x (0.2 + 0.8 x 0.2)
    CHECK(p.score < score_run(perfect_run(100), song));
    CHECK(p.score > 0);
    CHECK(scored(loop, song).covered_percent == 20);
}

TEST_CASE("score: a record scored once keeps its coverage when scored again without the song") {
    RunRecord loop = perfect_run(400);
    for (int bar = 1; bar <= 4; ++bar) {
        loop.measures.push_back({bar, 100, 0, 0});
    }
    const RunRecord kept = scored(loop, {.song_notes = 100, .song_bars = 20});
    CHECK(score_run(kept) == kept.score); // the stored coverage stands in for the song
    // A record from before scores (no coverage) counts as a whole pass.
    CHECK(score_run(loop) == 1000);
}

TEST_CASE("score: the score is a whole number inside its range for any record") {
    RunRecord r;
    r.counts = {.perfect = 2'000'000'000, .good = 2'000'000'000};
    r.wrong = -5;
    r.all = {10, 10};
    r.longest_combo = -1;
    const int s = score_run(r);
    CHECK(s >= 0);
    CHECK(s <= kMaxRunScore);
    CHECK(sanitized(scored(perfect_run(), {})).score == 1000);
}
