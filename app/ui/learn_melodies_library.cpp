// SPDX-License-Identifier: GPL-3.0-or-later
// The "From your library" row of the Melodies page: the tiles made from the library's stored facts
// (app/library_row.hpp). Made when the library's songs or ratings, the player's added list or the
// page's opening changed, never in a frame that changed none of them; a frame only refreshes the
// best scores, which are numbers already in the catalogue.
#include "learn_screen.hpp"


namespace etude::app::ui {

namespace {

// A hand's keys as the tile's glyph reads them: `keys` from the lowest key up, none for no notes.
MelodyHandRange range_of(bool used, int keys) {
    if (!used || keys <= 0) {
        return {};
    }
    return {0, keys - 1};
}

} // namespace

// Whether every tile still points at its song (the catalogue lays its list out again as the library
// is read, and an index then means another song).
bool LearnScreen::tiles_point_at_songs(const LibraryCatalogue& cat) const {
    const LibraryTiles& tiles = library_tiles_;
    for (std::size_t i = 0; i < tiles.pieces.size(); ++i) {
        if (tiles.index[i] >= cat.size() || cat.song(tiles.index[i]).hash != tiles.pieces[i].hash) {
            return false;
        }
    }
    return true;
}

// Makes the tiles from the library's stored facts, and has the added list follow the files the player
// edited (a new hash, the same path).
void LearnScreen::build_tiles(AppSettings& app, LibraryCatalogue& cat) {
    LibraryTiles& tiles = library_tiles_;
    tiles_stale_ = false;
    cat.refresh_versions(); // the groups of versions are laid over the songs as the library screen would
    tiles_songs_ = cat.songs_version();
    tiles_facts_ = cat.facts_version();
    tiles_versions_ = cat.versions_serial();
    build_library_row(cat, MelodyAdded::parse(app.melody_added), library_row_);
    if (!library_row_.followed.empty()) {
        MelodyAdded list = MelodyAdded::parse(app.melody_added);
        for (const LibraryRowFollow& f : library_row_.followed) {
            list.rebind(f.old_hash, f.new_hash, f.path);
        }
        app.melody_added = list.text();
    }
    tiles_added_ = app.melody_added;
    tiles.pieces.clear();
    tiles.index.clear();
    for (const LibraryRowSong& r : library_row_.songs) {
        const CatalogueSong& s = cat.song(r.index);
        MelodyPiece p;
        p.title = s.title;
        p.composer = s.composer;
        p.step = r.step;
        p.hash = s.hash;
        p.half_stars = s.half_stars;
        p.shape.left = range_of(s.left, s.left_keys);
        p.shape.right = range_of(s.right, s.right_keys);
        // What tells two songs of one title apart: the composer, else the file's name.
        p.goal = s.composer.empty() ? s.file_name : s.composer;
        tiles.pieces.push_back(std::move(p));
        tiles.index.push_back(r.index);
    }
    tiles.sorted = library_row_.sorted;
    ++tiles.serial;
}

const LibraryTiles& LearnScreen::library_tiles(AppSettings& app, double now) {
    LibraryTiles& tiles = library_tiles_;
    if (library_ == nullptr) {
        tiles.on = false;
        return tiles;
    }
    LibraryCatalogue& cat = library_->catalogue();
    const bool added_changed = app.melody_added != tiles_added_;
    const bool changed =
        tiles_stale_ || added_changed || cat.songs_version() != tiles_songs_ || cat.facts_version() != tiles_facts_ ||
        cat.versions_serial() != tiles_versions_;
    // While the library is still being read it changes all the time: the row is then made at most once
    // a second, as long as its tiles still point at their songs.
    const bool wait = changed && !tiles_stale_ && !added_changed && cat.pending() > 0 &&
                      now - tiles_built_at_ < kLibraryRowEveryS && tiles_point_at_songs(cat);
    if (changed && !wait) {
        build_tiles(app, cat);
        tiles_built_at_ = now;
    } else if (!tiles_point_at_songs(cat)) {
        build_tiles(app, cat);
        tiles_built_at_ = now;
    }
    tiles.on = true;
    for (std::size_t i = 0; i < tiles.pieces.size(); ++i) {
        tiles.pieces[i].best_permille = cat.song(tiles.index[i]).best_permille;
    }
    return tiles;
}

} // namespace etude::app::ui
