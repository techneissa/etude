// SPDX-License-Identifier: GPL-3.0-or-later
// A renderer lost after start-up: SDL reports the device lost or reset, or the present keeps
// failing (core RendererRecovery decides when). The loop serves it between the events and the
// drawing: play pauses, every texture is let go while the old renderer is alive, the renderer is
// made again on the same window (the path in use, else basic), and the textures and atlases are
// made again by the scene, the sheet view and the UI. Nothing is drawn with a texture of the old
// renderer: the owners that keep textures across the swap compare gfx::epoch().
#include "frame_log.hpp"
#include "gfx.hpp"
#include "window_state.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <string>

namespace etude::app {

// SDL's three renderer events; a pretend loss of the developer's (take_simulated_loss) arrives as
// the first of them, and is told apart so the policy does not take it for the echo of a recovery.
void WindowState::note_render_event(const SDL_Event& event) {
    LossCause cause = LossCause::DeviceLost;
    switch (event.type) {
    case SDL_EVENT_RENDER_DEVICE_LOST:
        break;
    case SDL_EVENT_RENDER_DEVICE_RESET:
        cause = LossCause::DeviceReset;
        break;
    case SDL_EVENT_RENDER_TARGETS_RESET:
        cause = LossCause::TargetsReset;
        break;
    default:
        return;
    }
    recovery.note_loss(sim_loss ? LossCause::Simulated : cause, frame_index);
}

// --device-lost: the options of the loss the script made due this frame.
void WindowState::take_simulated_loss() {
    const std::optional<DeviceLossSim> loss = opt.script.take_device_loss();
    if (!loss) {
        return;
    }
    gfx::refuse_paths(loss->refuse);
    sim_target = loss->to;
    if (loss->by_present) {
        sim_present_failures = etude::RendererRecovery::kPresentFailures;
    } else {
        sim_loss = true; // the script pushed the event; the poll after this sees it
    }
}

// Pauses, lets go of the textures, makes the renderer again and gives it to the owners. False when
// no renderer can be made (or it keeps failing): the run ends, with an error status for whoever
// restarts it. The window system's events from a window made again are not redrawn from in here.
bool WindowState::recover_renderer() {
    const FrameDetailScope timing(FrameDetail::Recovery);
    hook.redrawing = true;
    const bool back = replace_renderer();
    hook.redrawing = false;
    if (!back) {
        running = false;
        renderer_gone = true;
    }
    return back;
}

bool WindowState::replace_renderer() {
    const auto began = std::chrono::steady_clock::now();
    const etude::LossCause cause = recovery.cause();
    const etude::RecoveryPlan plan = recovery.begin(frame_index);
    if (plan.give_up) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "the renderer was lost again and again (%s), quitting",
                     etude::loss_cause_name(cause));
        return false;
    }
    // Nothing plays through a recovery: the player resumes when the picture is back.
    const bool was_playing = songs.playing() || songs.counting_down();
    songs.lost_focus(midi_clock_ns());
    if (replay.viewing() && replay.playing()) {
        replay.play_pause();
    }

    gfx::set_target(renderer, nullptr);
    if (headless_target != nullptr) {
        gfx::destroy_texture(headless_target);
        headless_target = nullptr;
        headless_w = 0;
        headless_h = 0;
    }
    scene.drop_renderer();
    sheet_view.drop_renderer();
    ui.drop_renderer();

    // A path the developer asked for goes first; then the plan's.
    std::array<etude::RendererPath, 3> tries{};
    std::size_t count = 0;
    const auto add = [&](etude::RendererPath path) {
        for (std::size_t i = 0; i < count; ++i) {
            if (tries[i] == path) {
                return;
            }
        }
        tries[count++] = path;
    };
    if (sim_target) {
        add(*sim_target);
    }
    for (std::size_t i = 0; i < plan.count; ++i) {
        add(plan.tries[i]);
    }
    sim_target.reset();
    sim_loss = false;

    etude::RendererPath used = etude::RendererPath::Basic;
    if (!gfx::replace_renderer(window, &renderer, {tries.data(), count}, &used)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "no renderer could be made after %s, quitting",
                     etude::loss_cause_name(cause));
        return false;
    }
    recovery.finish(used);
    ++recoveries;
    recovered_frame = frame_index;
    vsync = !opt.headless && gfx::set_vsync(renderer, 1);
    next_frame_ns = midi_clock_ns();
    scene.adopt_renderer(renderer);
    sheet_view.adopt_renderer(renderer);
    ui.adopt_renderer(renderer);
    ensure_headless_target();
    idle.note_input(true); // the next frame draws whatever the old picture's hash said

    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                 began)
                          .count();
    SDL_Log("renderer recovered after %s: %s (SDL %s), %.0f ms%s", etude::loss_cause_name(cause),
            std::string(etude::renderer_path_name(used)).c_str(), gfx::renderer_name(renderer),
            ms, was_playing ? ", play paused" : "");
    if (was_playing) {
        show_message("The screen was restored. Press Space to go on.");
    }
    return true;
}

} // namespace etude::app
