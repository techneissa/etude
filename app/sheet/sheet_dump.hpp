// SPDX-License-Identifier: GPL-3.0-or-later
// `etude --sheet-dump FILE`: engraves a song's sheet music with no window and prints what it
// took (the source, the layout, the match with the song, each system's picture), writing the
// MusicXML and the pictures to a folder when one is given. For checking the engraver and its
// numbers, and with --names the note names' room at a window size.
#pragma once

#include <string>

namespace etude::app {

struct SheetDumpOptions {
    std::string song;
    std::string out_dir;
    float width_px = 1300.0f;
    float staff_space_px = 12.0f;
    bool abc_chords = false;
    bool verbose = false;
    int max_pictures = 4;
    // Planned as the app would for a window of this size (0: --width and --space as given), in
    // the Sheet view or the strip of Both.
    int window_w = 0;
    int window_h = 0;
    bool both = false;
    // Note names placed as the app places them ("letters" or "solfege"; empty: none), reporting
    // how the names were placed over the whole score (straight, staggered, sloped, smaller, chord
    // symbols, left out under a "…", dropped). With names the engraving takes their spacing unless
    // `spacing_linear` / `spacing_non_linear` override it (above 0). `chord_symbols`: a recognised
    // chord as one symbol.
    std::string names;
    bool chord_symbols = false;
    float spacing_linear = 0.0f;
    float spacing_non_linear = 0.0f;
};

int run_sheet_dump(const SheetDumpOptions& options);

} // namespace etude::app
