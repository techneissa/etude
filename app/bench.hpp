// SPDX-License-Identifier: GPL-3.0-or-later
// `etude --bench`: draws a synthetic free-play scene (rising notes, chords, held notes, sparks)
// with no MIDI and prints frame-time statistics per note style (each frame's CPU and GPU work, up
// to a one-pixel read-back, without the wait for the display) and the effects level the
// auto-degrade reached. Also renders exact screenshots (`--screenshot`) and measures the revealed
// bar (`--bar`) or the open settings (`--panel`); `--allocs` adds the heap allocations per frame.
// Frames over kFrameStallNs print their time by phase.
#pragma once

#include "etude/library_browse.hpp"
#include "etude/look.hpp"
#include "etude/renderer_path.hpp"
#include "ui.hpp"

#include <optional>
#include <string>
#include <vector>

namespace etude::app {

struct BenchOptions {
    std::vector<NoteStyle> styles; // run each in turn; empty means the look's own style
    // --song-bars: the scene's notes also fall as song blocks (left hand below middle C) over a
    // 4/4 grid at the scene's beat, so the beat lines have a song to follow.
    bool song_bars = false;
    int notes = 200;      // notes on screen at once, about
    double seconds = 5.0; // measured time per style, after a short warm-up
    int width = 1920;     // pixels drawn
    int height = 1080;
    bool exact_window = false;     // --window: draw straight into a window of exactly that size
    bool degrade = true;           // run the EffectsGovernor; off measures full effects only
    RendererPath renderer = RendererPath::Basic; // --renderer: which SDL renderer draws
    bool headless = false;         // --headless: a hidden window, drawn into a target, never presented
    // --bar SONG: load the song, keep the bar revealed (a mouse resting on the top row) and flip
    // play/pause and a hand toggle every few frames, so the transport's icons keep changing; the
    // UI's own CPU time is printed apart.
    std::string bar_song;
    BarStyle bar_style = BarStyle::Strip; // --bar-style: which bar --bar shows
    bool bar_pinned = false;              // --bar-pinned: the bar kept shown, notes below it
    // --view sheet|both (with --bar SONG): the song's sheet music over the note area or above the
    // notes, its cursor and colours updated every frame; measuring starts once its first pictures
    // are on screen.
    ViewMode view = ViewMode::Falling;
    bool count_allocs = false; // --allocs: count heap allocations per frame (slows each one)
    // --panel PAGE: open the settings sheet on that page and keep it open.
    std::optional<PanelTab> panel;
    // --library-view grid|list|shelves (with --panel library): the view the library is drawn in;
    // --library-group my_groups groups it by the made-up groups.
    BrowseView library_view = BrowseView::Grid;
    bool library_my_groups = false;
    // --combo: the scene also shows the combo number, climbing and breaking
    // over and over (combo_demo.hpp, "perf"), so the frame times include them.
    bool combo = false;
    // --combo-demo NAME (appear, pulse or break) with --combo-out DIR: developer, no timing.
    // Steps that combo timeline at 50 Hz of synthetic time and saves every recorded frame as
    // DIR/f0000.png, f0001.png ..., the frames of the looping GIFs. Use --bar SONG --bar-pinned
    // --song-bars --size WxH --headless for the full screen.
    std::string combo_demo;
    std::string combo_out;
    // Screenshot mode: step a fixed 60 Hz clock to `at_seconds` and save that frame as a PNG.
    std::string screenshot;
    double at_seconds = 3.0;
    // --shot-pattern run|chord: instead of the bench scene, a fast run of notes or one chord whose
    // last note-on is at 2 s; the screenshot is `shot_after_ms` after it. song-chromatic,
    // song-chord, song-run: song notes falling in both hands' colours, nothing pressed. "all": both
    // patterns for every hit effect in one window, saved as SCREENSHOT-EFFECT-PATTERN.png.
    std::string shot_pattern;
    int shot_after_ms = 100;
};

// Returns the process exit code.
int run_bench(const BenchOptions& options, const LookSettings& look);

} // namespace etude::app
