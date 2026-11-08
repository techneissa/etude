// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_screen.hpp"

#include "library_screen_shared.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "preview.hpp"
#include "../sheet/sheet_preview.hpp"
#include "settings_catalogue.hpp"

#include "etude/level_stars.hpp"
#include "etude/tap_rule.hpp"
#include "etude/unit_format.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront, the IME request

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace etude::app::ui {

using namespace lib_shared;

void LibraryScreen::draw_search(Kit& k, LibraryCatalogue& cat, const Box& box) {
    const float hit = k.s(tok::kHit);
    const bool has_text = !query_.empty();
    const float field_r = has_text ? box.x1 - hit - k.s(tok::kHitGap) : box.x1;
    ImGui::SetCursorScreenPos({box.x0, box.y0});
    const bool pressed = ImGui::InvisibleButton("##library-search", {field_r - box.x0, box.h()});
    const bool focus = edit_ == Edit::Search;
    State st = item_state(k, false);
    st.focus = st.focus || focus;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box face{box.x0, box.y0, box.x1, box.y1};
    draw_state(k, dl, "##library-search", face, face, st, Idle::Fill);
    note("##library-search", {box.x0, box.y0}, {field_r, box.y1}, InkKind::Target);
    if (pressed) {
        edit_ = Edit::Search;
        keys_ = by_touch();
        over_ = Over::None;
    }
    const float cy = box.cy();
    const float icon_x = box.x0 + k.s(tok::kS4) + k.s(tok::kIcon) * 0.5f;
    glyph(k, dl, lib_icon::kSearch, {icon_x, cy}, k.s(tok::kIcon),
          focus ? col::kAccent : col::kText2);
    const float tx = icon_x + k.s(tok::kIcon) * 0.5f + k.s(tok::kS3);
    const float right = field_r - k.s(tok::kS3);
    FitText hint;
    if (joining_) {
        hint.assign("Search for the song it is the same piece as");
    } else if (cat.size() > 0) {
        hint.format("Search %zu songs", cat.size());
    } else {
        hint.assign("Search songs");
    }
    osk::field_text(k, dl, {tx, box.y0, right, box.y1}, query_, hint.c_str(), focus);
    if (has_text) {
        if (round_button(k, "##library-clear", icon::kClose, {box.x1 - hit * 0.5f, cy}, false)) {
            query_.clear();
        }
    }
}

void LibraryScreen::draw_chips(Kit& k, UiModel& model, LibraryCatalogue& cat, const Box& box) {
    (void)cat;
    BrowseOrder& order = model.app.library;
    const float gap = k.s(tok::kS2);
    float x = box.x0;
    const float y = box.y0;
    // Sort: its name and an arrow for its direction.
    {
        const char* name = sort_name(order.sort);
        const char* arrow = order.reversed ? icon::kArrowUp : icon::kArrowDown;
        const float w = chip_width(k, sort_icon(order.sort), name, arrow);
        catalogue_sweep::drawn(Setting::LibrarySort);
        if (text_chip(k, "##library-sort", sort_icon(order.sort), name, arrow, {x, y}, w,
                      over_ == Over::Sort)) {
            over_ = Over::Sort;
            anchor_ = {x, box.y1};
        }
        long_press_tip(k, "Sort");
        x += w + gap;
    }
    // Group: "Group", or what it groups by.
    {
        const bool grouped = order.group != BrowseGroup::None;
        const char* name = grouped ? group_name(order.group) : "Group";
        const float w = chip_width(k, lib_icon::kGroup, name, nullptr);
        catalogue_sweep::drawn(Setting::LibraryGroup);
        if (text_chip(k, "##library-group", lib_icon::kGroup, name, nullptr, {x, y}, w,
                      over_ == Over::Group || grouped)) {
            over_ = Over::Group;
            anchor_ = {x, box.y1};
        }
        long_press_tip(k, "Group");
        x += w + gap;
    }
    // A hairline between the order and the filters.
    ImGui::GetWindowDrawList()->AddLine({x, y + k.s(tok::kS3)}, {x, box.y1 - k.s(tok::kS3)},
                                        u32(col::kHairline), k.s(tok::kHairline));
    x += gap;
    // The Filters chip stands at the end of the row; the quick filters take what is left, in
    // words, then as icons, and those with no room at all go into its popover.
    const int facets = facet_count(order);
    FitText filters_text;
    if (facets > 0) {
        filters_text.format("Filters %d", facets);
    } else {
        filters_text.assign("Filters");
    }
    float filters_w = chip_width(k, lib_icon::kFunnel, filters_text.view(), nullptr);
    if (box.x1 - x < filters_w + 2.0f * k.s(tok::kHit) + 2.0f * gap) {
        filters_w = chip_width(k, lib_icon::kFunnel, "", nullptr); // a narrow row: the icon
    }
    const float limit = box.x1 - filters_w - gap;
    catalogue_sweep::drawn(Setting::LibraryFilter);
    folded_ = 0;
    for (const FilterChip& c : kFilters) {
        float w = chip_width(k, c.glyph_text, c.text, nullptr);
        const char* text = c.text;
        // A row too narrow for the words keeps the icon.
        if (x + w > limit && c.glyph_text != nullptr) {
            text = "";
            w = chip_width(k, c.glyph_text, text, nullptr);
        }
        if (x + w > limit) {
            folded_ = static_cast<std::uint8_t>(folded_ | (1u << static_cast<unsigned>(c.filter)));
            continue;
        }
        if (text_chip(k, c.id, c.glyph_text, text, nullptr, {x, y}, w, order.filter == c.filter)) {
            order.filter = c.filter;
        }
        long_press_tip(k, c.label);
        x += w + gap;
    }
    // Filters: the level and hands, and the folded quick filters.
    {
        catalogue_sweep::drawn(Setting::LibraryFacets);
        const bool folded_on =
            (folded_ & (1u << static_cast<unsigned>(order.filter))) != 0 &&
            order.filter != BrowseFilter::All;
        const float fx = box.x1 - filters_w;
        if (text_chip(k, "##library-filters", lib_icon::kFunnel,
                      filters_w > k.s(tok::kHit) ? filters_text.view() : std::string_view(),
                      nullptr, {fx, y}, filters_w,
                      over_ == Over::Filters || facets > 0 || folded_on)) {
            over_ = Over::Filters;
            anchor_ = {fx + filters_w, box.y1};
        }
        long_press_tip(k, "Filters");
    }
}

} // namespace etude::app::ui
