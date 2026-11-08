// SPDX-License-Identifier: GPL-3.0-or-later
// The settings check's taps (settings_check_rig.hpp): the reset buttons and the (i) buttons of the
// real pages, tapped as a finger would. A reset must put back the value the settings struct defines
// (so a number of its own in a row fails), do nothing while greyed, leave Grade words alone when the
// Opacity strength is reset, and be undone by the toast whether or not the sheet is still open; an
// (i) must close on a tap on itself and switch to another in one tap.
#include "settings_check_rig.hpp"

#include "ui/icon_audit.hpp"
#include "ui/item_probe.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace etude::app {

namespace {

constexpr int kFrames = 12;
constexpr const char* kUndoChip = "##toast-undo-chip";

struct Spot {
    float x = 0.0f;
    float y = 0.0f;
    bool found = false;
};

// The middle of the button the audit saw named `name` (a note), or of the item whose name contains
// `name` (a hit), in the last frame drawn.
Spot note_spot(const std::string& name) {
    for (const ui::audit::Note& n : ui::audit::notes()) {
        if (n.name == name) {
            return {(n.min.x + n.max.x) * 0.5f, (n.min.y + n.max.y) * 0.5f, true};
        }
    }
    return {};
}

Spot hit_spot(const std::string& part) {
    for (const ui::audit::Hit& h : ui::audit::hits()) {
        if (h.name.find(part) != std::string::npos) {
            return {(h.min.x + h.max.x) * 0.5f, (h.min.y + h.max.y) * 0.5f, true};
        }
    }
    return {};
}

// The plus buttons of the steppers on the page, top to bottom.
std::vector<Spot> plus_spots() {
    std::vector<Spot> out;
    for (const ui::audit::Note& n : ui::audit::notes()) {
        if (n.name == "##plus") {
            out.push_back({(n.min.x + n.max.x) * 0.5f, (n.min.y + n.max.y) * 0.5f, true});
        }
    }
    std::sort(out.begin(), out.end(), [](const Spot& l, const Spot& r) { return l.y < r.y; });
    return out;
}

void frames(const CheckRig& rig, int n = kFrames) {
    for (int i = 0; i < n; ++i) {
        rig.draw();
    }
}

// The open page back at its top.
void scroll_to_top(const CheckRig& rig) {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    rig.mouse(SDL_EVENT_MOUSE_MOTION, size.x * 0.5f, size.y * 0.5f);
    rig.mouse(SDL_EVENT_MOUSE_WHEEL, 0.0f, 80.0f);
    frames(rig, 4);
}

// Scrolls the open page until the button `name` is in the middle of the window, and says where it is.
// The page starts at its top; a page that fits does not move. False when it never shows.
bool reveal(const CheckRig& rig, const std::string& name, Spot* at, float lowest = 0.94f) {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    scroll_to_top(rig);
    for (int i = 0; i < 60; ++i) {
        const Spot s = note_spot(name);
        if (s.found && s.y > size.y * 0.2f && s.y < size.y * lowest) {
            *at = s;
            return true;
        }
        rig.mouse(SDL_EVENT_MOUSE_WHEEL, 0.0f, s.found && s.y <= size.y * 0.2f ? 1.0f : -1.0f);
        frames(rig, 3);
    }
    return false;
}

// A tap: the press in one frame, the release in the next, and a few frames after.
void tap(const CheckRig& rig, Spot at) {
    rig.mouse(SDL_EVENT_MOUSE_MOTION, at.x, at.y);
    rig.mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, at.x, at.y);
    rig.draw();
    rig.mouse(SDL_EVENT_MOUSE_BUTTON_UP, at.x, at.y);
    frames(rig, 3);
}

// A setting a row resets: where it is, and a value off its default.
struct IntRow {
    PanelTab tab;
    const char* label;
    int* value;
    int fresh;
    int moved;
};
struct FloatRow {
    PanelTab tab;
    const char* label;
    float* value;
    float fresh;
    float moved;
};

// A reset's toast sits over the rows below the button: it goes before the next row is looked at.
void clear_toast(const CheckRig& rig) {
    rig.ui.dismiss_toast();
    frames(rig, 2);
}

void check_int_row(const CheckRig& rig, const IntRow& r, std::vector<std::string>& problems) {
    const std::string name = std::string("##reset ") + r.label;
    rig.ui.open_panel(r.tab);
    *r.value = r.fresh;
    frames(rig);
    if (note_spot(name).found || hit_spot("##reset").found) {
        problems.push_back(std::string(r.label) + ": the reset is a pressable target at its default");
    }
    *r.value = r.moved;
    frames(rig);
    Spot at;
    if (!reveal(rig, name, &at)) {
        problems.push_back(std::string(r.label) + ": no active reset off its default");
        return;
    }
    tap(rig, at);
    clear_toast(rig);
    if (*r.value != r.fresh) {
        problems.push_back(std::string(r.label) + ": a reset left " + std::to_string(*r.value) +
                           ", the struct's default is " + std::to_string(r.fresh));
    }
}

void check_float_row(const CheckRig& rig, const FloatRow& r, std::vector<std::string>& problems) {
    const std::string name = std::string("##reset ") + r.label;
    rig.ui.open_panel(r.tab);
    *r.value = r.moved;
    frames(rig);
    Spot at;
    if (!reveal(rig, name, &at)) {
        problems.push_back(std::string(r.label) + ": no active reset off its default");
        return;
    }
    tap(rig, at);
    clear_toast(rig);
    if (*r.value != r.fresh) {
        problems.push_back(std::string(r.label) + ": a reset left " + std::to_string(*r.value) +
                           ", the struct's default is " + std::to_string(r.fresh));
    }
}

// Opacity: the strength goes back and Grade words stay; the toast's Undo gives the strength back,
// with the sheet open and with it closed.
void check_opacity(const CheckRig& rig, std::vector<std::string>& problems) {
    LookSettings& look = rig.model.look;
    const LookSettings fresh;
    for (const bool close_first : {false, true}) {
        rig.ui.open_panel(PanelTab::View);
        look.grade_words = false;
        look.grade_opacity = 60;
        frames(rig);
        Spot at;
        if (!reveal(rig, "##reset Opacity", &at)) {
            problems.push_back("Opacity: no active reset with the words off and the strength 60");
            return;
        }
        tap(rig, at);
        if (look.grade_opacity != fresh.grade_opacity || look.grade_words) {
            problems.push_back("Opacity: a reset left strength " + std::to_string(look.grade_opacity) +
                               (look.grade_words ? " and turned Grade words on" : ""));
        }
        if (close_first) {
            rig.ui.close_panel(); // the row is no longer drawn when Undo is tapped
            frames(rig, 4);
        }
        const Spot chip = hit_spot(kUndoChip);
        if (!chip.found) {
            problems.push_back("Opacity: the reset's toast has no Undo");
            return;
        }
        tap(rig, chip);
        if (look.grade_opacity != 60 || look.grade_words) {
            problems.push_back(std::string("Opacity: Undo ") + (close_first ? "after the sheet closed " : "") +
                               "left strength " + std::to_string(look.grade_opacity));
        }
    }
}

// A greyed reset takes no touch, and the row does not move when the reset lights up.
void check_greyed(const CheckRig& rig, std::vector<std::string>& problems) {
    AppSettings& app = rig.model.app;
    const AppSettings fresh;
    clear_toast(rig);
    rig.ui.open_panel(PanelTab::Practice);
    app.skip_back_seconds = fresh.skip_back_seconds;
    app.skip_forward_seconds = fresh.skip_forward_seconds;
    frames(rig);
    Spot probe;
    reveal(rig, "##plus", &probe, 0.55f); // the three steppers in view
    const std::vector<Spot> before = plus_spots();
    app.skip_back_seconds = 10; // lights Skip back's reset
    frames(rig);
    const Spot lit = note_spot("##reset Skip back");
    const std::vector<Spot> after = plus_spots();
    if (!lit.found || before.size() != 3 || after.size() != 3) {
        problems.push_back("the Skip back reset did not light, or the steppers were not found (" +
                           std::string(lit.found ? "lit" : "not lit") + ", " +
                           std::to_string(before.size()) + " before, " +
                           std::to_string(after.size()) + " after)");
        return;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::abs(before[i].x - after[i].x) > 0.01f || std::abs(before[i].y - after[i].y) > 0.01f) {
            problems.push_back("a stepper moved when its reset lit up");
        }
    }
    // Skip forward's reset is greyed: where it stands (its sibling's x, its own row) a tap does
    // nothing, and no pressable item is there.
    Spot greyed = lit;
    greyed.y = after[2].y;
    tap(rig, greyed);
    if (app.skip_forward_seconds != fresh.skip_forward_seconds) {
        problems.push_back("a tap on a greyed reset changed its setting");
    }
    if (rig.ui.info_bubble() != 0) {
        problems.push_back("a tap on a greyed reset opened something");
    }
}

// The scripted run finds a stepper's buttons by the ids ImGui gives them (`probe::row_item_id`): the
// plus and the minus of Resume after must be among the page's items under those ids, and a tap on
// each moves the setting by one second.
void check_stepper_ids(const CheckRig& rig, std::vector<std::string>& problems) {
    AppSettings& app = rig.model.app;
    clear_toast(rig);
    rig.ui.open_panel(PanelTab::Practice);
    app.resume_countdown_s = AppSettings{}.resume_countdown_s;
    ui::probe::enable(true);
    frames(rig);
    scroll_to_top(rig);
    // Where the button is once the page has scrolled to it (a small window scrolls), as the scripted
    // run goes to it; one frame's items, so a scroll moves the answer.
    const auto find = [&rig](const char* leaf, Spot* at) {
        for (int i = 0; i < 60; ++i) {
            ui::probe::next_frame(); // drop what the frames before collected,
            rig.draw();
            ui::probe::next_frame(); // and take this one frame's items
            bool seen = false;
            for (const ui::probe::Item& it : ui::probe::items()) {
                for (const bool in_table : {true, false}) {
                    if (it.id != ui::probe::row_item_id(it.window, "Resume after", leaf, in_table)) {
                        continue;
                    }
                    seen = true;
                    if (it.centre_visible(4.0f)) {
                        *at = {it.centre().x, it.centre().y, true};
                        return true;
                    }
                }
            }
            if (!seen) {
                return false;
            }
            rig.mouse(SDL_EVENT_MOUSE_WHEEL, 0.0f, -1.0f);
            frames(rig, 3);
        }
        return false;
    };
    for (const int direction : {1, -1}) {
        Spot at;
        const int before = app.resume_countdown_s;
        if (!find(direction > 0 ? "##plus" : "##minus", &at)) {
            problems.push_back("Resume after: the scripted run's id for its stepper button is not on the page");
            break;
        }
        tap(rig, at);
        if (app.resume_countdown_s != before + direction) {
            problems.push_back("Resume after: a tap on its " + std::string(direction > 0 ? "plus" : "minus") +
                               " left " + std::to_string(app.resume_countdown_s));
        }
    }
    ui::probe::enable(false);
}

// An (i): a tap on the open one closes it; a tap on another switches to it in that one tap; the open
// bubble stays while the second is only pressed.
void check_info(const CheckRig& rig, std::vector<std::string>& problems) {
    clear_toast(rig);
    rig.ui.open_panel(PanelTab::Practice);
    frames(rig);
    scroll_to_top(rig);
    std::vector<Spot> infos;
    for (int i = 0; i < 60 && infos.size() < 2; ++i) {
        infos.clear();
        for (const ui::audit::Note& n : ui::audit::notes()) {
            if (n.name == "##info") {
                infos.push_back({(n.min.x + n.max.x) * 0.5f, (n.min.y + n.max.y) * 0.5f, true});
            }
        }
        if (infos.size() < 2) {
            rig.mouse(SDL_EVENT_MOUSE_WHEEL, 0.0f, -1.0f); // two (i) in view: scroll on
            frames(rig, 3);
        }
    }
    if (infos.size() < 2) {
        problems.push_back("fewer than two (i) buttons on the Practice page");
        return;
    }
    const Spot a = infos[0];
    const Spot b = infos[1];
    if (rig.ui.info_bubble() != 0) {
        problems.push_back("an (i) bubble was open before any tap");
    }
    tap(rig, a);
    const std::uint32_t first = rig.ui.info_bubble();
    if (first == 0) {
        problems.push_back("a tap on an (i) did not open its bubble");
    }
    tap(rig, a);
    if (rig.ui.info_bubble() != 0) {
        problems.push_back("a tap on the open (i) did not close its bubble");
    }
    tap(rig, a);
    // Press on the other (i): the open bubble stays; release: it switches.
    rig.mouse(SDL_EVENT_MOUSE_MOTION, b.x, b.y);
    rig.mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, b.x, b.y);
    rig.draw();
    if (rig.ui.info_bubble() != first) {
        problems.push_back("pressing another (i) closed the open bubble before the release");
    }
    rig.mouse(SDL_EVENT_MOUSE_BUTTON_UP, b.x, b.y);
    rig.draw();
    const std::uint32_t second = rig.ui.info_bubble();
    if (second == 0 || second == first) {
        problems.push_back("a tap on another (i) did not switch to its bubble in that one tap");
    }
    tap(rig, b); // closes it again, leaving the page as found
}

} // namespace

int check_taps(const CheckRig& rig) {
    std::vector<std::string> problems;
    const AppSettings app;
    const LookSettings look;
    AppSettings& a = rig.model.app;
    LookSettings& l = rig.model.look;
    const IntRow ints[] = {
        {PanelTab::Practice, "Custom", &a.hit_window_ms, app.hit_window_ms, 120},
        {PanelTab::Practice, "Replays kept per song", &a.run_events_kept, app.run_events_kept, 5},
        {PanelTab::Practice, "Resume after", &a.resume_countdown_s, app.resume_countdown_s, 7},
        {PanelTab::Practice, "Skip back", &a.skip_back_seconds, app.skip_back_seconds, 10},
        {PanelTab::Practice, "Skip forward", &a.skip_forward_seconds, app.skip_forward_seconds, 12},
        {PanelTab::Practice, "Accompaniment volume", &a.accompaniment_volume,
         app.accompaniment_volume, 60},
        {PanelTab::Practice, "Volume", &a.metronome_volume, app.metronome_volume, 30},
        {PanelTab::Input, "Latency", &a.latency_correction_ms, app.latency_correction_ms, 40},
        {PanelTab::Notes, "Block glow", &l.block_glow, look.block_glow, 10},
        {PanelTab::View, "Opacity", &l.grade_opacity, look.grade_opacity, 40},
    };
    const FloatRow floats[] = {
        {PanelTab::Notes, "Intensity", &l.style_intensity, look.style_intensity, 0.2f},
        {PanelTab::View, "Size", &l.chord_label_size, look.chord_label_size, 0.7f},
        {PanelTab::View, "Notes ahead", &l.visible_seconds, look.visible_seconds, 2.5f},
        {PanelTab::View, "Keyboard", &l.keyboard_height, look.keyboard_height, 0.3f},
    };
    for (const IntRow& r : ints) {
        check_int_row(rig, r, problems);
    }
    for (const FloatRow& r : floats) {
        check_float_row(rig, r, problems);
    }
    check_opacity(rig, problems);
    check_greyed(rig, problems);
    check_stepper_ids(rig, problems);
    check_info(rig, problems);
    for (const std::string& p : problems) {
        std::printf("settings check: %s\n", p.c_str());
    }
    if (problems.empty()) {
        std::printf("settings check: the reset buttons and the (i) buttons answer taps as ruled\n");
    }
    return static_cast<int>(problems.size());
}

} // namespace etude::app
