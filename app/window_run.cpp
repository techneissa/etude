// SPDX-License-Identifier: GPL-3.0-or-later
// The window run: opens the window, runs the frame loop in a WindowState and closes the window.
#include "window_run.hpp"
#include "dev_jitter.hpp"
#include "gfx.hpp"
#include "touch_device.hpp"
#include "watchdog.hpp"
#include "window_icon.hpp"
#include "window_state.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace etude::app {
namespace {

// Frame period for pacing when the driver refuses vsync, from the display's refresh rate.
std::int64_t fallback_frame_ns(SDL_Window* window) {
    float hz = 60.0f;
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        mode != nullptr && mode->refresh_rate > 1.0f) {
        hz = mode->refresh_rate;
    }
    return static_cast<std::int64_t>(1e9 / static_cast<double>(hz));
}

} // namespace

// Opens the window and its renderer and paces the frames by the display: false when SDL or the
// window cannot be made.
bool open_window(WindowOptions& opt, WindowHandles& handles) {
    const WindowSize size = opt.size;
    if (opt.headless && !opt.audit.active()) {
        opt.script.quit_after_last_shot();
    }
    if (opt.headless) {
        // No Dock icon, no focus: a background app on macOS.
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    etude::RendererPath path = etude::RendererPath::Basic;
    // An exact --window size also turns off the high pixel density, so the output has exactly
    // that many pixels, like a small panel.
    const bool exact = size.width > 0;
    if (!gfx::create_window_and_renderer(
            "Etude", exact ? size.width : 1280, exact ? size.height : 720,
            SDL_WINDOW_RESIZABLE | (exact ? 0 : SDL_WINDOW_HIGH_PIXEL_DENSITY) |
                (opt.headless ? SDL_WINDOW_HIDDEN : 0),
            renderer_for(opt), &window, &renderer, &path)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    set_window_icon(window);
    etude::app::log_touch_devices();
    // The layout rules hold from 800x480; a window asked for smaller (--window) may go that small.
    SDL_SetWindowMinimumSize(window, exact ? std::min(size.width, 800) : 800,
                             exact ? std::min(size.height, 480) : 480);
    // Headless presents nothing, so the frames are paced by the clock at the display's rate, as
    // when vsync is refused.
    const bool vsync = !opt.headless && gfx::set_vsync(renderer, 1);
    const std::int64_t frame_ns = fallback_frame_ns(window);
    if (!vsync && !opt.headless) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "vsync refused (%s), pacing frames at %.1f Hz",
                    SDL_GetError(), 1e9 / static_cast<double>(frame_ns));
    }
    if (opt.latency_log) {
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        std::printf("renderer %s, vsync %s, display %.1f Hz\n", gfx::renderer_name(renderer),
                    vsync ? "on" : "off", 1e9 / static_cast<double>(frame_ns));
    }
    handles = {window, renderer, path, vsync, frame_ns};
    return true;
}

// The frame loop: events, then the frame, until something asks to quit.
void WindowState::run_frames() {
    // While the OS runs its own loop for a window drag (macOS live resize), SDL_PollEvent is not
    // called and the window would show a stretched old frame.
    // The event watch is called inside that loop, so it redraws from there: layout, keys,
    // notes, text and controls follow the new size in the same frame.
    hook.draw = [this] {
        if (running) {
            finish_frame(begin_frame(), true);
        }
    };
    SDL_AddEventWatch(on_window_event_watch, &hook);

    // The unit's watchdog (a Pi under systemd; nothing anywhere else): answered by this loop and by
    // nothing else, so a frame that hangs stops the pings.
    Watchdog watchdog;
    while (running) {
        if (idle.wait_if_idle(midi_clock_ns())) {
            frame_log_end_ns = etude::app::frame_clock_ns(); // the wait is not the frame's
        }
        watchdog.ping(midi_clock_ns());
        if (!vsync) {
            // Sleep before draining, so the events read are as fresh as possible.
            const std::int64_t now = midi_clock_ns();
            if (next_frame_ns > now) {
                SDL_DelayPrecise(static_cast<Uint64>(next_frame_ns - now));
                next_frame_ns += frame_ns;
            } else {
                next_frame_ns = now + frame_ns; // fell behind: start the cadence again
            }
        }
        hook.state.in_frame = true;
        if (log_frames) {
            frame_log.start(frame_log_end_ns);
        }
        std::int64_t work_start_ns = begin_frame();
        log_mark(etude::app::FramePhase::Wait, false);
        handle_events();
        log_mark(etude::app::FramePhase::Events, false);
        if (running && recovery.pending()) {
            // A lost device is served between the events and the drawing, never inside a frame.
            if (!recover_renderer()) {
                break;
            }
            work_start_ns = begin_frame();
        }
        finish_frame(work_start_ns, false);
        hook.state.in_frame = false;
        if (opt.dev_frame_jitter_ms > 0) {
            SDL_Delay(static_cast<Uint32>(etude::app::dev_jitter_ms(opt.dev_frame_jitter_ms)));
        }
    }
    SDL_RemoveEventWatch(on_window_event_watch, &hook);
}

int run_window(WindowOptions& opt) {
    WindowHandles handles;
    if (!open_window(opt, handles)) {
        return 1;
    }
    SDL_Renderer* renderer = nullptr;
    bool renderer_gone = false;
    {
        WindowState state(opt, handles);
        state.run_frames();
        state.shutdown();
        renderer = state.renderer; // the first one, or the one that replaced it
        renderer_gone = state.renderer_gone;
    }
    if (renderer != nullptr) {
        gfx::destroy_renderer(renderer);
    }
    SDL_DestroyWindow(handles.window);
    SDL_Quit();
    if (opt.script.shot_failed() || renderer_gone) {
        return 1;
    }
    return 0;
}

} // namespace etude::app
