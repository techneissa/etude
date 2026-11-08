// SPDX-License-Identifier: GPL-3.0-or-later
// The Effects page's hold menu: a card of one or two rows under the swatch or the saved palette a
// hold (or a right click) opened it on.
#include "colour_widgets_internal.hpp"

#include "bar.hpp" // begin_layer
#include "top_menu_layout.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront

#include <string>

namespace etude::app::ui {

using colour_detail::g_menu;
using colour_detail::HoldMenu;
using colour_detail::palette_base_name;
using colour_detail::swatch_labels;

namespace {

enum class Pick : std::uint8_t { None, ResetSwatch, SaveSlot, ClearSlot };

struct Item {
    Pick pick = Pick::None;
    const char* id = "";
    const char* glyph = "";
    const char* words = "";
    bool enabled = true; // a row that would change nothing is dimmed and says why when tapped
};

// The rows of the open menu; their count.
int menu_items(const LookSettings& look, Item (&out)[2]) {
    if (g_menu.kind == HoldMenu::Kind::Swatch) {
        out[0] = {Pick::ResetSwatch, "##menu-reset-swatch", icon::kReset, "Reset to default",
                  !swatch_is_palette_colour(look, look.color_scheme, g_menu.index)};
        return 1;
    }
    out[0] = {Pick::SaveSlot, "##menu-save-slot", icon::kSave, "Save current colours here", true};
    out[1] = {Pick::ClearSlot, "##menu-clear-slot", icon::kClear, "Clear", true};
    return 2;
}

// One row: its hit area is the whole row, its face the row less the inset.
bool draw_item(Kit& k, ImDrawList* dl, const Item& item, const Box& area) {
    const bool pressed = press_area(k, item.id, area);
    const State st = item_state(k, false, true);
    const float inset = k.s(tok::kInset) * 0.5f;
    const Box face{area.x0, area.y0 + inset, area.x1, area.y1 - inset};
    draw_state(k, dl, item.id, face, area, st);
    audit::note(item.id, {area.x0, area.y0}, {area.x1, area.y1}, InkKind::Target);
    const float icon_size = k.s(tok::kIcon);
    const ImVec4 base = item.enabled ? col::kText : with_alpha(col::kText3, 0.7f);
    const ImVec2 at{area.x0 + k.s(tok::kS3) + icon_size * 0.5f, area.cy()};
    if (!audit::icons_hidden()) {
        glyph(k, dl, item.glyph, at, icon_size, state_ink(st, base));
    }
    const float x = area.x0 + k.s(tok::kS3) + icon_size + k.s(tok::kS3);
    // The card is as wide as its longest words need, which they fill exactly: a pixel of slack keeps
    // rounding from cutting them.
    const FitText shown =
        fit(k.regular, k.s(tok::kBody), item.words, area.x1 - k.s(tok::kS3) - x + 1.0f);
    label_at(k, dl, k.regular, k.s(tok::kBody), {x, face.y0, area.x1 - k.s(tok::kS3), face.y1},
             LabelAlign::Left, state_ink(st, base), shown.c_str());
    return tapped(pressed);
}

// Overwrites or clears a filled slot in one tap, with a toast whose Undo puts the slots back. `look`
// is the live look (the model's, which outlives the toast).
void undoable_slot_change(Kit& k, LookSettings& look, int slot, bool clear) {
    const SavedPaletteState before = saved_palette_state(look);
    if (clear) {
        clear_palette_slot(look, slot);
    } else if (!save_palette_slot(look, slot)) {
        return;
    }
    LookSettings* live = &look;
    k.say_undoable("Saved " + std::to_string(slot + 1) + (clear ? " cleared" : " replaced"),
                   [live, before] { restore_saved_palettes(*live, before); });
}

// What a pick does, and what the toast says about it.
void act(Kit& k, LookSettings& look, const Item& item) {
    const ColorScheme scheme = look.color_scheme;
    std::array<char, 16> inside{};
    std::array<char, 32> name{};
    switch (item.pick) {
    case Pick::ResetSwatch:
        swatch_labels(look, scheme, g_menu.index, inside, name);
        if (item.enabled) {
            const std::string base = palette_base_name(look, scheme);
            reset_swatch(look, scheme, g_menu.index);
            k.say(std::string(name.data()) + " is back to " + base);
        } else {
            k.say(std::string(name.data()) + " already has its " + palette_base_name(look, scheme) +
                  " colour");
        }
        break;
    case Pick::SaveSlot:
        undoable_slot_change(k, look, g_menu.index, false);
        break;
    case Pick::ClearSlot:
        undoable_slot_change(k, look, g_menu.index, true);
        break;
    case Pick::None:
        break;
    }
}

} // namespace

namespace colour_detail {

std::string palette_base_name(const LookSettings& look, ColorScheme scheme) {
    if (scheme == ColorScheme::ByPitchClass && saved_palette_filled(look, look.note_slot)) {
        return "Saved " + std::to_string(look.note_slot + 1);
    }
    return std::string(palette_name(look.palettes[static_cast<std::size_t>(scheme)]));
}

std::string active_palette_name(const ActivePalette& active) {
    if (active.slot >= 0) {
        return "Saved " + std::to_string(active.slot + 1);
    }
    return active.built_in ? std::string(palette_name(*active.built_in)) : std::string("Custom");
}

void follow_hold_menu_cell(HoldMenu::Kind kind, int index, ImVec2 min, ImVec2 max) {
    if (g_menu.kind != kind || g_menu.index != index) {
        return;
    }
    if (cell_moved({g_menu.min.x, g_menu.min.y, g_menu.max.x, g_menu.max.y},
                   {min.x, min.y, max.x, max.y})) {
        g_menu.kind = HoldMenu::Kind::None;
    }
}

void open_hold_menu(HoldMenu::Kind kind, int index, ImVec2 min, ImVec2 max) {
    g_menu.kind = kind;
    g_menu.index = index;
    g_menu.min = min;
    g_menu.max = max;
    g_menu.outside = OutsideTap{};
}

void hold_menu(Kit& k, LookSettings& look) {
    const int frame = ImGui::GetFrameCount();
    if (g_menu.kind != HoldMenu::Kind::None && g_menu.frame < frame - 1 && g_menu.frame >= 0) {
        g_menu.kind = HoldMenu::Kind::None; // the page was away: a menu left open goes
    }
    g_menu.frame = frame;
    // A menu on a swatch that is no longer there (another scheme) goes too.
    if (g_menu.kind == HoldMenu::Kind::Swatch && g_menu.index >= swatch_count(look.color_scheme)) {
        g_menu.kind = HoldMenu::Kind::None;
    }
    if (g_menu.kind == HoldMenu::Kind::None) {
        g_menu.card_max = g_menu.card_min = {};
        return;
    }
    Item items[2];
    const int count = menu_items(look, items);
    float word_w = 0.0f;
    for (int i = 0; i < count; ++i) {
        word_w = std::max(word_w, text_size(k.regular, k.s(tok::kBody), items[i].words).x);
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // The card hangs a dead gap in from the cell's left edge, so its rows keep 2 mm clear of the
    // swatch beside the cell.
    const TopMenuLayout lay =
        top_menu_layout(display.x, display.y, g_menu.max.y + k.s(tok::kS1),
                        g_menu.min.x + k.s(tok::kHitGap), count, word_w, k.u);
    std::optional<Item> picked;
    if (begin_layer("##colours-menu", {lay.card.x0, lay.card.y0}, {lay.card.w(), lay.card.h()})) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        for (int i = 0; i < count; ++i) {
            if (draw_item(k, dl, items[i], lay.row[static_cast<std::size_t>(i)])) {
                picked = items[i];
            }
        }
        dl->ChannelsSetCurrent(0);
        const ImVec2 lo{lay.card.x0, lay.card.y0};
        const ImVec2 hi{lay.card.x1, lay.card.y1};
        g_menu.card_min = lo;
        g_menu.card_max = hi;
        k.toast_avoid({lo.x, lo.y, lay.card.w(), lay.card.h()});
        overlay_card(k, dl, lo, hi, k.s(tok::kRadiusCard), col::kRaised);
        dl->ChannelsMerge();
        if (g_menu.outside.away(true, lo, hi)) {
            g_menu.kind = HoldMenu::Kind::None;
        }
    }
    ImGui::End();
    if (picked) {
        act(k, look, *picked);
        g_menu.kind = HoldMenu::Kind::None;
    }
}

} // namespace colour_detail

} // namespace etude::app::ui
