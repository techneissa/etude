// SPDX-License-Identifier: GPL-3.0-or-later
// How a hand's chord label changes (chord_label_fade.hpp): a fade out and a fade in, no gliding,
// out while the band hangs over the strip, at once with reduced effects.
#include "etude/chord_label_fade.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr std::int64_t kMs = 1'000'000;

// Frames of 10 ms until `ms` have passed.
void run(ChordLabelFade& fade, std::int64_t& now, int index, bool visible, int ms,
         bool reduced = false) {
    for (int i = 0; i < ms / 10; ++i) {
        now += 10 * kMs;
        fade.update(index, visible, now, reduced);
    }
}

float alpha_of(const ChordLabelFade& fade, int index) {
    for (const ChordLabelAlpha& item : fade.items()) {
        if (item.index == index) {
            return item.alpha;
        }
    }
    return 0.0f;
}

} // namespace

TEST_CASE("chord label fade: a first label fades in and stays") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(2, true, now, false);
    CHECK(fade.items().empty()); // the first frame shows nothing yet
    run(fade, now, 2, true, 60);
    const float part = alpha_of(fade, 2);
    CHECK(part > 0.0f);
    CHECK(part < 1.0f);
    run(fade, now, 2, true, 200);
    CHECK(alpha_of(fade, 2) == doctest::Approx(1.0f));
    REQUIRE(fade.items().size() == 1);
}

TEST_CASE("chord label fade: a change fades the old label out while the new one fades in") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, 1, true, 50); // changed to label 1
    REQUIRE(fade.items().size() == 2);
    CHECK(fade.items()[0].index == 0); // leaving first
    CHECK(fade.items()[1].index == 1);
    CHECK(fade.items()[0].alpha < 1.0f);
    CHECK(fade.items()[1].alpha < 1.0f);
    CHECK(fade.items()[0].alpha > fade.items()[1].alpha - 1.0f); // both strictly between 0 and 1
    run(fade, now, 1, true, 200);
    REQUIRE(fade.items().size() == 1);
    CHECK(fade.items()[0].index == 1);
    CHECK(fade.items()[0].alpha == doctest::Approx(1.0f));
}

TEST_CASE("chord label fade: the fade lasts kChordFadeNs") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, 1, true, 10);
    const int frames_to_finish = static_cast<int>(kChordFadeNs / (10 * kMs));
    run(fade, now, 1, true, (frames_to_finish - 1) * 10 - 10);
    CHECK(fade.items().size() == 2); // still crossing a frame before the end
    run(fade, now, 1, true, 30);
    CHECK(fade.items().size() == 1);
}

TEST_CASE("chord label fade: a label that ends fades out and leaves nothing") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, -1, true, 40);
    REQUIRE(fade.items().size() == 1);
    CHECK(fade.items()[0].index == 0);
    CHECK(fade.items()[0].alpha < 1.0f);
    run(fade, now, -1, true, 300);
    CHECK(fade.items().empty());
}

TEST_CASE("chord label fade: while the band hangs over the strip the label fades out, then back") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, 0, false, 40);
    REQUIRE(fade.items().size() == 1);
    CHECK(fade.items()[0].alpha < 1.0f);
    run(fade, now, 0, false, 300);
    CHECK(fade.items().empty());
    run(fade, now, 0, true, 300);
    CHECK(alpha_of(fade, 0) == doctest::Approx(1.0f));
}

TEST_CASE("chord label fade: a change while the band hangs shows nothing until it goes") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, 0, false, 300);
    run(fade, now, 1, false, 300); // the chord changed under the band
    CHECK(fade.items().empty());
    run(fade, now, 1, true, 300);
    REQUIRE(fade.items().size() == 1);
    CHECK(fade.items()[0].index == 1);
}

TEST_CASE("chord label fade: reduced effects switch at once") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, true);
    REQUIRE(fade.items().size() == 1);
    CHECK(fade.items()[0].alpha == doctest::Approx(1.0f));
    now += 10 * kMs;
    fade.update(1, true, now, true);
    REQUIRE(fade.items().size() == 1); // no leaving label
    CHECK(fade.items()[0].index == 1);
    now += 10 * kMs;
    fade.update(1, false, now, true);
    CHECK(fade.items().empty());
}

TEST_CASE("chord label fade: reset forgets the labels, a stall ends a fade and no further") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    fade.reset();
    CHECK(fade.items().empty());
    now += 5'000 * kMs; // a long hitch
    fade.update(3, true, now, false);
    CHECK(fade.items().empty()); // the first frame after a reset starts the fade
    now += 5'000 * kMs;
    fade.update(3, true, now, false);
    CHECK(alpha_of(fade, 3) == doctest::Approx(1.0f)); // one frame ends it
    now -= 10'000 * kMs;                               // a clock that goes back changes nothing
    fade.update(3, true, now, false);
    CHECK(alpha_of(fade, 3) == doctest::Approx(1.0f));
}

TEST_CASE("chord label fade: a change inside another fade lets the earlier name finish its own") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, 1, true, 50); // 0 leaves, 1 arrives
    const float a_before = alpha_of(fade, 0);
    run(fade, now, 2, true, 20); // 1 is cut short: 2 arrives, 0 and 1 both leave
    REQUIRE(fade.items().size() == 3);
    const float a_after = alpha_of(fade, 0);
    CHECK(a_after > 0.0f); // the first name did not pop out
    CHECK(a_after < a_before);
    CHECK(alpha_of(fade, 1) > 0.0f);
    CHECK(alpha_of(fade, 1) < 1.0f);
    // Each fades out smoothly; nothing jumps up or out between frames.
    float last0 = a_after;
    float last1 = alpha_of(fade, 1);
    for (int i = 0; i < 12; ++i) {
        run(fade, now, 2, true, 10);
        const float a0 = alpha_of(fade, 0);
        const float a1 = alpha_of(fade, 1);
        CHECK(a0 <= last0);
        CHECK(a1 <= last1);
        last0 = a0;
        last1 = a1;
    }
    run(fade, now, 2, true, 300);
    REQUIRE(fade.items().size() == 1);
    CHECK(fade.items()[0].index == 2);
    CHECK(fade.items()[0].alpha == doctest::Approx(1.0f));
}

TEST_CASE("chord label fade: with no room for another leaving name the faintest goes") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, 1, true, 30);
    run(fade, now, 2, true, 30);
    run(fade, now, 3, true, 10); // 0, 1 and 2 would be leaving: one too many
    CHECK(fade.items().size() <= kChordMaxLeaving + 1);
    CHECK(alpha_of(fade, 3) > 0.0f);
    bool sorted = true;
    for (const ChordLabelAlpha& item : fade.items()) {
        sorted = sorted && item.alpha > 0.0f && item.alpha <= 1.0f;
    }
    CHECK(sorted);
    // 1 had hardly come in when it was cut short: it is the faintest and the one dropped; the two
    // names that were well on show finish their fades.
    CHECK(alpha_of(fade, 1) == 0.0f);
    CHECK(alpha_of(fade, 0) > 0.0f);
    CHECK(alpha_of(fade, 2) > 0.0f);
}

TEST_CASE("chord label fade: a name that comes back while it fades out is not drawn twice") {
    ChordLabelFade fade;
    std::int64_t now = 0;
    fade.update(0, true, now, false);
    run(fade, now, 0, true, 300);
    run(fade, now, 1, true, 60); // 0 leaves
    const float leaving = alpha_of(fade, 0);
    REQUIRE(leaving > 0.0f);
    run(fade, now, 0, true, 10); // and is back
    int count = 0;
    for (const ChordLabelAlpha& item : fade.items()) {
        count += item.index == 0 ? 1 : 0;
    }
    CHECK(count == 1);
    CHECK(alpha_of(fade, 0) > 0.0f); // it picks up from where it was, not from nothing
}
