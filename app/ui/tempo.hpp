// SPDX-License-Identifier: GPL-3.0-or-later
// The tempo chip, the metronome toggle and what hangs off them, shared by the three bar styles:
//
//   the chip      "(note) 84": the tempo at the playhead times the speed, with the speed in percent
//                 beside it when it is not 100. A tap opens the popover, a long press puts the
//                 speed back to 100%.
//   the popover   a large slider for the speed (25..200%), minus and plus in 5% steps, presets
//                 (50, 75, 100%) and "set (note) =" to drag or type a target tempo, the count-in
//                 (off, 1 or 2 bars) and, where the bar has no room for its own, the metronome.
//   the toggle    the metronome on the bar; a long press opens its settings page.
//   transpose     a chip in the readout, between the tempo and the speed, shows the song's
//                 transposition and opens its card in the popover's place (transpose.hpp).
//
// Where the three of them fit is decided by core (etude::fit_bar_form); this file only draws.
#pragma once

#include "bar.hpp"
#include "osk.hpp"
#include "pages.hpp"
#include "transpose.hpp"

#include "etude/count_band.hpp"
#include "etude/top_indicator.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace etude::app::ui {

// How the chip shows the speed: a badge beside the number (Full), not at all (Narrow), or with
// "Tempo" under it as the Groups bar captions its controls, in the accent while the speed is not
// 100% (Captioned).
enum class ChipLook : std::uint8_t { Full, Narrow, Captioned };

class TempoControls {
  public:
    // The chip's width in points: the note (dotted in 6/8), the number and, for Full, a slot for
    // the speed badge. The same numbers as bar_metrics::chip_width.
    static float chip_width(const Kit& k, ChipLook look, bool dotted);

    // The chip at `pos`, `hit` big (the hit area; the face is `drawn_h` tall). `live` false: drawn
    // but not pressable. Remembers its middle as the popover's anchor.
    void chip(Kit& k, SongPort& song, ImVec2 pos, ImVec2 hit, float drawn_h, ChipLook look,
              bool live);

    // The popover under the bar, from `below_y` down to the keyboard; `metronome_row` puts the
    // metronome switch in it (the bar has no toggle of its own). Draws nothing while shut.
    void popover(Kit& k, UiModel& model, SongPort& song, const Frame& f, float below_y,
                 bool metronome_row);

    // The popover or the transposition card it opens is up.
    bool open() const { return open_ || transpose_.is_open(); }
    void close() {
        open_ = false;
        bpm_focus_ = false;
        osk::set_field_open(osk::Owner::Tempo, false);
        transpose_.close();
    }
    // A long press on the metronome toggle asked for its settings page.
    bool wants_open(Page* page);

  private:
    bool open_ = false;
    float open_p_ = 0.0f;
    OutsideTap outside_; // the tap outside that closes it, never the release that opened it
    float anchor_x_ = 0.0f;
    float card_h_ = 0.0f;
    float column_h_[2] = {0.0f, 0.0f}; // each column's height last frame, whole pixels (0: not known)
    bool bpm_mode_ = false;   // the slider and the field speak of the tempo, not the percent
    // The typed tempo: its field takes the computer keyboard once focused, and a finger's tap on
    // it raises the number pad. The digits typed so far replace the tempo shown, which is the
    // field's hint until then.
    std::string bpm_text_;
    bool bpm_focus_ = false;
    bool bpm_keys_ = false;  // the number pad is wanted
    int bpm_reveal_ = 0;     // frames left to scroll the card down to the field the "=" mode opened
    double bpm_hint_until_ =
        0.0;                 // the range is said again (accent) until then: Done refused the number
    bool bpm_apply_ = false; // Enter or the pad's Done: set the tempo typed
    osk::Keyboard bpm_pad_;
    Box bpm_pad_box_; // where the pad is drawn this frame (empty while it is down)
    bool want_settings_ = false;
    TransposeMenu transpose_; // opened by the readout's chip, drawn at the same anchor
};

// The metronome's button: an icon that lights up while the metronome is on. A tap switches it
// (and says so); a long press asks for its settings (`*long_press` set). Returns true on a tap.
bool metronome_button(Kit& k, UiModel& model, ImVec2 centre, ImVec2 hit, float drawn, bool live,
                      const char* caption, bool* long_press);

// The metronome's own settings, in a column of the Practice page: volume, sound, accent,
// subdivision and beat pulse. Its on-off switch and the count-in are not here: they live on the bar
// and in the tempo popover.
void metronome_rows(Kit& k, UiModel& model);

// The count-in's beat dots and number, small, in the top row where `where` puts them (core
// place_top_indicator; no disc, nothing over the notes), in the status font's size `font`. Draws
// nothing when no count-in runs or `where` is empty.
void count_in_indicator(Kit& k, const SongView& song, const TopIndicator& where, float font);
// A count-in (its beat, large, with a dot per beat of the bar under it) or the resume countdown
// (its seconds, large) in the count band over the falling notes (`band` in window pixels, `shown`
// 0..1 of the way in; `scale` turns pixels into points). Each beat or second pulses as it comes.
void count_in_band(Kit& k, const SongView& song, const CountBand& band, float shown, float scale);
// The metronome's pulse on the hit line, in `min`..`max` (the note area). Draws nothing when none
// runs.
void beat_pulse_overlay(Kit& k, const SongView& song, ImVec2 min, ImVec2 max);

} // namespace etude::app::ui
