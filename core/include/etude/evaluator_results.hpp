// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/hand.hpp"
#include "etude/transport.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace etude {

// Where the song clock was at each wall time, rebuilt from the Transport's ops. The evaluators use
// it to place a key press in the song; it has no clock of its own.
class TransportTimeline {
  public:
    // Ops in the order the Transport produced them. Click ops are ignored.
    void apply(const TransportOp& op);
    // The song position at wall time `ns`: inside a Move it interpolates, between ops it is where
    // the last op left the clock. Before the first op it is where the first op started.
    double song_at(std::int64_t ns) const;
    // The playback rate (speed / 100) at wall time `ns`: the speed of the last Move at or before
    // it, 1.0 before any Move.
    double rate_at(std::int64_t ns) const;
    // Drops history before `ns`, keeping the op in force at `ns`.
    void forget_before(std::int64_t ns);
    void clear() { spans_.clear(); }

  private:
    struct Span {
        std::int64_t at_ns = 0;
        std::int64_t end_ns = 0; // == at_ns except for a Move
        double from_s = 0.0;
        double to_s = 0.0;
        int speed_percent = 100;
    };
    std::vector<Span> spans_; // in op order, so sorted by at_ns
};

enum class NoteGrade : std::uint8_t {
    Perfect,
    Good,
    Early,
    Late,
    Missed,
    Played, // Wait Mode: the note was played; Wait Mode has no timing grade
};

// What became of one expected note.
struct NoteResult {
    std::uint32_t note_index = 0; // into Song::notes
    int pass = 0;                 // see PassResult
    NoteGrade grade = NoteGrade::Missed;
    int expected_pitch = 0; // the key expected, after transposition
    Hand hand = Hand::Unknown;
    int measure = 1;
    double song_s = 0.0;         // the note's start
    std::int64_t arrival_ns = 0; // when the song clock reached the note
    std::int64_t press_ns = 0;   // Flow: the matched press, latency corrected; Wait: when the
                                 // chord was complete. 0 when Missed.
    std::int64_t error_ns = 0;   // Flow: press - arrival (negative is early). Wait: how long the
                                 // song waited for the chord (0 if it never stopped).
    bool released_early = false; // Wait Mode with hold_full_length: let go before the end
    // The press that earned it, for the run's record (run_events.hpp). All 0 for a Missed note.
    int velocity = 0;           // the matched press's MIDI velocity, 1..127
    double press_song_s = 0.0;  // where the song clock was at that press (Flow: latency corrected)
    // press - the note's due time, in wall time, negative is early. Flow: error_ns. Wait: how
    // early the key went down before the song reached the chord (0 once the song was waiting).
    std::int64_t timing_error_ns = 0;
};

// A press that matched no expected note, for the brief mark at the hit line. `time_ns` is the
// press's stamp (Flow Mode: less the latency correction).
struct WrongPress {
    int key = 0;
    std::int64_t time_ns = 0;
    int velocity = 0;       // 1..127
    double song_s = 0.0;    // where the song clock was at the press
};

// The most wrong presses an evaluator keeps for take_wrong_presses(); more in one frame are only
// counted in the summary.
inline constexpr std::size_t kMaxKeptWrongPresses = 32;

struct GradeCounts {
    int perfect = 0;
    int good = 0;
    int early = 0;
    int late = 0;
    int missed = 0;
    int played = 0; // Wait Mode
};

struct HandStats {
    int graded = 0; // notes with a final result
    int hit = 0;    // every grade except Missed
    // hit / graded; empty when nothing was graded yet, which is no accuracy at all (not 100%).
    std::optional<double> accuracy() const {
        if (graded <= 0) {
            return std::nullopt;
        }
        return static_cast<double>(hit) / static_cast<double>(graded);
    }
};

// One bar of the trouble-spot map.
struct MeasureStats {
    int measure = 1;
    int graded = 0;
    int missed = 0;
    int wrong = 0; // wrong presses placed in this bar
};

// The result of a run, as plain values so it can be saved as it is.
struct EvaluationSummary {
    GradeCounts counts;
    // Presses that matched no expected note. Counted on their own; they never change the grade
    // of a note.
    int wrong = 0;
    // Notes of the practised hands that no key of the keyboard can play (Transposition gave
    // playable = false). Neither hit nor missed.
    int unplayable = 0;
    // Notes whose window held an event that could not be judged (FlowEvaluator::apply_unsure) and
    // that no press matched: neither hit nor missed, and the combo does not change.
    int unjudged = 0;
    int combo = 0;
    int longest_combo = 0;
    // Each combo (1 or more) that a Miss or a wrong press reset to 0, in order. A frame that
    // decides a Miss and a later hit together shows only the combo after both; this keeps the
    // one that was lost (game_feel.hpp's Break). Reserved with the results, so no allocation.
    std::vector<int> combo_losses;
    HandStats left;
    HandStats right;
    HandStats unknown; // notes with Hand::Unknown, graded when practising both hands
    HandStats all;
    std::vector<MeasureStats> measures; // sorted by measure; only bars that have something
    std::vector<NoteResult> notes;      // in the order results became final
};

// One pass through the A-B loop, reported once every note of it has a final result.
struct PassResult {
    int pass = 0;
    double start_s = 0.0; // where the pass began: the loop start, or a seek or play position
    double end_s = 0.0;   // the loop end it wrapped from
    int graded = 0;
    int missed = 0;
    int wrong = 0;
    // The graded notes by grade (Wait Mode counts `played` instead) and the longest run of hits in
    // the pass, a wrong press ending it, as the results card counts them for a whole run.
    int perfect = 0;
    int good = 0;
    int early = 0;
    int late = 0;
    int played = 0;
    int longest_combo = 0;
    bool released_early = false; // Wait Mode
    // Began at or before the loop start, had no seek inside, no miss, no wrong press and (Wait
    // Mode) no note let go early. This is what Transport::report_pass wants.
    bool clean = false;
};

namespace detail {
// Bookkeeping for one pass, shared by both evaluators.
struct PassState {
    double start_s = 0.0;
    double end_s = 0.0;
    std::int64_t start_ns = 0;
    std::int64_t end_ns = 0;
    double loop_start_s = 0.0; // where the wrap that ended it jumped to
    bool ended = false;
    bool interrupted = false; // a seek inside it
    bool reported = false;    // every result of it is final
    bool dropped = false;     // ended by a seek: never reported to the caller
    bool moved = false;
    int combo = 0; // the run of hits so far
    PassResult result;
};

// The passes not yet handed to the caller, oldest first, numbered from `base`. Passes end and are
// reported in order, so the reported ones are a prefix: each frame looks only at the passes after
// it, and take() forgets the prefix. However many loops, it holds the passes the caller has not
// taken plus the one in progress.
class PassLog {
  public:
    // Keeps only `current`, renumbered 0.
    void reset(PassState current);
    PassState& back() { return passes_.back(); }
    const PassState& back() const { return passes_.back(); }
    // The pass with this number; it must not have been taken.
    PassState& at(int number) { return passes_[static_cast<std::size_t>(number - base_)]; }
    // Appends `p` as the newest pass and numbers it.
    void push(PassState p);
    // The newest pass that started at or before `ns`, or the oldest kept.
    int number_at(std::int64_t ns) const;
    // Marks ended passes reported, oldest first, while `ready(pass)` holds.
    template <typename Ready> void report(Ready ready) {
        while (reported_ < passes_.size()) {
            PassState& p = passes_[reported_];
            if (!p.dropped && (!p.ended || !ready(p))) {
                return;
            }
            p.reported = true;
            ++reported_;
        }
    }
    // Hands each reported pass that was not dropped to `take`, then forgets them all.
    template <typename Take> void take(Take take) {
        for (std::size_t i = 0; i < reported_; ++i) {
            if (!passes_[i].dropped) {
                take(passes_[i]);
            }
        }
        passes_.erase(passes_.begin(), passes_.begin() + static_cast<std::ptrdiff_t>(reported_));
        base_ += static_cast<int>(reported_);
        reported_ = 0;
    }
    std::size_t size() const { return passes_.size(); }

  private:
    std::vector<PassState> passes_;
    int base_ = 0;
    std::size_t reported_ = 0;
};
} // namespace detail

// Song seconds: notes this close are one chord in Wait Mode, and notes on one key this close are
// one key press in both modes. 30 ms is below the shortest gap a player can strike one key twice
// in (about 60 ms) and above the spread of a chord written as one but quantised apart.
inline constexpr double kChordSeconds = 0.030;
inline constexpr double kUnisonSeconds = kChordSeconds;

} // namespace etude
