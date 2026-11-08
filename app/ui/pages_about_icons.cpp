// SPDX-License-Identifier: GPL-3.0-or-later
// The About page's row of icon colours (icon_picker_layout.hpp): a colour for each title, the ones the
// profiles have not reached dimmed, each with its name under it, straight on the page. Beside them,
// or above them in one line on a narrow window, the player's title medal (a tap opens the Medals
// view) and what the medals add up to. The words are measured and made again only when the font, the
// size or the medals change; no frame measures text or builds a string.
#include "pages.hpp"

#include "brand_marks.hpp"
#include "fitted_text.hpp"
#include "icon_audit.hpp"
#include "icon_picker_facts.hpp"
#include "icon_picker_layout.hpp"
#include "icon_port.hpp"
#include "kit_internal.hpp"
#include "medal_emblem.hpp"

#include "etude/icon_colour.hpp"
#include "etude/medals.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

Box box_at(ImVec2 origin, const Rect& r) {
    return {origin.x + r.x, origin.y + r.y, origin.x + r.right(), origin.y + r.bottom()};
}

ImVec2 centre_of(const Box& b) { return {b.cx(), b.cy()}; }

// What the row's words measure at the real font, and the facts about the medals, kept until the font,
// the size or the medals change.
struct PickerWords {
    ImFont* bold = nullptr;
    ImFont* regular = nullptr;
    float caption = 0.0f;
    float label = 0.0f;
    int points = -2; // -2: nothing made yet; -1: the medals are not known, none are shown
    float name_w = 0.0f;
    float title_w = 0.0f;
    float stats_w = 0.0f;
    float hint_w = 0.0f; // the longest "<Colour> unlocks at <Title>" on one line
    IconPickerFacts facts;
};

const PickerWords& picker_words(const Kit& k, int points) {
    static PickerWords w;
    const bool fonts = w.bold == k.bold && w.regular == k.regular && w.caption == k.s(tok::kCaption) &&
                       w.label == k.s(tok::kLabel);
    if (fonts && w.points == points) {
        return w;
    }
    if (!fonts) {
        w.bold = k.bold;
        w.regular = k.regular;
        w.caption = k.s(tok::kCaption);
        w.label = k.s(tok::kLabel);
        w.name_w = 0.0f;
        w.title_w = 0.0f;
        w.hint_w = 0.0f;
        for (const etude::IconColourInfo& info : etude::kIconColours) {
            w.name_w = std::max(w.name_w, label_width(w.bold, w.caption, info.name.data()));
            w.hint_w = std::max(w.hint_w, label_width(w.regular, w.label, icon_unlock_hint(info).c_str()));
        }
        for (int title = 1; title <= etude::kTierCount; ++title) {
            w.title_w = std::max(w.title_w, label_width(w.bold, w.label, etude::tier_title(title).data()));
        }
        w.name_w += k.s(tok::kS1); // a pixel or two over what the words measure
        w.title_w += k.s(tok::kS1);
        w.hint_w += k.s(tok::kS1);
    }
    w.points = points;
    w.stats_w = 0.0f;
    w.facts = IconPickerFacts{};
    if (points >= 0) {
        w.facts = icon_picker_facts(points);
        for (const std::string& text : w.facts.side) {
            w.stats_w = std::max(w.stats_w, label_width(w.regular, w.caption, text.c_str()));
        }
        w.stats_w += k.s(tok::kS1);
    }
    return w;
}

// `texts` said in `room` as one line or two (icon_picker_lines), each cut only where it is still
// wider, which a window this narrow could not avoid.
std::vector<std::string> fitted_lines(ImFont* font, float size, const std::vector<std::string>& texts,
                                      float room) {
    std::vector<std::string> lines = icon_picker_lines(texts, room, [&](const std::string& text) {
        return label_width(font, size, text.c_str());
    });
    for (std::string& line : lines) {
        line = fit(font, size, line, room);
    }
    return lines;
}

// The stats as lines of `room` (the line above the cells): one, or two on a narrow window.
const std::vector<std::string>& stats_lines(const Kit& k, const PickerWords& w, float room) {
    static int points = -2;
    static float for_room = -1.0f;
    static ImFont* font = nullptr;
    static float size = 0.0f;
    static std::vector<std::string> out;
    if (points != w.points || for_room != room || font != k.regular || size != w.caption) {
        points = w.points;
        for_room = room;
        font = k.regular;
        size = w.caption;
        out = fitted_lines(font, size, {w.facts.line.begin(), w.facts.line.end()}, room);
    }
    return out;
}

// The lines under the cells about the colour last touched, when that one is locked.
const std::vector<std::string>& hint_lines(const Kit& k, const PickerWords& w,
                                           const etude::IconColourInfo& about, float room) {
    static int for_colour = -1;
    static float for_room = -1.0f;
    static ImFont* font = nullptr;
    static float size = 0.0f;
    static std::vector<std::string> out;
    if (for_colour != static_cast<int>(about.id) || for_room != room || font != k.regular ||
        size != w.label) {
        for_colour = static_cast<int>(about.id);
        for_room = room;
        font = k.regular;
        size = w.label;
        out = fitted_lines(font, size, {icon_unlock_hint(about)}, room);
    }
    return out;
}

ImVec4 palette_colour(std::uint32_t rgb) {
    return {static_cast<float>((rgb >> 16) & 0xff) / 255.0f, static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
            static_cast<float>(rgb & 0xff) / 255.0f, 1.0f};
}

// The chosen colour's highlight in its own colour (`t` 0..1 is the kit's eased selection): a tint of the
// cell, a soft glow under the icon and a rim, behind the icon. Rects only, nothing made per frame.
void draw_selection(const Kit& k, ImDrawList* dl, const Box& hit, const Box& swatch,
                    const etude::IconColourInfo& info, float t) {
    if (!(t > 0.0f)) {
        return;
    }
    const ImVec4 rim = palette_colour(info.rim);
    const float r = face_radius(k, hit);
    dl->AddRectFilled({hit.x0, hit.y0}, {hit.x1, hit.y1}, u32(with_alpha(rim, picker_tok::kTintAlpha * t)), r);
    const float round = swatch.w() * 0.22f; // the icon's own corner
    for (int layer = picker_tok::kGlowLayers; layer >= 1; --layer) {
        const float grow = k.s(picker_tok::kGlowSpread) * static_cast<float>(layer) /
                           static_cast<float>(picker_tok::kGlowLayers);
        const float fall = 1.0f - static_cast<float>(layer - 1) / static_cast<float>(picker_tok::kGlowLayers);
        dl->AddRectFilled({swatch.x0 - grow, swatch.y0 - grow}, {swatch.x1 + grow, swatch.y1 + grow},
                          u32(with_alpha(rim, picker_tok::kGlowAlpha * t * fall * fall)),
                          round + grow);
    }
    const float line = k.s(tok::kHairline);
    const float in = line * 0.5f;
    dl->AddRect({hit.x0 + in, hit.y0 + in}, {hit.x1 - in, hit.y1 - in},
                u32(with_alpha(rim, picker_tok::kRimAlpha * t)), std::max(0.0f, r - in), 0, line);
}

// The title medal and its title: one touch target that opens the Medals view. Beside the cells the
// title is under the disc, in the line above them it is right of it.
void draw_medal(Kit& k, ImDrawList* dl, const IconPickerLayout& g, ImVec2 origin, const PickerWords& words) {
    const Box hit = box_at(origin, g.medal_hit);
    const bool pressed = press_area(k, "##about_medal", hit);
    const State st = item_state(k, false, true, false);
    draw_state(k, dl, "##about_medal", hit, hit, st, Idle::None);
    audit::note("##about_medal", {hit.x0, hit.y0}, {hit.x1, hit.y1}, InkKind::Target);
    const Box disc = box_at(origin, g.medal);
    draw_title_medal(k, dl, words.facts.title, centre_of(disc), disc.w(), false);
    label_at(k, dl, k.bold, words.label, box_at(origin, g.medal_title),
             g.beside ? LabelAlign::Centre : LabelAlign::Left, state_ink(st, col::kText),
             words.facts.title_name.c_str());
    if (tapped(pressed) && k.icons != nullptr) {
        k.icons->open_medals();
    }
}

// The stats on the right of the cells, one line each, or in one or two lines above them.
void draw_stats(Kit& k, ImDrawList* dl, const IconPickerLayout& g, ImVec2 origin, const PickerWords& words) {
    if (!g.beside) {
        const Box room = box_at(origin, g.line);
        const std::vector<std::string>& lines = stats_lines(k, words, room.w());
        const float line_h = picker_tok::kStatH * k.u;
        float y = room.cy() - line_h * static_cast<float>(lines.size()) * 0.5f;
        for (const std::string& line : lines) {
            label_at(k, dl, k.regular, words.caption, {room.x0, y, room.x1, y + line_h}, LabelAlign::Left,
                     col::kText2, line.c_str());
            y += line_h;
        }
        return;
    }
    const std::array<ImVec4, picker_tok::kStatLines> inks{col::kText, col::kText2, col::kText3};
    for (std::size_t i = 0; i < words.facts.side.size(); ++i) {
        label_at(k, dl, k.regular, words.caption, box_at(origin, g.stat[i]), LabelAlign::Left, inks[i],
                 words.facts.side[i].c_str());
    }
}

} // namespace

// A colour the device's best title has reached is chosen by a tap (on lift); the others are dimmed and
// a tap on one says which title unlocks it, in the line under the cells.
void about_icon_picker(Kit& k, UiModel& model) {
    static const std::array<std::string, picker_tok::kCount> ids = [] {
        std::array<std::string, picker_tok::kCount> out;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = "##about_icon_" + std::string(etude::kIconColours[i].key);
        }
        return out;
    }();
    catalogue_sweep::drawn(Setting::IconColour);
    const int best = k.icons != nullptr ? k.icons->best_title() : 0;
    const PickerWords& words = picker_words(k, best > 0 ? k.icons->best_points() : -1);
    std::array<bool, picker_tok::kCount> locked{};
    bool any_locked = false;
    for (const etude::IconColourInfo& info : etude::kIconColours) {
        const std::size_t i = static_cast<std::size_t>(info.id);
        locked[i] = !etude::icon_colour_available(info.id, model.app.icon_colour, best);
        any_locked = any_locked || locked[i];
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    IconPickerSizes sizes;
    sizes.w = w;
    sizes.u = k.u;
    sizes.name_w = words.name_w;
    sizes.title_w = words.title_w;
    sizes.stats_w = words.stats_w;
    sizes.sides = words.points >= 0;
    sizes.hint = any_locked;
    sizes.hint_lines = words.hint_w > w ? 2 : 1;
    const IconPickerLayout g = icon_picker(sizes);
    static int touched = -1; // the colour the line under the cells is about; -1: none
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (sizes.sides) {
        draw_medal(k, dl, g, origin, words);
        draw_stats(k, dl, g, origin, words);
    }
    for (const etude::IconColourInfo& info : etude::kIconColours) {
        const std::size_t i = static_cast<std::size_t>(info.id);
        const IconPickerCell& c = g.cells[i];
        const bool chosen = model.app.icon_colour == info.id;
        const Box hit = box_at(origin, c.hit);
        const bool pressed = press_area(k, ids[i].c_str(), hit);
        State st = item_state(k, chosen, true, locked[i]);
        const float selected = st.selected;
        st.selected = 0.0f; // the kit would draw the accent; the colour's own highlight is drawn below
        draw_state(k, dl, ids[i].c_str(), hit, hit, st, Idle::None);
        audit::note(ids[i].c_str(), {hit.x0, hit.y0}, {hit.x1, hit.y1}, InkKind::Target);
        const Box swatch = box_at(origin, c.swatch);
        draw_selection(k, dl, hit, swatch, info, selected);
        draw_app_icon(k, dl, info.id, IconSlot::Swatch, centre_of(swatch), swatch.w(),
                      locked[i] ? 0.35f : 1.0f);
        label_at(k, dl, k.bold, words.caption, box_at(origin, c.name), LabelAlign::Centre,
                 mix(state_ink(st, locked[i] ? col::kText3 : col::kText), palette_colour(info.ink), selected),
                 info.name.data());
        if (tapped(pressed)) {
            touched = static_cast<int>(i);
            if (!locked[i]) {
                model.app.icon_colour = info.id;
            }
        }
    }
    if (any_locked && touched >= 0 && locked[static_cast<std::size_t>(touched)]) {
        const Box room = box_at(origin, g.hint);
        const std::vector<std::string>& lines =
            hint_lines(k, words, etude::kIconColours[static_cast<std::size_t>(touched)], room.w());
        const float line_h = picker_tok::kHintH * k.u;
        float y = room.y0;
        for (const std::string& line : lines) {
            label_at(k, dl, k.regular, words.label, {room.x0, y, room.x1, y + line_h}, LabelAlign::Centre,
                     col::kText2, line.c_str());
            y += line_h;
        }
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({w, g.height});
}

} // namespace etude::app::ui
