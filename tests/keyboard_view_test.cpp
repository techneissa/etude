// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/keyboard_view.hpp"

#include "etude/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr float kPpmm = 96.0f / 25.4f; // 3.78 px per mm: a 96 dpi display

int whites_in(const KeyRange& r) { return white_rank(r.highest) - white_rank(r.lowest) + 1; }

bool white_edges(const KeyRange& r) { return !is_black_key(r.lowest) && !is_black_key(r.highest); }

bool inside(const KeyRange& r, const KeyRange& outer) {
    return r.lowest >= outer.lowest && r.highest <= outer.highest;
}

KeyViewParams params(KeyRange keyboard, float width, KeySize size) {
    KeyViewParams p;
    p.keyboard = keyboard;
    p.width_px = width;
    p.px_per_mm = kPpmm;
    p.size = size;
    return p;
}

// A note is fully shown when the white key it sits on, and for a black key the one above too, are
// inside the window.
bool fully_visible(const KeyRange& r, int note) {
    const int rank = white_rank(note);
    const int top = is_black_key(note) ? rank + 1 : rank;
    return rank >= white_rank(r.lowest) && top <= white_rank(r.highest);
}

} // namespace

TEST_CASE("white rank counts white keys from note 0 and inverts") {
    CHECK(white_rank(0) == 0);
    CHECK(white_rank(2) == 1);
    CHECK(white_rank(11) == 6);
    CHECK(white_rank(12) == 7);
    CHECK(white_rank(60) == 35);
    CHECK(white_rank(61) == 35); // C#4 sits on the rank of C4
    CHECK(white_rank(127) == 74);
    for (int rank = 0; rank <= 74; ++rank) {
        const int note = note_of_rank(rank);
        REQUIRE(note >= 0);
        CHECK_FALSE(is_black_key(note));
        CHECK(white_rank(note) == rank);
    }
    CHECK(note_of_rank(-1) == -1);
}

TEST_CASE("fixed key widths are 12, 16, 20 and 24 mm, the fit modes have none") {
    CHECK(key_size_white_mm(KeySize::Small) == doctest::Approx(12.0f));
    CHECK(key_size_white_mm(KeySize::Medium) == doctest::Approx(16.0f));
    CHECK(key_size_white_mm(KeySize::Large) == doctest::Approx(20.0f));
    CHECK(key_size_white_mm(KeySize::ExtraLarge) == doctest::Approx(24.0f));
    CHECK(key_size_white_mm(KeySize::FitKeyboard) == 0.0f);
    CHECK(key_size_white_mm(KeySize::FitSong) == 0.0f);
}

TEST_CASE("fit keyboard shows every key of 49, 61 and 88 key keyboards") {
    for (const KeyRange kb : {kRange49, kRange61, kRange88}) {
        const KeyViewTarget t = compute_key_view(params(kb, 1280.0f, KeySize::FitKeyboard));
        CHECK(t.range.lowest == kb.lowest);
        CHECK(t.range.highest == kb.highest);
        }
}

TEST_CASE("fit keyboard ignores the width and the song") {
    KeyViewParams p = params(kRange61, 100.0f, KeySize::FitKeyboard);
    p.song = KeyRange{60, 72};
    const KeyViewTarget t = compute_key_view(p);
    CHECK(t.range.lowest == 36);
    CHECK(t.range.highest == 96);
}

TEST_CASE("a keyboard ending on a black key widens to white keys like KeyboardLayout") {
    const KeyViewTarget t =
        compute_key_view(params(KeyRange{37, 94}, 1280.0f, KeySize::FitKeyboard));
    CHECK(t.range.lowest == 36);
    CHECK(t.range.highest == 95);
}

TEST_CASE("a bad keyboard falls back to 88 keys") {
    const KeyViewTarget t = compute_key_view(params(KeyRange{90, 20}, 1280.0f, KeySize::Small));
    CHECK(inside(t.range, kRange88));
    CHECK(white_edges(t.range));
}

TEST_CASE("fit song zooms to the song plus two white keys each side") {
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::FitSong);
    p.song = KeyRange{60, 72}; // C4 to C5: 8 white keys
    const KeyViewTarget t = compute_key_view(p);
    CHECK(whites_in(t.range) == 8 + 2 * kSongMarginWhites);
    CHECK(white_rank(t.range.lowest) == white_rank(60) - kSongMarginWhites);
    CHECK(white_rank(t.range.highest) == white_rank(72) + kSongMarginWhites);
    CHECK(white_edges(t.range));
}

TEST_CASE("fit song with a black key at the top of the song keeps the key and its neighbours") {
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::FitSong);
    p.song = KeyRange{60, 73}; // ends on C#5
    const KeyViewTarget t = compute_key_view(p);
    CHECK(fully_visible(t.range, 73));
    CHECK(fully_visible(t.range, 60));
}

TEST_CASE("fit song is never narrower than an octave") {
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::FitSong);
    p.song = KeyRange{64, 64};
    const KeyViewTarget t = compute_key_view(p);
    CHECK(whites_in(t.range) == kMinSongWhites);
    CHECK(fully_visible(t.range, 64));
    // Centred on the note: three white keys each side.
    CHECK(white_rank(64) - white_rank(t.range.lowest) == 3);
}

TEST_CASE("fit song is never wider than the keyboard") {
    KeyViewParams p = params(kRange61, 1280.0f, KeySize::FitSong);
    p.song = KeyRange{21, 108}; // wider than the keyboard
    const KeyViewTarget t = compute_key_view(p);
    CHECK(t.range.lowest == 36);
    CHECK(t.range.highest == 96);
}

TEST_CASE("fit song keeps its size at the edge of the keyboard and slides inside") {
    KeyViewParams p = params(kRange61, 1280.0f, KeySize::FitSong);
    p.song = KeyRange{36, 48}; // at the keyboard's lowest note
    const KeyViewTarget low = compute_key_view(p);
    CHECK(low.range.lowest == 36);
    CHECK(whites_in(low.range) == 8 + 2 * kSongMarginWhites); // same size, slid inside
    p.song = KeyRange{84, 96};
    const KeyViewTarget high = compute_key_view(p);
    CHECK(high.range.highest == 96);
}

TEST_CASE("fit song without a song shows the whole keyboard") {
    const KeyViewTarget t = compute_key_view(params(kRange88, 1280.0f, KeySize::FitSong));
    CHECK(t.range.lowest == 21);
    CHECK(t.range.highest == 108);
}

TEST_CASE("a fixed size shows as many whole white keys as fit the width") {
    // Small is 12 mm: 1280 px / 45.4 px = 28 keys.
    const KeyViewTarget t = compute_key_view(params(kRange88, 1280.0f, KeySize::Small));
    CHECK(whites_in(t.range) == 28);
    CHECK(white_edges(t.range));
    // Extra large, 24 mm: 14 keys.
    CHECK(whites_in(compute_key_view(params(kRange88, 1280.0f, KeySize::ExtraLarge)).range) == 14);
    CHECK(whites_in(compute_key_view(params(kRange88, 1280.0f, KeySize::Medium)).range) == 21);
    CHECK(whites_in(compute_key_view(params(kRange88, 1280.0f, KeySize::Large)).range) == 16);
}

TEST_CASE("each step up in key size shows no more keys") {
    for (const float width : {800.0f, 1024.0f, 1280.0f, 1920.0f, 3840.0f}) {
        int previous = 1000;
        for (const KeySize s :
             {KeySize::Small, KeySize::Medium, KeySize::Large, KeySize::ExtraLarge}) {
            const int n = whites_in(compute_key_view(params(kRange88, width, s)).range);
            CHECK(n <= previous);
            previous = n;
        }
    }
}

TEST_CASE("a fixed size that fits the whole keyboard shows it all") {
    const KeyViewTarget t = compute_key_view(params(kRange61, 4000.0f, KeySize::Large));
    CHECK(t.range.lowest == 36);
    CHECK(t.range.highest == 96);
}

TEST_CASE("a window too narrow for three keys still shows three") {
    const KeyViewTarget t = compute_key_view(params(kRange88, 50.0f, KeySize::ExtraLarge));
    CHECK(whites_in(t.range) == kMinFixedWhites);
}

TEST_CASE("a fixed size with nothing to show starts on middle C") {
    const KeyViewTarget t = compute_key_view(params(kRange88, 1280.0f, KeySize::Large));
    CHECK(fully_visible(t.range, 60));
    CHECK(inside(t.range, kRange88));
    // Centred: middle C is within one key of the window's middle.
    const int mid = (white_rank(t.range.lowest) + white_rank(t.range.highest)) / 2;
    CHECK(std::abs(mid - white_rank(60)) <= 1);
}

TEST_CASE("a keyboard without middle C starts on its own middle") {
    const KeyViewTarget t = compute_key_view(params(KeyRange{72, 108}, 1280.0f, KeySize::Large));
    CHECK(inside(t.range, KeyRange{72, 108}));
}

TEST_CASE("a bad width or pixel density falls back instead of failing") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float width : {0.0f, -10.0f, nan, std::numeric_limits<float>::infinity()}) {
        const KeyViewTarget t = compute_key_view(params(kRange88, width, KeySize::Medium));
        CHECK(inside(t.range, kRange88));
        CHECK(white_edges(t.range));
        CHECK(whites_in(t.range) >= kMinFixedWhites);
    }
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::Medium);
    p.px_per_mm = nan;
    CHECK(whites_in(compute_key_view(p).range) >= kMinFixedWhites);
    p.px_per_mm = 0.0f;
    CHECK(whites_in(compute_key_view(p).range) >= kMinFixedWhites);
}

TEST_CASE("a stray key size value shows the whole keyboard") {
    KeyViewParams p = params(kRange61, 1280.0f, KeySize::FitKeyboard);
    p.size = static_cast<KeySize>(42);
    CHECK(compute_key_view(p).range.lowest == 36);
}

TEST_CASE("auto shows the whole song range: C2 to C5 shows C2 to C5 with two white keys each side") {
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::FitSong);
    p.song = KeyRange{36, 72}; // C2 to C5
    const KeyViewTarget t = compute_key_view(p);
    CHECK(t.range.lowest == note_of_rank(white_rank(36) - kSongMarginWhites)); // A1
    CHECK(t.range.highest == note_of_rank(white_rank(72) + kSongMarginWhites)); // E5
    CHECK(fully_visible(t.range, 36));
    CHECK(fully_visible(t.range, 72));
}

TEST_CASE("a fixed size is centred on the song and keeps its count when the song is wider") {
    // Large, 20 mm: 16 white keys at 1280 px.
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::Large);
    p.song = KeyRange{60, 72}; // C4 to C5: 8 white keys, centre G4 (rank 39)
    const KeyViewTarget t = compute_key_view(p);
    CHECK(whites_in(t.range) == 16);
    CHECK(fully_visible(t.range, 60));
    CHECK(fully_visible(t.range, 72));
    const int mid2 = white_rank(t.range.lowest) + white_rank(t.range.highest);
    CHECK(std::abs(mid2 - (white_rank(60) + white_rank(72))) <= 1);
    // A song of 4 octaves: still 16 keys, both ends cut alike.
    p.song = KeyRange{36, 84};
    const KeyViewTarget wide = compute_key_view(p);
    CHECK(whites_in(wide.range) == 16);
    const int cut_low = white_rank(wide.range.lowest) - white_rank(36);
    const int cut_high = white_rank(84) - white_rank(wide.range.highest);
    CHECK(std::abs(cut_low - cut_high) <= 1);
}

TEST_CASE("a fixed size centred on a song at the keyboard's edge stays on the keyboard") {
    KeyViewParams p = params(kRange61, 1280.0f, KeySize::Large);
    p.song = KeyRange{36, 40};
    const KeyViewTarget t = compute_key_view(p);
    CHECK(t.range.lowest == 36);
    CHECK(whites_in(t.range) == 16);
    p.song = KeyRange{93, 96};
    CHECK(compute_key_view(p).range.highest == 96);
}

TEST_CASE("free play: a fixed size moves once, the least it can, to show a key held off it") {
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::Medium); // 21 white keys
    const KeyViewTarget start = compute_key_view(p);
    CHECK(fully_visible(start.range, 60)); // middle C to begin with
    // A key held past the right edge: the window's right edge comes to it, no further.
    const int far[] = {96};
    p.current = start.range;
    p.held = far;
    const KeyViewTarget moved = compute_key_view(p);
    CHECK(whites_in(moved.range) == 21);
    CHECK(moved.range.highest == 96);
    // Let go: it stays where it moved to.
    p.current = moved.range;
    p.held = {};
    const KeyViewTarget stays = compute_key_view(p);
    CHECK(stays.range.lowest == moved.range.lowest);
    CHECK(stays.range.highest == moved.range.highest);
    // A key held inside the window moves nothing.
    const int inside_key[] = {84};
    p.held = inside_key;
    CHECK(compute_key_view(p).range.lowest == moved.range.lowest);
    // Past the left edge: the left edge comes to it.
    const int low[] = {36};
    p.held = low;
    CHECK(compute_key_view(p).range.lowest == 36);
}

TEST_CASE("free play: a black key held past the right edge brings its upper neighbour too") {
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::Medium);
    p.current = compute_key_view(p).range;
    const int black[] = {85}; // C#6
    p.held = black;
    const KeyViewTarget t = compute_key_view(p);
    CHECK(fully_visible(t.range, 85));
    CHECK(t.range.highest == 86);
}

TEST_CASE("free play: held keys wider than the window centre it on them") {
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::ExtraLarge); // 14 white keys
    p.current = compute_key_view(p).range;
    const int wide[] = {36, 96};
    p.held = wide;
    const KeyViewTarget t = compute_key_view(p);
    CHECK(whites_in(t.range) == 14);
    const int mid2 = white_rank(t.range.lowest) + white_rank(t.range.highest);
    CHECK(std::abs(mid2 - (white_rank(36) + white_rank(96))) <= 1);
}

TEST_CASE("with a song the view never moves for a held key, nor for Auto or Fit keyboard") {
    const int far[] = {100};
    KeyViewParams p = params(kRange88, 1280.0f, KeySize::Medium);
    p.song = KeyRange{60, 72};
    const KeyViewTarget rest = compute_key_view(p);
    p.current = KeyRange{21, 50};
    p.held = far;
    const KeyViewTarget t = compute_key_view(p);
    CHECK(t.range.lowest == rest.range.lowest);
    CHECK(t.range.highest == rest.range.highest);
    // No song, Auto: the whole keyboard whatever is held.
    p.song.reset();
    p.size = KeySize::FitSong;
    CHECK(compute_key_view(p).range.lowest == 21);
    p.size = KeySize::FitKeyboard;
    CHECK(compute_key_view(p).range.highest == 108);
}

TEST_CASE("property: every size gives a valid window that only the song, the size and the width decide") {
    std::mt19937 rng(20261013);
    const KeyRange keyboards[] = {kRange49, kRange61, kRange76, kRange88, KeyRange{48, 84}};
    const KeySize sizes[] = {KeySize::FitKeyboard, KeySize::FitSong, KeySize::Small,
                             KeySize::Medium,      KeySize::Large,   KeySize::ExtraLarge};
    for (int iter = 0; iter < 4000; ++iter) {
        KeyViewParams p;
        p.keyboard = keyboards[rng() % 5];
        p.size = sizes[rng() % 6];
        p.width_px = 200.0f + static_cast<float>(rng() % 4000);
        p.px_per_mm = 2.0f + static_cast<float>(rng() % 60) / 10.0f;
        if (rng() % 2 == 0) {
            const int a = p.keyboard.lowest + static_cast<int>(rng() % 40);
            p.song = KeyRange{a, a + static_cast<int>(rng() % 30)};
        }
        const KeyboardLayout normalised(p.keyboard);
        const KeyViewTarget t = compute_key_view(p);
        CAPTURE(iter);
        CHECK(white_edges(t.range));
        CHECK(inside(t.range, normalised.range()));
        CHECK(t.range.lowest <= t.range.highest);
        CHECK(whites_in(t.range) <= normalised.white_key_count());
        if (p.size == KeySize::FitSong && p.song) {
            // Auto holds every key of the song that the keyboard has.
            for (const int note : {std::max(p.song->lowest, normalised.range().lowest),
                                   std::min(p.song->highest, normalised.range().highest)}) {
                const bool top_black = is_black_key(note) && note + 1 > normalised.range().highest;
                if (note <= normalised.range().highest && note >= normalised.range().lowest &&
                    !top_black) {
                    CHECK(fully_visible(t.range, note));
                }
            }
        }
        const KeyViewTarget again = compute_key_view(p);
        CHECK(again.range.lowest == t.range.lowest);
        CHECK(again.range.highest == t.range.highest);
    }
}

TEST_CASE("property: a fixed size never shows fewer keys than fit, nor more than one extra") {
    std::mt19937 rng(7);
    for (int iter = 0; iter < 2000; ++iter) {
        const KeySize size = static_cast<KeySize>(2 + rng() % 4);
        KeyViewParams p = params(kRange88, 300.0f + static_cast<float>(rng() % 3500), size);
        p.px_per_mm = 3.0f + static_cast<float>(rng() % 40) / 10.0f;
        const float key_px = key_size_white_mm(size) * p.px_per_mm;
        const int fit = static_cast<int>(std::floor(p.width_px / key_px));
        const int n = whites_in(compute_key_view(p).range);
        CAPTURE(iter);
        CHECK(n == std::clamp(fit, kMinFixedWhites, 52));
    }
}

TEST_CASE("a follower snapped to a target draws exactly that range with no pan") {
    KeyViewFollower f;
    CHECK_FALSE(f.has_position());
    f.snap(kRange88, KeyRange{48, 91});
    CHECK(f.has_position());
    const KeyViewFrame fr = f.frame();
    CHECK(fr.layout_range.lowest == 48);
    CHECK(fr.layout_range.highest == 91);
    CHECK(fr.pan == 0.0f);
    CHECK(fr.span_whites == doctest::Approx(static_cast<float>(whites_in(KeyRange{48, 91}))));
    CHECK(fr.settled);
}

TEST_CASE("a follower snapped to the whole keyboard shows it with a span of its white keys") {
    KeyViewFollower f;
    f.snap(kRange61, kRange61);
    const KeyViewFrame fr = f.frame();
    CHECK(fr.layout_range.lowest == 36);
    CHECK(fr.layout_range.highest == 96);
    CHECK(fr.span_whites == doctest::Approx(36.0f));
}

TEST_CASE("a follower moves toward the target without overshooting and arrives") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{36, 79});
    const KeyRange target{48, 91}; // 7 white keys to the right
    float previous_left = 0.0f;
    bool first = true;
    for (int i = 0; i < 600; ++i) {
        f.advance(kRange88, target, 1.0 / 60.0);
        const KeyViewFrame fr = f.frame();
        const float left = static_cast<float>(white_rank(fr.layout_range.lowest)) + fr.pan;
        CHECK(left <= static_cast<float>(white_rank(48)) + 1e-4f); // no overshoot
        if (!first) {
            CHECK(left >= previous_left - 1e-6f); // never moves backwards
        }
        previous_left = left;
        first = false;
    }
    const KeyViewFrame fr = f.frame();
    CHECK(fr.settled);
    CHECK(fr.layout_range.lowest == 48);
    CHECK(fr.layout_range.highest == 91);
    CHECK(fr.pan == 0.0f);
}

TEST_CASE("a follower in motion draws partial keys at both edges with white-key range ends") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{36, 79});
    f.advance(kRange88, KeyRange{48, 91}, 0.05);
    const KeyViewFrame fr = f.frame();
    CHECK_FALSE(fr.settled);
    CHECK(fr.pan > 0.0f);
    CHECK(fr.pan < 1.0f);
    CHECK(white_edges(fr.layout_range));
    CHECK(fr.layout_range.lowest >= 36);
    // Covers the whole window: first key starts at or before the left edge, last ends at or after
    // the right edge.
    const float left = static_cast<float>(white_rank(fr.layout_range.lowest));
    const float right = static_cast<float>(white_rank(fr.layout_range.highest) + 1);
    CHECK(left <= left + fr.pan);
    CHECK(right >= left + fr.pan + fr.span_whites - 1e-4f);
    CHECK(right - left < fr.span_whites + 2.0f);
}

TEST_CASE("the follower's result does not depend on the frame rate") {
    const KeyRange target{60, 103};
    std::vector<float> lefts;
    for (const int steps : {1, 7, 10, 60, 144, 1000}) {
        KeyViewFollower f;
        f.snap(kRange88, KeyRange{36, 79});
        for (int i = 0; i < steps; ++i) {
            f.advance(kRange88, target, 0.25 / steps);
        }
        const KeyViewFrame fr = f.frame();
        lefts.push_back(static_cast<float>(white_rank(fr.layout_range.lowest)) + fr.pan);
    }
    for (const float v : lefts) {
        CHECK(v == doctest::Approx(lefts.front()).epsilon(1e-4));
    }
}

TEST_CASE("the follower covers nearly all of a jump within a second") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{36, 79});
    f.advance(kRange88, KeyRange{60, 103}, 1.0);
    const KeyViewFrame fr = f.frame();
    const float left = static_cast<float>(white_rank(fr.layout_range.lowest)) + fr.pan;
    const float from = static_cast<float>(white_rank(36));
    const float to = static_cast<float>(white_rank(60));
    CHECK((left - from) / (to - from) > 0.95f);
}

TEST_CASE("zero, negative and NaN frame times do not move the follower") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{36, 79});
    for (const double dt : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
        f.advance(kRange88, KeyRange{60, 103}, dt);
        CHECK(f.frame().layout_range.lowest == 36);
        CHECK(f.frame().pan == 0.0f);
    }
}

TEST_CASE("advancing a follower that was never snapped starts at the target") {
    KeyViewFollower f;
    f.advance(kRange88, KeyRange{48, 91}, 1.0 / 60.0);
    CHECK(f.has_position());
    CHECK(f.frame().layout_range.lowest == 48);
    CHECK(f.frame().settled);
}

TEST_CASE("a zoom animates both edges") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{48, 91});
    f.advance(kRange88, kRange88, 0.05);
    const KeyViewFrame mid = f.frame();
    CHECK_FALSE(mid.settled);
    CHECK(mid.span_whites > static_cast<float>(whites_in(KeyRange{48, 91})));
    CHECK(mid.span_whites < 52.0f);
    for (int i = 0; i < 300; ++i) {
        f.advance(kRange88, kRange88, 1.0 / 60.0);
    }
    CHECK(f.frame().settled);
    CHECK(f.frame().layout_range.lowest == 21);
    CHECK(f.frame().layout_range.highest == 108);
}

TEST_CASE("the follower stays inside the keyboard even with leftover speed") {
    KeyViewFollower f;
    f.snap(kRange61, KeyRange{36, 79});
    // Fling it right, then reverse hard.
    f.advance(kRange61, KeyRange{53, 96}, 0.05);
    f.advance(kRange61, KeyRange{36, 79}, 0.03);
    for (int i = 0; i < 100; ++i) {
        f.advance(kRange61, i % 2 == 0 ? KeyRange{53, 96} : KeyRange{36, 79}, 0.016);
        const KeyViewFrame fr = f.frame();
        CHECK(fr.layout_range.lowest >= 36);
        CHECK(fr.layout_range.highest <= 96);
        CHECK(fr.pan >= 0.0f);
        CHECK(fr.pan < 1.0f);
    }
}

TEST_CASE("snap again after a keyboard change replaces the position") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{48, 91});
    f.snap(kRange61, kRange61);
    CHECK(f.frame().layout_range.lowest == 36);
    CHECK(f.frame().layout_range.highest == 96);
}

TEST_CASE("to_window converts layout units to window units for pan and span") {
    // Settled on the whole 61 key keyboard: identity.
    KeyViewFollower f;
    f.snap(kRange61, kRange61);
    const KeyboardLayout layout(f.frame().layout_range);
    const KeyRect c4 = layout.key(60);
    const KeyRect w = f.frame().to_window(c4, layout.white_key_count());
    CHECK(w.x == doctest::Approx(c4.x));
    CHECK(w.width == doctest::Approx(c4.width));
    CHECK(w.black == c4.black);
}

TEST_CASE("to_window shifts left by the pan and scales by the span") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{48, 91}); // 26 white keys... 44 keys: 26 whites
    f.advance(kRange88, KeyRange{60, 103}, 0.06);
    const KeyViewFrame fr = f.frame();
    const KeyboardLayout layout(fr.layout_range);
    const int whites = layout.white_key_count();
    const KeyRect first_white = layout.key(fr.layout_range.lowest);
    const KeyRect w = fr.to_window(first_white, whites);
    // The first key starts `pan` white keys left of the window edge.
    CHECK(w.x == doctest::Approx(-fr.pan / fr.span_whites).epsilon(1e-4));
    CHECK(w.width == doctest::Approx(1.0f / fr.span_whites).epsilon(1e-4));
}

TEST_CASE("side_of says which side of the window a note is on") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{48, 83}); // C3 to B5
    const KeyViewFrame fr = f.frame();
    CHECK(fr.side_of(60) == 0);
    CHECK(fr.side_of(48) == 0);
    CHECK(fr.side_of(83) == 0);
    CHECK(fr.side_of(47) == -1);
    CHECK(fr.side_of(21) == -1);
    CHECK(fr.side_of(0) == -1);
    CHECK(fr.side_of(84) == 1);
    CHECK(fr.side_of(127) == 1);
}

TEST_CASE("side_of matches what the layout draws, black keys included") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{48, 83});
    const KeyViewFrame fr = f.frame();
    CHECK(fr.side_of(49) == 0);  // C#3
    CHECK(fr.side_of(82) == 0);  // A#5, below the last white key
    CHECK(fr.side_of(46) == -1); // A#2, left of C3
    CHECK(fr.side_of(84) == 1);  // C6
    CHECK(fr.side_of(85) == 1);  // C#6
}

TEST_CASE("side_of follows the pan: a half-hidden key is not off screen") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{48, 83});
    f.advance(kRange88, KeyRange{60, 95}, 0.05); // sliding right: C3 is leaving on the left
    const KeyViewFrame fr = f.frame();
    REQUIRE(fr.pan > 0.0f);
    const float left = static_cast<float>(white_rank(fr.layout_range.lowest)) + fr.pan;
    // The key that the left edge is cutting is still on screen.
    CHECK(fr.side_of(note_of_rank(static_cast<int>(std::floor(left)))) == 0);
    CHECK(fr.side_of(note_of_rank(static_cast<int>(std::floor(left)) - 1)) == -1);
}

TEST_CASE("side_of is 0 for every note of a fit keyboard view") {
    KeyViewFollower f;
    f.snap(kRange61, kRange61);
    for (int n = 36; n <= 96; ++n) {
        CHECK(f.frame().side_of(n) == 0);
    }
    CHECK(f.frame().side_of(35) == -1);
    CHECK(f.frame().side_of(97) == 1);
}
