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

bool LibraryScreen::join_cancel(Kit& k, ImVec2 c) {
    if (round_button(k, "##join-cancel", icon::kClose, c, false, Idle::Fill)) {
        stop_joining();
        return true;
    }
    long_press_tip(k, "Cancel");
    return false;
}

// The song the pick waits for is gone (a rescan): nothing to join, and the pick stops.
static bool join_gone(const LibraryCatalogue& cat, const std::optional<std::size_t>& joining,
                      std::uint64_t hash) {
    return !joining || *joining >= cat.size() || cat.song(*joining).hash != hash;
}

void LibraryScreen::draw_join_strip(Kit& k, LibraryPort& port, const LibraryFrame& lay) {
    if (join_gone(port.catalogue(), joining_, joining_hash_)) {
        stop_joining();
        return;
    }
    if (join_cancel(k, {lay.view.cx(), lay.view.cy()})) {
        return;
    }
    const StackLine top{k.bold, k.s(tok::kCaption), col::kText, "Tap the song"};
    const StackLine bottom{k.bold, k.s(tok::kCaption), col::kText, "it joins"};
    label_stack(k, ImGui::GetWindowDrawList(), lay.hint, top, &bottom);
}

void LibraryScreen::draw_join_bar(Kit& k, LibraryPort& port, const Box& box) {
    LibraryCatalogue& cat = port.catalogue();
    if (join_gone(cat, joining_, joining_hash_)) {
        stop_joining();
        return;
    }
    const float hit = k.s(tok::kHit);
    if (join_cancel(k, {box.x0 + hit * 0.5f, box.cy()})) {
        return;
    }
    FitText line;
    line.format("Same piece as \xE2\x80\x9C%s\xE2\x80\x9D: tap the song", cat.song(*joining_).title.c_str());
    const float cx0 = box.x0 + hit + k.s(tok::kS2);
    const FitText cut = fit(k.bold, k.s(tok::kLabel), line.view(), std::max(0.0f, box.x1 - cx0));
    label_at(k, ImGui::GetWindowDrawList(), k.bold, k.s(tok::kLabel),
             {cx0, box.y0, box.x1, box.y1}, LabelAlign::Left, col::kText, cut.c_str());
}

void LibraryScreen::draw_selection_bar(Kit& k, LibraryPort& port, const Box& box) {
    const float gap = k.s(tok::kS2);
    const float hit = k.s(tok::kHit);
    const std::size_t picked = targets_for_bar(port);
    // Done, then how many are picked.
    if (round_button(k, "##select-done", icon::kClose, {box.x0 + hit * 0.5f, box.cy()}, false,
                     Idle::Fill)) {
        stop_selecting();
        return;
    }
    long_press_tip(k, "Done");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    FitText count;
    const std::size_t hidden = picks_.size() - std::min(picks_.size(), picked);
    if (picked == 0 && hidden == 0) {
        count.assign("Tap songs to pick them");
    } else if (hidden > 0) {
        // The rest are picked but out of view (a search, a filter, another place): not acted on.
        count.format("%zu picked, %zu hidden", picked, hidden);
    } else {
        count.format("%zu picked", picked);
    }
    struct Action {
        const char* id;
        const char* glyph_text;
        const char* text;
    };
    const bool all_favourite = picked > 0 && all_targets_favourite(port);
    const Action actions[] = {
        {"##select-all", icon::kCheck, "All"},
        {"##select-group", lib_icon::kAddTo, "Add to group"},
        {"##select-heart", lib_icon::kHeart, all_favourite ? "Unfavourite" : "Favourite"},
        {"##select-move", lib_icon::kMoveTo, "Move"},
        {"##select-delete", icon::kClear, "Delete"},
    };
    constexpr std::size_t kActions = std::size(actions);
    // In words when they fit beside the count, else as icons.
    float words = 0.0f;
    for (const Action& a : actions) {
        words += chip_width(k, a.glyph_text, a.text, nullptr) + gap;
    }
    const float count_w = text_size(k.bold, k.s(tok::kLabel), count.c_str()).x;
    const float room = box.w() - hit - gap;
    const bool with_words = words + count_w + gap <= room;
    float x = box.x1;
    std::array<float, kActions> at{};
    std::array<float, kActions> width{};
    for (std::size_t n = kActions; n-- > 0;) {
        width[n] = with_words ? chip_width(k, actions[n].glyph_text, actions[n].text, nullptr) : hit;
        x -= width[n];
        at[n] = x;
        x -= gap;
    }
    {
        const float cx0 = box.x0 + hit + gap;
        const FitText cut = fit(k.bold, k.s(tok::kLabel), count.view(), std::max(0.0f, x - cx0));
        label_at(k, dl, k.bold, k.s(tok::kLabel), {cx0, box.y0, x, box.y1}, LabelAlign::Left,
                 col::kText, cut.c_str());
    }
    for (std::size_t n = 0; n < kActions; ++n) {
        const Action& a = actions[n];
        // Move and Delete wait while a move or a delete is under way.
        const bool enabled = n == 0 || (picked > 0 && !((n == 3 || n == 4) && port.files_busy()));
        ImGui::BeginDisabled(!enabled);
        const bool pressed =
            text_chip(k, a.id, a.glyph_text, with_words ? a.text : "", nullptr, {at[n], box.y0},
                      width[n], n == 2 && all_favourite);
        ImGui::EndDisabled();
        long_press_tip(k, a.text);
        if (!pressed || !enabled) {
            continue;
        }
        const ImVec2 anchor{at[n] + width[n], box.y1};
        switch (n) {
        case 0: { // every song shown
            const LibraryCatalogue& cat = port.catalogue();
            for (const std::uint32_t i : visible_order(cat)) {
                picks_.add(cat.song(i).relative);
            }
            targets_version_ = ~std::uint64_t{0};
            break;
        }
        case 1:
            open_for(Over::AddToGroup, true, anchor);
            break;
        case 2: {
            many_ = true;
            const std::vector<std::size_t>& songs = targets(port);
            port.set_favourites(piece_of(port, songs), !all_favourite);
            FitText line;
            songs_text(line, songs.size());
            line.append(all_favourite ? " out of favourites" : " in favourites");
            k.say(line.view());
            break;
        }
        case 3:
            folders_ = port.folders();
            open_for(Over::MoveTo, true, anchor);
            break;
        case 4:
            confirm_group_ = false;
            open_for(Over::Confirm, true, anchor);
            break;
        default:
            break;
        }
    }
}

std::size_t LibraryScreen::targets_for_bar(LibraryPort& port) {
    const bool was = many_;
    many_ = true;
    const std::size_t n = targets(port).size();
    many_ = was;
    return n;
}

const std::vector<std::size_t>& LibraryScreen::piece_of(LibraryPort& port,
                                                        std::span<const std::size_t> cards) {
    piece_.clear();
    std::vector<std::size_t> one;
    for (const std::size_t card : cards) {
        port.catalogue().piece_songs(card, one);
        piece_.insert(piece_.end(), one.begin(), one.end());
    }
    std::sort(piece_.begin(), piece_.end());
    piece_.erase(std::unique(piece_.begin(), piece_.end()), piece_.end());
    return piece_;
}

void LibraryScreen::toggle_favourite(Kit& k, LibraryPort& port, std::size_t card) {
    const bool on = !port.catalogue().song(card).piece_favourite;
    port.set_favourites(piece_of(port, std::span<const std::size_t>(&card, 1)), on);
    k.say(on ? "Added to favourites" : "Removed from favourites");
}

bool LibraryScreen::all_targets_favourite(LibraryPort& port) {
    const bool was = many_;
    many_ = true;
    bool all = true;
    for (const std::size_t i : targets(port)) {
        all = all && port.catalogue().song(i).piece_favourite;
    }
    many_ = was;
    return all;
}

} // namespace etude::app::ui
