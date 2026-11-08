// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/chord.hpp"
#include "etude/chord_spelling.hpp"
#include "etude/song.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace etude {

// The chords of a song, named the way a musician would: one name for a stretch of music, from the
// harmony its notes make, never from the tune. Pure, built once per song on a worker (cost: see
// build_harmony), never per frame.
//
// The method, in short:
//  1. The song is cut into stretches: a bar, or half a bar where the two halves clearly name
//     different chords. A beat is a quarter note (a dotted quarter in 6/8, 9/8, 12/8, 3/8).
//  2. Each stretch gathers the pitch classes of its notes, from both hands, broken chords
//     included. A note weighs its sounding time in the stretch times the strength of the beat it
//     starts on, so a short note off the beat counts little. A pitch class that sounds only once
//     and briefly is a passing note and is left out.
//  3. Every chord shape (chord.hpp) is fitted to the weights from every root: the weight a shape
//     covers, less the weight it leaves out, less a cost for each tone it claims beyond a triad and
//     for each tone of it that is missing, plus a bonus for the root in the bass. A seventh, sixth
//     or ninth counts only when the stretch backs it (it starts again, is held a beat and a half,
//     or carries a share of the weight), a root likewise, and the third (or a suspended chord's
//     second or fourth) must sound. The bass (the lowest note of real weight) must be a tone of the
//     chord to be named after the slash.
//  4. The name changes only when the new chord is clearly better than the one before: a margin
//     over how well the previous chord fits the new stretch, a larger one for half a bar. A
//     stretch with too little in it to say (a rest, a lone tune note) leaves the held chord alone.
//  5. Root and bass are spelled from the key at that point and the chord (chord_spelling.hpp).
// Where the file carries chord symbols (MusicXML `<harmony>`), they are the chords there; see
// analyse_harmony for how far a symbol reaches.

// Which notes a timeline is made from. A hand's timeline names that hand's own notes only: the
// file's chord symbols are not used for it, and a stretch is named only when the hand's notes hold
// the chord's root, third and fifth (and its seventh, for a seventh chord), each of them one that
// starts again or is held, so a passing note is not one. A bare fifth, two notes or a tune make no
// name: a line that moves mostly by step is a tune whatever notes it touches, a line that moves by
// the intervals of a chord (a broken chord, an Alberti bass) is harmony, and a hand that plays
// chords under a top line of its own (a note an octave or more over the voice below it) is named
// without that line.
enum class HarmonyScope : std::uint8_t {
    All,   // every pitched note, both hands and the song's own accompaniment parts
    Left,  // the left hand's notes only
    Right, // the right hand's notes only
};

// The tuning of the analysis. Named so the tests can refer to them.
//
// Fit: the cost of a unit of weight no chord tone covers; the bonus for the root in the bass; the
// penalty for a bass that is not a tone of the chord; the share of the weight a named chord must
// cover.
inline constexpr double kHarmonyOutsideWeight = 1.5;
inline constexpr double kHarmonyBassRootBonus = 0.10;
inline constexpr double kHarmonyBassOutsidePenalty = 0.20;
inline constexpr double kHarmonyMinCoverage = 0.70;
// Hysteresis: a whole bar changes the name over this lead in fit; half a bar needs the larger one.
inline constexpr double kHarmonyBarMargin = 0.06;
inline constexpr double kHarmonyHalfMargin = 0.25;
// Half bars: each half must be covered this well by a chord of its own, and the two chords must
// beat one chord for the bar by this much.
inline constexpr double kHarmonySplitCoverage = 0.85;
inline constexpr double kHarmonySplitGain = 0.08;
// Weights: a note struck before the stretch began counts at this strength; a bass note weighs at
// least this share of the heaviest note; two notes sound together when they share this many beats.
inline constexpr double kHarmonyHeldOverStrength = 0.6;
inline constexpr double kHarmonyBassShare = 0.40;
inline constexpr double kHarmonyMinSimultaneous = 0.25;
// A seventh, sixth, ninth or root that starts once counts when held this many beats, or when an
// extension carries this share of the weight, or a root this share.
inline constexpr double kHarmonyBackedBeats = 1.5;
inline constexpr double kHarmonyExtensionShare = 0.15;
inline constexpr double kHarmonyRootShare = 0.20;
// A stretch in which notes sound for less than this share of the time has too little in it to say
// "no chord" (see HarmonyState::Unclear).
inline constexpr double kHarmonyMinSounding = 0.5;
// Chord symbols: a symbol reaches to the next symbol, but no further than the end of the bar in
// which the next symbol would be expected, that is the longest gap between two symbols in the
// file that are at most this many longest bars apart.
inline constexpr int kHarmonySymbolGapBars = 4;

// How strongly a note starting `position` beats from the bar's start sounds, 0..1: the downbeat
// 1.0, the middle of an even bar 0.8, other beats 0.65, the half beat 0.45, anything finer 0.3.
// `beats_per_bar` is the bar's count of beats (a dotted beat in 6/8).
double harmony_beat_strength(double position, int beats_per_bar);

// How much a note weighs: its strength times a factor for how long it sounds in the stretch
// (`sounding_beats`): 0.35 for an instant up to 1 from a beat and a half.
double harmony_note_weight(double sounding_beats, double strength);

// What a stretch says. `Chord`: chord.found is true. `NoChord`: the stretch positively makes no
// chord (a tune on its own, a bar of one note in octaves, the file's "N.C."): hide the label.
// `Unclear`: there is too little to say either way (a rest, one tune note in half a bar, a hand
// resting): the label of the stretch before stays. An unclear stretch between two stretches of
// one chord is made part of that chord, so the label does not blink out across a rest.
enum class HarmonyState : std::uint8_t { Unclear, NoChord, Chord };

// One named stretch of a song.
struct HarmonyStretch {
    std::int64_t start_tick = 0;
    std::int64_t end_tick = 0; // exclusive
    double start_s = 0.0;
    double end_s = 0.0;
    HarmonyState state = HarmonyState::Unclear;
    Chord chord; // chord.found is false unless state is Chord
    ChordSpelling spelling;
    float confidence = 0.0f; // 0..1: how much of the weight the chord covers and how clear it won;
                             // 1 for a chord the file writes
    // The lowest and highest note (MIDI, as in the file) of the stretch that is a tone of the
    // chord, where the label belongs on the keyboard; -1 when there is no chord or no such note.
    int low_pitch = -1;
    int high_pitch = -1;
    bool from_file = false; // the chord is a symbol the file writes
    // How far a rest or a lone short note after this chord still keeps its name on a label: the end
    // of the chord's bar, or one beat past the stretch, whichever is later; 0 when unset (the label
    // then keeps it no further than the stretch's end). A hand's name is shown only while its notes
    // form the chord, so a longer silence hides it.
    std::int64_t hold_end_tick = 0;
    double hold_end_s = 0.0;

    friend bool operator==(const HarmonyStretch&, const HarmonyStretch&) = default;
};

// The stretches of `scope`, in time order, each ending where the next begins; a stretch with no
// chord is kept (see HarmonyState) so a gap is a gap.
//
// Chord symbols: where the song has them they are the chords of the All timeline. A symbol holds
// until the next symbol, but no further than the end of the bar where the next one would be
// expected (the longest gap between two symbols in the file that are at most
// `kHarmonySymbolGapBars` bars apart, rounded up to the end of a bar, at least the bar the symbol
// is in); past that the chords are detected from the notes, as in a song with none, until the next
// symbol. A symbol that names no chord ("N.C.") ends the chord before it. A hand's timeline never
// uses the symbols: it is detected from the hand's notes and holds a chord only where those notes
// make a full one (see HarmonyScope).
//
// Without symbols the chords are detected as in the method above; a hand's stretch that is only a
// tune, with no tones that sound together or come back, has no chord.
std::vector<HarmonyStretch> analyse_harmony(const Song& song, HarmonyScope scope);

// All three timelines, for the Combined and the Per hand settings. The cost is linear in the notes
// (a stretch's overlap of notes is swept once, its tune searched only up to 600 notes): measured on
// a made-up song of 120,000 notes, 0.43 s in a Debug build (3.6 ms per thousand notes) and 0.064 s
// in a Release build (0.5 ms per thousand) for the three timelines; 120,000 notes crowded into 20
// bars took 7 ms in Release. Run it once per song on a worker.
struct HarmonyTimeline {
    std::vector<HarmonyStretch> all;
    std::vector<HarmonyStretch> left;
    std::vector<HarmonyStretch> right;

    const std::vector<HarmonyStretch>& of(HarmonyScope scope) const {
        return scope == HarmonyScope::Left ? left : scope == HarmonyScope::Right ? right : all;
    }
};
HarmonyTimeline build_harmony(const Song& song);
// The two hands' timelines only (`all` stays empty): what the chord label of the scene is made
// from, in about two thirds of build_harmony's time.
HarmonyTimeline build_hand_harmony(const Song& song);

// The stretch holding a time (seconds) or a tick; null before the first stretch and from the end of
// the last. The stretch may have no chord.
const HarmonyStretch* harmony_at_seconds(const std::vector<HarmonyStretch>& stretches,
                                         double seconds);
const HarmonyStretch* harmony_at_tick(const std::vector<HarmonyStretch>& stretches,
                                      std::int64_t tick);

// "F#maj7/E#": the stretch's chord with its spelling; empty without a chord.
std::string harmony_name(const HarmonyStretch& stretch);

} // namespace etude
