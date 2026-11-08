// SPDX-License-Identifier: GPL-3.0-or-later
// The Ear card's session (#374): which question is asked, whether it has been answered right or wrong,
// and the streak. Std only: no sound, no drawing, no clock, so tests/ear_session_test.cpp drives it
// with a seed. The card draws it (app/ui/learn_ear_card.cpp) and the Learn port sounds the question
// (core `ear_question_song`).
//
// A question is drawn from the notes on the keys shown (the C the keys start on and how many white
// keys): the piano plays a note, or a note and a second one up, down or together, and the player finds
// the note (or the second note) on the keys. One try: a press that is not the shown starting note ends
// the question, right or wrong, and Next asks the next. Hint shows the answer, and then the question
// neither adds to the streak nor breaks it.
#pragma once

#include "etude/ear_training.hpp"
#include "etude/midi_event.hpp"
#include "etude/session_state.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace etude::app {

// Whether the Ear card takes `ev` as an answer: a key pressed on a MIDI keyboard or on the computer's
// or the screen's keys. Never the microphone (a sung or played-in-the-room note is not an answer, and
// the card's own question, sounding, would be heard as one). A note-on with no velocity is a release.
inline bool ear_takes_answer(const MidiEvent& ev) {
    return ev.type == MidiEventType::NoteOn && ev.data2 > 0 && ev.source != InputSource::Microphone;
}

// The ten dots of the streak row: the last ten answers, oldest first.
inline constexpr int kEarDots = 10;

// What the card shows and asks. The keys start on the C `low_note` and run `whites` white keys; every
// note asked is on them. `whites` is what the window shows, not what is asked: a window that is resized
// (a different count) keeps the question in hand, and equal configs are those that ask the same (the
// next question is drawn from the keys then shown).
struct EarConfig {
    LearnEarMode mode = LearnEarMode::Note;
    int low_note = 60; // a C
    int whites = 15;
    bool any_octave = false;
    int max_interval = 12; // the smallest is a semitone

    friend bool operator==(const EarConfig& a, const EarConfig& b) {
        return a.mode == b.mode && a.low_note == b.low_note && a.any_octave == b.any_octave &&
               a.max_interval == b.max_interval;
    }
};

inline constexpr int kEarMinWhites = 8;  // a C to the C above
inline constexpr int kEarMaxWhites = 15; // two octaves and a key

// The note of white key `index` (0 is `low_note`), and the top of the keys: `low_note` is a C.
int ear_white_note(int low_note, int index);
inline int ear_top_note(const EarConfig& c) { return ear_white_note(c.low_note, c.whites - 1); }
// The index of the white key at or below `note`, counted from the C `low_note` (a black key's is the
// white key under its left edge).
int ear_white_index(int low_note, int note);
// The C of octave `octave` (C4 is 60).
inline int ear_c_note(int octave) { return 12 * (octave + 1); }
// The settings the questions are drawn with.
EarSettings ear_settings(const EarConfig& c);

enum class EarPhase : std::uint8_t {
    Idle,  // no question yet (or the settings changed): `next` asks one
    Asked, // waiting for the player's answer
    Right,
    Wrong,
};

enum class EarMark : std::uint8_t { None, Right, Wrong, Hinted };

// What a key is once the question is answered (or hinted): only the keys of the answer have a role, so
// with any octave judged right the other octaves' keys of the same note have none.
enum class EarKeyRole : std::uint8_t {
    Plain,
    Tapped, // the key the player answered with (right or wrong: `EarSession::phase`)
    Missed, // the right key, in the octave asked, after a wrong tap
    Start,  // the note an interval started from
    Hint,   // the right key, hinted
};

class EarSession {
  public:
    explicit EarSession(std::uint64_t seed = 1);

    // Sets what is asked. A change drops the question in hand (Idle) and keeps the streak; the same
    // configuration changes nothing. True when it changed. A different `whites` alone (the window was
    // resized) is no change while the question in hand still fits the keys then shown: it stays, the
    // keys on view (`view_low`) move if its notes would fall off them, and the next question is drawn
    // from the keys then shown. A question that no longer fits (its notes, with the white keys a
    // black one needs beside it, span more white keys than are shown) is dropped as a change is
    // (true, Idle): the next question is asked from the keys on view.
    bool configure(const EarConfig& config);
    const EarConfig& config() const { return config_; }
    // The keys on view: the white key the view starts on (a white key at or above `low_note`, counted
    // in white keys from it) and the notes of its first and last key. They start at `low_note` and
    // move only when a resize would hide a note of the question.
    int view_first() const { return view_first_; }
    int view_low() const { return ear_white_note(config_.low_note, view_first_); }
    int view_top() const { return ear_white_note(config_.low_note, view_first_ + config_.whites - 1); }

    // Asks the next question: from Idle, or after an answer (not while one waits: there is no
    // skipping). False when the question could not be asked, with `message` saying why.
    bool next();
    const std::string& message() const { return message_; }

    EarPhase phase() const { return phase_; }
    const EarQuestion& question() const { return question_; }
    // The question again, for the sound: null when none is in hand. Counts the replay.
    const EarQuestion* replay();
    int replays() const { return replays_; }

    // The player pressed `note`. A note off the keys on view, or the starting note of an interval (the
    // reference, not an answer), is ignored; else the question is answered. The phase after.
    EarPhase press(int note);
    // The key the player answered with (-1 before an answer).
    int answered() const { return answered_; }

    // Shows the answer. True when it took: a question is waiting and has not been hinted.
    bool hint();
    bool hinted() const { return hinted_; }
    // The note whose key the hint (or the result) shows: the pitch asked.
    int expected() const { return expected_pitch(question_); }
    // Whether `note` is a right answer (the pitch asked, or its pitch class with any octave).
    bool is_expected(int note) const;
    // Whether a key (any note, on the keys or not) is part of the question and so shown with a ring:
    // the starting note of an interval.
    int start_note() const { return question_.kind == EarQuestionKind::Interval ? question_.first : -1; }

    // The role of the key of `note` now.
    EarKeyRole key_role(int note) const;

    int streak() const { return streak_; }
    int best() const { return best_; }
    const std::array<EarMark, kEarDots>& dots() const { return dots_; }
    int answers() const { return answers_; }
    int rights() const { return rights_; }

  private:
    void mark(EarMark m);
    bool fit_view();

    std::uint64_t seed_;
    int epoch_ = 0; // a new generator for each configuration, from the same seed
    EarConfig config_;
    EarTrainer trainer_;
    EarPhase phase_ = EarPhase::Idle;
    EarQuestion question_;
    std::string message_;
    int answered_ = -1;
    int view_first_ = 0;
    bool hinted_ = false;
    int replays_ = 0;
    int streak_ = 0;
    int best_ = 0;
    int answers_ = 0;
    int rights_ = 0;
    std::array<EarMark, kEarDots> dots_{};
};

} // namespace etude::app
