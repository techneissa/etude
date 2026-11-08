// SPDX-License-Identifier: GPL-3.0-or-later
#include "transpose.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "item_probe.hpp"
#include "transpose_card.hpp"
#include "transpose_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude::app::ui {

namespace {

constexpr int kOctave = 12;

// One row of the strip: the song's range after `shift` semitones, `octaves` from the shift in
// force (the label), `current` for the one in force.
struct StripBar {
    int shift = 0;
    int octaves = 0;
    bool current = false;
};

bool black_key(int pitch) {
    const int pc = ((pitch % kOctave) + kOctave) % kOctave;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// Where a key lies along a row of keys, in white keys from C0: a white key is one wide, a black key
// a little over half of one, centred on the line between its two white neighbours.
constexpr float kBlackWidth = 0.6f;
float white_keys_below(int pitch) {
    constexpr int kBelow[kOctave] = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
    const int octave = pitch >= 0 ? pitch / kOctave : -((-pitch + kOctave - 1) / kOctave);
    return static_cast<float>(octave * 7 + kBelow[pitch - octave * kOctave]);
}
float key_left(int pitch) {
    return white_keys_below(pitch) - (black_key(pitch) ? kBlackWidth * 0.5f : 0.0f);
}
float key_right(int pitch) {
    return white_keys_below(pitch) + (black_key(pitch) ? kBlackWidth * 0.5f : 1.0f);
}

// The keyboard and the song's range against it, drawn into `slot` (`strip_height` tall) and nothing
// else. The keys run over the span from the lowest to the highest of the keyboard's range and the
// song's range at every row; where the keyboard has no keys they are dim and hatched. Under them
// one row for each option, its short label at the left and a bar aligned to the keys: the song's
// range at that shift, in the accent where the keyboard has keys for it and in the warning colour
// where it has none. The option in force has its row lit and its label in the accent.
void draw_strip(const Kit& k, ImDrawList* dl, const Box& slot, const ShiftFits& fits,
                const std::span<const StripBar> bars) {
    if (fits.notes <= 0) {
        return;
    }
    const int board_lo = fits.keyboard.lowest;
    const int board_hi = fits.keyboard.highest;
    int lo = board_lo;
    int hi = board_hi;
    for (const StripBar& b : bars) {
        lo = std::min(lo, fits.lowest + b.shift);
        hi = std::max(hi, fits.highest + b.shift);
    }
    lo = std::max(lo, 0);
    hi = std::min(hi, 127);
    const float label_w = k.s(tok::kStripLabel);
    const float x0 = slot.x0 + label_w + k.s(tok::kS1);
    const float x1 = slot.x1;
    const float from_x = key_left(lo);
    const float to_x = key_right(hi);
    const float per = (x1 - x0) / (to_x - from_x);
    const auto x_of = [&](float keys) { return x0 + (keys - from_x) * per; };
    const float keys_h = k.s(tok::kStripKeys);
    const float hairline = k.s(tok::kHairline);
    // The keys: white first, black over them; those the keyboard lacks dim.
    for (int p = lo; p <= hi; ++p) {
        if (!black_key(p)) {
            const float alpha = fits.keyboard.contains(p) ? 0.92f : 0.2f;
            dl->AddRectFilled({x_of(key_left(p)), slot.y0},
                              {x_of(key_right(p)) - hairline, slot.y0 + keys_h},
                              u32({0.80f, 0.83f, 0.90f, alpha}));
        }
    }
    for (int p = lo; p <= hi; ++p) {
        if (black_key(p)) {
            const float alpha = fits.keyboard.contains(p) ? 1.0f : 0.5f;
            dl->AddRectFilled({x_of(key_left(p)), slot.y0},
                              {x_of(key_right(p)), slot.y0 + keys_h * 0.62f},
                              u32({0.12f, 0.14f, 0.19f, alpha}));
        }
    }
    // Hatching over what the keyboard lacks, so it reads as "no keys here" and not as quiet keys.
    const float edge_lo = x_of(key_left(board_lo));
    const float edge_hi = x_of(key_right(board_hi));
    const auto hatch = [&](float a, float z) {
        if (z - a < hairline) {
            return;
        }
        dl->PushClipRect({a, slot.y0}, {z, slot.y0 + keys_h}, true);
        const float step = k.s(tok::kS1) * 1.5f;
        for (float x = a - keys_h; x < z; x += step) {
            dl->AddLine({x, slot.y0 + keys_h}, {x + keys_h, slot.y0},
                        u32({1.0f, 1.0f, 1.0f, 0.35f}), hairline);
        }
        dl->PopClipRect();
    };
    hatch(x_of(from_x), edge_lo);
    hatch(edge_hi, x_of(to_x));
    // The option rows.
    float y = slot.y0 + keys_h + k.s(tok::kS1);
    const float row_h = k.s(tok::kStripRow);
    const float bar_h = k.s(tok::kStripBar);
    const float left_edge = key_left(board_lo);
    const float right_edge = key_right(board_hi);
    for (const StripBar& b : bars) {
        if (b.current) {
            dl->AddRectFilled({slot.x0, y}, {slot.x1, y + row_h}, u32(col::kSelected),
                              row_h * 0.5f);
        }
        char label[text::kShort];
        text::shift(label, sizeof label, b.octaves);
        label_at(k, dl, b.current ? k.bold : k.regular, k.s(tok::kCaption),
                 {slot.x0 + k.s(tok::kS1), y, slot.x0 + label_w, y + row_h}, LabelAlign::Left,
                 b.current ? col::kAccent : col::kText2, label);
        const int from = std::clamp(fits.lowest + b.shift, lo, hi);
        const int to = std::clamp(fits.highest + b.shift, lo, hi);
        const float a = key_left(from);
        const float z = key_right(to);
        const float top = y + (row_h - bar_h) * 0.5f;
        const float round = bar_h * 0.5f;
        // Left of the keys, on them, right of them: each part in its colour.
        if (a < left_edge) {
            dl->AddRectFilled({x_of(a), top}, {x_of(std::min(z, left_edge)), top + bar_h},
                              u32(col::kWarning), round);
        }
        if (std::max(a, left_edge) < std::min(z, right_edge)) {
            dl->AddRectFilled({x_of(std::max(a, left_edge)), top},
                              {x_of(std::min(z, right_edge)), top + bar_h}, u32(col::kAccent),
                              round);
        }
        if (z > right_edge) {
            dl->AddRectFilled({x_of(std::max(a, right_edge)), top}, {x_of(z), top + bar_h},
                              u32(col::kWarning), round);
        }
        y += row_h + k.s(tok::kS1);
    }
}

// A cell of two centred lines (a title over a detail) that can be pressed: the octave moves.
// Returns true on a tap.
bool option_cell(Kit& k, const char* id, const Box& area, const char* title, const char* detail,
                 bool on, bool enabled) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = press_area(k, id, area);
    ImGui::EndDisabled();
    const State st = item_state(k, on, true, !enabled);
    const float fh = k.s(tok::kFace);
    const Box face{area.x0, area.cy() - fh * 0.5f, area.x1, area.cy() + fh * 0.5f};
    draw_state(k, dl, id, face, area, st, Idle::Fill);
    audit::note(id, {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
    const float ts = k.s(tok::kLabel);
    const float ds = k.s(tok::kCaption);
    const bool two = detail != nullptr && detail[0] != '\0';
    const float split = face.y0 + face.h() * 0.5f;
    label_at(k, dl, on ? k.bold : k.regular, ts, {face.x0, face.y0, face.x1, two ? split : face.y1},
             LabelAlign::Centre, state_ink(st, col::kText), title);
    if (two) {
        label_at(k, dl, k.regular, ds, {face.x0, split, face.x1, face.y1}, LabelAlign::Centre,
                 state_ink(st, col::kText2), detail);
    }
    return enabled && tapped(pressed);
}

// The bars of the strip for the shift in force: it, and each whole-octave move within -12..+12.
int strip_bars(int current, std::array<StripBar, kStripMaxBars>& out) {
    int n = 0;
    for (int o = -1; o <= 1; ++o) {
        const int shift = current + o * kOctave;
        if (shift >= -kMaxTransposeSemitones && shift <= kMaxTransposeSemitones) {
            out[static_cast<std::size_t>(n++)] = {shift, o, o == 0};
        }
    }
    return n;
}

// What the offer proposes (a move of `shift.octaves` octaves of the keys, to `shift.target`) over
// what it leaves off the keyboard, in `slot` at the popup rule's steps for `shift.step`. With no
// `fits` the keys follow the player's keyboard and there is nothing left off.
struct OfferShift {
    int from;
    int target;
    int octaves;
    int step;
};

void draw_offer_words(Kit& k, ImDrawList* dl, const etude::ShiftFits* fits, const OfferShift& shift,
                      const Box& slot) {
    char title[text::kLong];
    char detail[text::kLong];
    if (fits == nullptr) {
        text::keyboard_shifted(title, sizeof title, shift.octaves);
        std::snprintf(detail, sizeof detail, "The keys will follow what it sends");
    } else {
        text::option(title, sizeof title, shift.octaves, fits->outside_at(shift.target));
        char now[text::kShort];
        text::outside(now, sizeof now, fits->outside_at(shift.from));
        std::snprintf(detail, sizeof detail, "%s now", now);
    }
    const float title_size = k.s(tok::popup_type(tok::kLabel, shift.step, tok::PopupText::Title));
    const float detail_size = k.s(tok::popup_type(tok::kCaption, shift.step, tok::PopupText::Text));
    const FitText top_cut = fit(k.bold, title_size, title, slot.w());
    const FitText bottom_cut = fit(k.regular, detail_size, detail, slot.w());
    const StackLine top{k.bold, title_size, col::kText, top_cut.c_str()};
    const StackLine bottom{k.regular, detail_size, col::kText2, bottom_cut.c_str()};
    label_stack(k, dl, slot, top, &bottom);
}

} // namespace

void TransposeMenu::popover(Kit& k, SongPort& song, const Frame& f, float below_y, float anchor_x) {
    const SongView& v = song.view();
    if (!v.has_song) {
        open_ = false;
    }
    open_p_ = approach(open_p_, open_ ? 1.0f : 0.0f, k.dt, tok::kMove);
    if (open_p_ <= 0.0f) {
        return;
    }
    const etude::ShiftFits* fits = v.shift_fits;
    if (fits == nullptr || fits->notes <= 0) {
        strip_ = false; // nothing to show against the keys
    }
    std::array<StripBar, kStripMaxBars> bars{};
    const int bar_count = strip_bars(v.transpose, bars);
    const TransposeCardPlan plan =
        plan_transpose_card(f.display.x, below_y, f.keyboard_top, k.u, strip_, bar_count);
    const float t = ease_out(open_p_);
    const float w = plan.w;
    // The card's left edge is a whole pixel: every chip's face is placed from it, and a face on a
    // fractional x puts its icon up to half a pixel off the face's centre (the + stepper was 0.56 px
    // off at 1920x1080 x1.0, where anchor_x - w/2 lands on .74).
    const float x = std::round(
        std::clamp(anchor_x - w * 0.5f, k.s(tok::kS3), f.display.x - w - k.s(tok::kS3)));
    const float y = below_y + (1.0f - t) * -k.s(tok::kS2);
    const float shown_h = std::max(0.0f, std::min(plan.max_h, plan.h));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    // The card is its own window: the bar's window would clip it. One that does not fit scrolls.
    if (begin_layer("##transpose-card", {x, y}, {w, shown_h}, true)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        const float hit = k.s(tok::kHit);
        const float gap = plan.gap;       // between two rows
        const float side = k.s(tok::kHitGap); // between two cells of a row: the dead gap
        const float left = x + plan.pad;
        const float right = x + w - plan.pad;
        float row = y + plan.pad;

        // The stepper: minus, the shift in force with its name, plus; then Keys (the fit strip) and
        // Reset (only while a shift is in force), each an icon.
        {
            const bool at_low = v.transpose <= -kMaxTransposeSemitones;
            const bool at_high = v.transpose >= kMaxTransposeSemitones;
            const float tools = right - 2.0f * hit - side; // Keys, then Reset at `right - hit`
            const float plus_x = tools - 2.0f * side - hit;
            if (chip(k, "##transpose-minus", "", {left, row},
                     {icon::kMinus, nullptr, false, !at_low})) {
                song.set_transpose(v.transpose - 1, k.now_ns);
            }
            if (chip(k, "##transpose-plus", "", {plus_x, row},
                     {icon::kPlus, nullptr, false, !at_high})) {
                song.set_transpose(v.transpose + 1, k.now_ns);
            }
            char value[text::kShort];
            text::shift(value, sizeof value, song.view().transpose);
            const Box mid{left + hit + side, row, plus_x - side, row + hit};
            const float split = mid.y0 + mid.h() * 0.4f;
            label_at(k, dl, k.regular, k.s(tok::kCaption), {mid.x0, mid.y0, mid.x1, split},
                     LabelAlign::Centre, col::kText3, "Transpose");
            label_at(k, dl, k.bold, k.s(tok::kTitle), {mid.x0, split, mid.x1, mid.y1},
                     LabelAlign::Centre, v.transpose != 0 ? col::kAccent : col::kText, value);
            ChipOpts keys;
            keys.icon = icon::kPiano;
            keys.on = strip_;
            keys.live = fits != nullptr && fits->notes > 0;
            if (chip(k, "##transpose-keys", "", {tools, row}, keys)) {
                strip_ = !strip_;
            }
            if (song.view().transpose != 0) {
                ChipOpts reset;
                reset.icon = icon::kReset;
                if (chip(k, "##transpose-reset", "", {right - hit, row}, reset)) {
                    song.set_transpose(0, k.now_ns);
                }
            }
            row += hit + gap;
        }

        // The whole-octave moves, each with what it leaves off the keyboard.
        {
            constexpr int kMoves[3] = {-1, 0, 1};
            const float cell = (right - left - 2.0f * side) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                const int target = v.transpose + kMoves[i] * kOctave;
                const bool reachable =
                    target >= -kMaxTransposeSemitones && target <= kMaxTransposeSemitones;
                char title[text::kShort];
                char detail[text::kShort] = "";
                text::octaves(title, sizeof title, kMoves[i]);
                if (reachable && fits != nullptr && fits->notes > 0) {
                    text::outside(detail, sizeof detail, fits->outside_at(target));
                }
                char id[32];
                std::snprintf(id, sizeof id, "##transpose-oct%d", i);
                const Box area{left + static_cast<float>(i) * (cell + side), row,
                               left + static_cast<float>(i) * (cell + side) + cell, row + hit};
                if (option_cell(k, id, area, title, detail, kMoves[i] == 0, reachable) &&
                    kMoves[i] != 0) {
                    song.set_transpose(target, k.now_ns);
                    char said_text[text::kLong];
                    text::option(said_text, sizeof said_text, kMoves[i],
                                 fits != nullptr ? fits->outside_at(target) : 0);
                    k.say(said_text);
                }
            }
            row += hit;
        }

        if (strip_ && fits != nullptr) {
            draw_strip(k, dl, {left, row + gap, right, row + gap + strip_height(bar_count, k.u)},
                       *fits,
                       std::span<const StripBar>(bars.data(), static_cast<std::size_t>(bar_count)));
        }

        drag_scroll(k);
        dl->ChannelsSetCurrent(0);
        const ImVec2 wp = ImGui::GetWindowPos();
        k.toast_avoid({wp.x, wp.y, w, shown_h});
        overlay_card(k, dl, wp, {wp.x + w, wp.y + shown_h}, k.s(tok::kRadiusCard));
        dl->ChannelsMerge();
        probe::add_rect("##transpose-card-rect", wp, {wp.x + w, wp.y + shown_h});
        if (tapped_outside(outside_, open_, f, wp, {wp.x + w, wp.y + shown_h})) {
            open_ = false;
        }
    }
    ImGui::End();
    if (!open_) {
        block_input("##transpose-card"); // sliding closed: it takes no input
    }
    ImGui::PopStyleVar();
}

void ShiftOfferCard::draw(Kit& k, SongPort& song, const Frame& f, float below_y, bool allowed) {
    const SongView& v = song.view();
    const etude::ShiftFits* fits = v.shift_fits;
    // An offer, the song's or the keys', waits while the song plays and shows when it is paused or
    // the run ends: it never sits over the falling notes. The keys' offer is about the keyboard,
    // not the song, so it needs no notes.
    const bool fits_ok = fits != nullptr && fits->notes > 0;
    const bool want = allowed && v.has_song && v.shift_offer &&
                      (v.shift_offer_from_keys || fits_ok) && !v.playing;
    if (want) {
        target_ = v.shift_offer_target;
        from_ = v.shift_offer_from;
        from_keys_ = v.shift_offer_from_keys;
    }
    shown_p_ = approach(shown_p_, want ? 1.0f : 0.0f, k.dt, tok::kMove);
    rect_ = {};
    if (shown_p_ <= 0.0f || (!from_keys_ && !fits_ok)) {
        strip_ = false;
        return;
    }
    if (from_keys_) {
        strip_ = false; // what the song leaves off is not what the offer is about
    }
    const int octave_move = (target_ - from_) / kOctave;
    // The popup rule: a wider window gets a wider card, larger words and taller buttons.
    const int step = tok::wide_step(f.display.x);
    const float hit = k.s(tok::kHit) * tok::popup_grow(step);
    const float u = k.u;
    // The answers share one row when they fit the card, else the last wraps.
    ChipOpts accept_o = popup_button(k, step, icon::kCheck, "Accept");
    accept_o.on = true;
    ChipOpts dismiss_o = popup_button(k, step, icon::kClose, "Dismiss");
    ChipOpts never_o = popup_button(k, step, nullptr, "Don't ask again");
    const float aw = std::max(chip_width(k, accept_o.icon, "Accept"), accept_o.width);
    const float dw = std::max(chip_width(k, dismiss_o.icon, "Dismiss"), dismiss_o.width);
    const float nw = std::max(chip_width(k, nullptr, "Don't ask again"), never_o.width);
    const float side = k.s(tok::kHitGap); // between two answers: the dead gap
    const float card_w = tok::popup_width(tok::kOfferW, step, f.display.x / u);
    const float inner_w = std::min(k.s(card_w), std::max(f.display.x - k.s(tok::kS5), 0.0f)) -
                          2.0f * k.s(tok::kS2);
    const bool one_row = aw + dw + nw + 2.0f * side <= inner_w;
    const TransposeCardPlan plan =
        plan_offer_card(f.display.x, below_y, f.keyboard_top, u, one_row ? 1 : 2, strip_, step);
    const float gap = plan.gap; // between two rows
    const float t = ease_out(shown_p_);
    const float w = plan.w;
    const float x = (f.display.x - w) * 0.5f;
    const float y = below_y + (1.0f - t) * -k.s(tok::kS2);
    const float shown_h = std::max(0.0f, std::min(plan.max_h, plan.h));
    rect_ = {x, below_y, w, y - below_y + shown_h};
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    if (begin_layer("##shift-offer", {x, y}, {w, shown_h}, true)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        const float left = x + plan.pad;
        const float right = x + w - plan.pad;
        float row = y + plan.pad;

        // What is proposed, and what it leaves off the keyboard.
        {
            glyph(k, dl, icon::kHeight, {left + hit * 0.5f, row + hit * 0.5f}, k.s(tok::kIcon),
                  col::kAccent);
            const float text_x = left + hit + k.s(tok::kS1);
            const float text_right = from_keys_ ? right : right - hit - k.s(tok::kS1);
            draw_offer_words(k, dl, from_keys_ ? nullptr : fits, {from_, target_, octave_move, step},
                             {text_x, row, text_right, row + hit});
            if (!from_keys_) {
                ChipOpts keys;
                keys.icon = icon::kPiano;
                keys.on = strip_;
                if (step > 0) {
                    keys.width = hit;
                    keys.hit_h = hit;
                    keys.face_h = k.s(tok::kFace) * tok::popup_grow(step);
                }
                if (chip(k, "##offer-keys", "", {right - hit, row}, keys)) {
                    strip_ = !strip_;
                }
            }
            row += hit + gap;
        }

        // The answers.
        {
            float cx = left;
            const auto place = [&](float cw) {
                if (cx + cw > right + 0.01f) {
                    cx = left;
                    row += hit + gap;
                }
                const ImVec2 at{cx, row};
                cx += cw + side;
                return at;
            };
            if (chip(k, "##offer-accept", "Accept", place(aw), accept_o)) {
                song.accept_shift_offer(k.now_ns);
                k.say(from_keys_ ? "The keys follow your keyboard" : "Shifted");
            }
            if (chip(k, "##offer-dismiss", "Dismiss", place(dw), dismiss_o)) {
                song.dismiss_shift_offer(k.now_ns);
            }
            if (chip(k, "##offer-never", "Don't ask again", place(nw), never_o)) {
                song.never_offer_shift(k.now_ns);
                k.say("Won't ask again for this song");
            }
            row += hit;
        }
        if (strip_) {
            const std::array<StripBar, 2> pair{StripBar{from_, 0, true},
                                               StripBar{target_, octave_move, false}};
            draw_strip(k, dl, {left, row + gap, right, row + gap + strip_height(2, u)}, *fits,
                       pair);
        }

        drag_scroll(k);
        dl->ChannelsSetCurrent(0);
        const ImVec2 wp = ImGui::GetWindowPos();
        k.toast_avoid({wp.x, wp.y, w, shown_h});
        overlay_card(k, dl, wp, {wp.x + w, wp.y + shown_h}, k.s(tok::kRadiusCard));
        dl->ChannelsMerge();
        probe::add_rect("##shift-offer-rect", wp, {wp.x + w, wp.y + shown_h});
    }
    ImGui::End();
    if (!want) {
        block_input("##shift-offer");
    }
    ImGui::PopStyleVar();
}

} // namespace etude::app::ui
