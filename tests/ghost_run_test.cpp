// SPDX-License-Identifier: GPL-3.0-or-later
// Which past run can be the ghost of the run being played (ghost_run.hpp): the same mode, hands and
// transposition, a speed within 10% of now, complete, and the best of those.
#include "etude/ghost_run.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

GhostSetup setup(int speed = 100, int transpose = 0, RunMode mode = RunMode::Flow,
                 HandFilter hands = HandFilter::Both) {
    GhostSetup s;
    s.mode = mode;
    s.hands = hands;
    s.speed_percent = speed;
    s.transpose_semitones = transpose;
    return s;
}

GhostCandidate run(std::int64_t id, GhostSetup s, int hit, int graded = 100, bool complete = true) {
    GhostCandidate c;
    c.run_id = id;
    c.setup = s;
    c.complete = complete;
    c.all = {graded, hit};
    return c;
}

} // namespace

TEST_CASE("a run at the same setup is comparable") {
    CHECK(ghost_comparable(setup(), run(1, setup(), 90)));
}

TEST_CASE("a different transposition is a different exercise, even one semitone") {
    CHECK_FALSE(ghost_comparable(setup(100, 0), run(1, setup(100, 1), 90)));
    CHECK_FALSE(ghost_comparable(setup(100, -2), run(1, setup(100, 0), 90)));
    CHECK(ghost_comparable(setup(100, -2), run(1, setup(100, -2), 90)));
}

TEST_CASE("the mode and the hands must match") {
    CHECK_FALSE(
        ghost_comparable(setup(100, 0, RunMode::Flow), run(1, setup(100, 0, RunMode::Wait), 90)));
    CHECK_FALSE(ghost_comparable(setup(100, 0, RunMode::Flow, HandFilter::Right),
                                 run(1, setup(100, 0, RunMode::Flow, HandFilter::Both), 90)));
    CHECK_FALSE(ghost_comparable(setup(100, 0, RunMode::Flow, HandFilter::Right),
                                 run(1, setup(100, 0, RunMode::Flow, HandFilter::Left), 90)));
    CHECK(ghost_comparable(setup(100, 0, RunMode::Wait, HandFilter::Left),
                           run(1, setup(100, 0, RunMode::Wait, HandFilter::Left), 90)));
}

TEST_CASE("an incomplete run is never comparable") {
    CHECK_FALSE(ghost_comparable(setup(), run(1, setup(), 99, 100, false)));
}

TEST_CASE("no runs, or none comparable, gives no ghost") {
    CHECK_FALSE(best_ghost_run(setup(), {}).has_value());
    const std::vector<GhostCandidate> all{run(1, setup(100, 1), 90), run(2, setup(150), 95),
                                          run(3, setup(), 99, 100, false),
                                          run(4, setup(100, 0, RunMode::Wait), 80)};
    CHECK_FALSE(best_ghost_run(setup(), all).has_value());
}

TEST_CASE("the best comparable run wins; the others do not count however good") {
    const std::vector<GhostCandidate> all{
        run(1, setup(), 80),
        run(2, setup(105), 92),          // comparable, best
        run(3, setup(), 99, 100, false), // incomplete
        run(4, setup(100, 2), 100),      // another key
        run(5, setup(), 91),
    };
    const auto best = best_ghost_run(setup(), all);
    REQUIRE(best.has_value());
    CHECK(*best == 1);
}

TEST_CASE("equal accuracy goes to more graded notes, then to the later run") {
    const std::vector<GhostCandidate> a{run(1, setup(), 45, 50), run(2, setup(), 90, 100)};
    CHECK(*best_ghost_run(setup(), a) == 1); // 90% of 100 notes beats 90% of 50
    const std::vector<GhostCandidate> b{run(7, setup(), 90, 100), run(3, setup(), 90, 100)};
    CHECK(*best_ghost_run(setup(), b) == 0); // the same score: the run saved later
}

TEST_CASE("accuracy is compared exactly, not rounded") {
    // 667 / 1000 against 2 / 3: the first is higher.
    const std::vector<GhostCandidate> c{run(1, setup(), 2, 3), run(2, setup(), 667, 1000)};
    CHECK(*best_ghost_run(setup(), c) == 1);
}

TEST_CASE("a run that graded nothing never wins") {
    const std::vector<GhostCandidate> c{run(1, setup(), 0, 0)};
    CHECK_FALSE(best_ghost_run(setup(), c).has_value());
}
