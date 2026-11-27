// SPDX-License-Identifier: GPL-3.0-or-later
// The `Kit` every control draws with: the frame's state, the ports, the toast and the (i) bubble.
#pragma once

#include "../port_lists.hpp"
#include "../ui_model.hpp"

#include "etude/scroll_drag.hpp"
#include "etude/slider_drag.hpp"
#include "etude/toast.hpp"

#include <array>
#include <functional>

#include <imgui.h>

#include <cstdint>
#include <string>
#include <string_view>

struct SDL_Renderer;

namespace etude::app {
class SheetPreview;
struct ProfileInfo;
}

namespace etude::app::ui {

class LivePreview;
class MedalAtlas;
class InputPort;
class SongPort;
class SectionsPort;
class UploadPort;
class SourcesPort;
class AboutPort;
class IconPort;
// --- Kit ---------------------------------------------------------------------------------------

// The toast on screen (core ToastState: one at a time, a fixed buffer) and where it points.
struct Toast {
    ToastState state;
    // The Undo of a toast that is not a section edit's (a saved palette replaced or cleared): what
    // its chip does. Its toast's token has `kUndoOwnBit` set (see `Kit::say_undoable`).
    std::function<void()> undo;
    std::uint32_t undo_serial = 0;
    bool anchored = false; // near the control that caused it
    ImVec2 anchor_min{0, 0};
    ImVec2 anchor_max{0, 0};
};

// Whether the open song has sheet music: the host sets it every frame for the view switch.
struct SheetChoice {
    bool song = false;    // a song is open
    bool known = false;   // the answer is in
    bool offered = false; // it has sheet music
    std::string_view reason; // why not, when known and not offered
    bool refused() const { return song && known && !offered; }
};

struct Kit {
    float u = 1.0f;            // ImGui points per design point
    ImFont* regular = nullptr; // Atkinson Hyperlegible with Lucide merged in
    ImFont* bold = nullptr;
    ImFont* music = nullptr;   // the music font (Noto Music): the notes and the clef of the title medals' emblems
    float dt = 0.016f;
    double now = 0.0;        // seconds
    std::int64_t now_ns = 0; // the same, for SongPort calls
    bool touch = false;      // the last pointer event came from a finger: no hover states
    float top_row_bottom =
        0.0f;                // where the top row (the status text, the gear) ends; the host sets it
    Toast toast;             // transient feedback (see `say`)
    // What the toast keeps clear of and where it may go (core place_toast). The host clears it at the
    // start of every frame; the sheet, the editor and the tempo popover add their rects as they
    // draw, and the host fills in the rest before the toast is drawn.
    ToastRegions toast_regions;
    // A warning found no room for its words and shows as the icon alone. The host then hides a bar
    // that is shown but not pinned (as it does after its timeout), which frees the status row. Set
    // when the toast is drawn, read by the next frame.
    bool toast_needs_room = false;
    // Where the Undo chip of the toast is, in ImGui points as the last frame drew it (empty when
    // there is none): a finger that lands there is the chip's, and a screen reading raw finger
    // events (the section editor) leaves it out.
    RectF toast_chip;
    // Keeps the toast clear of `r` (core ToastRegions::add_avoid): a list that cannot take it fails
    // placement closed, and asserts here.
    void toast_avoid(const RectF& r);
    SheetChoice sheet;
    // The section band is drawn in its slim form (a short or very dense window with the bar
    // pinned, see core BarFit); the host sets it every frame.
    bool slim_band = false;
    // The band the icon controls drawn now may fill, top and bottom in points: the card or pill
    // they sit on (see `FaceBand`). Empty (`face_top >= face_bottom`): a control's own hit area.
    float face_top = 0.0f;
    float face_bottom = 0.0f;

    // A label for the icon-only control under a long press, drawn on top by `finish_frame`.
    std::string tip;
    ImVec2 tip_min{0, 0};
    ImVec2 tip_max{0, 0};
    // The open (i) bubble.
    ImGuiID info_id = 0;
    std::string info_text;
    ImVec2 info_anchor{0, 0};
    bool info_seen = false;
    bool info_in_settings = false; // opened from a Settings page: it keeps its size (the popup rule)
    bool in_settings = false;      // the Settings sheet is drawing its pages
    // A destructive action waiting for its second tap.
    ImGuiID confirm_id = 0;
    double confirm_until = 0.0;

    bool reduced_motion = false; // LookSettings::reduced_effects: no UI animation either

    // The song and its sections, for pages and the section band (the host sets them every frame).
    SongPort* song = nullptr;
    SectionsPort* sections = nullptr;
    // The look's hand colours, which the hand toggles light up in.
    ImVec4 hand_left{1.0f, 0.663f, 0.302f, 1.0f};
    ImVec4 hand_right{0.353f, 0.667f, 1.0f, 1.0f};
    // The section band asked for the full section editor; the host opens it.
    bool open_editor = false;
    // Settings > Input's "Run the device check" was tapped; the host takes it as the card's answer.
    bool run_device_check = false;

    // The live look preview (pages draw it; the host owns it) and what it needs to render.
    LivePreview* preview = nullptr;
    // One preview for each saved config's card (the Configs page), so the three can show at once:
    // a preview renders into its own target.
    std::array<LivePreview*, kUserPresetSlots> config_previews{};
    // The household, for the Configs page's copy row (null when the app has none).
    const ProfileInfo* profiles = nullptr;
    // The medals' texture atlas (the host owns it; null in a test that draws no medal).
    MedalAtlas* medals = nullptr;
    // The library's own, for a song's opening bars (its Scene is made the first time it draws).
    LivePreview* library_preview = nullptr;
    // And its sheet music (its worker starts the first time it is shown).
    SheetPreview* library_sheet = nullptr;
    // The microphone, for the Input pages; the host sets it every frame.
    InputPort* input = nullptr;
    // The MIDI ports the watcher thread listed, for the Input page's port rows; null: only Auto.
    const PortLists* midi_ports = nullptr;
    // The phone upload server, for the Add songs page; null when the app has none.
    UploadPort* upload = nullptr;
    // The network folder and the USB stick, for the same page; null when the app has none.
    SourcesPort* sources = nullptr;
    // The update check and the links, for the About page; null when the app has none.
    AboutPort* about = nullptr;
    // The best title on the device, for the About page's icon colours; null: only the default.
    IconPort* icons = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool high_contrast = false; // the look's High contrast: the grade colours lift with it
    float fb_scale = 1.0f;     // output pixels per ImGui point
    bool short_screen = false; // under 600 points tall: pages trim what they can

    // Drag to scroll. `slider_active`: a slider is following the touch this frame, so the touch is
    // the slider's, not the sheet's.
    bool slider_active = false;
    // A settings slider's track is held by a finger or the mouse this frame (a press that has not
    // moved yet counts). The frame loop reads it through Ui::slider_held().
    bool slider_held = false;
    SliderDrag slider;
    ScrollDrag scroll;         // the touch that scrolls the sheet, and its coasting
    ImGuiID scroll_window = 0; // the child window `scroll` is following, 0 for none
    int scroll_frame = -1;     // the last frame that window called drag_scroll

    float s(float v) const { return v * u; }

    // Shows `text` as the toast, replacing the one on screen, near the control just drawn (the last
    // item) when called right after it, else centred under the bar. Empty text does nothing.
    // `warning` keeps it up longer (something the player must not miss). The one way to give
    // transient feedback; the status line keeps only lasting state.
    void say(std::string_view text, bool warning = false);
    void say_free(std::string_view text, bool warning = false);
    // The words of a section edit just made (`sections->take_message()`), as a toast near the
    // control just drawn. `before` is `sections->edit_serial()` read before the edit: when it moved,
    // the edit went through and the toast carries an Undo that takes it back, for as long as the
    // toast shows and nothing else has been edited or undone since; otherwise (a refused edit) it
    // is a plain message.
    void say_edit(std::uint32_t before);
    // Like `say`, with an Undo chip that runs `undo` (once) while the toast shows; the next toast
    // takes the offer away. For a change that went through at once and is not a section edit.
    void say_undoable(std::string_view text, std::function<void()> undo);
    // Like `say_free`, but from outside a frame: it starts on the next one. `name` is the part of
    // the text that is a song's title (see `TextSpan`).
    void say_later(std::string_view text, bool warning = false, TextSpan name = {},
                   SongLines lines = {});
    // Starts a toast queued by `say_later`.
    void start_pending_toast();
};

} // namespace etude::app::ui
