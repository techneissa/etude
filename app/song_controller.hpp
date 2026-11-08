// SPDX-License-Identifier: GPL-3.0-or-later
// The song half of the app (practice modes): one loaded song
// and everything that plays, shows and grades it. The UI binds its buttons to the actions below;
// until it does, keyboard shortcuts in main.cpp call them. No SDL, no MIDI, no files: the frame
// loop hands in the time, the player's events and the song, and takes out the MIDI messages to
// send and the finished runs to save, so the tests drive it the same way.
//
// Each frame, in this order:
//   update(now, player_events)    the clock, the player's notes, Wait Mode, the countdown
//   take_sound_events(out)        the song's sound for the audio thread or the MIDI sender
//   take_finished_runs()          runs to save and show results for
//
// The sound is handed over ahead of time (etude/look_ahead.hpp): each frame, the notes that reach
// the hit line within the next options().sound_lead_ns are handed over, each with the exact time it
// reaches it (etude/song_schedule.hpp), so the receiver has them before they must start and a
// frame shorter than the look-ahead cannot make one late. A command that stops or moves the song
// (pause, seek, replay, song change, opening the settings, focus loss, quit) or changes what was
// planned (speed, loop, mute, transposition) is a CancelUnstarted event followed by the messages
// that stop what had started: a note that had not started never does.
//
// The metronome and the count-in are made the same way (etude/metronome.hpp): the clicks inside
// each span the look-ahead plans, and the count-in's clicks due in the next window, each with its
// exact time. A change of the metronome settings, like a new speed, cancels what was handed over
// and plans again from the position.
//
// Stuck-note safety: every such action leaves Playback with nothing sounding, and the events keep
// the order Playback produced, so a note-off is never due before its note-on. shutdown() silences
// and hands back everything still queued; after it needs_silence() is false.
#pragma once

#include "status_parts.hpp"

#include "etude/accompaniment.hpp"
#include "etude/difficulty.hpp"
#include "etude/profile.hpp"
#include "etude/run_score.hpp"
#include "etude/evaluator.hpp"
#include "etude/game_feel.hpp"
#include "etude/hand_split.hpp"
#include "etude/hands_present.hpp"
#include "etude/hit_line.hpp"
#include "etude/keyboard.hpp"
#include "etude/metronome.hpp"
#include "etude/midi_event.hpp"
#include "etude/musicxml.hpp"
#include "etude/note_window.hpp"
#include "etude/octave_shift.hpp"
#include "etude/playback.hpp"
#include "etude/practice_plan.hpp"
#include "etude/progress.hpp"
#include "etude/run_events.hpp"
#include "etude/run_record.hpp"
#include "etude/session_state.hpp"
#include "etude/shift_fit.hpp"
#include "etude/shift_offer.hpp"
#include "etude/song.hpp"
#include "etude/song_practice.hpp"
#include "etude/song_schedule.hpp"
#include "etude/tempo_control.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace etude::app {

class HandSplitJob;
struct LentSong;
class PracticeTablesJob;
struct TablesResult;

// What you hear of the song (the song sound setting). Your own playing always sounds.
enum class SongSound : std::uint8_t {
    Mute,      // default: nothing from the song
    OtherHand, // the hand you are not practising; nothing when you practise both
    BothHands, // the whole song
};
const char* song_sound_name(SongSound sound);
PianoPlays to_piano_plays(SongSound sound);
// Left and Right (an older choice) read as OtherHand.
SongSound song_sound_from(PianoPlays plays);
const char* hands_name(HandFilter hands);
const char* mode_name(PracticeMode mode);

// A song ready to play: hands assigned (names, else the smart split), with what identifies it.
struct LoadedSong {
    Song song;
    std::string title;
    std::uint64_t hash = 0;    // content_hash() of the file's bytes
    std::string library_path;  // relative to a library folder, '/' separated; empty when outside
    std::string absolute_path; // where it was opened from, absolute; empty when not known
    std::string display_path;  // as the player named it, for messages
    bool hands_split = false;  // the split assigned some hands
    // The hands of the song's notes and parts as its file gave them, when the split decides the
    // hand of some of its notes: what the player's choice of split point
    // (SongController::set_hand_split) splits again, on a copy of the song that gets these hands
    // back first. Null when the file names the hands, or the song was not made by the loader.
    // `song` has the split applied, so this is the only trace of the file's own hands, a few bytes
    // a note instead of a second copy of the notes.
    std::shared_ptr<const HandSplit> file_hands;
    // The sheet's staves decided the hands (hands_from_staves in core, read before the split);
    // false when `file_hands` is null.
    bool hands_from_staves = false;
    // What the file is and how it was read, so sheet music can read the same score again: the
    // format by content, and for ABC the tune's address and whether chord symbols became a left
    // hand.
    SongFileFormat format = SongFileFormat::Unknown;
    // song_fingerprint(song), taken once by the loader on its own thread so load() compares two
    // numbers instead of passing over the notes again. 0 when not taken (load() then takes it).
    std::uint64_t fingerprint = 0;
    std::string tune;
    bool abc_chords = false;
    // How hard each bar is and the song as a whole (difficulty.hpp), rated by the loader on its own
    // thread so the frame thread only reads it. Null for a song built without a loader.
    std::shared_ptr<const SongDifficulty> difficulty;
};

// What a loaded song's practice tables depend on besides the song: the hands asked for and the
// transposition in force, and how far ahead the notes are shown (to size the frame's lists).
struct SongLoadPlan {
    HandFilter hands = HandFilter::Both; // asked for; the song's hands limit what applies
    Transposition transposition;
    double ahead_s = 4.0;
};

// The tables SongController::load() builds from a song, made ahead by SongController::prepare() on
// another thread (the opener's) so the frame thread only takes them over. load() uses them when
// they were built for the hands and transposition it ends up with, else it builds its own.
struct PreparedSong {
    bool valid = false;
    std::uint64_t fingerprint = 0; // song_fingerprint of the song the tables were built from
    HandFilter hands = HandFilter::Both; // what applies: the asked-for hands, limited to the song's
    Transposition transposition;
    HandsPresent present;            // the hands with notes in the whole song
    std::size_t densest_window = 0;  // the most notes in view at once (densest_window)
    FlowEvaluator::Prepared flow;
    WaitEvaluator::Prepared wait;
    NoteWindow window;
    Playback::Prepared playback; // shared by the song's playback and its audition's
};

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

class SongController {
  public:
    SongController();
    ~SongController();
    SongController(const SongController&) = delete;
    SongController& operator=(const SongController&) = delete;

    // ---- Song

    // Finishes the run in progress, stops what the old song sounds and starts the new one paused
    // at 0. The practice choices (mode, hands, sound, speed, transposition) stay; the loop goes.
    void load(LoadedSong song, std::int64_t now_ns);
    // As above, taking `prepared` (made by prepare() for this song) over where it fits.
    void load(LoadedSong song, std::int64_t now_ns, PreparedSong&& prepared);
    // Builds what load() would build on the frame thread. Reads no controller state, so any thread
    // may call it, with the plan taken by load_plan() when the song was asked for.
    static PreparedSong prepare(const Song& song, const SongLoadPlan& plan,
                                std::uint64_t fingerprint = 0);
    // The plan for a song loaded now, or for one loaded under `state`'s hands and transposition
    // (a restored session applies them right after the song is in).
    SongLoadPlan load_plan() const;
    SongLoadPlan load_plan_for(const SessionState& state) const;
    // The most notes in view at once in the loaded song (0 for none): what a frame's lists need.
    std::size_t densest_window() const { return densest_window_; }
    // Back to free play.
    void unload(std::int64_t now_ns);
    bool has_song() const { return loaded_; }
    // A song with more notes than this has its evaluators' tables for another hand choice or
    // another key built on a worker and taken over by a later update() (the change is in force
    // then: the grading, the notes and the sound change together); a song of this size or less
    // builds them on the spot, a fraction of a millisecond. A frame that builds the tables of a
    // 150,000-note song takes 20 to 70 ms.
    static constexpr std::size_t kInlineTableNotes = 2000;
    // Overrides kInlineTableNotes (tests, the bench).
    void set_inline_table_notes(std::size_t notes) { inline_table_notes_ = notes; }
    // True when the evaluators grade for the hands asked for and the transposition asked for: no
    // table build is waiting or running.
    bool tables_settled() const { return !tables_pending(); }
    // Blocks until the worker has no build waiting or running (tests); update() takes the answer.
    void wait_tables();
    // Holds the worker before its next build until released (tests).
    void hold_tables(bool on);
    // The hands the evaluators grade for, which the notes' colouring and the song's sound follow:
    // after a change of hands on a long song it is the new hands only when their tables are in
    // (practice_hands() is the hands asked for, for the toggles).
    HandFilter applied_hands() const { return applied_hands_; }
    // How many tables the evaluators built themselves, on whatever thread called (a load without
    // tables made ahead, a stale plan, a small song): the frame thread's own builds, for tests.
    std::uint64_t evaluator_builds() const { return flow_.builds() + wait_.builds(); }
    const LoadedSong& song() const { return song_; }
    // The song's notes and bars as the evaluators hold them, shared and never copied: what the
    // chord label's worker names the hands' chords from. They follow the hands of the notes the
    // moment those change (a load, a new split of the hands).
    FlowEvaluator::NoteTable note_table() const { return flow_.note_table(); }
    FlowEvaluator::MeasureTable measure_table() const { return flow_.measure_table(); }
    // Counts up with every load() and unload(), so a screen knows its song changed even when the
    // new one has the same content.
    std::uint32_t load_serial() const { return load_serial_; }
    // Changes with every load and every new split of the hands taken over: what the sheet music
    // (whose staves are the hands) is engraved from.
    std::uint32_t notes_serial() const { return notes_serial_; }

    // ---- Transport actions

    void play(std::int64_t now_ns);
    void pause(std::int64_t now_ns);
    // Play, or pause; ends a countdown (paused); at the end of the song, replay().
    void toggle_play(std::int64_t now_ns);
    // From the loop start when a loop is on, else from the start, and play.
    void replay(std::int64_t now_ns);
    // One step back by options().skip_back_seconds, stopping at the loop start (when a loop is
    // on and the position is in it) or at 0. A held button calls it again for each repeat.
    void skip_back(std::int64_t now_ns);
    // One step forward by options().skip_forward_seconds, stopping at the song's end; inside a
    // loop, a step that would pass the loop end wraps to the loop start; a skip from outside the
    // loop that lands outside it ends the loop (Transport::skip).
    void skip_forward(std::int64_t now_ns);
    // The step sizes, for drawing the buttons ("5s").
    int skip_back_seconds() const { return options_.skip_back_seconds; }
    int skip_forward_seconds() const { return options_.skip_forward_seconds; }
    // A jump: a drag or tap on the scrubber, a section, a restored position. A time outside the
    // loop ends the loop (Transport::scrub).
    void seek(double song_s, std::int64_t now_ns);
    void change_speed(int delta_percent, std::int64_t now_ns);
    void set_speed(int percent, std::int64_t now_ns);
    // The speed that plays `bpm` beats a minute (tempo_beat_dotted() says which beat) at the song's
    // main tempo, to the nearest whole percent.
    void set_target_bpm(double bpm, std::int64_t now_ns);
    // The tempo at the playhead times the speed, in beats a minute (the chip's number), and the
    // song's main tempo in the same beat (what a typed tempo is measured against). Both 0 without
    // a song. The beat is a quarter, or a dotted quarter in 6/8, 9/8 and 12/8 (meter.hpp).
    double tempo_bpm() const;
    double main_tempo_bpm() const;
    bool tempo_beat_dotted() const;
    // Bars of count-in before the song moves: 0 (off), 1 or 2. Saved with the song's session.
    void set_count_in_bars(int bars);
    int count_in_bars() const { return count_in_bars_; }
    // The speed, count-in, hands and transposition a session saved for this very song (the same
    // content hash), for a start that names the song itself (`--song FILE`): the same song comes
    // back as it was left. Without a song, or for another hash: nothing.
    void restore_song_settings(const SessionState& state, std::int64_t now_ns);

    // A-B loop at bar boundaries: A at the start of the current bar, B at its end. With both set
    // the loop plays; B before A swaps them.
    void set_loop_start_here(std::int64_t now_ns);
    void set_loop_end_here(std::int64_t now_ns);
    // Exact times, as a section or the practice list sets them. False when too short.
    bool set_loop(double start_s, double end_s, std::int64_t now_ns);
    void clear_loop(std::int64_t now_ns);
    // Loops the bar with this number (Measure::number, counting played bars) and plays it from its
    // start, as the results card's trouble bars do. False, and nothing changed, when the song has
    // no such bar or it is too short to loop.
    bool loop_bar(int measure, std::int64_t now_ns);
    // Applies one step of a practice list (practice_plan.hpp): the loop, the speed and the
    // speed-up it asks for. False when the transport refused the loop.
    bool apply_plan_step(const PlanStep& step, std::int64_t now_ns);
    // Loop passes finished since the last call, oldest first.
    std::vector<LoopPass> take_loop_passes();
    // The speed-up policy now in force (what the plan set, or the player did).
    const AutoSpeedUp& auto_speed_up() const { return transport_.auto_speed_up(); }

    // ---- Auditioning

    // Plays [start_s, end_s] of the song once, at normal speed, with both hands sounding whatever
    // the song sound setting says, then stops. It runs on its own clock: the practice position,
    // loop, speed and grading are untouched, and the player's own notes are not graded. Starting
    // another audition replaces the one playing. Pausing the song, opening a menu's counterpart
    // (menu_closed), a new song and shutdown() stop it.
    void audition(double start_s, double end_s, std::int64_t now_ns);
    // The same, but each time the pass ends while the loop toggle is on it starts again at
    // start_s (audition_repeat); with the loop off the pass in progress finishes and stops.
    // stop_audition, another audition and everything that stops one end the repeating at once.
    void audition_repeating(double start_s, double end_s, std::int64_t now_ns);
    void stop_audition(std::int64_t now_ns);
    bool auditioning() const { return auditioning_; }
    // Where the audition is in the song, for a playing cursor; meaningful while auditioning().
    double audition_position_s() const { return audition_tp_.position_s(); }

    // ---- Practice choices

    // The hands you ask for. What is practised is that, limited to the hands that have notes (see
    // hands_present.hpp): in a song with one hand's notes only, that hand, whatever was asked, and
    // in a looped section where one hand rests, the other. Asking is kept, so leaving the section
    // brings it back.
    void set_practice_hands(HandFilter hands, std::int64_t now_ns);
    HandFilter practice_hands() const { return clamp_hands(hands_, present_); }
    // The hands with notes in the looped stretch, or in the whole song without a loop. Both are
    // present in free play.
    const HandsPresent& hands_available() const { return present_; }
    // True when hands_available() is read from a loop rather than the whole song.
    bool hands_scope_is_loop() const { return scope_loop_; }

    // ---- The split of the hands

    // The song's hands come from the split: some of its notes have no hand in the file, so the
    // player can choose where the notes divide (a song whose file names the hands cannot).
    bool hand_split_available() const { return loaded_ && song_.file_hands != nullptr; }
    // The player's choice for this song: kAutoSplit (the smart split), or the pitch, in the song's
    // own pitches, from which notes are the right hand. Kept per song (SongPractice).
    int hand_split() const { return split_point_; }
    // Chooses how the notes are split. The song's hands are made again on a thread of its own and
    // taken over by a later update(): until then the old hands stay. The choice is kept for a song
    // that has no split to change. A change ends the run in progress and starts again from the
    // playhead, like a change of the hands practised.
    void set_hand_split(int split_point, std::int64_t now_ns);
    // The pitch the Hands row shows: the chosen point, and while the hands come from the sheet
    // where a straight line best divides the hands the split made. kMiddleC without a split.
    int hand_split_point() const;
    // The sheet's staves decided the hands (two or more tracks, hands_from_staves in core), rather
    // than a split of one staff's notes. False for a song with no split to choose.
    bool hands_from_staves() const { return hand_split_available() && song_.hands_from_staves; }
    // The longest a hand split took the worker since the song was loaded, and how many were
    // taken over, for the frame log and tests.
    double hand_split_seconds() const { return split_seconds_; }
    int hand_splits_applied() const { return splits_applied_; }
    // A split is waiting for the worker or being made; false once its answer is ready (tests).
    bool hand_split_working() const;
    // The lock a worker's copy of the song holds, and every write of the song takes (tests).
    std::shared_ptr<LentSong> lent_song() const { return lent_; }
    // Blocks until the worker is done and takes its answer over (tests, and quitting).
    void wait_hand_split(std::int64_t now_ns);

    void set_song_sound(SongSound sound, std::int64_t now_ns);
    // Mute, then other hand (only while both hands have notes), then both hands.
    void cycle_song_sound(std::int64_t now_ns);
    // The sound in effect: "the other hand" is mute while only one hand has notes.
    SongSound song_sound() const {
        return sound_ == SongSound::OtherHand && !present_.both() ? SongSound::Mute : sound_;
    }
    // Flow or Wait (FreePlay with a song is watching it: nothing graded).
    void set_mode(PracticeMode mode, std::int64_t now_ns);
    void toggle_mode(std::int64_t now_ns); // Flow <-> Wait
    PracticeMode mode() const { return mode_; }
    void set_transpose(int semitones, std::int64_t now_ns);
    int transpose() const { return semitones_; }
    // The same, chosen by the player: it also ends the octave-shift offer (they took charge).
    void player_set_transpose(int semitones, std::int64_t now_ns);

    // ---- The octave-shift offer (core shift_offer.hpp)
    // What each shift would leave off the keyboard, for the open song and the player's keyboard;
    // none without a song. Worked out when the song or the keyboard is new, never per frame.
    const ShiftFits* shift_fits() const { return loaded_ ? &fits_ : nullptr; }
    // The shift proposed now, none when no card shows. update() raises it from the song's notes
    // outside the keyboard and from the player's note-ons (OctaveShiftDetector, fed there).
    const std::optional<ShiftOffer>& shift_offer() const {
        static const std::optional<ShiftOffer> none;
        return loaded_ ? offer_.offer() : none;
    }
    void accept_shift_offer(std::int64_t now_ns);
    void dismiss_shift_offer();
    // Dismiss, and never offer for this song again (saved with the song's practice choices).
    void never_offer_shift();
    // The octave-shift offer the player's presses raised does not move the song: accepting it asks
    // the app to take the keyboard's notes as shifted by `*semitones` in all (the app moves its
    // KeyRange, hands the new one to set_keyboard, and says what is in force with
    // set_keyboard_shift). True once per accepted offer.
    bool take_keyboard_shift(int* semitones);
    // The shift of the keyboard's notes the app has in force (0: the keyboard sends what its
    // profile says); the detector's evidence is judged against it.
    void set_keyboard_shift(int semitones) { keyboard_shift_ = semitones; }
    // The pitches the open song expects of the player around now, after transposition (the
    // detector's evidence): the chord Wait Mode holds at, else the practised notes near the clock
    // while it plays; none when paused. Fills `out` (no allocation), returns how many.
    int expected_around(std::span<int> out) const;
    // The player's keyboard: transposed notes outside it fold by octaves.
    void set_keyboard(KeyRange keyboard, std::int64_t now_ns);
    // Takes the app's settings. A change of the look-ahead goes into the running evaluators in
    // place. A change of the hit window, latency or input delay ends the run and rebuilds the
    // evaluators, so while `settling` (a slider is held: the value changes every frame) they wait
    // at their last applied values, and the first call without it applies what the slider ended
    // on: one rebuild for a drag. `options()` holds what is applied.
    void set_options(const SongOptions& options, std::int64_t now_ns, bool settling = false);
    const SongOptions& options() const { return options_; }
    // How many times the evaluators were rebuilt from the song (every load, hand split and
    // grading change), for tests.
    int evaluator_rebuilds() const { return evaluator_rebuilds_; }

    // ---- App events

    // The settings (or any menu) opened: pause and silence. Closed: if the song was playing, or the
    // player asked to play while it was open, count down options().resume_countdown_s seconds
    // with a tick each, then play.
    void menu_opened(std::int64_t now_ns);
    void menu_closed(std::int64_t now_ns);
    bool menu_open() const { return menu_open_; }
    // The window lost focus: pause and silence.
    void lost_focus(std::int64_t now_ns);
    // Quit: the run is finished, the piano silenced, and every queued event handed to `out`.
    void shutdown(std::int64_t now_ns, std::vector<SongSoundEvent>& out);
    // Stops every song note now (a different output took over).
    void silence(std::int64_t now_ns);

    // ---- Per frame

    // The player's NoteOn/NoteOff events of this frame, stamped, in order. Those already given to
    // take_in() (the same growing list) are not applied again. The list is only read here, before
    // the clock is pumped: a pull may add to the host's list (and move it) after that.
    void update(std::int64_t now_ns, std::span<const MidiEvent> player_events);
    // Hands the evaluators the events of `player_events` not given before (the list only grows
    // within a frame; update() ends the frame). A press is judged under the state it was played
    // under, so every event stamped before an action (a seek, a skip, a transposition, a wrap, a
    // song switch) must be in before the action is: the host calls this whenever it has events,
    // and gives `set_event_pull` for the rest.
    void take_in(std::span<const MidiEvent> player_events);
    // A function that gathers the events received so far and calls take_in() with them. Every
    // action and every pump of the clock calls it first, so an event the host has not handed over
    // yet but that was stamped before the action is not judged after it.
    void set_event_pull(std::function<void()> pull) { event_pull_ = std::move(pull); }
    // The sound events made since the last call, appended to `out` in the order Playback made them.
    // Each message is due at its at_ns (steady-clock ns); the receiver holds it until then.
    void take_sound_events(std::vector<SongSoundEvent>& out);
    std::vector<FinishedRun> take_finished_runs();
    // The profile the runs from now on belong to. A change ends the run in progress first, as a
    // change of mode or hands does, so that run keeps the profile it was played under.
    void set_profile(std::int64_t id, std::int64_t now_ns);
    std::int64_t profile_id() const { return profile_id_; }
    // True once after a run that pressed keys but graded no note (the practised hand has nothing
    // there): it has no score and is not saved; the results card says no notes were graded.
    bool take_ungraded_run() { return std::exchange(ungraded_run_, false); }

    // ---- What the screen shows

    double position_s() const { return transport_.position_s(); }
    double duration_s() const { return transport_.duration_s(); }
    int speed_percent() const { return transport_.speed_percent(); }
    TransportState state() const { return transport_.state(); }
    bool playing() const { return transport_.playing(); }
    bool loop_enabled() const { return transport_.loop_enabled(); }
    double loop_start_s() const { return transport_.loop_start_s(); }
    double loop_end_s() const { return transport_.loop_end_s(); }
    std::optional<double> pending_loop_start() const { return loop_a_; }
    // Bar number at the position (Measure::number: counts played bars, never repeats).
    int bar() const;
    int bar_count() const;
    // The same bar's number in the written score (Measure::source_number): what the printed page
    // shows. Equal to bar() unless the score has repeats.
    int written_bar() const;
    const Transposition& transposition() const { return source_.value(); }
    // The notes on screen, `ahead_s` song seconds above the hit line. Every hand is shown; the
    // caller dims the hands not practised.
    void visible_notes(double ahead_s, std::vector<VisibleNote>& out) const;
    // The same for another position and transposition than the song's own (a replay shows the song
    // where the replay is, as the run was played). Reads the song only; the transport is untouched.
    void visible_notes_at(double position_s, double ahead_s, const Transposition& transposition,
                          std::vector<VisibleNote>& out) const;
    // The keys the notes to play are drawn on (core drawn_key_range), for the key view's Auto size
    // and its centre; empty when none. Worked out once per song and transposition.
    std::optional<KeyRange> key_range() const;
    // Wait Mode while the clock waits at a chord: its keys, with the hand of each (Unknown when
    // both hands share a key). Empty otherwise.
    struct HintKey {
        int key = 0;
        Hand hand = Hand::Unknown;
    };
    // The span reads a member that the next call replaces.
    std::span<const HintKey> waiting_keys() const;
    bool waiting() const;
    // Wait Mode while the song plays or waits: the keys of the chord it will wait for next, after
    // transposition. A pitch detector checks for exactly these. Empty in every other state. The
    // span reads the member that waiting_keys() of the evaluator rewrites: use it before the next
    // call of either.
    std::span<const int> expected_keys() const;
    // An event from the microphone that could not be judged, stamped at the onset: not a press and
    // never a wrong note; Flow Mode leaves a note it covers ungraded (FlowEvaluator::apply_unsure).
    // Wait Mode needs nothing: no key goes down, so the song keeps waiting.
    void input_unsure(std::int64_t onset_ns);
    // Grades that became final within kGradeShowNs before `now_ns`, oldest first.
    std::span<const GradeMark> recent_grades() const { return grades_; }
    // Wrong presses that became known within kWrongFlashNs before `now_ns`, oldest first.
    std::span<const WrongMark> recent_wrongs() const { return wrongs_; }
    // The run's press count and mean, calibration suggestion and combo (core game_feel.hpp,
    // #88), fed by Flow Mode's graded presses as they arrive. No drawing yet.
    const GameFeel& game_feel() const { return feel_; }
    // The combo pulse (Milestone) and the combo break since the last call; None when there is
    // none. A Break and a Milestone are kept apart, so neither hides the other; a newer one of
    // the same kind replaces an untaken one. A Break on the song's last note is not shown: the
    // run's end clears both.
    ComboEvent take_combo_milestone() { return std::exchange(combo_milestone_, ComboEvent{}); }
    ComboEvent take_combo_break() { return std::exchange(combo_break_, ComboEvent{}); }
    // Whether the falling note on `key` starting at `note_start_s` (song seconds) is struck by the
    // player's held key, by the current mode's grading (hit_line.hpp): a wrong held key lights no
    // note. `grade` is the note's grade_of().
    bool note_struck(int key, bool key_down, double note_start_s, int grade) const;
    // Whether the latest press on `key` looks wrong as it is made, by the current mode's grading
    // (FlowEvaluator::press_looks_wrong). A wrong press sparks nothing.
    bool press_looks_wrong(int key) const;
    // A press on `key` that the current mode's grading has not applied yet (Wait Mode holds
    // events a few ms for older ones): its look is not known.
    bool press_pending(int key) const;
    // The final grade of each note this run, or -1; indexed like Song::notes.
    int grade_of(std::uint32_t note_index) const;
    int combo() const;
    int longest_combo() const;
    const EvaluationSummary* summary() const;
    // Seconds left of the resume countdown (1..10), or 0 when none runs.
    int countdown() const;
    bool counting_down() const { return countdown_end_ns_.has_value(); }
    // The count-in: the beat number to show (1-based in the bar) while the clicks count, else 0;
    // the beats in all and the share of them counted (0..1).
    int count_in_label() const;
    int count_in_beats() const { return transport_.count_in_total(); }
    int count_in_bar_beats() const {
        return transport_.counting_in() ? transport_.count_in_numerator() : 0;
    }
    double count_in_progress() const;
    // Where the song is drawn: the position, which holds still while a count-in counts.
    double display_position_s() const;
    // The beat the metronome (or the count-in) last sounded and how long ago on the wall clock, for
    // the pulse on the hit line; inactive when neither runs.
    struct BeatPulse {
        bool active = false;
        double age_s = 0.0;
        bool accent = false; // beat one
    };
    BeatPulse beat_pulse() const;
    // One line for the status row: title, mode, hands, speed, bar, sound, combo. Built again only
    // when one of those changes, so the frame loop can ask every frame; valid until the next call.
    const std::string& status_line() const;
    // The same line as parts with a drop rank (status_parts.hpp), views into `status_line()`'s text
    // and valid until the next call to either, added to `out` as one group: the narrow top row gives
    // the parts up whole, in rank order, where the plain line is cut at its end.
    void add_status_parts(StatusParts& out, std::uint8_t group) const;

    // ---- Saved state

    // `base` with the song's fields set: path (relative to the library), hash, position, speed,
    // hands, sound, loop, mode, transposition. Without a library path the song is not saved.
    SessionState session_state(const SessionState& base) const;
    // The same, written into `session` in place (its strings keep their storage), for the frame
    // loop, which asks every frame while paused.
    void write_session_state(SessionState& session) const;
    // The choices kept per song now: speed, count-in, hands asked for, transposition, the muted
    // accompaniment parts (`parts_set` once the song has any) and the loop. The section and practice
    // list item are the sections controller's and stay 0 here.
    SongPractice practice() const;
    // The same into `out`, reusing what it holds (no allocation once its parts' room is there).
    void practice_into(SongPractice& out) const;
    // Sets them (only what differs, so an equal value does no work). A loop past the song's end is
    // cut to it and dropped when too short.
    void apply_practice(const SongPractice& practice, std::int64_t now_ns);

    // ---- Accompaniment

    // The parts that sound with the song and are not a practice hand (accompaniment.hpp), in the
    // order of the song's parts; empty for a song with none.
    const std::vector<AccompanimentPart>& accompaniment_parts() const { return acc_parts_; }
    // The keys of the parts muted now (sorted). Each part is muted on its own; the song sound
    // setting does not touch them.
    const std::vector<std::uint32_t>& accompaniment_muted() const { return acc_muted_; }
    // Counts, allocates nothing: acc_muted_ only ever holds keys of listed parts.
    AccompanimentState accompaniment_state() const {
        return etude::accompaniment_state(acc_parts_.size(), acc_muted_.size());
    }
    // Mutes exactly the parts with these keys (keys the song has no part for are ignored).
    void set_accompaniment_muted(const std::vector<std::uint32_t>& keys, std::int64_t now_ns);
    // Applies a restored session (plan_restore's state) to the loaded song; paused.
    void apply_session(const SessionState& state, std::int64_t now_ns);

    // ---- For tests

    bool needs_silence() const { return playback_.needs_silence() || audition_pb_.needs_silence(); }
    std::size_t queued_events() const { return outbox_.size(); }

  private:
    void pump(std::int64_t now_ns);
    void pump_audition(std::int64_t now_ns);
    // Plays the range on the audition clock; the caller sets whether it repeats.
    void start_audition(double start_s, double end_s, std::int64_t now_ns);
    // Hands the notes of the next look-ahead over (etude/look_ahead.hpp); first cancels and
    // starts again from the position when speed, loop or transposition changed since they were
    // planned.
    void schedule_sound(std::int64_t now_ns);
    // What the transport did, for the sound.
    void sound_op(const TransportOp& op);
    // Hands over the notes up to `position_s` that the cursor has not reached, without a window.
    void catch_up(std::int64_t at_ns, double position_s);
    // Cancels the notes handed over and not started, and goes on from `position_s`.
    void restart_sound(std::int64_t at_ns, double position_s);
    void note_plan();
    // Messages with the due time Playback gave them (the exact time of the note).
    void enqueue_timeline(std::vector<PlaybackMessage>& msgs);
    // Messages that answer a command at `now_ns`: due at once. They go after a CancelUnstarted,
    // which is queued when `cancel` is set or there is anything to send.
    void enqueue_reactive(std::vector<PlaybackMessage>& msgs, std::int64_t now_ns, bool cancel);
    // Messages with a time of their own (the countdown ticks).
    void enqueue_plain(std::vector<PlaybackMessage>& msgs);
    void apply_mutes(std::int64_t now_ns);
    // Hands each accompaniment part its mute from acc_muted_.
    void apply_part_mutes(std::int64_t now_ns);
    // The loop as the session keeps it, cut to the song.
    void apply_loop(double start_s, double end_s, bool enabled, std::int64_t now_ns);
    // Reads which hands have notes (the loop, else the whole song) into present_; true when the
    // scope changed or `force` is set.
    bool read_hands_scope(bool force);
    // read_hands_scope(), then, when what is practised changed, a restart of the evaluators.
    void refresh_hands_scope(std::int64_t now_ns);
    // Builds both evaluators over the loaded song for the hands and the transposition in force.
    // `same_song`: only the options changed, so the tables they hold are kept.
    void rebuild_evaluators(std::int64_t now_ns, bool same_song = false);
    // The hands asked for or the transposition asked for is not what the evaluators grade for.
    bool tables_pending() const;
    bool tables_inline() const;
    // Brings the evaluators to what is asked for: on the spot for a small song, else asks the
    // worker for the tables, takes its answer when it is in and puts the change in force.
    void update_tables(std::int64_t now_ns);
    void request_tables();
    void retire_tables();
    void install_tables(TablesResult&& tables, std::int64_t now_ns);
    // The song under the evaluators changed (a load, a split, an unload): tables built for the old
    // one are never used.
    void drop_tables();
    // Takes over a finished split (HandSplitJob): the song's hands, and every table built from
    // them.
    void poll_hand_split(std::int64_t now_ns);
    void restart_here(std::int64_t now_ns); // seek in place: evaluators start fresh
    // Wait Mode: hold, release or arm the clock as the evaluator says.
    void follow_wait(std::int64_t now_ns);
    void collect_grades(std::int64_t now_ns);
    // Clears the combo events, keeping the combo in force. `whole_run` (a
    // song load or the run's end) also clears the run's press count, mean and calibration
    // suggestion; a seek does not, since the suggestion fires at most once per run.
    void reset_feel(bool whole_run);
    void collect_wrongs(std::int64_t now_ns);
    void finish_run(std::int64_t now_ns);
    void start_run_if_needed(std::int64_t now_ns);
    void discard_run();
    void watch_run_setup();
    void start_countdown(std::int64_t now_ns);
    void cancel_countdown();
    // Starts the clock, counting in first when `count_in`.
    void start_transport(std::int64_t now_ns, bool count_in);
    // Whether a start counts in: every start (a play from a stop, a play after a seek, a restart,
    // the resume after the settings close) when the count-in is on; in Wait Mode only when the
    // song starts from its beginning. A loop wrap never starts one: the clock does not stop there.
    bool count_in_wanted() const;
    CountInInput count_in_input() const;
    // Hands the ops of plan_ops_ over to Playback, with the metronome's clicks inside each Move.
    void hand_over_ops();
    double bar_start_at(double song_s) const;

    std::vector<AccompanimentPart> acc_parts_;
    std::vector<std::uint32_t> acc_muted_;
    bool loaded_ = false;
    // Declaration order matters: split_job_ (below) must be destroyed before song_, so its worker
    // is joined before the song it may be copying goes (the members go in reverse order).
    LoadedSong song_;
    NoteWindow window_;
    std::size_t densest_window_ = 0;
    mutable std::vector<HintKey> hint_keys_; // waiting_keys()'s answer, kept so a call allocates nothing
    // What the song's playback and the audition's hold, shared: the audition takes it when one
    // starts, not at every install.
    Playback::Prepared playback_tables_;
    std::uint64_t playback_fingerprint_ = 0;
    bool audition_stale_ = true; // audition_pb_ holds no song yet
    PreparedSong* prepared_ = nullptr;   // only while load() runs: what rebuild_evaluators takes
    std::uint64_t prepared_fingerprint_ = 0; // the loaded song's, while load() runs
    Transport transport_;
    Playback playback_;
    // The audition's own clock and piano feed (see audition()).
    Transport audition_tp_;
    Playback audition_pb_;
    std::vector<TransportOp> audition_ops_;
    bool auditioning_ = false;
    double audition_end_s_ = 0.0;
    // audition_repeating(): the range to start again, while it is repeating.
    bool audition_repeating_ = false;
    double audition_repeat_start_s_ = 0.0;
    double audition_repeat_end_s_ = 0.0;
    std::uint32_t load_serial_ = 0;
    std::uint32_t notes_serial_ = 0;
    // key_range(), kept for the song and transposition it was worked out for.
    mutable std::optional<KeyRange> key_range_;
    mutable bool key_range_valid_ = false;
    mutable std::uint32_t key_range_serial_ = 0;
    mutable std::uint64_t key_range_version_ = 0;
    std::vector<LoopPass> loop_passes_;
    bool ungraded_run_ = false;
    FlowEvaluator flow_;
    WaitEvaluator wait_;
    TranspositionSource source_;
    KeyRange keyboard_{0, 127};
    int semitones_ = 0;
    // The octave-shift offer: the fits for the song and keyboard they were worked out for, the
    // detector fed by the player's note-ons, the offer's state, and the songs (by content hash)
    // the player asked never to be offered a shift for.
    ShiftFits fits_;
    std::uint32_t fits_serial_ = 0;
    bool fits_valid_ = false;
    OctaveShiftDetector shift_detector_{kRange61};
    ShiftOfferState offer_;
    bool offer_settled_by_choice_ = false; // a saved transposition came with the song
    bool never_offer_ = false; // for the open song: the player asked never to be offered a shift
    int keyboard_shift_ = 0;
    std::optional<int> keyboard_shift_asked_;
    void refresh_shift_fits();
    void feed_shift_detector(std::span<const MidiEvent> player_events);

    SongOptions options_;
    PracticeMode mode_ = PracticeMode::Flow;
    HandFilter hands_ = HandFilter::Both; // asked for; practice_hands() is what applies
    // The split of the hands (see hand_split()): the player's choice, the one the song's hands were
    // made by, the worker that makes another, and what the marker stands at while the split is
    // Auto.
    int split_point_ = kAutoSplit;
    int applied_split_ = kAutoSplit;
    std::uint64_t split_serial_ = 0;
    std::unique_ptr<HandSplitJob> split_job_; // after song_: see there
    // The open song, lent to split_job_'s worker to copy (lent_song.hpp). Every write of `song_`
    // after the first holds its mutex.
    std::shared_ptr<LentSong> lent_;
    // The tables for another hand choice or key of a long song (update_tables). `tables_serial_`
    // goes up with every load, split and unload, and an answer built under another is dropped;
    // `wanted_` is the transposition last asked for, which source_ takes with its tables.
    std::unique_ptr<PracticeTablesJob> tables_job_;
    std::uint64_t tables_serial_ = 0;
    std::size_t inline_table_notes_ = kInlineTableNotes;
    Transposition wanted_;
    struct TablesAsked {
        HandFilter hands = HandFilter::Both;
        Transposition transposition;
    };
    std::optional<TablesAsked> tables_asked_; // the request in flight, if any
    mutable int suggested_split_ = 0; // 0: not worked out yet for these hands
    double split_seconds_ = 0.0;
    int splits_applied_ = 0;
    int evaluator_rebuilds_ = 0;
    HandsPresent present_;                // the hands with notes in the scope
    bool scope_loop_ = false;
    HandFilter applied_hands_ = HandFilter::Both; // what the evaluators were built with
    double scope_start_s_ = 0.0;
    double scope_end_s_ = 0.0;
    SongSound sound_ = SongSound::Mute;
    std::optional<double> loop_a_;
    // Every value the cached status line prints, as the line reads it: the hands and the sound in
    // effect (a loop can change both without anything being asked), not what was asked for.
    struct StatusKey {
        bool loaded = false;
        PracticeMode mode = PracticeMode::Flow;
        HandFilter hands = HandFilter::Both;
        int speed = 0;
        int bar = 0;
        int bar_count = 0;
        int written_bar = 0;
        bool loop_enabled = false;
        bool loop_a_set = false;
        SongSound sound = SongSound::Mute;
        int semitones = 0;
        int combo = 0;
        TransportState state = TransportState::Paused;
        bool counting_down = false;
        friend bool operator==(const StatusKey&, const StatusKey&) = default;
    };
    mutable StatusKey status_key_;
    mutable std::string status_title_; // the title it was built for, compared without a copy
    mutable std::string status_text_;
    // Where each part of `status_text_` is: its bytes, so the parts are views, not copies.
    struct StatusSpan {
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
        StatusRank rank = StatusRank::Title;
    };
    static constexpr int kMaxStatusSpans = 12;
    mutable std::array<StatusSpan, kMaxStatusSpans> status_spans_{};
    mutable int status_span_count_ = 0;
    mutable bool status_valid_ = false;
    std::optional<double> loop_b_;

    std::vector<TransportOp> ops_;
    std::vector<PlaybackMessage> scratch_;
    std::deque<SongSoundEvent> outbox_;
    // The song time up to which the sound has been handed over; unset when nothing plays.
    std::optional<double> sound_cursor_;
    int planned_wraps_ = 0; // loop wraps the plan has made, on the transport's count
    // What the plan rests on; when it changes, the plan is made again.
    struct Plan {
        int speed_percent = 100;
        bool loop_on = false;
        double loop_start_s = 0.0;
        double loop_end_s = 0.0;
        std::uint64_t transposition = 0;
        std::uint32_t clicks = 0;
    } plan_;
    std::vector<TransportOp> plan_ops_;
    BeatGrid grid_;
    std::vector<TransportOp> click_ops_;
    int count_in_bars_ = 0; // the app sets it from the session, where one bar is the default
    int count_in_next_ = 0; // the next count-in beat to plan
    std::uint32_t click_version_ = 0;
    std::int64_t last_due_ns_ = INT64_MIN;
    std::int64_t last_now_ns_ = 0;
    std::function<void()> event_pull_;
    bool pulling_ = false;     // take_in() reached through the pull: no second pull inside it
    std::size_t taken_in_ = 0; // how many of the frame's player events take_in() has applied

    // The run: from the first play after a load or a finish, to the end, a replay, a song or
    // choice change, or quit.
    bool run_active_ = false;
    // The speed, transposition and look-ahead the run began with, and whether any has been
    // different since: a run played at two speeds, in two keys or with two reaches is one run (its
    // summary is kept), but its events are no pass at a single setup, so it is never complete.
    int run_speed_percent_ = 100;
    int run_semitones_ = 0;
    double run_lookahead_s_ = 0.0; // the look-ahead the run started with
    bool run_setup_changed_ = false;
    std::int64_t run_started_ns_ = 0;
    std::int64_t run_playing_ns_ = 0;
    std::vector<PassResult> passes_;
    std::vector<FinishedRun> finished_;
    std::int64_t profile_id_ = kDefaultProfileId;
    // Every graded note and wrong press of the run so far, in a buffer sized once (the frame fills
    // it from the evaluator's results; nothing is allocated until the run ends).
    RunEventRecorder events_;

    std::vector<GradeMark> grades_;
    std::vector<WrongMark> wrongs_;
    std::vector<WrongPress>
        wrong_scratch_; // collect_wrongs' take, kept so a frame allocates nothing
    std::vector<std::int8_t> grade_by_note_;
    std::size_t results_seen_ = 0;
    GameFeel feel_;
    ComboEvent combo_milestone_;
    ComboEvent combo_break_;
    std::size_t losses_seen_ = 0; // into the summary's combo_losses

    bool menu_open_ = false;
    bool resume_after_menu_ = false;
    std::optional<std::int64_t> countdown_end_ns_;
    int countdown_ticks_left_ = 0;
};

// "Right 92%, Left 85% · longest combo 41 · up 4% on last time": the results of a run, accuracy for
// each hand that had graded notes.
std::string results_line(const RunRecord& record, const AttemptChange& change,
                         bool comparable = true);

} // namespace etude::app
