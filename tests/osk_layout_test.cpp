// SPDX-License-Identifier: GPL-3.0-or-later
// The on-screen keyboard's geometry: every row runs the window's whole width, and the keys and the
// text on them grow with the window (the fix for keys bunched in the middle).
#include <doctest/doctest.h>

#include "ui/osk_layout.hpp"

#include <array>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;

namespace {
struct Size {
    float w;
    float h;
};
constexpr std::array<Size, 6> kWindows{
    {{800, 480}, {1024, 600}, {1280, 800}, {1366, 768}, {1920, 1080}, {640, 360}}};
constexpr std::array<float, 3> kScales{1.0f, 1.25f, 1.5f};
} // namespace

TEST_CASE("on-screen keyboard: every row runs the whole width of the window, edge to edge") {
    // The fix for keys bunched in the middle: at the audited sizes and past them, every row of
    // every layer starts at the pad and ends at the pad from the right edge, its keys touching.
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            const Box kb{0.0f, 0.0f, s.w, keyboard_height(s.w, u)};
            const float pad = osk_tok::kKeyPad * u;
            for (const Layer layer : {Layer::Letters, Layer::Symbols, Layer::Numbers}) {
                const auto rows = keyboard_rows(layer);
                Box area = kb;
                if (layer == Layer::Numbers) {
                    area.y1 = keyboard_height(s.w, u, 1e9f, 2);
                }
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    const Box first = keyboard_key(area, u, layer, r, 0);
                    const Box last = keyboard_key(area, u, layer, r, rows[r].count - 1);
                    CHECK(first.x0 == doctest::Approx(pad));
                    CHECK(last.x1 == doctest::Approx(s.w - pad));
                    for (std::size_t i = 1; i < rows[r].count; ++i) {
                        CHECK(keyboard_key(area, u, layer, r, i).x0 ==
                              doctest::Approx(keyboard_key(area, u, layer, r, i - 1).x1));
                    }
                }
            }
        }
    }
}

TEST_CASE("on-screen keyboard: keys grow with the window and the text on them with the keys") {
    const KeyInk small = key_ink(osk_tok::kKeyPitchMin, 1.0f);
    const KeyInk big = key_ink(osk_tok::kKeyPitchMax, 1.0f);
    CHECK(small.letter >= osk_tok::kKeyLetter);
    CHECK(big.letter > small.letter);
    CHECK(big.word > small.word);
    CHECK(big.icon > small.icon);
    CHECK(big.letter <= osk_tok::kKeyLetterMax);
    // At 1366x768 a letter key is about a tenth of the window wide and well over a hit tall.
    const Box kb{0.0f, 0.0f, 1366.0f, keyboard_height(1366.0f, 1.0f)};
    const Box q = keyboard_key(kb, 1.0f, Layer::Letters, 0, 0);
    CHECK(q.w() > 120.0f);
    CHECK(q.h() >= 1.5f * tok::kHit);
    // At 800x480 it is a tenth too, and still a hit tall.
    const Box kb800{0.0f, 0.0f, 800.0f, keyboard_height(800.0f, 1.0f)};
    CHECK(keyboard_key(kb800, 1.0f, Layer::Letters, 0, 0).h() >= tok::kHit);
}
