// SPDX-License-Identifier: GPL-3.0-or-later
// What the section screens need from the sections of the loaded song: a read-only view (the
// bands, the practice list, the cuts the suggestions propose, the hand-path hint) and the actions
// that edit them, audition the music and run the practice list. The UI holds no sections of its
// own; app/sections_controller.hpp binds this to the library the settings file keeps and to the
// song controller, so the section band, its lane and the editor always show the real thing.
#pragma once

#include "etude/sections.hpp"
#include "etude/song.hpp"
#include "etude/trouble_run.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etude::app::ui {

// One section, with its words already made.
struct SectionRow {
    SectionId id = kNoSection;
    std::string label;  // "Bars 9–16", "Verse"
    std::string detail; // "0:12–0:28", led by the bars for a custom name
    double start_s = 0.0;
    double end_s = 0.0;
    bool custom_name = false;
};

// One item of the saved practice list.
struct PlanRow {
    SectionId section = kNoSection;
    std::string name;   // the section's label, "(gone)" if it no longer exists
    std::string detail; // "×3 · clean only · 60–100% +5"
    PlanItem item;
    bool exists = true;
};

// A cut the suggestions propose, drawn dashed on the roll.
struct CutRow {
    double time_s = 0.0;
    bool trouble = false;
    int measure = 0;
};

struct SectionsView {
    // Changes whenever anything below does, so a screen can keep what it built from it.
    std::uint32_t revision = 0;
    // The loaded song and its sections can be edited (a song with an identity).
    bool available = false;
    const Song* song = nullptr; // for the roll and the grid; valid while available
    std::uint32_t song_serial = 0; // changes with every song loaded
    double duration_s = 0.0;

    std::vector<SectionRow> sections;
    SongSections data; // the same sections as values, for hit tests and snapping
    std::vector<double> markers; // the boundaries between sections, in time order
    // The section being looped (the A-B loop equals its span), or kNoSection.
    SectionId looping = kNoSection;
    // The A-B loop in force is not a section's: it can be saved as one.
    bool loop_can_be_saved = false;

    std::vector<PlanRow> plan;
    bool plan_running = false;
    // A trouble-bar run is under way (a bar playing or waiting at its check-in): the band shows
    // its skip and end controls.
    bool run_active = false;
    // Skip has something to do: not at the check-in of the last bar.
    bool run_can_skip = false;
    int plan_item = -1;      // the running item, 0-based
    std::string plan_status; // the status row's words while it runs, else empty

    bool can_undo = false;
    std::size_t undo_depth = 0;

    // Suggestions shown as dashed cuts. `cuts` is empty when they are off or all are in place.
    bool suggestions_on = false;
    std::vector<CutRow> cuts;
    bool suggestions_from_history = false;

    // The right-then-left-then-both path for the looped section, else the whole song.
    std::string hand_scope; // "Whole song" or the section's label
    std::string hand_hint;  // path_hint(): "Right hand: not practised yet. Try it at 60%."
    std::string hand_apply; // what one tap sets: "Right hand · 60%", empty when nothing to apply
    bool hand_can_apply = false;
};

class SectionsPort {
  public:
    virtual ~SectionsPort() = default;
    virtual const SectionsView& view() const = 0;
    // Counts every edit and every undo (and a song loaded), never back; read at once, not with the
    // frame's view. A toast that offers Undo for an edit keeps the number the edit made and offers
    // it only while that is still the latest: anything since would make Undo take back something
    // else.
    virtual std::uint32_t edit_serial() const = 0;
    // One sentence about what the last action did or why it could not ("Split at Bar 12"), empty
    // if none; taken once.
    virtual std::string take_message() = 0;

    // ---- The section band

    // Loops the section, or ends its loop when it is the one looping.
    virtual void select_section(SectionId id, std::int64_t now_ns) = 0;
    // A marker at the playhead, on the nearest bar start.
    virtual void add_marker_at_playhead(std::int64_t now_ns) = 0;
    virtual void save_loop_as_section(std::int64_t now_ns) = 0;

    // ---- Edits (each one undo step)

    // A marker at `seconds` (already snapped by the caller when it wants that).
    virtual bool split_at(double seconds, std::int64_t now_ns) = 0;
    virtual bool move_marker(std::size_t marker, double seconds, std::int64_t now_ns) = 0;
    virtual bool rename(SectionId id, std::string_view name) = 0;
    virtual bool merge_with_next(SectionId id, std::int64_t now_ns) = 0;
    virtual bool erase_section(SectionId id, std::int64_t now_ns) = 0;
    virtual bool undo(std::int64_t now_ns) = 0;

    // ---- Suggestions

    virtual void set_suggestions(bool on) = 0;
    virtual bool accept_cut(std::size_t index, std::int64_t now_ns) = 0;
    virtual bool accept_all_cuts(bool trouble_only, std::int64_t now_ns) = 0;

    // ---- Listening (through the song's sound path, whatever the song sound setting is)

    virtual void audition(double start_s, double end_s, std::int64_t now_ns) = 0;
    // The same, but while the band's loop toggle is on each pass that ends starts again at
    // `start_s`; with it off the pass in progress finishes. stop_audition ends it at once.
    virtual void audition_repeating(double start_s, double end_s, std::int64_t now_ns) = 0;
    virtual void stop_audition(std::int64_t now_ns) = 0;
    virtual bool auditioning() const = 0;
    virtual double audition_position_s() const = 0;

    // ---- The practice list

    virtual void plan_add(SectionId id) = 0;
    virtual void plan_add_all() = 0;
    virtual void plan_remove(std::size_t index) = 0;
    virtual void plan_move(std::size_t from, std::size_t to) = 0;
    virtual void plan_set(std::size_t index, const PlanItem& item) = 0;
    virtual void plan_clear() = 0;
    virtual void plan_start(std::int64_t now_ns) = 0;
    virtual void plan_stop(std::int64_t now_ns) = 0;
    virtual void plan_skip(int direction, std::int64_t now_ns) = 0;

    // ---- The trouble-bar run (see TroubleRun): Next bar, Again, Skip, End
    virtual void run_command(RunCommand command, std::int64_t now_ns) = 0;

    // ---- Hand path

    virtual void apply_hand_path(std::int64_t now_ns) = 0;
};

} // namespace etude::app::ui
