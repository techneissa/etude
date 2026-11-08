// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen: a place to learn, over everything under the top row, opened from the Learn
// button beside the library's. While it is open the song is paused and silent, as with the library
// and the settings. Its parts are laid out by learn_layout.hpp.
//
//   gallery   a 2x2 grid of tiles with a picture each (Scales and Chords over Ear and Melodies) and a
//             Continue row that opens the scale the player looked at last.
//   scales    the scale card (#370): pick a key (the ring of fifths, or the twelve keys as buttons;
//             a small switch changes the look and the choice is kept per profile) and a scale type;
//             the keys of the keyboard are lit in the card's colour for the scale and the fingers of both hands are
//             numbered above and below; Listen sounds the scale up and down in the hands chosen with
//             the two hand toggles. The key, type, hands and look are kept in the profile's settings
//             (`learn_*`), so the card opens on the scale it was left on.
//   ear       the ear card (#374, learn_ear_card.cpp): the piano plays a note or an interval and the
//             player finds it on the keys.
//   melodies  the bundled pieces in tutorial paths (#375, learn_melodies.cpp): a tile for each, its
//             progress the tile's border; a tap opens the piece in the normal player.
#pragma once

#include "bar.hpp"
#include "ear_layout.hpp"
#include "ear_session.hpp"
#include "chip_strip.hpp"
#include "kit.hpp"
#include "key_ring.hpp"
#include "learn_draw.hpp"
#include "learn_layout.hpp"
#include "learn_melodies.hpp"
#include "learn_port.hpp"
#include "library_port.hpp"

#include "../library_row.hpp"

#include "etude/chord_diagram.hpp"
#include "etude/scale_diagram.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace etude::app::ui {

class LearnScreen {
  public:
    // Shows the gallery (the cards' page), or the scale card straight away.
    void open();
    void open_scales();
    void open_chords();
    void open_ear();
    void open_melodies();
    void close();
    bool is_open() const { return open_; }
    // Listen is sounding a scale (the keys light) or a finger is moving the Melodies page or coasting
    // on after it: the frame loop keeps drawing.
    bool listening() const {
        return open_ && (playing_ || ear_sounding_ || (page_ == Page::Melodies && melodies_.moving()));
    }
    // A key pressed on a keyboard of the player's own: the ear card's answer, at the next draw.
    void note_on(int note);
    void set_ear_seed(std::uint64_t seed);
    // Still drawn while it fades out.
    bool showing() const { return open_ || open_p_ > 0.0f; }
    // Sliding away (closed, still on screen): the screen takes no pointer input from the next frame
    // on. Called every frame of the slide, from the frame the close starts.
    void stop_input() const { block_input("##learn"); }
    // Draws it over everything under the top row. `model.app.learn_*` is what it keeps. `library` is
    // the song library the Melodies page sorts a row of songs from (null: no such row).
    void draw(Kit& k, UiModel& model, LearnPort& port, LibraryPort* library, const Frame& f);
    // The "Open library" button of the Melodies page was tapped since the last call (once): the host
    // closes this screen and opens the library.
    bool take_library_ask() { return std::exchange(library_asked_, false); }
    // Escape: the card back to the gallery. False when nothing was open: the screen itself should
    // close.
    bool back();
    // The page open, as the name the next start comes back to.
    const char* resume_screen() const;

  private:
    enum class Page : std::uint8_t { Gallery, Scales, Chords, Ear, Melodies };

    // The header row's pieces; `card` is the scale card's, with the tabs and the look switch.
    bool draw_back(Kit& k, const Box& box, const char* id);
    void draw_gallery(Kit& k, UiModel& model, LearnPort& port, const Box& area);
    void draw_tile(Kit& k, const LookSettings& look, int card, const LearnTileBoxes& t, bool built,
                   const char* detail);
    // The Melodies page (learn_melodies.cpp).
    void draw_melodies(Kit& k, UiModel& model, LearnPort& port, const Box& area);
    // The "From your library" row: the library's songs sorted into steps and the player's added ones
    // (learn_melodies_library.cpp), made again only when the library's songs or ratings, the added list
    // or the page's opening changed; the best scores are read live. Null library: no row.
    const LibraryTiles& library_tiles(AppSettings& app, double now);
    bool tiles_point_at_songs(const LibraryCatalogue& cat) const;
    void build_tiles(AppSettings& app, LibraryCatalogue& cat);
    void draw_scales(Kit& k, UiModel& model, LearnPort& port, const Box& area);
    void draw_chords(Kit& k, UiModel& model, LearnPort& port, const Box& area);
    // The ear card (learn_ear_card.cpp).
    void draw_ear(Kit& k, UiModel& model, LearnPort& port, const Box& area);
    void ear_left(Kit& k, const LookSettings& look, LearnPort& port, const EarLayout& e);
    void ear_streak(Kit& k, const LookSettings& look, const EarLayout& e);
    void ear_pill(Kit& k, const EarLayout& e);
    void ear_disc(Kit& k, LearnPort& port, const EarLayout& e);
    void ear_settings_row(Kit& k, AppSettings& app, const EarLayout& e);
    void ear_settings_card(Kit& k, AppSettings& app, const EarLayout& e);
    void ear_modes(Kit& k, AppSettings& app, const EarLayout& e);
    int ear_keys(Kit& k, const EarLayout& e);
    void ear_foot(Kit& k, LearnPort& port, const EarLayout& e);
    void ear_press(Kit& k, LearnPort& port, int note, bool sound);
    void ear_hear(Kit& k, LearnPort& port, const EarQuestion& question);
    void ear_next(Kit& k, LearnPort& port);
    // The three tabs; the card shown (0 scales, 1 chords) is lit and a tap on the other built one
    // changes the page.
    void draw_tabs(Kit& k, const LookSettings& look, const std::array<Box, kLearnTabs>& tabs,
                   const std::array<bool, kLearnTabs>& labels, int shown);
    // The key picker for `root`, the keys spelled as a `spelling` scale's.
    void draw_picker(Kit& k, AppSettings& app, const LearnCardLayout& c, int* root,
                     ScaleType spelling, const KeyRingOpts& ring = {});
    void draw_types(Kit& k, AppSettings& app, const LearnCardLayout& c);
    // The keys of `d` and the fingers on them (or `none` where there is no fingering).
    void draw_keyboard(Kit& k, const ScaleDiagram& d, const LearnCardLayout& c, float right_alpha,
                       float left_alpha, const char* none, const LearnKeyPaint& paint,
                       const LearnLit* lit = nullptr);
    void draw_hands(Kit& k, AppSettings& app, const LearnCardLayout& c);
    void draw_chord_types(Kit& k, AppSettings& app, const LearnCardLayout& c);
    void draw_chord_name(Kit& k, const AppSettings& app, const LearnCardLayout& c);
    void draw_inversion(Kit& k, AppSettings& app, const Box& box);
    void draw_scale_name(Kit& k, const AppSettings& app, const LearnCardLayout& c);
    // The drill's controls (learn_drill_ui.cpp): the tempo, direction, octaves and scale / arpeggio
    // where the card puts them (or behind one button), the foot row's hands, Listen and Practice, and
    // the stepper the tempo and the chord's inversion share (-1 or 1 for the cell tapped).
    void draw_drill(Kit& k, AppSettings& app, const LearnCardLayout& c, bool kinds);
    void draw_drill_rows(Kit& k, AppSettings& app, const DrillBoxes& d, bool kinds);
    void draw_foot(Kit& k, AppSettings& app, LearnPort& port, const LearnCardLayout& c, bool chords);
    int draw_stepper(Kit& k, const Box& b, const char* minus_id, const char* plus_id, bool can_minus,
                     bool can_plus, const char* icon, const char* words, const char* short_words,
                     float label_pt);
    void listen(Kit& k, AppSettings& app, LearnPort& port, bool chords);
    void practise(Kit& k, AppSettings& app, LearnPort& port, bool chords);
    // The `count` chips (`ids`, `words`) in the strip `view`, scrolling sideways, the chip `current`
    // lit and kept in view; the place of the one tapped this frame, else -1.
    int draw_strip(Kit& k, const Box& view, const char* const* ids, const char* const* words,
                   int count, int current);
    void strip_pointer(Kit& k, const Box& view);
    void strip_fades(Kit& k, const Box& view);
    const ChordDiagram& chord_view(const AppSettings& app);
    // The scale of the settings as a diagram, made again only when the key or the type changed.
    const ScaleDiagram& diagram(const AppSettings& app);

    bool open_ = false;
    float open_p_ = 0.0f;
    Page page_ = Page::Gallery;
    Page play_page_ = Page::Scales; // the card Listen is sounding for
    ChipStrip strip_;        // the narrow card's strip of types
    int strip_shown_ = -1;   // the type the strip last brought into view

    ScaleDiagram diagram_;
    int diagram_root_ = -1;
    int diagram_type_ = -1;
    ChordDiagram chord_;
    std::string chord_word_; // the key ring's word for chord_ (learn_chord_centre_word)
    int chord_root_cached_ = -1;
    int chord_quality_cached_ = -1;
    int chord_inversion_cached_ = -1;

    // The ear card: the session, the notes pressed on the player's own keyboard waiting for the next
    // draw, and the question's sound (its disc pulses while it plays).
    EarSession ear_{1};
    bool ear_seeded_ = false;
    bool ear_failed_ = false; // the settings cannot make a question: not asked again until they change
    std::array<int, 8> ear_in_{};
    int ear_in_n_ = 0;
    bool ear_sounding_ = false;
    bool ear_settings_open_ = false; // the short form's Settings card is up
    double ear_t0_ = 0.0;
    double ear_until_ = 0.0;
    FitText ear_name_;        // the answered question's name for the pill
    MelodiesView melodies_;
    LibraryPort* library_ = nullptr;
    LibraryTiles library_tiles_;
    LibraryRow library_row_;
    std::string tiles_added_;           // the added list the tiles were made from
    std::uint64_t tiles_songs_ = 0;     // and the catalogue's versions
    std::uint64_t tiles_facts_ = 0;
    std::uint64_t tiles_versions_ = 0;
    double tiles_built_at_ = -1e9;      // the clock (Kit::now) when they were made
    bool tiles_stale_ = true;           // made again on the next frame (the page was opened)
    bool library_asked_ = false;
    FitText melody_detail_;   // the Melodies tile's "3 paths, 14 pieces"
    bool playing_ = false;
    double play_until_ = 0.0; // the clock (Kit::now) when the scale ends
    double play_t0_ = 0.0;    // and when Listen was tapped, which the lit keys are timed from
    bool settings_open_ = false; // the Popover column's settings card is up
    float type_need_ = 0.0f; // the widest type name in the chips' font, design points (cell need)
};

} // namespace etude::app::ui
