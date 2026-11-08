// SPDX-License-Identifier: GPL-3.0-or-later
#include "tap.hpp"

#include "etude/tap_rule.hpp"

#include <imgui.h>

namespace etude::app::ui {

namespace {

struct State {
    TapTracker press;
    float unit = 1.0f;
    int release_frame = -1; // the frame the last press was lifted in
    bool extra_finger = false; // a second finger landed before the press it belongs to began
};

State& state() {
    static State s;
    return s;
}

// The slop is the finger's drift on the screen, so it follows the frame's unit and not the scale a
// card draws at: it is the same distance the bar's swipe claims a touch at.
TapRuleConfig config(float repeat_s = 0.0f) {
    return TapRuleConfig::scaled(state().unit, static_cast<double>(repeat_s));
}

} // namespace

void tap_frame(float unit) {
    State& s = state();
    s.unit = unit > 0.0f ? unit : 1.0f;
    const ImGuiIO& io = ImGui::GetIO();
    const double t = ImGui::GetTime();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s.press.down(io.MouseClickedPos[0].x, io.MouseClickedPos[0].y, t);
        if (s.extra_finger) {
            s.press.cancel();
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        s.press.up(io.MousePos.x, io.MousePos.y, t);
        s.release_frame = ImGui::GetFrameCount();
        s.extra_finger = false;
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        s.press.move(io.MousePos.x, io.MousePos.y, t);
    }
}

void tap_cancel() { state().press.cancel(); }

void tap_extra_finger() {
    state().press.cancel();
    state().extra_finger = true;
}

void tap_fingers_clear() { state().extra_finger = false; }

std::uint64_t tap_serial() { return state().press.serial(); }

bool tap_cancelled() { return state().press.cancelled(); }

bool tapped(bool pressed) {
    if (!pressed) {
        return false;
    }
    const State& s = state();
    if (s.release_frame != ImGui::GetFrameCount()) {
        return true; // no pointer release this frame: the keyboard or the navigation pressed it
    }
    return s.press.tap(config());
}

int hold_repeats_now(double repeat_s) {
    return state().press.repeats(config(static_cast<float>(repeat_s)));
}

bool held_still() { return state().press.holding(config()); }

ImVec2 tap_start() { return {state().press.start_x(), state().press.start_y()}; }

bool tapped_away_from(ImVec2 min, ImVec2 max) {
    const State& s = state();
    if (s.release_frame != ImGui::GetFrameCount() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) || !s.press.tap(config())) {
        return false;
    }
    const auto outside = [&](ImVec2 at) {
        return at.x < min.x || at.x > max.x || at.y < min.y || at.y > max.y;
    };
    return outside(tap_start()) && outside(ImGui::GetIO().MousePos);
}

// The last item's tap or long press. A press held still past the long-press time reports `hold`
// once, and its release is then no tap; a press that moved past the slop (a scroll took it) is
// neither (the tap rule, core tap_rule.hpp).
Press press_of(bool pressed) {
    Press p;
    const ImGuiID id = ImGui::GetItemID();
    ImGuiStorage* st = ImGui::GetStateStorage();
    bool fired = st->GetBool(id, false);
    // The hold needs a press that never left the slop, not only one that is inside it now.
    if (ImGui::IsItemActive() && !fired && held_still()) {
        fired = true;
        st->SetBool(id, true);
        p.hold = true;
    }
    if (pressed) {
        p.tap = !fired && tapped(pressed);
        st->SetBool(id, false);
    } else if (!ImGui::IsItemActive() && fired) {
        st->SetBool(id, false);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        p.hold = true;
        p.right = true;
    }
    return p;
}

bool OutsideTap::away(bool shown, ImVec2 min, ImVec2 max) {
    const int frame = ImGui::GetFrameCount();
    if (seen_ < frame - 1) {
        close_.update(false, 0); // not drawn last frame: this is a new opening
    }
    seen_ = frame;
    close_.update(shown, tap_serial());
    return close_.may_close(tap_serial()) && tapped_away_from(min, max);
}

} // namespace etude::app::ui
