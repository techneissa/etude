// SPDX-License-Identifier: GPL-3.0-or-later
// What the sheet worker needs from the app: the score to engrave for a loaded song, and where the
// engraver's fonts are.
#pragma once

#include "loaded_song.hpp"

#include "etude/sheet_source.hpp"

#include <string>

namespace etude::app {

// The MusicXML to engrave for `song`: a MIDI song from its notes, a score file read again from
// where it was opened (it must still hash as it did when it was loaded). Reads a file: never on
// the frame thread.
SheetSource make_sheet_source(const LoadedSong& song);

// The folder of Verovio's fonts: beside the executable, else in ../share/etude.
std::string sheet_resource_dir();

} // namespace etude::app
