// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_screen_shared.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app::ui::lib_shared {

const char* sort_name(BrowseSort s) {
    switch (s) {
    case BrowseSort::Name:
        return "Name";
    case BrowseSort::Composer:
        return "Composer";
    case BrowseSort::Difficulty:
        return "Level";
    case BrowseSort::Progress:
        return "Progress";
    case BrowseSort::Length:
        return "Length";
    case BrowseSort::Recent:
        return "Recently played";
    case BrowseSort::Added:
        return "Date added";
    }
    return "";
}

const char* sort_icon(BrowseSort s) {
    switch (s) {
    case BrowseSort::Name:
        return lib_icon::kSortAz;
    case BrowseSort::Composer:
        return lib_icon::kUser;
    case BrowseSort::Difficulty:
        return lib_icon::kLevel;
    case BrowseSort::Progress:
        return lib_icon::kTrend;
    case BrowseSort::Length:
        return icon::kTimer;
    case BrowseSort::Recent:
        return lib_icon::kHistory;
    case BrowseSort::Added:
        return lib_icon::kAdded;
    }
    return lib_icon::kSortAz;
}

// The direction a sort goes, in words.
const char* sort_hint(BrowseSort s, bool reversed) {
    switch (s) {
    case BrowseSort::Name:
    case BrowseSort::Composer:
        return reversed ? "Z to A" : "A to Z";
    case BrowseSort::Difficulty:
        return reversed ? "Hard first" : "Easy first";
    case BrowseSort::Progress:
        return reversed ? "Lowest first" : "Best first";
    case BrowseSort::Length:
        return reversed ? "Long first" : "Short first";
    case BrowseSort::Recent:
        return reversed ? "Oldest first" : "Latest first";
    case BrowseSort::Added:
        return reversed ? "Oldest first" : "Newest first";
    }
    return "";
}

const char* group_name(BrowseGroup g) {
    switch (g) {
    case BrowseGroup::None:
        return "No groups";
    case BrowseGroup::Composer:
        return "Composer";
    case BrowseGroup::Difficulty:
        return "Level";
    case BrowseGroup::Progress:
        return "Progress";
    case BrowseGroup::Genre:
        return "Genre";
    case BrowseGroup::MyGroups:
        return "My groups";
    }
    return "";
}

const char* group_icon(BrowseGroup g) {
    switch (g) {
    case BrowseGroup::None:
        return lib_icon::kNoGroup;
    case BrowseGroup::Composer:
        return lib_icon::kUser;
    case BrowseGroup::Difficulty:
        return lib_icon::kLevel;
    case BrowseGroup::Progress:
        return lib_icon::kTrend;
    case BrowseGroup::Genre:
        return lib_icon::kTag;
    case BrowseGroup::MyGroups:
        return lib_icon::kGroup;
    }
    return lib_icon::kGroup;
}

const std::array<ImVec2, kHeartPoints>& heart_outline() {
    static const std::array<ImVec2, kHeartPoints> pts = [] {
        std::array<ImVec2, kHeartPoints> p{};
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int i = 0; i < kHeartPoints; ++i) {
            const float t = 2.0f * IM_PI * static_cast<float>(i) / kHeartPoints;
            const float st = std::sin(t);
            const float x = 16.0f * st * st * st;
            const float y = -(13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) -
                              2.0f * std::cos(3.0f * t) - std::cos(4.0f * t));
            p[static_cast<std::size_t>(i)] = {x, y};
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
        const float scale = 2.0f / (x1 - x0);
        for (ImVec2& q : p) {
            q = {(q.x - (x0 + x1) * 0.5f) * scale, (q.y - (y0 + y1) * 0.5f) * scale};
        }
        return p;
    }();
    return pts;
}

// The favourite's heart, `r` half as wide, its box centred on `c`; filled when the song is one.
void draw_heart(const Kit& k, ImDrawList* dl, ImVec2 c, float r, bool filled, ImVec4 colour) {
    if (audit::icons_hidden()) {
        return; // the audit's frame without icons: the heart is one
    }
    const std::array<ImVec2, kHeartPoints>& unit = heart_outline();
    std::array<ImVec2, kHeartPoints> pts;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        pts[i] = {c.x + unit[i].x * r, c.y + unit[i].y * r};
    }
    if (filled) {
        dl->AddConcavePolyFilled(pts.data(), kHeartPoints, u32(colour));
    }
    // The outline in two halves from the dip at the top to the tip: a closed line would mitre
    // those two sharp corners into spikes.
    constexpr int kHalf = kHeartPoints / 2;
    const float line = k.s(lib_tok::kHeartLine);
    dl->AddPolyline(pts.data(), kHalf + 1, u32(colour), ImDrawFlags_None, line);
    std::array<ImVec2, kHalf + 1> left;
    std::copy(pts.begin() + kHalf, pts.end(), left.begin());
    left[kHalf] = pts[0];
    dl->AddPolyline(left.data(), kHalf + 1, u32(colour), ImDrawFlags_None, line);
    // Round where the halves meet, so the tip has the line's full ink.
    dl->AddCircleFilled(pts[0], line * 0.5f, u32(colour), 8);
    dl->AddCircleFilled(pts[kHalf], line * 0.5f, u32(colour), 8);
}

// The level as five stars from `x`, centred on `cy`: `half_stars` of them filled (in halves) in
// the level's colour, the rest a dim outline. Nothing for a song not read yet (0).
void draw_level_stars(const Kit& k, ImDrawList* dl, float x, float cy, int half_stars,
                      const LevelStarLook* look) {
    if (half_stars <= 0) {
        return;
    }
    const float r = k.s(look != nullptr ? look->radius : lib_tok::kLevelStar);
    const float gap = k.s(look != nullptr ? look->gap : lib_tok::kLevelStarGap);
    const float line = k.s(look != nullptr ? look->line : lib_tok::kLevelStarLine);
    const ImU32 fill = u32(to_vec(level_star_colour(half_stars * 0.5, k.high_contrast)));
    const ImU32 empty = u32(k.high_contrast ? col::kText3 : kStarOff);
    std::array<ImVec2, 10> pts;
    for (int i = 0; i < kLevelStars; ++i) {
        const ImVec2 c{x + r + static_cast<float>(i) * (2.0f * r + gap), cy};
        star_points(c, r, pts);
        const double f = level_star_fill(half_stars, i);
        if (f < 1.0) {
            dl->AddPolyline(pts.data(), 10, empty, ImDrawFlags_Closed, line);
        }
        if (f >= 1.0) {
            dl->AddConcavePolyFilled(pts.data(), 10, fill);
        } else if (f > 0.0) {
            // The left half: the bottom inner point round to the top point, all left of `c`.
            const std::array<ImVec2, 6> half{pts[5], pts[6], pts[7], pts[8], pts[9], pts[0]};
            dl->AddConcavePolyFilled(half.data(), 6, fill);
        }
    }
}

void draw_ring(const Kit& k, ImDrawList* dl, ImVec2 c, float r, int permille) {
    const float thick = k.s(lib_tok::kRing);
    dl->AddCircle(c, r, u32(kTrack), 40, thick);
    if (permille <= 0) {
        return;
    }
    const ImVec4 fill = permille >= kMasteredPermille ? kGood : col::kAccent;
    const float frac = std::min(1.0f, static_cast<float>(permille) / 1000.0f);
    if (frac >= 0.999f) {
        dl->AddCircle(c, r, u32(fill), 40, thick);
        return;
    }
    const float start = -IM_PI * 0.5f;
    dl->PathArcTo(c, r, start, start + 2.0f * IM_PI * frac, 40);
    dl->PathStroke(u32(fill), 0, thick);
}

// A round button with an icon: `hit` square at `c`, the face `tok::kFace` across. `ring` >= 0
// draws the best-score ring round it. `on` draws the selected state.
bool round_button(Kit& k, const char* id, const char* glyph_text, ImVec2 c, bool on,
                  Idle idle, int ring, InkKind kind, float icon_size) {
    c = {std::round(c.x), std::round(c.y)}; // on whole pixels, so the icon sits centred in it
    const float hit = k.s(tok::kHit);
    ImGui::SetCursorScreenPos({c.x - hit * 0.5f, c.y - hit * 0.5f});
    const bool pressed = ImGui::InvisibleButton(id, {hit, hit});
    const State st = item_state(k, on);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float half = k.s(tok::kFace) * 0.5f;
    const Box face{c.x - half, c.y - half, c.x + half, c.y + half};
    draw_state(k, dl, id, face,
               {c.x - hit * 0.5f, c.y - hit * 0.5f, c.x + hit * 0.5f, c.y + hit * 0.5f}, st, idle,
               half);
    if (ring >= -1) {
        draw_ring(k, dl, c, half + k.s(lib_tok::kRing) * 0.5f + k.s(tok::kHairline), ring);
    }
    note(id, {face.x0, face.y0}, {face.x1, face.y1}, kind);
    glyph(k, dl, glyph_text, c, icon_size > 0.0f ? icon_size : k.s(tok::kIconSm),
          state_ink(st, col::kText));
    return pressed;
}

// One row of a popover menu: an icon, words, an optional hint at the right.
bool menu_row(Kit& k, const char* id, const char* glyph_text, const char* label, const char* hint,
              ImVec2 pos, float w, bool on, bool enabled) {
    const float h = k.s(tok::kHit);
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, {w, h});
    ImGui::EndDisabled();
    const State st = item_state(k, on, true, !enabled);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box face{pos.x, pos.y + k.s(tok::kInset) * 0.5f, pos.x + w,
                   pos.y + h - k.s(tok::kInset) * 0.5f};
    draw_state(k, dl, id, face, {pos.x, pos.y, pos.x + w, pos.y + h}, st);
    note(id, pos, {pos.x + w, pos.y + h}, InkKind::Target);
    const float cy = pos.y + h * 0.5f;
    const ImVec4 ink = state_ink(st, col::kText);
    float x = pos.x + k.s(tok::kS3);
    if (glyph_text != nullptr) {
        draw_icon(k, dl, glyph_text, {x + k.s(tok::kIcon) * 0.5f, cy}, k.s(tok::kIcon),
                  state_ink(st, col::kText2));
        x += k.s(tok::kIcon) + k.s(tok::kS3);
    }
    const float size = k.s(tok::kBody);
    float right = face.x1 - k.s(tok::kS3);
    if (hint != nullptr) {
        const float hs = k.s(tok::kCaption);
        const ImVec2 hsz = text_size(k.regular, hs, hint);
        label_at(k, dl, k.regular, hs, {x, face.y0, right, face.y1}, LabelAlign::Right,
                 state_ink(st, col::kText2), hint);
        right -= hsz.x + k.s(tok::kS2);
    }
    const FitText shown = fit(on ? k.bold : k.regular, size, label, right - x);
    label_at(k, dl, on ? k.bold : k.regular, size, {x, face.y0, right, face.y1}, LabelAlign::Left,
             ink, shown.c_str());
    return enabled && pressed;
}

// `text` on at most two lines, the first `first_w` wide and the second `rest_w`, broken between
// words, the second cut with an ellipsis. Cached by (font, size, widths, text): a card that did
// not change is neither measured nor copied.
namespace {
std::unordered_map<std::uint64_t, TitleLines>& title_cache() {
    static std::unordered_map<std::uint64_t, TitleLines> cache;
    return cache;
}
} // namespace

void forget_title_lines() { title_cache().clear(); }

const TitleLines& title_lines(ImFont* font, float size, std::string_view text, float first_w,
                              float rest_w) {
    auto& cache = title_cache();
    std::uint64_t key = std::hash<std::string_view>{}(text);
    const auto mix_in = [&key](std::uint64_t v) {
        key ^= v + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2);
    };
    mix_in(reinterpret_cast<std::uintptr_t>(font));
    mix_in(static_cast<std::uint64_t>(size * 64.0f));
    mix_in(static_cast<std::uint64_t>(first_w * 8.0f));
    mix_in(static_cast<std::uint64_t>(rest_w * 8.0f));
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    if (cache.size() > 20000) {
        cache.clear(); // window sizes come and go; titles stay few
    }
    TitleLines out;
    // The most words that fit on the first line.
    std::size_t cut = 0; // bytes on the first line
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(' ', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string candidate(text.substr(0, end));
        if (cut > 0 && text_size(font, size, candidate.c_str()).x > first_w) {
            break;
        }
        cut = end;
        at = end + 1;
    }
    if (cut >= text.size()) {
        out.line[0] = fit(font, size, text, first_w);
        out.count = 1;
    } else {
        out.line[0] = fit(font, size, text.substr(0, cut), first_w);
        std::string_view rest = text.substr(cut);
        while (!rest.empty() && rest.front() == ' ') {
            rest.remove_prefix(1);
        }
        out.line[1] = fit(font, size, rest, rest_w);
        out.count = 2;
    }
    return cache.emplace(key, out).first->second;
}

VersionChips measure_version_chips(ChipWidthCache& cache, const Kit& k, const LibraryCatalogue& cat,
                                   std::size_t song, float width, bool wrap) {
    VersionChips v;
    const std::span<const std::uint32_t> versions = cat.versions_of(song);
    v.n = std::min(versions.size(), kMaxVersions);
    for (std::size_t i = 0; i < v.n; ++i) {
        ChipWidthCache::Widths& w = widths_of(cache, k, cat, versions[i]);
        v.named[i] = cat.version_name(versions[i]);
        v.scored[i] = cat.version_scored(versions[i]);
        if (w.name < 0.0f) {
            w.name = chip_width(k, nullptr, v.named[i]);
        }
        if (w.scored < 0.0f) {
            w.scored = v.scored[i].size() == v.named[i].size()
                           ? w.name
                           : chip_width(k, nullptr, v.scored[i]);
        }
        v.name_w[i] = w.name;
        v.score_w[i] = w.scored;
        if (versions[i] == song) {
            v.chosen = i;
        }
    }
    if (cache.more_w < 0.0f || cache.more_unit != k.u) {
        cache.more_w = chip_width(k, nullptr, "+9");
        cache.more_unit = k.u;
    }
    v.more_w = cache.more_w;
    v.plan = plan_version_chips({v.score_w.data(), v.n}, {v.name_w.data(), v.n}, v.chosen, width,
                                k.s(tok::kHitGap), v.more_w, wrap);
    return v;
}

} // namespace etude::app::ui::lib_shared
