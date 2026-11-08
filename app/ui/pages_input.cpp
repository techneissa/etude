// SPDX-License-Identifier: GPL-3.0-or-later
// Settings > Input: the source, the keyboard and its ports, the microphone and its level.
#include "pages.hpp"

#include "icons.hpp"
#include "input_port.hpp"
#include "item_probe.hpp"
#include "kit_internal.hpp"
#include "pages_columns.hpp"
#include "row_layout.hpp"
#include "settings_defaults.hpp"

#include "etude/keyboard.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

// The keyboard in use: Auto, a profile, or a key count by hand, as one row of choices.
void keyboard_rows(Kit& k, UiModel& model) {
    SessionState& s = model.session;
    constexpr std::size_t kMaxProfiles = 6;
    // The labels live as long as the app: the profiles are fixed.
    static const struct Labels {
        std::array<std::array<char, 40>, kMaxProfiles> text{};
        std::array<Seg, kMaxProfiles + 2> segs{};
        std::size_t count = 0;
        Labels() {
            segs[count++] = {nullptr, "Auto"};
            for (const KeyboardProfile& p : builtin_keyboard_profiles()) {
                if (count > kMaxProfiles) {
                    break;
                }
                auto& t = text[count - 1];
                std::snprintf(t.data(), t.size(), "%.*s", static_cast<int>(p.name.size()),
                              p.name.data());
                segs[count++] = {nullptr, t.data()};
            }
            segs[count++] = {nullptr, "Manual"};
        }
    } kLabels;
    const std::span<const KeyboardProfile> profiles = builtin_keyboard_profiles();
    const std::size_t shown_profiles = kLabels.count - 2;
    // A saved profile this build does not list shows no choice, never a wrong one.
    int current = s.keyboard == KeyboardChoice::Auto ? 0 : -1;
    if (s.keyboard == KeyboardChoice::Manual) {
        current = static_cast<int>(kLabels.count) - 1;
    } else if (s.keyboard == KeyboardChoice::Profile) {
        for (std::size_t i = 0; i < shown_profiles; ++i) {
            if (profiles[i].name == s.keyboard_name) {
                current = static_cast<int>(i) + 1;
            }
        }
    }
    catalogue_sweep::drawn(Setting::KeyboardProfile);
    if (segmented(k, "keyboard", nullptr, nullptr,
                  std::span<const Seg>(kLabels.segs.data(), kLabels.count), &current)) {
        if (current == 0) {
            s.keyboard = KeyboardChoice::Auto;
        } else if (static_cast<std::size_t>(current) == kLabels.count - 1) {
            s.keyboard = KeyboardChoice::Manual;
        } else {
            const KeyboardProfile& p = profiles[static_cast<std::size_t>(current) - 1];
            s.keyboard = KeyboardChoice::Profile;
            s.keyboard_name = std::string(p.name);
            s.keyboard_lowest = p.range.lowest;
            s.keyboard_highest = p.range.highest;
        }
    }
    if (s.keyboard == KeyboardChoice::Manual) {
        const KeyRange sizes[4] = {kRange88, kRange76, kRange61, kRange49};
        int size = -1;
        for (int i = 0; i < 4; ++i) {
            if (sizes[i].lowest == s.keyboard_lowest && sizes[i].highest == s.keyboard_highest) {
                size = i;
            }
        }
        constexpr Seg kKeys[] = {
            {nullptr, "88"}, {nullptr, "76"}, {nullptr, "61"}, {nullptr, "49"}};
        if (segmented(k, "keys", nullptr, "Keys", kKeys, &size)) {
            s.keyboard_lowest = sizes[size].lowest;
            s.keyboard_highest = sizes[size].highest;
        }
    }
}

// ---- Input

const char* source_name(InputMode m) {
    switch (m) {
    case InputMode::Midi:
        return "MIDI keyboard";
    case InputMode::Microphone:
        return "Microphone";
    case InputMode::Both:
        return "Both";
    }
    return "";
}

// The input level: a bar with the calibrated noise floor marked on it, a line of status above.
// Only drawn, nothing to tap. Red over the last tenth, where a loud room or a close microphone
// clips.
void meter_row(Kit& k, const InputView& v) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = k.s(tok::kHit) + k.s(8.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool live = v.capturing;
    const float x = pos.x + k.s(tok::kS3);
    glyph(k, dl, icon::kMic, {x + k.s(tok::kIcon) * 0.5f, pos.y + h * 0.5f}, k.s(tok::kIcon) * 0.9f,
          live ? col::kText2 : col::kText3);
    const float bx = x + k.s(tok::kIcon) + k.s(tok::kS3);
    const float bw = pos.x + w - k.s(tok::kS3) - bx;
    const float cap = k.s(tok::kLabel);
    const FitText status = fit(k.regular, cap, v.status, bw);
    text_at(k, dl, k.regular, cap, {bx, pos.y + k.s(4.0f)}, live ? col::kText2 : col::kText3,
            status.c_str());
    const float bar_h = k.s(12.0f);
    const float by = pos.y + h - bar_h - k.s(8.0f);
    dl->AddRectFilled({bx, by}, {bx + bw, by + bar_h}, u32(col::kFill), bar_h * 0.5f);
    const float level = std::clamp(v.level, 0.0f, 1.0f);
    if (live && level > 0.0f) {
        const float fx = bx + bw * level;
        const float safe = bx + bw * 0.9f;
        dl->AddRectFilled({bx, by}, {std::min(fx, safe), by + bar_h},
                          u32(with_alpha(col::kAccent, 0.9f)), bar_h * 0.5f);
        if (fx > safe) {
            dl->AddRectFilled({safe, by}, {fx, by + bar_h}, u32(col::kDanger), bar_h * 0.5f);
        }
    }
    if (v.floor_level > 0.0f) {
        const float fx = bx + bw * std::clamp(v.floor_level, 0.0f, 1.0f);
        dl->AddLine({fx, by - k.s(3.0f)}, {fx, by + bar_h + k.s(3.0f)}, u32(col::kText), 2.0f);
    }
    ImGui::Dummy({w, h});
}

// "Calibrate noise": a full-width button with a ring that fills over the 3 s it listens. The
// whole row is the hit area.
bool calibrate_row(Kit& k, const InputView& v) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = k.s(tok::kHit) + k.s(8.0f);
    ImGui::PushID("##calibrate");
    const bool pressed = ImGui::InvisibleButton("##calibrate", {w, h});
    const bool enabled = v.capturing;
    const State st = item_state(k, false, true, !enabled);
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a{pos.x, pos.y + k.s(2.0f)};
    const ImVec2 b{pos.x + w, pos.y + h - k.s(2.0f)};
    draw_state(k, dl, "##calibrate", {a.x, a.y, b.x, b.y}, {pos.x, pos.y, pos.x + w, pos.y + h}, st,
               Idle::Fill);
    const float r = k.s(15.0f);
    const ImVec2 c{pos.x + k.s(tok::kS3) + r, pos.y + h * 0.5f};
    const float thick = k.s(3.5f);
    dl->AddCircle(c, r, u32(with_alpha(col::kText, v.calibrating ? 0.18f : 0.12f)), 40, thick);
    if (v.calibrating) {
        const float start = -3.14159265f * 0.5f;
        dl->PathArcTo(c, r, start, start + 6.2831853f * std::clamp(v.calibration, 0.0f, 1.0f), 40);
        dl->PathStroke(u32(col::kAccent), 0, thick);
        const int left = std::max(1, static_cast<int>(std::ceil(3.0f * (1.0f - v.calibration))));
        char n[16];
        std::snprintf(n, sizeof n, "%d", left);
        label_at(k, dl, k.bold, k.s(tok::kLabel), {c.x - r, c.y - r, c.x + r, c.y + r},
                 LabelAlign::Centre, col::kText, n);
    } else {
        glyph(k, dl, v.floor_level > 0.0f ? icon::kCheck : icon::kMic, c, k.s(tok::kIconSm),
              enabled ? col::kText : col::kText3);
    }
    const float tx = c.x + r + k.s(tok::kS3);
    const float size = k.s(tok::kBody);
    const char* label = v.calibrating ? "Listening to the room..." : "Calibrate noise";
    const ImVec2 ts = text_size(k.regular, size, label);
    label_at(k, dl, k.regular, size, {tx, pos.y, pos.x + w, pos.y + h}, LabelAlign::Left,
             enabled ? col::kText : col::kText3, label);
    if (!v.calibrating) {
        const char* hint = enabled ? "3 s of quiet" : "needs the microphone";
        const float hs = k.s(tok::kCaption);
        const ImVec2 hz = text_size(k.regular, hs, hint);
        if (tx + ts.x + k.s(tok::kS3) + hz.x < pos.x + w - k.s(tok::kS3)) {
            label_at(k, dl, k.regular, hs, {tx, pos.y, pos.x + w - k.s(tok::kS3), pos.y + h},
                     LabelAlign::Right, col::kText3, hint);
        }
    }
    return pressed && !v.calibrating;
}

// "Tuning": the A4 in use, with a button back to 440 Hz when it is not.
void tuning_row(Kit& k, const InputView& v, int a4_tenths, InputPort& input) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = k.s(tok::kHit);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = pos.x + k.s(tok::kS3);
    glyph(k, dl, icon::kWave, {x + k.s(tok::kIcon) * 0.5f, pos.y + h * 0.5f},
          k.s(tok::kIcon) * 0.9f, col::kText2);
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    const float size = k.s(tok::kBody);
    label_at(k, dl, k.regular, size, {x, pos.y, pos.x + w, pos.y + h}, LabelAlign::Left, col::kText,
             "Tuning");
    info_button(k, "The pitch the microphone takes A4 to be: 440 Hz unless Learn tuning has found "
                   "your piano sits higher or lower.",
                {x + text_size(k.regular, size, "Tuning").x + k.s(kInfoOffset), pos.y + h * 0.5f});
    char text[32];
    std::snprintf(
        text, sizeof text, "A4 = %.1f Hz",
        static_cast<double>(v.capturing ? v.a4_hz : static_cast<float>(a4_tenths) / 10.0f));
    const float hit = k.s(tok::kHit);
    const bool reset_shown = a4_tenths != kDefaultMicA4Tenths;
    const float right = pos.x + w - (reset_shown ? hit : k.s(tok::kS3));
    label_at(k, dl, k.bold, size, {x, pos.y, right, pos.y + h}, LabelAlign::Right, col::kAccent,
             text);
    if (reset_shown) {
        ImGui::SetCursorScreenPos({pos.x + w - hit, pos.y});
        if (icon_action(k, "##reset-a4", icon::kReset, "Back to 440 Hz", hit, true)) {
            input.reset_tuning();
            k.say("Tuning: A4 = 440 Hz");
        }
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({w, h});
}

// The microphones as a list of tap targets, the system default first.
void microphone_rows(Kit& k, UiModel& model, const std::vector<std::string>& names) {
    AppSettings& app = model.app;
    catalogue_sweep::drawn(Setting::MicDevice);
    if (radio_row(k, "System default", nullptr, app.mic_device.empty())) {
        app.mic_device.clear();
        k.say("Microphone: system default");
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        const FitText shown = fit(k.regular, k.s(tok::kBody), names[i],
                                  ImGui::GetContentRegionAvail().x - k.s(60.0f));
        ImGui::PushID(static_cast<int>(i));
        if (radio_row(k, shown.c_str(), nullptr, app.mic_device == names[i])) {
            app.mic_device = names[i].substr(0, kMaxNameBytes);
            k.say("Microphone: " + names[i]);
        }
        ImGui::PopID();
    }
    if (names.empty()) {
        const float size = k.s(tok::kLabel);
        text_at(k, ImGui::GetWindowDrawList(), k.regular, size, ImGui::GetCursorScreenPos(),
                col::kText3, "No other microphone found.");
        ImGui::Dummy({1.0f, size + k.s(tok::kS2)});
    }
}

} // namespace

// Input. First column, the MIDI side: the source and the timing that applies to either, then the
// keyboard, its ports, the check and the computer keys, then the ABC tunes' accompaniment. Second
// column, the microphone: which one, its level, the noise floor, the tuning.
void page_input(Kit& k, UiModel& model, int columns) {
    AppSettings& app = model.app;
    Columns cols(k, columns);
    constexpr Seg kSources[] = {{nullptr, "MIDI"}, {nullptr, "Microphone"}, {nullptr, "Both"}};
    int source = static_cast<int>(app.input_mode);
    catalogue_sweep::drawn(Setting::InputSource);
    if (segmented(k, "source", icon::kKeyboard, "Source", kSources, &source)) {
        app.input_mode = static_cast<InputMode>(source);
        k.say(std::string("Input: ") + source_name(app.input_mode));
    }
    catalogue_sweep::drawn(Setting::Latency);
    slider_row_int(k, icon::kGauge, "Latency", &app.latency_correction_ms, -250, 250,
                   Unit::Milliseconds,
                   "Shifts when key presses count, to make up for the delay of the keyboard and "
                   "screen.", app_defaults().latency_correction_ms);
    section(k, "Keyboard");
    keyboard_rows(k, model);
    midi_port_rows(k, app);
    catalogue_sweep::drawn(Setting::DeviceCheck);
    if (nav_row(k, icon::kKeyboard, "Run the device check", "")) {
        k.run_device_check = true;
    }
    probe::add_rect("##sheet-device-check-run", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    catalogue_sweep::drawn(Setting::TypingLetters);
    switch_row(k, icon::kTyping, "Typing letters", &model.look.show_typing_keys,
               "Shows which computer key plays each piano key.");
    section(k, "ABC tunes");
    catalogue_sweep::drawn(Setting::AbcChords);
    switch_row(k, icon::kChord, "ABC chords on the left", &app.abc_chords,
               "ABC tunes with chord symbols get a left-hand part, root and fifth on each chord. "
               "Takes effect when a tune is opened.");
    cols.next();
    if (k.input == nullptr) {
        return;
    }
    InputPort& input = *k.input;
    section(k, "Microphone");
    microphone_rows(k, model, input.devices());
    meter_row(k, input.view());
    catalogue_sweep::drawn(Setting::CalibrateNoise);
    if (calibrate_row(k, input.view())) {
        if (input.view().capturing) {
            input.calibrate_noise(k.now_ns);
        } else {
            k.say("Choose Microphone or Both first");
        }
    }
    catalogue_sweep::drawn(Setting::LearnTuning);
    switch_row(k, icon::kWave, "Learn tuning", &app.mic_learn_tuning,
               "Follows the piano's tuning from the notes it hears.");
    catalogue_sweep::drawn(Setting::Tuning);
    tuning_row(k, input.view(), app.mic_a4_tenths, input);
}

} // namespace etude::app::ui
