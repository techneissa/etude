// SPDX-License-Identifier: GPL-3.0-or-later
// The View page's Learn colours (learn_colours_layout.hpp): one row of tabs, Scales, Chords, Ear and
// Melodies (the kit's `segmented`), the seven shades to pick from for the tab picked under it, and one
// "Reset to default". A pick is a tap on the shade (on lift); the chosen one has a ring and a check in
// it, so it is told without its colour. A tap changes only the picked tab's section.
#include "pages.hpp"

#include "icon_audit.hpp"
#include "kit_internal.hpp"
#include "learn_colours_layout.hpp"
#include "tap.hpp"

#include "etude/learn_colours.hpp"
#include "etude/look.hpp"

#include <array>
#include <string>

namespace etude::app::ui {

namespace {

constexpr std::array<const char*, kLearnSectionCount> kSectionNames{"Scales", "Chords", "Ear", "Melodies"};
constexpr std::array<std::string_view, kLearnSectionCount> kSectionKeys{"scales", "chords", "ear",
                                                                       "melodies"};

// The tab picked (not a setting: Scales when the sheet opens, kept while it is open).
LearnColoursTab g_tab;

Box box_at(ImVec2 origin, const Rect& r) {
    return {origin.x + r.x, origin.y + r.y, origin.x + r.right(), origin.y + r.bottom()};
}

ImVec4 ink_of(RGBA c) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, 1.0f};
}

// Every shade's control name, made once.
const std::string& cell_id(std::size_t section, std::size_t shade) {
    static const std::array<std::array<std::string, kLearnShadeCount>, kLearnSectionCount> ids = [] {
        std::array<std::array<std::string, kLearnShadeCount>, kLearnSectionCount> out;
        for (std::size_t s = 0; s < out.size(); ++s) {
            for (std::size_t c = 0; c < out[s].size(); ++c) {
                out[s][c] = "##learn-colour-" + std::string(kSectionKeys[s]) + "-" +
                            std::string(kLearnShades[c].key);
            }
        }
        return out;
    }();
    return ids[section][shade];
}

void draw_cell(Kit& k, ImDrawList* dl, const Box& hit, const std::string& id, const LearnShadeInfo& info,
               bool chosen, bool* picked) {
    const bool pressed = press_area(k, id.c_str(), hit);
    State st = item_state(k, chosen, true, false);
    st.selected = 0.0f; // the ring below says it; the kit's accent fill would tint the disc's room
    draw_state(k, dl, id.c_str(), hit, hit, st, Idle::None);
    audit::note(id.c_str(), {hit.x0, hit.y0}, {hit.x1, hit.y1}, InkKind::Target);
    const ImVec2 c{hit.cx(), hit.cy()};
    const float r = k.s(learn_colours_tok::kSwatch) * 0.5f;
    dl->AddCircleFilled(c, r, u32(ink_of(info.colour)), 32);
    if (chosen) {
        const float line = k.s(tok::kHairline) * 2.0f;
        dl->AddCircle(c, r + k.s(learn_colours_tok::kRingGap), u32(col::kText), 40, line);
        glyph(k, dl, icon::kCheck, c, k.s(tok::kIcon), ink_of(label_color_for(info.colour)));
    }
    *picked = tapped(pressed);
}

} // namespace

void learn_colour_rows(Kit& k, UiModel& model) {
    LookSettings& look = model.look;
    catalogue_sweep::drawn(Setting::LearnColours);
    constexpr Seg kTabs[kLearnSectionCount] = {
        {nullptr, "Scales"}, {nullptr, "Chords"}, {nullptr, "Ear"}, {nullptr, "Melodies"}};
    segmented(k, "learn-colours-tab", nullptr, nullptr, kTabs, &g_tab.index);
    const std::size_t section = learn_tab_section(g_tab.index);
    ImGui::Dummy({1.0f, k.s(tok::kHitGap)});
    // The swatches run from where the tab track starts to where it ends (a row with no label).
    float row_w = 0.0f;
    const ImVec2 row_at = row_start(&row_w);
    const Span track = block_span(row_frame(row_at.x, row_w, k.u).plan, k.u);
    LearnColoursSizes sizes;
    sizes.w = track.w();
    sizes.u = k.u;
    const LearnColoursLayout g = learn_colours_layout(sizes);
    const ImVec2 origin{track.x0, row_at.y};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (std::size_t c = 0; c < kLearnShadeCount; ++c) {
        bool picked = false;
        draw_cell(k, dl, box_at(origin, g.cells[c]), cell_id(section, c), kLearnShades[c],
                  learn_tab_pick(look.learn_picks, g_tab.index) == static_cast<LearnShade>(c), &picked);
        if (picked) {
            look.learn_picks = learn_pick_on_tab(look.learn_picks, g_tab.index, static_cast<LearnShade>(c));
            k.say(std::string(kSectionNames[section]) + ": " + std::string(kLearnShades[c].name));
        }
    }
    ImGui::SetCursorScreenPos(row_at);
    ImGui::Dummy({row_w, g.height});
    ImGui::Dummy({1.0f, k.s(tok::kHitGap)});
    if (text_button(k, "##learn-colours-reset", icon::kReset, "Reset to default", false)) {
        // Only the tab shown goes back to its default; Undo puts the shade back.
        const LearnShade prior = reset_learn_tab(look.learn_picks, g_tab.index);
        k.say_undoable(std::string(kSectionNames[section]) + " colour reset",
                       undo_of(&look.learn_picks[section], prior));
    }
}

void learn_colours_sheet_opened() { g_tab.sheet_opened(); }

} // namespace etude::app::ui
