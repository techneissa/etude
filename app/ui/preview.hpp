// SPDX-License-Identifier: GPL-3.0-or-later
// The live look previews of the Notes page and the Effects page. A second Scene, the
// one the main screen draws with, renders a short loop into a small render target that ImGui
// shows as an image, so the preview matches the main screen and follows every change of style,
// intensity, colour, names and effects at once. It renders only on the frames it is drawn.
// - `PreviewLoop::Played`: notes played on the keys rise from them (a line, then a chord), with
//   the grade words.
// - `PreviewLoop::Song`: a song's notes fall onto the keys, white and black keys, both hands and a
//   note with no hand, soft to hard and over three octaves, so every colour scheme shows what it
//   does; each key is played as its note lands, so the hit effect and the key flash show too.
// - `PreviewLoop::Opening`: a song's own opening bars (set_opening), over the player's keyboard,
//   for the library's details; it loops after a short rest.
#pragma once

#include "kit.hpp"

#include "../scene.hpp"
#include "etude/key_state.hpp"
#include "etude/keyboard.hpp"
#include "etude/keyboard_view.hpp"
#include "etude/note_trail.hpp"
#include "etude/song_opening.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <bitset>
#include <memory>
#include <span>
#include <vector>

namespace etude::app::ui {

enum class PreviewLoop : std::uint8_t { Played, Song, Opening };

class LivePreview {
  public:
    explicit LivePreview(SDL_Renderer* renderer);
    ~LivePreview();
    LivePreview(const LivePreview&) = delete;
    LivePreview& operator=(const LivePreview&) = delete;

    // The renderer is being replaced: the target and the scene go while the old one is alive, and
    // are made again, on the next draw, for the new one.
    void drop_renderer();
    void adopt_renderer(SDL_Renderer* renderer) { renderer_ = renderer; }

    // Renders the preview for `look` and lays it out at the cursor, `width` points wide, 16:6.
    void draw(Kit& k, const LookSettings& look, float width);
    // The same with the `loop`, `width` by `height` points.
    void draw(Kit& k, const LookSettings& look, float width, float height, PreviewLoop loop);

    // The frame of the loop at `now_s` (seconds on the clock the loop runs on), rendered into the
    // preview's own target: `w` by `visible_h` output pixels show, under `crop` (the share of the
    // target's height above the note area, which the image leaves out). Null texture on failure.
    struct Rendered {
        SDL_Texture* texture = nullptr;
        float crop = 0.0f;
        float keys = 1.0f; // where the keyboard starts, as a share of the target's height
    };
    Rendered render(const LookSettings& look, int w, int visible_h, PreviewLoop loop,
                    double now_s);

    // The Opening loop over [a, b] with a clear band `band` points tall above the keys, for
    // controls: the notes fall to the band's top as they reach the keys, so they arrive on time and
    // nothing is drawn under the band. The keys' bottom corners are rounded by `rounding`. Returns
    // the band's top.
    float draw_banded(Kit& k, const LookSettings& look, ImVec2 a, ImVec2 b, float band,
                      float rounding);

    // The song the Opening loop plays (kept by the caller while it is drawn; null for none). A
    // different song starts the loop again over its own keys.
    // `keys`: the keyboard it falls onto, fixed for the whole preview (the player's, so it never
    // zooms with the notes; a note off it shows as an arrow at that edge).
    void set_opening(const SongOpening* opening, KeyRange keys);
    // The keys the loop pressed (velocity > 0) and let go (0) since the caller last cleared them,
    // for a caller that sounds them (at most a frame's worth is kept).
    struct Struck {
        std::uint8_t key = 0;
        std::uint8_t velocity = 0;
    };
    std::span<const Struck> struck() const { return {struck_.data(), struck_count_}; }
    void clear_struck() { struck_count_ = 0; }
    // The keys the loop holds down now.
    const std::bitset<128>& sounding() const { return sounding_; }
    // Runs the Opening loop to `now_s` without drawing it (the sheet music shows instead), its
    // keys struck as when it is drawn, so a switch between the two keeps its place. The seconds
    // since the opening's first note (past its length: the rest before it starts again).
    double run_opening(double now_s);
    // Lets go of every key the loop holds (reported as struck), as when it stops.
    void release_all() { reset(0); }

  private:
    void feed(double t, std::int64_t now_ns);
    void feed_opening(double t);
    void strike(std::uint8_t key, std::uint8_t velocity);
    void feed_song(double t);
    void feed_grades(double t);
    void reset(std::int64_t now_ns);

    SDL_Renderer* renderer_;
    std::unique_ptr<Scene> scene_;
    SDL_Texture* target_ = nullptr;
    int target_w_ = 0;
    int target_h_ = 0;
    KeyboardLayout layout_{KeyRange{48, 76}};
    KeyViewFrame view_; // the whole layout, never moving
    KeyState keys_;
    NoteTrail trail_;
    std::vector<TrailBar> bars_;
    std::array<SongBar, 32> song_bars_{}; // the falling notes of `PreviewLoop::Song`
    std::size_t song_bar_count_ = 0;
    PreviewLoop loop_ = PreviewLoop::Played;
    std::array<GradePopup, 5> grades_{}; // the grade words the loop cycles through
    std::size_t grade_count_ = 0;
    std::array<std::int64_t, 128> last_press_{};
    std::array<std::uint8_t, 128> last_velocity_{};
    double start_ = -1.0;
    double last_t_ = -1.0;
    double last_drawn_ = -1.0;
    const SongOpening* opening_ = nullptr;
    std::vector<SongBar> opening_bars_; // as many as the opening has notes, twice
    std::array<Struck, 64> struck_{};
    std::size_t struck_count_ = 0;
    std::bitset<128> sounding_; // pressed by the loop and not let go yet
};

} // namespace etude::app::ui
