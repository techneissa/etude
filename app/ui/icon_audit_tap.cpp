// SPDX-License-Identifier: GPL-3.0-or-later
// The icon audit's scripted taps: a scenario's `--audit-tap` names, each tapped in turn once the UI
// has settled after the one before.
#include "icon_audit.hpp"

#include <cstddef>
#include <string_view>

namespace etude::app {

// The scenario's next tap, once the frames since the last action have passed: the first button
// whose name holds the wanted one gets a click (a finger's, the right button's or the mouse's),
// then the pointer leaves so nothing is drawn hovered.
void IconAudit::tap_next(SDL_Window* window, long frame) {
    if (phase_ != Phase::Settling || next_tap_ >= taps_.size() ||
        frame < last_action_frame_ + kTapGapFrames) {
        return;
    }
    // "touch:NAME" taps with a finger: SDL's mouse made from a touch, which Dear ImGui reads as
    // a touch screen (the library's fields bring up their on-screen keyboard only for one).
    // "right:NAME" clicks with the right button, which opens what a long press opens (a hold menu).
    constexpr std::string_view kTouch = "touch:";
    constexpr std::string_view kRight = "right:";
    std::string_view want = taps_[next_tap_];
    const bool touch = want.starts_with(kTouch);
    if (touch) {
        want.remove_prefix(kTouch.size());
    }
    const bool right = want.starts_with(kRight);
    if (right) {
        want.remove_prefix(kRight.size());
    }
    const Uint8 button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
    const SDL_MouseID mouse_id = touch ? SDL_TOUCH_MOUSEID : 0;
    // A button named exactly so is the one meant; only when none is, a name that holds it ("##card"
    // is not "##card-play", the Play button of a card whose own tap area is scrolled out of view).
    const Seen* found = nullptr;
    for (const Seen& s : seen_) {
        if (s.name == want) {
            found = &s;
            break;
        }
    }
    for (const Seen& s : seen_) {
        if (found == nullptr && s.name.find(want) == std::string::npos) {
            continue;
        }
        if (found != nullptr && &s != found) {
            continue;
        }
        const float x = (s.min.x + s.max.x) * 0.5f;
        const float y = (s.min.y + s.max.y) * 0.5f;
        const SDL_WindowID id = SDL_GetWindowID(window);
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = id;
        e.motion.which = mouse_id;
        e.motion.x = x;
        e.motion.y = y;
        SDL_PushEvent(&e);
        for (const bool down : {true, false}) {
            SDL_zero(e);
            e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.windowID = id;
            e.button.which = mouse_id;
            e.button.button = button;
            e.button.down = down;
            e.button.clicks = 1;
            e.button.x = x;
            e.button.y = y;
            SDL_PushEvent(&e);
        }
        // Then the pointer leaves, so no button is drawn hovered.
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = id;
        e.motion.which = mouse_id;
        e.motion.x = 1.0f;
        e.motion.y = 1.0f;
        SDL_PushEvent(&e);
        ++next_tap_;
        last_action_frame_ = frame;
        last_action_ms_ = SDL_GetTicks();
        return;
    }
    // Not on screen (yet): try again next frame.
}

} // namespace etude::app
