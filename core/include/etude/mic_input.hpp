// SPDX-License-Identifier: GPL-3.0-or-later
// What the microphone's pitch events do in the trainer: which become key presses and releases (the
// same notes MIDI would send), which become a soft "not sure" marker, and how the two sources
// share a keyboard. Pure values: the caller supplies every time, so nothing here reads a clock.
#pragma once

#include "etude/keyboard.hpp"
#include "etude/pitch_detect.hpp"

#include <array>
#include <cstdint>

namespace etude {

// The loudness given to a note heard by the microphone: the detector measures pitch, not force.
inline constexpr int kMicVelocity = 80;

// How long after the keyboard sent a note the same note heard by the microphone is taken for it
// (Both mode), in ns. Covers the detector's confirmation delay with room to spare.
inline constexpr std::int64_t kMicMergeNs = 400'000'000;
// A note heard this long before the keyboard's stamp is the same note too: the onset is the
// sound's time, the keyboard's stamp is when its message arrived.
inline constexpr std::int64_t kMicMergeBeforeNs = 100'000'000;

// How long a note confirmed by an expected-note verdict stays down, in ns. That mode reports an
// onset and no note-off, so the key is released after this.
inline constexpr std::int64_t kMicExpectedHoldNs = 300'000'000;

// Extra time the evaluators wait for a microphone note beyond a keyboard's, in ms. A note is
// reported after its pitch is confirmed (10 to 100 ms after the onset it is stamped with); an
// evaluator that decided the note's window before then would count it missed.
inline constexpr int kMicSettleMs = 130;

// A "not sure" marker: fully drawn for this long after the event, in ns, then fading out over
// kUnsureFadeNs.
inline constexpr std::int64_t kUnsureHoldNs = 350'000'000;
inline constexpr std::int64_t kUnsureFadeNs = 900'000'000;

// How visible a "not sure" marker is `age_ns` after the event: 1 at first, 0 once faded.
float unsure_marker_alpha(std::int64_t age_ns);

// The key a "not sure" marker is drawn over: `guess` (the detector's likeliest note) when it has
// one, else the last note heard, else the middle of the keyboard; always inside `range`. It marks
// a register, never a key to press.
int unsure_marker_note(int guess, int last_heard, const KeyRange& range);

// The input level meter: 0 at -60 dBFS and below, 1 at -6 dBFS and above, linear in dB between.
float mic_meter_fraction(float rms);

// One thing the app does with a pitch event.
enum class MicOutKind : std::uint8_t {
    NoteOn,  // light the key and send it to the evaluators, exactly as a MIDI note-on
    NoteOff, // as a MIDI note-off
    Unsure,  // show the "?" marker. The evaluators hear nothing: no hit, no wrong note, no miss,
             // no change of the combo, and Wait Mode keeps waiting.
};

struct MicOut {
    MicOutKind kind = MicOutKind::NoteOn;
    int note = -1;            // NoteOn, NoteOff; the guessed register for Unsure (or -1)
    std::int64_t time_ns = 0; // NoteOn, Unsure: the onset; NoteOff: when it was decided
};

// A pitch event with the times the capture path put on it and the notes the detector was asked
// about when it decided (expected-note mode).
struct MicEventIn {
    PitchEvent event;
    std::int64_t onset_ns = 0;  // event.onset_sample on the steady clock
    std::int64_t sample_ns = 0; // event.sample on the steady clock
    std::array<std::int8_t, kMaxChordNotes> expected{};
    int expected_count = 0;
};

// Turns pitch events into key presses and releases.
//
// Rules:
// - NoteOn becomes NoteOn stamped at the onset; NoteOff becomes NoteOff.
// - NotSure becomes Unsure. It never presses or releases a key.
// - Expected-note mode, one note: the verdict Expected presses the expected note; a neighbour or
//   an octave verdict presses the note that was heard (a wrong key, shown as such), but only once
//   the tuning has settled; before that it is Unsure, since a wrong A4 could make a right note look
//   a semitone off. The tuning has settled when learning is off (the A4 is fixed) or
//   kTuningSettledNotes confident notes have been heard since learning was switched on. An unsure
//   verdict is Unsure.
// - Expected-note mode, a chord: every note the detector found present is pressed at the onset;
//   when a note is neither present nor absent it is also Unsure.
// - A note confirmed by an expected-note verdict is released kMicExpectedHoldNs later
//   (release_due), or at once if the same note is pressed again.
// - Both mode: a NoteOn for a note the keyboard pressed within kMicMergeNs before it (or
//   kMicMergeBeforeNs after it; note_midi_press) is dropped, and so is its NoteOff. That holds
//   even when the microphone still holds the note from an earlier strike: the keyboard owns the
//   note from its press on, and a release or re-strike from the microphone must not end the
//   keyboard's key or press it twice. (A new onset of a note the microphone holds, with no
//   keyboard press near it, is its own press: note-off and note-on both go through.) The
//   microphone's later release, and a timed release of an expected-note press, of a note the
//   keyboard has pressed since are dropped. A NoteOff for a note this source did not press is
//   dropped.
class MicNoteBridge {
  public:
    static constexpr int kMaxOut = 2 * kMaxChordNotes + 2;

    // Appends to `out` (capacity kMaxOut); returns the count.
    int apply(const MicEventIn& in, MicOut* out);

    // NoteOffs for expected-mode presses whose hold has ended by `now_ns`.
    int release_due(std::int64_t now_ns, MicOut* out, int capacity);

    // Whether the detector is learning the tuning (Settings > Input > Learn tuning). Switching it
    // on starts the count of confident notes again.
    void set_learning_tuning(bool on);
    // A new detector has learned nothing: forget the count of confident notes and set whether it
    // is learning. Call it on every successful open of the microphone, whatever the setting was
    // before (set_learning_tuning only starts over on an off-to-on change).
    void reset_tuning(bool learning);
    // The A4 in use is trustworthy: see the rules above.
    bool tuning_settled() const { return !learning_ || confident_notes_ >= kTuningSettledNotes; }

    // A note the MIDI keyboard played, for the Both-mode merge.
    void note_midi_press(int note, std::int64_t time_ns);

    // Forget everything held (source switched, device closed). Appends a NoteOff for each key
    // this source holds.
    int release_all(std::int64_t now_ns, MicOut* out, int capacity);

  private:
    int press(int note, std::int64_t time_ns, bool timed_release, MicOut* out, int n);
    // The keyboard pressed this note after the microphone did, so its release is the keyboard's.
    bool keyboard_owns(std::size_t i) const;

    bool learning_ = false;
    int confident_notes_ = 0; // since learning was switched on

    static constexpr std::int64_t kNever = INT64_MIN / 2;
    std::array<bool, 128> down_{};   // pressed by this source and not released
    std::array<bool, 128> merged_{}; // heard right after the keyboard: its note-off is dropped
    std::array<std::int64_t, 128> midi_press_ns_ = [] {
        std::array<std::int64_t, 128> a{};
        a.fill(kNever);
        return a;
    }();
    std::array<std::int64_t, 128> mic_press_ns_ = [] {
        std::array<std::int64_t, 128> a{};
        a.fill(kNever);
        return a;
    }();
    std::array<std::int64_t, 128> release_at_ns_ = [] {
        std::array<std::int64_t, 128> a{};
        a.fill(kNever);
        return a;
    }();
};

} // namespace etude
