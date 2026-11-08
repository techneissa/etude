// SPDX-License-Identifier: GPL-3.0-or-later
#include "learn_sound.hpp"

#include "progress_fixtures.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

// Up and down, one octave, at 80 a minute: what the scale card played before it had settings.
DrillSpec scale(int root, ScaleType type, HandsChoice hands) {
    DrillSpec s;
    s.root_pc = root;
    s.scale = type;
    s.hands = hands;
    s.direction = Direction::UpDown;
    return s;
}

double listen(LearnSound& sound, const DrillSpec& spec) {
    std::string why;
    return sound.listen(spec, why);
}

std::size_t notes_starting(const std::vector<SongSoundEvent>& events) {
    std::size_t n = 0;
    for (const SongSoundEvent& e : events) {
        n += e.kind == SongSoundEvent::Kind::Message && starts_note(e.message) ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("learn sound: a drill goes out as a stop, then its notes due a look-ahead after now") {
    LearnSound sound;
    sound.set_frame(10'000'000'000, kRange61);
    // G major up and down in both hands: 8 notes up and 7 down in each hand.
    const double seconds = listen(sound, scale(7, ScaleType::Major, HandsChoice::Together));
    CHECK(seconds == doctest::Approx(15 * 0.375 + 0.030).epsilon(0.01));
    std::vector<SongSoundEvent> out;
    sound.take(out);
    REQUIRE_FALSE(out.empty());
    CHECK(out.front().kind == SongSoundEvent::Kind::StopAll);
    CHECK(notes_starting(out) == 30);
    // The first note is due a look-ahead after the frame's time, never before it.
    std::int64_t first = INT64_MAX;
    for (const SongSoundEvent& e : out) {
        if (e.kind == SongSoundEvent::Kind::Message) {
            first = std::min(first, e.message.at_ns);
        }
    }
    CHECK(first == 10'000'000'000 + kSongSoundLeadNs);
    // Taken once: nothing is left to send.
    std::vector<SongSoundEvent> again;
    sound.take(again);
    CHECK(again.empty());
    CHECK(sound.scales_started() == 1);
    CHECK(sound.messages_sent() == static_cast<long>(out.size()));
}

TEST_CASE("learn sound: one hand plays half the notes, and a hand's scale stays on its side of "
          "middle C") {
    LearnSound sound;
    sound.set_frame(0, kRange61);
    std::vector<SongSoundEvent> right;
    listen(sound, scale(0, ScaleType::Major, HandsChoice::Right));
    sound.take(right);
    std::vector<SongSoundEvent> left;
    listen(sound, scale(0, ScaleType::Major, HandsChoice::Left));
    sound.take(left);
    CHECK(notes_starting(right) == 15);
    CHECK(notes_starting(left) == 15);
    for (const SongSoundEvent& e : right) {
        if (starts_note(e.message)) {
            CHECK(e.message.data >= 60);
        }
    }
    for (const SongSoundEvent& e : left) {
        if (starts_note(e.message)) {
            CHECK(e.message.data <= 60);
        }
    }
}

TEST_CASE("learn sound: every scale type of every key plays inside a 61-key keyboard") {
    LearnSound sound;
    sound.set_frame(0, kRange61);
    for (const ScaleType type : kAllScaleTypes) {
        for (int root = 0; root < 12; ++root) {
            CAPTURE(root);
            CAPTURE(static_cast<int>(type));
            CHECK(listen(sound, scale(root, type, HandsChoice::Together)) > 0.0);
            std::vector<SongSoundEvent> out;
            sound.take(out);
            for (const SongSoundEvent& e : out) {
                if (e.kind == SongSoundEvent::Kind::Message) {
                    CHECK(e.message.data >= kRange61.lowest);
                    CHECK(e.message.data <= kRange61.highest);
                }
            }
        }
    }
}

TEST_CASE("learn sound: a new scale replaces one not taken yet, and stop leaves only a stop") {
    LearnSound sound;
    sound.set_frame(0, kRange61);
    listen(sound, scale(0, ScaleType::Major, HandsChoice::Together));
    listen(sound, scale(2, ScaleType::Dorian, HandsChoice::Right));
    std::vector<SongSoundEvent> out;
    sound.take(out);
    CHECK(notes_starting(out) == 15); // the second scale alone
    listen(sound, scale(0, ScaleType::Major, HandsChoice::Together));
    sound.stop_sound();
    out.clear();
    sound.take(out);
    REQUIRE(out.size() == 1);
    CHECK(out.front().kind == SongSoundEvent::Kind::StopAll);
}

TEST_CASE("learn sound: a keyboard too small for the scale plays nothing and says so") {
    LearnSound sound;
    sound.set_frame(0, KeyRange{60, 66}); // seven keys
    CHECK(listen(sound, scale(0, ScaleType::Major, HandsChoice::Together)) == 0.0);
    std::vector<SongSoundEvent> out;
    sound.take(out);
    CHECK(out.empty());
    CHECK(sound.scales_started() == 0);
}

TEST_CASE("learn sound: tempo, octaves and the arpeggio change what Listen plays") {
    LearnSound sound;
    sound.set_frame(0, kRange61);
    DrillSpec s = scale(7, ScaleType::Major, HandsChoice::Right);
    s.direction = Direction::Up;
    s.octaves = 2;
    s.bpm = 160;
    const double seconds = listen(sound, s);
    // 15 notes, an eighth each at 160 a minute: 0.1875 s apart.
    CHECK(seconds == doctest::Approx(15 * 0.1875 + 0.030).epsilon(0.01));
    std::vector<SongSoundEvent> out;
    sound.take(out);
    CHECK(notes_starting(out) == 15);
    // The arpeggio of G major: G B D G, in the right hand, one octave up.
    DrillSpec a = scale(7, ScaleType::Major, HandsChoice::Right);
    a.kind = DrillKind::Arpeggio;
    a.direction = Direction::Up;
    CHECK(listen(sound, a) > 0.0);
    out.clear();
    sound.take(out);
    CHECK(notes_starting(out) == 4);
    // The chord card's arpeggio, a seventh chord in its first inversion: four tones and the octave.
    DrillSpec c;
    c.kind = DrillKind::Arpeggio;
    c.chord = ChordQuality::Major7;
    c.inversion = 1;
    c.hands = HandsChoice::Right;
    CHECK(listen(sound, c) > 0.0);
    out.clear();
    sound.take(out);
    CHECK(notes_starting(out) == 5);
}

TEST_CASE("learn sound: Listen says why a drill cannot be played") {
    LearnSound sound;
    sound.set_frame(0, kRange61);
    DrillSpec s = scale(0, ScaleType::Chromatic, HandsChoice::Right);
    s.kind = DrillKind::Arpeggio;
    std::string why;
    CHECK(sound.listen(s, why) == 0.0);
    CHECK(why.find("Chromatic") != std::string::npos);
    std::vector<SongSoundEvent> out;
    sound.take(out);
    CHECK(out.empty());
}

TEST_CASE("learn sound: Practice leaves one request for the window, which answers it once") {
    LearnSound sound;
    sound.set_frame(0, kRange61);
    CHECK_FALSE(sound.take_drill().has_value());
    std::string why;
    DrillSpec s = scale(5, ScaleType::NaturalMinor, HandsChoice::Together);
    s.bpm = 95;
    REQUIRE(sound.practise(s, why));
    // Nothing sounds for it: the player plays the drill.
    std::vector<SongSoundEvent> out;
    sound.take(out);
    CHECK(out.empty());
    const std::optional<DrillRequest> asked = sound.take_drill();
    REQUIRE(asked.has_value());
    CHECK(asked->spec.root_pc == 5);
    CHECK(asked->spec.scale == ScaleType::NaturalMinor);
    CHECK(asked->spec.bpm == 95);
    // The drill comes made, once, with the request: the window opens that one.
    CHECK(asked->drill.ok);
    CHECK_FALSE(asked->drill.song.notes.empty());
    CHECK(asked->drill.hash == make_drill(s, kRange61).hash);
    CHECK_FALSE(sound.take_drill().has_value());
}

TEST_CASE("learn sound: Practice on a keyboard too small for the drill refuses with a message") {
    LearnSound sound;
    sound.set_frame(0, KeyRange{60, 66});
    std::string why;
    CHECK_FALSE(sound.practise(scale(0, ScaleType::Major, HandsChoice::Together), why));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(sound.take_drill().has_value());
}

TEST_CASE("learn sound: an Ear question goes out as a stop and its notes, a second each, with the "
          "keys left dark") {
    LearnSound sound;
    sound.set_frame(5'000'000'000, kRange61);
    std::string why;
    EarQuestion note;
    note.first = note.second = 64;
    CHECK(sound.ask_ear(note, why) == doctest::Approx(1.030).epsilon(0.01));
    std::vector<SongSoundEvent> out;
    sound.take(out);
    REQUIRE_FALSE(out.empty());
    CHECK(out.front().kind == SongSoundEvent::Kind::StopAll);
    CHECK(notes_starting(out) == 1);
    CHECK(out[1].message.at_ns == 5'000'000'000 + kSongSoundLeadNs);
    CHECK(sound.listening().empty()); // nothing lit before the answer

    EarQuestion up;
    up.kind = EarQuestionKind::Interval;
    up.first = 60;
    up.second = 67;
    CHECK(sound.ask_ear(up, why) == doctest::Approx(2.030).epsilon(0.01));
    out.clear();
    sound.take(out);
    CHECK(notes_starting(out) == 2);
    // One after the other: the second note is due a second after the first.
    std::vector<std::int64_t> starts;
    for (const SongSoundEvent& e : out) {
        if (e.kind == SongSoundEvent::Kind::Message && starts_note(e.message)) {
            starts.push_back(e.message.at_ns);
        }
    }
    REQUIRE(starts.size() == 2);
    CHECK(starts[1] - starts[0] == 1'000'000'000);

    up.harmonic = true;
    CHECK(sound.ask_ear(up, why) == doctest::Approx(2.030).epsilon(0.01));
    out.clear();
    sound.take(out);
    std::vector<std::int64_t> together;
    for (const SongSoundEvent& e : out) {
        if (e.kind == SongSoundEvent::Kind::Message && starts_note(e.message)) {
            together.push_back(e.message.at_ns);
        }
    }
    REQUIRE(together.size() == 2);
    CHECK(together[0] == together[1]);
    CHECK(sound.questions_sounded() == 3);
}

TEST_CASE("learn sound: a tapped Ear key sounds for half a second and does not stop the question") {
    LearnSound sound;
    sound.set_frame(1'000'000'000, kRange61);
    sound.sound_key(72);
    std::vector<SongSoundEvent> out;
    sound.take(out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].kind == SongSoundEvent::Kind::Message);
    CHECK(starts_note(out[0].message));
    CHECK(out[0].message.data == 72);
    CHECK(ends_note(out[1].message));
    CHECK(out[1].message.at_ns - out[0].message.at_ns == 500'000'000);
}

namespace {

// A run of the piece `hash` with `graded` graded notes of which `missed` were missed.
RunRecord melody_run(std::uint64_t hash, int graded, int missed) {
    etude::testing::RunSpec spec;
    spec.song = hash;
    spec.graded = graded;
    spec.missed = missed;
    return etude::testing::make(spec);
}

} // namespace

TEST_CASE("learn sound: the melodies' best scores come from the runs' accuracy by file hash, and only a "
          "run of enough notes counts") {
    LearnSound sound;
    const MelodyBook& book = sound.melodies();
    REQUIRE(book.pieces.size() >= 14);
    const std::vector<RunRecord> runs{melody_run(book.pieces[2].hash, 40, 2),   // 95%
                                      melody_run(book.pieces[3].hash, 40, 20),  // 50%
                                      melody_run(book.pieces[4].hash, 5, 0),    // too short to count
                                      melody_run(12345, 40, 0)};                // not a melody
    sound.update_melody_progress(runs, 1);
    CHECK(book.pieces[2].best_permille == 950);
    CHECK(book.pieces[3].best_permille == 500);
    CHECK(book.pieces[4].best_permille == -1);
    CHECK(book.pieces[0].best_permille == -1);
}

TEST_CASE("learn sound: the best scores are taken again when the runs or the profile change, and not "
          "before the melodies are asked for") {
    LearnSound sound;
    sound.update_melody_progress({}, 1); // nothing to update yet, and nothing breaks
    const MelodyBook& book = sound.melodies();
    std::vector<RunRecord> runs{melody_run(book.pieces[1].hash, 40, 0)};
    sound.update_melody_progress(runs, 1);
    CHECK(book.pieces[1].best_permille == 1000);
    // Another profile with no runs: the scores go.
    sound.update_melody_progress({}, 2);
    CHECK(book.pieces[1].best_permille == -1);
    // A new run for the same profile.
    runs.push_back(melody_run(book.pieces[1].hash, 40, 4));
    sound.update_melody_progress(runs, 2);
    CHECK(book.pieces[1].best_permille == 1000);
}

TEST_CASE("learn sound: a melody the player taps is handed over once, ready to play, and a piece that is "
          "not there is refused") {
    LearnSound sound;
    std::string why;
    CHECK_FALSE(sound.take_melody().has_value());
    REQUIRE(sound.play_melody(2, why));
    const std::optional<OpenedMelody> opened = sound.take_melody();
    REQUIRE(opened.has_value());
    CHECK(opened->song.title == sound.melodies().pieces[2].title);
    CHECK(opened->song.hash == sound.melodies().pieces[2].hash);
    CHECK_FALSE(sound.take_melody().has_value());
    CHECK(sound.melodies_opened() == 1);
    CHECK_FALSE(sound.play_melody(99, why));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(sound.take_melody().has_value());
}

TEST_CASE("learn sound: the demo scores fill the first paths with passed, started and new pieces") {
    LearnSound sound;
    sound.use_demo_scores();
    const MelodyBook& book = sound.melodies();
    sound.update_melody_progress({}, 1);
    const MelodyPath& first = book.paths[0];
    CHECK(book.pieces[first.pieces[0]].best_permille >= 900);
    CHECK(book.pieces[first.pieces[3]].best_permille > 0);
    CHECK(book.pieces[first.pieces[3]].best_permille < 900);
    CHECK(book.pieces[first.pieces[5]].best_permille == -1);
}

TEST_CASE("learn sound: the gallery's counts come from the manifest alone, with no file read") {
    LearnSound sound;
    const LearnSound::MelodyCounts counts = sound.melody_counts();
    CHECK(counts.paths >= 3);
    CHECK(counts.pieces >= 14);
    // The pieces are read only when the page asks for them.
    CHECK(sound.melodies().pieces.size() == static_cast<std::size_t>(counts.pieces));
}
