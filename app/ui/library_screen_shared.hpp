// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of library_screen.cpp share: the icons, colours, names and drawn pieces the
// library screen uses (definitions in library_screen_shared.cpp). Not part of the app UI's interface.
#pragma once

#include "library_screen.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "preview.hpp"
#include "tap.hpp"
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

namespace etude::app::ui::lib_shared {

// Lucide icons the library uses beyond icons.hpp (lucide-font 1.55.0).
namespace lib_icon {
constexpr const char* kSearch = "\xEE\x85\x91";    // search
constexpr const char* kHeart = "\xEE\x83\xB2";     // heart: drawn (draw_heart), never the font's
constexpr const char* kClock = "\xEE\x82\x87";     // clock
constexpr const char* kSortAz = "\xEE\x90\x95";    // arrow-down-a-z
constexpr const char* kGroup = "\xEE\x91\xA4";     // group
constexpr const char* kListView = "\xEE\x96\x8A";  // rows-3
constexpr const char* kShelves = "\xEE\x93\x8E";   // gallery-horizontal
constexpr const char* kUser = "\xEE\x86\x9F";      // user
constexpr const char* kLevel = "\xEE\x81\xA8";     // chart-no-axes-column
constexpr const char* kTrend = "\xEE\x86\x91";     // trending-up
constexpr const char* kHistory = "\xEE\x87\xB5";   // history
constexpr const char* kAdded = "\xEE\x8A\xBC";     // calendar-plus
constexpr const char* kTag = "\xEE\x85\xBF";       // tag
constexpr const char* kNoGroup = "\xEE\x90\x82";   // circle-slash
constexpr const char* kInfo = "\xEE\x83\xB9";      // info
constexpr const char* kFunnelX = "\xEE\x8E\xB5";   // funnel-x
constexpr const char* kFunnel = "\xEE\x83\x9C";    // funnel
constexpr const char* kSelect = "\xEE\x87\x90";    // list-checks
constexpr const char* kFolder = "\xEE\x83\x97";    // folder
constexpr const char* kNewFolder = "\xEE\x83\x99"; // folder-plus
constexpr const char* kMoveTo = "\xEE\x8C\xB4";    // folder-input
constexpr const char* kInbox = "\xEE\x83\xB7";     // inbox
constexpr const char* kAddTo = "\xEE\x88\xBF";     // list-plus
constexpr const char* kMore = "\xEE\x82\xB6";      // ellipsis
constexpr const char* kPreview = "\xEE\x82\x80";   // circle-play
constexpr const char* kUp = "\xEE\x81\xB0";        // chevron-up
constexpr const char* kDown = "\xEE\x81\xAD";      // chevron-down
constexpr const char* kSome = "\xEE\x84\x9C";      // minus: some of the songs are in it
constexpr const char* kGlobe = "\xEE\x83\xA8";     // globe: the free catalogues
constexpr const char* kSheet = "\xEE\x95\x9E";     // file-music: the preview as sheet music
} // namespace lib_icon

// The screen's own background (col::kScreenBg).
constexpr ImVec4 kScreenBg = col::kScreenBg;

// A mastered song's ring; a favourite's heart (the star means difficulty).
constexpr ImVec4 kGood{0.42f, 0.84f, 0.58f, 1.0f};

constexpr ImVec4 kHeartRed{1.0f, 0.40f, 0.50f, 1.0f};

constexpr ImVec4 kTrack{1.0f, 1.0f, 1.0f, 0.14f};

constexpr ImVec4 kStarOff{1.0f, 1.0f, 1.0f, 0.22f}; // an empty level star's outline

const char* sort_name(BrowseSort s);

const char* sort_icon(BrowseSort s);

// The direction a sort goes, in words.
const char* sort_hint(BrowseSort s, bool reversed);

const char* group_name(BrowseGroup g);

const char* group_icon(BrowseGroup g);

inline const char* level_word(int level) {
    static const char* const names[] = {"Not read yet", "Easy", "Simple",
                                        "Medium",       "Hard", "Expert"};
    return names[std::clamp(level, 0, 5)];
}

// The thin band along a card's top: one colour per genre, grey for "Other".
inline ImVec4 genre_colour(std::string_view genre) {
    static constexpr ImVec4 kHues[] = {{0.80f, 0.62f, 0.36f, 1.0f}, {0.44f, 0.66f, 0.88f, 1.0f},
                                       {0.85f, 0.47f, 0.62f, 1.0f}, {0.50f, 0.78f, 0.71f, 1.0f},
                                       {0.61f, 0.77f, 0.48f, 1.0f}, {0.88f, 0.54f, 0.35f, 1.0f},
                                       {0.71f, 0.55f, 0.88f, 1.0f}, {0.90f, 0.78f, 0.36f, 1.0f}};
    if (genre.empty()) {
        return {0.48f, 0.52f, 0.60f, 1.0f};
    }
    std::uint32_t h = 2166136261u;
    for (const char c : genre) {
        h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    }
    return kHues[h % std::size(kHues)];
}

// "3:25", or "1:02:03" for an hour or more.
inline void length_text(FitText& out, double seconds) {
    const auto s = static_cast<long>(std::lround(std::max(0.0, seconds)));
    if (s >= 3600) {
        out.format("%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    } else {
        out.format("%ld:%02ld", s / 60, s % 60);
    }
}

inline void note(const char* name, ImVec2 min, ImVec2 max, InkKind kind = InkKind::Centred) {
    audit::note(name, min, max, kind);
}

// --- Taps and long presses (tap.hpp) -------------------------------------------------------------

// The press being handled came from a finger: the on-screen keyboard comes up for a field only
// then (a mouse click focuses it, and the computer keyboard types).
inline bool by_touch() { return osk::by_touch(); }

// --- Drawn pieces ------------------------------------------------------------------------------

// The heart's outline, `kHeartPoints` points round it, its box centred on 0 and its width 2.
constexpr int kHeartPoints = 48;

const std::array<ImVec2, kHeartPoints>& heart_outline();

// The favourite's heart, `r` half as wide, its box centred on `c`; filled when the song is one.
void draw_heart(const Kit& k, ImDrawList* dl, ImVec2 c, float r, bool filled, ImVec4 colour);

// An icon `size` across at `c`: the drawn heart for kHeart (filled red when `on`), else the
// font's glyph.
inline void draw_icon(const Kit& k, ImDrawList* dl, const char* glyph_text, ImVec2 c, float size,
               ImVec4 ink, bool on = false) {
    if (glyph_text == lib_icon::kHeart) {
        draw_heart(k, dl, c, size * 0.5f, on, on ? kHeartRed : ink);
    } else {
        glyph(k, dl, glyph_text, c, size, ink);
    }
}

// A card's surface, inside the current clip (the grid's): the kit's card without its shadow,
// which would reach past the grid's edge.
inline void card_face(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius) {
    dl->AddRectFilled(a, b, u32(col::kSurface), radius);
    dl->AddRectFilledMultiColor({a.x + radius * 0.5f, a.y + 1.0f},
                                {b.x - radius * 0.5f, a.y + k.s(tok::kS4)}, u32({1, 1, 1, 0.025f}),
                                u32({1, 1, 1, 0.025f}), u32({1, 1, 1, 0.0f}), u32({1, 1, 1, 0.0f}));
    dl->AddRect(a, b, u32(col::kHairline), radius, 0, k.s(tok::kHairline));
}

// A List row's surface: the card's, with the song's genre colour as a faint line round the whole
// row inside its edge, in the hairline's place.
inline void row_face(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, ImVec4 genre) {
    dl->AddRectFilled(a, b, u32(col::kSurface), radius);
    dl->AddRectFilledMultiColor({a.x + radius * 0.5f, a.y + 1.0f},
                                {b.x - radius * 0.5f, a.y + k.s(tok::kS4)}, u32({1, 1, 1, 0.025f}),
                                u32({1, 1, 1, 0.025f}), u32({1, 1, 1, 0.0f}), u32({1, 1, 1, 0.0f}));
    const float line = k.s(lib_tok::kRowOutline);
    dl->AddRect({a.x + line * 0.5f, a.y + line * 0.5f}, {b.x - line * 0.5f, b.y - line * 0.5f},
                u32(with_alpha(genre, lib_tok::kRowOutlineAlpha)), radius - line * 0.5f, 0, line);
}

// The colour band along the top of a card whose corners are rounded by `radius`: the card's own
// outline cut `h` down, so no clip rect is pushed (one per card would split the grid's draw list).
inline void card_band(ImDrawList* dl, ImVec2 a, float right, float h, float radius, ImU32 colour) {
    if (radius <= h) {
        dl->AddRectFilled(a, {right, a.y + h}, colour, radius, ImDrawFlags_RoundCornersTop);
        return;
    }
    // Where the band's lower edge meets each corner's arc.
    const float cut = std::asin((radius - h) / radius);
    constexpr float kHalfPi = 1.5707963f;
    dl->PathArcTo({a.x + radius, a.y + radius}, radius, 2.0f * kHalfPi + cut, 3.0f * kHalfPi);
    dl->PathArcTo({right - radius, a.y + radius}, radius, 3.0f * kHalfPi, 4.0f * kHalfPi - cut);
    dl->PathFillConvex(colour);
}

// A five-pointed star's ten points round `c`, the top point first, clockwise.
inline void star_points(ImVec2 c, float r, std::array<ImVec2, 10>& pts) {
    constexpr float kInner = 0.45f;
    // Its points reach r up and 0.81 r down: moved down by half the difference, its box is centred.
    const float down = r * (1.0f - std::cos(IM_PI / 5.0f)) * 0.5f;
    for (int i = 0; i < 10; ++i) {
        const float ang = -IM_PI * 0.5f + static_cast<float>(i) * IM_PI / 5.0f;
        const float rr = i % 2 == 0 ? r : r * kInner;
        pts[static_cast<std::size_t>(i)] = {c.x + std::cos(ang) * rr,
                                            c.y + down + std::sin(ang) * rr};
    }
}

inline ImVec4 to_vec(RGBA c) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, 1.0f};
}

inline float level_stars_width(const Kit& k) {
    const float r = k.s(lib_tok::kLevelStar);
    return static_cast<float>(kLevelStars) * 2.0f * r +
           static_cast<float>(kLevelStars - 1) * k.s(lib_tok::kLevelStarGap);
}

// The level as five stars from `x`, centred on `cy`: `half_stars` of them filled (in halves) in
// the level's colour, the rest a dim outline. Nothing for a song not read yet (0). `look` draws them
// in another size (the Melodies tiles'), the colours and the filled, half and empty stars the same.
struct LevelStarLook {
    float radius = 0.0f; // the outer radius, design points
    float gap = lib_tok::kLevelStarGap;
    float line = lib_tok::kLevelStarLine;
};
void draw_level_stars(const Kit& k, ImDrawList* dl, float x, float cy, int half_stars,
                      const LevelStarLook* look = nullptr);

// The gap between the hands, in a hand's size (lib_tok).
using lib_tok::kHandGapShare;

inline float hands_width(float size) { return size * lib_tok::kHandPairShare; }

inline void draw_hands(const Kit& k, ImDrawList* dl, float x, float cy, float size, bool left,
                bool right) {
    if (left) {
        glyph(k, dl, icon::kLeftHand, {x + size * 0.5f, cy}, size, k.hand_left);
    }
    if (right) {
        glyph(k, dl, icon::kRightHand, {x + size * (1.5f + kHandGapShare), cy}, size, k.hand_right);
    }
}

void draw_ring(const Kit& k, ImDrawList* dl, ImVec2 c, float r, int permille);

// Rows of the details card (library_screen_details_rows.cpp): the facts and the practice choices, and
// the best score with the runs. Each draws from `cy` and moves it down past what it drew.
void draw_details_facts(Kit& k, ImDrawList* dl, const CatalogueSong& s, const SongPractice& practice,
                        float tx, float& cy);
void draw_details_score(Kit& k, ImDrawList* dl, const LibraryPort& port, const CatalogueSong& s, float tx,
                        float right, float& cy);

// A round button with an icon: `hit` square at `c`, the face `tok::kFace` across. `ring` >= 0
// draws the best-score ring round it. `on` draws the selected state.
bool round_button(Kit& k, const char* id, const char* glyph_text, ImVec2 c, bool on,
                  Idle idle = Idle::None, int ring = -2, InkKind kind = InkKind::Centred,
                  float icon_size = 0.0f);

// A chip (the kit's: it sizes itself, `w` only widens it to a cell). Its width is `chip_width`.
inline bool text_chip(Kit& k, const char* id, const char* glyph_text, std::string_view text,
               const char* trail, ImVec2 pos, float w, bool on) {
    ChipOpts o;
    o.icon = glyph_text;
    o.trail = trail;
    o.on = on;
    o.width = w;
    o.draw_icon = draw_icon;
    return chip(k, id, text, pos, o);
}

// One row of a popover menu: an icon, words, an optional hint at the right.
bool menu_row(Kit& k, const char* id, const char* glyph_text, const char* label, const char* hint,
              ImVec2 pos, float w, bool on, bool enabled = true);

// --- Title lines -------------------------------------------------------------------------------

struct TitleLines {
    FitText line[2];
    int count = 0;
};

// `text` on at most two lines, the first `first_w` wide and the second `rest_w`, broken between
// words, the second cut with an ellipsis. Cached by (font, size, widths, text): a card that did
// not change is neither measured nor copied.
const TitleLines& title_lines(ImFont* font, float size, std::string_view text, float first_w,
                              float rest_w);
// Empties the title cache (its keys hold font addresses, see `forget_fonts`).
void forget_title_lines();

// The view's icon and name.
inline const char* view_icon(BrowseView v) {
    switch (v) {
    case BrowseView::Grid:
        return icon::kGrid;
    case BrowseView::List:
        return lib_icon::kListView;
    case BrowseView::Shelves:
        return lib_icon::kShelves;
    }
    return icon::kGrid;
}

inline const char* view_name(BrowseView v) {
    switch (v) {
    case BrowseView::Grid:
        return "Grid";
    case BrowseView::List:
        return "List";
    case BrowseView::Shelves:
        return "Shelves";
    }
    return "";
}

// "3 songs", "1 song".
inline void songs_text(FitText& out, std::size_t n) { out.format("%zu %s", n, n == 1 ? "song" : "songs"); }

// The songs the screen shows, in order: the shown list, or the nearest matches of a search that
// found none.
inline std::span<const std::uint32_t> visible_order(const LibraryCatalogue& cat) {
    return cat.searching() && cat.shown().empty() ? std::span<const std::uint32_t>(cat.near())
                                                  : std::span<const std::uint32_t>(cat.shown());
}

// The quick filters: the chips row's, and the Filters popover's rows for those it had no room for.
struct FilterChip {
    BrowseFilter filter;
    const char* glyph_text;
    const char* text; // empty: the icon alone
    const char* label;
    const char* id;
};

constexpr FilterChip kFilters[] = {
    {BrowseFilter::All, nullptr, "All", "All songs", "##library-all"},
    {BrowseFilter::Favourites, lib_icon::kHeart, "", "Favourites", "##library-favourites"},
    {BrowseFilter::Recent, lib_icon::kClock, "", "Played in the last 30 days", "##library-recent"},
    {BrowseFilter::InProgress, icon::kPractice, "In progress", "In progress",
     "##library-progress"}};

// The chips of one piece's versions: the words on each (the name, and the name with the best
// score, both made once by the catalogue) and how wide the chip is (measured once, ChipWidthCache),
// then which of them show (plan_version_chips).
struct VersionChips {
    std::size_t n = 0;
    std::size_t chosen = 0;
    std::array<std::string_view, kMaxVersions> named;
    std::array<std::string_view, kMaxVersions> scored;
    std::array<float, kMaxVersions> name_w{};
    std::array<float, kMaxVersions> score_w{};
    float more_w = 0.0f;
    VersionPlan plan;
};

inline ChipWidthCache::Widths& widths_of(ChipWidthCache& cache, const Kit& k, const LibraryCatalogue& cat,
                                  std::size_t song) {
    return cache.at(song, cat.size(), cat.version_words_version(), k.u);
}

VersionChips measure_version_chips(ChipWidthCache& cache, const Kit& k, const LibraryCatalogue& cat,
                                   std::size_t song, float width, bool wrap);

// A catalogue's colour, for the band at its rows' left end.
inline ImVec4 catalogue_colour(CatalogueId id) {
    switch (id) {
    case CatalogueId::Mutopia:
        return {0.44f, 0.66f, 0.88f, 1.0f};
    case CatalogueId::PianoMidi:
        return {0.88f, 0.54f, 0.35f, 1.0f};
    case CatalogueId::OpenScoreLieder:
        return {0.61f, 0.77f, 0.48f, 1.0f};
    }
    return col::kText3;
}

inline bool file_busy(const CatalogueFile& f) {
    return f.state == CatalogueFile::State::Queued || f.state == CatalogueFile::State::Fetching;
}

} // namespace etude::app::ui::lib_shared
