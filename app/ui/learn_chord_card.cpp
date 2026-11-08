// SPDX-License-Identifier: GPL-3.0-or-later
// The chord card (learn_screen.hpp): the root picker, the chord qualities, the chord on the keys
// with each note's name and the fingers, the chord's name, the stepper through its inversions and the
// drill's controls (learn_drill_ui.cpp): the chord is played as an arpeggio.
// It is laid out and drawn like the scale card (learn_scale_card.cpp, which has the shared pieces:
// the tabs, the picker, the keyboard and the strip of chips): the chord's name over the picker, the
// qualities in rows (or the strip) over the keyboard, the foot row under it.
#include "learn_screen.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "learn_draw.hpp"
#include "learn_lit.hpp"
#include "learn_types.hpp"
#include "note_name.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace {

// The stepper's value: the inversion in words where there is room, short on the narrow card.
constexpr std::array<const char*, 5> kInversionFull{
    "Root position", "1st inversion", "2nd inversion", "3rd inversion", "4th inversion"};
constexpr std::array<const char*, 5> kInversionShort{"Root", "1st inv.", "2nd inv.", "3rd inv.",
                                                     "4th inv."};

// The scale type whose key names the picker spells the roots as ("Bb", "F#" by the chord's key).
ScaleType picker_spelling(ChordQuality quality) {
    return chord_is_minor_like(quality) ? ScaleType::NaturalMinor : ScaleType::Major;
}

} // namespace

void LearnScreen::draw_chords(Kit& k, UiModel& model, LearnPort& port, const Box& area) {
    AppSettings& app = model.app;
    // Rows only where the widest quality fits a cell in the font: measured, not assumed.
    type_need_ = learn_chip_need_pt(k, learn_card_unit(area, k.u), kLearnChordCount, [](int i) {
        return kLearnChordWords[static_cast<std::size_t>(i)];
    });
    LearnCells cells = kChordCells;
    cells.cell_min = type_need_;
    const LearnCardLayout c = learn_card_layout(area, k.u, cells, learn_measure_tabs(k, 1));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int count = chord_inversion_count(app.learn_chord);
    app.learn_inversion = std::clamp(app.learn_inversion, 0, std::max(count - 1, 0));
    if (draw_back(k, c.back, "##learn-back")) {
        page_ = Page::Gallery;
    }
    label_at(k, dl, k.bold, k.s(tok::kTitle), c.title, LabelAlign::Left,
             c.name_in_header ? col::kText2 : col::kText, "Chords");
    dl->AddLine({area.x0, c.header.y1}, {area.x1, c.header.y1}, u32(col::kHairline),
                k.s(tok::kHairline));
    draw_tabs(k, model.look, c.tabs, c.tab_label, 1);
    // The body is drawn in its own unit: the card grows with a larger window.
    const float window_unit = k.u;
    k.u = learn_card_unit(area, window_unit);
    const ChordDiagram& d = chord_view(app);
    // The settings' card, open, covers part of what is under it: that is not the audit's.
    audit::set_quiet(settings_open_ && c.column == LearnColumn::Popover);
    // The ring's middle is the chord's root and quality, not a scale type's word.
    KeyRingOpts ring;
    ring.centre_root = d.name.root.c_str();
    ring.centre_word = chord_word_.c_str();
    draw_picker(k, app, c, &app.learn_chord_root, picker_spelling(app.learn_chord), ring);
    draw_chord_types(k, app, c);
    // While Listen sounds the arpeggio, each key lights as its note starts and lets go as it ends.
    const LearnLit lit = playing_ ? learn_lit_at(port.listening(), d.picture, k.now - play_t0_ - kLearnLeadS)
                                  : LearnLit{};
    draw_keyboard(k, d.picture, c, app.learn_hands == LearnHands::Left ? tok::kDisabledAlpha : 1.0f,
                  app.learn_hands == LearnHands::Right ? tok::kDisabledAlpha : 1.0f,
                  "No standard fingering for this chord", {&model.look, learn_section_colour(model.look, LearnSection::Chords)}, &lit);
    draw_chord_name(k, app, c);
    audit::set_quiet(false);
    draw_foot(k, app, port, c, true);
    if (c.stepper.w() > 0.0f) {
        draw_inversion(k, app, c.stepper);
    }
    draw_drill(k, app, c, false);
    k.u = window_unit;
}

void LearnScreen::draw_chord_types(Kit& k, AppSettings& app, const LearnCardLayout& c) {
    const int current = learn_chord_index(app.learn_chord);
    if (!c.rows) {
        // All of them in a strip that scrolls sideways, the chosen one kept in view.
        const int place = draw_strip(k, c.strip, kLearnChordIds.data(), kLearnChordWords.data(),
                                     kLearnChordCount, current);
        if (place >= 0) {
            app.learn_chord = kChordCardQualities[static_cast<std::size_t>(place)];
        }
        return;
    }
    strip_shown_ = -1; // the strip, when it is next drawn, brings the chosen quality into view again
    for (int i = 0; i < c.type_cells; ++i) {
        const Box& cell = c.type[static_cast<std::size_t>(i)];
        const char* words = kLearnChordWords[static_cast<std::size_t>(i)];
        ChipOpts o;
        o.on = i == current;
        o.width = cell.w();
        o.fixed = true;
        o.large = true;
        o.label_pt = tok::kBody * learn_label_scale(cell.w() / k.u, type_need_);
        o.hit_h = cell.h();
        o.face_h = cell.h() * (tok::kFace / tok::kHit);
        if (chip(k, kLearnChordIds[static_cast<std::size_t>(i)], words, {cell.x0, cell.y0}, o)) {
            app.learn_chord = kChordCardQualities[static_cast<std::size_t>(i)];
        }
    }
}

void LearnScreen::draw_chord_name(Kit& k, const AppSettings& app, const LearnCardLayout& c) {
    const ChordDiagram& d = chord_view(app);
    if (d.name.empty()) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Centred over the picker, shrunk to its width when wider; in the header, after the page's word.
    const float size = k.s(c.name_in_header ? tok::kTitle : tok::kDisplay);
    const float width = std::min(c.name.w(), chord_name_width(k.bold, size, d.name.root.c_str(),
                                                              d.name.quality.c_str(),
                                                              d.name.bass.c_str()));
    const float x0 = c.name_in_header ? c.name.x0 : c.name.cx() - width * 0.5f;
    const Box slot{x0, c.name.y0, x0 + width, c.name.y1};
    chord_name_at(k, dl, k.bold, size, slot, col::kText, d.name.root.c_str(),
                  d.name.quality.c_str(), d.name.bass.c_str());
}

// The inversion stepper: a minus cell, the inversion in words and a plus cell on a track, each cell
// a full hit (the shared stepper, in the foot row or the settings' card).
void LearnScreen::draw_inversion(Kit& k, AppSettings& app, const Box& box) {
    const int count = chord_inversion_count(app.learn_chord);
    const int current = app.learn_inversion;
    const std::size_t place = static_cast<std::size_t>(std::clamp(current, 0, 4));
    const int step = draw_stepper(k, box, "##learn-inv-minus", "##learn-inv-plus", current > 0,
                                  current < count - 1, nullptr, kInversionFull[place],
                                  kInversionShort[place], tok::kLabel);
    if (step != 0) {
        app.learn_inversion = std::clamp(current + step, 0, std::max(count - 1, 0));
    }
}

} // namespace etude::app::ui
