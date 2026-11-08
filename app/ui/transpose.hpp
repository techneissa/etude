// SPDX-License-Identifier: GPL-3.0-or-later
// The transposition control and the octave-shift offer.
//
//   the card    opened from the transposition chip in the tempo popover's readout (the number
//               beside the tempo), under the bar like it: a
//               stepper (minus, the value, plus; one semitone a tap), the three whole-octave moves
//               (-1 oct, 0, +1 oct) each with what it would leave off the keyboard ("all fit",
//               "14 outside"), and a row with Keys (the fit strip) and Reset. A tap on an octave
//               move applies it. The value is the song's, kept with it.
//   the offer   a compact card at the top of the note area when the song has notes outside the
//               keyboard that an octave move would bring in, or the player's own keyboard seems
//               to be sending its notes an octave off: the move proposed with what it leaves
//               outside, the strip on a tap, and three answers: Accept, Dismiss, Don't ask again
//               for this song (core shift_offer.hpp decides when it shows).
//   the strip   the keyboard as a row of semitones with the song's range under it, one bar for
//               the shift in force and one for each octave move, the part of a bar past the keys
//               in the warning colour. Shown on a tap, never by itself.
#pragma once

#include "bar.hpp"

namespace etude::app::ui {

class TransposeMenu {
  public:
    void open() { open_ = true; }
    bool is_open() const { return open_; }
    void close() { open_ = false; }

    // The card under the bar from `below_y` down to the keyboard, its middle at `anchor_x`. Draws
    // nothing while shut.
    void popover(Kit& k, SongPort& song, const Frame& f, float below_y, float anchor_x);

  private:
    bool open_ = false;
    float open_p_ = 0.0f;
    OutsideTap outside_; // the tap outside that closes it, never the release that opened it
    bool strip_ = false;
};

class ShiftOfferCard {
  public:
    // The offer card from `below_y` down. `allowed` false (a panel, the bar's popovers or the
    // results card is up) hides it without forgetting it.
    void draw(Kit& k, SongPort& song, const Frame& f, float below_y, bool allowed);
    bool shown() const { return shown_p_ > 0.0f; }
    // Where the card was drawn in the last frame (points), empty when it is not shown: the combo
    // number keeps clear of it.
    const RectF& rect() const { return rect_; }

  private:
    RectF rect_;
    float shown_p_ = 0.0f;
    bool strip_ = false;
    // The offer as it was last asked for, kept while the card fades out after an answer.
    int target_ = 0;
    int from_ = 0;
    bool from_keys_ = false;
};

} // namespace etude::app::ui
