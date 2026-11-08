// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen's frame, its header and the gallery (learn_screen.hpp); the scale card is in
// learn_scale_card.cpp and the chord card in learn_chord_card.cpp.
#include "learn_screen.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "learn_draw.hpp"
#include "learn_types.hpp"
#include "key_ring.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace etude::app::ui {

namespace {

struct CardInfo {
    const char* id;
    const char* title;
    const char* detail;
    const char* icon;
};

constexpr std::array<CardInfo, kLearnCards> kCards{{
    {"##learn-tile-scales", "Scales", "14 types, both hands", icon::kScales},
    {"##learn-tile-chords", "Chords", "Inversions, fingering", icon::kLayers},
    {"##learn-tile-ear", "Ear", "Notes and intervals", icon::kEar},
    {"##learn-tile-melodies", "Melodies", "Pieces to play", icon::kMelodies}, // detail: the counts
}};

// The words Continue gives: "G major scale", the key as the code spells it (the screen turns the
// signs into ♭ and ♯).
void continue_words(FitText& out, const AppSettings& app) {
    char key[8];
    key_ring_key_name(app.learn_root, app.learn_scale, key, sizeof key);
    const char* full = scale_type_name(app.learn_scale);
    out.format("%s %c%s scale", key, std::tolower(static_cast<unsigned char>(full[0])), full + 1);
}

} // namespace

void LearnScreen::open() {
    open_ = true;
    page_ = Page::Gallery;
    strip_shown_ = -1;
    strip_.cancel();
}

void LearnScreen::open_scales() {
    open_ = true;
    page_ = Page::Scales;
    strip_shown_ = -1;
    strip_.cancel();
}

void LearnScreen::open_chords() {
    open_ = true;
    page_ = Page::Chords;
    strip_shown_ = -1;
    strip_.cancel();
}

void LearnScreen::open_ear() {
    open_ = true;
    page_ = Page::Ear;
    strip_shown_ = -1;
    strip_.cancel();
}

void LearnScreen::open_melodies() {
    open_ = true;
    page_ = Page::Melodies;
    strip_shown_ = -1;
    strip_.cancel();
}

void LearnScreen::note_on(int note) {
    if (open_ && page_ == Page::Ear && ear_in_n_ < static_cast<int>(ear_in_.size())) {
        ear_in_[static_cast<std::size_t>(ear_in_n_++)] = note;
    }
}

void LearnScreen::set_ear_seed(std::uint64_t seed) {
    ear_ = EarSession(seed);
    ear_seeded_ = true;
}

void LearnScreen::close() {
    open_ = false;
    melodies_.cancel();
    strip_shown_ = -1;
    strip_.cancel();
}

bool LearnScreen::back() {
    if (page_ != Page::Gallery) {
        page_ = Page::Gallery;
        melodies_.cancel();
        return true;
    }
    return false;
}

const char* LearnScreen::resume_screen() const {
    return page_ == Page::Scales ? "learn-scales" : page_ == Page::Chords ? "learn-chords"
           : page_ == Page::Ear ? "learn-ear" : page_ == Page::Melodies ? "learn-melodies" : "learn";
}

const ChordDiagram& LearnScreen::chord_view(const AppSettings& app) {
    const int count = chord_inversion_count(app.learn_chord);
    const int inversion = std::clamp(app.learn_inversion, 0, std::max(count - 1, 0));
    const int quality = static_cast<int>(app.learn_chord);
    if (chord_root_cached_ != app.learn_chord_root || chord_quality_cached_ != quality ||
        chord_inversion_cached_ != inversion) {
        chord_ = chord_diagram(app.learn_chord_root, app.learn_chord, inversion);
        chord_word_ = learn_chord_centre_word(chord_.name);
        chord_root_cached_ = app.learn_chord_root;
        chord_quality_cached_ = quality;
        chord_inversion_cached_ = inversion;
    }
    return chord_;
}

const ScaleDiagram& LearnScreen::diagram(const AppSettings& app) {
    const int type = static_cast<int>(app.learn_scale);
    if (diagram_root_ != app.learn_root || diagram_type_ != type) {
        diagram_ = scale_diagram(app.learn_root, app.learn_scale);
        diagram_root_ = app.learn_root;
        diagram_type_ = type;
    }
    return diagram_;
}

bool LearnScreen::draw_back(Kit& k, const Box& box, const char* id) {
    return icon_toggle(k, id, icon::kBack, "Back", {box.cx(), box.cy()}, {box.w(), box.h()},
                       k.s(tok::kFace), false, true);
}

void LearnScreen::draw(Kit& k, UiModel& model, LearnPort& port, LibraryPort* library, const Frame& f) {
    library_ = library;
    if (!open_ || page_ != Page::Melodies) {
        tiles_stale_ = true; // the library's row is made again when the page is next drawn
    }
    open_p_ = approach(open_p_, open_ ? 1.0f : 0.0f, k.dt, tok::kSheet);
    // A drill stops sounding when it ends, and when the card or the screen is left; so does a question.
    if (playing_ && (!open_ || page_ != play_page_ || k.now >= play_until_)) {
        if (!open_ || page_ != play_page_) {
            port.stop_sound();
        }
        playing_ = false;
    }
    if (ear_sounding_ && (!open_ || page_ != Page::Ear || k.now >= ear_until_)) {
        if (!open_ || page_ != Page::Ear) {
            port.stop_sound();
        }
        ear_sounding_ = false;
    }
    if (!open_ || page_ != Page::Ear) {
        ear_in_n_ = 0; // a key pressed meanwhile is not an answer to a later card
    }
    if (open_p_ <= 0.0f) {
        return;
    }
    const float shown = ease_out(open_p_);
    const Box area{0.0f, f.row_max.y + (1.0f - shown) * k.s(tok::kS3), f.display.x, f.display.y};
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown);
    if (begin_layer("##learn", {area.x0, area.y0}, {area.w(), area.h()})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({area.x0, area.y0}, {area.x1, area.y1}, u32(col::kScreenBg));
        // While the screen slides away it takes no taps and its buttons and labels are not the
        // audit's.
        const bool frozen = !open_;
        audit::set_quiet(frozen);
        ImGui::BeginDisabled(frozen);
        if (page_ == Page::Gallery) {
            draw_gallery(k, model, port, area);
        } else if (page_ == Page::Chords) {
            draw_chords(k, model, port, area);
        } else if (page_ == Page::Ear) {
            draw_ear(k, model, port, area);
        } else if (page_ == Page::Melodies) {
            draw_melodies(k, model, port, area);
        } else {
            draw_scales(k, model, port, area);
        }
        ImGui::EndDisabled();
        audit::set_quiet(false);
    }
    ImGui::End();
    if (!open_) {
        stop_input();
    }
    ImGui::PopStyleVar();
}

void LearnScreen::draw_gallery(Kit& k, UiModel& model, LearnPort& port, const Box& area) {
    const LearnGalleryLayout g = learn_gallery_layout(area, k.u);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (draw_back(k, g.back, "##learn-back")) {
        close();
    }
    label_at(k, dl, k.bold, k.s(tok::kTitle), g.title, LabelAlign::Left, col::kText, "Learn");
    dl->AddLine({area.x0, g.header.y1}, {area.x1, g.header.y1}, u32(col::kHairline),
                k.s(tok::kHairline));
    // The Melodies tile counts what the manifest lists; no file is read for it.
    if (melody_detail_.empty()) {
        const LearnPort::MelodyCounts counts = port.melody_counts();
        melody_detail_.format("%d paths, %d pieces", counts.paths, counts.pieces);
    }
    for (int i = 0; i < kLearnCards; ++i) {
        const std::size_t n = static_cast<std::size_t>(i);
        draw_tile(k, model.look, i, g.tiles[n], true, i == 3 ? melody_detail_.c_str() : kCards[n].detail);
    }
    if (g.cont.w() > 0.0f) {
        const char* id = "##learn-continue";
        const bool pressed = tapped(press_area(k, id, g.cont));
        const State st = item_state(k, false);
        draw_state(k, dl, id, g.cont, g.cont, st, Idle::Fill, k.s(tok::kRadiusCard));
        audit::note(id, {g.cont.x0, g.cont.y0}, {g.cont.x1, g.cont.y1}, InkKind::Target);
        const float chip_half = k.s(tok::kFace) * 0.5f;
        const ImVec2 ic{g.cont_icon.cx(), g.cont_icon.cy()};
        dl->AddRectFilled({ic.x - chip_half, ic.y - chip_half}, {ic.x + chip_half, ic.y + chip_half},
                          u32(col::kFill), k.s(tok::kRadiusControl));
        glyph(k, dl, icon::kHistory, ic, k.s(tok::kIcon), state_ink(st, col::kText2));
        FitText words;
        continue_words(words, model.app);
        const StackLine title{k.bold, k.s(tok::kBody), state_ink(st, col::kText), "Continue"};
        const StackLine detail{k.regular, k.s(tok::kLabel), col::kText2, words.c_str()};
        label_stack(k, dl, g.cont_text, title, &detail);
        glyph(k, dl, icon::kChevron, {g.cont_chevron.cx(), g.cont_chevron.cy()}, k.s(tok::kIcon),
              col::kText3);
        if (pressed) {
            page_ = Page::Scales;
        }
    }
}

namespace {

// The icon chip of a stacked tile, in the card's own colour as its tab is.
void draw_tile_chip(const Kit& k, const LookSettings& look, ImDrawList* dl, int card, const Box& box) {
    const CardInfo& info = kCards[static_cast<std::size_t>(card)];
    const float chip_half = k.s(tok::kFace) * 0.5f;
    const ImVec2 ic{box.cx(), box.cy()};
    const RGBA own = learn_section_colour(look, kLearnTabInfo[static_cast<std::size_t>(card)].section);
    const ImVec4 tint{static_cast<float>(own.r) / 255.0f, static_cast<float>(own.g) / 255.0f,
                      static_cast<float>(own.b) / 255.0f, 1.0f};
    dl->AddRectFilled({ic.x - chip_half, ic.y - chip_half}, {ic.x + chip_half, ic.y + chip_half},
                      u32(with_alpha(tint, tok::kSelectedFill)), k.s(tok::kRadiusControl));
    glyph(k, dl, info.icon, ic, k.s(tok::kIcon), tint);
}

} // namespace

void LearnScreen::draw_tile(Kit& k, const LookSettings& look, int card, const LearnTileBoxes& t, bool built,
                            const char* detail_words) {
    const CardInfo& info = kCards[static_cast<std::size_t>(card)];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    State st;
    bool pressed = false;
    if (built) {
        pressed = tapped(press_area(k, info.id, t.tile));
        st = item_state(k, false);
    }
    if (built) {
        draw_state(k, dl, info.id, t.tile, t.tile, st, Idle::Fill, k.s(tok::kRadiusCard));
        audit::note(info.id, {t.tile.x0, t.tile.y0}, {t.tile.x1, t.tile.y1}, InkKind::Target);
    } else {
        dl->AddRectFilled({t.tile.x0, t.tile.y0}, {t.tile.x1, t.tile.y1},
                          u32(with_alpha(col::kFill, learn_tok::kSoonAlpha)),
                          k.s(tok::kRadiusCard));
    }
    // A tile that is not built yet is shown faint, picture and all.
    const float dim = built ? 1.0f : learn_tok::kSoonAlpha;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * dim);
    learn_draw_preview(k, dl, t.preview, card, look);
    if (!t.side) {
        draw_tile_chip(k, look, dl, card, t.icon);
    }
    const StackLine title{k.bold, k.s(tok::kTitle), state_ink(st, col::kText), info.title};
    const StackLine detail{k.regular, k.s(tok::kLabel), col::kText2, detail_words};
    // The one-row form has the title alone: the room beside the picture is for the name.
    label_stack(k, dl, t.text, title, t.side ? nullptr : &detail);
    if (built) {
        glyph(k, dl, icon::kChevron, {t.chevron.cx(), t.chevron.cy()}, k.s(tok::kIcon), col::kText3);
    } else {
        label_at(k, dl, k.regular, k.s(tok::kCaption), t.chevron, LabelAlign::Centre, col::kText2,
                 "Soon");
    }
    ImGui::PopStyleVar();
    if (pressed) {
        page_ = card == 3 ? Page::Melodies : card == 2 ? Page::Ear : card == 1 ? Page::Chords : Page::Scales;
        // A fling of the other card's strip does not carry over to this one.
        strip_shown_ = -1;
        strip_.cancel();
    }
}

} // namespace etude::app::ui
