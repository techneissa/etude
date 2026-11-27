// SPDX-License-Identifier: GPL-3.0-or-later
// The small types and names SongController hands out, apart from the class so the UI can use them
// without pulling in the whole song half.
#pragma once

#include "song_sound.hpp"

#include "etude/evaluator_results.hpp"
#include "etude/hand.hpp"
#include "etude/metronome.hpp"
#include "etude/note_window.hpp"
#include "etude/profile.hpp"
#include "etude/progress.hpp"
#include "etude/run_events.hpp"
#include "etude/run_record.hpp"
#include "etude/session_state.hpp"
#include "etude/song_schedule.hpp"

#include <cstdint>
#include <string>

namespace etude::app {

const char* song_sound_name(SongSound sound);
PianoPlays to_piano_plays(SongSound sound);
// Left and Right (an older choice) read as OtherHand.
SongSound song_sound_from(PianoPlays plays);
const char* hands_name(HandFilter hands);
const char* mode_name(PracticeMode mode);

// A note result as it became final, for the brief grade shown where the note met the hit line.
struct GradeMark {
    std::uint32_t note_index = 0;
    NoteGrade grade = NoteGrade::Missed;
    int key = 0; // the key it was expected on, after transposition
    Hand hand = Hand::Unknown;
    std::int64_t at_ns = 0; // when it became final
};

// A wrong press, as it became known, for the brief red flash on its key.
struct WrongMark {
    int key = 0;
    std::int64_t at_ns = 0;
};

// A run that ended with graded notes. The record's wall times and day are left at 0: the caller
// knows the time zone. `started_ns` and `ended_ns` are on the frame clock, to place it.
struct FinishedRun {
    RunRecord record;
    std::int64_t started_ns = 0;
    std::int64_t ended_ns = 0;
    std::string song_path; // where the song file was opened from; empty when not known
    RunEvents events;      // what the player played, note by note (run_events.hpp)
    // Whose run it is: the controller's profile when the run ended (set_profile), so a profile
    // switched or a song opened afterwards does not move it to someone else.
    std::int64_t profile_id = kDefaultProfileId;
};

// How long one grade stays on screen.
inline constexpr std::int64_t kGradeShowNs = 900'000'000;

// One pass through the A-B loop, as the evaluator judged it (or a plain wrap while nothing is
// graded). The section screens take these to drive the practice list.
struct LoopPass {
    bool clean = false;
    std::int64_t at_ns = 0;
    // The notes graded in the pass, the ones missed and the wrong presses (0 for a plain wrap
    // while nothing is graded).
    int graded = 0;
    int missed = 0;
    int wrong = 0;
    // The graded notes by grade (`played` in Wait Mode) and the pass's longest run of hits.
    int perfect = 0;
    int good = 0;
    int early = 0;
    int late = 0;
    int played = 0;
    int combo = 0;
};

struct SongOptions {
    double hit_window_ms = 80.0;  // AppSettings::hit_window_ms
    double latency_ms = 0.0;      // AppSettings::latency_correction_ms
    double lookahead_s = 4.0;     // LookSettings::visible_seconds: what the player can see
    int resume_countdown_s = 3;   // AppSettings::resume_countdown_s, 1..10
    int skip_back_seconds = 5;    // AppSettings::skip_back_seconds, 1..30
    int skip_forward_seconds = 5; // AppSettings::skip_forward_seconds, 1..30
    // How far ahead of reaching the hit line a note of the song is handed over (--song-lead-ms). 0
    // hands each note over when the frame that passes it runs, as it did before notes were
    // scheduled.
    std::int64_t sound_lead_ns = kSongSoundLeadNs;
    // Extra time the evaluators wait for an event after its stamp, for a source that reports a
    // note some time after it was struck (the microphone: kMicSettleMs). 0 for MIDI.
    int input_delay_ms = 0;
    // The metronome and the count-in's voice (AppSettings): the metronome clicks while the song
    // plays; the count-in uses the voice whether or not it is on.
    bool metronome = false;
    ClickVoice click_voice = ClickVoice::WoodBlock;
    int click_volume = kDefaultClickVolume;
    MetronomeSettings metronome_grid;
    // The count-in a song starts with when nothing is kept for it. The app passes
    // kDefaultCountInBars; the controller itself starts with none.
    int default_count_in_bars = 0;
    // How loud the accompaniment sounds against the piano, in percent (AppSettings).
    int accompaniment_volume = 100;
    friend bool operator==(const SongOptions&, const SongOptions&) = default;
};

// "Right 92%, Left 85% · longest combo 41 · up 4% on last time": the results of a run, accuracy for
// each hand that had graded notes.
std::string results_line(const RunRecord& record, const AttemptChange& change,
                         bool comparable = true);

} // namespace etude::app
