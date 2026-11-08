// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/sections.hpp"
#include "etude/song.hpp"

#include <cstddef>
#include <string>

namespace etude {

// The pure half of the section screens: where a touch on the song overview lands, where a dragged
// marker may go, and the words the screens show. No drawing, no clock, no files.

// The overview's track in screen units (points): time 0 at x0, the song's end at x1.
struct OverviewTrack {
    float x0 = 0.0f;
    float x1 = 0.0f;
    double duration_s = 0.0;

    // The song time under `x`, clamped to the song. 0 for a track with no width or no length.
    double time_at(float x) const;
    // The x of a song time, clamped to the track.
    float x_at(double seconds) const;
};

// What a touch at `x` grabs: a marker when one is within `marker_half_w` (the nearest wins), else
// the section under it, else nothing (a song with no sections, or a track with no length).
struct OverviewHit {
    enum class Kind : std::uint8_t { None, Marker, Segment };
    Kind kind = Kind::None;
    std::size_t marker = 0;      // Marker: index into SongSections::markers()
    SectionId section = kNoSection; // Segment
};

// `marker_half_w` is half the width of a marker's touch zone: 22 points makes it the 44-point
// target the touch rules ask for.
OverviewHit overview_hit(const SongSections& sections, const OverviewTrack& track, float x,
                         float marker_half_w);

// Where a dragged marker lands: `raw_s` held between the two sections' limits (each keeps at least
// kMinSectionSeconds) and, with `snap_to_bars`, on the nearest bar start that keeps both long
// enough; with no such bar the unsnapped time is used. An unknown marker returns `raw_s`
// unchanged. The result is what SongSections::move_marker accepts.
double marker_drag_time(const Song& song, const SongSections& sections, std::size_t marker,
                        double raw_s, bool snap_to_bars = true);
// The same for a boundary between a section starting at `left_start_s` and one ending at
// `right_end_s`.
double marker_drag_time(const Song& song, double left_start_s, double right_end_s, double raw_s,
                        bool snap_to_bars = true);

// ---- Words

// "m:ss" for a time, from 0:00 on.
std::string clock_text(double seconds);

// The name a section shows: a custom name as typed, else "Bars 9–16" or "Bar 9" (an en dash) when
// it sits on bars, else its default "Section N".
std::string section_label(const Section& section);

// What goes under the name in a list: its time span "0:12–0:28", led by "Bars 9–16 · " when the
// section has a custom name (the name no longer says where it is) and sits on bars.
std::string section_detail_text(const Section& section);

// The status row while a practice list runs: "Section 2/5 · Bars 9–16 · pass 1/3". `item_index` is
// 0-based, `passes_done` the passes counted so far on the item, `repeats` its repeat count (0 stays
// until skipped: "pass 4"). An inactive plan gives an empty string.
struct PlanStatus {
    bool active = false;
    std::size_t item_index = 0;
    std::size_t item_count = 0;
    int passes_done = 0;
    int repeats = 1;
};
std::string plan_status_text(const PlanStatus& status, const Section* section);

// One practice list item in a line: "×3 · clean only · 60–100% +5" ("×∞" repeats until skipped).
std::string plan_item_text(const PlanItem& item);

} // namespace etude
