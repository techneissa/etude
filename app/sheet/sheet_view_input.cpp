// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_view.hpp"

#include "sheet_view_internal.hpp"

#include "../gfx.hpp"
#include "../song_controller.hpp"
#include "../touch_device.hpp"
#include "sheet_name_fit.hpp"

#include "etude/hands_present.hpp"
#include "etude/note_colours.hpp"
#include "etude/screen_layout.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace etude::app {

void SheetView::handle_event(const SDL_Event& e, SDL_Window* window, std::int64_t now_ns,
                             bool other_finger_down) {
    clock_s_ = std::max(clock_s_, static_cast<double>(now_ns) * 1e-9);
    const double t = clock_s_;
    int w = 0;
    int h = 0;
    int pw = 0;
    int ph = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    pixel_scale_ = w > 0 ? static_cast<float>(pw) / static_cast<float>(w) : 1.0f;
    const auto on_score = [&](float px, float py) {
        return front_.ready && px >= paper_.x && px < paper_.right() && py >= paper_.y &&
               py < paper_.bottom();
    };
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        // A trackpad's fingers are not on the screen: its pointer events cover it.
        if (!is_screen_touch(e.tfinger.touchID)) {
            return;
        }
        last_finger_s_ = t;
        const float px = e.tfinger.x * static_cast<float>(pw);
        const float py = e.tfinger.y * static_cast<float>(ph);
        const auto id = static_cast<std::int64_t>(e.tfinger.fingerID);
        const float x = px / pixel_scale_;
        const float y = py / pixel_scale_;
        if (e.type == SDL_EVENT_FINGER_DOWN) {
            // A lift that never came: this finger starts clean. A mouse drag in progress is no
            // finger's lost lift, so a finger that lands leaves it alone.
            if (!other_finger_down && gesture_live_ && !mouse_live_) {
                drop_gesture();
            }
            if (mouse_live_) {
                return;
            }
            if (!gesture_live_ && (!on_score(px, py) ||
                                   (ui_owns_point_ && ui_owns_point_(e.tfinger.x, e.tfinger.y)))) {
                return;
            }
            gesture_live_ = true;
            gestures_.down(id, x, y, t, other_finger_down);
        } else if (gesture_live_ && !mouse_live_) {
            if (e.type == SDL_EVENT_FINGER_MOTION) {
                gestures_.move(id, x, y, t);
            } else if (e.type == SDL_EVENT_FINGER_UP) {
                gestures_.up(id, x, y, t);
            } else {
                gestures_.cancel();
            }
        }
        return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.button.which == SDL_TOUCH_MOUSEID || t - last_finger_s_ <= kIgnoreMouseAfterFingerS ||
            (e.button.button != SDL_BUTTON_LEFT && e.button.button != SDL_BUTTON_RIGHT)) {
            return;
        }
        const float x = e.button.x;
        const float y = e.button.y;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (gesture_live_ || !on_score(x * pixel_scale_, y * pixel_scale_) ||
                ImGui::GetIO().WantCaptureMouse) {
                return;
            }
            gesture_live_ = true;
            mouse_live_ = true;
            gestures_.down(0, x, y, t);
            if (e.button.button == SDL_BUTTON_RIGHT) {
                gestures_.long_press_now(x, y, t); // a right click holds at once
            }
        } else if (mouse_live_) {
            gestures_.up(0, x, y, t);
        }
        return;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (mouse_live_ && e.motion.which != SDL_TOUCH_MOUSEID) {
            gestures_.move(0, e.motion.x, e.motion.y, t);
        }
        return;
    default:
        return;
    }
}

void SheetView::drop_gesture() {
    if (!gesture_live_ && !dragging_loop_ && gestures_.events().empty()) {
        return;
    }
    gestures_.cancel();
    gestures_.clear_events();
    gesture_live_ = false;
    mouse_live_ = false;
    dragging_loop_ = false;
}

bool SheetView::take_request(SheetRequest& out) {
    if (request_count_ == 0) {
        return false;
    }
    out = requests_[0];
    for (int i = 1; i < request_count_; ++i) {
        requests_[static_cast<std::size_t>(i - 1)] = requests_[static_cast<std::size_t>(i)];
    }
    --request_count_;
    return true;
}

bool SheetView::hit(float px, float py, int& system, float& x) const {
    if (px < paper_.x || px >= paper_.right() || py < paper_.y || py >= paper_.bottom()) {
        return false;
    }
    for (int i = 0; i < placed_count_; ++i) {
        const Placed& p = placed_[static_cast<std::size_t>(i)];
        if (py >= p.top && py < p.bottom && p.scale > 0.0f) {
            system = p.system;
            x = (px - p.x) / p.scale;
            return true;
        }
    }
    return false;
}

void SheetView::run_gestures(const SongController& songs, std::int64_t now_ns) {
    clock_s_ = std::max(clock_s_, static_cast<double>(now_ns) * 1e-9);
    gestures_.update(clock_s_);
    const Song& song = songs.song().song;
    const auto push = [&](const SheetRequest& r) {
        if (request_count_ < static_cast<int>(requests_.size())) {
            requests_[static_cast<std::size_t>(request_count_++)] = r;
        }
    };
    for (const GestureRecognizer::Event& ev : gestures_.events()) {
        int system = -1;
        float x = 0.0f;
        const bool on = front_.ready && hit(ev.x * pixel_scale_, ev.y * pixel_scale_, system, x);
        switch (ev.kind) {
        case GestureRecognizer::Event::Kind::DoubleTap: {
            if (!on) {
                break;
            }
            const SheetTarget t = sheet_target_at(front_.layout, front_.map, song, system, x, tick_);
            if (t.played_bar >= 0) {
                SheetRequest r;
                r.kind = SheetRequest::Kind::Start;
                r.beat_tick = t.beat_tick;
                r.onset_tick = t.onset_tick;
                push(r);
            }
            break;
        }
        case GestureRecognizer::Event::Kind::LongPress: {
            if (!on) {
                break;
            }
            const int k = sheet_played_bar_for(
                front_.map, sheet_written_bar_at(front_.layout, system, x), tick_);
            if (k >= 0) {
                dragging_loop_ = true;
                drag_anchor_ = k;
                drag_last_ = k;
            }
            break;
        }
        case GestureRecognizer::Event::Kind::HeldMove:
            if (dragging_loop_ && on) {
                const SheetLoopEnds ends = sheet_loop_ends(
                    front_.map, drag_anchor_, sheet_written_bar_at(front_.layout, system, x));
                if (ends.anchor >= 0) {
                    drag_anchor_ = ends.anchor;
                    drag_last_ = ends.last;
                }
            }
            break;
        case GestureRecognizer::Event::Kind::HeldEnd:
            if (dragging_loop_) {
                const SheetLoopRange range = sheet_loop_range(front_.map, song, drag_anchor_, drag_last_);
                if (range.first_bar >= 0) {
                    SheetRequest r;
                    r.kind = SheetRequest::Kind::Loop;
                    r.start_tick = range.start_tick;
                    r.end_tick = range.end_tick;
                    push(r);
                }
            }
            dragging_loop_ = false;
            break;
        case GestureRecognizer::Event::Kind::Cancel:
            dragging_loop_ = false;
            break;
        default:
            break; // a single tap, a drag or a pinch does nothing on the score
        }
    }
    gestures_.clear_events();
    if (!gestures_.touching()) {
        gesture_live_ = false;
        mouse_live_ = false;
        dragging_loop_ = false;
    }
    // The song's loop, as played bars, for the band on the score.
    loop_first_ = -1;
    loop_last_ = -1;
    if (songs.loop_enabled() && !front_.map.bar_start.empty()) {
        const auto bar_at = [&](double t) {
            const auto it = std::upper_bound(front_.map.bar_start.begin(), front_.map.bar_start.end(), t,
                                             [](double v, std::int64_t s) { return v < static_cast<double>(s); });
            return it == front_.map.bar_start.begin() ? 0 : static_cast<int>(it - front_.map.bar_start.begin()) - 1;
        };
        loop_first_ = bar_at(song.tempo.tick_at(songs.loop_start_s()) + 0.5);
        loop_last_ = std::max(loop_first_, bar_at(song.tempo.tick_at(songs.loop_end_s()) - 0.5));
    }
}

} // namespace etude::app
