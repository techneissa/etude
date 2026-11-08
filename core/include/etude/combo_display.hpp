// SPDX-License-Identifier: GPL-3.0-or-later
// The on-screen combo (#88): the number ("×" and the digits, with a fire icon beside it) in the
// note area's top-right corner, as plain values with no drawing. `ComboDisplay` is a small state
// machine fed the combo each frame; its `ComboView` says which digits show and how far each has
// eased in or out. Everything is a few floats and fixed arrays: a frame allocates nothing.
//
// The number shows from kComboShowAt on. A change eases the digits that changed (the new one
// slides up into place while it fades in, the old one lifts away while it fades out); a milestone
// adds a short, small pulse. A combo lost (a miss or a wrong press) fades the last number out
// quickly, with no motion. With reduced effects nothing animates: the number just follows the
// combo.
#pragma once

#include "etude/game_feel.hpp"
#include "etude/screen_layout.hpp"

#include <array>
#include <cstdint>

namespace etude {

// The number shows at this combo and above; below it nothing shows.
inline constexpr int kComboShowAt = 10;
// Digits drawn at most; a longer combo shows 99999.
inline constexpr int kComboMaxDigits = 5;
inline constexpr int kComboMaxShown = 99999;

// Seconds.
inline constexpr float kComboEnterS = 0.13f;   // a changed digit eases in and out
inline constexpr float kComboFadeInS = 0.12f;  // the number appearing at kComboShowAt
inline constexpr float kComboFadeOutS = 0.18f; // and going when the combo is lost
inline constexpr float kComboPulseS = 0.26f;   // a milestone's pulse

// A digit's slide, as a share of the digit's height: the new one starts this far below its place,
// the old one leaves this far above it.
inline constexpr float kComboSlide = 0.28f;
// The pulse grows the number by up to this share.
inline constexpr float kComboPulseGrow = 0.14f;

// One digit on its way in or out: `digit` 0..9, or -1 for none; `alpha` 0..1; `dy` the offset in
// digit heights, positive down.
struct ComboGlyph {
    int digit = -1;
    float alpha = 0.0f;
    float dy = 0.0f;
};
// One column of the number: the digit that is there now, and the one it replaces while it leaves.
struct ComboColumn {
    ComboGlyph now;
    ComboGlyph out;
};

struct ComboView {
    // 0: nothing shows (below kComboShowAt, or faded out).
    float alpha = 0.0f;
    // The pulse: the number's scale (1 at rest) and how bright it is (0 at rest, 1 at the peak).
    float scale = 1.0f;
    float glow = 0.0f;
    // Columns used, left to right: columns[0] is the leftmost digit, the last is the ones.
    int columns = 0;
    std::array<ComboColumn, kComboMaxDigits> column{};
    // True under reduced effects: no pulse, no slide, no fade.
    bool reduced = false;

    bool shown() const { return alpha > 0.0f && columns > 0; }
};

class ComboDisplay {
  public:
    // One frame: `dt_s` since the last (clamped), the evaluator's `combo`, whether this frame
    // crossed a milestone (GameFeel::update_combo, SongController::take_combo_milestone), and
    // whether effects are reduced.
    // `epoch` changes when the song does: the number then starts over with no fade.
    void update(double dt_s, int combo, bool milestone, bool reduced, std::uint32_t epoch = 0);
    // Forget everything: nothing shows (a song loaded: the last song's number does not fade over
    // the next).
    void reset();

    const ComboView& view() const { return view_; }

  private:
    void build_view(bool reduced);

    std::uint32_t epoch_ = 0;
    int value_ = 0; // the number on screen, 0 for none
    int prev_ = 0;  // the number it replaces while a change eases, 0 for none
    float enter_ = 1.0f;
    float pulse_ = 1.0f;
    float alpha_ = 0.0f;
    bool lost_ = false; // the combo fell below kComboShowAt and the number is fading out
    ComboView view_;
};

// The number's box in window pixels: `width` x `height` in the top-right corner of `note_area`,
// one margin in from the right edge and below `top_clear` (the count band, and the section band
// hanging from the bar while it shows). `u` is pixels per design point. `keep_out`, when given
// (the octave-shift offer's card), is stepped under when the box would touch it. Empty when the
// box would not fit in the note area: then nothing is drawn rather than something on top of a
// neighbour.
inline constexpr float kComboMarginPt = 12.0f; // from the right edge
inline constexpr float kComboTopPt = 10.0f;    // under whatever is above
inline constexpr float kComboGapPt = 8.0f;     // clear space around a keep-out
// `right_clear` is room kept free along the right edge (the arrows that point to a note off
// screen), in pixels: the box stays that far, and a gap, from the edge when that is more than the
// margin.
RectF place_combo(const RectF& note_area, float top_clear, float u, float width, float height,
                  const RectF& keep_out = {}, float right_clear = 0.0f);

} // namespace etude
