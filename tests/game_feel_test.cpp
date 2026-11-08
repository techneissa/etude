// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/game_feel.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {
constexpr std::int64_t kMs = 1'000'000;

// Feeds `n` presses of `error_ms` each; returns the last add_press result.
std::optional<TimingDirection> feed(GameFeel& g, int n, std::int64_t error_ms) {
    std::optional<TimingDirection> r;
    for (int i = 0; i < n; ++i) {
        r = g.add_press(error_ms * kMs);
    }
    return r;
}
} // namespace

TEST_CASE("suggestion: exactly 25 ms late is not beyond, 26 ms is") {
    GameFeel a;
    CHECK_FALSE(feed(a, 40, 25).has_value());
    CHECK_FALSE(a.calibration_suggestion().has_value());
    GameFeel b;
    auto r = feed(b, kCalibrationMinPresses, 26);
    REQUIRE(r.has_value());
    CHECK(*r == TimingDirection::Late);
}

TEST_CASE("suggestion: early direction, and exactly -25 ms is not beyond") {
    GameFeel a;
    CHECK_FALSE(feed(a, 40, -25).has_value());
    GameFeel b;
    auto r = feed(b, kCalibrationMinPresses, -26);
    REQUIRE(r.has_value());
    CHECK(*r == TimingDirection::Early);
}

TEST_CASE("suggestion: fewer than the minimum presses never fires") {
    GameFeel g;
    CHECK_FALSE(feed(g, kCalibrationMinPresses - 1, 100).has_value());
    CHECK_FALSE(g.calibration_suggestion().has_value());
    CHECK(feed(g, 1, 100).has_value()); // the 30th press does
}

TEST_CASE("suggestion: fires once per run, then again after a reset") {
    GameFeel g;
    CHECK(feed(g, kCalibrationMinPresses, 50).has_value());
    CHECK_FALSE(feed(g, 5, 50).has_value());
    CHECK(g.calibration_suggestion() == TimingDirection::Late);
    g.reset();
    CHECK_FALSE(g.calibration_suggestion().has_value());
    CHECK(g.presses() == 0);
    CHECK(feed(g, kCalibrationMinPresses, -50) == TimingDirection::Early);
}

TEST_CASE("suggestion: a mean that crosses back inside 25 ms does not fire") {
    GameFeel g;
    // 29 presses at +60, then one at -1000: the mean is far inside, at the 30th press.
    CHECK_FALSE(feed(g, 29, 60).has_value());
    CHECK_FALSE(g.add_press(-1000 * kMs).has_value());
    // Still inside afterwards while the mean stays there.
    CHECK_FALSE(feed(g, 2, 0).has_value());
}

TEST_CASE("suggestion: a mean that crosses out after 30 presses fires then") {
    GameFeel g;
    CHECK_FALSE(feed(g, 30, 0).has_value());
    // Each +400 ms press lifts the mean; fires when sum > 25 ms * count.
    std::optional<TimingDirection> r;
    int n = 0;
    while (!r && n < 10) {
        r = g.add_press(400 * kMs);
        ++n;
    }
    REQUIRE(r.has_value());
    CHECK(*r == TimingDirection::Late);
    CHECK(n == 3); // two presses sum 800 = 25 ms x 32: not beyond; the third does
}

TEST_CASE("run mean covers every press, not only the last 20") {
    GameFeel g;
    CHECK_FALSE(g.run_mean_ns().has_value());
    feed(g, 30, 10);
    feed(g, 20, 20);
    CHECK(g.presses() == 50);
    CHECK(*g.run_mean_ns() == 14 * kMs);
}

TEST_CASE("combo: 9 to 10 pulses, 10 to 11 does not") {
    CHECK(combo_event(9, 10).kind == ComboEventKind::Milestone);
    CHECK(combo_event(9, 10).value == 10);
    CHECK(combo_event(10, 11).kind == ComboEventKind::None);
    CHECK(combo_event(0, 1).kind == ComboEventKind::None);
}

TEST_CASE("combo: 24 to 25, 49 to 50 and 99 to 100 pulse") {
    CHECK(combo_event(24, 25).value == 25);
    CHECK(combo_event(49, 50).value == 50);
    CHECK(combo_event(99, 100).kind == ComboEventKind::Milestone);
    CHECK(combo_event(99, 100).value == 100);
}

TEST_CASE("combo: every 100 after the last fixed milestone pulses") {
    CHECK(combo_event(199, 200).value == 200);
    CHECK(combo_event(299, 300).value == 300);
    CHECK(combo_event(100, 101).kind == ComboEventKind::None);
    CHECK(combo_event(150, 199).kind == ComboEventKind::None);
}

TEST_CASE("combo: a jump over several milestones reports the highest") {
    CHECK(combo_event(8, 30).value == 25);
    CHECK(combo_event(99, 205).value == 200);
}

TEST_CASE("combo: 10 or more falling to 0 breaks, 5 to 0 does not") {
    const ComboEvent b = combo_event(100, 0);
    CHECK(b.kind == ComboEventKind::Break);
    CHECK(b.value == 100);
    CHECK(combo_event(10, 0).kind == ComboEventKind::Break);
    CHECK(combo_event(9, 0).kind == ComboEventKind::None);
    CHECK(combo_event(5, 0).kind == ComboEventKind::None);
    CHECK(combo_event(0, 0).kind == ComboEventKind::None);
}

TEST_CASE("game feel tracks the previous combo itself, and reset keeps a combo in force") {
    GameFeel g;
    CHECK(g.update_combo(9).milestone.kind == ComboEventKind::None);
    CHECK(g.update_combo(10).milestone.kind == ComboEventKind::Milestone);
    const int lost[] = {10};
    const ComboEvents e = g.update_combo(0, lost);
    CHECK(e.loss.kind == ComboEventKind::Break);
    CHECK(e.loss.value == 10);
    g.reset(50);
    CHECK(g.update_combo(50).milestone.kind == ComboEventKind::None);
}

TEST_CASE("a combo lost and raised again inside one frame is still a Break") {
    GameFeel g;
    g.update_combo(12);
    // A Miss and a Perfect decided in one update: 12 -> 0 -> 1, so the combo reads 1.
    const int lost[] = {12};
    const ComboEvents e = g.update_combo(1, lost);
    CHECK(e.loss.kind == ComboEventKind::Break);
    CHECK(e.loss.value == 12);
    CHECK(e.milestone.kind == ComboEventKind::None);
}

TEST_CASE("a lost combo under 10 is no Break, and the largest of several is reported") {
    GameFeel g;
    const int small[] = {5};
    CHECK(g.update_combo(0, small).loss.kind == ComboEventKind::None);
    const int two[] = {11, 30};
    CHECK(g.update_combo(2, two).loss.value == 30);
}

TEST_CASE("a Break and a Milestone in one frame are both reported") {
    GameFeel g;
    g.update_combo(12);
    const int lost[] = {12};
    // Lost 12, then climbed to 25 in the same frame.
    const ComboEvents e = g.update_combo(25, lost);
    CHECK(e.loss.kind == ComboEventKind::Break);
    CHECK(e.milestone.kind == ComboEventKind::Milestone);
    CHECK(e.milestone.value == 25);
}

TEST_CASE("rebase_combo keeps the run's count, mean and suggestion") {
    GameFeel g;
    CHECK(feed(g, kCalibrationMinPresses, 50).has_value());
    g.update_combo(40);
    g.rebase_combo(40);
    CHECK(g.combo() == 40);
    CHECK(g.presses() == kCalibrationMinPresses);
    CHECK(*g.run_mean_ns() == 50 * kMs);
    CHECK(g.calibration_suggestion() == TimingDirection::Late);
    // 30 more presses after the seek: no second suggestion.
    CHECK_FALSE(feed(g, kCalibrationMinPresses, 50).has_value());
}

TEST_CASE("combo() is the combo of the last update, and 0 after a full reset") {
    GameFeel g;
    CHECK(g.combo() == 0);
    g.update_combo(7);
    CHECK(g.combo() == 7);
    g.reset();
    CHECK(g.combo() == 0);
}

