// SPDX-License-Identifier: GPL-3.0-or-later
// A song ready to play: what the loaders (song_library.hpp, the sheet worker) hand around, kept
// apart from SongController so a file that only passes a song on does not rebuild with it.
#pragma once

#include "etude/difficulty.hpp"
#include "etude/hand_split.hpp"
#include "etude/musicxml.hpp"
#include "etude/song.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace etude::app {

// A song ready to play: hands assigned (names, else the smart split), with what identifies it.
struct LoadedSong {
    Song song;
    std::string title;
    std::uint64_t hash = 0;    // content_hash() of the file's bytes
    std::string library_path;  // relative to a library folder, '/' separated; empty when outside
    std::string absolute_path; // where it was opened from, absolute; empty when not known
    std::string display_path;  // as the player named it, for messages
    bool hands_split = false;  // the split assigned some hands
    // The hands of the song's notes and parts as its file gave them, when the split decides the
    // hand of some of its notes: what the player's choice of split point
    // (SongController::set_hand_split) splits again, on a copy of the song that gets these hands
    // back first. Null when the file names the hands, or the song was not made by the loader.
    // `song` has the split applied, so this is the only trace of the file's own hands, a few bytes
    // a note instead of a second copy of the notes.
    std::shared_ptr<const HandSplit> file_hands;
    // The sheet's staves decided the hands (hands_from_staves in core, read before the split);
    // false when `file_hands` is null.
    bool hands_from_staves = false;
    // What the file is and how it was read, so sheet music can read the same score again: the
    // format by content, and for ABC the tune's address and whether chord symbols became a left
    // hand.
    SongFileFormat format = SongFileFormat::Unknown;
    // song_fingerprint(song), taken once by the loader on its own thread so load() compares two
    // numbers instead of passing over the notes again. 0 when not taken (load() then takes it).
    std::uint64_t fingerprint = 0;
    std::string tune;
    bool abc_chords = false;
    // How hard each bar is and the song as a whole (difficulty.hpp), rated by the loader on its own
    // thread so the frame thread only reads it. Null for a song built without a loader.
    std::shared_ptr<const SongDifficulty> difficulty;
};

} // namespace etude::app
