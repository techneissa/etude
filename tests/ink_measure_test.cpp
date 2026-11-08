// SPDX-License-Identifier: GPL-3.0-or-later
// The icon audit's measuring: the ink box of what differs between two renders, with sub-pixel edges
// from the antialiasing, and the centring rule (0.5 px; a play triangle sits right by 8% of its
// width).
#include "ui/ink_measure.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace etude::app::ui;

namespace {

struct Image {
    int w;
    int h;
    std::vector<std::uint8_t> px;
    Image(int w_, int h_) : w(w_), h(h_), px(static_cast<std::size_t>(w_ * h_ * 4), 20) {}
    PixelView view() const { return {px.data(), w, h, w * 4}; }
    // Fills the rectangle [x0, x1) x [y0, y1) in pixel units with white, each pixel by the share it
    // covers: the antialiasing a renderer gives an axis-aligned edge.
    void fill(float x0, float y0, float x1, float y1) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float cx = std::max(0.0f, std::min(static_cast<float>(x + 1), x1) -
                                                    std::max(static_cast<float>(x), x0));
                const float cy = std::max(0.0f, std::min(static_cast<float>(y + 1), y1) -
                                                    std::max(static_cast<float>(y), y0));
                const float cover = cx * cy;
                for (int c = 0; c < 3; ++c) {
                    std::uint8_t& p = px[static_cast<std::size_t>((y * w + x) * 4 + c)];
                    p = static_cast<std::uint8_t>(std::lround(p + (255.0f - p) * cover));
                }
            }
        }
    }
};

} // namespace

TEST_CASE("measure_ink: whole-pixel and fractional edges are read back") {
    const Image blank(40, 40);
    Image whole(40, 40);
    whole.fill(10.0f, 12.0f, 18.0f, 20.0f);
    InkBox b = measure_ink(whole.view(), blank.view(), {0, 0, 40, 40});
    REQUIRE(b.found);
    CHECK(b.left == doctest::Approx(10.0f).epsilon(0.01));
    CHECK(b.right == doctest::Approx(18.0f).epsilon(0.01));
    CHECK(b.cx() == doctest::Approx(14.0f).epsilon(0.01));
    CHECK(b.cy() == doctest::Approx(16.0f).epsilon(0.01));

    Image half(40, 40);
    half.fill(10.5f, 12.25f, 17.5f, 19.75f); // centre 14.0, 16.0
    b = measure_ink(half.view(), blank.view(), {0, 0, 40, 40});
    REQUIRE(b.found);
    CHECK(b.cx() == doctest::Approx(14.0f).epsilon(0.01));
    CHECK(b.cy() == doctest::Approx(16.0f).epsilon(0.01));
    CHECK(b.width() == doctest::Approx(7.0f).epsilon(0.02));
}

TEST_CASE("measure_ink: a half-pixel shift shows as half a pixel") {
    const Image blank(40, 40);
    Image a(40, 40);
    Image b(40, 40);
    a.fill(10.0f, 10.0f, 18.0f, 18.0f);
    b.fill(10.5f, 10.0f, 18.5f, 18.0f);
    const InkBox ma = measure_ink(a.view(), blank.view(), {0, 0, 40, 40});
    const InkBox mb = measure_ink(b.view(), blank.view(), {0, 0, 40, 40});
    REQUIRE(ma.found);
    REQUIRE(mb.found);
    CHECK(mb.cx() - ma.cx() == doctest::Approx(0.5f).epsilon(0.03));
    CHECK(mb.cy() == doctest::Approx(ma.cy()));
}

TEST_CASE("measure_ink: only the area counts, and no difference finds nothing") {
    Image with(40, 40);
    const Image without(40, 40);
    with.fill(5.0f, 5.0f, 9.0f, 9.0f);
    with.fill(30.0f, 30.0f, 34.0f, 34.0f);
    const InkBox b = measure_ink(with.view(), without.view(), {20, 20, 40, 40});
    REQUIRE(b.found);
    CHECK(b.cx() == doctest::Approx(32.0f).epsilon(0.01));
    CHECK_FALSE(measure_ink(without.view(), without.view(), {0, 0, 40, 40}).found);
    CHECK_FALSE(measure_ink(with.view(), without.view(), {10, 10, 20, 20}).found); // empty spot
    CHECK_FALSE(measure_ink(with.view(), without.view(), {50, 50, 60, 60}).found); // off the image
    CHECK_FALSE(measure_ink(PixelView{}, without.view(), {0, 0, 40, 40}).found);
}

TEST_CASE("judge_ink: 0.5 px on each axis, a play triangle right of centre by 8% of its width") {
    InkBox ink;
    ink.found = true;
    ink.left = 10.0f;
    ink.right = 20.0f; // centre 15
    ink.top = 10.0f;
    ink.bottom = 20.0f;
    CHECK(judge_ink(ink, 15.0f, 15.0f, InkKind::Centred).ok);
    CHECK(judge_ink(ink, 14.5f, 15.0f, InkKind::Centred).ok);  // exactly at the tolerance
    CHECK(judge_ink(ink, 14.46f, 15.0f, InkKind::Centred).ok); // the measuring slack: 0.04 over
    CHECK_FALSE(judge_ink(ink, 14.4f, 15.0f, InkKind::Centred).ok); // just over
    CHECK_FALSE(judge_ink(ink, 15.0f, 15.6f, InkKind::Centred).ok);
    CHECK(judge_ink(ink, 15.0f, 15.6f, InkKind::Centred, false).ok); // the vertical axis left out
    // A play triangle 10 px wide is wanted 0.8 px right.
    CHECK(judge_ink(ink, 14.2f, 15.0f, InkKind::PlayTriangle).ok);
    CHECK_FALSE(judge_ink(ink, 15.0f, 15.0f, InkKind::PlayTriangle).ok); // dead centre is 0.8 off
    CHECK(judge_ink(ink, 14.2f, 15.0f, InkKind::PlayTriangle).dx == doctest::Approx(0.8f));
    CHECK_FALSE(judge_ink(InkBox{}, 15.0f, 15.0f, InkKind::Centred).ok); // nothing found
}

TEST_CASE("place_label: the cap band is centred, not the line box") {
    // A font whose line box is 20 tall, capitals from 5 to 15 (a 10 px cap band), a letter's ink
    // from 1 to 9 wide.
    LabelMetrics m;
    m.cap_top = 5.0f;
    m.baseline = 15.0f;
    m.ink_left = 1.0f;
    m.ink_right = 9.0f;
    // Centred on y = 50: the band's middle (10) lands on 50, so the pen is 40 down (here the same
    // as centring the 20 px line box).
    CHECK(place_label(m, 0.0f, 100.0f, 50.0f, LabelAlign::Centre).y == doctest::Approx(40.0f));
    // Caps 2 px higher in the line box (room kept below for descenders): the pen follows the caps,
    // 2 px lower than the line box's centre would put it.
    m.cap_top = 3.0f;
    m.baseline = 13.0f;
    CHECK(place_label(m, 0.0f, 100.0f, 50.0f, LabelAlign::Centre).y == doctest::Approx(42.0f));
}

TEST_CASE("place_label: the ink's sides, not the advance's") {
    LabelMetrics m;
    m.cap_top = 5.0f;
    m.baseline = 15.0f;
    m.ink_left = 2.0f;
    m.ink_right = 22.0f; // the advance is wider than the ink on both sides
    // Centred in 0..100: the ink spans 20 px, so it starts at 40 and the pen at 38.
    CHECK(place_label(m, 0.0f, 100.0f, 0.0f, LabelAlign::Centre).x == doctest::Approx(38.0f));
    // Right: the ink's right edge at 100, so the pen is 22 left of it.
    CHECK(place_label(m, 0.0f, 100.0f, 0.0f, LabelAlign::Right).x == doctest::Approx(78.0f));
    // Left: the pen starts at the slot.
    CHECK(place_label(m, 10.0f, 100.0f, 0.0f, LabelAlign::Left).x == doctest::Approx(10.0f));
}

TEST_CASE("measure_label: the baseline is the foot of the letters, not of a descender") {
    Image without(60, 40);
    Image with = without;
    // The letters: a 10 px tall block standing on y = 30, x from 10 to 30.
    with.fill(10.0f, 20.0f, 30.0f, 30.0f);
    // A descender: a thin stem under it, to y = 34.
    with.fill(22.0f, 30.0f, 24.0f, 34.0f);
    const LabelInk ink = measure_label(with.view(), without.view(), {0, 0, 60, 40}, true);
    REQUIRE(ink.found);
    CHECK(ink.baseline == doctest::Approx(30.0f));
    // A word with no descenders reads its baseline from the foot of the ink, stems and all.
    Image lone = without;
    lone.fill(10.0f, 20.0f, 20.0f, 22.0f); // a T's bar
    lone.fill(14.0f, 22.0f, 16.0f, 30.0f); // and its thin stem: under a third of the bar's row
    const LabelInk t = measure_label(lone.view(), without.view(), {0, 0, 60, 40}, false);
    REQUIRE(t.found);
    CHECK(t.baseline == doctest::Approx(30.0f).epsilon(0.02));
    CHECK(ink.box.left == doctest::Approx(10.0f).epsilon(0.02));
    CHECK(ink.box.right == doctest::Approx(30.0f).epsilon(0.02));
    CHECK(ink.box.bottom == doctest::Approx(34.0f).epsilon(0.02)); // the whole box has it
    CHECK_FALSE(measure_label(without.view(), without.view(), {0, 0, 60, 40}, false).found);
}

TEST_CASE("measure_label: a p, its bowl on the baseline and its stem hanging below") {
    Image without(60, 40);
    Image with = without;
    with.fill(10.0f, 20.0f, 12.0f, 34.0f); // the stem, from the x-height to 4 px under the baseline
    with.fill(12.0f, 22.0f, 20.0f, 24.0f); // the bowl: top,
    with.fill(12.0f, 28.0f, 20.0f, 30.0f); // foot, standing on y = 30,
    with.fill(18.0f, 24.0f, 20.0f, 28.0f); // and its right side
    const LabelInk ink = measure_label(with.view(), without.view(), {0, 0, 60, 40}, true);
    REQUIRE(ink.found);
    CHECK(ink.baseline == doctest::Approx(30.0f));            // the bowl's foot, not the stem's
    CHECK(ink.box.bottom == doctest::Approx(34.0f).epsilon(0.02)); // the box has the stem
    // Read as a word with no descenders, the same pixels would put the baseline on the stem.
    const LabelInk flat = measure_label(with.view(), without.view(), {0, 0, 60, 40}, false);
    REQUIRE(flat.found);
    CHECK(flat.baseline == doctest::Approx(34.0f).epsilon(0.02));
}

TEST_CASE("judge_label: the cap band on the measured baseline, 1 px on each axis") {
    LabelInk ink;
    ink.found = true;
    ink.box.found = true;
    ink.box.left = 10.0f;
    ink.box.right = 30.0f; // centre 20
    ink.box.top = 20.0f;
    ink.box.bottom = 34.0f;
    ink.baseline = 30.0f;
    // Capitals 10 tall standing on y = 30 span 20..30: their middle is 25.
    CHECK(judge_label(ink, 10.0f, 20.0f, 25.0f, true).ok);
    CHECK(judge_label(ink, 10.0f, 21.0f, 26.0f, true).ok);       // exactly at the tolerance
    CHECK_FALSE(judge_label(ink, 10.0f, 20.0f, 26.2f, true).ok); // just over, vertical
    CHECK_FALSE(judge_label(ink, 10.0f, 21.2f, 25.0f, true).ok); // just over, horizontal
    CHECK(judge_label(ink, 10.0f, 60.0f, 25.0f, false).ok);      // the horizontal axis left out
    CHECK(judge_label(ink, 10.0f, 20.0f, 25.0f, true).dy == doctest::Approx(0.0f));
    // The old line-box placement put the baseline 3 px high: it reads -3 and fails.
    ink.baseline = 27.0f;
    CHECK_FALSE(judge_label(ink, 10.0f, 20.0f, 25.0f, true).ok);
    CHECK(judge_label(ink, 10.0f, 20.0f, 25.0f, true).dy == doctest::Approx(-3.0f));
    CHECK_FALSE(judge_label(LabelInk{}, 10.0f, 20.0f, 25.0f, true).ok); // nothing found
}

TEST_CASE("label_inside: a centred label wider than its face fails however well it is centred") {
    LabelInk ink;
    ink.found = true;
    ink.box.found = true;
    ink.box.left = 0.0f;
    ink.box.right = 80.0f; // 80 wide, centred on 40
    ink.box.top = 20.0f;
    ink.box.bottom = 34.0f;
    ink.baseline = 30.0f;
    // On a face 50 wide centred on the same middle (x 15..65) the label sits dead centre, which
    // the old rule took for a pass ...
    CHECK(judge_label(ink, 10.0f, 40.0f, 25.0f, true).ok);
    // ... but it runs 15 px past each edge of the face.
    CHECK_FALSE(label_inside(ink.box.left, ink.box.right, 15.0f, 65.0f));
    // Run past one edge only (a left label on a face it overhangs).
    CHECK_FALSE(label_inside(10.0f, 70.0f, 15.0f, 65.0f));
    CHECK_FALSE(label_inside(20.0f, 67.0f, 15.0f, 65.0f));
    CHECK(label_inside(14.0f, 66.0f, 15.0f, 65.0f));      // within the pixel of rounding
    CHECK_FALSE(label_inside(13.5f, 66.0f, 15.0f, 65.0f)); // 1.5 px over
}

TEST_CASE("label_inside: a label as wide as its slot inside a face with padding passes") {
    // The slot is the text's own width, 56.4 px, centred in a 80 px face (11.8 px of padding each
    // side). The drawn ink is 1.3 px wider than the slot (a whole font size against the fractional
    // one the slot was measured at): 0.65 px past the slot on each side, still 11 px in from the
    // face's edges.
    const float face_l = 100.0f;
    const float face_r = 180.0f;
    const float slot_l = 111.8f;
    const float slot_r = 168.2f;
    CHECK(label_inside(slot_l - 0.65f, slot_r + 0.65f, face_l, face_r));
    CHECK(label_inside(slot_l, slot_r, face_l, face_r)); // exactly the slot
    CHECK(label_inside(face_l, face_r, face_l, face_r)); // filling the face, no padding at all
}

TEST_CASE("label_face: the smallest face of the label's own list that holds the middle or an end") {
    int list_a = 0;
    int list_b = 0;
    // A chip 56..116 on a row 20..888, both on list A, and a face on list B (a control behind a
    // card) that is smaller than either and sits right over the ink.
    const FaceBox row{20.0f, 0.0f, 888.0f, 40.0f, &list_a};
    const FaceBox chip{56.0f, 4.0f, 116.0f, 36.0f, &list_a};
    const FaceBox behind{60.0f, 4.0f, 190.0f, 36.0f, &list_b};
    const std::vector<FaceBox> faces{row, chip, behind};
    // A face on another list is ignored.
    CHECK(label_face(faces, &list_a, 60.0f, 100.0f, 20.0f) == 1);
    CHECK(label_face(faces, &list_b, 60.0f, 100.0f, 20.0f) == 2);
    int list_c = 0;
    CHECK(label_face(faces, &list_c, 60.0f, 100.0f, 20.0f) == -1); // on no face
    // Left-aligned ink 60..190 spills past the chip: its middle (125) is off the chip but its left
    // end is on it, so the chip is chosen (not the row) and the label fails.
    const int spilling = label_face(faces, &list_a, 60.0f, 190.0f, 20.0f);
    REQUIRE(spilling == 1);
    CHECK_FALSE(label_inside(60.0f, 190.0f, chip.left, chip.right));
    // The right end alone also finds the chip.
    CHECK(label_face(faces, &list_a, 0.0f, 100.0f, 20.0f) == 1);
    // A fitting label on the chip passes.
    const int fitting = label_face(faces, &list_a, 62.0f, 110.0f, 20.0f);
    REQUIRE(fitting == 1);
    CHECK(label_inside(62.0f, 110.0f, chip.left, chip.right));
    // A label that stops where the next control's face starts is not on it: ink 10..60 beside a
    // switch at 60..100.
    const std::vector<FaceBox> beside{{0.0f, 0.0f, 300.0f, 40.0f, &list_a},
                                      {60.0f, 4.0f, 100.0f, 36.0f, &list_a}};
    CHECK(label_face(beside, &list_a, 10.0f, 60.0f, 20.0f) == 0);
    CHECK(label_face(beside, &list_a, 10.0f, 60.8f, 20.0f) == 0);
    CHECK(label_face(beside, &list_a, 10.0f, 63.0f, 20.0f) == 1); // 3 px onto it
    // Out of the chip's height: the row.
    CHECK(label_face(faces, &list_a, 62.0f, 110.0f, 2.0f) == 0);
}

// ---- The labels' metrics store ----------------------------------------------------------------

TEST_CASE("label metrics key: the font, the size and the shown words each change it, and it is never 0") {
    int font_a = 0;
    int font_b = 0;
    const std::uint64_t base = label_metrics_key(&font_a, 16.0f, "Play");
    CHECK(base == label_metrics_key(&font_a, 16.0f, "Play"));
    CHECK(base != label_metrics_key(&font_b, 16.0f, "Play")); // another font
    CHECK(base != label_metrics_key(&font_a, 17.0f, "Play")); // another size
    CHECK(base != label_metrics_key(&font_a, 16.0f, "Plays")); // other words
    // F# is drawn as F♯, a wider label: the key of the shown words differs from the key of the
    // written ones, which is why the store is asked with what is drawn.
    CHECK(label_metrics_key(&font_a, 16.0f, "F#") != label_metrics_key(&font_a, 16.0f, "F\xE2\x99\xAF"));
    std::set<std::uint64_t> seen;
    for (int size = 8; size < 40; ++size) {
        for (const char* text : {"", "A", "Start", "Continue", "Again", "1 of 6"}) {
            const std::uint64_t key = label_metrics_key(&font_a, static_cast<float>(size), text);
            CHECK(key != 0);
            CHECK(seen.insert(key).second);
        }
    }
}

TEST_CASE("label metrics store: a key reads only what was kept for it") {
    LabelMetricsStore store;
    LabelMetrics a;
    a.ink_right = 40.0f;
    LabelMetrics b;
    b.ink_right = 55.0f;
    const std::uint64_t key_a = 12345;
    const std::uint64_t key_b = key_a + LabelMetricsStore::kSlots; // the same slot, another label
    CHECK(store.find(key_a) == nullptr);
    store.put(key_a, a);
    REQUIRE(store.find(key_a) != nullptr);
    CHECK(store.find(key_a)->ink_right == doctest::Approx(40.0f));
    // A label that shares the slot never reads the other's width (a stale answer): it finds nothing,
    // is measured and kept, and then the first is the one that finds nothing.
    CHECK(store.find(key_b) == nullptr);
    store.put(key_b, b);
    REQUIRE(store.find(key_b) != nullptr);
    CHECK(store.find(key_b)->ink_right == doctest::Approx(55.0f));
    CHECK(store.find(key_a) == nullptr);
    CHECK(store.used() == 1);
    // A flagged answer (a letter the font lacks) is kept like any other.
    LabelMetrics lacking;
    lacking.sides_known = false;
    store.put(key_a, lacking);
    REQUIRE(store.find(key_a) != nullptr);
    CHECK_FALSE(store.find(key_a)->sides_known);
    store.clear();
    CHECK(store.find(key_a) == nullptr);
    CHECK(store.used() == 0);
}

// The kit cannot be linked into the tests (ImGui), so the two rules that keep the store honest are
// held on the source: `label_metrics` keys it with the font, the size and the words as displayed (the
// text after `screen_text`), and the fonts being built empties it.
TEST_CASE("label metrics: the kit keys the store by font, size and displayed text, and empties it with the fonts") {
    const auto read = [](const char* name) {
        std::ifstream in(std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "ui" / name);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const std::string text = read("kit_text.cpp");
    const std::size_t fn = text.find("LabelMetrics label_metrics(ImFont* font, float size, const char* text) {");
    REQUIRE(fn != std::string::npos);
    const std::size_t shown = text.find("text = screen_text(text);", fn);
    const std::size_t key = text.find("label_metrics_key(font, size, text)", fn);
    const std::size_t find = text.find("metrics_store().find(key)", fn);
    const std::size_t put = text.find("metrics_store().put(key, m)", fn);
    REQUIRE(shown != std::string::npos);
    REQUIRE(key != std::string::npos);
    CHECK(shown < key); // the key is of the displayed words
    CHECK(key < find);
    CHECK(find < put);
    // Both ways the store is emptied: with the kit's other font caches, and as the fonts are built.
    CHECK(text.find("metrics_store().clear();") != std::string::npos);
    const std::string fonts = read("host_fonts.cpp");
    const std::size_t build = fonts.find("void add_ui_fonts(Kit& kit) {");
    REQUIRE(build != std::string::npos);
    const std::size_t first = fonts.find("forget_fonts();", build);
    const std::size_t last = fonts.rfind("forget_fonts();");
    const std::size_t added = fonts.find("io.Fonts->AddFontFromMemoryTTF", build);
    REQUIRE(first != std::string::npos);
    CHECK(first < added);         // before the first font goes in
    CHECK(last > fonts.rfind("io.Fonts->AddFontFromMemoryTTF")); // and after the last
}
