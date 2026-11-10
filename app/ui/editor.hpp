// SPDX-License-Identifier: GPL-3.0-or-later
// The section editor: a piano-roll strip of the whole song to see where the music breathes, and
// the means to cut it there and hear what is being cut. It opens full (over the screen, from the
// section band's editor button) or docked as the lane under the section band (its roll, bands,
// handles and cursor with a grid of four tools, see core lane_layout): the same view, touches and
// edits either way.
//
//   toolbar   close, fit, zoom out and in, undo, suggestions
//   roll      the notes (hand colours) with a bar ruler; sections tint it, a cursor crosses it
//   bands     each section's name (tap to rename), its colour, and the handles between them
//   loupe     the notes around the cursor (or the handle being dragged), magnified, split by the
//   cut actions   cut at the cursor, play from it, play across it, and the selected band's own bar
//
// Touch first, mouse too. Fingers come in as SDL finger events and go through core's
// GestureRecognizer (tap, double tap, long press, drag, pinch, fling), so two fingers pinch around
// their midpoint, one finger pans, and a drag of a handle or the cursor starts only when the touch
// began on its grab zone (about 15 mm wide). A mouse feeds the same recogniser: drag pans, the
// wheel or a trackpad pinch zooms, a right click is a long press. Long press: a cut where the
// finger is (on a handle: join the two sections, on a band's name: rename). A double tap on a band
// zooms to it; on the roll it zooms in. The cursor and the handles snap to bars, and to beats when
// zoomed in, with a flash where they land; while one is dragged a magnifier floats above the
// finger. Pans and pinches coast on after the fingers lift and stop at the next touch. Releasing
// the cursor or a handle plays about two seconds from there, through the song's sound path.
#pragma once

#include "bar.hpp"
#include "kit.hpp"
#include "sections_port.hpp"

#include "etude/editor_layout.hpp"
#include "etude/finger_claims.hpp"
#include "etude/handle_grab.hpp"
#include "etude/roll_view.hpp"
#include "etude/section_editing.hpp"
#include "etude/touch_gesture.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>

#include <vector>

namespace etude::app::ui {

// A tap or Play on the music: stops it while it plays, else plays `r` (tap_play), which starts
// again while the band's loop is on when `repeat` is set (audition_repeat).
inline void play_or_stop(SectionsPort& sp, const AuditionRange& r, std::int64_t now_ns,
                         bool repeat = false) {
    if (tap_play(sp.auditioning(), false) == TapPlay::Stop) {
        sp.stop_audition(now_ns);
    } else if (repeat) {
        sp.audition_repeating(r.start_s, r.end_s, now_ns);
    } else {
        sp.audition(r.start_s, r.end_s, now_ns);
    }
}

class SectionEditor {
  public:
    explicit SectionEditor(SDL_Renderer* renderer);
    ~SectionEditor();
    SectionEditor(const SectionEditor&) = delete;
    SectionEditor& operator=(const SectionEditor&) = delete;

    // The renderer is being replaced: the roll's tile goes while the old one is alive, and is drawn
    // again for the new one.
    void drop_renderer();
    void adopt_renderer(SDL_Renderer* renderer) { renderer_ = renderer; }

    // The full editor is open (over everything under the top row).
    bool is_open() const { return open_; }
    // Shows the whole song with the cursor at the playhead.
    void open(Kit& k);
    void close(Kit& k);

    // Draws the editor below the top row. Returns true when it asks to close.
    bool draw(Kit& k, const Frame& f);

    // The lane: the editor docked in `lane` (points) under the section band, with the whole song
    // shown and the cursor at the playhead when it opens. `bubble` is where the magnifier may float
    // while a handle or the cursor is dragged. Closing it ends the audition and the suggestions.
    bool lane_open() const { return lane_; }
    void open_lane(Kit& k);
    void close_lane(Kit& k);
    void draw_lane(Kit& k, const RectF& lane, const RectF& bubble);

    // Fingers (SDL_EVENT_FINGER_*, in window points via `display`) and trackpad pinches.
    // `other_fingers_down`: the host knows of a finger already on the screen, so a landing one is a
    // second finger; false makes it the first, whatever the recogniser still holds (a lost lift).
    // `on_overlay`: a landing finger is on a control drawn over the editor (a toast's Undo), so it
    // is that control's and the roll never sees it.
    void on_event(const SDL_Event& event, ImVec2 display, std::int64_t now_ns,
                  bool other_fingers_down = true, bool on_overlay = false);

  private:
    enum class Drag : std::uint8_t { None, Pan, Cursor, Handle };

    void toolbar(Kit& k, const SectionsView& v, const EditorLayout& l);
    void roll(Kit& k, const SectionsView& v, const EditorLayout& l);
    void loupe(Kit& k, const SectionsView& v, const EditorLayout& l);
    void actions(Kit& k, const SectionsView& v, const EditorLayout& l);
    std::size_t selected_index(const SectionsView& v) const;
    void play_pressed(Kit& k, const SectionsView& v, std::size_t index);
    void lane_tools(Kit& k, const SectionsView& v, const LaneLayout& l);
    void reset(Kit& k);
    void ensure_tile(Kit& k, const SectionsView& v, const EditorLayout& l);
    void gestures(Kit& k, const SectionsView& v, const EditorLayout& l);
    void on_tap(Kit& k, const SectionsView& v, const EditorLayout& l);
    void on_gesture(Kit& k, const SectionsView& v, const EditorLayout& l,
                    const GestureRecognizer::Event& e);
    void draw_loupe(Kit& k, ImDrawList* dl, const SectionsView& v, const RectF& rect, double focus);
    void floating_loupe(Kit& k, const SectionsView& v);
    void touch_feedback(Kit& k, const EditorLayout& l);
    void note_snap(Kit& k, const SectionsView& v, double snapped);
    EditorHit hit_at(Kit& k, const SectionsView& v, const EditorLayout& l, float x, float y) const;
    void zoom(const SectionsView& v, double factor, double focus_s);
    void place_cursor(const SectionsView& v, double seconds);
    bool beats(const SectionsView& v) const;

    SDL_Renderer* renderer_;
    bool open_ = false;
    bool lane_ = false;
    RectF bubble_area_; // where the floating magnifier may go

    // What is shown.
    RollView view_;
    double cursor_s_ = 0.0;
    bool cursor_set_ = false;
    SectionId selected_ = kNoSection;
    bool want_close_ = false;

    // The gesture in progress.
    GestureRecognizer gestures_;
    Drag drag_ = Drag::None;
    bool held_ = false;     // drag_ is a handle a long press took (not one a drag started on)
    HandleGrab grab_;       // the held handle's state (core)
    FingerClaims claims_;   // fingers that landed on a control over the editor
    bool tracking_ = false; // the touch began in the roll area
    bool pinching_ = false; // a two-finger gesture whose midpoint began in the roll area
    bool momentum_ok_ = false; // a fling or zoom fling that belongs to the roll
    bool mouse_down_ = false;  // the recogniser holds a mouse press
    float pinch_x_ = 0.0f;     // the pinch midpoint, kept up to date
    ImVec2 touch_pt_{0, 0};    // where the finger (or pointer) is
    ImVec2 press_{0, 0};
    RollView press_view_;
    EditorHit hit_;
    std::size_t cut_hit_ = 0;
    bool cut_badge_ = false;
    std::size_t handle_ = 0;
    double handle_s_ = 0.0;
    double grab_s_ = 0.0;   // the song time under the finger when the drag began
    double orig_s_ = 0.0;   // where the handle or cursor was then
    bool moved_ = false;
    double last_finger_s_ = -10.0; // when a finger event last came (the mouse is ignored while fresh)
    double clock_s_ = 0.0;         // the latest time seen, so the recogniser never goes back
    double external_zoom_ = 1.0;   // trackpad pinch, accumulated until the next frame
    // Where the last snap landed and when, for the flash.
    double snap_at_s_ = -1.0;
    double snap_time_ = -10.0;
    bool snap_bar_ = false;
    double press_marker_s_ = 0.0; // the time of the handle a press landed on

    // The cached note image.
    SDL_Texture* tile_tex_ = nullptr;
    int tex_h_ = 0;
    RollTile tile_;
    std::uint32_t tile_serial_ = 0xFFFFFFFFu;
    int tile_view_w_ = 0;
    ImVec4 tile_left_{}, tile_right_{};
    PitchRange pitches_;

    // Scratch, kept between frames so drawing allocates nothing once warm.
    std::vector<RollRect> rects_;
    std::vector<SDL_FRect> frects_;
    std::vector<GridLine> grid_;
    std::vector<float> name_w_;
    std::vector<GestureRecognizer::Event> evs_;
};

} // namespace etude::app::ui
