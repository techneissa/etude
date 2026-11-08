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

namespace {

// A popover of `rows` rows of a hit each, `w` wide, under its anchor where it fits, else as low as
// the area allows; taller than the area, it scrolls. Draws its card and opens a child window for
// the rows (end it with `end_popover`). `x` and `y` are where its first row goes.
struct Popover {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
};

Popover begin_popover(Kit& k, const char* id, ImVec2 anchor, bool right_aligned, float w,
                      float content_h, const Box& area) {
    const float pad = k.s(tok::kS1);
    const float h = std::min(content_h + 2.0f * pad, area.h() - 2.0f * pad);
    const float x =
        std::clamp(right_aligned ? anchor.x - w : anchor.x, area.x0 + pad, area.x1 - w - pad);
    const float y =
        std::clamp(anchor.y + pad, area.y0 + pad, std::max(area.y0 + pad, area.y1 - h - pad));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    overlay_card(k, dl, {x, y}, {x + w, y + h}, k.s(tok::kRadiusCard), col::kRaised);
    k.toast_avoid({x, y, w, h});
    ImGui::SetCursorScreenPos({x + pad, y + pad});
    ImGui::BeginChild(id, {w - 2.0f * pad, h - 2.0f * pad}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    return {at.x, at.y, w - 2.0f * pad};
}

void end_popover(Kit& k, float content_h) {
    ImGui::SetCursorPos({0.0f, content_h});
    ImGui::Dummy({1.0f, 0.0f});
    drag_scroll(k);
    ImGui::EndChild();
}

// The confirm's title: one line, or as many as a long name needs (it wraps, it is never cut).
struct ConfirmTitle {
    bool wraps;
    float h;
};

ConfirmTitle measure_confirm_title(const Kit& k, const FitText& title, float size, float w) {
    const ImVec2 sz =
        k.bold->CalcTextSizeA(size, FLT_MAX, w, title.c_str(), title.c_str() + title.view().size());
    const bool wraps = sz.y > size * 1.3f;
    return {wraps, wraps ? sz.y : size * 1.3f};
}

// `text` in `font` wrapped to `w` from `at`.
void wrapped_text(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour,
                  const FitText& text, float w) {
    dl->AddText(font, size, at, colour, text.c_str(), text.c_str() + text.view().size(), w);
}

void draw_confirm_title(Kit& k, ImDrawList* dl, const FitText& title, float size, ImVec2 at,
                        float w, const ConfirmTitle& head) {
    if (head.wraps) {
        wrapped_text(dl, k.bold, size, at, u32(col::kText), title, w);
    } else {
        text_at(k, dl, k.bold, size, at, col::kText, title.c_str());
    }
}

enum class ConfirmTap : std::uint8_t { None, Cancel, Delete };

// The confirm's two buttons side by side from `at`, each `bw` wide and `hit` tall, at the popup
// rule's face and words for `step`.
ConfirmTap confirm_buttons(Kit& k, int step, ImVec2 at, float bw, float hit, float gap,
                           bool can_delete) {
    const float face_h = step > 0 ? tok::kFace * tok::popup_grow(step) : 0.0f;
    const float label_pt =
        step > 0 ? tok::popup_type(tok::kLabel, step, tok::PopupText::Button) : 0.0f;
    if (action_button(k, "##confirm-cancel", icon::kClose, "Cancel", at, {bw, hit}, true, false,
                      false, false, nullptr, face_h, label_pt)) {
        return ConfirmTap::Cancel;
    }
    if (action_button(k, "##confirm-delete", icon::kClear, "Delete", {at.x + bw + gap, at.y},
                      {bw, hit}, can_delete, false, true, false, nullptr, face_h, label_pt)) {
        return ConfirmTap::Delete;
    }
    return ConfirmTap::None;
}

} // namespace

void LibraryScreen::draw_menu(Kit& k, UiModel& model, LibraryPort& port, const Box& area) {
    const float row = k.s(tok::kCell); // a row's pitch: its hit area and the dead gap
    const float w = std::min(k.s(lib_tok::kPopoverW), area.w() - 2.0f * k.s(lib_tok::kSidePad));
    int rows = 0;
    switch (over_) {
    case Over::Sort:
        rows = kBrowseSortCount;
        break;
    case Over::Group:
        rows = kBrowseGroupCount;
        break;
    case Over::View:
        rows = kBrowseViewCount + 1; // the views, then Group versions
        break;
    case Over::Actions:
        rows = 8;
        break;
    case Over::GroupMenu:
        rows = 4;
        break;
    default:
        return;
    }
    const float content_h = static_cast<float>(rows) * row - k.s(tok::kHitGap);
    const Popover pop = begin_popover(k, "##library-menu", anchor_, over_ == Over::View, w,
                                      content_h, area);
    BrowseOrder& order = model.app.library;
    float ry = pop.y;
    const float rx = pop.x;
    const float rw = pop.w;
    const auto next = [&] {
        const float at = ry;
        ry += row;
        return ImVec2{rx, at};
    };
    switch (over_) {
    case Over::Sort:
        for (int i = 0; i < kBrowseSortCount; ++i) {
            const auto so = static_cast<BrowseSort>(i);
            const bool on = order.sort == so;
            char id[24];
            std::snprintf(id, sizeof id, "##sort-%d", i);
            if (menu_row(k, id, sort_icon(so), sort_name(so),
                         on ? sort_hint(so, order.reversed) : nullptr, next(), rw, on)) {
                order = with_sort(order, so);
                over_ = Over::None;
            }
        }
        break;
    case Over::Group:
        for (int i = 0; i < kBrowseGroupCount; ++i) {
            const auto g = static_cast<BrowseGroup>(i);
            char id[24];
            std::snprintf(id, sizeof id, "##group-%d", i);
            if (menu_row(k, id, group_icon(g), group_name(g), nullptr, next(), rw,
                         order.group == g)) {
                order.group = g;
                over_ = Over::None;
            }
        }
        break;
    case Over::View:
        for (int i = 0; i < kBrowseViewCount; ++i) {
            const auto v = static_cast<BrowseView>(i);
            char id[24];
            std::snprintf(id, sizeof id, "##view-%d", i);
            if (menu_row(k, id, view_icon(v), view_name(v), nullptr, next(), rw,
                         model.app.library_view == v)) {
                model.app.library_view = v;
                over_ = Over::None;
            }
        }
        // Versions of one piece as one card; it stays open so the change shows.
        if (menu_row(k, "##view-versions", icon::kLayers, "Group versions",
                     model.app.group_versions ? "On" : "Off", next(), rw,
                     model.app.group_versions)) {
            model.app.group_versions = !model.app.group_versions;
        }
        break;
    case Over::Actions: {
        const CatalogueSong& s = port.catalogue().song(over_song_);
        const bool known = s.hash != 0;
        // Not while a move or a delete is under way.
        const bool movable = port.why_not_movable(over_song_).empty() && !port.files_busy();
        // Rename, Composer, Move and Delete act on the version the card shows: its name on the row.
        const std::string_view said = port.catalogue().version_name(over_song_);
        const char* version = said.empty() ? nullptr : said.data();
        if (menu_row(k, "##act-play", icon::kPlay, "Play", nullptr, next(), rw, false)) {
            port.open(over_song_);
            over_ = Over::None;
        }
        if (menu_row(k, "##act-heart", lib_icon::kHeart,
                     s.piece_favourite ? "Remove from favourites" : "Add to favourites", nullptr,
                     next(), rw, false, known)) {
            toggle_favourite(k, port, over_song_);
            over_ = Over::None;
        }
        if (menu_row(k, "##act-group", lib_icon::kAddTo, "Add to group", nullptr, next(), rw,
                     false, known)) {
            open_for(Over::AddToGroup, false, anchor_);
        }
        if (menu_row(k, "##act-rename", icon::kRename, "Rename", version, next(), rw, false,
                     known)) {
            start_name(port, over_song_, Edit::Rename);
        }
        if (menu_row(k, "##act-composer", lib_icon::kUser, "Set the composer", version, next(), rw,
                     false, known)) {
            start_name(port, over_song_, Edit::Composer);
        }
        if (menu_row(k, "##act-move", lib_icon::kMoveTo, "Move to a folder", version, next(), rw,
                     false, movable)) {
            folders_ = port.folders();
            open_for(Over::MoveTo, false, anchor_);
        }
        if (menu_row(k, "##act-delete", icon::kClear, "Delete", version, next(), rw, false,
                     movable)) {
            confirm_group_ = false;
            open_for(Over::Confirm, false, anchor_);
        }
        if (menu_row(k, "##act-details", lib_icon::kInfo, "Details", nullptr, next(), rw, false)) {
            open_details(port, over_song_);
        }
        break;
    }
    case Over::GroupMenu: {
        const SongGroup* g = port.groups().find(menu_group_);
        if (g == nullptr) {
            over_ = Over::None;
            break;
        }
        const auto& all = port.groups().groups();
        const bool first = !all.empty() && all.front().id == menu_group_;
        const bool last = !all.empty() && all.back().id == menu_group_;
        if (menu_row(k, "##gm-rename", icon::kRename, "Rename", nullptr, next(), rw, false)) {
            start_text(Edit::GroupName, g->name, Over::None);
        }
        if (menu_row(k, "##gm-up", lib_icon::kUp, "Move earlier", nullptr, next(), rw, false,
                     !first)) {
            port.move_group(menu_group_, -1);
        }
        if (menu_row(k, "##gm-down", lib_icon::kDown, "Move later", nullptr, next(), rw, false,
                     !last)) {
            port.move_group(menu_group_, 1);
        }
        if (menu_row(k, "##gm-delete", icon::kClear, "Delete the group", nullptr, next(), rw,
                     false)) {
            confirm_group_ = true;
            open_for(Over::Confirm, false, anchor_);
        }
        break;
    }
    default:
        break;
    }
    end_popover(k, content_h);
}

void LibraryScreen::draw_filters(Kit& k, UiModel& model, const Box& area) {
    BrowseOrder& order = model.app.library;
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kHitGap);
    const float head = k.s(tok::kCaption) * 1.8f;
    const float w = std::min(k.s(lib_tok::kFiltersW), area.w() - 2.0f * k.s(lib_tok::kSidePad));
    int folded_rows = 0;
    for (const FilterChip& c : kFilters) {
        folded_rows += (folded_ & (1u << static_cast<unsigned>(c.filter))) != 0 ? 1 : 0;
    }
    const bool any = facet_count(order) > 0;
    const float shows = static_cast<float>(folded_rows) * (hit + gap); // the quick filters' rows
    const float content_h = head + hit + gap + head + hit + (folded_rows > 0 ? head + shows : 0.0f) +
                            (any ? (folded_rows > 0 ? 0.0f : gap) + hit : 0.0f);
    const Popover pop = begin_popover(k, "##library-filter-list", anchor_, true, w, content_h, area);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = pop.y;
    const auto heading = [&](const char* text) {
        const float cs = k.s(tok::kCaption);
        label_at(k, dl, k.bold, cs, {pop.x + k.s(tok::kS2), y, pop.x + pop.w, y + head},
                 LabelAlign::Left, col::kText3, text);
        y += head;
    };
    // Level: five toggles, a star in the level's colour and its number; none on shows every level.
    heading("Level");
    {
        const float each = std::min(k.s(lib_tok::kLevelChipW),
                                    (pop.w - 4.0f * gap) / static_cast<float>(kLevelStars));
        for (int lv = 1; lv <= kLevelStars; ++lv) {
            const int bit = 1 << (lv - 1);
            const bool on = (order.levels & bit) != 0;
            char id[24];
            std::snprintf(id, sizeof id, "##level-%d", lv);
            const float x = pop.x + static_cast<float>(lv - 1) * (each + gap);
            ImGui::SetCursorScreenPos({x, y});
            if (ImGui::InvisibleButton(id, {each, hit})) {
                order.levels ^= bit;
            }
            const State st = item_state(k, on);
            const float fh = k.s(tok::kFace);
            const Box face{x, y + (hit - fh) * 0.5f, x + each, y + (hit + fh) * 0.5f};
            draw_state(k, dl, id, face, {x, y, x + each, y + hit}, st, Idle::Fill, fh * 0.5f);
            note(id, {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
            long_press_tip(k, level_word(lv));
            // A star filled in the level's colour, then the number.
            const float r = k.s(lib_tok::kLevelStar);
            const float ls = k.s(tok::kLabel);
            char num[4];
            std::snprintf(num, sizeof num, "%d", lv);
            const ImVec2 ns = text_size(k.bold, ls, num);
            const float total = 2.0f * r + k.s(tok::kS1) + ns.x;
            const float sx = face.cx() - total * 0.5f;
            std::array<ImVec2, 10> pts;
            star_points({sx + r, face.cy()}, r, pts);
            dl->AddConcavePolyFilled(
                pts.data(), 10,
                u32(to_vec(level_star_colour(static_cast<double>(lv), k.high_contrast))));
            label_at(k, dl, k.bold, ls,
                     {sx + 2.0f * r + k.s(tok::kS1), face.y0, sx + 2.0f * r + k.s(tok::kS1) + ns.x,
                      face.y1},
                     LabelAlign::Left, state_ink(st, col::kText), num);
        }
        y += hit + gap;
    }
    // Hands: any, both, the right only, the left only.
    heading("Hands");
    {
        struct HandChip {
            BrowseHands hands;
            const char* id;
            const char* label;
            bool left;
            bool right;
        };
        static constexpr HandChip kHands[] = {
            {BrowseHands::Any, "##hands-any", "Any", false, false},
            {BrowseHands::Both, "##hands-both", "Both hands", true, true},
            {BrowseHands::RightOnly, "##hands-right", "Right hand only", false, true},
            {BrowseHands::LeftOnly, "##hands-left", "Left hand only", true, false}};
        const float each = (pop.w - 3.0f * gap) / 4.0f;
        for (std::size_t n = 0; n < std::size(kHands); ++n) {
            const HandChip& c = kHands[n];
            const float x = pop.x + static_cast<float>(n) * (each + gap);
            const bool on = order.hands == c.hands;
            ImGui::SetCursorScreenPos({x, y});
            if (ImGui::InvisibleButton(c.id, {each, hit})) {
                order.hands = c.hands;
            }
            const State st = item_state(k, on);
            const float fh = k.s(tok::kFace);
            const Box face{x, y + (hit - fh) * 0.5f, x + each, y + (hit + fh) * 0.5f};
            draw_state(k, dl, c.id, face, {x, y, x + each, y + hit}, st, Idle::Fill, fh * 0.5f);
            note(c.id, {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
            long_press_tip(k, c.label);
            if (c.hands == BrowseHands::Any) {
                const float ls = k.s(tok::kLabel);
                label_at(k, dl, k.regular, ls, face, LabelAlign::Centre, state_ink(st, col::kText),
                         "Any");
            } else {
                const float hs = k.s(lib_tok::kHand);
                const float hw = c.left && c.right ? hands_width(hs) : hs;
                draw_hands(k, dl, face.cx() - hw * 0.5f - (c.left ? 0.0f : hs * (1.0f + kHandGapShare)),
                           face.cy(), hs, c.left, c.right);
            }
        }
        y += hit;
    }
    // The quick filters the chips row had no room for.
    if (folded_rows > 0) {
        heading("Show");
        for (const FilterChip& c : kFilters) {
            if ((folded_ & (1u << static_cast<unsigned>(c.filter))) == 0) {
                continue;
            }
            if (menu_row(k, c.id, c.glyph_text, c.label, nullptr, {pop.x, y}, pop.w,
                         order.filter == c.filter)) {
                order.filter = c.filter;
            }
            y += hit + gap;
        }
    }
    if (any) {
        y += folded_rows > 0 ? 0.0f : gap;
        if (menu_row(k, "##filters-clear", lib_icon::kFunnelX, "Clear the level and hands", nullptr,
                     {pop.x, y}, pop.w, false)) {
            order.levels = kAllLevels;
            order.hands = BrowseHands::Any;
        }
    }
    end_popover(k, content_h);
}

void LibraryScreen::draw_list_popover(Kit& k, LibraryPort& port, const Box& area) {
    const std::vector<std::size_t>& songs = targets(port);
    if (songs.empty()) {
        over_ = Over::None;
        return;
    }
    const float row = k.s(tok::kCell); // a row's pitch: its hit area and the dead gap
    const float w = std::min(k.s(lib_tok::kPopoverW), area.w() - 2.0f * k.s(lib_tok::kSidePad));
    const bool groups = over_ == Over::AddToGroup;
    const LibraryGroups& all = port.groups();
    const std::size_t count = groups ? all.groups().size() : folders_.size();
    const float content_h = static_cast<float>(count + 1) * row - k.s(tok::kHitGap);
    const Popover pop = begin_popover(k, "##library-list", anchor_, true, w, content_h, area);
    // Only the rows in view are drawn.
    const float scroll = ImGui::GetScrollY();
    const float view_h = ImGui::GetWindowHeight();
    const auto first = static_cast<std::size_t>(std::max(0.0f, scroll / row));
    const std::size_t last =
        std::min(count, static_cast<std::size_t>((scroll + view_h) / row) + 1);
    const LibraryCatalogue& cat = port.catalogue();
    // Add to group is about the piece: every version of each card goes in or out together (#215).
    const std::vector<std::size_t>& versions = groups ? piece_of(port, songs) : songs;
    for (std::size_t r = first; r < last; ++r) {
        ImGui::PushID(static_cast<int>(r));
        const ImVec2 at{pop.x, pop.y + static_cast<float>(r) * row};
        if (groups) {
            const SongGroup& g = all.groups()[r];
            // In it: every card asked about (a check), some of them (a dash), or none. A card is in
            // when any of its versions is.
            std::size_t in = 0;
            std::vector<std::size_t> own;
            for (const std::size_t i : songs) {
                cat.piece_songs(i, own);
                bool any = false;
                for (const std::size_t v : own) {
                    any = any || all.contains(g.id, cat.song(v).hash);
                }
                in += any ? 1 : 0;
            }
            const bool every = in == songs.size();
            const char* mark = every ? icon::kCheck : (in > 0 ? lib_icon::kSome : nullptr);
            if (menu_row(k, "##group-row", lib_icon::kGroup, g.name.c_str(), nullptr, at, pop.w,
                         every)) {
                port.set_in_group(g.id, versions, !every);
                FitText line;
                songs_text(line, songs.size());
                line.append(every ? " out of \xE2\x80\x9C" : " in \xE2\x80\x9C");
                line.append(g.name);
                line.append("\xE2\x80\x9D");
                k.say(line.view());
            }
            if (mark != nullptr) {
                glyph(k, ImGui::GetWindowDrawList(), mark,
                      {at.x + pop.w - k.s(tok::kS3) - k.s(tok::kIconSm) * 0.5f, at.y + k.s(tok::kHit) * 0.5f},
                      k.s(tok::kIconSm), col::kAccent);
            }
        } else {
            const std::string& f = folders_[r];
            if (menu_row(k, "##folder-row", r == 0 ? icon::kHome : lib_icon::kFolder,
                         f.empty() ? "Songs (the top)" : f.c_str(), nullptr, at, pop.w, false)) {
                port.move_songs(songs, f);
                if (many_) {
                    stop_selecting();
                }
                over_ = Over::None;
            }
        }
        ImGui::PopID();
    }
    // A new group or folder, by name.
    const ImVec2 at{pop.x, pop.y + static_cast<float>(count) * row};
    if (groups) {
        if (menu_row(k, "##group-new", icon::kPlus, "New group", nullptr, at, pop.w, false)) {
            start_text(Edit::NewGroup, "", many_ ? Over::None : Over::Details);
        }
    } else if (menu_row(k, "##folder-new", lib_icon::kNewFolder, "New folder", nullptr, at, pop.w,
                        false)) {
        start_text(Edit::NewFolder, "", Over::None);
    }
    end_popover(k, content_h);
}

void LibraryScreen::draw_confirm(Kit& k, LibraryPort& port, const Box& area) {
    const float margin = k.s(lib_tok::kSidePad);
    const float pad = k.s(tok::kS4);
    // The popup rule: a wider window gets a larger card, title, words and buttons.
    const int step = tok::wide_step(ImGui::GetIO().DisplaySize.x);
    const float hit = k.s(tok::kHit) * tok::popup_grow(step);
    const float gap = k.s(tok::kS2);
    const float w = std::min(k.s(tok::popup_width(lib_tok::kConfirmW, step, ImGui::GetIO().DisplaySize.x / k.u)),
                             area.w() - 2.0f * margin);
    FitText title;
    FitText body;
    std::size_t n = 0;
    if (confirm_group_) {
        const SongGroup* g = port.groups().find(menu_group_);
        if (g == nullptr) {
            over_ = Over::None;
            return;
        }
        title.format("Delete \xE2\x80\x9C%s\xE2\x80\x9D?", g->name.c_str());
        body.assign("The group goes; its songs stay in the library.");
    } else {
        n = targets(port).size();
        if (n == 0) {
            over_ = Over::None;
            return;
        }
        // One card, picked or held: the confirmation names the version it deletes.
        const std::string_view said = port.catalogue().version_name(targets(port).front());
        if (n == 1 && !said.empty()) {
            title.format("Delete the %.*s version?", static_cast<int>(said.size()), said.data());
        } else {
            FitText count;
            songs_text(count, n);
            title.format("Delete %s?", count.c_str());
        }
        body.assign(n == 1 ? "It goes to Removed songs in the settings folder, to be taken back from there."
                           : "They go to Removed songs in the settings folder, to be taken back from there.");
    }
    const float ts = k.s(tok::popup_type(tok::kTitle, step, tok::PopupText::Title));
    const float bs = k.s(tok::popup_type(tok::kLabel, step, tok::PopupText::Text));
    // The title and the body on as many lines as they need: a long name wraps, it is never cut.
    const float text_w = w - 2.0f * pad;
    const ConfirmTitle head = measure_confirm_title(k, title, ts, text_w);
    const ImVec2 bsz =
        k.regular->CalcTextSizeA(bs, FLT_MAX, text_w, body.c_str(), body.c_str() + body.size());
    const float h = pad * 2.0f + head.h + gap + bsz.y + k.s(tok::kS4) + hit;
    // On whole pixels, so the icons on the card (and its rows, which count from its top) sit
    // centred in their buttons.
    const float x = std::floor(area.cx() - w * 0.5f);
    const float y = std::floor(area.y0 + std::max(margin, (area.h() - h) * 0.5f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    sheet_card(k, dl, {x, y}, {x + w, y + h}, k.s(tok::kRadiusSheet), col::kSurface);
    k.toast_avoid({x, y, w, h});
    float cy = y + pad;
    draw_confirm_title(k, dl, title, ts, {x + pad, cy}, text_w, head);
    cy += head.h + gap;
    wrapped_text(dl, k.regular, bs, {x + pad, cy}, u32(col::kText2), body, text_w);
    cy += bsz.y + k.s(tok::kS4);
    const float bw = (w - 2.0f * pad - gap) * 0.5f;
    const ConfirmTap tap =
        confirm_buttons(k, step, {x + pad, cy}, bw, hit, gap, confirm_group_ || !port.files_busy());
    if (tap == ConfirmTap::Cancel) {
        over_ = confirm_group_ || many_ ? Over::None : Over::Details;
    } else if (tap == ConfirmTap::Delete) {
        if (confirm_group_) {
            port.remove_group(menu_group_);
        } else {
            port.remove_songs(targets(port));
            if (many_) {
                stop_selecting();
            }
        }
        confirm_group_ = false;
        over_ = Over::None;
    }
}

} // namespace etude::app::ui
