// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etude {

// The order a score's bars are played in once repeat barlines, endings (voltas), D.C., D.S., segno,
// coda, To Coda and Fine are followed. Pure logic on one record per written bar: the readers fill
// the records, plan_repeats() returns the playing order, and the score assembler lays the bars out
// in that order once, at load.

// Played bars never exceed this many times the written bars, and never kMaxSongMeasures.
inline constexpr std::size_t kMaxRepeatExpansion = 16;
// A repeat sign's `times` is clamped to 1..this.
inline constexpr int kMaxRepeatTimes = 16;
// Ending numbers 1..kMaxEndingNumber are tracked; a higher number is ignored.
inline constexpr int kMaxEndingNumber = 63;

// What one written bar says about repeats. All flags default to "nothing".
struct BarMarks {
    bool forward_repeat = false;  // a forward repeat barline at the start of the bar
    bool backward_repeat = false; // a backward repeat barline at the end of the bar
    int times = 0;                // plays of the passage a backward repeat closes; 0: not given (2)

    // Endings. `endings` is a bit set (bit n is ending number n) for an ending bracket that starts
    // at this bar; `ending_stops` closes the open bracket after this bar. A bracket that is never
    // closed ends at its bar with a backward repeat, at the next bracket's start, or at the last
    // bar.
    std::uint64_t endings = 0;
    bool ending_stops = false;

    // Jumps and the places they go. A label is "" when the file names none.
    bool dacapo = false;   // after this bar, go back to the first bar (once)
    bool dalsegno = false; // after this bar, go to the segno named `dalsegno_label` (once)
    std::string dalsegno_label;
    bool segno = false; // this bar holds a segno
    std::string segno_label;
    bool coda = false; // this bar holds a coda
    std::string coda_label;
    bool tocoda = false; // after a D.C. or D.S., go to the coda named `tocoda_label` from here
    std::string tocoda_label;
    bool fine = false; // after a D.C. or D.S., the piece ends after this bar

    bool any() const {
        return forward_repeat || backward_repeat || endings != 0 || ending_stops || dacapo ||
               dalsegno || segno || coda || tocoda || fine;
    }
};

// Bit n set for ending number n in text like "1", "1, 2", "1,2,3" or "1-3"; 0 for no number.
std::uint64_t parse_ending_numbers(const char* text);

struct RepeatPlan {
    std::vector<std::size_t> order; // written bar (0-based) of each played bar, in playing order
    int repeats_followed = 0;       // repeat barlines and jumps the order follows
    int repeats_implied = 0;        // endings whose own backward repeat was missing and was implied
    int fallbacks = 0;              // signs that were unmatched or unusable and were left out
    bool capped = false;            // the order would pass the cap, so it is the written order
};

// The playing order for bars with these marks.
//
// - Repeats nest like brackets: a backward repeat goes back to the nearest forward repeat still
//   open before it, and an inner passage plays in full on each pass of the outer one. With no
//   forward repeat open it goes back to the start of its ending run (a second ending's repeat),
//   else to the bar after the previous backward repeat, else to the first bar. It plays the
//   passage `times` times in all (2 when not given).
// - An ending bracket plays only on the passes its numbers name. A repeat whose passes outnumber
//   its `times` still goes back while a higher-numbered ending is waiting. An ending that is not
//   the last of its run and has no backward repeat at its end gets one implied (counted in
//   `repeats_implied`), when the run holds a written backward repeat or a forward repeat is open
//   before it. A bracket run with neither is not a volta and plays straight through.
// - D.C. and D.S. are taken once, at the first bar that has them. From then on repeat barlines
//   are not taken again, and a bracket run plays its highest-numbered ending. A D.S. to an
//   unknown segno goes to the first segno, and is dropped when the score has none.
// - After a jump, Fine ends the piece after its bar and To Coda goes to the coda, once.
//   Without a jump both are ignored.
// - Past kMaxRepeatExpansion times the written bars (or `max_bars`) the plan gives up and returns
//   the written order with `capped` set.
RepeatPlan plan_repeats(const std::vector<BarMarks>& bars,
                        std::size_t max_bars = static_cast<std::size_t>(-1));

} // namespace etude
