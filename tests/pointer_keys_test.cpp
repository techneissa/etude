// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/pointer_keys.hpp"

#include "etude/key_state.hpp"
#include "etude/pitch.hpp"

#include <cmath>
#include <random>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

using Events = std::vector<MidiEvent>;

constexpr RectF kArea{0.0f, 500.0f, 1200.0f, 200.0f}; // keys y 500..700, black keys to y 624

PointerGeometry whole(KeyRange range = kRange61, BlockLayout blocks = BlockLayout::KeyWidth) {
    KeyViewFollower f;
    f.snap(range, range);
    const KeyViewFrame fr = f.frame();
    return {KeyboardLayout(fr.layout_range, blocks), fr, kArea};
}

// x of the middle of a key (a black key with KeyWidth is centred on the line between two white
// keys).
float centre_x(const PointerGeometry& g, int note) {
    const KeyRect r = g.view.to_window(g.layout.key(note), g.layout.white_key_count());
    return kArea.x + (r.x + r.width / 2.0f) * kArea.w;
}

// x of a white key's right edge.
float right_edge_x(const PointerGeometry& g, int note) {
    const KeyRect r = g.view.to_window(g.layout.key(note), g.layout.white_key_count());
    return kArea.x + (r.x + r.width) * kArea.w;
}

int notes_on(const Events& e) {
    int n = 0;
    for (const MidiEvent& ev : e) {
        n += ev.type == MidiEventType::NoteOn ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("a point on a white key hits that key") {
    const PointerGeometry g = whole();
    for (int note = 36; note <= 96; ++note) {
        if (is_black_key(note)) {
            continue;
        }
        const auto hit = hit_test_key(g, centre_x(g, note), 680.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->note == note);
    }
}

TEST_CASE("in the upper part a black key is hit, below it the white key under it") {
    const PointerGeometry g = whole();
    const float x = centre_x(g, 37); // C#2
    CHECK(hit_test_key(g, x, 510.0f)->note == 37);
    CHECK(hit_test_key(g, x, 620.0f)->note == 37);
    // Just under the black key's tip the point is on the white key whose area it falls in.
    const auto below = hit_test_key(g, x, 630.0f);
    REQUIRE(below.has_value());
    CHECK_FALSE(is_black_key(below->note));
    // The boundary line between C and D is where C# is centred: with the point left of centre the
    // white key is C, right of it D.
    CHECK(hit_test_key(g, x - 1.0f, 630.0f)->note == 36);
    CHECK(hit_test_key(g, x + 1.0f, 630.0f)->note == 38);
}

TEST_CASE("the black key's edges and the white keys beside it") {
    const PointerGeometry g = whole();
    const float x = centre_x(g, 37);
    const float half = 0.5f * KeyboardLayout::kBlackWidthRatio * (kArea.w / 36.0f);
    CHECK(hit_test_key(g, x + half - 0.5f, 520.0f)->note == 37);
    CHECK(hit_test_key(g, x - half + 0.5f, 520.0f)->note == 37);
    CHECK(hit_test_key(g, x + half + 0.5f, 520.0f)->note == 38);
    CHECK(hit_test_key(g, x - half - 0.5f, 520.0f)->note == 36);
}

TEST_CASE("even lanes: a black key is its slot, left or right of the white keys' edge") {
    const PointerGeometry g = whole(kRange61, BlockLayout::EvenLanes);
    const float ww = kArea.w / 36.0f;
    const float c = kArea.x + g.view.to_window(g.layout.key(36), 36).x * kArea.w;
    // C# fills the second of the five slots of C-E: 3/5 to 6/5 of a white key from C.
    const float left = c + 0.6f * ww;
    const float right = c + 1.2f * ww;
    CHECK(hit_test_key(g, left + 0.5f, 520.0f)->note == 37);
    CHECK(hit_test_key(g, right - 0.5f, 520.0f)->note == 37);
    CHECK(hit_test_key(g, left - 0.5f, 520.0f)->note == 36);
    CHECK(hit_test_key(g, right + 0.5f, 520.0f)->note == 38);
    // Under its tip, its middle is over C: C# leans left of the C-D edge.
    CHECK(hit_test_key(g, centre_x(g, 37), 630.0f)->note == 36);
}

TEST_CASE("touch is right at every key edge of a whole keyboard, in front and at the back") {
    for (BlockLayout blocks : {BlockLayout::KeyWidth, BlockLayout::EvenLanes})
        for (KeyRange range : {kRange88, kRange61, kRange76, KeyRange{40, 77}}) {
            const PointerGeometry g = whole(range, blocks);
            const int whites = g.layout.white_key_count();
            const auto px = [&](float u) { return kArea.x + u * kArea.w; };
            for (int note = range.lowest; note <= range.highest; ++note) {
                const KeyRect k = g.view.to_window(g.layout.key(note), whites);
                const float x0 = px(k.x);
                const float x1 = px(k.x + k.width);
                INFO("layout " << static_cast<int>(blocks) << " range " << range.lowest << " note "
                               << note);
                if (is_black_key(note)) {
                    // At the back: on the key just inside its edges, its white neighbours just
                    // outside.
                    CHECK(hit_test_key(g, x0 + 0.25f, 560.0f)->note == note);
                    CHECK(hit_test_key(g, x1 - 0.25f, 560.0f)->note == note);
                    CHECK(hit_test_key(g, x0 - 0.25f, 560.0f)->note == note - 1);
                    CHECK(hit_test_key(g, x1 + 0.25f, 560.0f)->note == note + 1);
                    continue;
                }
                // In front: the whole width is the white key's.
                CHECK(hit_test_key(g, x0 + 0.25f, 690.0f)->note == note);
                if (x1 - 0.25f < kArea.right()) {
                    CHECK(hit_test_key(g, x1 - 0.25f, 690.0f)->note == note);
                }
                // At the back: its slot, the part no black key covers, is the white key's.
                const KeyRect s = g.view.to_window(g.layout.slot(note), whites);
                CHECK(hit_test_key(g, px(s.x + 0.5f * s.width), 560.0f)->note == note);
            }
        }
}

TEST_CASE("every note on the keyboard can be hit") {
    const PointerGeometry g = whole(kRange88);
    for (int note = 21; note <= 108; ++note) {
        const float y = is_black_key(note) ? 520.0f : 680.0f;
        const auto hit = hit_test_key(g, centre_x(g, note), y);
        REQUIRE(hit.has_value());
        CHECK(hit->note == note);
    }
}

TEST_CASE("points outside the key area hit nothing") {
    const PointerGeometry g = whole();
    CHECK_FALSE(hit_test_key(g, 600.0f, 499.0f).has_value());
    CHECK_FALSE(hit_test_key(g, 600.0f, 700.0f).has_value());
    CHECK_FALSE(hit_test_key(g, -1.0f, 600.0f).has_value());
    CHECK_FALSE(hit_test_key(g, 1200.0f, 600.0f).has_value());
    CHECK(hit_test_key(g, 0.0f, 500.0f).has_value());
    CHECK(hit_test_key(g, 1199.9f, 699.9f).has_value());
    const float nan = std::nanf("");
    CHECK_FALSE(hit_test_key(g, nan, 600.0f).has_value());
    CHECK_FALSE(hit_test_key(g, 600.0f, nan).has_value());
    PointerGeometry empty = g;
    empty.key_area = {};
    CHECK_FALSE(hit_test_key(empty, 600.0f, 600.0f).has_value());
}

TEST_CASE("velocity grows from the back of the key to the front") {
    const PointerGeometry g = whole();
    const float x = centre_x(g, 48);
    const int back = hit_test_key(g, x, 500.0f)->velocity;
    const int middle = hit_test_key(g, x, 600.0f)->velocity;
    const int front = hit_test_key(g, x, 699.9f)->velocity;
    CHECK(back == kPointerMinVelocity);
    CHECK(front >= 126);
    CHECK(front <= kPointerMaxVelocity);
    CHECK(back < middle);
    CHECK(middle < front);
    // Linear: the middle is halfway.
    CHECK(middle ==
          doctest::Approx((kPointerMinVelocity + kPointerMaxVelocity) / 2.0).epsilon(0.02));
}

TEST_CASE("velocity never decreases going down a white key and stays in 1..127") {
    const PointerGeometry g = whole();
    const float x = centre_x(g, 52);
    int previous = 0;
    for (float y = 500.0f; y < 700.0f; y += 0.5f) {
        const auto hit = hit_test_key(g, x, y);
        REQUIRE(hit.has_value());
        CHECK(hit->velocity >= previous);
        CHECK(hit->velocity >= 1);
        CHECK(hit->velocity <= 127);
        previous = hit->velocity;
    }
}

TEST_CASE("a black key's velocity runs over its own length") {
    const PointerGeometry g = whole();
    const float x = centre_x(g, 37);
    CHECK(hit_test_key(g, x, 500.0f)->velocity == kPointerMinVelocity);
    CHECK(hit_test_key(g, x, 623.0f)->velocity >= 125);
}

TEST_CASE("hit testing follows a panned and zoomed view") {
    KeyViewFollower f;
    f.snap(kRange88, KeyRange{48, 83});
    f.advance(kRange88, KeyRange{60, 95}, 0.05);
    const KeyViewFrame fr = f.frame();
    REQUIRE(fr.pan > 0.0f);
    const PointerGeometry g{KeyboardLayout(fr.layout_range), fr, kArea};
    for (int note = fr.layout_range.lowest + 2; note <= fr.layout_range.highest - 2; ++note) {
        if (is_black_key(note)) {
            continue;
        }
        const auto hit = hit_test_key(g, centre_x(g, note), 680.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->note == note);
    }
}

TEST_CASE("pointer events carry the pointer source, channel 0 and the time") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 777, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOn);
    CHECK(out[0].source == InputSource::Pointer);
    CHECK(out[0].is_virtual());
    CHECK(out[0].data1 == 60);
    CHECK(out[0].channel == 0);
    CHECK(out[0].time_ns == 777);
    out.clear();
    p.up(1, 888, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOff);
    CHECK(out[0].data1 == 60);
    CHECK(out[0].time_ns == 888);
    CHECK(out[0].source == InputSource::Pointer);
}

TEST_CASE("each finger holds one note and a chord plays all of them") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    p.down(2, centre_x(g, 64), 650.0f, g, 2, out);
    p.down(3, centre_x(g, 67), 650.0f, g, 3, out);
    CHECK(notes_on(out) == 3);
    CHECK(p.active_pointers() == 3);
    CHECK(p.held_notes() == 3);
    CHECK(p.note_of(2) == 64);
    out.clear();
    p.up(2, 4, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].data1 == 64);
    CHECK(p.held_notes() == 2);
    CHECK(p.note_of(1) == 60);
}

TEST_CASE("a press that lands on no key plays nothing and the pointer stays unused") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, 600.0f, 300.0f, g, 1, out); // in the note area
    CHECK(out.empty());
    CHECK(p.active_pointers() == 0);
    // Dragging that pointer onto the keys does not play: it never went down on a key.
    p.move(1, centre_x(g, 60), 650.0f, g, 2, out);
    CHECK(out.empty());
    p.up(1, 3, out);
    CHECK(out.empty());
}

TEST_CASE("moving onto a new key releases the old one and presses the new (glissando)") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    out.clear();
    p.move(1, centre_x(g, 60) + 1.0f, 660.0f, g, 2, out); // same key
    CHECK(out.empty());
    p.move(1, centre_x(g, 62), 650.0f, g, 3, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].type == MidiEventType::NoteOff);
    CHECK(out[0].data1 == 60);
    CHECK(out[1].type == MidiEventType::NoteOn);
    CHECK(out[1].data1 == 62);
    CHECK(p.held_notes() == 1);
    CHECK(p.note_of(1) == 62);
}

TEST_CASE("slide margin: drift inside the margin keeps the key, passing it changes the key") {
    PointerGeometry g = whole();
    g.slide_margin_px = kKeySlideMarginMm * 4.0f; // a 4 px/mm display: 6 px
    const float edge = right_edge_x(g, 60);       // between C4 and D4
    PointerKeys p;
    Events out;
    p.down(1, edge - 20.0f, 650.0f, g, 1, out);
    REQUIRE(out.size() == 1);
    out.clear();
    p.move(1, edge + 3.0f, 650.0f, g, 2, out); // over D4, but within 6 px of C4
    CHECK(out.empty());
    CHECK(p.note_of(1) == 60);
    p.move(1, edge + 5.5f, 650.0f, g, 3, out);
    CHECK(out.empty());
    p.move(1, edge + 7.0f, 650.0f, g, 4, out); // past the margin
    REQUIRE(out.size() == 2);
    CHECK(out[0].type == MidiEventType::NoteOff);
    CHECK(out[0].data1 == 60);
    CHECK(out[1].type == MidiEventType::NoteOn);
    CHECK(out[1].data1 == 62);
    CHECK(p.held_notes() == 1);
}

TEST_CASE("slide margin: drifting back and forth at an edge never sounds the neighbour") {
    PointerGeometry g = whole();
    g.slide_margin_px = kKeySlideMarginMm * 4.0f;
    const float edge = right_edge_x(g, 60);
    PointerKeys p;
    Events out;
    p.down(1, edge - 2.0f, 650.0f, g, 1, out);
    out.clear();
    for (int i = 0; i < 20; ++i) {
        p.move(1, edge + (i % 2 == 0 ? 4.0f : -4.0f), 650.0f, g, 2 + i, out);
    }
    CHECK(out.empty());
    CHECK(p.note_of(1) == 60);
}

TEST_CASE("slide margin: a glissando still plays every key, and a fast jump changes key") {
    PointerGeometry g = whole();
    g.slide_margin_px = kKeySlideMarginMm * 4.0f;
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    out.clear();
    int pressed = 0;
    for (float x = centre_x(g, 60); x < centre_x(g, 72); x += 2.0f) {
        p.move(1, x, 650.0f, g, 2, out);
        pressed += notes_on(out);
        out.clear();
    }
    CHECK(pressed >= 7);
    p.move(1, centre_x(g, 84), 650.0f, g, 3, out); // a jump far away
    REQUIRE(out.size() == 2);
    CHECK(out[1].data1 == 84);
}

TEST_CASE("slide margin: leaving the keys releases only past the margin") {
    PointerGeometry g = whole();
    g.slide_margin_px = kKeySlideMarginMm * 4.0f;
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 690.0f, g, 1, out);
    out.clear();
    p.move(1, centre_x(g, 60), kArea.bottom() + 3.0f, g, 2, out); // just below the keys
    CHECK(out.empty());
    CHECK(p.note_of(1) == 60);
    p.move(1, centre_x(g, 60), kArea.bottom() + 20.0f, g, 3, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOff);
}

TEST_CASE(
    "slide margin: drifting diagonally off a black key's lower corner keeps it, past the margin"
    " releases it") {
    PointerGeometry g = whole();
    g.slide_margin_px = kKeySlideMarginMm * 4.0f; // 6 px
    const KeyRect r = g.view.to_window(g.layout.key(37), g.layout.white_key_count());
    const float right = kArea.x + (r.x + r.width) * kArea.w;      // the black key's right edge
    const float split = kArea.y + kArea.h * kBlackKeyLengthShare; // its lower edge
    PointerKeys p;
    Events out;
    p.down(1, right - 1.0f, split - 1.0f, g, 1, out);
    REQUIRE(p.note_of(1) == 37);
    out.clear();
    // 4.5 px along each axis is 6.4 px away: past the margin, though neither axis is.
    p.move(1, right + 4.0f, split + 4.0f, g, 2, out); // 5.66 px: inside
    CHECK(out.empty());
    CHECK(p.note_of(1) == 37);
    p.move(1, right + 4.5f, split + 4.5f, g, 3, out); // 6.36 px: outside
    REQUIRE(out.size() == 2);
    CHECK(out[0].type == MidiEventType::NoteOff);
    CHECK(out[0].data1 == 37);
    CHECK(out[1].type == MidiEventType::NoteOn);
}

TEST_CASE("slide margin: exactly the margin away still keeps the key, a hair more leaves") {
    PointerGeometry g = whole();
    g.slide_margin_px = 6.0f;
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 690.0f, g, 1, out);
    out.clear();
    p.move(1, centre_x(g, 60), kArea.bottom() + 6.0f, g, 2, out);
    CHECK(out.empty());
    CHECK(p.note_of(1) == 60);
    p.move(1, centre_x(g, 60), kArea.bottom() + 6.5f, g, 3, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOff);
}

TEST_CASE("slide margin: a white key's notch beside a black key counts as the key") {
    PointerGeometry g = whole();
    g.slide_margin_px = 6.0f;
    // D4 (62) has black keys on both sides. A finger low on D4 drifts up into the notch at the
    // top of D4 between them, then over the black key's body: near the white key still.
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 62), 690.0f, g, 1, out);
    out.clear();
    p.move(1, centre_x(g, 62), 520.0f, g, 2, out); // the notch: still D4
    CHECK(out.empty());
    const KeyRect r = g.view.to_window(g.layout.key(61), g.layout.white_key_count());
    const float black_right = kArea.x + (r.x + r.width) * kArea.w;
    p.move(1, black_right - 2.0f, 520.0f, g, 3, out); // 2 px inside C#4, within 6 px of D4's notch
    CHECK(out.empty());
    CHECK(p.note_of(1) == 62);
    p.move(1, black_right - 8.0f, 520.0f, g, 4, out); // well inside C#4
    CHECK(out.size() == 2);
}

TEST_CASE("slide margin: with no margin the edge itself changes the key") {
    const PointerGeometry g = whole();
    CHECK(g.slide_margin_px == 0.0f);
    const float edge = right_edge_x(g, 60);
    PointerKeys p;
    Events out;
    p.down(1, edge - 5.0f, 650.0f, g, 1, out);
    out.clear();
    p.move(1, edge + 1.0f, 650.0f, g, 2, out);
    CHECK(out.size() == 2);
}

TEST_CASE("dragging across keys plays each key passed, one at a time") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    KeyState state;
    const auto apply = [&] {
        for (const MidiEvent& e : out) {
            state.apply(e);
        }
        out.clear();
    };
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    apply();
    int max_held = 0;
    int pressed = 1;
    for (float x = centre_x(g, 60); x < centre_x(g, 72); x += 2.0f) {
        p.move(1, x, 650.0f, g, 2, out);
        pressed += notes_on(out);
        apply();
        max_held = std::max(max_held, state.held_count());
    }
    CHECK(max_held == 1);
    CHECK(pressed >= 8); // C D E F G A B on the way, black keys in the lower part are skipped
    p.up(1, 3, out);
    apply();
    CHECK(state.held_count() == 0);
}

TEST_CASE("a drag through the upper part plays the black keys it crosses") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    bool saw_black = false;
    p.down(1, centre_x(g, 60), 520.0f, g, 1, out);
    for (float x = centre_x(g, 60); x < centre_x(g, 72); x += 1.0f) {
        out.clear();
        p.move(1, x, 520.0f, g, 2, out);
        for (const MidiEvent& e : out) {
            saw_black = saw_black || (e.type == MidiEventType::NoteOn && is_black_key(e.data1));
        }
    }
    CHECK(saw_black);
}

TEST_CASE("leaving the key area releases the note, coming back presses it again") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    out.clear();
    p.move(1, centre_x(g, 60), 300.0f, g, 2, out); // up into the note area
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOff);
    CHECK_FALSE(p.note_of(1).has_value());
    CHECK(p.active_pointers() == 1); // still down
    out.clear();
    p.move(1, centre_x(g, 60), 400.0f, g, 3, out);
    CHECK(out.empty());
    p.move(1, centre_x(g, 64), 650.0f, g, 4, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOn);
    CHECK(out[0].data1 == 64);
    out.clear();
    p.up(1, 5, out);
    CHECK(out.size() == 1);
    CHECK(p.active_pointers() == 0);
}

TEST_CASE("leaving off the side of the keyboard releases too") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, 5.0f, 650.0f, g, 1, out);
    out.clear();
    p.move(1, -50.0f, 650.0f, g, 2, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOff);
}

TEST_CASE("an up for a pointer that is not down does nothing") {
    PointerKeys p;
    Events out;
    p.up(9, 1, out);
    p.move(9, 1.0f, 1.0f, whole(), 1, out);
    CHECK(out.empty());
}

TEST_CASE("a second finger on the same key shares the note until both let go") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    p.down(2, centre_x(g, 60) + 3.0f, 600.0f, g, 2, out);
    CHECK(notes_on(out) == 1); // one note-on
    out.clear();
    p.up(1, 3, out);
    CHECK(out.empty()); // finger 2 still holds it
    p.up(2, 4, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOff);
}

TEST_CASE("release_all lets go of every pointer's note exactly once") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    p.down(1, centre_x(g, 60), 650.0f, g, 1, out);
    p.down(2, centre_x(g, 64), 650.0f, g, 2, out);
    p.down(3, centre_x(g, 64), 650.0f, g, 3, out);
    out.clear();
    p.release_all(9, out);
    int offs = 0;
    for (const MidiEvent& e : out) {
        CHECK(e.type == MidiEventType::NoteOff);
        CHECK(e.time_ns == 9);
        ++offs;
    }
    CHECK(offs == 2);
    CHECK(p.active_pointers() == 0);
    CHECK(p.held_notes() == 0);
    out.clear();
    p.release_all(10, out);
    CHECK(out.empty());
    p.up(1, 11, out); // a late up
    CHECK(out.empty());
}

TEST_CASE("more pointers than slots: the extra ones play nothing and nothing sticks") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    for (std::uint64_t id = 0; id < 40; ++id) {
        p.down(id, centre_x(g, 36 + static_cast<int>(id % 20)), 650.0f, g, 1, out);
    }
    CHECK(p.active_pointers() <= static_cast<int>(PointerKeys::kMaxPointers));
    for (std::uint64_t id = 0; id < 40; ++id) {
        p.up(id, 2, out);
    }
    CHECK(p.held_notes() == 0);
    KeyState state;
    for (const MidiEvent& e : out) {
        state.apply(e);
    }
    CHECK(state.held_count() == 0);
}

TEST_CASE("property: random pointer storms always end with every key up") {
    std::mt19937 rng(42);
    const PointerGeometry g = whole(kRange88);
    for (int round = 0; round < 200; ++round) {
        PointerKeys p;
        Events out;
        KeyState state;
        for (int i = 0; i < 300; ++i) {
            const std::uint64_t id = rng() % 6;
            const float x = static_cast<float>(rng() % 1300) - 50.0f;
            const float y = 480.0f + static_cast<float>(rng() % 260);
            switch (rng() % 3) {
            case 0:
                p.down(id, x, y, g, i, out);
                break;
            case 1:
                p.move(id, x, y, g, i, out);
                break;
            default:
                p.up(id, i, out);
                break;
            }
            for (const MidiEvent& e : out) {
                state.apply(e);
            }
            // The state always agrees with the pointers.
            REQUIRE(state.held_count() == p.held_notes());
            out.clear();
        }
        p.release_all(999, out);
        for (const MidiEvent& e : out) {
            state.apply(e);
        }
        CHECK(state.held_count() == 0);
    }
}

TEST_CASE("finger filter: only a touch screen plays, a trackpad's fingers are ignored") {
    for (const FingerPhase phase :
         {FingerPhase::Down, FingerPhase::Motion, FingerPhase::Up, FingerPhase::Cancelled}) {
        CHECK(finger_action(phase, false, false, true) == FingerAction::Ignore);
    }
    CHECK(finger_action(FingerPhase::Down, true, false, true) == FingerAction::Press);
    CHECK(finger_action(FingerPhase::Motion, true, false, true) == FingerAction::Move);
    CHECK(finger_action(FingerPhase::Up, true, false, true) == FingerAction::Release);
}

TEST_CASE("finger filter: a touch the UI owns never presses, but a lift always releases") {
    CHECK(finger_action(FingerPhase::Down, true, true, true) == FingerAction::Ignore);
    CHECK(finger_action(FingerPhase::Up, true, true, true) == FingerAction::Release);
    CHECK(finger_action(FingerPhase::Cancelled, true, true, true) == FingerAction::Release);
    CHECK(finger_action(FingerPhase::Down, true, false, false) == FingerAction::Ignore);
    CHECK(finger_action(FingerPhase::Motion, true, false, false) == FingerAction::Ignore);
    CHECK(finger_action(FingerPhase::Up, true, false, false) == FingerAction::Release);
}

namespace {

// Feeds one finger event through the filter the way VirtualInput does.
void feed(PointerKeys& p, FingerPhase phase, bool direct, bool ui_owns, std::uint64_t id, float x,
          float y, const PointerGeometry& g, std::int64_t t, Events& out) {
    switch (finger_action(phase, direct, ui_owns, true)) {
    case FingerAction::Press:
        p.down(id, x, y, g, t, out);
        break;
    case FingerAction::Move:
        p.move(id, x, y, g, t, out);
        break;
    case FingerAction::Release:
        p.up(id, t, out);
        break;
    case FingerAction::Ignore:
        break;
    }
}

} // namespace

TEST_CASE("finger path: a cancelled touch releases its key and the finger can play again") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    const float x = centre_x(g, 60);
    feed(p, FingerPhase::Down, true, false, 7, x, 650.0f, g, 1, out);
    REQUIRE(out.size() == 1);
    CHECK(p.held_notes() == 1);
    out.clear();
    feed(p, FingerPhase::Cancelled, true, false, 7, x, 650.0f, g, 2, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].type == MidiEventType::NoteOff);
    CHECK(p.held_notes() == 0);
    CHECK(p.active_pointers() == 0);
    out.clear();
    feed(p, FingerPhase::Down, true, false, 7, x, 650.0f, g, 3, out); // the same id, a new touch
    CHECK(notes_on(out) == 1);
}

TEST_CASE("finger path: a trackpad's fingers and a touch on the sheet play nothing") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    const float x = centre_x(g, 60);
    feed(p, FingerPhase::Down, false, false, 1, x, 650.0f, g, 1, out);
    feed(p, FingerPhase::Motion, false, false, 1, x + 40.0f, 650.0f, g, 2, out);
    feed(p, FingerPhase::Down, true, true, 2, x, 650.0f, g, 3, out);
    feed(p, FingerPhase::Motion, true, false, 2, x + 40.0f, 650.0f, g, 4, out);
    CHECK(out.empty());
    CHECK(p.active_pointers() == 0);
}

TEST_CASE(
    "finger path: a finger that slides onto the sheet's area then lifts leaves nothing held") {
    const PointerGeometry g = whole();
    PointerKeys p;
    Events out;
    feed(p, FingerPhase::Down, true, false, 3, centre_x(g, 60), 650.0f, g, 1, out);
    feed(p, FingerPhase::Motion, true, true, 3, centre_x(g, 64), 650.0f, g, 2, out);
    feed(p, FingerPhase::Up, true, true, 3, centre_x(g, 64), 650.0f, g, 3, out);
    CHECK(p.held_notes() == 0);
    CHECK(p.active_pointers() == 0);
}
