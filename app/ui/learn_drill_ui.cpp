// SPDX-License-Identifier: GPL-3.0-or-later
// The drill's controls on the Learn cards (#373; learn_screen.hpp): the tempo stepper, the direction,
// octave and scale / arpeggio buttons, the foot row's Listen and Practice, the settings' popover where
// the column has no room for them, and the stepper the chord card's inversion shares. Where each goes
// is learn_card_layout.hpp's; what they mean for the player is learn_drill.hpp's.
#include "learn_screen.hpp"

#include "bar.hpp" // begin_layer
#include "icon_audit.hpp"
#include "icons.hpp"
#include "learn_drill.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace etude::app::ui {

namespace {

constexpr std::array<const char*, 3> kDirIds{"##learn-dir-up", "##learn-dir-down",
                                             "##learn-dir-both"};
constexpr std::array<const char*, 2> kOctIds{"##learn-oct-1", "##learn-oct-2"};
constexpr std::array<const char*, 2> kKindIds{"##learn-kind-scale", "##learn-kind-arpeggio"};

// One button of a row of choices: the cell's face with its icon and words (or either alone).
bool empty(const Box& b) { return !(b.w() > 0.0f) || !(b.h() > 0.0f); }

bool cell(Kit& k, const char* id, const char* icon, std::string_view words, const Box& b, bool on,
          bool live = true) {
    ChipOpts o;
    o.icon = icon;
    o.on = on;
    o.live = live;
    o.width = b.w();
    o.fixed = true;
    o.hit_h = b.h();
    o.face_h = b.h() * (tok::kFace / tok::kHit);
    return chip(k, id, words, {b.x0, b.y0}, o);
}

} // namespace

// A stepper: a minus cell and a plus cell at the ends of a track, the value between them. Returns -1
// or 1 for the cell tapped, else 0. `icon` (may be null) stands before the value.
int LearnScreen::draw_stepper(Kit& k, const Box& b, const char* minus_id, const char* plus_id,
                              bool can_minus, bool can_plus, const char* icon, const char* words,
                              const char* short_words, float label_pt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cell_w = std::min(b.h(), b.w() * 0.5f);
    const float grown = b.h() / k.s(tok::kHit); // as tall as its row, its parts grown with it
    const float face = k.s(tok::kFace) * grown;
    const float track_y = b.cy() - face * 0.5f;
    dl->AddRectFilled({b.x0, track_y}, {b.x1, track_y + face}, u32(col::kFill),
                      k.s(tok::kRadiusControl));
    int step = 0;
    const auto button = [&](const char* id, const char* glyph_id, float x0, int direction,
                            bool enabled) {
        const Box hit{x0, b.y0, x0 + cell_w, b.y1};
        const bool pressed = tapped(press_area(k, id, hit));
        const State st = item_state(k, false, true, !enabled);
        audit::note(id, {hit.x0, track_y}, {hit.x1, track_y + face});
        const float in = k.s(tok::kInset);
        draw_state(k, dl, id, {hit.x0 + in, track_y + in, hit.x1 - in, track_y + face - in}, hit,
                   st, Idle::None, k.s(tok::kRadiusInner));
        glyph(k, dl, glyph_id, {hit.cx(), hit.cy()}, k.s(tok::kIcon), state_ink(st, col::kText));
        if (pressed && enabled) {
            step = direction;
        }
    };
    button(minus_id, icon::kMinus, b.x0, -1, can_minus);
    button(plus_id, icon::kPlus, b.x1 - cell_w, 1, can_plus);
    const float label = k.s(label_pt) * grown;
    const float room = b.w() - 2.0f * cell_w;
    const float icon_w = icon != nullptr ? k.s(tok::kIconSm) * grown + k.s(tok::kS1) : 0.0f;
    const char* shown_words = text_size(k.bold, label, words).x + icon_w <= room || short_words == nullptr
                                  ? words
                                  : short_words;
    const FitText shown = fit(k.bold, label, shown_words, room - icon_w);
    const float tw = text_size(k.bold, label, shown.c_str()).x;
    float x = b.cx() - (tw + icon_w) * 0.5f;
    if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIconSm) * grown * 0.5f, b.cy()},
              k.s(tok::kIconSm) * grown, col::kText2);
        x += icon_w;
    }
    label_at(k, dl, k.bold, label, {x, track_y, x + tw, track_y + face}, LabelAlign::Centre,
             col::kText, shown.c_str());
    return step;
}

void LearnScreen::draw_drill_rows(Kit& k, AppSettings& app, const DrillBoxes& d, bool kinds) {
    // The tempo.
    if (!empty(d.tempo)) {
        char number[8];
        std::snprintf(number, sizeof number, "%d", app.learn_tempo);
        const int step = draw_stepper(k, d.tempo, "##learn-tempo-minus", "##learn-tempo-plus",
                                      app.learn_tempo > kDrillMinBpm, app.learn_tempo < kDrillMaxBpm,
                                      icon::kMetronome, number, nullptr, tok::kTitle);
        if (step != 0) {
            app.learn_tempo = drill_tempo(app.learn_tempo + step * kDrillBpmStep);
        }
    }
    // The direction: up, down, up and down.
    constexpr std::array<const char*, 3> dir_icons{icon::kArrowUp, icon::kArrowDown, icon::kHeight};
    const int dir = static_cast<int>(app.learn_direction);
    for (std::size_t i = 0; i < d.dir.size() && !empty(d.dir[i]); ++i) {
        if (cell(k, kDirIds[i], dir_icons[i], "", d.dir[i], static_cast<int>(i) == dir)) {
            app.learn_direction = static_cast<LearnDirection>(i);
        }
    }
    // The octaves.
    for (std::size_t i = 0; i < d.oct.size() && !empty(d.oct[i]); ++i) {
        const char* words = d.oct_words ? (i == 0 ? "1 Octave" : "2 Octaves") : (i == 0 ? "1 oct" : "2 oct");
        if (cell(k, kOctIds[i], nullptr, words, d.oct[i], app.learn_octaves == static_cast<int>(i) + 1)) {
            app.learn_octaves = static_cast<int>(i) + 1;
        }
    }
    // Scale or arpeggio: the arpeggio is the scale's triad, so a scale with none has no arpeggio.
    if (kinds && !empty(d.kind[0])) {
        const bool has_triad = scale_triad(app.learn_scale).has_value();
        const bool arpeggio = learn_scale_arpeggio(app);
        for (std::size_t i = 0; i < d.kind.size(); ++i) {
            const bool live = i == 0 || has_triad;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                                ImGui::GetStyle().Alpha * (live ? 1.0f : tok::kDisabledAlpha));
            const char* icon = i == 0 ? icon::kScales : icon::kWave;
            const char* words = d.kind_words ? (i == 0 ? "Scale" : "Arpeggio") : "";
            if (cell(k, kKindIds[i], icon, words, d.kind[i], (i == 1) == arpeggio, live) && live) {
                app.learn_arpeggio = i == 1;
            }
            ImGui::PopStyleVar();
        }
    }
}

// The scale practice's settings where the card puts them: in the column (Full, Compact, and in the
// Popover column as many as fit under the keys), and the rest behind the Options button.
void LearnScreen::draw_drill(Kit& k, AppSettings& app, const LearnCardLayout& c, bool kinds) {
    draw_drill_rows(k, app, c.drill, kinds);
    if (!kinds && !empty(c.drill.stepper)) {
        draw_inversion(k, app, c.drill.stepper);
    }
    if (c.column != LearnColumn::Popover || !c.options) {
        settings_open_ = false;
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ChipOpts o;
    o.icon = icon::kOptions;
    o.on = settings_open_;
    o.width = c.settings.w();
    o.fixed = true;
    o.hit_h = c.settings.h();
    o.face_h = c.settings.h() * (tok::kFace / tok::kHit);
    if (chip(k, "##learn-settings", "Options", {c.settings.x0, c.settings.y0}, o)) {
        settings_open_ = !settings_open_;
    }
    if (!settings_open_) {
        return;
    }
    // A tap off the card (and off its button) closes it.
    const ImGuiIO& io = ImGui::GetIO();
    const Box p = c.popover;
    const bool inside = io.MousePos.x >= p.x0 && io.MousePos.x <= p.x1 && io.MousePos.y >= p.y0 &&
                        io.MousePos.y <= p.y1;
    const Box s = c.settings;
    const bool on_button = io.MousePos.x >= s.x0 && io.MousePos.x <= s.x1 &&
                           io.MousePos.y >= s.y0 && io.MousePos.y <= s.y1;
    if (ImGui::IsMouseClicked(0) && !inside && !on_button) {
        settings_open_ = false;
        return;
    }
    // Opaque: the picker, the types and the keys are under it.
    ImVec4 solid = col::kSurface;
    solid.w = 1.0f;
    ui::overlay_card(k, dl, {p.x0, p.y0}, {p.x1, p.y1}, k.s(tok::kRadiusLg), solid);
    // The card takes the taps that fall between its controls, so nothing under it answers them.
    press_area(k, "##learn-settings-card", p);
    draw_drill_rows(k, app, c.pop, kinds);
    if (!kinds && !empty(c.pop.stepper)) {
        draw_inversion(k, app, c.pop.stepper);
    }
}

// Listen: plays the drill with the sound and lights the keys; the same button stops it.
void LearnScreen::listen(Kit& k, AppSettings& app, LearnPort& port, bool chords) {
    if (playing_) {
        port.stop_sound();
        playing_ = false;
        return;
    }
    std::string why;
    const double seconds = port.listen(learn_drill_spec(app, chords), why);
    if (seconds > 0.0) {
        playing_ = true;
        play_page_ = page_;
        play_t0_ = k.now;
        play_until_ = k.now + seconds;
    } else {
        k.say_free(why.empty() ? "That cannot be played on this keyboard." : why, true);
    }
}

// Practice: the app opens the scale or chord practice in the player, and the Learn screen gives way to it.
void LearnScreen::practise(Kit& k, AppSettings& app, LearnPort& port, bool chords) {
    std::string why;
    if (port.practise(learn_drill_spec(app, chords), why)) {
        if (playing_) {
            port.stop_sound();
            playing_ = false;
        }
        close();
    } else {
        k.say_free(why.empty() ? "That cannot be played on this keyboard." : why, true);
    }
}

// The foot row: the hands, then Listen and Practice (their words where the row holds them).
void LearnScreen::draw_foot(Kit& k, AppSettings& app, LearnPort& port, const LearnCardLayout& c,
                            bool chords) {
    draw_hands(k, app, c);
    const auto button = [&](const char* id, const char* icon, const char* words, const Box& b,
                            bool primary) {
        const float grown = b.h() / k.s(tok::kHit);
        return action_button(k, id, icon, c.foot_words ? words : "", {b.x0, b.y0}, {b.w(), b.h()},
                             true, primary, false, false, nullptr, tok::kFace * grown,
                             tok::kLabel * grown);
    };
    if (button("##learn-play", playing_ ? icon::kStop : icon::kSoundBoth,
               playing_ ? "Stop" : "Listen", c.play, false)) {
        listen(k, app, port, chords);
    }
    if (button("##learn-practise", icon::kPlay, "Practice", c.practise, true)) {
        practise(k, app, port, chords);
    }
}

} // namespace etude::app::ui
