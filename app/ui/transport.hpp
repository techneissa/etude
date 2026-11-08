// SPDX-License-Identifier: GPL-3.0-or-later
// The song's transport in the revealed bar (replay, skip back, play/pause, skip forward, the
// song's sound, the hands, Flow or Wait) and the resume countdown that follows closing the
// settings. Everything reads and acts on a SongPort. The song's sections and the playhead along
// them are the section band's (section_band.hpp).
#pragma once

#include "bar_metrics.hpp"
#include "kit.hpp"
#include "song_port.hpp"

#include "etude/top_indicator.hpp"

namespace etude::app::ui {

// Width of one hand toggle for `cell` wide transport cells: 0.9 of a cell, but never under the
// smallest hit width (44 pt), which the drawn icon does not follow.
float hand_cell(const Kit& k, float cell);
// One of the song's controls on the bar (replay, back, play/pause, forward, the song's sound, a
// hand toggle) in a hit area `hit` big round `centre`, its face `drawn` big. `prominent` fills
// play/pause with the accent; `caption` (may be null) goes under the icon, else a long press names
// the control.
void song_control(Kit& k, SongPort& song, bar_metrics::Ctl c, ImVec2 centre, ImVec2 hit,
                  float drawn, bool live, bool prominent, const char* caption);
// How the Flow/Wait control looks: a chip with the mode's icon and name (Chip), the icon alone in a
// bar cell (Icon, the tight and narrower bar forms), or the icon with the mode's name under it, as the
// Groups bar captions its icons (Captioned).
enum class ModeLook : std::uint8_t { Chip, Icon, Captioned };
// A hand toggle's face as the bar draws it: the hand, its L or R in the corner, the shake of a
// refused tap, dimmed when the hand has no notes; true when it was tapped. `x` is the left edge of a
// `width` by `hit_h` hit area centred on `cy`, `drawn` the face's size. The bar's own toggles and
// the Learn cards' (their `id` differs) are this one widget: left hand on the left, right on the
// right. `hand_toggle_refused` shakes the toggle: the tap was refused (one hand always stays on).
bool hand_toggle_face(Kit& k, const char* id, bool left, float x, float cy, float width,
                      float hit_h, float drawn, bool on, bool available, bool live, bool caption);
void hand_toggle_refused(bool left, double now);
// The Flow/Wait control in a hit area at `pos`, `hit` big (its face `drawn` tall). A tap switches
// the mode (SongPort::set_mode) and says so; a long press names it.
void mode_chip(Kit& k, SongPort& song, ImVec2 pos, ImVec2 hit, float drawn, ModeLook look,
               bool live);
// The skip icon: an open circular arrow (anticlockwise for back, clockwise for forward) with the
// step's seconds in the middle, `size` points across. `key` is the control's id: the number
// morphs when `seconds` changes. `turn` (radians, clockwise) rotates the arrow, not the number.
void skip_icon(const Kit& k, ImDrawList* dl, ImGuiID key, ImVec2 centre, float size, int seconds,
               bool forward, ImVec4 colour, float turn = 0.0f);
// The resume countdown: a small number in a thin ring in the top row where `where` puts it (core
// place_top_indicator; no disc), in the status font's size `font`. Draws nothing when no countdown
// runs or `where` is empty.
void countdown_indicator(Kit& k, const SongView& song, const TopIndicator& where, float font);

} // namespace etude::app::ui
