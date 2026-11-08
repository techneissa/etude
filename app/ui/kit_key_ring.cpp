// SPDX-License-Identifier: GPL-3.0-or-later
// The key ring (key_ring.hpp): twelve round keys on a circle in fifths and the picked key's name in
// the middle. One pressable covers the ring and the key under the finger is worked out from where it
// landed, so the keys keep their full hit squares and the nearest one wins.
#include "key_ring.hpp"

#include "icon_audit.hpp"
#include "note_name.hpp"
#include "tap.hpp"

#include <cctype>
#include <cstdlib>
#include <cstdio>

namespace etude::app::ui {

namespace {

// Names for the audit, one per pitch class (a name that is a prefix of another would tap the wrong
// key: all have two digits).
constexpr const char* kKeyIds[key_ring_tok::kKeys] = {
    "##ring-key-00", "##ring-key-01", "##ring-key-02", "##ring-key-03", "##ring-key-04",
    "##ring-key-05", "##ring-key-06", "##ring-key-07", "##ring-key-08", "##ring-key-09",
    "##ring-key-10", "##ring-key-11"};

// Where a key's eased states live in the ring's storage, apart from the ring's own id (each tween
// takes three slots).
constexpr ImGuiID kSelectedSlots = 0xA110C000u;
constexpr ImGuiID kPressedSlots = 0xB220D000u;
constexpr ImGuiID kSlotsPerKey = 4;

// The state of key `pc`: the ring's own item decides hover and press, and only the key they fell on
// takes them. The audit's forced frame draws every key in the forced state.
State key_state(const State& whole, ImGuiID base, int pc, bool picked, int under, int held) {
    State s = whole;
    if (audit::forced_state() != audit::Forced::None) {
        s.selected = (whole.selected > 0.0f || picked) ? 1.0f : 0.0f;
        return s;
    }
    const ImGuiID slot = static_cast<ImGuiID>(pc) * kSlotsPerKey;
    const bool is_held = pc == held;
    s.hover = pc == under;
    s.pressed = tween(base ^ (kPressedSlots + slot), is_held ? 1.0f : 0.0f,
                      is_held ? tok::kInstant : tok::kFast);
    s.selected = tween(base ^ (kSelectedSlots + slot), picked ? 1.0f : 0.0f, tok::kFast);
    s.focus = false;
    return s;
}

} // namespace

namespace {

// The twelve keys' spellings for one scale type, made when the type changes: spelling a key
// (`default_tonic`) takes memory, and a frame must not.
struct KeyNames {
    ScaleType type = ScaleType::Major;
    bool made = false;
    char name[key_ring_tok::kKeys][8]{};
};

const KeyNames& key_names(ScaleType type) {
    static KeyNames cache;
    if (!cache.made || cache.type != type) {
        for (int pc = 0; pc < key_ring_tok::kKeys; ++pc) {
            const Tonic tonic = default_tonic(pc, type);
            std::size_t n = 0;
            cache.name[pc][n++] = tonic.letter;
            for (int i = 0; i < std::abs(tonic.accidental) && n + 1 < sizeof cache.name[pc]; ++i) {
                cache.name[pc][n++] = tonic.accidental > 0 ? '#' : 'b';
            }
            cache.name[pc][n] = '\0';
        }
        cache.type = type;
        cache.made = true;
    }
    return cache;
}

} // namespace

void key_ring_key_name(int pc, ScaleType type, char* out, std::size_t size) {
    if (size == 0) {
        return;
    }
    std::snprintf(out, size, "%s", key_names(type).name[(pc % 12 + 12) % 12]);
}

// The picked key and its word in the middle, as one block centred in the inner circle, sized by the
// room there (key_ring_middle).
static void draw_middle(Kit& k, ImDrawList* dl, const KeyRingLayout& lay, int root, ScaleType type,
                        const KeyRingOpts& opts) {
    const KeyRingMiddle mid_text = key_ring_middle(lay, k.u);
    const Box& m = lay.middle;
    char name[8];
    key_ring_key_name(root, type, name, sizeof name);
    if (opts.centre_root != nullptr) {
        std::snprintf(name, sizeof name, "%s", opts.centre_root);
    }
    const float name_line = mid_text.name_size * 1.4f;
    note_name_at(k, dl, k.bold, mid_text.name_size,
                 {m.x0, mid_text.name_cy - name_line * 0.5f, m.x1, mid_text.name_cy + name_line * 0.5f},
                 col::kText, name);
    const float mode_line = mid_text.mode_size * 1.4f;
    if (opts.centre_word != nullptr) {
        // A chord's quality, its signs drawn as shapes, centred and shrunk to the room (down to its
        // floor).
        float size = mid_text.mode_size;
        const float want = chord_name_width(k.bold, size, "", opts.centre_word, "");
        if (want > mid_text.word_room && want > 0.0f) {
            size = std::max(k.s(key_ring_tok::kModeFloor), size * mid_text.word_room / want);
        }
        const float w = std::min(mid_text.word_room,
                                 chord_name_width(k.bold, size, "", opts.centre_word, ""));
        const float cx = (m.x0 + m.x1) * 0.5f;
        chord_name_at(k, dl, k.bold, size,
                      {cx - w * 0.5f, mid_text.mode_cy - mode_line * 0.5f, cx + w * 0.5f,
                       mid_text.mode_cy + mode_line * 0.5f},
                      col::kText2, "", opts.centre_word, "");
        return;
    }
    FitText mode;
    const char* word = scale_type_name(type);
    mode.format("%c%s", std::tolower(static_cast<unsigned char>(word[0])), word + 1);
    // A long word ("harmonic minor") shrinks to the chord it stands on, down to its floor, then is cut.
    float mode_size = mid_text.mode_size;
    const float want = text_size(k.bold, mode_size, mode.c_str()).x;
    if (want > mid_text.word_room && want > 0.0f) {
        mode_size = std::max(k.s(key_ring_tok::kModeFloor), mode_size * mid_text.word_room / want);
    }
    const FitText shown = fit(k.bold, mode_size, mode.view(), mid_text.word_room);
    label_at(k, dl, k.bold, mode_size,
             {m.x0, mid_text.mode_cy - mode_line * 0.5f, m.x1, mid_text.mode_cy + mode_line * 0.5f},
             LabelAlign::Centre, col::kText2, shown.c_str());
}

int key_ring(Kit& k, const char* id, const Box& area, int root_pc, ScaleType type,
             const KeyRingOpts& opts) {
    const KeyRingLayout lay = key_ring_layout(area, k.u, opts.max_key_pt);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos({lay.bounds.x0, lay.bounds.y0});
    const bool pressed = ImGui::InvisibleButton(id, {lay.bounds.w(), lay.bounds.h()});
    const State whole = item_state(k, false, opts.live);
    const ImGuiID base = ImGui::GetItemID();
    const ImGuiIO& io = ImGui::GetIO();
    const int under = whole.hover ? key_ring_nearest(lay, io.MousePos.x, io.MousePos.y) : -1;
    const int held = ImGui::IsItemActive()
                         ? key_ring_nearest(lay, io.MouseClickedPos[0].x, io.MouseClickedPos[0].y)
                         : -1;
    int tapped_key = -1;
    if (opts.live && tapped(pressed)) {
        // The key the press landed on, and only when the finger lifts on that same key.
        tapped_key = key_ring_release(lay, io.MouseClickedPos[0].x, io.MouseClickedPos[0].y,
                                      io.MousePos.x, io.MousePos.y);
    }

    // The circle the keys sit on, faint and only between the keys, so the ring reads as one thing
    // and no line runs under a key. ImGui's angles run clockwise from the right; the ring's from
    // the top.
    const ImVec2 mid{lay.bounds.cx(), lay.bounds.cy()};
    const float clear = (lay.key * 0.5f + k.s(tok::kS1)) / lay.radius;
    constexpr float kQuarter = 1.5707964f;
    for (int place = 0; place < key_ring_tok::kKeys; ++place) {
        const float from = static_cast<float>(place) * key_ring_tok::kStep - kQuarter + clear;
        const float to = static_cast<float>(place + 1) * key_ring_tok::kStep - kQuarter - clear;
        dl->PathArcTo(mid, lay.radius, from, to);
        dl->PathStroke(u32(col::kHairline), ImDrawFlags_None, k.s(tok::kHairline));
    }

    const int root = (root_pc % 12 + 12) % 12;
    const float radius = lay.key * 0.5f;
    for (int pc = 0; pc < key_ring_tok::kKeys; ++pc) {
        const Box box = key_ring_key_box(lay, pc);
        const State st = key_state(whole, base, pc, pc == root, under, held);
        draw_state(k, dl, kKeyIds[pc], box, box, st, Idle::Fill, radius);
        audit::note(kKeyIds[pc], {box.x0, box.y0}, {box.x1, box.y1}, InkKind::Target);
        char name[8];
        key_ring_key_name(pc, type, name, sizeof name);
        note_name_at(k, dl, k.bold, lay.key * key_ring_tok::kKeyLabelShare, box,
                     state_ink(st, col::kText), name);
    }

    draw_middle(k, dl, lay, root, type, opts);
    return tapped_key;
}

} // namespace etude::app::ui
