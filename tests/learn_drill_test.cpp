// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn drill (#373) as the cards use it: the settings it is made from, the keys lit while Listen
// sounds a second octave or an arpeggio, the foot row's room, the "How it went" card's layout, and
// what the card offers.
#include "ui/drill_card_layout.hpp"
#include "ui/learn_drill.hpp"
#include "ui/learn_drill_layout.hpp"
#include "ui/learn_lit.hpp"

#include "etude/chord_diagram.hpp"
#include "etude/exercise_sound.hpp"
#include "etude/results_card.hpp"
#include "etude/scale_diagram.hpp"

#include <doctest/doctest.h>

#include <array>
#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr float kEps = 1e-3f;

bool inside(const RectF& inner, const RectF& outer) {
    return inner.x >= outer.x - kEps && inner.y >= outer.y - kEps &&
           inner.right() <= outer.right() + kEps && inner.bottom() <= outer.bottom() + kEps;
}

bool overlap(const RectF& a, const RectF& b) {
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

} // namespace

TEST_CASE("learn drill: the scale card's settings make the scale or its arpeggio") {
    AppSettings app;
    app.learn_root = 7;
    app.learn_scale = ScaleType::Major;
    app.learn_hands = LearnHands::Right;
    app.learn_tempo = 100;
    app.learn_octaves = 2;
    app.learn_direction = LearnDirection::UpDown;
    DrillSpec s = learn_drill_spec(app, false);
    CHECK(s.kind == DrillKind::Scale);
    CHECK(s.root_pc == 7);
    CHECK(s.scale == ScaleType::Major);
    CHECK(s.hands == HandsChoice::Right);
    CHECK(s.bpm == 100);
    CHECK(s.octaves == 2);
    CHECK(s.direction == Direction::UpDown);
    CHECK_FALSE(s.chord.has_value());
    app.learn_arpeggio = true;
    CHECK(learn_drill_spec(app, false).kind == DrillKind::Arpeggio);
    // A scale with no triad plays as the scale, whatever the setting says.
    app.learn_scale = ScaleType::Chromatic;
    CHECK_FALSE(learn_scale_arpeggio(app));
    CHECK(learn_drill_spec(app, false).kind == DrillKind::Scale);
    app.learn_hands = LearnHands::Both;
    CHECK(learn_drill_spec(app, false).hands == HandsChoice::Together);
    app.learn_hands = LearnHands::Left;
    CHECK(learn_drill_spec(app, false).hands == HandsChoice::Left);
    app.learn_direction = LearnDirection::Down;
    CHECK(learn_drill_spec(app, false).direction == Direction::Down);
}

TEST_CASE("learn drill: the chord card's settings make the chord's arpeggio in its inversion") {
    AppSettings app;
    app.learn_chord_root = 3;
    app.learn_chord = ChordQuality::Minor7;
    app.learn_inversion = 2;
    app.learn_arpeggio = false; // the scale card's choice does not reach the chord card
    const DrillSpec s = learn_drill_spec(app, true);
    CHECK(s.kind == DrillKind::Arpeggio);
    CHECK(s.root_pc == 3);
    REQUIRE(s.chord.has_value());
    CHECK(*s.chord == ChordQuality::Minor7);
    CHECK(s.inversion == 2);
    const Drill d = make_drill(s);
    CHECK(d.ok);
}

TEST_CASE("learn drill: every scale of every key and every card quality can be listened to and "
          "practised on a small keyboard, or says why not") {
    AppSettings app;
    for (const ScaleType type : kAllScaleTypes) {
        for (int root = 0; root < 12; ++root) {
            for (const bool arpeggio : {false, true}) {
                for (const int octaves : {1, 2}) {
                    app.learn_root = root;
                    app.learn_scale = type;
                    app.learn_arpeggio = arpeggio;
                    app.learn_octaves = octaves;
                    app.learn_hands = LearnHands::Both;
                    const Drill d = make_drill(learn_drill_spec(app, false), kRange61);
                    CAPTURE(root);
                    CAPTURE(static_cast<int>(type));
                    CAPTURE(octaves);
                    CHECK(d.ok == !d.song.notes.empty());
                    if (!d.ok) {
                        CHECK_FALSE(d.message.empty());
                    }
                }
            }
        }
    }
}

TEST_CASE("learn lit: a drill's second octave lights the keys of the first, and a chord's octave "
          "above its bass lights its bass") {
    ScaleSpec spec;
    spec.root_pc = 0;
    spec.hands = HandsChoice::Right;
    spec.octaves = 2;
    spec.direction = Direction::Up;
    const ExerciseResult r = make_scale_exercise(spec);
    REQUIRE(r.ok);
    const std::vector<ExerciseSpan> spans = exercise_spans(r.song);
    const ScaleDiagram d = scale_diagram(0, ScaleType::Major);
    // Every one of the 15 notes lights one key of the one-octave picture, the middle C and the top C
    // the diagram's top tonic.
    for (const ExerciseSpan& s : spans) {
        const LearnLit lit = learn_lit_at(spans, d, 0.5 * (s.start_s + s.end_s));
        std::size_t key = kLearnMaxKeys;
        for (std::size_t i = 0; i < kLearnMaxKeys; ++i) {
            if (lit.right[i] >= 0) {
                key = i;
            }
        }
        CAPTURE(s.semitone);
        REQUIRE(key < kLearnMaxKeys);
        CHECK(lit.right[key] == s.pitch);
        CHECK(d.keys[key].in_scale);
        // The second octave's notes light the same pitch classes as the first's.
        CHECK(d.keys[key].note % 12 == s.pitch % 12);
    }
    // The chord's arpeggio, C major from the bass up to the bass's octave: the top note lights the bass.
    ArpeggioSpec chord;
    chord.root_pc = 0;
    chord.quality = ChordQuality::Major;
    chord.hands = HandsChoice::Right;
    const ExerciseResult a = make_arpeggio_exercise(chord);
    REQUIRE(a.ok);
    const std::vector<ExerciseSpan> notes = exercise_spans(a.song);
    const ChordDiagram cd = chord_diagram(0, ChordQuality::Major, 0);
    const ExerciseSpan& top = notes.back();
    const LearnLit lit = learn_lit_at(notes, cd.picture, 0.5 * (top.start_s + top.end_s));
    REQUIRE(lit.any());
    std::size_t key = kLearnMaxKeys;
    for (std::size_t i = 0; i < kLearnMaxKeys; ++i) {
        if (lit.right[i] >= 0) {
            key = i;
        }
    }
    REQUIRE(key < kLearnMaxKeys);
    CHECK(cd.picture.keys[key].note % 12 == 0);
}

TEST_CASE("learn foot: every control keeps a hit and the row fits its room, at the unit scales a taste setting makes") {
    const float hit = tok::kHit;
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        for (const float room : {260.0f, 320.0f, 420.0f, 520.0f, 700.0f, 1000.0f}) {
            for (const float stepper : {0.0f, learn_tok::kStepperW}) {
                for (const bool words : {false, true}) {
                    const float foot_h = learn_tok::kBigScale * hit * u;
                    const FootPlan p = learn_foot_plan(room * u, foot_h, hit * u, tok::kS2 * u,
                                                       tok::kS1 * u, u, stepper, words);
                    CAPTURE(room);
                    CAPTURE(u);
                    CAPTURE(stepper);
                    CHECK(p.hand >= hit * u - kEps);
                    CHECK(p.listen >= hit * u - kEps);
                    CHECK(p.practise >= hit * u - kEps);
                    if (stepper > 0.0f) {
                        CHECK(p.stepper >= 2.0f * hit * u - kEps);
                    }
                    if (!words) {
                        CHECK_FALSE(p.words);
                    }
                    // What the row needs at the very least (a hit each) must fit, else it is
                    // allowed to be more than the room: nothing smaller could be pressed.
                    const float least = 2.0f * hit * u + tok::kS1 * u +
                                        (stepper > 0.0f ? tok::kS2 * u + 2.0f * hit * u + 0.6f * learn_tok::kTempoValueW * u : 0.0f) +
                                        2.0f * (tok::kS2 * u + hit * u);
                    if (room * u >= least) {
                        CHECK(foot_total(p, tok::kS1 * u, tok::kS2 * u) <= room * u + kEps);
                    }
                }
            }
        }
    }
}

TEST_CASE("drill card: its parts sit inside the area, the buttons a hit and apart, at every size") {
    struct Area {
        float w;
        float h;
        float u;
    };
    // The note area under the top row and over the keyboard: the 7 inch 800x480 panel, then the usual screens.
    for (const Area a : {Area{800, 300, 1.0f}, Area{800, 260, 1.25f}, Area{800, 300, 1.5f},
                         Area{1024, 380, 1.0f}, Area{1366, 560, 1.0f}, Area{1920, 800, 1.0f},
                         Area{1920, 800, 1.4f}}) {
        CAPTURE(a.w);
        CAPTURE(a.h);
        CAPTURE(a.u);
        const RectF area{0.0f, 100.0f, a.w, a.h};
        const float u = drill_card_unit(area, a.u);
        CHECK(u >= a.u - kEps);
        CHECK(u <= a.u * drill_card_tok::kMaxGrow + kEps);
        const DrillCardLayout l = drill_card_layout(area, u);
        REQUIRE(l.fits);
        CHECK(inside(l.card, area));
        const float hit = tok::kHit * u;
        for (const RectF* b : {&l.again, &l.faster, &l.to_card}) {
            CHECK(b->w >= hit - kEps);
            CHECK(b->h >= hit - kEps);
            CHECK(inside(*b, l.card));
        }
        CHECK_FALSE(overlap(l.again, l.faster));
        CHECK_FALSE(overlap(l.faster, l.to_card));
        CHECK_FALSE(overlap(l.again, l.to_card));
        for (const RectF* part : {&l.figure, &l.notes}) {
            CHECK(inside(*part, l.card));
            for (const RectF* b : {&l.again, &l.faster, &l.to_card}) {
                CHECK_FALSE(overlap(*part, *b));
            }
        }
        CHECK_FALSE(overlap(l.figure, l.notes));
        if (l.timing.w > 0.0f) {
            CHECK(inside(l.timing, l.card));
            CHECK_FALSE(overlap(l.timing, l.notes));
            CHECK_FALSE(overlap(l.timing, l.figure));
        }
        if (!l.compact) {
            CHECK(l.done.w >= hit - kEps);
            CHECK(inside(l.done, l.card));
            CHECK_FALSE(overlap(l.done, l.title));
            CHECK_FALSE(overlap(l.head, l.figure));
        }
    }
    // Where the area is too short for the head, the figure row and the buttons stay.
    const DrillCardLayout tiny = drill_card_layout({0.0f, 100.0f, 800.0f, 150.0f}, 1.0f);
    REQUIRE(tiny.fits);
    CHECK(tiny.compact);
    // And where there is not room for even those, no card is laid out (nothing is clipped).
    CHECK_FALSE(drill_card_layout({0.0f, 100.0f, 800.0f, 60.0f}, 1.0f).fits);
}

TEST_CASE("drill card: Faster and the Learn card are offered for a drill's card only") {
    ResultsCard plain;
    CHECK_FALSE(card_offers(plain, {CardAction::Faster}));
    CHECK_FALSE(card_offers(plain, {CardAction::ToCard}));
    CHECK(card_offers(plain, {CardAction::Replay}));
    ResultsCard drill;
    drill.drill_bpm = 80;
    drill.drill_title = "G major scale, RH, 1 oct";
    CHECK(card_offers(drill, {CardAction::Faster}));
    // At the cap there is no faster: the card does not offer it (a drill is never made above it).
    drill.drill_bpm = kDrillMaxBpm - kDrillBpmStep;
    CHECK(card_offers(drill, {CardAction::Faster}));
    drill.drill_bpm = kDrillMaxBpm;
    CHECK_FALSE(card_offers(drill, {CardAction::Faster}));
    CHECK(card_offers(drill, {CardAction::ToCard}));
    drill.drill_bpm = 80;
    CHECK(card_offers(drill, {CardAction::ToCard}));
    CHECK(card_offers(drill, {CardAction::Replay}));
    CHECK(card_offers(drill, {CardAction::Done}));
}
