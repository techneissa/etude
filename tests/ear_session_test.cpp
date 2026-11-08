// SPDX-License-Identifier: GPL-3.0-or-later
// The Ear card's session (app/ear_session.hpp): questions from the keys shown, one try each, the
// streak, the hint and the replay, driven with a seed.
#include "ear_session.hpp"
#include "mic_inbox.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

EarConfig config(LearnEarMode mode, int whites = 15, int low = 60) {
    EarConfig c;
    c.mode = mode;
    c.whites = whites;
    c.low_note = low;
    return c;
}

// A session on `c` with a question asked.
EarSession asked(const EarConfig& c, std::uint64_t seed = 7) {
    EarSession s(seed);
    s.configure(c);
    REQUIRE(s.next());
    return s;
}

// A key that is not the answer, not the starting note and on the keys shown.
int wrong_key(const EarSession& s) {
    for (int n = s.config().low_note; n <= ear_top_note(s.config()); ++n) {
        if (!s.is_expected(n) && n != s.start_note()) {
            return n;
        }
    }
    return -1;
}

constexpr std::array<LearnEarMode, 4> kModes{LearnEarMode::Note, LearnEarMode::IntervalUp,
                                             LearnEarMode::IntervalDown, LearnEarMode::Together};

} // namespace

TEST_CASE("ear session: the keys shown run from a C for the white keys asked") {
    CHECK(ear_c_note(4) == 60);
    CHECK(ear_white_note(60, 0) == 60);
    CHECK(ear_white_note(60, 6) == 71);
    CHECK(ear_white_note(60, 7) == 72);
    CHECK(ear_top_note(config(LearnEarMode::Note, 15)) == 84);
    CHECK(ear_top_note(config(LearnEarMode::Note, 8)) == 72);
    CHECK(ear_top_note(config(LearnEarMode::Note, 11)) == 77); // F5
    // The keys shown are a valid keyboard: a white key at each end, so questions are on real keys.
    for (int whites = kEarMinWhites; whites <= kEarMaxWhites; ++whites) {
        const EarSettings s = ear_settings(config(LearnEarMode::Note, whites));
        CHECK(s.range.valid());
    }
}

TEST_CASE("ear session: every mode asks only notes on the keys shown, in its own direction") {
    for (const LearnEarMode mode : kModes) {
        for (const int whites : {8, 11, 15}) {
            CAPTURE(static_cast<int>(mode));
            CAPTURE(whites);
            const EarConfig c = config(mode, whites);
            EarSession s(3);
            s.configure(c);
            for (int i = 0; i < 300; ++i) {
                REQUIRE(s.next());
                const EarQuestion& q = s.question();
                CHECK(q.first >= c.low_note);
                CHECK(q.first <= ear_top_note(c));
                CHECK(q.second >= c.low_note);
                CHECK(q.second <= ear_top_note(c));
                switch (mode) {
                case LearnEarMode::Note:
                    CHECK(q.kind == EarQuestionKind::Note);
                    CHECK(q.first == q.second);
                    CHECK(s.start_note() == -1);
                    break;
                case LearnEarMode::IntervalUp:
                    CHECK(q.kind == EarQuestionKind::Interval);
                    CHECK(q.semitones() > 0);
                    CHECK_FALSE(q.harmonic);
                    break;
                case LearnEarMode::IntervalDown:
                    CHECK(q.kind == EarQuestionKind::Interval);
                    CHECK(q.semitones() < 0);
                    CHECK_FALSE(q.harmonic);
                    break;
                case LearnEarMode::Together:
                    CHECK(q.kind == EarQuestionKind::Interval);
                    CHECK(q.semitones() > 0);
                    CHECK(q.harmonic);
                    break;
                }
                if (q.kind == EarQuestionKind::Interval) {
                    CHECK(s.start_note() == q.first);
                }
                // Answer it right, to ask the next.
                s.press(s.expected());
                CHECK(s.phase() == EarPhase::Right);
            }
        }
    }
}

TEST_CASE("ear session: the largest interval is kept") {
    EarConfig c = config(LearnEarMode::IntervalUp);
    c.max_interval = 5;
    EarSession s(1);
    s.configure(c);
    std::set<int> sizes;
    for (int i = 0; i < 400; ++i) {
        REQUIRE(s.next());
        sizes.insert(std::abs(s.question().semitones()));
        s.press(s.expected());
    }
    CHECK(sizes == std::set<int>{1, 2, 3, 4, 5});
}

TEST_CASE("ear session: the same seed asks the same questions, another seed others") {
    const auto run = [](std::uint64_t seed) {
        EarSession s(seed);
        s.configure(config(LearnEarMode::IntervalUp));
        std::vector<int> out;
        for (int i = 0; i < 30; ++i) {
            REQUIRE(s.next());
            out.push_back(s.question().first * 100 + s.question().second);
            s.press(s.expected());
        }
        return out;
    };
    CHECK(run(5) == run(5));
    CHECK(run(5) != run(6));
}

TEST_CASE("ear session: a right answer adds to the streak and shows a green dot") {
    EarSession s = asked(config(LearnEarMode::Note));
    CHECK(s.phase() == EarPhase::Asked);
    CHECK(s.press(s.expected()) == EarPhase::Right);
    CHECK(s.answered() == s.expected());
    CHECK(s.streak() == 1);
    CHECK(s.best() == 1);
    CHECK(s.dots()[0] == EarMark::Right);
    CHECK(s.dots()[1] == EarMark::None);
    CHECK(s.rights() == 1);
    CHECK(s.answers() == 1);
}

TEST_CASE("ear session: a wrong answer ends the streak, shows a red dot and keeps the best") {
    EarSession s = asked(config(LearnEarMode::Note));
    s.press(s.expected());
    REQUIRE(s.next());
    s.press(s.expected());
    REQUIRE(s.next());
    CHECK(s.streak() == 2);
    const int bad = wrong_key(s);
    REQUIRE(bad >= 0);
    CHECK(s.press(bad) == EarPhase::Wrong);
    CHECK(s.answered() == bad);
    CHECK(s.streak() == 0);
    CHECK(s.best() == 2);
    CHECK(s.dots()[2] == EarMark::Wrong);
    CHECK(s.rights() == 2);
    CHECK(s.answers() == 3);
}

TEST_CASE("ear session: one try: an answered question takes no more presses, and Next asks the next") {
    EarSession s = asked(config(LearnEarMode::Note));
    const EarQuestion q = s.question();
    s.press(wrong_key(s));
    CHECK(s.phase() == EarPhase::Wrong);
    CHECK(s.press(s.expected()) == EarPhase::Wrong); // too late: still wrong, nothing changes
    CHECK(s.streak() == 0);
    CHECK(s.answers() == 1);
    CHECK(s.next());
    CHECK(s.phase() == EarPhase::Asked);
    CHECK(s.answered() == -1);
    (void)q;
}

TEST_CASE("ear session: there is no skipping a question that waits") {
    EarSession s = asked(config(LearnEarMode::IntervalUp));
    const EarQuestion q = s.question();
    CHECK_FALSE(s.next());
    CHECK(s.phase() == EarPhase::Asked);
    CHECK(s.question().first == q.first);
    CHECK(s.question().second == q.second);
}

TEST_CASE("ear session: the starting note of an interval and keys off the keyboard are no answer") {
    EarSession s = asked(config(LearnEarMode::IntervalUp));
    CHECK(s.press(s.start_note()) == EarPhase::Asked);
    CHECK(s.press(s.config().low_note - 1) == EarPhase::Asked);
    CHECK(s.press(ear_top_note(s.config()) + 1) == EarPhase::Asked);
    CHECK(s.press(0) == EarPhase::Asked);
    CHECK(s.press(127) == EarPhase::Asked);
    CHECK(s.answers() == 0);
    CHECK(s.streak() == 0);
    // Nothing happens before a question either.
    EarSession idle;
    CHECK(idle.press(60) == EarPhase::Idle);
}

TEST_CASE("ear session: ignore octave takes the note's pitch class on any octave") {
    EarConfig c = config(LearnEarMode::Note);
    EarSession strict = asked(c);
    const int want = strict.expected();
    const int other = want + 12 <= ear_top_note(c) ? want + 12 : want - 12;
    REQUIRE(other >= c.low_note);
    CHECK_FALSE(strict.is_expected(other));
    CHECK(strict.press(other) == EarPhase::Wrong);

    c.any_octave = true;
    EarSession loose(7);
    loose.configure(c);
    REQUIRE(loose.next());
    const int want2 = loose.expected();
    const int other2 = want2 + 12 <= ear_top_note(c) ? want2 + 12 : want2 - 12;
    CHECK(loose.is_expected(other2));
    CHECK_FALSE(loose.is_expected(want2 + 1));
    CHECK(loose.press(other2) == EarPhase::Right);
}

TEST_CASE("ear session: an octave interval with ignore octave still needs a key that is not the "
          "starting note") {
    EarConfig c = config(LearnEarMode::IntervalUp);
    c.any_octave = true;
    c.max_interval = 12;
    EarSession s(2);
    s.configure(c);
    int octaves = 0;
    for (int i = 0; i < 600 && octaves < 3; ++i) {
        REQUIRE(s.next());
        if (s.question().semitones() == 12) {
            ++octaves;
            CHECK(s.press(s.start_note()) == EarPhase::Asked); // the shown key: not an answer
            CHECK(s.press(s.start_note() + 12) == EarPhase::Right);
        } else {
            s.press(s.expected());
        }
    }
    CHECK(octaves > 0);
}

TEST_CASE("ear session: a hint shows the answer once, and the answer after it neither adds to "
          "the streak nor breaks it") {
    EarSession s = asked(config(LearnEarMode::Note));
    s.press(s.expected());
    REQUIRE(s.next());
    CHECK_FALSE(s.hinted());
    CHECK(s.hint());
    CHECK(s.hinted());
    CHECK_FALSE(s.hint()); // once
    CHECK(s.press(s.expected()) == EarPhase::Right);
    CHECK(s.streak() == 1); // kept, not added
    CHECK(s.dots()[1] == EarMark::Hinted);
    CHECK_FALSE(s.hint()); // answered: nothing to hint
    REQUIRE(s.next());
    CHECK_FALSE(s.hinted()); // the next question starts without one
    CHECK(s.press(wrong_key(s)) == EarPhase::Wrong); // a wrong answer after no hint breaks it
    CHECK(s.streak() == 0);
}

TEST_CASE("ear session: a wrong answer after a hint still breaks the streak") {
    EarSession s = asked(config(LearnEarMode::Note));
    s.press(s.expected());
    REQUIRE(s.next());
    CHECK(s.hint());
    s.press(wrong_key(s));
    CHECK(s.streak() == 0);
    CHECK(s.dots()[1] == EarMark::Wrong);
}

TEST_CASE("ear session: the replay gives the question in hand, again, and changes nothing") {
    EarSession idle;
    CHECK(idle.replay() == nullptr);
    EarSession s = asked(config(LearnEarMode::IntervalDown));
    const EarQuestion q = s.question();
    const EarQuestion* again = s.replay();
    REQUIRE(again != nullptr);
    CHECK(again->first == q.first);
    CHECK(again->second == q.second);
    CHECK(s.phase() == EarPhase::Asked);
    CHECK(s.replays() == 1);
    s.press(s.expected());
    CHECK(s.replay() != nullptr); // an answered question is heard again too
    CHECK(s.replays() == 2);
    CHECK(s.phase() == EarPhase::Right);
    CHECK(s.streak() == 1);
}

TEST_CASE("ear session: a new setting drops the question and keeps the streak; the same one "
          "changes nothing") {
    EarSession s = asked(config(LearnEarMode::Note));
    s.press(s.expected());
    REQUIRE(s.next());
    CHECK_FALSE(s.configure(config(LearnEarMode::Note))); // as it was
    CHECK(s.phase() == EarPhase::Asked);
    CHECK(s.configure(config(LearnEarMode::IntervalUp)));
    CHECK(s.phase() == EarPhase::Idle);
    CHECK(s.replay() == nullptr);
    CHECK(s.streak() == 1);
    CHECK(s.next());
    CHECK(s.question().kind == EarQuestionKind::Interval);
}

TEST_CASE("ear session: the ten dots fill from the left and then the oldest goes") {
    EarSession s(4);
    s.configure(config(LearnEarMode::Note));
    for (int i = 0; i < kEarDots; ++i) {
        REQUIRE(s.next());
        s.press(s.expected());
    }
    for (const EarMark m : s.dots()) {
        CHECK(m == EarMark::Right);
    }
    REQUIRE(s.next());
    s.press(wrong_key(s));
    CHECK(s.dots().back() == EarMark::Wrong);
    CHECK(s.dots().front() == EarMark::Right);
    CHECK(s.streak() == 0);
    CHECK(s.best() == kEarDots);
    int rights = 0;
    for (const EarMark m : s.dots()) {
        rights += m == EarMark::Right ? 1 : 0;
    }
    CHECK(rights == kEarDots - 1);
}

TEST_CASE("ear session: settings that cannot make a question say why and stay idle") {
    EarConfig c = config(LearnEarMode::IntervalUp, 8);
    c.max_interval = 12;
    EarSession ok(1);
    ok.configure(c);
    CHECK(ok.next()); // the narrowest keys still hold an octave
    EarConfig bad = c;
    bad.max_interval = 40; // outside the core's limit
    EarSession s(1);
    s.configure(bad);
    CHECK_FALSE(s.next());
    CHECK_FALSE(s.message().empty());
    CHECK(s.phase() == EarPhase::Idle);
}

bool is_black(int note) {
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// Whether the note's key, and the white keys a black one needs beside it, are on the keys on view.
bool on_view(const EarSession& s, int note) {
    return is_black(note) ? note > s.view_low() && note < s.view_top()
                 : note >= s.view_low() && note <= s.view_top();
}

// The white keys a question's notes need on view: from its lowest white key to its highest, with the
// white key to the right of a black one.
int white_span(const EarQuestion& q, int expected) {
    int lo = 1000;
    int hi = -1;
    for (const int note : {q.first, expected}) {
        const int at = ear_white_index(60, note);
        lo = std::min(lo, at);
        hi = std::max(hi, at + (is_black(note) ? 1 : 0));
    }
    return hi - lo + 1;
}

TEST_CASE("ear session: a resize mid-question keeps the question while it still fits the keys on view "
          "and moves them so its notes are on them; a question that no longer fits is asked again "
          "from the keys on view") {
    int kept = 0;
    int asked_again = 0;
    for (const LearnEarMode mode : {LearnEarMode::Note, LearnEarMode::IntervalUp,
                                    LearnEarMode::IntervalDown, LearnEarMode::Together}) {
        for (std::uint64_t seed = 1; seed <= 400; ++seed) {
            CAPTURE(static_cast<int>(mode));
            CAPTURE(seed);
            EarSession s(seed);
            s.configure(config(mode, 15));
            REQUIRE(s.next());
            const EarQuestion q = s.question();
            const int span = white_span(q, s.expected());
            const int streak = s.streak();
            CAPTURE(span);
            // The window shrinks to the least.
            const bool changed = s.configure(config(mode, kEarMinWhites));
            CHECK(changed == (span > kEarMinWhites));
            CHECK(s.streak() == streak);
            if (!changed) {
                ++kept;
                CHECK(s.phase() == EarPhase::Asked);
                CHECK(s.question().first == q.first);
                CHECK(s.question().second == q.second);
                CHECK(on_view(s, q.first));
                CHECK(on_view(s, s.expected()));
                CHECK(s.view_top() - s.view_low() == ear_white_note(0, kEarMinWhites - 1));
                // Wider again: the same question, still on view, and answered as asked.
                CHECK_FALSE(s.configure(config(mode, 11)));
                CHECK(s.phase() == EarPhase::Asked);
                CHECK(s.question().second == q.second);
                CHECK(on_view(s, q.first));
                CHECK(on_view(s, s.expected()));
                const int answer = s.expected();
                REQUIRE(answer >= s.view_low());
                REQUIRE(answer <= s.view_top());
                CHECK(s.press(answer) == EarPhase::Right);
                CHECK(s.streak() == streak + 1);
            } else {
                ++asked_again;
                // Dropped as any change is: Idle, then a new question from the keys on view only.
                CHECK(s.phase() == EarPhase::Idle);
                REQUIRE(s.next());
                CHECK(s.view_first() == 0);
                CHECK(on_view(s, s.question().first));
                CHECK(on_view(s, s.expected()));
            }
        }
    }
    CHECK(kept > 0);
    CHECK(asked_again > 0); // C#4 to C#5 and the like: nine white keys do not fit eight
}

TEST_CASE("ear session: an octave from a black key does not fit eight white keys and is asked again") {
    // C#4 to C#5: nine white keys wide with the black key's right neighbour.
    EarQuestion q;
    q.first = 61;
    CHECK(white_span(q, 73) == 9);
    q.first = 60;
    CHECK(white_span(q, 72) == 8); // C4 to C5: eight, fits
}

TEST_CASE("ear session: a resize after the answer keeps the answered key on view, and the next question "
          "starts from the first key on view again") {
    EarSession s(9);
    s.configure(config(LearnEarMode::Note, 15));
    REQUIRE(s.next());
    const int wrong = s.expected() == 60 ? 62 : 60;
    CHECK(s.press(wrong) == EarPhase::Wrong);
    s.configure(config(LearnEarMode::Note, kEarMinWhites));
    CHECK(s.phase() == EarPhase::Wrong);
    CHECK(s.answered() == wrong);
    CHECK(s.answered() >= s.view_low() - 1);
    CHECK(s.answered() <= s.view_top() + 1);
    REQUIRE(s.next());
    CHECK(s.view_first() == 0);
    // The question is drawn from the keys then shown.
    CHECK(s.question().first >= 60);
    CHECK(s.question().first <= ear_white_note(60, kEarMinWhites - 1));
}

TEST_CASE("ear session: a changed mode still drops the question (only the window size does not)") {
    EarSession s(3);
    s.configure(config(LearnEarMode::Note, 15));
    REQUIRE(s.next());
    CHECK(s.configure(config(LearnEarMode::IntervalUp, 15)));
    CHECK(s.phase() == EarPhase::Idle);
    CHECK(ear_white_index(60, 61) == 0);
    CHECK(ear_white_index(60, 62) == 1);
    CHECK(ear_white_index(60, 72) == 7);
}

TEST_CASE("ear session: the card takes answers from MIDI and the keys, never from the microphone") {
    // A note the microphone detected, as the app makes it (MicInbox: tagged as the microphone, with
    // the velocity every detected note has).
    MicInbox inbox;
    std::vector<MicEventIn> heard(1);
    heard[0].event.type = PitchEventType::NoteOn;
    heard[0].event.note = 64;
    heard[0].onset_ns = 100'000'000;
    heard[0].sample_ns = 140'000'000;
    std::vector<MidiEvent> events;
    std::vector<UnsureMark> marks;
    std::size_t at = 0;
    inbox.take(
        [&](MicEventIn& in) {
            if (at >= heard.size()) {
                return false;
            }
            in = heard[at++];
            return true;
        },
        300'000'000, events, marks);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].type == MidiEventType::NoteOn);
    CHECK(events[0].source == InputSource::Microphone);
    CHECK_FALSE(ear_takes_answer(events[0]));
    // The same note from a MIDI keyboard, the computer's keys or the screen's is an answer.
    MidiEvent key = events[0];
    for (const InputSource source : {InputSource::Midi, InputSource::ComputerKeyboard, InputSource::Pointer}) {
        key.source = source;
        CHECK(ear_takes_answer(key));
    }
    key.source = InputSource::Midi;
    key.data2 = 0; // a note-on with no velocity is a release
    CHECK_FALSE(ear_takes_answer(key));
    key.data2 = 90;
    key.type = MidiEventType::NoteOff;
    CHECK_FALSE(ear_takes_answer(key));
}

TEST_CASE("ear session: with any octave right, only the key tapped is the answer's: the other "
          "octaves' keys of the note have no role; a miss shows the one right key in the octave asked") {
    EarConfig c = config(LearnEarMode::Note, 15);
    c.any_octave = true;
    int rights = 0;
    int wrongs = 0;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        CAPTURE(seed);
        EarSession s = asked(c, seed);
        const int pc = s.expected() % 12;
        // The same note in the other octave of the keys: right, and the only key with a role.
        int other = -1;
        for (int n = 60; n <= ear_white_note(60, c.whites - 1); ++n) {
            if (n % 12 == pc && n != s.expected()) {
                other = n;
            }
        }
        if (other >= 0 && !(s.question().kind == EarQuestionKind::Interval && other == s.start_note())) {
            CHECK(s.press(other) == EarPhase::Right);
            ++rights;
            for (int n = 60; n <= ear_white_note(60, c.whites - 1); ++n) {
                CAPTURE(n);
                CHECK((s.key_role(n) == EarKeyRole::Tapped) == (n == other));
                CHECK(s.key_role(n) != EarKeyRole::Missed);
            }
        }
        // A wrong tap: the tapped key and the one right key in the octave asked, no others.
        EarSession w = asked(c, seed);
        int wrong = -1;
        for (int n = 60; n <= ear_white_note(60, c.whites - 1) && wrong < 0; ++n) {
            if (n % 12 != w.expected() % 12 && n != w.start_note()) {
                wrong = n;
            }
        }
        REQUIRE(wrong >= 0);
        CHECK(w.press(wrong) == EarPhase::Wrong);
        ++wrongs;
        for (int n = 60; n <= ear_white_note(60, c.whites - 1); ++n) {
            CAPTURE(n);
            const EarKeyRole role = w.key_role(n);
            if (n == wrong) {
                CHECK(role == EarKeyRole::Tapped);
            } else if (n == w.expected()) {
                CHECK(role == EarKeyRole::Missed);
            } else if (n == w.start_note()) {
                CHECK(role == EarKeyRole::Start);
            } else {
                CHECK(role == EarKeyRole::Plain); // not the other octaves of the right note
            }
        }
    }
    CHECK(rights > 0);
    CHECK(wrongs == 30);
}

TEST_CASE("ear session: a hint marks the one key asked, in the octave asked, with any octave on") {
    EarConfig c = config(LearnEarMode::Note, 15);
    c.any_octave = true;
    EarSession s = asked(c, 5);
    CHECK(s.hint());
    int hinted = 0;
    for (int n = 60; n <= ear_white_note(60, c.whites - 1); ++n) {
        if (s.key_role(n) == EarKeyRole::Hint) {
            ++hinted;
            CHECK(n == s.expected());
        }
    }
    CHECK(hinted == 1);
}
