// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn chord card's layout (learn_layout.hpp with kChordCells): the same shell as the scale
// card, so the same rules at the same windows, with 23 buttons and the inversion stepper.
#include "ui/key_ring_layout.hpp"
#include "learn_drill_util.hpp"
#include "ui/learn_layout.hpp"
#include "ui/learn_types.hpp"

#include "etude/chord_diagram.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

using namespace etude;
using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;
namespace lt = etude::app::ui::learn_tok;

namespace {

struct Window {
    float w;
    float h;
    float u;
};

// A window smaller in design points than 800x480 (a 7 inch panel at a taste scale above 1) cannot
// keep 12 mm controls with 2 mm gaps on this card, and is not supported.
constexpr std::array<Window, 6> kWindows{{{800, 480, 1.0f},
                                          {1024, 600, 1.0f},
                                          {1280, 720, 1.0f},
                                          {1366, 768, 1.0f},
                                          {1920, 1080, 1.4f},
                                          {1920, 1080, 1.0f}}};
constexpr float kEps = 1e-3f;

Box area_for(const Window& s) { return {0.0f, 57.0f * s.u, s.w, s.h}; }

bool inside(const Box& inner, const Box& outer) {
    return inner.x0 >= outer.x0 - kEps && inner.y0 >= outer.y0 - kEps &&
           inner.x1 <= outer.x1 + kEps && inner.y1 <= outer.y1 + kEps;
}
bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}
bool at_least_a_hit(const Box& b, float u) {
    return b.w() >= tok::kHit * u - kEps && b.h() >= tok::kHit * u - kEps;
}
bool empty(const Box& b) { return b.w() <= 0.0f || b.h() <= 0.0f; }

LearnCardLayout layout(const Window& s) { return learn_card_layout(area_for(s), s.u, kChordCells); }

} // namespace

TEST_CASE("learn chords: one button for every quality, and a word for each") {
    CHECK(kChordCells.count == static_cast<int>(kChordCardQualities.size()));
    CHECK(kLearnChordWords.size() == kChordCardQualities.size());
    CHECK(kLearnChordIds.size() == kChordCardQualities.size());
    CHECK(kChordCells.count <= kLearnMaxCells);
    for (std::size_t i = 0; i < kLearnChordWords.size(); ++i) {
        CHECK(std::string(kLearnChordWords[i]).size() >= 1);
        CHECK(std::string(kLearnChordWords[i]).size() <= 10);
        for (std::size_t j = i + 1; j < kLearnChordWords.size(); ++j) {
            CHECK(std::string(kLearnChordWords[i]) != std::string(kLearnChordWords[j]));
            CHECK(std::string(kLearnChordIds[i]) != std::string(kLearnChordIds[j]));
        }
        CHECK(learn_chord_index(kChordCardQualities[i]) == static_cast<int>(i));
    }
    CHECK(learn_chord_index(ChordQuality::None) == 0);
}

TEST_CASE(
    "learn chords: every control is at least a hit, inside the screen, none touching another") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = layout(s);
        std::vector<std::pair<std::string, Box>> controls;
        controls.push_back({"back", c.back});
        for (const Box& tab : c.tabs) {
            controls.push_back({"tab", tab});
        }
        if (c.ring_fits) {
            controls.push_back({"toggle", c.toggle});
        }
        controls.push_back({"slot", c.slot});
        controls.push_back({"listen", c.play});
        controls.push_back({"practise", c.practise});
        controls.push_back({"hand left", c.hand_left});
        controls.push_back({"hand right", c.hand_right});
        // The inversion stepper is in the foot row, or (no room) in the settings' card.
        if (c.stepper.w() > 0.0f) {
            controls.push_back({"stepper", c.stepper});
        } else {
            CHECK(c.column == LearnColumn::Popover);
            CHECK(c.pop.stepper.w() >= tok::kHit * s.u);
        }
        learn_test::add_drill_controls(controls, c);
        for (int i = 0; i < c.type_cells; ++i) {
            controls.push_back(
                {"quality " + std::to_string(i), c.type[static_cast<std::size_t>(i)]});
        }
        if (!c.rows) {
            controls.push_back({"strip", c.strip});
        }
        CHECK(c.type_cells == (c.rows ? kChordCells.count : 0));
        for (const auto& [name, box] : controls) {
            CAPTURE(name);
            CHECK(inside(box, area));
            CHECK(at_least_a_hit(box, s.u));
        }
        for (int i = 0; i < c.type_cells; ++i) {
            CHECK(c.type[static_cast<std::size_t>(i)].w() >= tok::kChipMinW * s.u - kEps);
        }
        for (std::size_t i = 0; i < controls.size(); ++i) {
            for (std::size_t j = i + 1; j < controls.size(); ++j) {
                INFO(controls[i].first << " and " << controls[j].first);
                CHECK_FALSE(overlap(controls[i].second, controls[j].second));
            }
        }
        // The keys with their rows of fingers, the hands' marks and the name, clear of every control.
        const Box keys{c.fingers_right.x0, c.fingers_right.y0, c.fingers_right.x1,
                       c.fingers_left.y1};
        CHECK(inside(keys, area));
        CHECK(c.keyboard.w() / 9.0f >= 28.0f * s.u);
        CHECK_FALSE(overlap(keys, c.slot));
        CHECK_FALSE(overlap(keys, c.toggle));
        CHECK_FALSE(overlap(keys, c.play));
        CHECK_FALSE(overlap(keys, c.practise));
        CHECK_FALSE(overlap(keys, c.stepper));
        CHECK_FALSE(overlap(keys, c.hand_left));
        CHECK_FALSE(overlap(keys, c.hand_right));
        CHECK_FALSE(overlap(keys, c.name));
        CHECK(inside(c.name, area));
        CHECK_FALSE(overlap(c.name, c.slot));
        for (int i = 0; i < c.type_cells; ++i) {
            CHECK_FALSE(overlap(keys, c.type[static_cast<std::size_t>(i)]));
        }
        CHECK(c.title.w() > 0.0f);
        CHECK_FALSE(overlap(c.title, c.back));
        CHECK_FALSE(overlap(c.title, c.toggle));
        for (const Box& tab : c.tabs) {
            CHECK_FALSE(overlap(c.title, tab));
        }
    }
}

TEST_CASE("learn chords: the same structure as the scale card, the qualities in rows of eight or "
          "a strip, the foot row centred under the keyboard") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = layout(s);
        const LearnCardLayout scales = learn_card_layout(area, s.u);
        const float ub = learn_card_unit(area, s.u);
        // The left column and the keyboard's column are the scale card's, which has a row more of
        // settings (scale / arpeggio), so its picker may be a little smaller.
        CHECK(c.slot.x0 == doctest::Approx(scales.slot.x0));
        CHECK(c.slot.w() >= scales.slot.w() - kEps);
        CHECK(c.keyboard.x1 == doctest::Approx(scales.keyboard.x1));
        if (!c.name_in_header) {
            CHECK(c.name.cx() == doctest::Approx(c.slot.cx()));
        }
        CHECK(c.keyboard.h() <= c.keyboard.w() / 8.0f * lt::kKeyAspect + kEps);
        // The foot (the hands, the stepper, Listen, Practice) is a group centred under the keyboard.
        CHECK(c.hand_left.x1 <= c.hand_right.x0 + kEps);
        const float first_after_hands = c.stepper.w() > 0.0f ? c.stepper.x0 : c.play.x0;
        CHECK(c.hand_right.x1 <= first_after_hands + kEps);
        CHECK(c.play.x1 <= c.practise.x0 + kEps);
        CHECK((c.hand_left.x0 + c.practise.x1) * 0.5f ==
              doctest::Approx(c.keyboard.cx()).epsilon(0.001));
        CHECK(c.play.y0 >= c.fingers_left.y1 - kEps);
        if (!c.rows) {
            CHECK(c.strip.y1 <= c.fingers_right.y0 + kEps);
            CHECK(c.strip.x0 == doctest::Approx(c.keyboard.x0));
            continue;
        }
        // Eight to a row, three rows, each cell holds the words and is bigger than a hit.
        for (int i = 0; i < kChordCells.count; ++i) {
            const Box& cell = c.type[static_cast<std::size_t>(i)];
            CAPTURE(i);
            CHECK(cell.w() >= kChordCells.cell_min * ub - kEps);
            CHECK(cell.h() == doctest::Approx(lt::kBigScale * tok::kHit * ub));
            CHECK(cell.y1 <= c.fingers_right.y0 + kEps);
            CHECK(cell.x0 >= c.keyboard.x0 - kEps);
            CHECK(cell.x1 <= c.keyboard.x1 + kEps);
        }
        CHECK(c.type[8].y0 >= c.type[0].y1 - kEps);
        CHECK(c.type[16].y0 >= c.type[8].y1 - kEps);
        CHECK(c.type[7].x1 == doctest::Approx(c.keyboard.x1));
    }
    // Rows at 1366x768 and 1920x1080; the strip at 800x480, where three rows do not fit.
    for (const Window s : {Window{1366, 768, 1.0f}, Window{1920, 1080, 1.0f}, Window{1920, 1080, 1.4f}}) {
        CAPTURE(s.w);
        CHECK(layout(s).rows);
    }
    CHECK_FALSE(layout(Window{800, 480, 1.0f}).rows);
}

TEST_CASE(
    "learn chords: the picker is the scale card's, ring or buttons, and the 1.5 fallback holds") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const LearnCardLayout c = layout(s);
        CHECK(inside(c.slot, area));
        if (c.ring_fits) {
            const KeyRingLayout ring = key_ring_layout(c.slot, s.u);
            CHECK(ring.fits);
            CHECK(ring.key >= tok::kHit * s.u - kEps);
        } else {
            CHECK(empty(c.toggle));
            // A 7 inch panel at a taste scale above 1 has no room for twelve 12 mm buttons.
            const bool supported = s.h >= 600.0f || s.u <= 1.0f;
            for (const Box& b : learn_key_grid(c.slot, s.u)) {
                CHECK((!supported || at_least_a_hit(b, s.u)));
                CHECK(inside(b, c.slot));
            }
        }
    }
    const Window dense{800, 480, 1.5f};
    CHECK_FALSE(layout(dense).ring_fits);
    CHECK(layout(dense).column == LearnColumn::Popover);
    CHECK_FALSE(layout(dense).rows);
}

TEST_CASE("learn chords: the scale card is unchanged by the shared layout") {
    const Window s{1366, 768, 1.0f};
    const LearnCardLayout scales = learn_card_layout(area_for(s), s.u);
    CHECK(scales.type_cells == kLearnTypes);
    CHECK(scales.play.w() == doctest::Approx(lt::kListenW));
}

TEST_CASE("learn chords: the ring's centre shows the selected chord's root and quality, from the "
          "same label as the title") {
    // The words for the common qualities, as the free-play label writes them.
    const auto word = [](int root, ChordQuality q, int inversion = 0) {
        return learn_chord_centre_word(chord_diagram(root, q, inversion).name);
    };
    CHECK(word(0, ChordQuality::Major) == "major");
    CHECK(word(0, ChordQuality::Minor) == "minor");
    CHECK(word(10, ChordQuality::Dominant7) == "7");
    CHECK(word(0, ChordQuality::Major7) == "maj7");
    CHECK(word(2, ChordQuality::Minor7) == "m7");
    CHECK(word(11, ChordQuality::HalfDiminished7) == "m7b5");
    // Every quality, root and inversion: the word is the label's quality (major and minor spelled
    // out), never empty, and the title's name is the same label's root.
    for (const ChordQuality q : kChordCardQualities) {
        for (int root = 0; root < 12; ++root) {
            for (int inv = 0; inv < chord_inversion_count(q); ++inv) {
                CAPTURE(static_cast<int>(q));
                CAPTURE(root);
                CAPTURE(inv);
                const ChordNameParts name = chord_diagram(root, q, inv).name;
                REQUIRE_FALSE(name.empty());
                const std::string w = learn_chord_centre_word(name);
                CHECK_FALSE(w.empty());
                if (name.quality.empty()) {
                    CHECK(w == "major");
                } else if (name.quality == "m") {
                    CHECK(w == "minor");
                } else {
                    CHECK(w == name.quality);
                }
            }
        }
    }
    // Choosing another quality changes the word: each of the 23 differs from every other that the
    // label also tells apart.
    for (std::size_t i = 0; i < kChordCardQualities.size(); ++i) {
        for (std::size_t j = i + 1; j < kChordCardQualities.size(); ++j) {
            const std::string a = word(0, kChordCardQualities[i]);
            const std::string b = word(0, kChordCardQualities[j]);
            const ChordNameParts na = chord_diagram(0, kChordCardQualities[i], 0).name;
            const ChordNameParts nb = chord_diagram(0, kChordCardQualities[j], 0).name;
            CHECK((a == b) == (na.quality == nb.quality));
        }
    }
}
