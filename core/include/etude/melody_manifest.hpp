// SPDX-License-Identifier: GPL-3.0-or-later
// The list of the Learn screen's bundled melodies (app/assets/melodies/manifest.txt): the files
// with where each came from and what lets us bundle it, and the tutorial paths through them. Pure
// text in, values out: the app reads the files themselves.
//
// One record a line, fields apart by " | " (blank lines and lines starting with # are skipped):
//   file | NAME.mid | title | composer | step | source | licence
//   path | id | title | icon
//   goal | path id | NAME.mid | goal
//   version | NAME.mid | tag
//       an optional small tag the tile wears beside its title ("Easy", "Original"): which version of
//       a piece the file is, for a piece the manifest lists more than once
// A path's pieces are its `goal` lines in order, and each file is in exactly one path.
// `parse_melody_manifest` says what is wrong in `errors`, never throws.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// The grade steps: 1 (right hand, a few keys, the hand stays put) to 5 (both hands, a wide range).
inline constexpr int kMelodyFirstStep = 1;
inline constexpr int kMelodyLastStep = 5;

// What the Melodies page lays out: at most this many paths, and this many pieces in one. A manifest
// over either is refused (an error), never cut short.
inline constexpr int kMelodyMaxPaths = 6;
inline constexpr int kMelodyMaxPieces = 16;
// A tag is at most this many characters, so it fits a tile's narrowest title row.
inline constexpr std::size_t kMelodyMaxTag = 12;

// The icons a path may wear (the page draws one of these, by name).
inline constexpr std::array<std::string_view, 3> kMelodyIcons{"sprout", "handshake", "award"};

struct MelodyFileEntry {
    std::string file;     // the file's name, a .mid
    std::string title;    // shown on the tile
    std::string composer; // "Traditional" for a folk tune
    int step = 0;         // kMelodyFirstStep to kMelodyLastStep
    std::string source;   // where the notes come from
    std::string licence;  // what lets us bundle the file
    std::string version;  // the tag the tile wears ("Easy"); empty for none
};

struct MelodyPathEntry {
    std::string id;
    std::string title;
    std::string icon;                // the name of the icon the path wears
    std::vector<std::size_t> files;  // places in MelodyManifest::files, in the order played
    std::vector<std::string> goals;  // one line for each of them
};

struct MelodyManifest {
    std::vector<MelodyFileEntry> files;
    std::vector<MelodyPathEntry> paths;
    std::vector<std::string> errors; // one sentence each, with the line number

    bool ok() const { return errors.empty(); }
};

// A path's order is valid when no step is lower than the one before it. A manifest that fails this
// (or any other rule: an icon not in `kMelodyIcons`, more paths or pieces than the page lays out) has
// it in `errors`.
MelodyManifest parse_melody_manifest(std::string_view text);

} // namespace etude
