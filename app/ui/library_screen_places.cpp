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

// A row that scrolls sideways under a finger (a shelf, the places): the drag and its coasting, and
// a trackpad's sideways scroll. `id` names the row; `offset` is its scroll, `max` its end.
void LibraryScreen::sideways(Kit& k, int id, const Box& row, float& offset, float max) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         row.holds({io.MousePos.x, io.MousePos.y, io.MousePos.x, io.MousePos.y});
    if (ImGui::IsMouseClicked(0) && hovered) {
        side_touch_ = id;
        side_drag_.set_axis(ScrollDrag::Axis::Horizontal, true);
        side_drag_.press(io.MousePos.x / k.u, io.MousePos.y / k.u, k.now,
                         io.MouseSource != ImGuiMouseSource_Mouse);
    }
    if (hovered && io.MouseWheelH != 0.0f) {
        offset -= io.MouseWheelH * k.s(lib_tok::kSideWheel);
    }
    if (side_touch_ == id) {
        side_seen_ = true;
        if (ImGui::IsMouseDown(0)) {
            side_drag_.move(io.MousePos.x / k.u, io.MousePos.y / k.u, k.now);
        } else {
            side_drag_.release(io.MousePos.x / k.u, io.MousePos.y / k.u, k.now);
        }
        const ScrollDrag::Step step =
            side_drag_.update(k.now, false, offset / k.u, std::max(0.0f, max) / k.u);
        if (step.took_touch) {
            ImGui::ClearActiveID(); // the card under the finger lets go
        }
        offset += step.scroll_by * k.u;
        if (!side_drag_.busy()) {
            side_touch_ = -1;
        }
    }
    offset = std::clamp(offset, 0.0f, std::max(0.0f, max));
}

float LibraryScreen::places_height(const Kit& k, const LibraryCatalogue& cat,
                                   BrowseView view) const {
    const bool scoped = cat.scope().kind != LibraryScope::Kind::All;
    const bool places = !cat.searching() && view != BrowseView::Shelves &&
                        (cat.scope().kind == LibraryScope::Kind::All ||
                         cat.scope().kind == LibraryScope::Kind::Folder) &&
                        (!cat.collections().empty() || cat.inbox_count() > 0 ||
                         (cat.scope().kind == LibraryScope::Kind::All && cat.group_count() > 0));
    // The All scope always has its row: the Progress chip is in it.
    const bool progress_chip = !cat.searching() && view != BrowseView::Shelves &&
                               cat.scope().kind == LibraryScope::Kind::All;
    return scoped || places || progress_chip ? k.s(tok::kHit) + k.s(tok::kS2) : 0.0f;
}

// Back, then where this is: Songs > Classical > Baroque (the places row inside a folder, the Inbox
// or a group). `chip` draws one chip of the row at its running position `x`.
void LibraryScreen::draw_place_crumbs(Kit& k, LibraryCatalogue& cat, const PlaceChip& chip,
                                      std::optional<LibraryScope>& go, const float& x, float y) {
    const LibraryScope& scope = cat.scope();
    const float hit = k.s(tok::kHit);
    if (chip("##place-back", icon::kBack, "", nullptr, false)) {
        if (scope.kind == LibraryScope::Kind::Folder && crumbs_.size() > 1) {
            LibraryScope up;
            up.kind = LibraryScope::Kind::Folder;
            up.path.assign(crumbs_[crumbs_.size() - 2].path);
            go = up;
        } else {
            go = LibraryScope{};
        }
    }
    long_press_tip(k, "Back");
    if (chip("##place-songs", lib_icon::kFolder, "Songs", nullptr, false)) {
        go = LibraryScope{};
    }
    if (scope.kind == LibraryScope::Kind::Folder) {
        for (std::size_t c = 0; c < crumbs_.size(); ++c) {
            ImGui::PushID(static_cast<int>(c));
            const bool last = c + 1 == crumbs_.size();
            if (chip("##place-crumb", nullptr, crumbs_[c].name, nullptr, last) && !last) {
                LibraryScope to;
                to.kind = LibraryScope::Kind::Folder;
                to.path.assign(crumbs_[c].path);
                go = to;
            }
            ImGui::PopID();
        }
    } else if (scope.kind == LibraryScope::Kind::Inbox) {
        chip("##place-here", lib_icon::kInbox, "Inbox", nullptr, true);
    } else if (const std::optional<std::size_t> g = cat.group_index(scope.group)) {
        chip("##place-here", lib_icon::kGroup, cat.group_name(*g), nullptr, true);
        const float mx = x;
        if (chip("##place-group-menu", lib_icon::kMore, "", nullptr, over_ == Over::GroupMenu)) {
            menu_group_ = scope.group;
            open_for(Over::GroupMenu, false, {mx, y + hit});
        }
        long_press_tip(k, "Group");
    }
}

void LibraryScreen::draw_places(Kit& k, LibraryPort& port, ImVec2 at, float width) {
    LibraryCatalogue& cat = port.catalogue();
    const LibraryScope& scope = cat.scope(); // changed only after the row, by `go`
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const float pad = k.s(lib_tok::kSidePad);
    const Box row{at.x, at.y, at.x + width, at.y + hit};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Everything in the row, laid out from its left end; the row scrolls when it is longer.
    float x = row.x0 + pad - places_x_;
    const float y = row.y0;
    dl->PushClipRect({row.x0, row.y0}, {row.x1, row.y1}, true);
    std::optional<LibraryScope> go;
    const PlaceChip chip = [&](const char* id, const char* glyph_text, std::string_view text,
                          const char* trail, bool on) {
        const float w = chip_width(k, glyph_text, text, trail);
        bool pressed = false;
        if (x + w > row.x0 && x < row.x1) {
            pressed = text_chip(k, id, glyph_text, text, trail, {x, y}, w, on);
        }
        x += w + gap;
        return pressed;
    };
    const bool searching = cat.searching();
    if (scope.kind != LibraryScope::Kind::All) {
        draw_place_crumbs(k, cat, chip, go, x, y);
    }
    const bool places = !searching && view_ != BrowseView::Shelves &&
                        (scope.kind == LibraryScope::Kind::All ||
                         scope.kind == LibraryScope::Kind::Folder);
    if (places) {
        FitText text;
        // The Inbox first while it holds new imports.
        if (scope.kind == LibraryScope::Kind::All && cat.inbox_count() > 0) {
            text.format("Inbox %zu", cat.inbox_count());
            if (chip("##place-inbox", lib_icon::kInbox, text.view(), nullptr, true)) {
                LibraryScope to;
                to.kind = LibraryScope::Kind::Inbox;
                go = to;
            }
        }
        if (scope.kind == LibraryScope::Kind::All &&
            chip("##place-progress", icon::kGauge, "Progress", nullptr, false)) {
            progress_.open(false);
        }
        const std::vector<Collection>& cols = cat.collections();
        for (std::size_t c = 0; c < cols.size(); ++c) {
            ImGui::PushID(static_cast<int>(c));
            text.format("%s %u", cols[c].name.c_str(), cols[c].songs);
            if (chip("##place-folder", lib_icon::kFolder, text.view(), nullptr, false)) {
                LibraryScope to;
                to.kind = LibraryScope::Kind::Folder;
                to.path = cols[c].path;
                go = to;
            }
            ImGui::PopID();
        }
        if (scope.kind == LibraryScope::Kind::All) {
            for (std::size_t g = 0; g < cat.group_count(); ++g) {
                ImGui::PushID(static_cast<int>(g));
                text.format("%.*s %u", static_cast<int>(cat.group_name(g).size()),
                            cat.group_name(g).data(), cat.group_size(g));
                if (chip("##place-group", lib_icon::kGroup, text.view(), nullptr, false)) {
                    LibraryScope to;
                    to.kind = LibraryScope::Kind::Group;
                    to.group = cat.group_id(g);
                    go = to;
                }
                ImGui::PopID();
            }
        }
    }
    dl->PopClipRect();
    const float content = x - gap + places_x_ + pad - row.x0;
    sideways(k, kPlacesRow, row, places_x_, content - width);
    if (go) {
        set_scope(cat, *go);
    }
}

} // namespace etude::app::ui
