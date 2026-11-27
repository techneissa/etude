// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/evaluator_results.hpp"
#include "etude/hand.hpp"
#include "etude/midi_event.hpp"
#include "etude/note_window.hpp"
#include "etude/song.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace etude {

// Flow Mode: the song keeps going and each expected note gets one grade.
//
// Inputs, in the order a frame produces them: the Transport's ops (apply), then the player's MIDI
// events with their arrival stamps (apply), then update(now_ns). Times are on the Transport's
// clock. Nothing reads a clock.
//
// Rules:
// - Timing is in wall time, so the window means the same at every speed. A press's time is its
//   stamp minus latency_ms. error = press - arrival, where arrival is when the song clock reached
//   the note's start (from the Moves, so speed changes are exact).
// - A press matches a note when the key is the note's expected pitch (Transposition) and
//   |error| <= window. Both edges are inside the window. |error| <= perfect_fraction * window is
//   Perfect, <= good_fraction * window is Good, otherwise Early (error < 0) or Late.
// - Each press satisfies at most one expected note and each note at most one press. Among all
//   such assignments the evaluator takes the one with the most matches, then the least total
//   |error|. A press that matches nothing is a wrong note: counted, never able to turn a match
//   into a miss. A note with no press is Missed.
// - Notes of the same expected pitch whose starts are within kUnisonSeconds (both hands on one
//   key, or two notes folded onto one key) are one key press: a single press grades them all. Wait
//   Mode merges them the same way, so both modes agree on how many strikes a passage takes.
// - A press that matches nothing is not wrong when it is the key of a practised note starting
//   within lookahead_s after it (song seconds; set it to the display's NoteWindowOptions::ahead_s,
//   so a key the player can see coming is never wrong). It is pressed early and simply not
//   counted. A press after its note's window is still wrong.
// - Only notes of the practised hand(s) are graded: HandFilter::Both grades Left, Right and
//   Unknown; Left or Right grades only that hand. Drum notes are never graded.
// - Combo: +1 per hit note; Missed and wrong presses reset it to 0. Results become final in time
//   order, so the combo follows the order the notes came in.
// - Results are final once nothing that comes later could change them: a group of notes and
//   presses is decided when the latest of them is more than a window behind both the song clock
//   and now - settle_ms. Events stamped earlier than that which arrive later are too late (a press
//   is then matched only against notes still undecided).
// - An event that could not be judged (apply_unsure: the microphone heard a note but not which) is
//   not a press: it is never a wrong note. A note with no matching press whose window holds such
//   an event is neither hit nor missed (counted in unjudged); a press that does match still hits
//   it.
// - Presses before the first play, and from one window after a pause or the song's end until
//   the next play, are ignored, so noodling never counts as wrong. A late press just after a
//   pause still hits.
// - A loop wrap starts a new pass; a late press just after the wrap can still hit the last note
//   of the old pass. A seek drops every undecided note and press (neither hit nor missed) and
//   starts a new pass.
// - A change of transposition (set_transposition or a followed TranspositionSource) acts like a
//   seek in place: undecided notes and presses are dropped, notes are expected in the new key from
//   then on, presses that match nothing in the next window are not wrong, and the pass is not
//   clean.
//
// Options are clamped to the ranges below; a NaN takes the default.
struct FlowOptions {
    double window_ms = 80.0;        // clamped to 10..500
    double perfect_fraction = 0.35; // of the window, clamped to 0..1
    double good_fraction = 0.70;    // of the window, clamped to perfect_fraction..1
    double latency_ms = 0.0;        // the player's measured lag, subtracted from each press; ±1000
    double settle_ms = 15.0;        // how late an event may arrive behind its stamp; 0..1000
    HandFilter hands = HandFilter::Both;
    Transposition transposition{}; // ignored while following a TranspositionSource
    double lookahead_s = 4.0;      // see "not wrong" above; 0..60
};

class FlowEvaluator {
    struct Target {
        double start_s = 0.0;
        int key = 0;
        int measure = 1;
        std::vector<std::uint32_t> notes;
    };

  public:
    FlowEvaluator() { reset_clock(); }
    explicit FlowEvaluator(const Song& song, const FlowOptions& options = {}) {
        reset_clock();
        set_song(song, options);
    }

    // The tables set_song() builds from a song, made ahead on another thread (a loader's) so the
    // frame thread only takes them over. They depend on the song, the hands practised and the
    // transposition; set_song() uses them only when those are what it was asked for, else it builds
    // its own.
    class Prepared {
      public:
        std::size_t note_count() const { return notes_ != nullptr ? notes_->size() : 0; }

      private:
        friend class FlowEvaluator;
        std::shared_ptr<const std::vector<Note>> notes_;
        std::shared_ptr<const std::vector<Measure>> measures_;
        std::vector<Target> targets_;
        std::vector<double> unplayable_s_;
        std::size_t practised_ = 0;
        HandFilter hands_ = HandFilter::Both;
        Transposition transposition_{};
        std::uint64_t fingerprint_ = 0; // song_fingerprint of the song the tables were built from
        bool built_ = false;
    };
    // Builds the tables for `song` under `hands` and `transposition`. Touches no evaluator, so any
    // thread may call it.
    static Prepared prepare(const Song& song, HandFilter hands, const Transposition& transposition);
    // As above for a caller that has the song's fingerprint already (song_fingerprint(song)).
    static Prepared prepare(const Song& song, HandFilter hands, const Transposition& transposition,
                            std::uint64_t song_fingerprint);
    // The notes and bars as an evaluator holds them, shared: tables for another hand choice or
    // another key are built from these on another thread without a copy of the song, and the
    // tables made from them share them again.
    using NoteTable = std::shared_ptr<const std::vector<Note>>;
    using MeasureTable = std::shared_ptr<const std::vector<Measure>>;
    NoteTable note_table() const { return notes_; }
    MeasureTable measure_table() const { return measures_; }
    // Builds the tables over a note table taken from note_table() (or made once for both
    // evaluators); `song_fingerprint` is the song's. `abort`, when given, is asked every few
    // thousand notes and between the phases: once it says true the build stops and the result is
    // empty (note_count() is 0), so a worker that is told to quit does not finish a long build.
    using AbortCheck = std::function<bool()>;
    static Prepared prepare(NoteTable notes, MeasureTable measures, HandFilter hands,
                            const Transposition& transposition, std::uint64_t song_fingerprint,
                            const AbortCheck& abort = {});
    // Keeps `prepared` for the next change of transposition: set_transposition() takes its targets
    // when they were built from this evaluator's very notes (the same table, not a look-alike), for
    // the hands it grades and the transposition it is asked for, else builds its own. A table that
    // is never wanted costs nothing but memory until the next set_song().
    void offer_targets(Prepared&& prepared);
    // The targets a set_song() with tables made ahead, or a change of transposition that took an
    // offer, replaced. Freeing a long song's targets takes milliseconds (a heap vector for each),
    // so the evaluator keeps them for its owner to free on another thread instead of on the frame
    // that swapped them. Empty (and cheap) when nothing was retired.
    // How many times this evaluator built its own targets (set_song and set_transposition without
    // tables that fit), for tests.
    bool has_retired() const { return !retired_.empty(); }
    std::vector<Prepared> take_retired() {
        std::vector<Prepared> out;
        out.swap(retired_);
        retired_.reserve(4); // the next swaps push without a call to the heap
        return out;
    }

    // Copies what it needs from the song and resets the results, undecided notes and presses. It
    // keeps what it has seen of the Transport's clock: if the transport keeps playing, the new
    // song's notes are graded from its next Move (presses within one window after are never
    // wrong); after Transport::set_song, its Pause and Seek ops say the clock stopped, in either
    // order, and grading starts again at the next Play.
    void set_song(const Song& song, const FlowOptions& options);
    // As above, taking `prepared` over (moved from) when it was built from this very song (its
    // fingerprint), for these options' hands and the transposition that applies; otherwise it is
    // ignored. A caller that has the song's fingerprint already passes it, to save a pass.
    void set_song(const Song& song, const FlowOptions& options, Prepared&& prepared);
    void set_song(const Song& song, const FlowOptions& options, Prepared&& prepared,
                  std::uint64_t song_fingerprint);
    const FlowOptions& options() const { return options_; }
    // New options, the song started over (results, undecided notes and presses, as set_song does),
    // with the tables kept: for a change that does not move the targets (the hit window, the
    // latency, the settle time). Returns false and changes nothing when `options` ask for other
    // hands than the tables were built for. The transposition stays the one the tables are for; a
    // change the source made since is still picked up at the next apply() or update().
    bool reconfigure(const FlowOptions& options);
    // Sets lookahead_s on the running evaluator (clamped like the option). It is only read when a
    // press is judged "not wrong", so nothing is rebuilt, and notes and presses already queued
    // keep their place. Each press is judged by the value in force when apply() took it, so a
    // change reaches only the presses after it.
    void set_lookahead(double lookahead_s);

    // Expects notes in the new key from `now_ns` (see the rules above). The same value does
    // nothing.
    void set_transposition(const Transposition& transposition, std::int64_t now_ns);
    // Reads the transposition from the session's one TranspositionSource instead: a change is
    // picked up at the next apply() or update(), as set_transposition() at the source's
    // changed_at_ns(). The source must outlive the evaluator, or be unset with follow(nullptr).
    void follow(const TranspositionSource* source);

    void apply(const TransportOp& op);
    // NoteOn with velocity > 0 is a press. Releases, the pedal and synthetic events change nothing.
    void apply(const MidiEvent& ev);
    // An event at wall time `ns` that could not be judged; see the rules above.
    void apply_unsure(std::int64_t ns);
    // Decides what can be decided by `now_ns`.
    void update(std::int64_t now_ns);
    // Decides everything now, as if no more input could come (for the results screen).
    void finish();
    // Clears the results but keeps the song, options, clock and undecided notes. The pass in
    // progress becomes pass 0 and is never clean, since it lost results.
    void reset_results();

    const EvaluationSummary& summary() const { return summary_; }
    // Appends finished passes to `out` and forgets them.
    void take_passes(std::vector<PassResult>& out);
    // Appends the presses decided wrong since the last call to `out` and forgets them. A press is
    // decided a window and the settle time after it, so this lags the press by about that.
    void take_wrong_presses(std::vector<WrongPress>& out);
    // How many wrong presses were decided since the last call that did not fit in
    // kMaxKeptWrongPresses and so never reached take_wrong_presses(); forgets the count. The
    // summary counts them.
    int take_dropped_wrong_presses();
    // The start (song seconds) of the note the latest press on `key` hit, by the rule that grades
    // it: the key is the note's expected pitch and the press is within the window of the note's
    // arrival. Known at the press, before the note's result is final; nullopt when that press
    // matched no note (or was ignored, or `key` was never pressed). The note nearest the press
    // wins, and notes of one unison share a start. A hint for the screen: the result is final only
    // in summary().
    // A note a key's previous press already struck is not struck again by the next press on it:
    // that press is a second strike of one note, and is wrong.
    std::optional<double> struck_start(int key) const;
    // Whether the latest press on `key` looks wrong as it is made: it struck no note and is not one
    // pressed early for a note still to come (or ignored: before play, while paused). Known at the
    // press, where the result is final only later; what the screen uses to keep a wrong press
    // from sparking.
    bool press_looks_wrong(int key) const;

    // How many notes of the song this evaluator grades (the practised hands' playable notes): what
    // a pass through the whole song gives results for.
    std::size_t practised_notes() const;

    // Undecided notes and presses, for tests.
    std::size_t pending_notes() const { return pending_.size(); }
    std::size_t pending_presses() const { return presses_.size(); }

  private:
    struct Pending {
        std::uint32_t target = 0;
        std::int64_t arrival_ns = 0;
        int pass = 0;
        bool done = false;
    };
    struct Press {
        int key = 0;
        std::int64_t time_ns = 0;
        bool done = false;
        int velocity = 0;
        double lookahead_s = 0.0; // the look-ahead in force when it was played: its verdict is its own
    };
    // One cell of decide_group's table: matches so far and their total |error|.
    struct MatchCell {
        int count = 0;
        std::int64_t cost = 0;
    };
    struct Outcome {
        std::int64_t time_ns = 0;
        bool wrong = false; // a press that matched nothing; else a target's result
        int key = 0;        // the wrong press's key
        int velocity = 0;   // of the press that matched, or the wrong one
        std::uint32_t target = 0;
        int pass = 0;
        std::int64_t arrival_ns = 0;
        std::int64_t press_ns = 0;
        bool hit = false;
        bool unjudged = false; // no press, but an event that could not be judged was in the window
    };

    void reset_clock();
    void apply_options(const FlowOptions& options);
    void start_song();
    // `practised` gets the notes the targets hold, counted as they are built: summing them
    // afterwards walks a heap vector per target.
    static std::vector<Target> targets_for(const std::vector<Note>& notes, HandFilter hands,
                                           const Transposition& transposition,
                                           std::vector<double>& unplayable_s,
                                           std::size_t& practised, const AbortCheck& abort = {});
    void build_targets();
    void reserve_working_space();
    void sync_transposition();
    // Decides what is decided by `at_ns`, drops the rest, and ignores presses stamped before it.
    void drop_undecided(std::int64_t at_ns);
    void resolve(std::int64_t frontier);
    // Decides one closed group: notes [a0, a0 + n) of pending_ and presses [b0, b0 + m).
    void decide_group(std::size_t a0, std::size_t n, std::size_t b0, std::size_t m);
    void apply_outcomes(std::int64_t safe);
    void apply_outcome(const Outcome& o);
    bool ignored(std::int64_t ns) const;
    // The start of the target on `key` nearest the song place of a press at (corrected) time `ns`
    // within the window, else NaN.
    double nearest_start(int key, std::int64_t ns, double except_s) const;
    // Forgets what the keys' latest presses struck: a seek, a loop wrap or a new key moves the
    // song from under them.
    void forget_presses();
    // A press that matched nothing but is the key of a note sounding or coming soon.
    bool excused(const Press& p) const;
    int measure_at(double song_s) const;
    NoteGrade grade(std::int64_t error_ns) const;

    FlowOptions options_;
    std::int64_t window_ns_ = 80'000'000;
    std::int64_t perfect_ns_ = 0;
    std::int64_t good_ns_ = 0;
    std::int64_t latency_ns_ = 0;
    std::int64_t settle_ns_ = 0;

    NoteTable notes_ = std::make_shared<const std::vector<Note>>();
    std::vector<Target> targets_;      // sorted by start
    std::vector<double> unplayable_s_; // starts of practised notes no key can play, sorted
    std::size_t practised_ = 0;        // the notes the targets hold
    MeasureTable measures_ = std::make_shared<const std::vector<Measure>>();
    Prepared offered_;                 // offer_targets()'s, kept for the next set_transposition()
    std::vector<Prepared> retired_;    // the tables the last swaps replaced, for take_retired()
    void retire_targets();             // moves targets_ and offered_ into retired_

    TransportTimeline timeline_;
    std::vector<Pending> pending_;
    std::vector<Press> presses_;
    std::vector<Outcome> outcomes_;
    // decide_group's working tables, kept between groups so a frame decides without the heap
    // (reserved in set_song for the usual group; a longer one grows them once).
    std::vector<std::size_t> match_lo_;
    std::vector<std::size_t> match_hi_;
    std::vector<std::size_t> match_row_;
    std::vector<MatchCell> match_cells_;
    std::vector<long> match_pick_;
    std::vector<char> match_used_;
    std::vector<std::int64_t> unsure_; // wall times of events that could not be judged
    std::vector<WrongPress> wrong_presses_;  // decided wrong, not yet taken
    int dropped_wrong_presses_ = 0;          // decided wrong past the cap, not kept
    std::array<double, 128> struck_start_{}; // per key, see struck_start(); NaN for none
    std::array<bool, 128> wrong_looking_{};  // per key, see press_looks_wrong()
    detail::PassLog passes_;
    // Wall-time spans where presses are ignored: [first, second). The last may be open.
    std::vector<std::array<std::int64_t, 2>> dead_;
    std::int64_t ignore_before_ns_ = 0;
    // Presses stamped in [first, second) that match nothing are not wrong (after a key change).
    std::array<std::int64_t, 2> grace_{0, 0};
    std::int64_t clock_ns_ = 0;
    bool moving_ = false;

    const TranspositionSource* source_ = nullptr;
    std::uint64_t source_version_ = 0;
    bool source_synced_ = false;

    EvaluationSummary summary_;
};

// Wait Mode: the song stops at each chord until it is played.
//
// Same inputs as FlowEvaluator. update() returns what the Transport should do now.
//
// Rules:
// - A chord is the practised, playable notes whose starts are within kChordSeconds of the
//   chord's first note, with notes on one key merged (kUnisonSeconds). The evaluator asks the
//   Transport to hold at the start of the next unplayed chord.
// - A chord is played when all its keys are down at the same moment. A key counts only if it was
//   pressed at most early_ms (wall time, at the speed of that moment) before the song clock
//   reaches the chord, and its press has not already played an earlier chord. So a key held from
//   before does not count until it is pressed again, and one press never plays two chords.
// - A press stamped while paused, or before the first play, never plays a chord; the key must be
//   pressed again after play.
// - A note-on for a key the evaluator thinks is down is a new strike: its key-up was lost (a
//   dropped event, a hot-plug). release_all() lets go of every key at once for a lost connection.
// - A chord may be played before the clock reaches it; the clock then does not stop for it.
// - With hold_full_length, each note of a played chord must stay down until release_slack_ms
//   before its end. Letting go earlier holds the song where it is until that key is pressed again,
//   and marks the note released_early.
// - A press is wrong when its key is in neither the waiting chord, nor a later chord starting
//   within lookahead_s (song seconds) of the press, nor a practised note still sounding. A key
//   pressed early for a later chord is not wrong, and does not play that chord either. Wrong
//   presses are counted in the waiting chord's bar and never block the chord. Ignored while paused.
// - Key state comes from note-on and note-off only (velocity-0 note-on is a release); the sustain
//   pedal never holds a key. All Notes Off and All Sound Off release every key. Events are applied
//   in stamp order once now - settle_ms has passed them; a late event older than the last one
//   applied for its key is dropped.
// - A seek or loop wrap makes the first chord at or after the new position the waiting one, and
//   every key down at that moment must be pressed again. So does a change of transposition or of
//   song (set_song keeps the clock, as FlowEvaluator::set_song does), and the pass is not clean.
//
// Options are clamped to the ranges below; a NaN takes the default.
struct WaitOptions {
    double early_ms = 150.0; // 0..2000
    bool hold_full_length = false;
    double release_slack_ms = 100.0; // hold_full_length: letting go this close to the end is fine;
                                     // 0..10000
    double settle_ms = 15.0;         // 0..1000
    HandFilter hands = HandFilter::Both;
    Transposition transposition{}; // ignored while following a TranspositionSource
    double lookahead_s = 4.0;      // see "wrong" above; 0..60
};

// What the caller does to the Transport after update(), in this order: hold_now -> hold(now),
// release -> release(now), arm_hold -> hold_at(now, hold_at_s). Repeating a directive is harmless.
struct WaitDirective {
    bool hold_now = false;
    bool release = false;
    bool arm_hold = false;
    double hold_at_s = 0.0;
};

class WaitEvaluator {
    struct ChordKey {
        int key = 0;
        std::vector<std::uint32_t> notes;
    };
    struct Chord {
        double start_s = 0.0;
        int measure = 1;
        std::vector<ChordKey> keys;
    };

  public:
    WaitEvaluator() { reset_clock(); }
    explicit WaitEvaluator(const Song& song, const WaitOptions& options = {}) {
        reset_clock();
        set_song(song, options);
    }

    // As FlowEvaluator::Prepared: the chords of a song, made ahead on another thread.
    class Prepared {
      public:
        std::size_t note_count() const { return notes_ != nullptr ? notes_->size() : 0; }

      private:
        friend class WaitEvaluator;
        std::shared_ptr<const std::vector<Note>> notes_;
        std::vector<Chord> chords_;
        std::vector<double> unplayable_s_;
        std::size_t practised_ = 0;
        std::size_t bars_ = 0;
        HandFilter hands_ = HandFilter::Both;
        Transposition transposition_{};
        std::uint64_t fingerprint_ = 0;
        bool built_ = false;
    };
    static Prepared prepare(const Song& song, HandFilter hands, const Transposition& transposition);
    // As above for a caller that has the song's fingerprint already (song_fingerprint(song)).
    static Prepared prepare(const Song& song, HandFilter hands, const Transposition& transposition,
                            std::uint64_t song_fingerprint);
    // As FlowEvaluator::note_table, prepare over one and offer_targets.
    using NoteTable = std::shared_ptr<const std::vector<Note>>;
    NoteTable note_table() const { return notes_; }
    using AbortCheck = std::function<bool()>;
    static Prepared prepare(NoteTable notes, HandFilter hands, const Transposition& transposition,
                            std::uint64_t song_fingerprint, const AbortCheck& abort = {});
    void offer_targets(Prepared&& prepared);
    // As FlowEvaluator::take_retired, for the chords.
    // How many times this evaluator built its own chords (set_song and set_transposition without
    // tables that fit), for tests.
    bool has_retired() const { return !retired_.empty(); }
    std::vector<Prepared> take_retired() {
        std::vector<Prepared> out;
        out.swap(retired_);
        retired_.reserve(4);
        return out;
    }

    // As FlowEvaluator::set_song: keeps the clock, so update() holds at the new song's next chord.
    void set_song(const Song& song, const WaitOptions& options);
    void set_song(const Song& song, const WaitOptions& options, Prepared&& prepared);
    void set_song(const Song& song, const WaitOptions& options, Prepared&& prepared,
                  std::uint64_t song_fingerprint);
    const WaitOptions& options() const { return options_; }
    // As FlowEvaluator::reconfigure.
    bool reconfigure(const WaitOptions& options);
    // As set_song(), sharing `notes` (another evaluator's note_table()) instead of copying the song's.
    void set_song(const Song& song, const WaitOptions& options, NoteTable notes);
    // As FlowEvaluator::set_lookahead: the waiting chord, the keys down and the obligations stay.
    // What is queued with a stamp up to `now_ns` is judged first (`settle`), under the old value;
    // a press stamped later keeps the value it arrived with as well.
    void set_lookahead(double lookahead_s, std::int64_t now_ns);
    // Waits for chords in the new key from `now_ns`: the waiting chord is the first at or after
    // the position, and keys down must be pressed again. The same value does nothing.
    void set_transposition(const Transposition& transposition, std::int64_t now_ns);
    // As FlowEvaluator::follow.
    void follow(const TranspositionSource* source);

    void apply(const TransportOp& op);
    // Queues the event by its stamp; it is judged at update(). A press stamped before a change of
    // transposition, a seek, a wrap or a song change that is still queued when the change comes is
    // judged under the state it was played under (`settle`), so apply every event stamped before a
    // change before the change reaches the evaluator.
    void apply(const MidiEvent& ev);
    // Judges every queued event stamped at or before `up_to_ns` now, against the state in force.
    // The evaluator does it itself at each of those changes; a caller that is about to collect the
    // results of a run (a song switch ends it) calls it first.
    void settle(std::int64_t up_to_ns);
    // Every key up as of `now_ns`, as All Notes Off would, but not the player's doing (no
    // released_early). For a lost MIDI connection; CC120, CC123 and the queue's synthetic resync
    // event do the same.
    void release_all(std::int64_t now_ns);
    WaitDirective update(std::int64_t now_ns);
    // As FlowEvaluator::reset_results; a pending "press it again" for hold_full_length is dropped.
    void reset_results();

    const EvaluationSummary& summary() const { return summary_; }
    void take_passes(std::vector<PassResult>& out);
    // As FlowEvaluator::take_wrong_presses; a wrong press is known at once here.
    void take_wrong_presses(std::vector<WrongPress>& out);
    // As FlowEvaluator::take_dropped_wrong_presses.
    int take_dropped_wrong_presses();
    // The start (song seconds) of the chord holding the key the latest press on `key` struck: the
    // key is in a chord starting within early_ms of the press's place in the song. Nullopt when
    // that press struck none (a wrong key, a press while paused, a key never pressed).
    std::optional<double> struck_start(int key) const;
    // As FlowEvaluator::press_looks_wrong: a wrong press, known when the press is applied.
    bool press_looks_wrong(int key) const;
    // A press on `key` has been handed over but not applied yet (events wait settle_ms for older
    // ones from other ports), so press_looks_wrong() still speaks of the press before it.
    bool press_pending(int key) const;

    // How many notes of the song this evaluator grades; see FlowEvaluator::practised_notes.
    std::size_t practised_notes() const;

    // The start of the chord being waited for, or a negative number when none is left.
    double waiting_for_s() const;
    // Keys of the chord being waited for, after transposition, in key order. The span reads a
    // member that the next call, or anything that changes the chords, replaces.
    std::span<const int> waiting_keys() const;

  private:
    struct Key {
        bool down = false;
        bool consumed = false; // this press already played a chord
        std::int64_t press_ns = 0;
        double press_s = 0.0;
        double press_rate = 1.0;
        std::int64_t last_ns = 0;
        bool seen = false;
        int velocity = 0;
        double lookahead_s = 0.0; // in force when the press was played (see Inbound)
    };
    struct Obligation { // hold_full_length
        int key = 0;
        double end_s = 0.0;
        std::size_t result = 0; // index into summary_.notes of the first note on this key
        std::size_t result_count = 0;
        bool needs_repress = false;
    };
    struct Sounding {
        int key = 0;
        double end_s = 0.0;
    };

    void reset_clock();
    void apply_options(const WaitOptions& options);
    void start_song();
    // `practised` and `bars` get the notes the chords hold and the bars (runs of chords of one bar)
    // they cover, counted as they are built.
    static std::vector<Chord> chords_for(const std::vector<Note>& notes, HandFilter hands,
                                         const Transposition& transposition,
                                         std::vector<double>& unplayable_s, std::size_t& practised,
                                         std::size_t& bars, const AbortCheck& abort = {});
    void build_chords();
    void sync_transposition();
    void process(const MidiEvent& ev, double lookahead_s);
    void press(int key, std::int64_t ns, int velocity, double lookahead_s);
    void release_key(int key, std::int64_t ns, bool by_player);
    void try_play(std::int64_t ns);
    void restart_at(double song_s, std::int64_t ns);
    void forget_presses();
    bool expected_press(int key, double song_s, double early_s, double lookahead_s) const;
    void new_pass(double start_s, std::int64_t ns);

    WaitOptions options_;
    std::int64_t early_ns_ = 0;
    std::int64_t slack_ns_ = 0;
    std::int64_t settle_ns_ = 0;

    NoteTable notes_ = std::make_shared<const std::vector<Note>>();
    std::vector<Chord> chords_; // sorted by start
    std::vector<double> unplayable_s_;
    std::size_t practised_ = 0; // the notes the chords hold
    std::size_t bars_ = 1;      // the bars they cover, and one for the run in progress
    Prepared offered_; // offer_targets()'s, kept for the next set_transposition()
    std::vector<Prepared> retired_; // the tables the last swaps replaced, for take_retired()
    void retire_chords();           // moves chords_ and offered_ into retired_
    std::size_t next_ = 0; // the chord being waited for
    mutable std::vector<int> waiting_keys_; // waiting_keys()'s answer, kept so a call allocates nothing

    TransportTimeline timeline_;
    // An event waiting for settle_ms to pass, with the look-ahead in force when it arrived: a press
    // is judged by the reach the player saw when they played it, not by a later change.
    struct Inbound {
        MidiEvent ev;
        double lookahead_s = 0.0;
    };
    std::vector<Inbound> inbox_; // sorted by stamp
    std::array<Key, 128> keys_{};
    std::vector<Obligation> obligations_;
    std::vector<Sounding> sounding_;
    detail::PassLog passes_;
    double position_s_ = 0.0;
    std::int64_t last_op_ns_ = 0; // the latest time an op has reported
    bool playing_ = false;
    bool held_ = false;
    std::int64_t held_since_ns_ = 0;
    std::int64_t restart_ns_ = 0; // presses stamped before this belong to before a seek or wrap
    std::vector<std::array<std::int64_t, 2>> dead_;
    std::vector<WrongPress> wrong_presses_;
    int dropped_wrong_presses_ = 0;
    std::array<double, 128> struck_start_{};
    std::array<bool, 128> wrong_looking_{};

    const TranspositionSource* source_ = nullptr;
    std::uint64_t source_version_ = 0;
    bool source_synced_ = false;

    EvaluationSummary summary_;
};

} // namespace etude
