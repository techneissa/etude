// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/section_suggestions.hpp"
#include "etude/sections.hpp"
#include "etude/song.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace etude {

// The section editor's rules that need no screen: undo, the cuts the suggestions propose, and the
// stretches of music it auditions.

// ---- Undo

// The last kMaxSectionUndo states of one song's sections, newest last. push() records the state
// before an edit; undo() brings the latest one back. The older states drop off the far end.
inline constexpr std::size_t kMaxSectionUndo = 20;

class SectionHistory {
  public:
    // Records `before`, the sections as they were just before an edit. Pushing a state equal to
    // the latest is ignored (an edit that changed nothing).
    void push(const SongSections& before);
    bool can_undo() const { return !states_.empty(); }
    std::size_t depth() const { return states_.size(); }

    // Returns the latest recorded state and forgets it, with `next_id` kept at the larger of the
    // two so an id made since is never given to another section. Empty when nothing is recorded.
    std::optional<SongSections> undo(const SongSections& current);
    void clear() { states_.clear(); }

  private:
    std::deque<SongSections> states_;
};

// ---- Proposed cuts

// A cut the suggestions would make: a new boundary at `time_s`.
struct ProposedCut {
    double time_s = 0.0;
    SuggestionKind kind = SuggestionKind::Phrase; // Trouble when the cut is the edge of a spot
    int measure = 0;                              // the bar the cut starts, 0 if unknown
    friend bool operator==(const ProposedCut&, const ProposedCut&) = default;
};

// The boundaries of `suggestions` that `sections` does not have yet, in time order. A cut is a
// Trouble cut when it is an edge of a Trouble section. With `trouble_only` only those are listed.
// Accepting them all (SongSections::add_marker for each, unsnapped) gives the same boundaries as
// apply_suggestions.
std::vector<ProposedCut> proposed_cuts(const SectionSuggestions& suggestions,
                                       const SongSections& sections, bool trouble_only = false);

// ---- Auditioning

// A stretch of the song to play once.
struct AuditionRange {
    double start_s = 0.0;
    double end_s = 0.0;
    bool valid() const { return end_s > start_s; }
};
inline constexpr double kAuditionSeconds = 2.0;

// kAuditionSeconds starting at `cursor_s` (releasing the cursor or a handle).
AuditionRange audition_from(const Song& song, double cursor_s);
// From kAuditionSeconds before the cursor to the same after it: does the cut fall at a breath?
AuditionRange audition_across(const Song& song, double cursor_s);
// What a tap in the editor does with the music: a tap while it plays stops it (and so does the
// double tap a quick second tap becomes); with nothing playing a tap plays, and a double tap
// leaves the music alone (it zooms).
enum class TapPlay { Play, Stop, Leave };
TapPlay tap_play(bool auditioning, bool double_tap);
// A whole section once.
AuditionRange audition_section(const Section& section);
// A tap on the ruler or the roll at `point_s`: from there to the end of the section holding the
// point (the next cut, or the song's end), so a point exactly on a cut plays the section that
// starts there, and so does a point within a hair below it. A song with no cuts is one section. A point too close to the end to leave
// anything to play (the song's last instant) falls back to audition_from. Invalid for a song
// with no length or a point that is not a number.
AuditionRange audition_tap(const Song& song, const SongSections& sections, double point_s);
// How long Play with no section selected plays from the cursor.
inline constexpr double kIdlePlaySeconds = 10.0;
// Less than this left from the cursor to the song's end is nothing to play: a repeat of a few
// milliseconds would restart every frame.
inline constexpr double kIdleMinSeconds = 0.5;
// Play with no section selected: kIdlePlaySeconds from `cursor_s`, cut at the song's end. Invalid
// for a song with no length, a cursor that is not a number or one with less than kIdleMinSeconds
// left before the song's end.
AuditionRange audition_idle(const Song& song, double cursor_s);
// What a repeating audition (the one audition_idle gives) does next: Continue while a pass plays
// (turning the loop off mid-pass lets it finish), Again when a pass has ended with the loop on,
// Done when it has ended with the loop off or was stopped (`repeating` is false once Play, a tap
// or closing the editor stopped it).
enum class RepeatStep { Continue, Again, Done };
RepeatStep audition_repeat(bool repeating, bool pass_ended, bool loop_on);

} // namespace etude
