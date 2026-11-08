// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "etude/hand.hpp"

namespace etude {

// The one rule that ties a note's hand to its staff: the treble staff is the right hand and the
// bass staff the left. The sheet-music writer reads it to put each track on a staff, and the hand
// split reads it to give the notes of an unnamed track its hand, so the page and the falling notes
// never disagree.

enum class Staff : std::uint8_t {
    None,   // a part that is not practised (accompaniment): left out of the score
    Treble, // the right hand
    Bass,   // the left hand
};

inline constexpr Hand hand_of_staff(Staff staff) {
    return staff == Staff::Bass ? Hand::Left : staff == Staff::Treble ? Hand::Right : Hand::Unknown;
}

// One track (a part of a song: a track and channel, or a staff of a score) as the rule sees it.
struct StaffTrack {
    Hand hand = Hand::Unknown;  // what the file says: a track name or a score's staff
    bool accompaniment = false; // not practised
    long long notes = 0;        // practised notes in it
    long long pitch_sum = 0;    // their MIDI pitches added
};

// An unnamed track that sits below this mean pitch (below middle C) is the left hand, but only
// next to another track; one track alone is the smart split's to divide.
inline constexpr double kBassTrackMeanPitch = 60.0;
// With one staff the clef is the bass clef when the music's mean pitch is below this (A3, the
// middle of the bass staff).
inline constexpr double kSingleBassMeanPitch = 57.0;
// Unnamed tracks that are all on one side of middle C are still two hands when their mean pitches
// are this many semitones apart or more (a left line at 63 under a right line at 79): the lower
// are the left hand.
inline constexpr double kTrackSeparation = 7.0;
// A track holding less than this share of the practised notes is a stray (a few notes on a spare
// channel), not a hand.
inline constexpr double kStrayTrackShare = 0.05;

// How the rule is read. The default is the score writer's: the register rule above and nothing
// more, so a score file is engraved as it always was.
struct StaffOptions {
    // For the hands of a MIDI file: unnamed tracks well apart are ranked (`kTrackSeparation`), and
    // an unnamed stray track is not a hand: it is left out of the decision and its notes go to the
    // staff of the track nearest in pitch.
    bool for_hands = false;
};

struct StaffPlan {
    std::vector<Staff> staff; // per track, in the order given
    int staves = 1;           // 1 or 2 staves in the score
    bool single_bass = false; // one staff only: it carries the bass clef
    int used = 0;             // tracks that are practised and have notes (strays left out)
    // The tracks alone make two staves, each a real hand: the staves are the hands, and no split
    // by pitch is needed. False for one track and for tracks too close to tell apart: the hands
    // are then the smart split's.
    bool decides = false;
};

// Which staff each track is on:
// - an accompaniment track is left out (None);
// - a track the file gave a hand goes to that hand's staff, and an unnamed track, next to another
//   practised track, to the bass staff when its mean pitch is below middle C and else to the treble
//   staff (`StaffOptions::for_hands` adds the ranking and the stray rule above);
// - when every track lands on one side the score has one staff, with the bass clef when its mean
//   pitch is below `kSingleBassMeanPitch` and the treble clef otherwise.
StaffPlan plan_staves(std::span<const StaffTrack> tracks, const StaffOptions& options = {});

} // namespace etude
