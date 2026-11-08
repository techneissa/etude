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

// The round mark a picked song shows where its heart is.
void draw_pick(const Kit& k, ImDrawList* dl, ImVec2 c, bool picked) {
    const float r = k.s(lib_tok::kPick);
    if (picked) {
        dl->AddCircleFilled(c, r, u32(col::kAccent), 24);
        glyph(k, dl, icon::kCheck, c, r * 1.3f, col::kAccentInk);
    } else {
        dl->AddCircle(c, r, u32(col::kText2), 24, k.s(lib_tok::kHeartLine));
    }
}

} // namespace

std::optional<std::size_t> LibraryScreen::draw_version_chips(Kit& k, LibraryPort& port,
                                                             std::size_t song, ImVec2 at,
                                                             float width, bool wrap, bool live) {
    const LibraryCatalogue& cat = port.catalogue();
    const std::span<const std::uint32_t> versions = cat.versions_of(song);
    const VersionChips v = measure_version_chips(chip_widths_, k, cat, song, width, wrap);
    if (v.n == 0) {
        return std::nullopt;
    }
    const float gap = k.s(tok::kHitGap);
    const float hit = k.s(tok::kHit);
    float x = at.x;
    float y = at.y;
    std::optional<std::size_t> tapped;
    const auto place = [&](float w) {
        if (wrap && x > at.x && x + w > at.x + width + 0.5f) {
            x = at.x;
            y += hit + gap;
        }
    };
    for (std::size_t i = v.plan.first; i < v.plan.first + v.plan.count; ++i) {
        const float w = v.plan.scores ? v.score_w[i] : v.name_w[i];
        place(w);
        char id[32];
        std::snprintf(id, sizeof id, "##ver-%zu", i);
        ChipOpts o;
        o.on = i == v.chosen;
        o.live = live;
        if (chip(k, id, v.plan.scores ? v.scored[i] : v.named[i], {x, y}, o) && live) {
            tapped = versions[i];
        }
        x += w + gap;
    }
    if (v.plan.more) {
        FitText more;
        more.format("+%zu", v.n - v.plan.count);
        ChipOpts o;
        o.live = live;
        if (chip(k, "##ver-more", more.view(), {x, y}, o) && live) {
            open_details(port, song);
        }
    }
    if (tapped) {
        port.choose_version(*tapped);
    }
    return tapped;
}

namespace {

// A card's heart in its corner, `c` its centre: a tap marks the piece a favourite. True when tapped.
bool card_heart(Kit& k, ImDrawList* dl, ImVec2 c, const CatalogueSong& s) {
    const float hit = k.s(tok::kHit);
    ImGui::SetCursorScreenPos({c.x - hit * 0.5f, c.y - hit * 0.5f});
    const bool pressed = ImGui::InvisibleButton("##card-heart", {hit, hit});
    const State st = item_state(k, false, s.hash != 0);
    const float half = k.s(tok::kFace) * 0.5f;
    const Box face{c.x - half, c.y - half, c.x + half, c.y + half};
    draw_state(k, dl, "##card-heart", face,
               {c.x - hit * 0.5f, c.y - hit * 0.5f, c.x + hit * 0.5f, c.y + hit * 0.5f}, st,
               Idle::None, half);
    note("##card-heart", {face.x0, face.y0}, {face.x1, face.y1});
    draw_heart(k, dl, c, k.s(lib_tok::kHeart), s.piece_favourite,
               s.piece_favourite ? kHeartRed : state_ink(st, col::kText3));
    return pressed && s.hash != 0;
}

// A list row's facts at its right of the title: the strip's stars (just before the heart), the length
// over the level's stars, and the hands, each where the layout kept room for it; "Reading..." for a
// song not read yet.
void row_facts(Kit& k, ImDrawList* dl, const CatalogueSong& s, const ListRow& lay) {
    const float cs = k.s(tok::kCaption);
    // The strip's stars, just before the heart (a song not read yet has none).
    if (lay.stars.w() > 0.0f && s.state != EntryState::Pending) {
        draw_level_stars(k, dl, lay.stars.x0, lay.stars.cy(), s.half_stars);
    }
    // The length over the stars, and the hands.
    if (s.state == EntryState::Pending) {
        if (lay.facts.w() > 0.0f) {
            label_at(k, dl, k.regular, cs, lay.facts, LabelAlign::Left, col::kText3,
                     "Reading\xE2\x80\xA6");
        }
    } else {
        if (lay.facts.w() > 0.0f) {
            const float star_r = k.s(lib_tok::kLevelStar);
            const float ls_size = k.s(tok::kLabel);
            FitText len;
            length_text(len, s.duration_s);
            const ImVec2 ls = text_size(k.regular, ls_size, len.c_str());
            const float total = ls.y + k.s(tok::kS1) + 2.0f * star_r;
            const float y0 = lay.facts.cy() - total * 0.5f;
            label_at(k, dl, k.regular, ls_size, {lay.facts.x0, y0, lay.facts.x1, y0 + ls.y},
                     LabelAlign::Left, col::kText2, len.c_str());
            draw_level_stars(k, dl, lay.facts.x0, y0 + ls.y + k.s(tok::kS1) + star_r,
                             s.half_stars);
        }
        if (lay.hands.w() > 0.0f) {
            draw_hands(k, dl, lay.hands.x0, lay.hands.cy(), k.s(lib_tok::kHand), s.left, s.right);
        }
    }
}

} // namespace

void LibraryScreen::draw_card(Kit& k, LibraryPort& port, std::size_t song, ImVec2 a, ImVec2 b) {
    const CatalogueSong& s = port.catalogue().song(song);
    ImGui::PushID(static_cast<int>(song));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = k.s(tok::kRadiusCard);
    const float w = b.x - a.x;
    const float hit = k.s(tok::kHit);
    const float pad = k.s(tok::kS3);
    const bool current = port.current() == song;
    const bool picked = selecting_ && is_selected(s);
    // The heart in the top corner, Play with the best-score ring in the bottom one.
    const bool versioned = !port.catalogue().versions_of(song).empty();
    const CardParts parts = card_parts({a.x, a.y, b.x, b.y}, k.u, selecting_, versioned);
    const ImVec2 heart_c{parts.heart.cx(), parts.heart.cy()};
    const ImVec2 play_c{parts.play.cx(), parts.play.cy()};
    // The heart and Play come first, on the upper channel, so they take a touch on them before the
    // card under them sees it (the card then needs no overlap mode, whose hover waits a frame).
    // The audit taps a card where no button of its own is (left of the middle), and the first
    // name holding "##card" it finds: only a card whose middle is in view (rows above and below
    // the view are drawn too, clipped).
    // (Above the chips, where a piece has versions.)
    const float chips_top = parts.chips_top;
    {
        const ImVec2 lo = dl->GetClipRectMin();
        const ImVec2 hi = dl->GetClipRectMax();
        const float tap_bottom = versioned ? chips_top : b.y;
        const float my = (a.y + tap_bottom) * 0.5f;
        if (my > lo.y && my < hi.y && a.x >= lo.x && a.x + w * 0.5f <= hi.x) {
            note("##card", a, {a.x + w * 0.5f, tap_bottom}, InkKind::Target);
        }
    }
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    bool heart = false;
    bool play = false;
    // The chips of the versions of this piece, in a row over Play: a tap on one chooses it, the
    // card then shows that version (the next frame).
    if (versioned) {
        const float inset = k.s(tok::kS2);
        draw_version_chips(k, port, song, {a.x + inset, chips_top}, w - 2.0f * inset, false,
                           !selecting_);
    }
    if (selecting_) {
        // Picking: the mark in the heart's corner, and no Play (the whole card picks).
        draw_pick(k, dl, heart_c, picked);
    } else {
        heart = card_heart(k, dl, heart_c, s);
        // Play, inside the best score's ring.
        play = round_button(k, "##card-play", icon::kPlay, play_c, false, Idle::Fill,
                            s.state == EntryState::Pending ? -2 : s.best_permille,
                            InkKind::PlayTriangle);
    }
    // The card itself, under them: a tap opens the details, a long press starts picking.
    dl->ChannelsSetCurrent(0);
    // The card's own tap area is the card less the column of the heart and Play (a dead gap clear
    // of them) and, where it has them, the chips' row: no tap area reaches over a button.
    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton("##card", {parts.tap.w(), parts.tap.h()});
    const Press press = press_of(pressed);
    const State st = item_state(k, current || picked);
    card_face(k, dl, a, b, radius);
    card_band(dl, a, b.x, k.s(lib_tok::kBand), radius, u32(genre_colour(s.genre)));
    draw_state(k, dl, "##card", {a.x, a.y, b.x, b.y}, {a.x, a.y, b.x, b.y}, st, Idle::None, radius);
    // Title on up to two lines (the first stops before the heart), then the composer.
    const float ts = k.s(tok::kBody);
    const float tx = a.x + pad;
    const float ty = a.y + k.s(lib_tok::kBand) + k.s(tok::kS2);
    const TitleLines& lines =
        title_lines(k.bold, ts, port.catalogue().card_title(song), heart_c.x - hit * 0.5f - tx,
                    b.x - pad - tx);
    for (int i = 0; i < lines.count; ++i) {
        text_at(k, dl, k.bold, ts, {tx, ty + k.s(lib_tok::kTitleLine) * static_cast<float>(i)},
                s.state == EntryState::Pending ? col::kText2 : col::kText, lines.line[i].c_str());
    }
    const float comp_y = ty + k.s(lib_tok::kTitleLine) * static_cast<float>(lines.count);
    const float cs = k.s(tok::kCaption);
    if (!s.composer.empty()) {
        const FitText c = fit(k.regular, cs, s.composer, b.x - pad - tx);
        text_at(k, dl, k.regular, cs, {tx, comp_y}, col::kText2, c.c_str());
    }
    // The facts at the bottom left: the length over the level's stars, the hands right of them.
    const float limit = selecting_ ? b.x - pad : play_c.x - hit * 0.5f;
    if (s.state == EntryState::Pending) {
        label_at(k, dl, k.regular, cs, {tx, play_c.y - hit * 0.5f, limit, play_c.y + hit * 0.5f},
                 LabelAlign::Left, col::kText3, "Reading\xE2\x80\xA6");
    } else {
        const float bottom = b.y - pad;
        const float star_r = k.s(lib_tok::kLevelStar);
        const float stars_cy = bottom - star_r;
        const float ls_size = k.s(tok::kLabel);
        FitText len;
        length_text(len, s.duration_s);
        const ImVec2 ls = text_size(k.regular, ls_size, len.c_str());
        const float len_y = stars_cy - star_r - k.s(tok::kS1) - ls.y;
        text_at(k, dl, k.regular, ls_size, {tx, len_y}, col::kText2, len.c_str());
        draw_level_stars(k, dl, tx, stars_cy, s.half_stars);
        const float hand = k.s(lib_tok::kHand);
        const float hx = tx + std::max(ls.x, level_stars_width(k)) + k.s(tok::kS3);
        if (hx + hands_width(hand) <= limit) {
            draw_hands(k, dl, hx, (len_y + bottom) * 0.5f, hand, s.left, s.right);
        }
    }
    dl->ChannelsMerge();
    song_input(k, port, song, press.tap, press.hold, press.right, heart, play, a, b);
    ImGui::PopID();
}

void LibraryScreen::draw_row(Kit& k, LibraryPort& port, std::size_t song, ImVec2 a, ImVec2 b) {
    const CatalogueSong& s = port.catalogue().song(song);
    ImGui::PushID(static_cast<int>(song));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = k.s(tok::kRadiusControl);
    const float hit = k.s(tok::kHit);
    const bool current = port.current() == song;
    const bool picked = selecting_ && is_selected(s);
    // A piece with versions: the version shown on a chip, a tap on which moves to the next.
    const std::span<const std::uint32_t> versions = port.catalogue().versions_of(song);
    const std::string_view version_name = port.catalogue().version_name(song);
    float version_w = 0.0f;
    if (!versions.empty()) {
        ChipWidthCache::Widths& cw = widths_of(chip_widths_, k, port.catalogue(), song);
        if (cw.row < 0.0f) {
            cw.row = chip_width(k, icon::kLayers, version_name, nullptr);
        }
        version_w = cw.row;
    }
    const ListRow lay = list_row({a.x, a.y, b.x, b.y}, k.u, version_w, compact_results_);
    {
        const ImVec2 lo = dl->GetClipRectMin();
        const ImVec2 hi = dl->GetClipRectMax();
        const float my = (a.y + b.y) * 0.5f;
        if (my > lo.y && my < hi.y && a.x >= lo.x) {
            note("##card", a, {lay.text.x1, b.y}, InkKind::Target);
        }
    }
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    bool heart = false;
    bool play = false;
    const ImVec2 heart_c{lay.heart.cx(), lay.heart.cy()};
    if (lay.version.w() > 0.0f) {
        ChipOpts o;
        o.icon = icon::kLayers;
        o.live = !selecting_;
        // Where the row has no room for the word the chip is its icon alone (the tip says which).
        const bool word = lay.version.w() >= version_w - 0.5f;
        if (chip(k, "##ver-next", word ? version_name : std::string_view(),
                 {lay.version.x0, lay.version.y0}, o) &&
            !selecting_) {
            // The next version, easiest first and round again.
            std::size_t at = 0;
            for (std::size_t i = 0; i < versions.size(); ++i) {
                at = versions[i] == song ? i : at;
            }
            port.choose_version(versions[(at + 1) % versions.size()]);
        }
        if (word) {
            long_press_tip(k, "Version: tap for the next");
        } else {
            FitText tip;
            tip.format("%.*s: tap for the next version", static_cast<int>(version_name.size()),
                       version_name.data());
            long_press_tip(k, tip.c_str());
        }
    }
    if (selecting_) {
        draw_pick(k, dl, {lay.play.cx(), lay.play.cy()}, picked);
    } else {
        ImGui::SetCursorScreenPos({lay.heart.x0, lay.heart.y0});
        const bool tapped_heart = ImGui::InvisibleButton("##card-heart", {hit, hit});
        const State ss = item_state(k, false, s.hash != 0);
        const float half = k.s(tok::kFace) * 0.5f;
        const Box face{heart_c.x - half, heart_c.y - half, heart_c.x + half, heart_c.y + half};
        draw_state(k, dl, "##card-heart", face, lay.heart, ss, Idle::None, half);
        note("##card-heart", {face.x0, face.y0}, {face.x1, face.y1});
        draw_heart(k, dl, heart_c, k.s(lib_tok::kHeart), s.piece_favourite,
                   s.piece_favourite ? kHeartRed : state_ink(ss, col::kText3));
        heart = tapped_heart && s.hash != 0;
        play = round_button(k, "##card-play", icon::kPlay, {lay.play.cx(), lay.play.cy()}, false,
                            Idle::Fill, s.state == EntryState::Pending ? -2 : s.best_permille,
                            InkKind::PlayTriangle);
    }
    dl->ChannelsSetCurrent(0);
    ImGui::SetCursorScreenPos(a);
    const bool pressed = ImGui::InvisibleButton("##card", {lay.tap.w(), lay.tap.h()});
    const Press press = press_of(pressed);
    const State st = item_state(k, current || picked);
    row_face(k, dl, a, b, radius, genre_colour(s.genre));
    draw_state(k, dl, "##card", {a.x, a.y, b.x, b.y}, {a.x, a.y, b.x, b.y}, st, Idle::None, radius);
    // The title over the composer.
    const float ts = k.s(tok::kBody);
    const float cs = k.s(tok::kCaption);
    const FitText title = fit(k.bold, ts, port.catalogue().card_title(song), lay.text.w());
    const FitText composer = fit(k.regular, cs, s.composer, lay.text.w());
    const StackLine top{k.bold, ts, s.state == EntryState::Pending ? col::kText2 : col::kText,
                        title.c_str()};
    const StackLine bottom{k.regular, cs, col::kText2, composer.c_str()};
    label_stack(k, dl, lay.text, top, s.composer.empty() ? nullptr : &bottom);
    row_facts(k, dl, s, lay);
    dl->ChannelsMerge();
    song_input(k, port, song, press.tap, press.hold, press.right, heart, play, a, b);
    ImGui::PopID();
}

} // namespace etude::app::ui
