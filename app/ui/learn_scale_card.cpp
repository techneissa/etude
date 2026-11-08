// SPDX-License-Identifier: GPL-3.0-or-later
// The scale card (learn_screen.hpp): the key picker, the scale types, the keyboard with the fingers,
// the hands' toggles, the scale's name and the drill's controls (learn_drill_ui.cpp).
#include "learn_screen.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "key_ring.hpp"
#include "learn_draw.hpp"
#include "learn_lit.hpp"
#include "learn_types.hpp"
#include "note_name.hpp"
#include "transport.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <array>
#include <cmath>

namespace etude::app::ui {

namespace {

// Names for the audit, two digits each so none is the start of another (the audit taps by a name's
// part).
constexpr std::array<const char*, 12> kKeyIds{
    "##learn-key-00", "##learn-key-01", "##learn-key-02", "##learn-key-03", "##learn-key-04",
    "##learn-key-05", "##learn-key-06", "##learn-key-07", "##learn-key-08", "##learn-key-09",
    "##learn-key-10", "##learn-key-11"};
constexpr std::array<const char*, kLearnTypes> kTypeIds{
    "##learn-type-00", "##learn-type-01", "##learn-type-02", "##learn-type-03", "##learn-type-04",
    "##learn-type-05", "##learn-type-06", "##learn-type-07", "##learn-type-08", "##learn-type-09",
    "##learn-type-10", "##learn-type-11", "##learn-type-12", "##learn-type-13"};

int type_index(ScaleType type) {
    for (std::size_t i = 0; i < kAllScaleTypes.size(); ++i) {
        if (kAllScaleTypes[i] == type) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

} // namespace

void LearnScreen::draw_scales(Kit& k, UiModel& model, LearnPort& port, const Box& area) {
    AppSettings& app = model.app;
    // Two rows are used only where the widest name fits a cell in the font: measured, not assumed.
    type_need_ = learn_chip_need_pt(k, learn_card_unit(area, k.u), kLearnTypes, [](int i) {
        return learn_type_words(static_cast<std::size_t>(i));
    });
    LearnCells cells = kScaleCells;
    cells.cell_min = type_need_;
    const LearnCardLayout c = learn_card_layout(area, k.u, cells, learn_measure_tabs(k, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (draw_back(k, c.back, "##learn-back")) {
        page_ = Page::Gallery;
    }
    label_at(k, dl, k.bold, k.s(tok::kTitle), c.title, LabelAlign::Left,
             c.name_in_header ? col::kText2 : col::kText, "Scales");
    dl->AddLine({area.x0, c.header.y1}, {area.x1, c.header.y1}, u32(col::kHairline),
                k.s(tok::kHairline));
    draw_tabs(k, model.look, c.tabs, c.tab_label, 0);
    // The body is drawn in its own unit: the card grows with a larger window.
    const float window_unit = k.u;
    k.u = learn_card_unit(area, window_unit);
    // The settings' card, open, covers part of what is under it: that is not the audit's.
    const bool covered = settings_open_ && c.column == LearnColumn::Popover;
    audit::set_quiet(covered);
    draw_picker(k, app, c, &app.learn_root, app.learn_scale);
    draw_types(k, app, c);
    const ScaleDiagram& d = diagram(app);
    // While Listen sounds the drill, each key lights as its note starts and lets go as it ends.
    const LearnLit lit = playing_ ? learn_lit_at(port.listening(), d, k.now - play_t0_ - kLearnLeadS)
                                  : LearnLit{};
    draw_keyboard(k, d, c,
                  app.learn_hands == LearnHands::Left ? tok::kDisabledAlpha : 1.0f,
                  app.learn_hands == LearnHands::Right ? tok::kDisabledAlpha : 1.0f,
                  "No standard fingering for this scale", {&model.look, learn_section_colour(model.look, LearnSection::Scales)}, &lit);
    draw_scale_name(k, app, c);
    audit::set_quiet(false);
    draw_foot(k, app, port, c, false);
    draw_drill(k, app, c, true);
    k.u = window_unit;
}

void LearnScreen::draw_tabs(Kit& k, const LookSettings& look, const std::array<Box, kLearnTabs>& tabs,
                            const std::array<bool, kLearnTabs>& labels, int shown) {
    for (int i = 0; i < kLearnTabs; ++i) {
        const std::size_t n = static_cast<std::size_t>(i);
        const Box& b = tabs[n];
        const LearnTabInfo& tab = kLearnTabInfo[n];
        const RGBA own = learn_section_colour(look, tab.section);
        const ImVec4 tint{static_cast<float>(own.r) / 255.0f, static_cast<float>(own.g) / 255.0f,
                          static_cast<float>(own.b) / 255.0f, 1.0f};
        ChipOpts o;
        o.icon = tab.icon;
        o.on = i == shown;
        o.tint = &tint;
        o.solid = true; // the shown tab is white with dark words, its icon in its colour
        o.width = b.w();
        o.fixed = true;
        const bool tapped_tab = chip(k, tab.id, labels[n] ? tab.label : "", {b.x0, b.y0}, o);
        if (tapped_tab && i != shown) {
            page_ = i == 3 ? Page::Melodies : i == 2 ? Page::Ear : i == 1 ? Page::Chords : Page::Scales;
            strip_shown_ = -1;
            strip_.cancel();
        }
    }
}

void LearnScreen::draw_picker(Kit& k, AppSettings& app, const LearnCardLayout& c, int* root,
                              ScaleType spelling, const KeyRingOpts& ring_opts) {
    // The ring needs room for keys of a hit: a body too short for it shows the buttons whatever the
    // look, and has no switch.
    const bool ring = app.learn_picker == LearnPicker::Ring && c.ring_fits;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ring) {
        const int tapped_key = key_ring(k, "##learn-ring", c.slot, *root, spelling, ring_opts);
        if (tapped_key >= 0) {
            *root = tapped_key;
        }
    } else {
        const std::array<Box, 12> grid = learn_key_grid(c.slot, k.u);
        for (int pc = 0; pc < 12; ++pc) {
            const Box& box = grid[static_cast<std::size_t>(pc)];
            const char* id = kKeyIds[static_cast<std::size_t>(pc)];
            const bool pressed = tapped(press_area(k, id, box));
            const State st = item_state(k, pc == *root);
            draw_state(k, dl, id, box, box, st, Idle::Fill, k.s(tok::kRadiusControl));
            audit::note(id, {box.x0, box.y0}, {box.x1, box.y1}, InkKind::Target);
            char name[8];
            key_ring_key_name(pc, spelling, name, sizeof name);
            note_name_at(k, dl, k.bold, box.h() * key_ring_tok::kKeyLabelShare, box,
                         state_ink(st, col::kText), name);
            if (pressed) {
                *root = pc;
            }
        }
    }
    // The look's switch shows the look it would change to.
    if (c.ring_fits && icon_toggle(k, "##learn-picker-look", ring ? icon::kGrid : icon::kOrbit,
                    ring ? "Show the keys as buttons" : "Show the ring of fifths",
                    {c.toggle.cx(), c.toggle.cy()}, {c.toggle.w(), c.toggle.h()}, k.s(tok::kFace),
                    false, true)) {
        app.learn_picker = ring ? LearnPicker::Keys : LearnPicker::Ring;
    }
}

void LearnScreen::draw_types(Kit& k, AppSettings& app, const LearnCardLayout& c) {
    const int current = type_index(app.learn_scale);
    if (!c.rows) {
        // All fourteen in a strip that scrolls sideways, the chosen one kept in view.
        const int place = draw_strip(k, c.strip, kTypeIds.data(), kLearnTypeBrief.data(),
                                     kLearnTypes, current);
        if (place >= 0) {
            app.learn_scale = kAllScaleTypes[static_cast<std::size_t>(place)];
        }
        return;
    }
    // The names in the largest size (up to the chips' own) that holds the widest; none is cut.
    strip_shown_ = -1; // the strip, when it is next drawn, brings the chosen type into view again
    for (int i = 0; i < c.type_cells; ++i) {
        const Box& cell = c.type[static_cast<std::size_t>(i)];
        const char* words = learn_type_words(static_cast<std::size_t>(i));
        ChipOpts o;
        o.on = i == current;
        o.width = cell.w();
        o.fixed = true;
        o.large = true;
        o.label_pt = tok::kBody * learn_label_scale(cell.w() / k.u, type_need_);
        o.hit_h = cell.h();
        o.face_h = cell.h() * (tok::kFace / tok::kHit);
        if (chip(k, kTypeIds[static_cast<std::size_t>(i)], words, {cell.x0, cell.y0}, o)) {
            app.learn_scale = kAllScaleTypes[static_cast<std::size_t>(i)];
        }
    }
}

void LearnScreen::draw_keyboard(Kit& k, const ScaleDiagram& d, const LearnCardLayout& c,
                                float right_alpha, float left_alpha, const char* none,
                                const LearnKeyPaint& paint, const LearnLit* lit) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    learn_draw_keys(k, dl, c.keyboard, d, true, paint, lit);
    if (!d.fingering) {
        // No standard fingering here: say so where the numbers would be.
        label_at(k, dl, k.regular, k.s(tok::kLabel), c.fingers_right, LabelAlign::Left, col::kText3,
                 none);
        return;
    }
    for (std::size_t i = 0; i < d.keys.size(); ++i) {
        const DiagramKey& key = d.keys[i];
        const bool tracked = lit != nullptr && i < kLearnMaxKeys;
        if (key.right > 0) {
            learn_draw_badge(k, dl,
                             learn_badge_box(c.fingers_right, c.keyboard, key, d.white_keys, c.badge),
                             key.right, k.hand_right, right_alpha,
                             tracked && lit->right[i] >= 0 ? lit->right_pulse[i] : 0.0f);
        }
        if (key.left > 0) {
            learn_draw_badge(k, dl,
                             learn_badge_box(c.fingers_left, c.keyboard, key, d.white_keys, c.badge),
                             key.left, k.hand_left, left_alpha,
                             tracked && lit->left[i] >= 0 ? lit->left_pulse[i] : 0.0f);
        }
    }
}

void LearnScreen::draw_hands(Kit& k, AppSettings& app, const LearnCardLayout& c) {
    const bool right_on = app.learn_hands != LearnHands::Left;
    const bool left_on = app.learn_hands != LearnHands::Right;
    // The top bar's own hand toggles (transport.hpp): left hand on the left, right on the right.
    const auto toggle = [&](const char* id, bool left, const Box& hit, bool on) {
        // A square as tall as Play, its face grown with it.
        const float face = hit.h() * (tok::kFace / tok::kHit);
        return hand_toggle_face(k, id, left, hit.x0, hit.cy(), hit.w(), hit.h(), face, on, true,
                                true, false);
    };
    if (toggle("##learn-hand-left", true, c.hand_left, left_on)) {
        if (!left_on) {
            app.learn_hands = LearnHands::Both;
        } else if (right_on) {
            app.learn_hands = LearnHands::Right;
        } else {
            hand_toggle_refused(true, k.now); // the only hand that plays stays on
        }
    }
    if (toggle("##learn-hand-right", false, c.hand_right, right_on)) {
        if (!right_on) {
            app.learn_hands = LearnHands::Both;
        } else if (left_on) {
            app.learn_hands = LearnHands::Left;
        } else {
            hand_toggle_refused(false, k.now);
        }
    }
}

// The scale's name: over the picker, centred, or in the header after the page's word.
void LearnScreen::draw_scale_name(Kit& k, const AppSettings& app, const LearnCardLayout& c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (c.name_in_header) {
        learn_draw_scale_name(k, dl, c.name, app.learn_root, app.learn_scale, tok::kTitle, false);
    } else {
        learn_draw_scale_name(k, dl, c.name, app.learn_root, app.learn_scale, tok::kDisplay, true);
    }
}

// The pointer on the strip: a press on it may become a scroll (the chip under it then lets go), the
// wheel scrolls it too.
void LearnScreen::strip_pointer(Kit& k, const Box& view) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         view.holds({io.MousePos.x, io.MousePos.y, io.MousePos.x, io.MousePos.y});
    const float px = io.MousePos.x / k.u;
    const float py = io.MousePos.y / k.u;
    if (ImGui::IsMouseClicked(0) && hovered) {
        strip_.press(px, py, k.now, io.MouseSource != ImGuiMouseSource_Mouse);
    }
    if (hovered && (io.MouseWheelH != 0.0f || io.MouseWheel != 0.0f)) {
        strip_.scroll_by(-(io.MouseWheelH + io.MouseWheel) * learn_tok::kStripWheel);
    }
    if (strip_.busy()) {
        if (ImGui::IsMouseDown(0)) {
            strip_.move(px, py, k.now);
        } else {
            strip_.release(px, py, k.now);
        }
    }
    if (strip_.update(k.now)) {
        ImGui::ClearActiveID();
    }
}

// A fade at each clipped edge of the strip, so it is clear there is more.
void LearnScreen::strip_fades(Kit& k, const Box& view) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fade = k.s(learn_tok::kStripFade);
    const ImU32 solid = u32(col::kScreenBg);
    const ImU32 clear = u32(with_alpha(col::kScreenBg, 0.0f));
    if (strip_.offset() > 0.5f / k.u) {
        dl->AddRectFilledMultiColor({view.x0, view.y0}, {view.x0 + fade, view.y1}, solid, clear,
                                    clear, solid);
    }
    if (strip_.offset() < strip_.max_offset() - 0.5f / k.u) {
        dl->AddRectFilledMultiColor({view.x1 - fade, view.y0}, {view.x1, view.y1}, clear, solid,
                                    solid, clear);
    }
}

int LearnScreen::draw_strip(Kit& k, const Box& view, const char* const* ids,
                            const char* const* words, int count, int current) {
    const float gap = k.s(tok::kHitGap);
    // The chips in their own widths, side by side: their places along the strip, in points.
    std::array<float, learn_tok::kMaxStrip> x0{};
    std::array<float, learn_tok::kMaxStrip> x1{};
    float at = 0.0f;
    for (int i = 0; i < count; ++i) {
        x0[static_cast<std::size_t>(i)] = at;
        at += chip_width(k, nullptr, words[i]);
        x1[static_cast<std::size_t>(i)] = at;
        at += gap;
    }
    const float content = std::max(0.0f, at - gap);
    // The chips' widths come from the fonts (a size baked the first time it is drawn) and the view
    // from the layout (which moves while the screen slides in): a strip whose content or view
    // changed under it brings the chosen chip into view again.
    if (std::abs(content / k.u - strip_.content()) > 0.5f ||
        std::abs(view.w() / k.u - strip_.view()) > 0.5f) {
        strip_shown_ = -1;
    }
    strip_.set_extent(content / k.u, view.w() / k.u);
    if (strip_shown_ != current) {
        // The chosen chip comes into view, with some of the next showing.
        const std::size_t place = static_cast<std::size_t>(current);
        strip_.show(x0[place] / k.u, x1[place] / k.u, learn_tok::kStripMargin);
        strip_shown_ = current;
    }
    strip_pointer(k, view);
    // The chips, clipped to the strip; one that the edge cuts is shown but is not the audit's.
    const float shift = view.x0 - strip_.offset() * k.u;
    int tapped_place = -1;
    ImGui::PushClipRect({view.x0, view.y0}, {view.x1, view.y1}, true);
    for (int i = 0; i < count; ++i) {
        const std::size_t n = static_cast<std::size_t>(i);
        const float left = shift + x0[n];
        const float right = shift + x1[n];
        if (right <= view.x0 || left >= view.x1) {
            continue;
        }
        const bool whole = left >= view.x0 - 0.5f && right <= view.x1 + 0.5f;
        const bool was_quiet = audit::quiet();
        audit::set_quiet(was_quiet || !whole);
        ChipOpts o;
        o.on = i == current;
        o.hit_h = view.h();
        o.face_h = view.h() * (tok::kFace / tok::kHit);
        if (chip(k, ids[i], words[i], {left, view.y0}, o)) {
            tapped_place = i;
        }
        audit::set_quiet(was_quiet);
    }
    strip_fades(k, view);
    ImGui::PopClipRect();
    return tapped_place;
}

} // namespace etude::app::ui
