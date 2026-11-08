// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the MusicXML reader: the limits and the element, unit and divisions helpers its
// part reader uses.
#pragma once

#include "score_raw.hpp"

#include "etude/hand.hpp"
#include "etude/song_builder.hpp"

#include "pugixml.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
#include <tuple>

namespace etude::musicxml_detail {

using namespace score_detail;

constexpr std::int64_t kMaxPpq = 30240; // 480 * 63: fits the 16-bit division of a MIDI file
constexpr long long kMaxDivisions = 1'000'000;
constexpr std::size_t kMaxBarRefs = 1'000'000; // bars of all parts together

inline bool is(const pugi::xml_node& n, const char* name) {
    return std::strcmp(n.name(), name) == 0;
}

// Velocity of a dynamics mark, 0 when the element is no mark we know.
int velocity_of_mark(const char* name);

// MIDI velocity for a percentage of the forte velocity (MusicXML's `dynamics` attribute: 90 is the
// default for a note, 100 a "forte" of the sound).
inline int velocity_of_percent(double percent) {
    return std::clamp(static_cast<int>(std::lround(percent * 0.9)), 1, 127);
}

// Quarter notes in a beat unit; 0 for a name we don't know.
double quarters_of_unit(const char* unit);

int step_semitone(char step);

// Reads a `<time>`: sets the bar's signature when it holds a usable one.
void read_time(const pugi::xml_node& time, RawMeasure& bar);

// The place on the line of fifths (chord_spelling.hpp) of a note named by a letter and a number of
// semitones above it (`<root-step>` and `<root-alter>`); false for a letter that is not A to G.
bool line_of_step(char step, long long alter, int& line);

// The root or the bass of a `<harmony>` (`<root>` or `<bass>`, with `<root-step>`/`<bass-step>` and
// the matching `-alter`).
bool read_harmony_note(const pugi::xml_node& node, const char* step_name, const char* alter_name,
                       int& line);

// The chord quality a `<kind>` names, with the `<degree>` changes that give 7sus4, 7b9 and 6/9;
// ChordQuality::None for a kind or a degree this app does not name.
ChordQuality quality_of_kind(const pugi::xml_node& harmony);

struct MeasureRef {
    pugi::xml_node content; // the element with notes in it: <measure> (partwise) or <part>
    pugi::xml_node bar;     // the <measure> element (number, implicit)
};

inline bool has_valid_divisions(const pugi::xml_node& attrs) {
    long long v = 0;
    return parse_int(attrs.child_value("divisions"), v) && v >= 1 && v <= kMaxDivisions;
}

// Units a bar's notes, backups and forwards reach, in the file's own (unknown) division of a
// quarter note. `sets_divisions` is true when a valid `<divisions>` shows up in the bar.
std::int64_t bar_units(const pugi::xml_node& content, bool& sets_divisions);

// A part that writes durations before any `<divisions>` has no unit to read them in. Its bars
// before the first `<divisions>` tell it: a full 4/4 bar of eight units is eight eighth notes, so
// two units to the quarter note. Each ordinary bar is read against its own signature (3/4 and then
// 4/4 are both whole bars); the value that makes the most bars complete wins, and a tie goes to the
// larger one (a short bar is more likely a pickup than a long one an overfull bar). Bars marked
// implicit or numbered 0 are pickups and don't count. Returns 0 when the part sets `<divisions>`
// before it needs it; else the divisions found, or 1 when no bar says. `name` goes in the warnings.
long long missing_divisions(const std::vector<MeasureRef>& bars, const std::string& name,
                            std::vector<std::string>& warnings);

} // namespace etude::musicxml_detail
