// SPDX-License-Identifier: GPL-3.0-or-later
// The frame loop's idle wait on SDL (idle_wait.hpp): an event wakes it, and so do the MIDI and
// microphone events through loop_wake.hpp.
#pragma once

#include "idle_wait.hpp"

#include "etude/effects_governor.hpp"
#include "etude/idle_gate.hpp"
#include "etude/look.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>

#include <cstdint>

namespace etude::app {

// Seconds since the last frame, at most 0.1: a frame that follows a wait must not run an easing to
// its end.
inline double capped_s(std::int64_t ns) {
    return static_cast<double>(ns < 100'000'000 ? ns : 100'000'000) / 1e9;
}

// Whether the frame must be drawn whatever the picture: a song counting in or playing or counting
// down to resume, Learn's Listen lighting its keys, a finger, mouse button or key held, or the motion background.
template <class Songs, class UiT, class Session, class Look, class SceneT>
bool frame_moves(const Songs& songs, const UiT& ui, const Session& session, const Look& look,
                 const SceneT& scene) {
    return etude::song_keeps_drawing(songs.state(), songs.counting_down()) ||
           ui.learn_listening() || ui.fingers_down() > 0 || session.keys.held_count() > 0 ||
           SDL_GetMouseState(nullptr, nullptr) != 0 ||
           (look.effective_background() == etude::BackgroundStyle::Motion &&
            scene.effects_level() < etude::EffectsLevel::NoMotion);
}

// The idle loop on SDL: waits in SDL_WaitEventTimeout, woken by a pushed SDL event.
inline IdleLoop make_idle_loop(bool allowed) {
    return IdleLoop(
        allowed,
        [](std::int64_t timeout_ns) {
            SDL_WaitEventTimeout(nullptr, static_cast<Sint32>((timeout_ns + 999'999) / 1'000'000));
        },
        [] {
            SDL_Event event{};
            event.type = SDL_EVENT_USER;
            SDL_PushEvent(&event);
        });
}

} // namespace etude::app
