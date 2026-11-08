// SPDX-License-Identifier: GPL-3.0-or-later
// The Melodies page of the Learn screen (#375): a section for each tutorial path
// with a tile for each of its pieces (learn_melodies_layout.hpp places them). A tap on a tile, or on
// a path's Continue, asks for that piece; the screen opens it in the normal player.
//
// The tile's border is the piece's progress: it fills clockwise from the top middle, yellow at the
// start through orange to green as the best score nears the pass mark, and is all green with a check
// once passed (the library's rule: core melody_progress.hpp); the ring round Play fills the same
// way. The difficulty is the library's five stars (`draw_level_stars`: the level's colour, filled, half
// and empty) between the hands and Play, the hands each with their key count under them.
//
// Everything a frame draws is laid out from numbers: the titles are wrapped once when the tile's
// width changes (`titles_`), the row of a path that does not fit across scrolls sideways (a
// `ChipStrip` each), and the page scrolls down where the sections do not fit, the two never taking
// one touch (`ScrollDrag` with its axis lock). A frame that changed nothing measures no text and
// allocates nothing: the titles were wrapped when the width last changed, the button's words are
// measured once for their size, and every other label goes through `label_at`, whose metrics the kit
// keeps (kit_text.cpp).
#pragma once

#include "chip_strip.hpp"
#include "fit_text.hpp"
#include "kit.hpp"
#include "learn_melodies_layout.hpp"

#include "../melody_book.hpp"

#include "etude/melody_progress.hpp"
#include "etude/scroll_drag.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace etude::app::ui {

// The "From your library" row as the page draws it: a piece for each tile (the song's title, its step,
// its rating, the keys each hand covers and its best score), and where each is in the library. Built by
// the screen from the library's stored facts when those changed (never per frame, never from a file).
struct LibraryTiles {
    bool on = false;                  // there is a library to show a row of (else the section is left out)
    std::vector<MelodyPiece> pieces;  // one a tile, in step order; `goal` is the composer, else the file's name
    std::vector<std::size_t> index;   // each one's place in the library's catalogue
    std::size_t sorted = 0;           // pieces that fit a step (or were added), shown or not
    std::uint64_t serial = 0;         // changes when the tiles do (the titles are wrapped again)
};

// What the player tapped on the page: a bundled piece (a place in `book.pieces`), a library tile (a
// place in `LibraryTiles::pieces`), or the "Open library" button. -1 for none.
struct MelodyAsk {
    int piece = -1;
    int library = -1;
    bool open_library = false;
};

class MelodiesView {
  public:
    MelodiesView();
    // The page's layout for this frame, after the page's touch and wheel moved it: the header's
    // parts and the sections. Call it before drawing either.
    MelodiesLayout begin(Kit& k, const Box& area, const MelodyNeeds& needs);
    // The body: every section with its tiles, clipped under the header, and the library's row
    // under them. Returns what the player tapped.
    // `colour` is the page's own, the player's pick for Melodies (`learn_section_colour`).
    MelodyAsk draw_body(Kit& k, const MelodyBook& book, const LibraryTiles& library,
                        const MelodiesLayout& lay, RGBA colour);
    // The page was left (or is not drawn): a touch or a coast in progress stops.
    void cancel();
    // A finger is on the page or a row, or one is coasting after a fling: frames must be drawn.
    bool moving() const;
    // How many of the page's pieces are shown whole (not cut by the row's or the page's edge).
    int whole_tiles() const { return whole_; }
    // How far the page is scrolled down, in design points.
    float scroll() const { return scroll_; }

  private:
    // A piece's title as the tile's width lays it out: one line, or two with the second cut short.
    struct Title {
        FitText line[2];
        int lines = 1;
        float tag_w = 0.0f;    // the version tag's pill, 0 for a piece with none
        float second_w = 0.0f; // the second line's width, where the tag follows it
    };
    void page_pointer(Kit& k, const Box& body, float max_scroll);
    void row_pointer(Kit& k, std::size_t path, const Box& view);
    void wrap_titles(Kit& k, const std::vector<MelodyPiece>& pieces, std::vector<Title>& out, float width,
                     float size, bool two_lines);
    void draw_section(Kit& k, const MelodyBook& book, std::size_t path,
                      const MelodiesLayout& lay, MelodyAsk* asked);
    void draw_head(Kit& k, const MelodyBook& book, std::size_t path, const MelodyPathProgress& progress,
                   const MelodySectionBoxes& s, MelodyAsk* asked);
    // One tile: a bundled piece (`library` false) or a library song, whose badge is its step and whose
    // second line says where it is from.
    void draw_tile(Kit& k, const MelodyPiece& piece, const Title& title, int place, bool next,
                   const Box& tile, const Box& visible, const MelodiesLayout& lay, const char* id,
                   bool library, bool* pressed);
    // The library's section: its header with "Open library", and its row of tiles.
    void draw_library(Kit& k, const LibraryTiles& library, const MelodiesLayout& lay, MelodyAsk* asked);
    // The tiles of one section's row, `count` of them, scrolled by its strip: calls
    // `one(place, tile, visible)` for each that shows.
    template <class One>
    void draw_row(Kit& k, const MelodiesLayout& lay, const MelodySectionBoxes& s, std::size_t strip_at,
                  std::size_t count, One&& one);
    float button_words_width(const Kit& k, std::size_t kind);
    // The title in its room, with the piece's version tag as a muted pill under a one-line title or after
    // the second line of a two-line one.
    void draw_title(Kit& k, const MelodyPiece& piece, const Title& title, const MelodyTileBoxes& t, float size,
                    ImVec4 ink, bool library_short);
    void draw_hands(Kit& k, const MelodyPiece& piece, const MelodyTileBoxes& t);
    void draw_play(Kit& k, const MelodyTileBoxes& t, const char* id, MelodyMark mark, float share);

    ScrollDrag page_drag_{};
    float scroll_ = 0.0f;
    std::array<ChipStrip, kMelodyMaxPaths + 1> strips_{}; // each path's row, then the library's
    std::vector<Title> titles_; // a title for each piece of the book
    float title_width_ = -1.0f; // what they were wrapped to, in points, and at which size
    float title_size_ = -1.0f;
    std::vector<Title> library_titles_; // and for each tile of the library's row
    float library_title_width_ = -1.0f;
    float library_title_size_ = -1.0f;
    std::uint64_t library_titles_for_ = ~std::uint64_t{0};
    bool library_two_lines_ = true; // whether they were wrapped to two lines
    // The words before the next piece's name ("Next ·"), measured once for the font size.
    float next_prefix_w_ = 0.0f;
    float next_prefix_size_ = -1.0f;
    // The widths of "Start", "Continue" and "Again" in the button's font, measured once for its size.
    std::array<float, 3> words_w_{};
    float words_size_ = -1.0f;
    int whole_ = 0;
    RGBA colour_{}; // the page's colour as of the frame being drawn
};

} // namespace etude::app::ui
