// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/key_label.hpp"

#include "etude/block_names.hpp"
#include "etude/grade_words.hpp"
#include "etude/screen_layout.hpp"
#include "ui/toast_regions.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace etude;

namespace {

constexpr float kPpmm = 96.0f / 25.4f;

bool intersects(const RectF& a, const RectF& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool inside(const RectF& inner, const RectF& outer) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
           inner.bottom() <= outer.bottom();
}

struct Window {
    float w;
    float h;
};
constexpr Window kWindows[] = {{800, 480}, {1366, 768}, {1920, 1080}};

// What the scene measures for "Eb minor 6b": about ten characters of the label's font, 1.3 tall.
RectF label_for(const ScreenLayout& lay, float h_px, std::span<const RectF> avoid = {}) {
    const float unit = std::max(h_px / 1080.0f, 0.25f);
    const float font = key_label_font_px(unit);
    const float height = std::ceil(1.25f * font);
    const float width = std::ceil(8.6f * font);
    const KeyLabelPlacement p =
        place_key_label(lay.note_area, width, height, std::round(kKeyLabelMarginEm * height), avoid);
    return p.visible ? p.rect : RectF{};
}

} // namespace

TEST_CASE("key label: the opacity is 35 to 45 percent and dims behind notes") {
    CHECK(kKeyLabelOpacity >= 0.35f);
    CHECK(kKeyLabelOpacity <= 0.45f);
    CHECK(kKeyLabelBehindShare > 0.0f);
    CHECK(kKeyLabelBehindShare < 1.0f);
}

TEST_CASE("key label: the size follows the window and is a whole, bounded number of pixels") {
    CHECK(key_label_font_px(0.25f) == kMinKeyLabelFontPx);
    CHECK(key_label_font_px(1.0f) == kKeyLabelFontPx);
    CHECK(key_label_font_px(100.0f) == kMaxKeyLabelFontPx);
    CHECK(key_label_font_px(0.0f) == kMinKeyLabelFontPx);
    CHECK(key_label_font_px(-1.0f) == kMinKeyLabelFontPx);
    CHECK(key_label_font_px(std::numeric_limits<float>::quiet_NaN()) == kMinKeyLabelFontPx);
    float last = 0.0f;
    for (const Window& win : kWindows) {
        const float f = key_label_font_px(win.h / 1080.0f);
        CHECK(f == std::floor(f));
        CHECK(f >= last);
        last = f;
    }
    CHECK(key_label_font_px(480.0f / 1080.0f) < key_label_font_px(1.0f));
}

TEST_CASE("key label: bottom left of the note area, clear of every other region") {
    for (const Window& win : kWindows) {
        for (const bool chord : {true, false}) {
            for (const bool pinned : {false, true}) {
                for (const bool expanded : {false, true}) {
                    CAPTURE(win.w);
                    CAPTURE(chord);
                    CAPTURE(pinned);
                    CAPTURE(expanded);
                    LookSettings look;
                    look.chord_label = chord;
                    PinnedBar bar;
                    bar.pinned = pinned;
                    bar.has_song = true;
                    bar.lane = expanded;
                    const ScreenLayout lay = compute_layout(win.w, win.h, kPpmm, look, bar);
                    const RectF r = label_for(lay, win.h);
                    REQUIRE_FALSE(r.empty());
                    CHECK(inside(r, lay.note_area));
                    // Left and just above the keyboard: the hit line is the note area's bottom.
                    CHECK(r.x > lay.note_area.x);
                    CHECK(r.x < lay.note_area.x + 0.1f * lay.note_area.w);
                    CHECK(r.bottom() < lay.note_area.bottom());
                    CHECK(r.bottom() > lay.note_area.bottom() - 0.2f * lay.note_area.h);
                    for (const RectF& other :
                         {lay.top_row, lay.bar_under, lay.lane, lay.chord_label, lay.keyboard,
                          lay.key_bed, lay.key_area, lay.gear, lay.library, lay.learn, lay.status_text}) {
                        CHECK_FALSE(intersects(r, other));
                    }
                }
            }
        }
    }
}

TEST_CASE("key label: a rect in its corner moves it up, and a full corner hides it") {
    const RectF area{0.0f, 100.0f, 800.0f, 300.0f}; // bottom at 400
    const float w = 120.0f;
    const float h = 20.0f;
    const KeyLabelPlacement plain = place_key_label(area, w, h, 10.0f);
    REQUIRE(plain.visible);
    CHECK(plain.rect.x == 10.0f);
    CHECK(plain.rect.bottom() == 390.0f);
    // A line across the corner: the label goes above it, with a gap.
    const RectF line{0.0f, 375.0f, 800.0f, 3.0f};
    const std::array<RectF, 1> avoid{line};
    const KeyLabelPlacement up = place_key_label(area, w, h, 10.0f, avoid);
    REQUIRE(up.visible);
    CHECK(up.rect.bottom() <= line.y - kKeyLabelGapPx + 0.001f);
    CHECK_FALSE(intersects(up.rect, line));
    // A rect away from the label changes nothing.
    const std::array<RectF, 1> far{RectF{400.0f, 300.0f, 100.0f, 90.0f}};
    CHECK(place_key_label(area, w, h, 10.0f, far).rect.y == plain.rect.y);
    // Touching edges are not overlap.
    const std::array<RectF, 1> touching{RectF{0.0f, 390.0f, 800.0f, 10.0f}};
    CHECK(place_key_label(area, w, h, 10.0f, touching).rect.y == plain.rect.y);
    // Two stacked rects: above both.
    const std::array<RectF, 2> two{RectF{0.0f, 360.0f, 800.0f, 30.0f},
                                   RectF{0.0f, 330.0f, 800.0f, 25.0f}};
    const KeyLabelPlacement both = place_key_label(area, w, h, 10.0f, two);
    REQUIRE(both.visible);
    CHECK_FALSE(intersects(both.rect, two[0]));
    CHECK_FALSE(intersects(both.rect, two[1]));
    // A rect that fills the corner up to the area's top leaves no place.
    const std::array<RectF, 1> full{RectF{0.0f, 100.0f, 800.0f, 300.0f}};
    CHECK_FALSE(place_key_label(area, w, h, 10.0f, full).visible);
    CHECK(place_key_label(area, w, h, 10.0f, full).rect.empty());
}

TEST_CASE("key label: too wide, too small an area or a bad number gives nothing") {
    const RectF area{0.0f, 100.0f, 200.0f, 100.0f};
    CHECK(place_key_label(area, 150.0f, 20.0f, 10.0f).visible);
    CHECK_FALSE(place_key_label(area, 181.0f, 20.0f, 10.0f).visible); // 181 + 20 > 200
    CHECK(place_key_label(area, 180.0f, 20.0f, 10.0f).visible);       // exactly fits
    CHECK_FALSE(place_key_label({}, 50.0f, 20.0f, 10.0f).visible);
    CHECK_FALSE(place_key_label(area, 0.0f, 20.0f, 10.0f).visible);
    CHECK_FALSE(place_key_label(area, 50.0f, 0.0f, 10.0f).visible);
    CHECK_FALSE(place_key_label(area, 50.0f, 20.0f, -1.0f).visible);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(place_key_label(area, nan, 20.0f, 10.0f).visible);
    CHECK_FALSE(place_key_label(area, 50.0f, nan, 10.0f).visible);
    CHECK_FALSE(place_key_label(RectF{0.0f, 100.0f, nan, 100.0f}, 50.0f, 20.0f, 10.0f).visible);
    // Taller than the area.
    CHECK_FALSE(place_key_label(area, 50.0f, 100.0f, 10.0f).visible);
}

TEST_CASE("key label: grade words keep clear of it, the older word giving way") {
    for (const Window& win : kWindows) {
        CAPTURE(win.w);
        const LookSettings look;
        const ScreenLayout lay = compute_layout(win.w, win.h, kPpmm, look);
        const RectF label = label_for(lay, win.h);
        REQUIRE_FALSE(label.empty());
        GradeLayoutParams p;
        p.note_area = lay.note_area;
        p.chord_label = lay.chord_label;
        p.position = GradePosition::Bottom;
        p.word_h = 22.0f;
        // A word over the label and one well to its right.
        const std::array<GradeWordIn, 2> words{
            GradeWordIn{label.x + 0.5f * label.w, 70.0f, 0.3f},
            GradeWordIn{label.right() + 300.0f, 70.0f, 0.3f}};
        std::array<GradeWordOut, 2> out{};
        const std::array<RectF, 1> avoid{label};
        place_grade_words(p, avoid, words, out);
        CHECK(out[1].visible);
        for (const GradeWordOut& o : out) {
            if (o.visible) {
                CHECK_FALSE(intersects(o.rect, label));
            }
        }
    }
}

TEST_CASE("key label: a block name goes above it, or is left out, never over it") {
    const RectF clip{0.0f, 100.0f, 800.0f, 300.0f};
    const RectF keep{10.0f, 360.0f, 150.0f, 30.0f};
    BlockNameParams params;
    params.clip = clip;
    params.max_font_px = 17.0f;
    params.keep_clear = keep;
    static const std::vector<float> table = [] {
        std::vector<float> t(64);
        for (std::size_t px = 0; px < t.size(); ++px) {
            t[px] = 0.7f * static_cast<float>(px);
        }
        return t;
    }();
    const auto plan = [&](const RectF& block, const BlockNameParams& pr) {
        BlockNameIn in;
        in.block = block;
        in.note = 36;
        in.width.letter = table;
        std::array<BlockNameIn, 1> blocks{in};
        std::array<BlockNamePlacement, 1> out{};
        plan_block_names(pr, blocks, out);
        return out[0];
    };
    // A tall block at the hit line, over the label's columns: the name sits above the label.
    const BlockNamePlacement above = plan(RectF{40.0f, 100.0f, 30.0f, 300.0f}, params);
    REQUIRE(above.form != NameForm::Hidden);
    CHECK(above.rect.bottom() <= keep.y);
    // Without the keep-clear rect the name is at the bottom, in it.
    BlockNameParams none = params;
    none.keep_clear = {};
    const BlockNamePlacement low = plan(RectF{40.0f, 100.0f, 30.0f, 300.0f}, none);
    REQUIRE(low.form != NameForm::Hidden);
    CHECK(low.rect.bottom() > keep.y);
    // A block beside the label (outside its columns) is untouched.
    const BlockNamePlacement beside = plan(RectF{300.0f, 100.0f, 30.0f, 300.0f}, params);
    CHECK(beside.rect.y == doctest::Approx(plan(RectF{300.0f, 100.0f, 30.0f, 300.0f}, none).rect.y));
    // A short block that sits wholly in the label's rect has nowhere above it: no name.
    CHECK(plan(RectF{40.0f, 365.0f, 30.0f, 25.0f}, params).form == NameForm::Hidden);
    // A block above the label (not yet reaching it) keeps its name where it was.
    const RectF high{40.0f, 120.0f, 30.0f, 100.0f};
    CHECK(plan(high, params).rect.y == doctest::Approx(plan(high, none).rect.y));
}

TEST_CASE("key label: the toasts keep clear of it") {
    app::ui::ToastRegionsInput in;
    in.display_w = 1366.0f;
    in.display_h = 768.0f;
    in.row_top = 0.0f;
    in.row_bottom = 57.0f;
    in.key_label = {20.0f, 560.0f, 140.0f, 24.0f};
    ToastRegions r;
    REQUIRE(app::ui::fill_toast_regions(r, in));
    bool found = false;
    for (std::size_t i = 0; i < r.avoid_count; ++i) {
        found = found || (r.avoid[i].x == 20.0f && r.avoid[i].y == 560.0f);
    }
    CHECK(found);
    // Hidden label: nothing added.
    app::ui::ToastRegionsInput none = in;
    none.key_label = {};
    ToastRegions q;
    REQUIRE(app::ui::fill_toast_regions(q, none));
    CHECK(q.avoid_count + 1 == r.avoid_count);
}

// ---- Fading

namespace {

constexpr std::int64_t kMs = 1'000'000;

} // namespace

TEST_CASE("key label fade: the first label fades in, and goes out when hidden") {
    KeyLabelFade f;
    CHECK_FALSE(f.visible());
    KeyLabelFade::Update u = f.update(0, true, 7, false, false);
    CHECK(u.new_label);
    CHECK(f.layer(u.slot).presence == 0.0f);
    f.update(200 * kMs, true, 7, false, false);
    CHECK(f.layer(u.slot).presence == doctest::Approx(0.5f));
    f.update(400 * kMs, true, 7, false, false);
    CHECK(f.layer(u.slot).presence == 1.0f);
    // The same id again asks for nothing new.
    CHECK_FALSE(f.update(500 * kMs, true, 7, false, false).new_label);
    // Hidden: fades out and is then gone.
    f.update(700 * kMs, false, 7, false, false);
    CHECK(f.layer(u.slot).presence == doctest::Approx(0.5f));
    f.update(900 * kMs, false, 7, false, false);
    CHECK_FALSE(f.visible());
    // Shown again: the same label comes back without a new one being made.
    CHECK_FALSE(f.update(1000 * kMs, true, 7, false, false).new_label);
}

TEST_CASE("key label fade: a changed key cross-fades between two slots") {
    KeyLabelFade f;
    const int first = f.update(0, true, 1, false, false).slot;
    f.update(500 * kMs, true, 1, false, false);
    f.update(900 * kMs, true, 1, false, false);
    REQUIRE(f.layer(first).presence == 1.0f);
    const KeyLabelFade::Update u = f.update(1000 * kMs, true, 2, false, false);
    CHECK(u.new_label);
    CHECK(u.slot != first);
    // 300 ms in (the change itself was seen 100 ms into it): the new rises as the old sinks.
    f.update(1200 * kMs, true, 2, false, false);
    CHECK(f.layer(u.slot).presence == doctest::Approx(0.75f));
    CHECK(f.layer(first).presence == doctest::Approx(0.25f));
    f.update(1400 * kMs, true, 2, false, false);
    CHECK(f.layer(u.slot).presence == 1.0f);
    CHECK(f.layer(first).presence == 0.0f);
    // The slot given to a third key is the first one again.
    CHECK(f.update(1500 * kMs, true, 3, false, false).slot == first);
}

TEST_CASE("key label fade: a third key within the fade rises from where its slot stood") {
    KeyLabelFade f;
    const int first = f.update(0, true, 1, false, false).slot;
    f.update(500 * kMs, true, 1, false, false);
    f.update(900 * kMs, true, 1, false, false);
    const int second = f.update(1000 * kMs, true, 2, false, false).slot;
    f.update(1200 * kMs, true, 2, false, false);
    REQUIRE(f.layer(first).presence == doctest::Approx(0.25f));
    REQUIRE(f.layer(second).presence == doctest::Approx(0.75f));
    // A third key 200 ms later takes the first slot, which is at 0.25: it does not drop to 0 first.
    const KeyLabelFade::Update u = f.update(1200 * kMs, true, 3, false, false);
    CHECK(u.new_label);
    CHECK(u.slot == first);
    CHECK(f.layer(first).presence == doctest::Approx(0.25f));
    f.update(1300 * kMs, true, 3, false, false);
    CHECK(f.layer(first).presence == doctest::Approx(0.5f));
    CHECK(f.layer(second).presence == doctest::Approx(0.5f));
    f.update(1700 * kMs, true, 3, false, false);
    CHECK(f.layer(first).presence == 1.0f);
    CHECK(f.layer(second).presence == 0.0f);
}

TEST_CASE("key label fade: with fades off the label is simply there") {
    KeyLabelFade f;
    const int slot = f.update(0, true, 1, false, true).slot;
    CHECK(f.layer(slot).presence == 1.0f);
    const KeyLabelFade::Update u = f.update(10 * kMs, true, 2, false, true);
    CHECK(f.layer(u.slot).presence == 1.0f);
    CHECK(f.layer(slot).presence == 0.0f);
    CHECK(f.opacity() == kKeyLabelOpacity);
    f.update(20 * kMs, true, 2, true, true); // behind: at once
    CHECK(f.opacity() == doctest::Approx(kKeyLabelOpacity * kKeyLabelBehindShare));
}

TEST_CASE("key label fade: notes passing behind it dim it and it returns") {
    KeyLabelFade f;
    f.update(0, true, 1, false, false);
    f.update(1000 * kMs, true, 1, false, false);
    CHECK(f.opacity() == kKeyLabelOpacity);
    f.update(1125 * kMs, true, 1, true, false); // half the dim time
    const float half = f.opacity();
    CHECK(half < kKeyLabelOpacity);
    CHECK(half > kKeyLabelOpacity * kKeyLabelBehindShare);
    f.update(1500 * kMs, true, 1, true, false);
    CHECK(f.opacity() == doctest::Approx(kKeyLabelOpacity * kKeyLabelBehindShare));
    f.update(2000 * kMs, true, 1, false, false);
    CHECK(f.opacity() == kKeyLabelOpacity);
}

TEST_CASE("key label fade: a clock that goes back does not run the fade backwards") {
    KeyLabelFade f;
    f.update(1000 * kMs, true, 1, false, false);
    f.update(1200 * kMs, true, 1, false, false);
    const float before = f.layer(0).presence + f.layer(1).presence;
    f.update(500 * kMs, true, 1, false, false);
    CHECK(f.layer(0).presence + f.layer(1).presence == doctest::Approx(before));
}

TEST_CASE("key label fade: reset forgets the label") {
    KeyLabelFade f;
    f.update(0, true, 1, false, true);
    REQUIRE(f.visible());
    f.reset();
    CHECK_FALSE(f.visible());
    CHECK(f.update(0, true, 1, false, true).new_label);
}
