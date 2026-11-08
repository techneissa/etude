// SPDX-License-Identifier: GPL-3.0-or-later
#include "atkinson_metrics.hpp"
#include "etude/exercise_sound.hpp"
#include "etude/exercises.hpp"
#include "etude/scale_diagram.hpp"
#include "ui/learn_layout.hpp"
#include "ui/learn_lit.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace etude;
using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;

namespace {

ExerciseResult scale(HandsChoice hands, int root_pc = 7) {
    ScaleSpec spec;
    spec.root_pc = root_pc;
    spec.hands = hands;
    spec.direction = Direction::UpDown;
    spec.octaves = 1;
    const ExerciseResult r = make_scale_exercise(spec);
    REQUIRE(r.ok);
    return r;
}

std::size_t lit_count(const LearnLit& l) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < kLearnMaxKeys; ++i) {
        n += (l.right[i] >= 0 ? 1 : 0) + (l.left[i] >= 0 ? 1 : 0);
    }
    return n;
}

} // namespace

TEST_CASE("learn lit: the lit set at time t is the notes sounding at t") {
    const ExerciseResult r = scale(HandsChoice::Together);
    const std::vector<ExerciseSpan> spans = exercise_spans(r.song);
    const ScaleDiagram d = scale_diagram(7, ScaleType::Major);
    REQUIRE(spans.size() == r.song.notes.size());
    for (const Note& n : r.song.notes) {
        const double mid = n.start_s + 0.5 * std::max(n.duration_s, kMinNoteSeconds);
        std::size_t sounding = 0;
        for (const Note& m : r.song.notes) {
            sounding += (mid >= m.start_s && mid < m.start_s + std::max(m.duration_s, kMinNoteSeconds)) ? 1 : 0;
        }
        const LearnLit lit = learn_lit_at(spans, d, mid);
        CAPTURE(mid);
        CHECK(lit_count(lit) == sounding);
        // Each hand's key lights with the pitch that hand is playing.
        const bool left = n.hand == Hand::Left;
        const auto& slot = left ? lit.left : lit.right;
        CHECK(std::count(slot.begin(), slot.end(), static_cast<std::int16_t>(n.pitch)) == 1);
    }
}

TEST_CASE("learn lit: nothing is lit before the first note or after the last, and a key lets go "
          "when its note ends") {
    const ExerciseResult r = scale(HandsChoice::Right);
    const std::vector<ExerciseSpan> spans = exercise_spans(r.song);
    const ScaleDiagram d = scale_diagram(7, ScaleType::Major);
    CHECK_FALSE(learn_lit_at(spans, d, -0.1).any());
    CHECK_FALSE(learn_lit_at(spans, d, exercise_length_s(r.song) + 0.01).any());
    const ExerciseSpan& first = spans.front();
    CHECK(learn_lit_at(spans, d, first.start_s).any());
    // Just after the first note ends the scale is on its second (a scale has no rests): its key is
    // not the first's any more.
    const LearnLit after = learn_lit_at(spans, d, first.end_s + 1e-9);
    CHECK(after.right[0] < 0);
}

TEST_CASE("learn lit: hands together light both hands' keys, an octave apart, at once") {
    const ExerciseResult r = scale(HandsChoice::Together);
    const std::vector<ExerciseSpan> spans = exercise_spans(r.song);
    const ScaleDiagram d = scale_diagram(7, ScaleType::Major);
    const LearnLit lit = learn_lit_at(spans, d, spans.front().start_s + 0.01);
    CHECK(lit_count(lit) == 2);
    // Both hands sound the same key of the diagram: the tonic at the bottom.
    std::size_t right_key = kLearnMaxKeys;
    std::size_t left_key = kLearnMaxKeys;
    for (std::size_t i = 0; i < kLearnMaxKeys; ++i) {
        right_key = lit.right[i] >= 0 ? i : right_key;
        left_key = lit.left[i] >= 0 ? i : left_key;
    }
    REQUIRE(right_key < kLearnMaxKeys);
    CHECK(right_key == left_key);
    CHECK(d.keys[right_key].root);
}

TEST_CASE("learn lit: the badge pulse is 1 as a note starts and gone a quarter second later") {
    const ExerciseResult r = scale(HandsChoice::Right);
    const std::vector<ExerciseSpan> spans = exercise_spans(r.song);
    const ScaleDiagram d = scale_diagram(7, ScaleType::Major);
    const ExerciseSpan& s = spans[2];
    const auto key_of = [&](const LearnLit& l) {
        for (std::size_t i = 0; i < kLearnMaxKeys; ++i) {
            if (l.right[i] >= 0) {
                return i;
            }
        }
        return kLearnMaxKeys;
    };
    const LearnLit at_start = learn_lit_at(spans, d, s.start_s);
    REQUIRE(key_of(at_start) < kLearnMaxKeys);
    CHECK(at_start.right_pulse[key_of(at_start)] == doctest::Approx(1.0f));
    const double late = s.start_s + kLearnPulseS + 0.01;
    if (s.sounding_at(late)) {
        const LearnLit l = learn_lit_at(spans, d, late);
        CHECK(l.right_pulse[key_of(l)] == doctest::Approx(0.0f));
    }
}

TEST_CASE("learn tabs: every tab is named while all three fit with room for the title, else "
          "only the shown one") {
    struct Win {
        float w;
        float h;
        float u;
    };
    // The measured words of the tabs (an icon and the name in the real font) at 1x, and a font a
    // third and then twice as wide: the wider the words, the sooner the others drop to icons.
    const std::array<float, kLearnTabs> measured{82.0f, 85.0f, 66.0f, 96.0f};
    for (const Win s : {Win{800, 480, 1.0f}, Win{800, 480, 1.5f}, Win{1366, 768, 1.0f},
                        Win{1920, 1080, 1.0f}}) {
        for (const float stretch : {1.0f, 1.33f, 2.0f}) {
            for (int selected = 0; selected < kLearnTabs; ++selected) {
                CAPTURE(s.w);
                CAPTURE(s.u);
                CAPTURE(stretch);
                CAPTURE(selected);
                LearnTabs tabs;
                tabs.selected = selected;
                for (std::size_t i = 0; i < measured.size(); ++i) {
                    tabs.need[i] = measured[i] * stretch;
                }
                const Box area{0.0f, 57.0f * s.u, s.w, s.h};
                const LearnCardLayout c = learn_card_layout(area, s.u, kScaleCells, tabs);
                const float hit = tok::kHit * s.u;
                int named = 0;
                for (std::size_t i = 0; i < kLearnTabs; ++i) {
                    named += c.tab_label[i] ? 1 : 0;
                    // A tab with its name is as wide as the name measured; one without is a hit.
                    if (c.tab_label[i]) {
                        CHECK(c.tabs[i].w() >= tabs.need[i] * s.u - 1e-3f);
                    } else {
                        CHECK(c.tabs[i].w() == doctest::Approx(hit));
                    }
                    CHECK(c.tabs[i].w() >= hit - 1e-3f);
                    CHECK(c.tabs[i].x0 >= c.back.x1);
                    if (i > 0) {
                        CHECK(c.tabs[i - 1].x1 <= c.tabs[i].x0 + 1e-3f);
                    }
                }
                const bool all_or_one = named == kLearnTabs || named == 1;
                CHECK(all_or_one);
                CHECK(c.tab_label[static_cast<std::size_t>(selected)]);
                // The page's name always keeps its room.
                CHECK(c.title.w() >= 0.0f);
            }
        }
    }
}

TEST_CASE("learn tabs: an upper bound of the names' widths, from the font metrics table, still fits "
          "at 800x480 at 1x, and keeps the shown tab at 1.5x") {
    // A tab is its icon and name between pads (`chip_width`). The advances table of the font
    // (`atkinson_metrics.hpp`, an em of the font's own) is wider than the loaded font's size gives
    // (an ImGui size is the line's height, not the em), so these widths are an upper bound of what
    // the app measures; the layout's defaults (what the app measured: 82, 85, 66, 96) stay under them
    // and the names fit with the bound as well.
    constexpr std::array<const char*, kLearnTabs> kNames{"Scales", "Chords", "Ear", "Melodies"}; // kLearnTabInfo
    LearnTabs bound;
    const LearnTabs defaults;
    for (std::size_t i = 0; i < kLearnTabs; ++i) {
        const float words = etude::test::text_em(false, kNames[i]) * tok::kLabel;
        const float chip = 2.0f * tok::kS3 + words + tok::kIconSm + tok::kS2 + tok::kHairline;
        bound.need[i] = std::max(tok::kChipMinW, chip);
        CAPTURE(i);
        CHECK(defaults.need[i] <= bound.need[i]);
    }
    // All four names fit the 7-inch 800x480 panel at 1x. At 1.5x, the 7 inch panel's own scale and
    // with the upper bound of the widths, the shown tab keeps its name and the others may be icons.
    {
        const Box a{0.0f, 57.0f, 800.0f, 480.0f};
        const LearnCardLayout c = learn_card_layout(a, 1.0f, kScaleCells, bound);
        for (std::size_t i = 0; i < kLearnTabs; ++i) {
            CAPTURE(i);
            CHECK(c.tab_label[i]);
        }
    }
    {
        const Box a{0.0f, 57.0f * 1.5f, 800.0f, 480.0f};
        const LearnCardLayout c = learn_card_layout(a, 1.5f, kScaleCells, bound);
        CHECK(c.tab_label[static_cast<std::size_t>(bound.selected)]);
    }
}

TEST_CASE("learn tabs: names a half wider again than measured drop all but the shown tab's to "
          "icons at 800x480") {
    LearnTabs tabs;
    for (std::size_t i = 0; i < tabs.need.size(); ++i) {
        tabs.need[i] *= 1.9f;
    }
    const Box a{0.0f, 57.0f * 1.5f, 800.0f, 480.0f};
    const LearnCardLayout c = learn_card_layout(a, 1.5f, kScaleCells, tabs);
    CHECK_FALSE(c.tab_label[1]);
    CHECK_FALSE(c.tab_label[2]);
    CHECK(c.tab_label[0]);
}
