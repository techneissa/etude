// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn drill (#373) as the cards use it: the settings it is made from, the keys lit while Listen
// sounds a second octave or an arpeggio, and what the card offers.
#include "ui/learn_drill.hpp"
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
