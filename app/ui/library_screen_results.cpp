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

void LibraryScreen::draw_results(Kit& k, UiModel& model, LibraryPort& port, const Box& box,
                                 bool compact) {
    if (!(box.h() >= 1.0f)) {
        return; // no room under the keyboard (a child window of no height would fill the screen)
    }
    side_seen_ = false;
    compact_results_ = compact;
    if (compact) {
        draw_flat(k, model, port, box, BrowseView::List);
    } else if (view_ == BrowseView::Shelves) {
        draw_shelves(k, model, port, box);
    } else {
        draw_flat(k, model, port, box, view_);
    }
    if (side_touch_ >= 0 && !side_seen_) {
        side_drag_.cancel(); // its row went out of view
        side_touch_ = -1;
    }
}

void LibraryScreen::draw_flat(Kit& k, UiModel& model, LibraryPort& port, const Box& box,
                              BrowseView view) {
    LibraryCatalogue& cat = port.catalogue();
    const bool compact = compact_results_;
    const bool list = view == BrowseView::List;
    const float pad = k.s(lib_tok::kSidePad);
    const float gap = list ? k.s(lib_tok::kListGap) : k.s(lib_tok::kCardGap);
    // Cards that have versions carry a row of chips: taller, and wider while some piece has them.
    const bool versions = !list && cat.has_versions();
    const float card_h = compact ? k.s(tok::kHit)
                         : list  ? k.s(lib_tok::kRowH)
                                 : card_height(k.u, versions);
    const float head_h = k.s(lib_tok::kHeadH);
    // What the grid shows: the shown list, or the nearest matches of a search that found none.
    const bool near = cat.searching() && cat.shown().empty() && !cat.near().empty();
    const std::vector<std::uint32_t>& order = near ? near_order_ : cat.shown();
    if (near) {
        near_order_.assign(cat.near().begin(), cat.near().end());
        near_sections_.assign(1, {0, 0, static_cast<std::uint32_t>(near_order_.size())});
    }
    const std::vector<BrowseSection>& sections = near ? near_sections_ : cat.sections();
    const bool headers = near || (!cat.searching() && cat.order().group != BrowseGroup::None);
    // Sorted by name, the A to Z index takes a hit's width at the right edge (not while the
    // keyboard leaves the results less than a card's height).
    const bool alpha =
        !near && cat.has_alpha() && order.size() > 1 && box.h() >= k.s(lib_tok::kCardH);
    const float alpha_w = alpha ? k.s(tok::kHit) : 0.0f;
    const Box cards{box.x0, box.y0, box.x1 - alpha_w, box.y1};
    const float width = cards.w();
    if (compact && !rows_compact_) {
        scroll_top_ = true; // the strip opens on its first row, whole
    }
    if (rows_version_ != cat.view_version() || rows_width_ != width || rows_unit_ != k.u ||
        rows_near_ != near || rows_view_ != view || rows_versions_ != versions ||
        rows_compact_ != compact) {
        rows_compact_ = compact;
        rows_version_ = cat.view_version();
        rows_width_ = width;
        rows_unit_ = k.u;
        rows_near_ = near;
        rows_view_ = view;
        rows_versions_ = versions;
        if (compact) {
            // The strip's one row has the whole width (it is a row of its own, not a list).
            columns_ = grid_columns(width, lib_tok::kListRowMinW * k.u, gap, 0.0f);
        } else if (list) {
            columns_ = list_columns(width, k.u);
        } else {
            columns_ = grid_columns(width, card_min_width(k.u, versions), gap,
                                    alpha ? pad - k.s(tok::kS2) : pad);
        }
        rows_.build(sections, columns_.columns, card_h, gap, head_h, headers);
    }
    if (alpha) {
        draw_alpha(k, cat, {cards.x1, box.y0, box.x1, box.y1}, grid_top_);
    } else {
        alpha_slot_ = -1;
    }

    ImGui::SetCursorScreenPos({cards.x0, cards.y0});
    ImGui::BeginChild("##library-grid", {cards.w(), cards.h()}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (scroll_top_) {
        scroll_top_ = false;
        ImGui::SetScrollY(0.0f);
    }
    if (alpha_jump_ >= 0.0f) {
        ImGui::SetScrollY(alpha_jump_);
        alpha_jump_ = -1.0f;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float top_pad = compact ? 0.0f : tok::kS1 * k.u; // the strip is one row, no room to spare
    const float scroll = keep_scroll(k, model);
    const float view_h = cards.h();
    // The places (the Inbox, the collections, the groups, or the way back) above the songs.
    const float places_h = compact ? 0.0f : places_height(k, cat, view); // none in the strip
    if (places_h > 0.0f) {
        draw_places(k, port, {origin.x, origin.y + top_pad - scroll}, cards.w());
    }
    // A line over the cards when the list is empty, or says what the search found.
    float lead = places_h > 0.0f ? top_pad + places_h : 0.0f;
    const auto message = [&](const char* glyph_text, const char* line, const char* sub) {
        if (compact) {
            // The strip is one hit tall: the line alone, no picture.
            const FitText cut = fit(k.bold, k.s(tok::kLabel), line, cards.w() - 2.0f * pad);
            label_at(k, dl, k.bold, k.s(tok::kLabel),
                     {origin.x + pad, origin.y - scroll, origin.x + cards.w() - pad,
                      origin.y - scroll + k.s(tok::kHit)},
                     LabelAlign::Left, col::kText2, cut.c_str());
            return k.s(tok::kHit);
        }
        const float y = origin.y + std::max(top_pad, lead) + k.s(tok::kS5) - scroll;
        const float cx = origin.x + cards.w() * 0.5f;
        if (glyph_text != nullptr) {
            glyph(k, dl, glyph_text, {cx, y + k.s(tok::kIconLg) * 0.5f}, k.s(tok::kIconLg),
                  col::kText3);
        }
        const float ts = k.s(tok::kBody);
        const ImVec2 sz = text_size(k.bold, ts, line);
        const float ly = y + k.s(tok::kIconLg) + k.s(tok::kS3);
        const Box line_slot{origin.x, ly, origin.x + cards.w(), ly + sz.y};
        label_at(k, dl, k.bold, ts, line_slot, LabelAlign::Centre, col::kText2, line);
        float bottom = ly + sz.y;
        if (sub != nullptr) {
            const float ss = k.s(tok::kLabel);
            const FitText cut = fit(k.regular, ss, sub, cards.w() - 2.0f * pad);
            const ImVec2 s2 = text_size(k.regular, ss, cut.c_str());
            label_at(k, dl, k.regular, ss,
                     {origin.x, bottom + k.s(tok::kS2), origin.x + cards.w(),
                      bottom + k.s(tok::kS2) + s2.y},
                     LabelAlign::Centre, col::kText3, cut.c_str());
            bottom += k.s(tok::kS2) + s2.y;
        }
        return bottom - (origin.y - scroll);
    };
    const bool filtered = cat.order().filter != BrowseFilter::All || facet_count(cat.order()) > 0;
    if (cat.size() == 0) {
        lead = message(icon::kListMusic, "No songs yet",
                       "Put song files (MIDI, MusicXML, MuseScore, ABC) in the Songs folder");
    } else if (order.empty() && cat.searching() && !port.catalogue_hits().empty()) {
        // The catalogues have it: a line, so their matches come up into view.
        FitText line;
        line.format("None of your songs match \xE2\x80\x9C%s\xE2\x80\x9D", cat.query().c_str());
        const float ts = k.s(tok::kLabel);
        const FitText cut = fit(k.regular, ts, line.view(), cards.w() - 2.0f * pad);
        const float ly = std::max(top_pad, lead) + k.s(tok::kS2);
        const float line_h = text_size(k.regular, ts, cut.c_str()).y;
        const float line_y = origin.y + ly - scroll;
        label_at(k, dl, k.regular, ts,
                 {origin.x + pad, line_y, origin.x + cards.w() - pad, line_y + line_h},
                 LabelAlign::Left, col::kText2, cut.c_str());
        lead = ly + line_h;
    } else if (order.empty() && cat.searching()) {
        FitText line;
        line.format("No songs match \xE2\x80\x9C%s\xE2\x80\x9D", cat.query().c_str());
        lead = message(lib_icon::kSearch, line.c_str(), nullptr);
    } else if (order.empty()) {
        lead = message(lib_icon::kFunnelX, "No songs here", nullptr);
    } else if (near) {
        FitText line;
        line.format("No songs match \xE2\x80\x9C%s\xE2\x80\x9D. The closest:", cat.query().c_str());
        const float ts = k.s(tok::kLabel);
        const FitText cut = fit(k.regular, ts, line.view(), cards.w() - 2.0f * pad);
        const float ly = std::max(top_pad, lead) + k.s(tok::kS2);
        text_at(k, dl, k.regular, ts, {origin.x + pad, origin.y + ly - scroll}, col::kText2,
                cut.c_str());
        lead = ly + text_size(k.regular, ts, cut.c_str()).y;
    }
    // "Clear filters" under a message when a filter hides songs.
    if (filtered && (order.empty() || near) && !compact) {
        const float bw = k.s(lib_tok::kPopoverW) * 0.66f;
        const ImVec2 pos{origin.x + (cards.w() - bw) * 0.5f,
                         origin.y + lead + k.s(tok::kS3) - scroll};
        if (action_button(k, "##library-clear-filters", lib_icon::kFunnelX, "Clear filters", pos,
                          {bw, k.s(tok::kHit)}, true)) {
            clear_filters_ = true; // the order lives in the model, which draw() holds
        }
        lead += k.s(tok::kS3) + k.s(tok::kHit);
    }
    const float grid_top = lead > 0.0f ? lead + k.s(tok::kS2) : top_pad;
    grid_top_ = grid_top;
    // Scroll the open song into view the first time, and a page on PageUp/PageDown.
    if (reveal_current_ && !order.empty()) {
        reveal_current_ = false;
        if (const std::optional<std::size_t> cur = port.current()) {
            // The card of the piece the open song is a version of.
            const auto it = std::find(order.begin(), order.end(),
                                      static_cast<std::uint32_t>(cat.card_of(*cur)));
            if (it != order.end()) {
                const GridRows::Place p =
                    rows_.place(static_cast<std::uint32_t>(it - order.begin()));
                ImGui::SetScrollY(std::max(0.0f, grid_top + p.y - (view_h - card_h) * 0.5f));
            }
        }
    }
    if (page_ != 0) {
        ImGui::SetScrollY(std::max(0.0f, scroll + static_cast<float>(page_) * (view_h - card_h)));
        page_ = 0;
    }
    // Only the cards in view are drawn.
    const GridRows::Range range =
        rows_.visible(scroll - grid_top - card_h, scroll - grid_top + view_h + card_h);
    for (std::uint32_t r = range.first; r < range.last; ++r) {
        const GridRows::Place p = rows_.place(r);
        const ImVec2 a{origin.x + columns_.x(p.column), origin.y + grid_top + p.y - scroll};
        const ImVec2 b{a.x + columns_.card_w, a.y + card_h};
        if (list) {
            draw_row(k, port, order[r], a, b);
        } else {
            draw_card(k, port, order[r], a, b);
        }
    }
    // Group headers: in their place, and the current one stuck to the top.
    if (headers) {
        draw_group_heads(k, dl, cat, sections, near, {origin.x, origin.y - scroll},
                         {cards.w(), view_h}, grid_top, scroll);
    }
    // Get songs: the catalogues' matches under the library's own.
    const float get_top = grid_top + (order.empty() ? 0.0f : rows_.content_height());
    const float get_h =
        draw_get_songs(k, model, port, {origin.x, origin.y + get_top - scroll}, cards.w());
    // The scroll range: everything above, then a pad under the last row.
    ImGui::SetCursorPos({0.0f, get_top + std::max(get_h, order.empty() ? 0.0f : pad)});
    ImGui::Dummy({1.0f, 1.0f});
    // A drag that starts on the places row may be its own, sideways.
    const ImVec2 press_at = ImGui::GetIO().MouseClickedPos[0];
    const float places_y = origin.y + top_pad - scroll;
    drag_scroll(k, places_h > 0.0f && press_at.y >= places_y &&
                       press_at.y < places_y + k.s(tok::kHit));
    ImGui::EndChild();
}

// The group headers of the flat view: each in its place over its songs, the one in force stuck to
// the top of the window. `at` is the content's top left (scrolled), `size` the window's.
void LibraryScreen::draw_group_heads(Kit& k, ImDrawList* dl, LibraryCatalogue& cat,
                                     const std::vector<BrowseSection>& sections, bool near,
                                     ImVec2 at, ImVec2 size, float grid_top, float scroll) {
    const float pad = k.s(lib_tok::kSidePad);
    const float head_h = k.s(lib_tok::kHeadH);
    const float top = at.y + scroll; // the window's top
    const GridRows::Sticky sticky = rows_.sticky(scroll - grid_top);
    for (std::size_t sec = 0; sec < sections.size(); ++sec) {
        const float y = at.y + grid_top + rows_.section_top(sec);
        const bool stuck = static_cast<int>(sec) == sticky.section;
        const float hy = stuck ? top + grid_top + sticky.y - scroll : y;
        if (hy + head_h < top || hy > top + size.y) {
            continue;
        }
        if (stuck) {
            dl->AddRectFilled({at.x, hy}, {at.x + size.x, hy + head_h}, u32(kScreenBg));
        }
        const std::string_view label =
            near ? std::string_view("Closest matches") : cat.section_label(sections[sec]);
        const float ts = k.s(tok::kLabel);
        FitText count;
        count.format("%u", sections[sec].end - sections[sec].begin);
        const ImVec2 cs = text_size(k.regular, ts, count.c_str());
        const FitText name = fit(k.bold, ts, label, size.x - 3.0f * pad - cs.x);
        const ImVec2 ns = text_size(k.bold, ts, name.c_str());
        label_at(k, dl, k.bold, ts, {at.x + pad, hy, at.x + pad + ns.x, hy + head_h},
                 LabelAlign::Left, col::kText2, name.c_str());
        const float count_x = at.x + pad + ns.x + k.s(tok::kS2);
        label_at(k, dl, k.regular, ts, {count_x, hy, count_x + cs.x, hy + head_h},
                 LabelAlign::Left, col::kText3, count.c_str());
    }
}

void LibraryScreen::draw_alpha(Kit& k, LibraryCatalogue& cat, const Box& strip, float grid_top) {
    static constexpr const char* kLetters[kAlphaSlots] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
        "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "#"};
    const AlphaIndex& index = cat.alpha();
    const float top = strip.y0 + k.s(tok::kS1);
    const float bottom = strip.y1 - k.s(tok::kS1);
    const float slot_h = (bottom - top) / static_cast<float>(kAlphaSlots);
    ImGui::SetCursorScreenPos({strip.x0, strip.y0});
    ImGui::InvisibleButton("##library-alpha", {strip.w(), strip.h()});
    const bool held = ImGui::IsItemActive();
    note("##library-alpha", {strip.x0, strip.y0}, {strip.x1, strip.y1}, InkKind::Target);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box face{strip.x0 + k.s(tok::kS1), top, strip.x1 - k.s(tok::kS1), bottom};
    draw_state(k, dl, "##library-alpha", face, strip, item_state(k, false), Idle::None);
    // Every letter when there is room for them, else every second or third one, a line apart (the
    // finger still reaches every letter: the index follows it, not the letters drawn).
    const float size = k.s(tok::kCaption);
    const int step = std::max(1, static_cast<int>(std::ceil(size * lib_tok::kAlphaPitch / slot_h)));
    for (int sl = 0; sl < kAlphaSlots; sl += step) {
        const bool has = index[static_cast<std::size_t>(sl)] >= 0;
        label_at(k, dl, k.bold, size,
                 {strip.x0, top + slot_h * static_cast<float>(sl), strip.x1,
                  top + slot_h * static_cast<float>(sl + 1)},
                 LabelAlign::Centre,
                 sl == alpha_slot_ ? col::kAccent : (has ? col::kText2 : col::kText3),
                 kLetters[sl]);
    }
    if (!held) {
        alpha_slot_ = -1;
        return;
    }
    const float my = ImGui::GetIO().MousePos.y;
    const int slot = std::clamp(static_cast<int>((my - top) / slot_h), 0, kAlphaSlots - 1);
    const int target = alpha_target(index, slot);
    if (target >= 0 && target != alpha_slot_) {
        alpha_slot_ = target;
        const std::int32_t first = index[static_cast<std::size_t>(target)];
        alpha_jump_ = std::max(0.0f, grid_top + rows_.place(static_cast<std::uint32_t>(first)).y -
                                         k.s(tok::kS2));
    }
    // The letter beside the finger.
    if (alpha_slot_ >= 0) {
        const float r = k.s(lib_tok::kAlphaBubble) * 0.5f;
        const ImVec2 c{strip.x0 - r - k.s(tok::kS2),
                       std::clamp(my, strip.y0 + r, strip.y1 - r)};
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddCircleFilled(c, r, u32(col::kRaised), 32);
        fg->AddCircle(c, r, u32(col::kHairline), 32, k.s(tok::kHairline));
        const float ls = k.s(tok::kTitle);
        label_at(k, fg, k.bold, ls, {c.x - r, c.y - r, c.x + r, c.y + r}, LabelAlign::Centre,
                 col::kText, kLetters[alpha_slot_]);
    }
}

void LibraryScreen::draw_shelves(Kit& k, UiModel& model, LibraryPort& port, const Box& box) {
    LibraryCatalogue& cat = port.catalogue();
    const float pad = k.s(lib_tok::kSidePad);
    const float gap = k.s(lib_tok::kCardGap);
    const bool versions = cat.has_versions();
    const float card_h = card_height(k.u, versions);
    const float head_h = k.s(tok::kHit);
    const GridColumns grid = grid_columns(box.w(), card_min_width(k.u, versions), gap, pad);
    const float card_w = shelf_card_width(grid.card_w, k.u);
    columns_ = grid;
    const std::vector<LibraryShelf>& shelves = cat.shelves();
    const std::vector<std::uint32_t>& order = cat.shelf_order();
    // Each shelf keeps its sideways scroll by what it is.
    if (sideways_.size() > shelves.size() * 2 + 16) {
        sideways_.clear();
    }
    const auto offset_of = [&](const LibraryShelf& sh) -> float& {
        for (Sideways& sw : sideways_) {
            if (sw.kind == sh.kind && sw.key == sh.key) {
                return sw.x;
            }
        }
        sideways_.push_back({sh.kind, sh.key, 0.0f});
        return sideways_.back().x;
    };

    ImGui::SetCursorScreenPos({box.x0, box.y0});
    ImGui::BeginChild("##library-shelves", {box.w(), box.h()}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (scroll_top_) {
        scroll_top_ = false;
        ImGui::SetScrollY(0.0f);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float scroll = keep_scroll(k, model);
    const float top_pad = tok::kS1 * k.u;
    float top = top_pad;
    const float places_h = places_height(k, cat, BrowseView::Shelves);
    if (places_h > 0.0f) {
        draw_places(k, port, {origin.x, origin.y + top - scroll}, box.w());
        top += places_h;
    }
    if (shelves.empty()) {
        const char* line = cat.size() == 0 ? "No songs yet" : "No songs here";
        const float ts = k.s(tok::kBody);
        const float line_y = origin.y + top + k.s(tok::kS5) - scroll;
        label_at(k, dl, k.bold, ts,
                 {origin.x, line_y, origin.x + box.w(), line_y + text_size(k.bold, ts, line).y},
                 LabelAlign::Centre, col::kText2, line);
    }
    const float pitch = head_h + card_h + gap * 2.0f;
    if (page_ != 0) {
        ImGui::SetScrollY(std::max(0.0f, scroll + static_cast<float>(page_) * (box.h() - pitch)));
        page_ = 0;
    }
    // Only the shelves in view.
    const auto first = static_cast<std::size_t>(std::max(0.0f, (scroll - top) / pitch));
    for (std::size_t si = first; si < shelves.size(); ++si) {
        const float sy = origin.y + top + pitch * static_cast<float>(si) - scroll;
        if (sy > origin.y + box.h()) {
            break;
        }
        const LibraryShelf& sh = shelves[si];
        ImGui::PushID(static_cast<int>(si));
        // The header: its name and count; a collection, a group or the Inbox opens from it.
        const bool opens = sh.kind == LibraryShelf::Kind::Collection ||
                           sh.kind == LibraryShelf::Kind::Group ||
                           sh.kind == LibraryShelf::Kind::Inbox;
        {
            const std::string_view label = cat.shelf_label(sh);
            const float ts = k.s(tok::kLabel);
            FitText count;
            count.format("%u", sh.total);
            const ImVec2 cs = text_size(k.regular, ts, count.c_str());
            const float room = box.w() - 2.0f * pad - cs.x - k.s(tok::kIconSm) - 2.0f * k.s(tok::kS2);
            const FitText name = fit(k.bold, ts, label, room);
            const ImVec2 ns = text_size(k.bold, ts, name.c_str());
            float hx = origin.x + pad;
            const float hw = ns.x + k.s(tok::kS2) + cs.x +
                             (opens ? k.s(tok::kS2) + k.s(tok::kIconSm) : 0.0f) + k.s(tok::kS3);
            bool pressed = false;
            State st{};
            if (opens) {
                ImGui::SetCursorScreenPos({hx - k.s(tok::kS2), sy});
                pressed = ImGui::InvisibleButton("##shelf-open",
                                                 {std::max(hw, k.s(tok::kHit)), head_h});
                st = item_state(k, false);
                const Box face{hx - k.s(tok::kS2), sy + k.s(tok::kS1),
                               hx - k.s(tok::kS2) + std::max(hw, k.s(tok::kHit)),
                               sy + head_h - k.s(tok::kS1)};
                draw_state(k, dl, "##shelf-open", face, {face.x0, sy, face.x1, sy + head_h}, st);
                note("##shelf-open", {face.x0, sy}, {face.x1, sy + head_h}, InkKind::Target);
            }
            label_at(k, dl, k.bold, ts, {hx, sy, hx + ns.x, sy + head_h}, LabelAlign::Left,
                     opens ? state_ink(st, col::kText) : col::kText2, name.c_str());
            hx += ns.x + k.s(tok::kS2);
            label_at(k, dl, k.regular, ts, {hx, sy, hx + cs.x, sy + head_h}, LabelAlign::Left,
                     col::kText3, count.c_str());
            hx += cs.x + k.s(tok::kS2);
            if (opens) {
                glyph(k, dl, icon::kChevron, {hx + k.s(tok::kIconSm) * 0.5f, sy + head_h * 0.5f},
                      k.s(tok::kIconSm), state_ink(st, col::kText2));
            }
            if (pressed) {
                LibraryScope to;
                if (sh.kind == LibraryShelf::Kind::Collection &&
                    sh.key < cat.collections().size()) {
                    to.kind = LibraryScope::Kind::Folder;
                    to.path = cat.collections()[sh.key].path;
                } else if (sh.kind == LibraryShelf::Kind::Group && sh.key < cat.group_count()) {
                    to.kind = LibraryScope::Kind::Group;
                    to.group = cat.group_id(sh.key);
                } else if (sh.kind == LibraryShelf::Kind::Inbox) {
                    to.kind = LibraryScope::Kind::Inbox;
                }
                ImGui::PopID();
                set_scope(cat, to);
                break; // the shelves change under it
            }
        }
        // The row of cards, scrolled sideways on its own.
        const float ry = sy + head_h + k.s(tok::kHitGap); // a dead gap under the header
        const auto n = static_cast<float>(sh.end - sh.begin);
        float& x = offset_of(sh);
        const float content = 2.0f * pad + n * card_w + std::max(0.0f, n - 1.0f) * gap;
        sideways(k, static_cast<int>(si), {origin.x, ry, origin.x + box.w(), ry + card_h}, x,
                 content - box.w());
        const float step = card_w + gap;
        const auto j0 = static_cast<std::uint32_t>(std::max(0.0f, (x - pad) / step));
        for (std::uint32_t j = sh.begin + j0; j < sh.end; ++j) {
            const float cx = origin.x + pad + static_cast<float>(j - sh.begin) * step - x;
            if (cx > origin.x + box.w()) {
                break;
            }
            draw_card(k, port, order[j], {cx, ry}, {cx + card_w, ry + card_h});
        }
        ImGui::PopID();
    }
    const float get_top =
        top + pitch * static_cast<float>(shelves.size()) + (shelves.empty() ? k.s(tok::kS6) : 0.0f);
    const float get_h =
        draw_get_songs(k, model, port, {origin.x, origin.y + get_top - scroll}, box.w());
    ImGui::SetCursorPos({0.0f, get_top + std::max(get_h, pad)});
    ImGui::Dummy({1.0f, 1.0f});
    drag_scroll(k, true);
    ImGui::EndChild();
}

void LibraryScreen::song_input(Kit& k, LibraryPort& port, std::size_t song, bool tap, bool hold,
                               bool right, bool heart, bool play, ImVec2 a, ImVec2 b) {
    const CatalogueSong& s = port.catalogue().song(song);
    if (joining_) {
        // A tap on a card joins the song waiting to it; anything else waits (a heart or Play
        // does nothing meanwhile: the player is picking a piece, not acting on a song).
        if (tap) {
            const std::size_t from = *joining_;
            const std::span<const std::uint32_t> mates = port.catalogue().versions_of(from);
            const bool already =
                from == song || std::find(mates.begin(), mates.end(), song) != mates.end();
            stop_joining();
            if (already) {
                k.say_free("Already the same piece");
            } else {
                switch (port.join_versions(from, song)) {
                case LibraryPort::JoinOutcome::Joined:
                    k.say_free("Joined as versions of one piece");
                    break;
                case LibraryPort::JoinOutcome::Full:
                    k.say_free("That piece already has 8 versions", true);
                    break;
                case LibraryPort::JoinOutcome::NotRead:
                    k.say_free("That song isn't read yet", true);
                    break;
                }
            }
        }
        return;
    }
    if (heart) {
        toggle_favourite(k, port, song);
    }
    if (play) {
        port.open(song);
    }
    if (selecting_ && (tap || (hold && !right))) {
        toggle_selected(s);
        return;
    }
    if (tap) {
        open_details(port, song);
    } else if (hold && right) {
        over_ = Over::Actions;
        many_ = false;
        hold_song(port, song);
        anchor_ = {std::min(a.x + k.s(tok::kS3),
                            ImGui::GetIO().DisplaySize.x - k.s(lib_tok::kPopoverW)),
                   std::min(b.y, ImGui::GetIO().DisplaySize.y)};
    } else if (hold) {
        // A long press starts picking songs, with this one picked.
        start_selecting();
        toggle_selected(s);
    }
}

} // namespace etude::app::ui
