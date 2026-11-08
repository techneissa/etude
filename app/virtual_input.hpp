// SPDX-License-Identifier: GPL-3.0-or-later
// Turns SDL mouse, touch and keyboard events into the same MidiEvents a MIDI keyboard makes, so
// playing on the screen or the computer keyboard goes through the same key, trail and chord path.
#pragma once

#include "etude/computer_keyboard.hpp"
#include "etude/keyboard.hpp"
#include "etude/pointer_keys.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace etude::app {

// Time stamps. An SDL event's timestamp is the moment the OS delivered it, in SDL's tick clock,
// which is not midi_clock_ns()'s. The two clocks are bridged by the event's age: the stamp is
// `midi_clock_ns()` now minus how long ago (SDL_GetTicksNS() - timestamp) the event happened, so a
// key pressed 4 ms before the frame drained its events is stamped 4 ms before. An age that is
// negative or over a second (a missing or stale timestamp) counts as now. The key-down to present
// time that --latency-log prints starts at this stamp.
std::int64_t event_time_ns(std::uint64_t sdl_timestamp_ns);

class VirtualInput {
  public:
    VirtualInput();

    // The player's keyboard range, for the typing keys' octave limits.
    void set_keyboard(KeyRange keyboard) { typing_.set_keyboard(keyboard); }
    // Where the keys are on screen after the last frame, in output pixels, and the left edge of the
    // settings panel (negative when closed): presses under the panel are the panel's. `px_per_mm`
    // is the display's output pixels per millimetre, which sizes the key-slide margin.
    void set_geometry(const KeyboardLayout& layout, const KeyViewFrame& view, const RectF& key_area,
                      float panel_x, float px_per_mm);

    // Says whether the UI has a visible part under a point (fractions of the window). A finger
    // that lands there is the UI's, not a key's. Without it, Dear ImGui's WantCaptureMouse decides,
    // which is where the last pointer was left and not where this finger is.
    void set_ui_owns_point(std::function<bool(float, float)> owns) { ui_owns_point_ = std::move(owns); }

    // Says whether a card is open whose dismissing tap must not play the key under it (the scale
    // popover): every press is the UI's while it is. Releases still get through.
    void set_ui_blocks_presses(std::function<bool()> blocks) {
        ui_blocks_presses_ = std::move(blocks);
    }

    // Handles one SDL event. `ui_wants_keys` is true while a text field in the UI has the keyboard.
    // Appends the events it makes to `out` and returns true when the event was a playing input
    // (typing key, a press on a key), so the caller leaves it alone. Presses the UI is using
    // (ImGui's WantCaptureMouse / WantCaptureKeyboard) and key presses with Ctrl, Alt or Cmd held
    // are not playing. Releases always get through.
    bool handle(const SDL_Event& event, SDL_Window* window, bool ui_wants_keys,
                std::vector<MidiEvent>& out);

    // Lets go of every note and the pedal: lost focus, quit.
    void release_all(std::vector<MidiEvent>& out);

    const ComputerKeyboard& typing() const { return typing_; }

  private:
    // Under the settings panel, or any press while a card that takes the dismissing tap is open.
    bool under_panel(float x) const {
        return (panel_x_ >= 0.0f && x >= panel_x_) || (ui_blocks_presses_ && ui_blocks_presses_());
    }
    // Window points to output pixels.
    static float pixel_scale(SDL_Window* window);

    ComputerKeyboard typing_;
    PointerKeys pointers_;
    std::optional<PointerGeometry> geometry_;
    float panel_x_ = -1.0f;
    std::function<bool(float, float)> ui_owns_point_;
    std::function<bool()> ui_blocks_presses_;
};

} // namespace etude::app
