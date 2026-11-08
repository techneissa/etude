// SPDX-License-Identifier: GPL-3.0-or-later
// The control bar's three styles (Settings > View > Bar style). The host (host.cpp) owns the top
// row, the gear, the status text, when the bar shows and the section band under it
// (section_band.hpp); a style only draws the bar's buttons in the room the layout gives it
// (etude::split_top_row).
//
// All three hold the same controls in the same order and give them up in the same order as the
// room shrinks (bar_metrics::lineup, drawn by draw_lineup in bar_lineup.hpp); they differ in look:
//   Strip   a full-width translucent strip, the controls against the gear.
//   Groups  floating cards centred in the window, a caption under every control naming it.
//   Dock    a dock centred in the window, play the one filled button.
#pragma once

#include "kit.hpp"
#include "pages.hpp"
#include "song_port.hpp"

#include "etude/screen_layout.hpp"
#include "etude/session_state.hpp"

#include <imgui.h>

#include <memory>

namespace etude::app::ui {

struct Frame {
    ImVec2 display{0, 0}; // ImGui points
    ImVec2 row_min{0, 0}; // the top row
    ImVec2 row_max{0, 0};
    ImVec2 bar_min{0, 0}; // the part of the top row the bar may use (split_top_row)
    ImVec2 bar_max{0, 0};
    float top = 0.0f; // overlays start here, below the top row
    float band_bottom = 0.0f; // the bottom of the section band (and the lane) while it shows, else
                              // of the top row: the bar's popovers open below it
    float keyboard_top = 0.0f;
    RectF side_button; // the sheet-music button at the notes' top right (points; empty for none):
                       // the bars' cards keep clear of it
    float reveal = 0.0f;  // the bar's buttons, eased 0..1
    bool live = false;    // the bar is far enough in to take taps
    // The form the bar is drawn in (etude::fit_bar_form): the richest the room allows.
    BarForm form = BarForm::Full;
    bool compact = false; // any form but the full one
    // The pin is on the bar. A bar too narrow for its minimal form with the pin lets it go
    // (whole; the switch stays in Settings > View), see BarForm.
    bool pin = true;
};

// A request from a bar to the host: open the settings on a page (a look shortcut, or a long press
// on the metronome).
struct OpenRequest {
    bool wanted = false;
    Page page = Page::Notes;
    void ask(Page p) {
        wanted = true;
        page = p;
    }
    bool take(Page* p) {
        if (!wanted) {
            return false;
        }
        wanted = false;
        *p = page;
        return true;
    }
};

class BarLook {
  public:
    virtual ~BarLook() = default;
    // The bar's width in points in `form` (every form after the first has fewer buttons: see
    // BarForm), the tempo chip and the metronome toggle included, for split_top_row; `pin` false
    // leaves the pin out. The numbers are bar_metrics.hpp's.
    virtual float width(const Kit& k, const SongView& song, BarForm form, bool pin = true) const = 0;
    // True when the bar should be centred in the window rather than set against the gear.
    virtual bool centred() const { return false; }
    // The bar's buttons, while `f.reveal` > 0, inside a window covering the top row.
    virtual void draw(Kit& k, UiModel& model, SongPort& song, const Frame& f) = 0;
    // True while something the bar opened is showing: the bar stays.
    virtual bool holds_bar() const { return false; }
    // Any card the bar's buttons opened closes (the settings opened, or the style changed).
    virtual void close_popovers() {}
    // A bar button asked for the settings to open on `*p` (a long press in the groups style).
    virtual bool wants_open(Page* p) {
        (void)p;
        return false;
    }
};

std::unique_ptr<BarLook> make_strip_bar();
std::unique_ptr<BarLook> make_groups_bar();
std::unique_ptr<BarLook> make_dock_bar();

// Helpers shared by the bars and the sheet.

// The pin at the bar's end ("Keep the bar shown"): pinned lights up and the icon morphs between
// the pin and its slashed twin. `caption` (may be null) goes under the icon. A tap flips
// `model.app.bar_pinned` and says so in the toast. Returns true on a tap.
bool pin_button(Kit& k, UiModel& model, ImVec2 centre, ImVec2 hit, float drawn, bool live,
                const char* caption = nullptr);
// The faint handle at the top edge while the bar is hidden, drawn on the foreground (it takes no
// touch: the whole top row does). `fade` is 1 when the bar is fully hidden, 0 when it is in.
void reveal_handle(const Kit& k, ImVec2 top_centre, float fade);

// Begins a borderless, backgroundless window at `pos`, `size`; always paired with ImGui::End().
bool begin_layer(const char* id, ImVec2 pos, ImVec2 size, bool scroll = false);
// The same for a scrim, the dimmed screen under an overlay: a click on it never brings it to the
// front, so it stays under the overlay it dims and never takes the overlay's clicks.
bool begin_scrim(const char* id, ImVec2 pos, ImVec2 size);
// Makes the window `id` and its children ignore the pointer from the next frame on (ImGui picks the
// window under the pointer at the start of a frame, from the flags the windows ended the last one
// with). Called each frame an overlay slides away, so a closing overlay takes no input from the
// frame its close starts and a tap on what it covers works at once.
void block_input(const char* id);
// A tap that began outside `min`..`max` and below the top row and was released without movement:
// an overlay should close.
bool tapped_outside(OutsideTap& guard, bool shown, const Frame& f, ImVec2 min, ImVec2 max);

} // namespace etude::app::ui
