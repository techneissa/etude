// SPDX-License-Identifier: GPL-3.0-or-later
#include "transport.hpp"

#include "bar_metrics.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "morph.hpp"

#include "etude/screen_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace etude::app::ui {

namespace {

constexpr float kHoldRepeatS = 0.35f; // a held skip repeats its step this often
constexpr float kPi = 3.14159265f;

// When each skip button last moved the song, for the nudge: the arrow turns a little the way it
// points and comes back. Index 0 back, 1 forward.
double g_skip_tapped[2] = {-10.0, -10.0};
constexpr double kNudgeS = 0.34;
constexpr float kNudgeRadians = 26.0f / 180.0f * kPi;

// The arrow's turn now: negative (anticlockwise) for back, positive for forward. A quick swing out
// that eases home, zero when effects are reduced.
float nudge_angle(const Kit& k, bool forward) {
    const double age = k.now - g_skip_tapped[forward ? 1 : 0];
    if (age < 0.0 || age >= kNudgeS || k.reduced_motion) {
        return 0.0f;
    }
    const auto u = static_cast<float>(age / kNudgeS);
    // sin(pi u)(1 - u) peaks near 0.58 at u = 0.36; scaled so the peak is the full nudge.
    const float swing = std::sin(kPi * u) * (1.0f - u) / 0.58f;
    return (forward ? 1.0f : -1.0f) * kNudgeRadians * swing;
}

void skip_moved(const Kit& k, bool forward) { g_skip_tapped[forward ? 1 : 0] = k.now; }

// A skip button: the press moves one step at once; held past the long-press time it repeats the
// step every kHoldRepeatS until let go.
void skip_button(Kit& k, SongPort& song, bool forward, ImVec2 centre, ImVec2 hit, float drawn,
                 bool live, const char* caption) {
    const SongView& v = song.view();
    const int seconds = forward ? v.skip_forward_s : v.skip_back_s;
    const char* const text = caption;
    const FacePlan plan = plan_face(k, centre, hit, drawn, drawn, text);
    const Box& face = plan.lay.face;
    ImGui::SetCursorScreenPos({centre.x - hit.x * 0.5f, centre.y - hit.y * 0.5f});
    const bool released = ImGui::InvisibleButton(forward ? "##fwd" : "##back", hit);
    const bool held = ImGui::IsItemActive() && live;
    const State st = item_state(k, false, live);
    char label[48];
    std::snprintf(label, sizeof label, "%s %d seconds", forward ? "Forward" : "Back", seconds);
    // A long press names it, also when its caption did not fit.
    if (live && !plan.lay.caption && held &&
        ImGui::GetIO().MouseDownDuration[0] > tok::kLongPress) {
        k.tip = label;
        k.tip_min = ImGui::GetItemRectMin();
        k.tip_max = ImGui::GetItemRectMax();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ic{face.cx(), face.cy()};
    draw_state(k, dl, forward ? "##fwd" : "##back", face, face_area(k, centre, hit), st);
    audit::note(forward ? "##fwd" : "##back", {face.x0, face.y0}, {face.x1, face.y1});
    skip_icon(k, dl, ImGui::GetItemID(), ic, k.s(tok::kIcon), seconds, forward,
              state_ink(st, col::kText2), nudge_angle(k, forward));
    if (plan.lay.caption) {
        label_at(k, dl, k.regular, plan.caption_size, plan.lay.caption_box, LabelAlign::Centre,
                 col::kText2, text);
    }
    if (!live) {
        return;
    }
    // A tap skips once, when the finger lifts; a swipe that starts here never skips.
    if (tapped(released)) {
        forward ? song.skip_forward(k.now_ns) : song.skip_back(k.now_ns);
        skip_moved(k, forward);
    }
    // The repeats of a hold that stays put: one each time it crosses the long-press time plus a
    // whole number of repeat periods.
    if (held) {
        for (int r = hold_repeats_now(kHoldRepeatS); r > 0; --r) {
            forward ? song.skip_forward(k.now_ns) : song.skip_back(k.now_ns);
            skip_moved(k, forward);
        }
    }
}

} // namespace

void skip_icon(const Kit& k, ImDrawList* dl, ImGuiID key, ImVec2 centre, float size, int seconds,
               bool forward, ImVec4 colour, float turn) {
    if (audit::icons_hidden()) {
        return;
    }
    const float r = size * 0.47f;
    const float stroke = std::max(1.5f, size * 0.08f);
    const ImU32 ink = u32(colour);
    // The arc runs 300 degrees and ends at 12 o'clock, where the arrowhead points the way it
    // turns: back from 10 o'clock anticlockwise, forward from 2 o'clock clockwise (y is down, so
    // clockwise is increasing angle). `turn` rotates ring and head about the centre; the number
    // stays upright.
    const float top = -0.5f * kPi;
    const float sweep = 300.0f / 180.0f * kPi;
    const float dir = forward ? 1.0f : -1.0f;
    const float end = top - dir * 0.10f; // stop short of the head so the stroke does not poke out
    const float head = size * 0.17f;
    // The arrowhead stands out above the ring, so the ink is taller on top than below and the
    // ring's centre is not the ink's. The icon, ring, head and number, moves so that the ink's
    // bounds, found from the same points that are drawn at rest, are centred on `centre`.
    {
        constexpr int kSteps = 60;
        float lo_x = 1e9f;
        float hi_x = -1e9f;
        float lo_y = 1e9f;
        float hi_y = -1e9f;
        const auto take = [&](float x, float y, float pad) {
            lo_x = std::min(lo_x, x - pad);
            hi_x = std::max(hi_x, x + pad);
            lo_y = std::min(lo_y, y - pad);
            hi_y = std::max(hi_y, y + pad);
        };
        for (int i = 0; i <= kSteps; ++i) {
            const float a = (end - dir * sweep) + dir * sweep * static_cast<float>(i) / kSteps;
            take(r * std::cos(a), r * std::sin(a), stroke * 0.5f);
        }
        take(dir * head * 1.05f, -r, 0.0f);
        take(-dir * head * 0.35f, -r - head, 0.0f);
        take(-dir * head * 0.35f, -r + head, 0.0f);
        centre.x -= (lo_x + hi_x) * 0.5f;
        centre.y -= (lo_y + hi_y) * 0.5f;
    }
    dl->PathArcTo(centre, r, end - dir * sweep + turn, end + turn, 40);
    dl->PathStroke(ink, ImDrawFlags_None, stroke);
    // The head: a solid triangle on the circle at 12 o'clock, pointing along the turn.
    const ImVec2 at{centre.x, centre.y - r};
    const float cs = std::cos(turn);
    const float sn = std::sin(turn);
    const auto place = [&](ImVec2 p) {
        const float dx = p.x - centre.x;
        const float dy = p.y - centre.y;
        return ImVec2{centre.x + dx * cs - dy * sn, centre.y + dx * sn + dy * cs};
    };
    dl->AddTriangleFilled(place({at.x + dir * head * 1.05f, at.y}),
                          place({at.x - dir * head * 0.35f, at.y - head}),
                          place({at.x - dir * head * 0.35f, at.y + head}), ink);
    // The step's seconds in the middle; a new step rolls in over the old one.
    roll_number(k, dl, key, seconds, centre, size, colour);
}

float hand_cell(const Kit& k, float cell) { return bar_metrics::hand_cell(cell, k.s(tok::kHit)); }

namespace {

// When each hand toggle last refused a tap (the only lit one), for the shake.
double g_hand_refused[2] = {-10.0, -10.0};
constexpr double kShakeS = 0.32;

const char* no_notes_hint(bool left, bool in_section) {
    return left ? (in_section ? "No left-hand notes in this section"
                              : "No left-hand notes in this song")
                : (in_section ? "No right-hand notes in this section"
                              : "No right-hand notes in this song");
}

// A hand with no notes in the song (or the looped section): drawn as the grey of an off hand but
// at this opacity, so it reads as unavailable rather than off.
constexpr float kUnavailableAlpha = 0.38f;

// Scales the opacity of everything `dl` drew since vertex `from`.
void fade_vertices_from(ImDrawList* dl, int from, float alpha) {
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImU32& c = dl->VtxBuffer[i].col;
        const auto a =
            static_cast<ImU32>(static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
        c = (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }
}

float shake_offset(const Kit& k, int which) {
    const double age = k.now - g_hand_refused[which];
    if (age < 0.0 || age > kShakeS || k.reduced_motion) {
        return 0.0f;
    }
    const auto decay = static_cast<float>(1.0 - age / kShakeS);
    return std::sin(static_cast<float>(age) * 62.0f) * decay * k.s(4.0f);
}

// Where a hand toggle's letter (L or R) goes in the toggle's square `half` around `ic`, and how the
// hand moves to make room for it. The hand and its letter are one icon: they are centred as one,
// by their ink, in the square.
struct HandMark {
    ImVec2 text;          // top left of the letter's line box
    float size = 0.0f;    // bold font size
    float hand_dx = 0.0f; // the hand's shift, positive to the right
    float hand_dy = 0.0f; // and down
};

// The letter sits in the outer bottom corner: bottom left for the left hand, bottom right for the
// right, where the thumbs (pointing inward) leave room. Its ink stays at least 2 points inside the
// square's rounded border: the corner of its box sits on the corner arc pulled in by 2 points. The
// hand then moves inward until its palm clears the letter, but never so far its thumb leaves the
// square. Last, the pair of them moves so that their joint ink is centred on `ic`.
HandMark hand_mark(const Kit& k, bool left, ImVec2 ic, float half) {
    HandMark m;
    m.size = k.s(tok::kMark);
    const char* text = left ? "L" : "R";
    const ImVec2 ts = text_size(k.bold, m.size, text);
    const float ascent = k.bold->GetFontBaked(m.size)->Ascent; // line top to baseline
    const float margin = k.s(2.0f);
    const float r = std::min(k.s(tok::kRadiusControl), half);
    const float a = std::max(margin, r - (r - margin) * 0.7071f); // the box corner's inset
    const float baseline = ic.y + half - a;
    m.text = {left ? ic.x - half + a : ic.x + half - a - ts.x, baseline - ascent};
    ImVec2 ink_min{0.0f, 0.0f};
    ImVec2 ink_max{ts.x, ts.y};
    font_ink(k.bold, m.size, text[0], &ink_min, &ink_max);
    // The hand's ink reaches `hw` and `hh` from its centre.
    const MorphGlyph hand = left ? MorphGlyph::LeftHand : MorphGlyph::RightHand;
    const float unit = k.s(tok::kIcon) * glyph_style(hand).scale / 24.0f;
    const GlyphExtent ext = glyph_extent(hand);
    const float hw = ext.half_w * unit;
    const float hh = ext.half_h * unit;
    const float inner = left ? m.text.x + ink_max.x : m.text.x + ink_min.x; // toward the hand
    const float need = left ? inner + k.s(1.5f) + hw - ic.x : ic.x - (inner - k.s(1.5f) - hw);
    const float room = std::max(0.0f, half - margin - hw);
    const float dx = std::clamp(need, 0.0f, room);
    m.hand_dx = left ? dx : -dx;
    // The joint ink of the hand and its letter, and where its centre is.
    const float x0 = std::min(ic.x + m.hand_dx - hw, m.text.x + ink_min.x);
    const float x1 = std::max(ic.x + m.hand_dx + hw, m.text.x + ink_max.x);
    const float y0 = std::min(ic.y - hh, m.text.y + ink_min.y);
    const float y1 = std::max(ic.y + hh, m.text.y + ink_max.y);
    const float sx = ic.x - (x0 + x1) * 0.5f;
    const float sy = ic.y - (y0 + y1) * 0.5f;
    m.hand_dx += sx;
    m.hand_dy = sy;
    m.text.x += sx;
    m.text.y += sy;
    return m;
}

void hand_toggle(Kit& k, SongPort& song, bool left, float x, float cy, float width, float hit_h,
                 float drawn, bool live, bool caption) {
    const SongView& v = song.view();
    const bool on = left ? v.left_hand : v.right_hand;
    const bool other = left ? v.right_hand : v.left_hand;
    const bool available = left ? v.left_available : v.right_available;
    const bool tap = hand_toggle_face(k, left ? "##hand-left" : "##hand-right", left, x, cy, width,
                                      hit_h, drawn, on, available, live, caption);
    if (!tap) {
        return;
    }
    if (!available) {
        // The toast, under the bar next to the toggle: the status line is too narrow for it while
        // the bar shows.
        k.say(no_notes_hint(left, v.hands_in_section));
        return;
    }
    if (on && !other) {
        hand_toggle_refused(left, k.now); // one hand always stays on
        return;
    }
    song.set_hands(left ? !on : v.left_hand, left ? v.right_hand : !on, k.now_ns);
}

} // namespace

void hand_toggle_refused(bool left, double now) { g_hand_refused[left ? 0 : 1] = now; }

bool hand_toggle_face(Kit& k, const char* id, bool left, float x, float cy, float width, float hit_h,
                      float drawn, bool on, bool available, bool live, bool caption) {
    const int which = left ? 0 : 1;
    const ImVec4 tint = left ? k.hand_left : k.hand_right;
    const char* label = left ? "Left hand" : "Right hand";
    const char* unavailable_label = left ? "Left hand: no notes" : "Right hand: no notes";
    // The face icon_toggle will draw, so the hand and its letter are centred in it as one.
    const char* caption_text = caption ? (left ? "Left" : "Right") : nullptr;
    const Box face =
        plan_face(k, {x + width * 0.5f, cy}, {width, hit_h}, drawn, drawn, caption_text).lay.face;
    const HandMark mark = hand_mark(k, left, {face.cx(), face.cy()}, face.w() * 0.5f);
    ImDrawList* const list = ImGui::GetWindowDrawList();
    const int first_vertex = list->VtxBuffer.Size;
    const bool tap = icon_toggle(
        k, id, left ? icon::kLeftHand : icon::kRightHand,
        caption ? nullptr : (available ? label : unavailable_label), {x + width * 0.5f, cy},
        {width, hit_h}, drawn, on, live, caption_text, &tint, mark.hand_dx + shake_offset(k, which),
        0.0f, mark.hand_dy);
    {
        // The letter stays still and fades between the tint and grey with the hand.
        const float sel = tween(ImGui::GetID(left ? "##hand-left-letter" : "##hand-right-letter"),
                                on ? 1.0f : 0.0f, tok::kFast);
        if (!audit::icons_hidden()) {
            text_at(k, ImGui::GetWindowDrawList(), k.bold, mark.size, mark.text,
                    mix(col::kText3, tint, sel), left ? "L" : "R");
        }
    }
    if (k.reduced_motion && k.now - g_hand_refused[which] < kShakeS) {
        // No movement: a brief red outline on the face says the same.
        ImGui::GetWindowDrawList()->AddRect(
            {face.x0, face.y0}, {face.x1, face.y1}, u32(col::kWarning),
            std::min(k.s(tok::kRadiusControl), face.w() * 0.5f), 0, 1.5f);
    }
    if (!available) {
        fade_vertices_from(list, first_vertex, kUnavailableAlpha);
    }
    return tap;
}


void song_control(Kit& k, SongPort& song, bar_metrics::Ctl c, ImVec2 centre, ImVec2 hit,
                  float drawn, bool live, bool prominent, const char* caption) {
    using bar_metrics::Ctl;
    const SongView& v = song.view();
    switch (c) {
    case Ctl::Replay:
        if (icon_toggle(k, "##replay", icon::kReplay, caption != nullptr ? nullptr : "Replay",
                        centre, hit, drawn, false, live, caption)) {
            song.replay(k.now_ns);
        }
        break;
    case Ctl::Back:
    case Ctl::Forward:
        skip_button(k, song, c == Ctl::Forward, centre, hit, drawn, live, caption);
        break;
    case Ctl::Play: {
        const char* label = v.playing ? "Pause" : "Play";
        if (prominent) {
            // The primary action: a face filled with the accent, the same shape as every face.
            const Box face = plan_face(k, centre, hit, drawn, drawn, caption).lay.face;
            ImGui::SetCursorScreenPos({centre.x - hit.x * 0.5f, centre.y - hit.y * 0.5f});
            const bool pressed = ImGui::InvisibleButton("##play", hit);
            const State st = item_state(k, false, live);
            if (live) {
                long_press_tip(k, label);
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 fc{face.cx(), face.cy()};
            draw_state(k, dl, "##play", face, face_area(k, centre, hit), st, Idle::Accent);
            audit::note("##play", {face.x0, face.y0}, {face.x1, face.y1},
                        v.playing ? InkKind::Centred : InkKind::PlayTriangle);
            morph_glyph(k, dl, ImGui::GetItemID(), v.playing ? MorphGlyph::Pause : MorphGlyph::Play,
                        fc, k.s(tok::kIcon), state_ink(st, col::kAccentInk, true));
            if (pressed && live) {
                song.toggle_play(k.now_ns);
            }
        } else if (icon_toggle(k, "##play", v.playing ? MorphGlyph::Pause : MorphGlyph::Play,
                               caption != nullptr ? nullptr : label, centre, hit, drawn, false,
                               live, caption)) {
            song.toggle_play(k.now_ns);
        }
        break;
    }
    case Ctl::Sound: {
        // The song's sound: mute (the default), the other hand, both hands.
        static constexpr MorphGlyph kSoundIcon[] = {MorphGlyph::Mute, MorphGlyph::SoundOther,
                                                    MorphGlyph::SoundBoth};
        static constexpr const char* kSoundLabel[] = {"Song muted", "Song plays the other hand",
                                                      "Song plays both hands"};
        const auto si = static_cast<std::size_t>(v.sound);
        if (icon_toggle(k, "##sound", kSoundIcon[si], caption != nullptr ? nullptr : kSoundLabel[si],
                        centre, hit, drawn, v.sound != SongSound::Mute, live, caption)) {
            auto step = (si + 1) % 3;
            if (step == static_cast<std::size_t>(SongSound::OtherHand) &&
                !(v.left_available && v.right_available)) {
                step = (step + 1) % 3; // "the other hand" needs two hands with notes
            }
            const auto next = static_cast<SongSound>(step);
            song.set_sound(next, k.now_ns);
            static constexpr const char* kSaid[] = {"Song sound off", "Song plays the other hand",
                                                    "Song plays both hands"};
            k.say(kSaid[static_cast<std::size_t>(next)]);
        }
        break;
    }
    case Ctl::LeftHand:
    case Ctl::RightHand:
        hand_toggle(k, song, c == Ctl::LeftHand, centre.x - hit.x * 0.5f, centre.y, hit.x, hit.y,
                    drawn, live, caption != nullptr);
        break;
    default:
        break;
    }
}

void mode_chip(Kit& k, SongPort& song, ImVec2 pos, ImVec2 hit, float drawn, ModeLook look,
               bool live) {
    const SongView& v = song.view();
    const bool wait = v.mode == PlayMode::Wait;
    const char* icon = wait ? icon::kSeconds : icon::kTrail;
    const char* name = wait ? "Wait" : "Flow";
    const char* label = "Flow or Wait: Wait holds the song at each chord";
    bool tap = false;
    switch (look) {
    case ModeLook::Chip: {
        // The bar's cell decides the hit area and the face it leaves.
        ChipOpts o;
        o.icon = icon;
        o.live = live;
        o.width = hit.x;
        o.hit_h = hit.y;
        o.face_h = drawn;
        tap = chip(k, "##mode", name, pos, o);
    }
        long_press_tip(k, label);
        audit::note("##mode", pos, {pos.x + hit.x, pos.y + hit.y}, InkKind::Target);
        break;
    case ModeLook::Icon:
    case ModeLook::Captioned:
        tap = icon_toggle(k, "##mode", icon, label, {pos.x + hit.x * 0.5f, pos.y + hit.y * 0.5f},
                          hit, drawn, false, live,
                          look == ModeLook::Captioned ? bar_metrics::mode_caption(wait) : nullptr);
        break;
    }
    if (tap) {
        song.set_mode(wait ? PlayMode::Flow : PlayMode::Wait, k.now_ns);
        k.say(wait ? "Flow: the song plays on and grades each note"
                   : "Wait: the song waits for each chord");
    }
}

void countdown_indicator(Kit& k, const SongView& song, const TopIndicator& t, float font) {
    const double left = song.countdown_left_s;
    if (left <= 0.0 || !t.visible()) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 c{t.rect.x + t.rect.w * 0.5f, t.rect.y + t.rect.h * 0.5f};
    const int number = static_cast<int>(std::ceil(left));
    // Within the current second: 0 as the number appears, 1 as it ends.
    const auto in_second = static_cast<float>(1.0 - (left - std::floor(left - 1e-9)));
    const float appear =
        k.reduced_motion ? 1.0f : ease_out(std::min(1.0f, in_second / tok::kSheet));
    // A thin ring that empties over the whole countdown, smoothly; the number is what ticks.
    const float width = k.s(2.0f);
    const float r = t.ring * 0.5f - width * 0.5f;
    dl->AddCircle(c, r, u32({1, 1, 1, 0.12f}), 32, width);
    const float frac = static_cast<float>(left / std::max(1, song.countdown_total_s));
    const float a0 = -0.5f * kPi;
    dl->PathArcTo(c, r, a0, a0 + 2.0f * kPi * std::min(1.0f, frac), 32);
    dl->PathStroke(u32(col::kAccent), 0, width);
    char text[16];
    std::snprintf(text, sizeof text, "%d", number);
    label_at(k, dl, k.bold, font, {c.x - r, c.y - r, c.x + r, c.y + r}, LabelAlign::Centre,
             with_alpha(col::kText, 0.6f + 0.4f * appear), text);
}

} // namespace etude::app::ui
