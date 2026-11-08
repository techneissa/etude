// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen's bundled melodies as the app holds them: the manifest (core melody_manifest.hpp)
// with each file read once, for what a tile shows (the difficulty in half stars, the key count of
// each hand, the file's hash that the practice history knows it by) and the best score its history
// holds. The files are compiled into the app (cmake/EmbedMelodies.cmake); a test hands in the same
// ones. Built when the Learn screen first needs it (a few small files), never per frame; a file that
// does not load is left out of its path rather than stopping the screen.
#pragma once

#include "song_controller.hpp"

#include "etude/library.hpp"
#include "etude/melody_manifest.hpp"
#include "etude/melody_shape.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace etude::app {

// Generated from app/assets/melodies by cmake/EmbedMelodies.cmake: the manifest's text, and a
// bundled file by its name (empty when the manifest does not list it).
std::span<const unsigned char> embedded_melody_manifest();
std::span<const unsigned char> embedded_melody_file(std::string_view name);

// The bytes of a bundled file by name; empty for none.
using MelodyFileFn = std::function<std::span<const std::uint8_t>(std::string_view name)>;
// The app's own: the files compiled in.
MelodyFileFn embedded_melody_files();

struct MelodyPiece {
    std::string file;
    std::string title;
    std::string composer;
    std::string tag; // which version of a piece listed twice this is ("Easy", "Original"); empty for none
    std::string goal; // the path's one line for it
    int step = 0;
    std::uint64_t hash = 0;  // content_hash of the file: the history's name for the piece
    int half_stars = 0;      // the difficulty rating, 2 to 10
    MelodyShape shape;       // each hand's lowest and highest key
    int best_permille = -1;  // the history's best accuracy for it, -1 for none
};

struct MelodyPath {
    std::string id;
    std::string title;
    std::string icon;
    std::vector<std::size_t> pieces; // places in MelodyBook::pieces, in the order played
};

struct MelodyBook {
    std::vector<MelodyPiece> pieces;
    std::vector<MelodyPath> paths;
    std::vector<std::string> problems; // the manifest's errors and the files that did not load

    bool empty() const { return pieces.empty(); }
    // Takes the best scores from the history's runs (the library's `summarise_plays` rule).
    void set_best(const std::unordered_map<std::uint64_t, SongPlays>& plays);
};

// Reads the manifest and every file it lists. A manifest with errors gives an empty book and the
// errors in `problems`.
MelodyBook load_melody_book(std::string_view manifest, const MelodyFileFn& files);
MelodyBook load_melody_book();

// A piece ready to play: its song, loaded the way a library file is but under the manifest's
// title, and its composer for the toast that says what opened.
struct OpenedMelody {
    LoadedSong song;
    std::string composer;
};
// Null when the file does not load.
std::optional<OpenedMelody> open_melody(const MelodyPiece& piece, const MelodyFileFn& files);

} // namespace etude::app
