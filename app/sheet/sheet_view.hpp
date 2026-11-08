// SPDX-License-Identifier: GPL-3.0-or-later
// Sheet music on screen: the frame-thread half. It asks the sheet worker to engrave the open song
// at the size the note area wants, turns the systems it draws into textures (at most one a frame),
// and each frame colours the note marks from the song's grades and moves the cursor, writing into
// buffers sized when the layout arrived: the steady state allocates nothing. A new engraving (a
// resize, another view) is prepared behind the one on screen, which stays up, scaled, until the
// new one has its pictures, so a change never leaves the score blank.
#pragma once

#include "sheet_name_fit.hpp"
#include "sheet_worker.hpp"
#include "song_controller.hpp"
#include "text.hpp"

#include "etude/look.hpp"
#include "etude/session_state.hpp"
#include "etude/sheet_map.hpp"
#include "etude/sheet_names.hpp"
#include "etude/sheet_target.hpp"
#include "etude/sheet_view.hpp"
#include "etude/touch_gesture.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace etude::app {

// Whether sheet music can be shown for the open song, for the view switch.
struct SheetOffer {
    bool song = false;    // a song is open
    bool known = false;   // the worker has answered
    bool offered = false; // it has sheet music (false while not known)
    std::string_view reason; // why not, when known and not offered
};

// What a gesture on the score asks of the song; the caller applies it (SongController).
struct SheetRequest {
    enum class Kind : std::uint8_t {
        None,
        Start, // a double tap: play from `beat_tick` (Wait Mode: wait on the chord at `onset_tick`)
        Loop,  // a long press dragged across bars: loop [start_tick, end_tick)
    };
    Kind kind = Kind::None;
    std::int64_t beat_tick = 0;
    std::int64_t onset_tick = 0;
    std::int64_t start_tick = 0;
    std::int64_t end_tick = 0;
};

// What the bench reads of the sheet.
struct SheetViewStats {
    int textures = 0;              // systems with pictures
    double first_picture_ms = 0.0; // from the song's first job to its first picture on screen
};

class SheetView {
  public:
    explicit SheetView(SDL_Renderer* renderer);
    ~SheetView();
    SheetView(const SheetView&) = delete;
    SheetView& operator=(const SheetView&) = delete;

    // The renderer is being replaced (gfx::replace_renderer): the pictures go while the old one is
    // alive, and are asked of the worker again once the new one is adopted.
    void drop_renderer();
    void adopt_renderer(SDL_Renderer* renderer);

    // Follows the open song (a new one starts afresh). Call first each frame.
    void sync_song(const SongController& songs, std::int64_t now_ns);
    // The reading helps and how they read (LookSettings: names_on_sheet, sheet_note_colours and
    // the naming and note colours they follow) and the screen's density, for the names' smallest
    // size. Only notes the change (the names and colours are made when a score needs them); call
    // each frame before update().
    void set_helps(const LookSettings& look, float px_per_mm);
    // The open song has notes for both hands: it is engraved on a grand staff.
    bool two_staves() const { return two_staves_; }
    // The room its note names take in the plan (none while names are off): lanes for the song's
    // tallest stacks at the names' one size.
    SheetNameRoom name_room(float px_per_mm) const;
    SheetOffer offer() const;

    // `engrave` is the plan the score is engraved for (the Sheet view's while the falling notes
    // show, so a switch has pictures ready); `shown` is the plan drawn this frame (not shown: the
    // falling notes alone). Takes the worker's results, asks for what is missing, places the
    // cursor and colours the marks.
    void update(const SongController& songs, const SheetPlan& engrave, const SheetPlan& shown,
                std::int64_t now_ns, bool reduced_effects);
    // Draws the paper, the systems and the cursor into `shown.sheet`.
    void draw(const SheetPlan& shown);

    // Touch and mouse on the score: a double tap (double click) starts there; a long press (or a
    // right click) dragged across bars sets a loop; a single tap does nothing. Events the UI owns
    // (`ui_owns_point`, the mouse while ImGui wants it) and those off the score are left alone.
    void set_ui_owns_point(std::function<bool(float, float)> owns) { ui_owns_point_ = std::move(owns); }
    // `other_finger_down`: for a finger that lands, whether another is already on the screen (the
    // host counts them); false makes it the first, whatever the recogniser still holds.
    void handle_event(const SDL_Event& event, SDL_Window* window, std::int64_t now_ns,
                      bool other_finger_down = true);
    // What the gestures asked for since the last call, oldest first.
    bool take_request(SheetRequest& out);

    const SheetViewStats& stats() const { return stats_; }

  private:
    struct Picture {
        int system = -1;
        SDL_Texture* base = nullptr;
        SDL_Texture* notes = nullptr;
        SDL_Texture* rings = nullptr;
        int width = 0;
        int height = 0;
    };
    struct Engraving {
        std::uint64_t generation = 0;
        bool ready = false;
        EngraveOptions options;
        SheetLayout layout;
        SheetMap map;
        std::vector<std::vector<std::uint32_t>> marks_of_system; // note and stem marks
        std::vector<Picture> pictures;
        std::vector<SheetNames> names; // per system, placed for `names_serial`
        std::uint32_t names_serial = 0;
        std::vector<int> box_of_written; // written bar -> its SheetBar (index), -1 for none
    };

    void drop_pictures(Engraving& e);
    const Picture* picture_of(const Engraving& e, int system) const;
    void take_results(std::int64_t now_ns);
    void ask_pictures(Engraving& e, int cursor_system);
    void picture_failed();
    void colour_marks(const SongController& songs, const Engraving& e, int system, double tick);
    void draw_system(const Engraving& e, const Picture* pic, int system, float x, float y,
                     float scale, const SheetPlan& plan);
    void ready_helps();
    void place_names(Engraving& e);
    void run_gestures(const SongController& songs, std::int64_t now_ns);
    void drop_gesture(); // whatever touch is in progress, and its events
    // The system and the x in its picture under a window pixel, from the last draw; false off it.
    bool hit(float px, float py, int& system, float& x) const;
    void draw_loop_band(const Engraving& e, int first_bar, int last_bar, bool dragging);
    void draw_rings(const Engraving& e, const Picture& pic, int system, float x, float y, float scale);
    void draw_names(const Engraving& e, int system, float x, float y, float scale);
    // The text of name `n` at `px`, made the first time (null for a "…" or no such name).
    TextSlot* name_slot(const Engraving& e, const SheetName& n, float px);

    SDL_Renderer* renderer_;
    std::unique_ptr<SheetWorker> worker_;
    std::uint32_t song_serial_ = 0;
    bool has_song_ = false;
    bool two_staves_ = false;
    std::shared_ptr<const LoadedSong> song_;
    std::uint64_t next_generation_ = 1;
    std::uint64_t asked_ = 0; // the generation of the last job sent
    EngraveOptions asked_options_;
    std::int64_t want_since_ns_ = 0; // when the size wanted last changed
    EngraveOptions want_options_;
    std::int64_t song_job_ns_ = 0;
    bool answered_ = false;
    bool offered_ = false;
    std::string reason_;
    int failed_pictures_ = 0; // in a row: a picture not drawn or a texture not made
    Engraving front_; // on screen
    Engraving back_;  // being prepared
    // The page turn: the top slot's system, sliding.
    float turn_from_ = 0.0f;
    float turn_to_ = 0.0f;
    std::int64_t turn_ns_ = 0;
    float top_ = 0.0f;
    // The cursor of the engraving on screen.
    SheetCursor cursor_;
    double tick_ = 0.0;
    HandFilter hands_ = HandFilter::Both;
    // Per mark of the engraving on screen: its colour this frame (index into the colours).
    std::vector<std::uint8_t> mark_colour_;
    // Per mark: its colour ring's strength (1 still to play, fading to 0 once graded; below 0
    // until first coloured).
    std::vector<float> ring_alpha_;
    float fade_dt_ = 0.0f; // seconds since the last update, for the fades
    std::int64_t last_update_ns_ = 0;
    bool reduced_ = false;
    // Scratch for one system's note quads, sized to the largest system.
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
    std::array<int, 8> wanted_{};
    SheetViewStats stats_;
    // The reading helps: what they follow, and what was made of it (per MIDI key).
    bool names_on_ = false;
    bool colours_on_ = false;
    LookSettings helps_look_;   // the look the helps read, as last seen
    bool helps_primed_ = false;
    bool naming_dirty_ = true;  // the names must be made again before they are drawn
    bool colours_dirty_ = true; // the colours too
    float px_per_mm_ = 0.0f;
    float name_slot_px_ = 0.0f;  // the shown systems' slot height and width the names were
    float name_width_px_ = 0.0f; // placed for
    float name_margin_px_ = 0.0f; // ... and the paper's margin (a tall system draws smaller)
    std::uint32_t names_serial_ = 1; // bumped when the names or their size rule change
    SheetNameTexts names_;       // the names' words and widths, made by ready_helps()
    float line_em_ = 1.2f;       // the font's line height per pixel of size
    SheetNameStacks stacks_;        // the song's tallest stacks of names, above and below
    SheetNameStacks chord_stacks_;  // ... with its chords as one symbol each
    std::array<SDL_FColor, 128> note_colour_{};
    Text text_;
    // Key: font px | id << 10 | kind << 30 (id: a note's MIDI key or a symbol's index).
    std::unordered_map<std::uint32_t, TextSlot> name_slots_;
    std::vector<float> unit_width_of_mark_;                   // scratch for placing
    std::vector<SheetNameColumn> name_columns_;               // scratch for placing
    SheetSymbols symbols_; // the chord symbols placed since the names were last made
    // Gestures. Positions fed to the recogniser are window points; hits are in pixels.
    GestureRecognizer gestures_;
    std::function<bool(float, float)> ui_owns_point_;
    bool gesture_live_ = false;     // a touch that began on the score is in progress
    bool mouse_live_ = false;       // ... and it is the mouse's
    double last_finger_s_ = -1e9;
    double clock_s_ = 0.0; // the gestures' clock: never goes back
    float pixel_scale_ = 1.0f;
    struct Placed {
        int system = -1;
        float x = 0.0f, y = 0.0f, scale = 1.0f; // where its picture went
        float top = 0.0f, bottom = 0.0f;        // its slot on the paper
    };
    std::array<Placed, 4> placed_{};
    int placed_count_ = 0;
    RectF paper_;
    bool dragging_loop_ = false;
    int drag_anchor_ = -1; // played bars
    int drag_last_ = -1;
    std::array<SheetRequest, 4> requests_{};
    int request_count_ = 0;
    // The loop the song has, as played bars (for the band on the score), -1: none.
    int loop_first_ = -1;
    int loop_last_ = -1;
};

} // namespace etude::app
