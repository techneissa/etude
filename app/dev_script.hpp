// SPDX-License-Identifier: GPL-3.0-or-later
// Developer-only scripted input for checking the UI without a person at the screen: taps, drags,
// typed text, PNG screenshots of the rendered frame and an exit, each on a given frame number.
// Events are pushed into SDL's queue, so the UI sees them exactly like a touch. Coordinates are
// window points. Nothing here runs unless one of the options is given.
//
//   --tap FRAME:X,Y            press and release at X,Y (three frames)
//   --drag FRAME:X,Y:X2,Y2     press at X,Y, move to X2,Y2 over eight frames, release
//   --move FRAME:X,Y           move the mouse to X,Y without pressing (a hover)
//   --type FRAME:TEXT          type TEXT into the focused text field
//   --shot FRAME:PATH          save the frame as a PNG
//   --shots FROM:TO:DIR        save every frame from FROM to TO as DIR/f0000.png, f0001.png ...
//   --key FRAME:NAME[:N]       press the key with this SDL name (A, Semicolon, Tab, ...) and
//                              release it N frames later (default 3)
//   --press FRAME:X,Y[:N]      press the left mouse button at X,Y and release it N frames later
//   --touch FRAME:X,Y          a finger taps X,Y (finger events; the first finger also sends the
//                              touch-made mouse events SDL sends on a touch screen, which is
//                              how the settings sheet and the bar see it)
//   --fdrag FRAME:X,Y:X2,Y2[:N]
//                              a finger drags from X,Y to X2,Y2 over eight frames and lifts N
//                              frames later (default 1; a large N keeps it down for a shot)
//   --fhold FRAME:X,Y:N        a finger rests at X,Y for N frames (a long press needs a few
//                              hundred: frames run about a millisecond apart)
//   --fholddrag FRAME:X,Y:X2,Y2[:N]
//                              a finger rests at X,Y for N frames (a long press), then drags to
//                              X2,Y2 over eight frames and lifts
//   --rdrag FRAME:X,Y:X2,Y2[:N]
//                              the mouse's right button: pressed at X,Y, held N frames, dragged to
//                              X2,Y2 over eight frames and released
//   --pinch FRAME:X1,Y1:X2,Y2:X3,Y3:X4,Y4[:N]
//                              two fingers move from (X1,Y1) and (X2,Y2) to (X3,Y3) and (X4,Y4)
//                              over eight frames and lift N frames later (default 1)
//   --device-check FRAME      open the device check card (as Settings > Input does)
//   --fake-keyboard FRAME:NAME  a MIDI keyboard called NAME is plugged in (its profile is the one
//                              the name matches, as for a real port)
//   --fake-unplug FRAME:NAME   it is unplugged
//   --fake-note FRAME:NAME:PITCH[:N]  it plays PITCH (key down), released N frames later (10)
//   --simulate-device-lost FRAME[:to=PATH][:refuse=PATH[,PATH]][:present]
//                              the render device is lost on FRAME (SDL's event; with `present`, the
//                              next presents fail instead). The renderer is made again on the same
//                              window: `to` names the path to try first (basic, gpu or gl),
//                              `refuse` the paths that pretend they cannot be made, to see the
//                              fall back to basic
//   --resize FRAME:WxH         resize the window to W x H points
//   --info FRAME               print the window's position, size and pixel size
//   --quit-frame FRAME         exit after this frame
#pragma once

#include "dev_keyboard_event.hpp"

#include "etude/renderer_path.hpp"

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_touch.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace etude::app {

// The touch device id of scripted fingers. The app treats it as a touch screen, so a scripted
// finger plays the on-screen keys as a real one does.
inline constexpr SDL_TouchID kScriptedTouchId = 0x7c0de5c0ULL;

// A pretend device loss (--simulate-device-lost): the path to make first, the paths that cannot be
// made (etude::renderer_path_bit), and whether it shows as presents that fail instead of an event.
struct DeviceLossSim {
    std::optional<etude::RendererPath> to;
    unsigned refuse = 0;
    bool by_present = false;
};

class DevScript {
  public:
    // Takes one of the options above and its value; false if it is not one of them or is badly
    // written.
    bool parse(const char* option, const char* value);
    bool active() const { return !steps_.empty() || quit_frame_ >= 0; }

    // Pushes the events due on `frame`. Call before polling events.
    void before_events(SDL_Window* window, long frame);
    // The pretend keyboards' events that fell due in `before_events`, handed over once.
    std::vector<DevKeyboardEvent> take_keyboard_events() { return std::exchange(due_keyboard_, {}); }
    // The device loss that fell due in `before_events` (its event is already queued), handed over once.
    std::optional<DeviceLossSim> take_device_loss() { return std::exchange(due_loss_, std::nullopt); }
    // Saves screenshots due on `frame`; call after drawing, before presenting. True when the
    // script is over and the app should exit.
    bool after_draw(SDL_Renderer* renderer, long frame);
    // With no --quit-frame, the script is over on the frame of its last --shot. For a headless
    // run, which has no window to close by hand.
    void quit_after_last_shot() { quit_after_shots_ = true; }
    // A --shot could not be read back or written.
    bool shot_failed() const { return shot_failed_; }

  private:
    struct Step {
        enum class Kind {
            Move,
            Down,
            Up,
            Text,
            Shot,
            Resize,
            Info,
            KeyDown,
            KeyUp,
            FingerDown,
            FingerMove,
            FingerUp,
            Keyboard, // a pretend keyboard's doing: see below
            DeviceLost // `finger` is the place of its DeviceLossSim in `losses_`
        } kind = Kind::Move;
        long frame = 0;
        float x = 0.0f;
        float y = 0.0f;
        std::string text; // typed text or the PNG path
        int finger = 1;   // for the Finger kinds; Down and Up: kRightButton for the right button
        // Keyboard: `finger` is the DevKeyboardEvent::Kind, `text` the keyboard's name, `x` the pitch.
    };
    static constexpr int kRightButton = 3;
    // A Resize step's width that asks for the window maximised, full screen or restored.
    static constexpr float kResizeMaximise = -1.0f;
    static constexpr float kResizeFullscreen = -2.0f;
    static constexpr float kResizeRestore = -3.0f;
    bool parse_keyboard(const char* option, long frame, const char* rest);
    bool parse_resize(long frame, const char* rest);
    bool parse_device_lost(long frame, const char* rest);
    void push_finger(const Step& s, SDL_Window* window);
    bool parse_shots(const char* option, long frame, const char* rest);
    void finger_path(long frame, std::initializer_list<std::pair<float, float>> from,
                     std::initializer_list<std::pair<float, float>> to, int moves, long hold);
    std::vector<Step> steps_;
    std::vector<DevKeyboardEvent> due_keyboard_;
    std::vector<DeviceLossSim> losses_;
    std::optional<DeviceLossSim> due_loss_;
    long quit_frame_ = -1;
    bool quit_after_shots_ = false;
    bool shot_failed_ = false;
};

} // namespace etude::app
