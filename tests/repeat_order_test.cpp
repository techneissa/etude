// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/repeat_order.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <vector>

using namespace etude;

namespace {

// Bars named A, B, C, ... so a play order reads as a word: "ABAB C" is "ABABC".
std::vector<BarMarks> bars_of(std::size_t n) { return std::vector<BarMarks>(n); }

std::string word(const RepeatPlan& plan) {
    std::string out;
    for (std::size_t bar : plan.order) {
        out += static_cast<char>('A' + bar);
    }
    return out;
}

constexpr std::uint64_t ending(int n) { return std::uint64_t{1} << n; }

} // namespace

TEST_CASE("repeat order: no signs plays the bars as written") {
    const RepeatPlan plan = plan_repeats(bars_of(4));
    CHECK(word(plan) == "ABCD");
    CHECK(plan.repeats_followed == 0);
    CHECK(plan.fallbacks == 0);
    CHECK_FALSE(plan.capped);
    CHECK(plan_repeats({}).order.empty());
}

TEST_CASE("repeat order: a forward and a backward repeat play the passage twice") {
    auto b = bars_of(4);
    b[1].forward_repeat = true;
    b[2].backward_repeat = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABCBCD");
    CHECK(plan.repeats_followed == 1);
}

TEST_CASE("repeat order: times is the number of plays, and is clamped") {
    auto b = bars_of(3);
    b[0].forward_repeat = true;
    b[1].backward_repeat = true;
    b[1].times = 3;
    CHECK(word(plan_repeats(b)) == "ABABABC");
    b[1].times = 1;
    CHECK(word(plan_repeats(b)) == "ABC");
    b[1].times = 100000;
    const RepeatPlan many = plan_repeats(b);
    CHECK(many.order.size() == 2 * static_cast<std::size_t>(kMaxRepeatTimes) + 1);
    CHECK_FALSE(many.capped);
}

TEST_CASE("repeat order: a backward repeat with no forward goes to the start") {
    auto b = bars_of(3);
    b[1].backward_repeat = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABABC");
    CHECK(plan.fallbacks == 0);
}

TEST_CASE("repeat order: a second unopened repeat goes back to the bar after the first") {
    auto b = bars_of(4);
    b[0].backward_repeat = true;
    b[2].backward_repeat = true;
    CHECK(word(plan_repeats(b)) == "AABCBCD");
}

TEST_CASE("repeat order: a forward repeat that nothing closes is left out and counted") {
    auto b = bars_of(3);
    b[1].forward_repeat = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABC");
    CHECK(plan.fallbacks == 1);
}

TEST_CASE("repeat order: back-to-back repeats each play their own passage") {
    auto b = bars_of(5);
    b[0].forward_repeat = true;
    b[1].backward_repeat = true;
    b[2].forward_repeat = true;
    b[3].backward_repeat = true;
    CHECK(word(plan_repeats(b)) == "ABABCDCDE");
}

TEST_CASE("repeat order: a one-bar repeat") {
    auto b = bars_of(3);
    b[1].forward_repeat = true;
    b[1].backward_repeat = true;
    CHECK(word(plan_repeats(b)) == "ABBC");
}

TEST_CASE("repeat order: nested repeats play the inner passage in full on each outer pass") {
    auto b = bars_of(5);
    b[0].forward_repeat = true;
    b[1].forward_repeat = true;
    b[2].backward_repeat = true;
    b[3].backward_repeat = true;
    CHECK(word(plan_repeats(b)) == "ABCBCDABCBCDE");
}

TEST_CASE("repeat order: first and second endings") {
    // |: A B |1. C :| 2. D  E
    auto b = bars_of(5);
    b[0].forward_repeat = true;
    b[2].endings = ending(1);
    b[2].ending_stops = true;
    b[2].backward_repeat = true;
    b[3].endings = ending(2);
    b[3].ending_stops = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABCABDE");
    CHECK(plan.repeats_followed == 1);
    CHECK(plan.fallbacks == 0);
}

TEST_CASE("repeat order: an ending that spans several bars") {
    // |: A |1. B C :| 2. D E  F
    auto b = bars_of(6);
    b[0].forward_repeat = true;
    b[1].endings = ending(1);
    b[2].ending_stops = true;
    b[2].backward_repeat = true;
    b[3].endings = ending(2);
    b[4].ending_stops = true;
    CHECK(word(plan_repeats(b)) == "ABCADEF");
}

TEST_CASE("repeat order: an ending with no closing mark ends at its repeat or the next ending") {
    auto b = bars_of(5);
    b[0].forward_repeat = true;
    b[2].endings = ending(1);
    b[2].backward_repeat = true; // closes the bracket
    b[3].endings = ending(2);    // never closed: runs to the end
    CHECK(word(plan_repeats(b)) == "ABCABDE");
}

TEST_CASE("repeat order: three endings, each closed by its own repeat sign") {
    // |: A |1. B :| 2. C :| 3. D  E
    auto b = bars_of(5);
    b[0].forward_repeat = true;
    b[1].endings = ending(1);
    b[1].ending_stops = true;
    b[1].backward_repeat = true;
    b[2].endings = ending(2);
    b[2].ending_stops = true;
    b[2].backward_repeat = true;
    b[3].endings = ending(3);
    b[3].ending_stops = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABACADE");
    CHECK(plan.repeats_followed == 2);
}

TEST_CASE("repeat order: one ending for several passes, and times") {
    // |: A |1,2. B :|x3  3. C  D
    auto b = bars_of(4);
    b[0].forward_repeat = true;
    b[1].endings = ending(1) | ending(2);
    b[1].ending_stops = true;
    b[1].backward_repeat = true;
    b[1].times = 3;
    b[2].endings = ending(3);
    b[2].ending_stops = true;
    CHECK(word(plan_repeats(b)) == "ABABACD");
}

TEST_CASE("repeat order: a repeat with one ending and nothing for the other pass") {
    // |: A |1. B :| C
    auto b = bars_of(3);
    b[0].forward_repeat = true;
    b[1].endings = ending(1);
    b[1].ending_stops = true;
    b[1].backward_repeat = true;
    CHECK(word(plan_repeats(b)) == "ABAC");
}

TEST_CASE("repeat order: a volta inside a repeated passage starts again on each outer pass") {
    // |: A |: B |1. C :| 2. D  E :| F
    auto b = bars_of(6);
    b[0].forward_repeat = true;
    b[1].forward_repeat = true;
    b[2].endings = ending(1);
    b[2].ending_stops = true;
    b[2].backward_repeat = true;
    b[3].endings = ending(2);
    b[3].ending_stops = true;
    b[4].backward_repeat = true;
    CHECK(word(plan_repeats(b)) == "ABCBDEABCBDEF");
}

TEST_CASE(
    "repeat order: nested repeats around endings pair each backward repeat with its own forward") {
    // |: A |: B 1.C :| 2.D :| E: the inner repeat has the endings, the outer one closes after them.
    auto b = bars_of(5);
    b[0].forward_repeat = true;
    b[1].forward_repeat = true;
    b[2].endings = ending(1);
    b[2].ending_stops = true;
    b[2].backward_repeat = true;
    b[3].endings = ending(2);
    b[3].ending_stops = true;
    b[3].backward_repeat = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABCBDABCBDE");
    CHECK(plan.fallbacks == 0);
    CHECK(plan.repeats_implied == 0);
}

TEST_CASE("repeat order: three levels of nesting") {
    // |: A |: B |: C D :| E :| F :| G
    auto b = bars_of(7);
    b[0].forward_repeat = true;
    b[1].forward_repeat = true;
    b[2].forward_repeat = true;
    b[3].backward_repeat = true;
    b[4].backward_repeat = true;
    b[5].backward_repeat = true;
    // Inner: C D C D. Middle: B (C D C D) E, twice. Outer: A (middle) F, twice.
    const std::string middle = "B"
                               "CDCD"
                               "E";
    const std::string outer = "A" + middle + middle + "F";
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == outer + outer + "G");
    CHECK(plan.fallbacks == 0);
}

TEST_CASE("repeat order: a volta inside a deeper nest keeps its passes apart") {
    // |: A |: B |: C 1.D :| 2.E :| F :| G
    auto b = bars_of(7);
    b[0].forward_repeat = true;
    b[1].forward_repeat = true;
    b[2].forward_repeat = true;
    b[3].endings = ending(1);
    b[3].ending_stops = true;
    b[3].backward_repeat = true;
    b[4].endings = ending(2);
    b[4].ending_stops = true;
    b[4].backward_repeat = true;
    b[5].backward_repeat = true;
    // D pairs with the forward at C, E with the one at B, F with A. The middle passage is
    // B C D C E, played twice with the volta starting again each time.
    const std::string middle = "BCDCE";
    const std::string outer = "A" + middle + middle + "F";
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == outer + outer + "G");
    CHECK(plan.fallbacks == 0);
}

TEST_CASE("repeat order: a first ending with no backward repeat gets one implied") {
    // |: A 1.B 2.C :| D
    auto b = bars_of(4);
    b[0].forward_repeat = true;
    b[1].endings = ending(1);
    b[1].ending_stops = true;
    b[2].endings = ending(2);
    b[2].ending_stops = true;
    b[2].backward_repeat = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABACD");
    CHECK(plan.repeats_implied == 1);
    CHECK(plan.fallbacks == 0);
    // With no written backward repeat but a forward repeat still open, the same.
    b[2].backward_repeat = false;
    const RepeatPlan open = plan_repeats(b);
    CHECK(word(open) == "ABACD");
    CHECK(open.repeats_implied == 1);
    // With no forward repeat either the endings are dropped (written backward repeat aside).
    b[0].forward_repeat = false;
    const RepeatPlan none = plan_repeats(b);
    CHECK(word(none) == "ABCD");
    CHECK(none.repeats_implied == 0);
    CHECK(none.fallbacks == 1);
}

TEST_CASE("repeat order: with no forward repeat an implied ending goes back to the start") {
    // A 1.B 2.C :| D, the sign written after the last ending, no forward repeat.
    auto b = bars_of(4);
    b[1].endings = ending(1);
    b[1].ending_stops = true;
    b[2].endings = ending(2);
    b[2].ending_stops = true;
    b[2].backward_repeat = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABACD");
    CHECK(plan.repeats_implied == 1);
}

TEST_CASE("repeat order: endings with no repeat in them play straight through") {
    auto b = bars_of(4);
    b[1].endings = ending(1);
    b[1].ending_stops = true;
    b[2].endings = ending(2);
    b[2].ending_stops = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABCD");
    CHECK(plan.fallbacks == 1);
}

TEST_CASE("repeat order: a closing bracket that was never opened is counted and changes nothing") {
    auto b = bars_of(3);
    b[1].ending_stops = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABC");
    CHECK(plan.fallbacks == 1);
}

TEST_CASE("repeat order: D.C. goes back to the start once") {
    auto b = bars_of(3);
    b[2].dacapo = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABCABC");
    CHECK(plan.repeats_followed == 1);
}

TEST_CASE("repeat order: D.C. al Fine stops at the Fine") {
    auto b = bars_of(4);
    b[1].fine = true;
    b[3].dacapo = true;
    CHECK(word(plan_repeats(b)) == "ABCDAB");
}

TEST_CASE("repeat order: D.S. al Coda jumps from the To Coda to the coda") {
    // A(segno) B(To Coda) C(D.S.) D(coda) E
    auto b = bars_of(5);
    b[0].segno = true;
    b[1].tocoda = true;
    b[2].dalsegno = true;
    b[3].coda = true;
    CHECK(word(plan_repeats(b)) == "ABCABDE");
}

TEST_CASE("repeat order: D.S. al Fine goes to its segno") {
    // A B(segno) C(Fine) D(D.S.)
    auto b = bars_of(4);
    b[1].segno = true;
    b[2].fine = true;
    b[3].dalsegno = true;
    CHECK(word(plan_repeats(b)) == "ABCDBC");
}

TEST_CASE("repeat order: labels pick the segno and the coda") {
    // A(segno x) B(segno y) C(D.S. y) D(To Coda q) E(coda p) F(coda q)
    auto b = bars_of(6);
    b[0].segno = true;
    b[0].segno_label = "x";
    b[1].segno = true;
    b[1].segno_label = "y";
    b[2].dalsegno = true;
    b[2].dalsegno_label = "y";
    b[3].tocoda = true;
    b[3].tocoda_label = "q";
    b[4].coda = true;
    b[4].coda_label = "p";
    b[5].coda = true;
    b[5].coda_label = "q";
    CHECK(word(plan_repeats(b)) == "ABCBCDF");
}

TEST_CASE("repeat order: Fine and To Coda do nothing before a jump") {
    auto b = bars_of(4);
    b[1].fine = true;
    b[2].tocoda = true;
    b[3].coda = true;
    CHECK(word(plan_repeats(b)) == "ABCD");
}

TEST_CASE("repeat order: repeat barlines are not taken again after a D.C.") {
    // |: A B :| C(D.C.)
    auto b = bars_of(3);
    b[0].forward_repeat = true;
    b[1].backward_repeat = true;
    b[2].dacapo = true;
    CHECK(word(plan_repeats(b)) == "ABABCABC");
}

TEST_CASE("repeat order: after a D.C. a volta plays its last ending") {
    // |: A |1. B :| 2. C  D(D.C.)
    auto b = bars_of(4);
    b[0].forward_repeat = true;
    b[1].endings = ending(1);
    b[1].ending_stops = true;
    b[1].backward_repeat = true;
    b[2].endings = ending(2);
    b[2].ending_stops = true;
    b[3].dacapo = true;
    CHECK(word(plan_repeats(b)) == "ABACDACD");
}

TEST_CASE("repeat order: a repeat and a D.C. on one bar repeat first") {
    auto b = bars_of(2);
    b[0].backward_repeat = true;
    b[0].dacapo = true;
    CHECK(word(plan_repeats(b)) == "AAAB");
}

TEST_CASE("repeat order: jumps are taken once, so jump loops end") {
    // A(segno) B(D.S.) C(D.S.): the second D.S. is ignored.
    auto b = bars_of(3);
    b[0].segno = true;
    b[1].dalsegno = true;
    b[2].dalsegno = true;
    CHECK(word(plan_repeats(b)) == "ABABC");
    // A D.C. on the first bar goes back to itself once.
    auto c = bars_of(2);
    c[0].dacapo = true;
    CHECK(word(plan_repeats(c)) == "AAB");
}

TEST_CASE(
    "repeat order: a D.S. to an unknown label goes to the first segno, and to none is dropped") {
    auto b = bars_of(3);
    b[0].segno = true;
    b[0].segno_label = "x";
    b[2].dalsegno = true;
    b[2].dalsegno_label = "nowhere";
    CHECK(word(plan_repeats(b)) == "ABCABC");
    auto c = bars_of(3);
    c[2].dalsegno = true;
    const RepeatPlan plan = plan_repeats(c);
    CHECK(word(plan) == "ABC");
    CHECK(plan.fallbacks == 1);
}

TEST_CASE("repeat order: a To Coda with no coda is dropped") {
    auto b = bars_of(3);
    b[1].tocoda = true;
    b[2].dacapo = true;
    const RepeatPlan plan = plan_repeats(b);
    CHECK(word(plan) == "ABCABC");
    CHECK(plan.fallbacks == 1);
}

TEST_CASE("repeat order: an expansion past the cap falls back to the written order") {
    // Three nested repeats of 16 plays each would be thousands of bars.
    auto b = bars_of(7);
    b[0].forward_repeat = true;
    b[1].forward_repeat = true;
    b[2].forward_repeat = true;
    b[4].backward_repeat = true;
    b[5].backward_repeat = true;
    b[6].backward_repeat = true;
    for (BarMarks& m : b) {
        m.times = 16;
    }
    const RepeatPlan plan = plan_repeats(b);
    CHECK(plan.capped);
    CHECK(word(plan) == "ABCDEFG");
    CHECK(plan.repeats_followed == 0);
}

TEST_CASE("repeat order: the cap is kMaxRepeatExpansion times the written bars, or max_bars") {
    auto b = bars_of(2);
    b[0].forward_repeat = true;
    b[1].backward_repeat = true;
    b[1].times = kMaxRepeatTimes;
    // 2 bars played 16 times is 32 = 16 x 2: just inside.
    const RepeatPlan inside = plan_repeats(b);
    CHECK_FALSE(inside.capped);
    CHECK(inside.order.size() == 32);
    CHECK(plan_repeats(b, 31).capped);
    CHECK_FALSE(plan_repeats(b, 32).capped);
}

TEST_CASE("repeat order: ending numbers are read from lists and ranges") {
    CHECK(parse_ending_numbers("1") == ending(1));
    CHECK(parse_ending_numbers("1, 2") == (ending(1) | ending(2)));
    CHECK(parse_ending_numbers("1,2,3") == (ending(1) | ending(2) | ending(3)));
    CHECK(parse_ending_numbers("1-3") == (ending(1) | ending(2) | ending(3)));
    CHECK(parse_ending_numbers("2 \xE2\x80\x93 3") == (ending(2) | ending(3)));
    CHECK(parse_ending_numbers("") == 0);
    CHECK(parse_ending_numbers(nullptr) == 0);
    CHECK(parse_ending_numbers("x") == 0);
    CHECK(parse_ending_numbers("0") == 0);
    CHECK(parse_ending_numbers("64") == 0);
    CHECK(parse_ending_numbers("63") == ending(63));
    CHECK(parse_ending_numbers("1-99999999999") != 0); // a huge range is clamped, not looped
}

TEST_CASE("repeat order: marks from several parts merge") {
    BarMarks a;
    a.forward_repeat = true;
    a.times = 2;
    a.segno = true;
    a.segno_label = "s";
    BarMarks b;
    b.backward_repeat = true;
    b.times = 3;
    b.endings = ending(1);
    b.segno_label = "other";
    merge_bar_marks(a, b);
    CHECK(a.forward_repeat);
    CHECK(a.backward_repeat);
    CHECK(a.times == 3);
    CHECK(a.endings == ending(1));
    CHECK(a.segno_label == "s");
    CHECK_FALSE(BarMarks{}.any());
    CHECK(a.any());
}
