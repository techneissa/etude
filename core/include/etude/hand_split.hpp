// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "etude/pitch.hpp"
#include "etude/song.hpp"

namespace etude {

// Hand split for songs whose notes have no hand. The order is:
// track and channel names first (assign_hands_from_names, done by the MIDI loader), then this split
// by pitch for the notes still Unknown. Nothing here changes a Song in place.
//
// What the split promises, for both methods:
// - A note that already has a hand keeps it. Drum notes stay Unknown, and so does nothing else.
// - Notes that start within kChordWindowSeconds of the first note of a group form a chord.
//
// Fixed: the split point decides. A chord goes to one hand as a whole, by where its middle note
// sits against the split point (from it up: the right hand), unless it is wider than
// kHandReachSemitones (a major tenth, 16 semitones). Then it is cut once, at the gap between two
// neighbouring notes whose midpoint is closest to the split point and that leaves both parts
// within reach (if no cut does, the closest one anyway).
//
// Smart weighs the whole song at once, measured in semitones and beats so it does not depend on
// the tempo. Each chord gets one cut (the notes below it are the left hand, the ones from it up the
// right, or no cut: one hand), and the way through the chords that costs least or nearly least
// wins (dynamic programming). It is a bounded search, not an exact one, to keep the split linear
// in the notes: at each chord a way more than a fixed margin dearer than the cheapest is dropped
// (a hand's recent notes can still win back up to 6 per note, so that margin can drop a way that
// would have ended cheapest), and a chord of more than twelve notes is tried only at its eight
// widest gaps.
// What a way costs:
// 1. Registers. For each chord the notes from kSplitWindowBeatsBefore beats before it to
//    kSplitWindowBeatsAfter beats after it are fitted with two registers (k-means on their
//    pitches, started at the previous chord's registers, or at the split point +/-6 semitones for
//    the first chord). Starting from the previous registers keeps a passage in the grouping it
//    already had. The fit counts only when it looks like two hands: each register holds at least
//    15% of the window's notes (and two notes) and the register averages are at least 8 semitones
//    apart. A note in the hand on the far side costs how much nearer it is to the other register
//    (with a one semitone pull toward the side of the split point it lies on). Without two
//    registers, for a single line or a narrow passage, the split point decides: a quarter per
//    semitone within 4 semitones of it (so a melody can dip a little under it and come back), 3
//    per semitone beyond that, for 6 more semitones.
// 2. Reach. A hand's part of a chord costs 3 per semitone of span beyond an octave, 30 more beyond
//    kHandReachSemitones, and 1 per note beyond five. So a chord wider than a hand is cut, at the
//    cheapest place.
// 3. Cuts. A cut between two notes less than a tritone apart costs 1 per semitone short of it: a
//    chord falls apart at its widest gaps, not where its middle happens to lie.
// 4. Continuity. A hand's last six notes count in full for a beat and fade out over the next
//    kFollowBeats - 1. A note costs its distance from the closest of its hand's recent notes (up
//    to 3, which is also the cost of a hand that has not been near: a fresh start), and a note the
//    hand just played again gains 6. So a figure that repeats stays in its hand, and a right hand
//    melody that dips under the dividing pitch stays right.
// Notes that already have a hand (named tracks, a single line) are the context of the others: they
// sit in the registers and in the hands' recent notes, and no cut may put them in the other hand.
//
// Limits, measured on the real files in tests/data: pitch alone
// cannot tell hands apart when they interleave, and the reference hands of a piece are the staves
// of its score, which a hand cannot always follow. Satie's first Gymnopedie has chords in the
// treble staff under a melody (59 62 66 together with 78), wider than one hand reaches, and
// chords in the bass staff that rise to the melody's register.
//
// The notes are expected sorted by start tick, as Song::notes always is.
//
// Three rules come before any of the above, part by part (a part is one track and channel):
// - Staves. When two or more practised parts are on different staves (staff_plan.hpp: a part the
//   file gave a hand goes to that hand's staff; an unnamed one next to another to the bass staff
//   below a mean pitch of middle C, else to the treble staff; unnamed parts all on one side but
//   kTrackSeparation apart are ranked, the lower the left hand), the unnamed parts take the hand
//   of their staff, whole: the treble staff is the right hand and the bass staff the left, and
//   the sheet-music writer reads the same rule, so the page and the falling notes agree. An
//   unnamed part holding under kStrayTrackShare of the notes is left out of that decision and its
//   notes follow the staff of the part nearest in pitch. It does not apply to one part (the smart
//   split divides it) or to parts too close in pitch to tell apart.
// - Accompaniment. When the song has a keyboard part, a part that is another instrument (voice,
//   strings; see instrument.hpp) is not a practice hand: its notes get no hand, are flagged
//   Note::accompaniment, and only sound with "song plays along". A song with no keyboard part
//   splits all its parts.
// - A single line. A part is one melodic line when at most kSingleLineChordFraction of its onsets
//   are chords (notes starting within kChordWindowSeconds of each other) and it does not look like
//   two hands woven together (no more than kInterleavedLeapFraction of the steps between onsets
//   leap further than an octave, and no more than kHeldAwayFraction of the onsets sound over a
//   note held since two onsets earlier that lies more than an octave away: a bass under an
//   arpeggio is another hand). A note held into the next, with onsets further apart than the
//   chord window, is legato or pedal and never counts. A single line goes to one hand whole,
//   Right when its median pitch is kSingleLineRightMedian (G3) or higher and Left below, and is
//   never cut by pitch, so a melody across middle C is not shared between the hands. The player
//   can swap its hand later. A part with two hands in it has far more chords or leaps, and is
//   split as described. HandSplitOptions::single_lines_whole = false skips this rule, to measure
//   the pitch rules on their own.

inline constexpr int kHandReachSemitones = 16; // a major tenth
inline constexpr double kChordWindowSeconds = 0.040;
inline constexpr double kSplitWindowBeatsBefore = 2.0;
inline constexpr double kSplitWindowBeatsAfter = 3.0;
inline constexpr double kFollowBeats = 4.0;
inline constexpr double kSingleLineChordFraction = 0.10;
inline constexpr double kInterleavedLeapFraction = 0.25;
inline constexpr double kHeldAwayFraction = 0.25;
inline constexpr int kSingleLineRightMedian = 55; // G3

enum class SplitMethod : std::uint8_t { Fixed, Smart };

struct HandSplitOptions {
    int split_point = kMiddleC; // 1..127; notes from here up are the right hand
    SplitMethod method = SplitMethod::Smart;
    bool single_lines_whole = true; // see "A single line" above
    // Unnamed tracks next to each other take the hand of their staff (staff_plan.hpp: the lower
    // tracks the left hand, the others the right), when that makes two staves. False measures the
    // pitch rules on their own.
    bool staves_decide = true;
    // A split the player placed: every note without a hand is the right hand from `split_point`
    // up and the left hand below it, by its pitch alone. Chords, single lines and the reach of a
    // hand are not looked at, and `method` is not used. Notes that have a hand keep it.
    bool by_pitch = false;
};

// A player's per-song choice of split, as one number (SongPractice::split_point): kAutoSplit is
// the smart split with its default point; 1..127 is a split the player placed at that pitch, in the
// song's own pitches (before transposition): notes from it up the right hand, the rest the left, by
// pitch alone (HandSplitOptions::by_pitch).
inline constexpr int kAutoSplit = 0;
inline constexpr int kMaxSplitPoint = 127;
// The choice forced into 0 or 1..127.
int sanitized_split_point(int split_point);
// The options a choice stands for. A split the player placed is obeyed strictly: by pitch alone,
// chords and single lines included (a melody is cut at the point like every other note), so the
// notes below the point are the left hand and the rest the right.
HandSplitOptions options_for_split_point(int split_point);

// True while some playable (non-drum) note has no hand, or an accompaniment part's notes are not
// yet flagged: the song needs the split, or names.
bool needs_hand_split(const Song& song);

// True when the split decides the hand of some note: a playable note outside the accompaniment
// parts has no hand yet. A song whose file names the hands (a track or channel each, or a
// MusicXML staff each) has none, and a split point changes nothing in it.
bool hand_split_decides(const Song& song);

// True when the staves decide the hands of a song that has notes without a hand: two or more
// practised tracks, the lower unnamed ones the left hand and the others the right (staff_plan.hpp),
// so no split by pitch is made (HandSplitOptions::staves_decide). False when the smart split does
// it (one track, tracks too close in pitch to tell apart) and for a song whose file names every
// hand. Reads the song as the file gave it, before any split.
bool hands_from_staves(const Song& song);

// The pitch a straight split would best match the hands a song has (the split point that puts the
// fewest notes in the other hand than theirs; of equally good points the middle of the run nearest
// middle C, or for a run open at an end of the keyboard its point nearest middle C): where the
// Hands row (Settings > Practice) says the split is while it is "from the sheet", and where it
// starts when the player chooses a point. kMiddleC for a song with notes in one hand only. Reads
// `Note::hand` of a song the split was applied to.
int suggested_split_point(const Song& song);

// One hand per note of `song.notes`, same order. Left or Right for every note without a hand and
// not a drum; a note that had a hand keeps it; drum notes are Unknown.
std::vector<Hand> split_hands(const Song& song, const HandSplitOptions& options = {});

// A copy of the song with split_hands() applied to every note, and each part's hand set when all
// its playable notes ended up in one hand.
Song with_split_hands(const Song& song, const HandSplitOptions& options = {});

// What the split decided, kept apart from the song so that it can be put on another parse of the
// same notes (the library scan splits a file and opening it splits it again; the app keeps the
// first answer). Tied to the song it was made from by song_fingerprint.
struct HandSplit {
    std::uint64_t fingerprint = 0; // of the song before the split
    std::vector<Hand> note_hands;
    std::vector<bool> note_accompaniment;
    std::vector<Hand> part_hands;
    std::vector<bool> part_accompaniment;
};

// The hands `song` carries now, in the shape of a split: its notes' hands and accompaniment flags,
// its parts' hands and flags, and the song's fingerprint. Taken from a song as its file gave it
// (before any split), it is all that is needed to get that song back from a copy of the same
// notes after a split changed them: apply_hand_split(copy, captured, true) puts the hands back, so
// a song does not have to be kept whole to be split again. Comparing song_fingerprint(copy) with
// `fingerprint` catches a copy whose notes' starts, lengths, pitches, velocities, channels, hands,
// flags and bars, or whose bars and channel events, differ from the song; it does not look at the
// fields that song_fingerprint leaves out (track, finger, ticks, Part), which are only ever copied.
HandSplit capture_hands(const Song& song);

// Does the work of with_split_hands() and returns what it decided.
HandSplit compute_hand_split(const Song& song, const HandSplitOptions& options = {});
// Puts a split on `song`, with the effect with_split_hands() has. False, and the song untouched,
// when the split was not made from these notes and parts (a different song, or an edited one).
// `verified` says the caller just made the split from this very song, so the check of its
// fingerprint, one pass over the notes, is skipped (the sizes are still looked at).
bool apply_hand_split(Song& song, const HandSplit& split, bool verified = false);
// How many splits compute_hand_split() has run since the start of the process (tests, and the
// frame log's count of what a library scan did).
std::uint64_t hand_split_runs();

// How well `assigned` (one hand per note of `truth`) agrees with the hands of `truth`, over the
// notes whose hand is Left or Right in `truth`. Used by the tests against files with named tracks.
struct HandAgreement {
    int compared = 0;
    int matching = 0;
    // 1 when nothing was compared.
    double fraction() const {
        return compared <= 0 ? 1.0 : static_cast<double>(matching) / static_cast<double>(compared);
    }
};
HandAgreement compare_hands(const Song& truth, std::span<const Hand> assigned);

} // namespace etude
