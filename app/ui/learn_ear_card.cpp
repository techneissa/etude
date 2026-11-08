// SPDX-License-Identifier: GPL-3.0-or-later
// The ear card (learn_screen.hpp, ear_layout.hpp, app/ear_session.hpp): the piano plays a note, or a
// note and a second, and the player finds it on the keys, on screen or on a keyboard of their own.
// One try per question; Hint shows the answer and Next asks the next.
#include "learn_screen.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "learn_draw.hpp"
#include "learn_types.hpp"
#include "note_name.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace etude::app::ui {

namespace {

constexpr std::array<const char*, kEarModes> kModeIds{"##ear-mode-note", "##ear-mode-up",
                                                      "##ear-mode-down", "##ear-mode-together"};
constexpr std::array<const char*, kEarModes> kModeWords{"Note", "Interval up", "Interval down",
                                                        "Together"};
constexpr std::array<const char*, kEarModes> kModeIcons{icon::kEarNote, icon::kEarUp,
                                                        icon::kEarDown, icon::kEarTogether};
constexpr std::array<const char*, 12> kPitchNames{"C", "C#", "D",  "D#", "E",  "F",
                                                  "F#", "G",  "G#", "A",  "A#", "B"};

constexpr ImVec4 vec(RGBA c) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, 1.0f};
}

namespace ear_col {
// The answers' colours are the tokens of core learn_colours.hpp.
inline constexpr ImVec4 kRight = vec(kEarRightColour);
inline constexpr ImVec4 kWrong = vec(kEarWrongColour);
inline constexpr ImVec4 kMissed = vec(kEarMissedColour);
inline constexpr ImVec4 kStart = vec(kEarStartColour); // the note the question started from
inline constexpr ImVec4 kHint = vec(kEarHintColour);   // the key a hint shows
} // namespace ear_col

RGBA rgba(ImVec4 c) {
    return {static_cast<std::uint8_t>(c.x * 255.0f + 0.5f), static_cast<std::uint8_t>(c.y * 255.0f + 0.5f),
            static_cast<std::uint8_t>(c.z * 255.0f + 0.5f), 255};
}

const char* pitch_name(int note) { return kPitchNames[static_cast<std::size_t>(((note % 12) + 12) % 12)]; }

// "C4-E5": the notes the keys cover.
void range_words(FitText& out, const EarConfig& c) {
    const int low = c.low_note;
    const int top = ear_top_note(c);
    out.format("%s%d-%s%d", pitch_name(low), low / 12 - 1, pitch_name(top), top / 12 - 1);
}

// The largest interval's words: "1-12".
void size_words(FitText& out, int max) { out.format("1-%d", max); }

} // namespace

void LearnScreen::draw_ear(Kit& k, UiModel& model, LearnPort& port, const Box& area) {
    AppSettings& app = model.app;
    if (!ear_seeded_) {
        ear_ = EarSession(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
        ear_seeded_ = true;
    }
    EarNeeds needs;
    needs.tabs = learn_measure_tabs(k, 2);
    const float ub = learn_card_unit(area, k.u);
    for (int i = 0; i < kEarModes; ++i) {
        const std::size_t n = static_cast<std::size_t>(i);
        needs.mode[n] = chip_width(k, kModeIcons[n], kModeWords[n]) / ub;
    }
    const EarLayout e = ear_layout(area, k.u, needs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (draw_back(k, e.back, "##learn-back")) {
        page_ = Page::Gallery;
    }
    label_at(k, dl, k.bold, k.s(tok::kTitle), e.title, LabelAlign::Left, col::kText, "Ear");
    dl->AddLine({area.x0, e.header.y1}, {area.x1, e.header.y1}, u32(col::kHairline),
                k.s(tok::kHairline));
    draw_tabs(k, model.look, e.tabs, e.tab_label, 2);
    // The body is drawn in its own unit: the card grows with a larger window.
    const float window_unit = k.u;
    k.u = e.ub;
    EarConfig cfg;
    cfg.mode = app.learn_ear_mode;
    cfg.low_note = ear_c_note(app.learn_ear_octave);
    cfg.whites = e.whites;
    cfg.any_octave = app.learn_ear_any_octave;
    cfg.max_interval = app.learn_ear_max;
    if (ear_.configure(cfg)) {
        ear_failed_ = false;
        port.stop_sound();
        ear_sounding_ = false;
    }
    if (ear_.phase() == EarPhase::Idle && !ear_failed_) {
        if (ear_.next()) {
            ear_hear(k, port, ear_.question());
        } else {
            ear_failed_ = true;
            k.say_free(ear_.message(), true);
        }
    }
    // Presses on a keyboard of the player's own, since the last draw.
    for (int i = 0; i < ear_in_n_; ++i) {
        ear_press(k, port, ear_in_[static_cast<std::size_t>(i)], false);
    }
    ear_in_n_ = 0;
    // With the Settings card open, what it covers takes no taps and is not the audit's.
    const bool covered = e.short_form && ear_settings_open_;
    audit::set_quiet(covered);
    ImGui::BeginDisabled(covered);
    ear_left(k, model.look, port, e);
    ear_modes(k, app, e);
    const int tapped_key = ear_keys(k, e);
    if (tapped_key >= 0) {
        ear_press(k, port, tapped_key, true);
    }
    ear_foot(k, port, e);
    ImGui::EndDisabled();
    audit::set_quiet(false);
    if (e.short_form) {
        ear_settings_card(k, app, e);
    } else {
        ear_settings_open_ = false;
        ear_settings_row(k, app, e);
    }
    k.u = window_unit;
}

void LearnScreen::ear_left(Kit& k, const LookSettings& look, LearnPort& port, const EarLayout& e) {
    ear_streak(k, look, e);
    ear_pill(k, e);
    ear_disc(k, port, e);
}

// The flame and the streak's number, the ten dots under it.
void LearnScreen::ear_streak(Kit& k, const LookSettings& look, const EarLayout& e) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char number[16];
    std::snprintf(number, sizeof number, "%d", ear_.streak());
    const float size = k.s(tok::kDisplay);
    const float icon_size = k.s(tok::kIcon) * 1.3f;
    const float gap = k.s(tok::kS2);
    const float across = text_size(k.bold, size, number).x;
    const float total = icon_size + gap + across;
    const float x0 = (e.short_form ? e.streak.x0 : e.streak.cx() - total * 0.5f);
    const ImVec4 flame = ear_.streak() > 0 ? vec(learn_section_colour(look, LearnSection::Ear)) : col::kText3;
    glyph(k, dl, icon::kFlame, {x0 + icon_size * 0.5f, e.streak.cy()}, icon_size, flame);
    label_at(k, dl, k.bold, size, {x0 + icon_size + gap, e.streak.y0, x0 + total, e.streak.y1},
             LabelAlign::Left, ear_.streak() > 0 ? col::kText : col::kText2, number);
    // The dots: the last ten answers, filled from the left.
    const float d = e.dot;
    const float dot_gap = k.s(learn_tok::kEarDotGap);
    const float row = static_cast<float>(kEarDots) * d + static_cast<float>(kEarDots - 1) * dot_gap;
    const float dx = e.short_form ? e.dots.x0 : e.dots.cx() - row * 0.5f;
    for (int i = 0; i < kEarDots; ++i) {
        ImVec4 c = with_alpha(col::kText, 0.12f);
        switch (ear_.dots()[static_cast<std::size_t>(i)]) {
        case EarMark::Right:
            c = ear_col::kRight;
            break;
        case EarMark::Wrong:
            c = ear_col::kWrong;
            break;
        case EarMark::Hinted:
            c = col::kText2;
            break;
        case EarMark::None:
            break;
        }
        dl->AddCircleFilled({dx + d * 0.5f + static_cast<float>(i) * (d + dot_gap), e.dots.cy()},
                            d * 0.5f, u32(c), 12);
    }
}

// The interval's name in a pill: a question mark until the question is answered. A note question
// names the note.
void LearnScreen::ear_pill(Kit& k, const EarLayout& e) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool answered = ear_.phase() == EarPhase::Right || ear_.phase() == EarPhase::Wrong;
    const EarQuestion& q = ear_.question();
    const bool interval = ear_.config().mode != LearnEarMode::Note;
    const char* mark = ear_.config().mode == LearnEarMode::IntervalDown
                           ? icon::kEarDown
                           : ear_.config().mode == LearnEarMode::Together ? icon::kEarTogether
                                                                          : icon::kEarUp;
    const float size = k.s(tok::kBody);
    const float icon_size = k.s(tok::kIconSm);
    const float gap = k.s(tok::kS1);
    const char* words = answered ? ear_name_.c_str() : "?";
    const float words_w = answered && !interval ? note_name_width(k.bold, size, words)
                                                : text_size(k.bold, size, words).x;
    const float lead = interval ? icon_size + gap : 0.0f;
    const float pad = k.s(tok::kS3);
    const float w = std::min(e.pill.w(), std::max(lead + words_w + 2.0f * pad, e.pill.h() * 2.2f));
    const Box face{e.pill.cx() - w * 0.5f, e.pill.y0, e.pill.cx() + w * 0.5f, e.pill.y1};
    dl->AddRectFilled({face.x0, face.y0}, {face.x1, face.y1}, u32(col::kFill),
                      face.h() * 0.5f);
    const ImVec4 ink = ear_.phase() == EarPhase::Right ? ear_col::kRight : col::kText;
    const float x = face.cx() - (lead + words_w) * 0.5f;
    if (interval) {
        glyph(k, dl, mark, {x + icon_size * 0.5f, face.cy()}, icon_size, col::kText2);
    }
    const Box slot{x + lead, face.y0, x + lead + words_w, face.y1};
    if (answered && !interval) {
        note_name_at(k, dl, k.bold, size, slot, ink, words);
    } else {
        label_at(k, dl, k.bold, size, slot, LabelAlign::Centre, answered ? ink : col::kText2, words);
    }
    (void)q;
}

// The replay disc: a tap plays the question again. Rings go out from it while it sounds, and an
// answered question rings it in the answer's colour with a check or a cross.
void LearnScreen::ear_disc(Kit& k, LearnPort& port, const EarLayout& e) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* id = "##ear-replay";
    const bool can = ear_.phase() != EarPhase::Idle;
    const bool pressed = tapped(press_area(k, id, e.disc)) && can;
    const State st = item_state(k, false, true, !can);
    audit::note(id, {e.disc.x0, e.disc.y0}, {e.disc.x1, e.disc.y1}, InkKind::Target);
    audit::live({e.ring.x0, e.ring.y0}, {e.ring.x1, e.ring.y1}); // the rings breathe while it sounds
    const ImVec2 c{e.ring.cx(), e.ring.cy()};
    const float radius = e.disc.w() * 0.5f;
    const float pulse =
        ear_sounding_ ? 0.5f + 0.5f * std::sin(static_cast<float>(k.now - ear_t0_) * 7.0f) : 0.0f;
    // Two rings round the disc (they breathe while it sounds).
    const float ring_out = e.ring.w() * 0.5f;
    for (int i = 1; i <= 2; ++i) {
        const float r = radius + (ring_out - radius) * static_cast<float>(i) * 0.5f - k.s(tok::kHairline);
        dl->AddCircle(c, r + pulse * k.s(tok::kS1) * static_cast<float>(i),
                      u32(with_alpha(col::kAccent, (0.12f + 0.10f * static_cast<float>(i)) *
                                                       (1.0f + pulse))),
                      48, k.s(tok::kHairline));
    }
    dl->AddCircleFilled(c, radius, u32(col::kAccent), 48);
    draw_state(k, dl, id, e.disc, e.disc, st, Idle::None, radius);
    glyph(k, dl, icon::kSoundBoth, c, radius * 0.9f, col::kAccentInk);
    const bool done = ear_.phase() == EarPhase::Right || ear_.phase() == EarPhase::Wrong;
    if (done) {
        const bool right = ear_.phase() == EarPhase::Right;
        const ImVec4 verdict = right ? ear_col::kRight : ear_col::kWrong;
        dl->AddCircle(c, radius + k.s(tok::kS1), u32(verdict), 48, k.s(tok::kFocusRing) * 1.6f);
        const float badge = std::min(radius * 0.36f, k.s(tok::kHit) * 0.5f);
        const ImVec2 bc{c.x + radius * 0.72f, c.y - radius * 0.72f};
        dl->AddCircleFilled(bc, badge, u32(col::kScreenBg), 24);
        dl->AddCircle(bc, badge, u32(verdict), 24, k.s(tok::kFocusRing) * 1.6f);
        glyph(k, dl, right ? icon::kCheck : icon::kClose, bc, badge * 1.1f, verdict);
    }
    if (pressed) {
        if (const EarQuestion* q = ear_.replay()) {
            ear_hear(k, port, *q);
        }
    }
}

// Sounds `question` and keeps the frame loop drawing while it plays.
void LearnScreen::ear_hear(Kit& k, LearnPort& port, const EarQuestion& question) {
    std::string why;
    const double seconds = port.ask_ear(question, why);
    if (seconds <= 0.0) {
        k.say_free(why.empty() ? "That cannot be played." : why, true);
        return;
    }
    ear_sounding_ = true;
    ear_t0_ = k.now;
    ear_until_ = k.now + seconds;
}

// The range and the any-octave switch, and the largest interval: two rows (in the short form, in the
// card the Settings button opens).
void LearnScreen::ear_settings_row(Kit& k, AppSettings& app, const EarLayout& e) {
    FitText range;
    FitText size;
    range_words(range, ear_.config());
    size_words(size, app.learn_ear_max);
    const bool intervals = app.learn_ear_mode != LearnEarMode::Note;
    ChipOpts o;
    o.fixed = true;
    o.hit_h = e.range.h();
    o.face_h = e.range.h() * (tok::kFace / tok::kHit);
    const int step = draw_stepper(k, e.range, "##ear-range-down", "##ear-range-up",
                                  app.learn_ear_octave > 1, app.learn_ear_octave < 6, icon::kPiano,
                                  range.c_str(), nullptr, tok::kLabel);
    app.learn_ear_octave = std::clamp(app.learn_ear_octave + step, 1, 6);
    o.icon = icon::kAnyOctave;
    o.on = app.learn_ear_any_octave;
    o.width = e.any.w();
    if (chip(k, "##ear-any", "", {e.any.x0, e.any.y0}, o)) {
        app.learn_ear_any_octave = !app.learn_ear_any_octave;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                        ImGui::GetStyle().Alpha * (intervals ? 1.0f : tok::kDisabledAlpha));
    const int grow = draw_stepper(k, e.size, "##ear-size-down", "##ear-size-up",
                                  intervals && app.learn_ear_max > 2,
                                  intervals && app.learn_ear_max < 12, icon::kSize, size.c_str(),
                                  nullptr, tok::kLabel);
    ImGui::PopStyleVar();
    app.learn_ear_max = std::clamp(app.learn_ear_max + grow, 2, 12);
}

// The short form's one button, and the card it opens (a tap off it closes it).
void LearnScreen::ear_settings_card(Kit& k, AppSettings& app, const EarLayout& e) {
    ChipOpts o;
    o.icon = icon::kSettings;
    o.on = ear_settings_open_;
    o.width = e.settings.w();
    o.fixed = true;
    o.hit_h = e.settings.h();
    o.face_h = e.settings.h() * (tok::kFace / tok::kHit);
    if (chip(k, "##ear-settings", "Settings", {e.settings.x0, e.settings.y0}, o)) {
        ear_settings_open_ = !ear_settings_open_;
    }
    if (!ear_settings_open_) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const auto at = [&](const Box& b) {
        return io.MousePos.x >= b.x0 && io.MousePos.x <= b.x1 && io.MousePos.y >= b.y0 &&
               io.MousePos.y <= b.y1;
    };
    if (ImGui::IsMouseClicked(0) && !at(e.popover) && !at(e.settings)) {
        ear_settings_open_ = false;
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 solid = col::kSurface; // opaque: the disc and the keys are under it
    solid.w = 1.0f;
    ui::overlay_card(k, dl, {e.popover.x0, e.popover.y0}, {e.popover.x1, e.popover.y1},
                     k.s(tok::kRadiusLg), solid);
    press_area(k, "##ear-settings-card", e.popover); // the taps between its controls stop here
    ear_settings_row(k, app, e);
}

// The four modes, in a row of chips or, when their words do not fit, a strip that scrolls sideways.
void LearnScreen::ear_modes(Kit& k, AppSettings& app, const EarLayout& e) {
    const int current = static_cast<int>(app.learn_ear_mode);
    int tapped_mode = -1;
    if (e.strip) {
        std::array<const char*, kEarModes> ids{};
        std::array<const char*, kEarModes> words{};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            ids[i] = kModeIds[i];
            words[i] = kModeWords[i];
        }
        tapped_mode = draw_strip(k, e.mode_strip, ids.data(), words.data(), kEarModes, current);
    } else {
        for (int i = 0; i < kEarModes; ++i) {
            const std::size_t n = static_cast<std::size_t>(i);
            ChipOpts o;
            o.icon = kModeIcons[n];
            o.on = i == current;
            o.width = e.mode[n].w();
            o.fixed = true;
            o.large = true;
            o.hit_h = e.mode[n].h();
            o.face_h = e.mode[n].h() * (tok::kFace / tok::kHit);
            if (chip(k, kModeIds[n], kModeWords[n], {e.mode[n].x0, e.mode[n].y0}, o)) {
                tapped_mode = i;
            }
        }
    }
    if (tapped_mode >= 0 && tapped_mode != current) {
        app.learn_ear_mode = static_cast<LearnEarMode>(tapped_mode);
    }
}

namespace {

// A key's name at its foot, in the ink that reads on its fill, shrunk to fit the key.
void ear_key_name(Kit& k, const EarLayout& e, const Box& b, int note, ImVec4 fill) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = k.s(tok::kS1);
    const float want = std::min(e.white_w * learn_tok::kEarKeyLabelShare, k.s(tok::kTitle));
    const float across = note_name_width(k.bold, want, pitch_name(note));
    const float room = b.w() - 2.0f * pad;
    const float size = std::floor(across > room && across > 0.0f ? want * room / across : want);
    const float name_h = std::round(size * 1.6f);
    const float bottom = std::round(b.y1 - k.s(tok::kS2));
    note_name_at(k, dl, k.bold, size, {b.x0, bottom - name_h, b.x1, bottom},
                 vec(label_color_for(rgba(fill))), pitch_name(note));
}

// A mark in a key (a check, a cross or a target), over its name, in the ink that reads on its fill:
// as wide as the key allows (a black key is narrower) and in the room the name leaves.
void ear_key_mark(Kit& k, const EarLayout& e, const EarKeyBox& key, const char* mark, ImVec4 fill) {
    const Box& b = key.box;
    const float name_room = std::round(std::min(e.white_w * learn_tok::kEarKeyLabelShare, k.s(tok::kTitle)) * 1.6f);
    const float name_top = b.y1 - k.s(tok::kS2) - name_room;
    const float want = std::min(b.w() * (key.black ? 0.7f : 0.42f), k.s(tok::kHit) * 0.7f);
    const float size = std::max(0.0f, std::min(want, name_top - k.s(tok::kS2) - b.y0));
    glyph(k, ImGui::GetWindowDrawList(), mark, {b.cx(), name_top - k.s(tok::kS2) - size * 0.5f}, size,
          vec(label_color_for(rgba(fill))));
}

// What a key is to the question once it is answered: the key the player tapped (right: green; wrong:
// wine red), the right key a wrong tap missed (grey), or the note the question started from. Only
// those keys: with any octave right, the other octaves' keys of the same note stay as they were.
ImVec4 ear_key_fill(const EarSession& ear, const EarKeyBox& key) {
    switch (ear.key_role(key.note)) {
    case EarKeyRole::Tapped:
        return ear.phase() == EarPhase::Right ? ear_col::kRight : ear_col::kWrong;
    case EarKeyRole::Missed:
        return ear_col::kMissed;
    case EarKeyRole::Start:
        return ear_col::kStart;
    case EarKeyRole::Hint:
        return ear_col::kHint;
    case EarKeyRole::Plain:
        break;
    }
    return key.black ? learn_col::kBlackOut : learn_col::kWhiteOut;
}

// The keys painted (the one under a finger lit), then their names, the wrong tap's cross and the
// starting key's ring.
void paint_ear_keys(Kit& k, const EarSession& ear, const EarLayout& e, const EarKeys& keys,
                    int pressed_note) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hair = k.s(tok::kHairline);
    const float radius = k.s(tok::kInset);
    const bool done = ear.phase() == EarPhase::Right || ear.phase() == EarPhase::Wrong;
    for (int pass = 0; pass < 2; ++pass) { // the white keys, then the black over them
        for (std::size_t i = 0; i < keys.count; ++i) {
            const EarKeyBox& key = keys.key[i];
            if (key.black != (pass == 1)) {
                continue;
            }
            const Box& b = key.box;
            dl->AddRectFilled({b.x0, b.y0}, {key.black ? b.x1 : b.x1 - hair, b.y1},
                              u32(ear_key_fill(ear, key)), radius,
                              ImDrawFlags_RoundCornersBottom);
            if (key.black) {
                dl->AddRect({b.x0, b.y0}, {b.x1, b.y1}, u32(learn_col::kBlackLine), radius,
                            ImDrawFlags_RoundCornersBottom, hair);
            }
            if (key.note == pressed_note) {
                dl->AddRectFilled({b.x0, b.y0}, {b.x1, b.y1}, u32(col::kPressed), radius,
                                  ImDrawFlags_RoundCornersBottom);
            }
        }
    }
    for (std::size_t i = 0; i < keys.count; ++i) {
        const EarKeyBox& key = keys.key[i];
        const EarKeyRole role = ear.key_role(key.note);
        const bool tapped_key = role == EarKeyRole::Tapped;
        const bool missed = role == EarKeyRole::Missed;
        const ImVec4 fill = ear_key_fill(ear, key);
        // A key with a role is named, and carries a mark above its name: a check or a cross for the
        // tap, a target for the key missed, a speaker for the note heard first, the bulb of the hint.
        const char* mark = tapped_key ? (ear.phase() == EarPhase::Right ? icon::kCheck : icon::kClose)
                           : missed   ? icon::kPractice
                           : role == EarKeyRole::Start && done ? icon::kSoundBoth
                           : role == EarKeyRole::Hint          ? icon::kHint
                                                               : nullptr;
        if (mark != nullptr) {
            ear_key_name(k, e, key.box, key.note, fill);
            ear_key_mark(k, e, key, mark, fill);
        }
        if (key.note == ear.start_note() && !done) {
            const float r = std::min(e.white_w * 0.22f, k.s(tok::kHit) * 0.5f);
            dl->AddCircle({key.box.cx(), key.box.y1 - k.s(tok::kS3) - r}, r, u32(ear_col::kStart), 24,
                          k.s(tok::kFocusRing) * 1.6f);
        }
    }
}

} // namespace

// The keyboard: the keys of the window, one pressable surface that gives a tap to the key under it
// (`ear_key_at`: so no two hit areas overlap); the notes asked are on them. A right answer's key is
// dark green with a check in it, a wrong tap's wine red with a cross in it and the key it missed grey
// with a target, the note the interval started from lavender (a hollow ring until the answer);
// a hint tints the answer.
// Returns the note tapped, else -1.
int LearnScreen::ear_keys(Kit& k, const EarLayout& e) {
    const EarKeys keys = ear_keys_layout(e.keyboard, e.white_w, ear_.view_low(), ear_.view_top(), k.s(tok::kHit));
    const bool tapped_now = tapped(press_area(k, "##ear-keys-surface", e.keyboard));
    const bool down = ImGui::IsItemActive();
    const ImVec2 at = ImGui::GetIO().MousePos;
    for (std::size_t i = 0; i < keys.count; ++i) {
        const EarKeyBox& key = keys.key[i];
        char id[24];
        std::snprintf(id, sizeof id, "##ear-key-%d", key.note);
        audit::note(id, {key.hit.x0, key.hit.y0}, {key.hit.x1, key.hit.y1}, InkKind::Target);
    }
    const int under = ear_key_at(keys, at.x, at.y);
    paint_ear_keys(k, ear_, e, keys, down ? under : -1);
    return tapped_now ? under : -1;
}

// Hint (the answer's key tinted) and Next, centred under the keyboard.
void LearnScreen::ear_foot(Kit& k, LearnPort& port, const EarLayout& e) {
    (void)port;
    const bool done = ear_.phase() == EarPhase::Right || ear_.phase() == EarPhase::Wrong;
    const bool can_hint = ear_.phase() == EarPhase::Asked && !ear_.hinted();
    const auto button = [&](const char* id, const char* icon, const char* words, const Box& b,
                            bool enabled, bool primary, const ImVec4* icon_ink = nullptr,
                            bool ink_always = false) {
        const float grown = b.h() / k.s(tok::kHit);
        return action_button(k, id, icon, e.foot_words ? words : "", {b.x0, b.y0}, {b.w(), b.h()},
                             enabled, primary, false, false, nullptr, tok::kFace * grown,
                             tok::kLabel * grown, icon_ink, ink_always);
    };
    // The bulb takes the hint's amber while the button is held, and once the hint is shown.
    if (button("##ear-hint", icon::kHint, "Hint", e.hint, can_hint, false, &ear_col::kHint,
               ear_.phase() == EarPhase::Asked && ear_.hinted())) {
        ear_.hint();
    }
    if (button("##ear-next", icon::kNext, "Next", e.next, done, done)) {
        ear_next(k, port);
    }
}

void LearnScreen::ear_next(Kit& k, LearnPort& port) {
    if (ear_.next()) {
        ear_hear(k, port, ear_.question());
    }
}

// A key was pressed (tapped on screen, or on the player's keyboard): the answer. A tap sounds the
// key, a key of the player's own sounds itself. The pill's words are made when the question ends.
void LearnScreen::ear_press(Kit& k, LearnPort& port, int note, bool sound) {
    (void)k;
    if (sound) {
        port.sound_key(note);
    }
    const EarPhase before = ear_.phase();
    if (ear_.press(note) == before || before != EarPhase::Asked) {
        return;
    }
    const EarQuestion& q = ear_.question();
    if (q.kind == EarQuestionKind::Interval) {
        ear_name_.assign(interval_name(q.semitones()));
    } else {
        ear_name_.assign(pitch_name(q.first));
    }
}

} // namespace etude::app::ui
