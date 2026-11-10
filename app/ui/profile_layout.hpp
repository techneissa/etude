// SPDX-License-Identifier: GPL-3.0-or-later
// Where the profile switcher's parts go, in ImGui points (the sizes are `profile_tok` in
// tokens.hpp): the chip in the top row, the "Who's playing" panel under it, and the sheet that adds
// or edits a profile, with the on-screen keyboard under it. No ImGui here.
//
// Rules: every pressable is at least `tok::kHit` each way and inside the window; no two pressables
// overlap; the sheet and its controls stay above the keyboard (osk_layout.hpp), never under it.
#pragma once

#include "face_layout.hpp"
#include "osk_layout.hpp"
#include "tokens.hpp"

#include "etude/profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace etude::app::ui {

// ---- The avatar ---------------------------------------------------------------------------------

// The letter on a profile's disc: the first character of the name, upper-cased when it is ASCII;
// "?" for an empty name. UTF-8 safe (a multi-byte first character comes whole).
inline std::string profile_initial(std::string_view name) {
    std::size_t at = 0;
    while (at < name.size() && name[at] == ' ') {
        ++at;
    }
    if (at >= name.size()) {
        return "?";
    }
    const auto lead = static_cast<unsigned char>(name[at]);
    std::size_t n = 1;
    if (lead >= 0xF0) {
        n = 4;
    } else if (lead >= 0xE0) {
        n = 3;
    } else if (lead >= 0xC0) {
        n = 2;
    }
    std::string out(name.substr(at, std::min(n, name.size() - at)));
    if (out.size() == 1 && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

// Whether the letter on a disc of colour 0xRRGGBB is drawn dark (a light colour) or light (a dark
// one): by the colour's relative luminance.


// Whether a disc of colour 0xRRGGBB is so dark it vanishes on the app's dark background: it is drawn
// with a light outline, and anything that uses the colour as ink on that background takes
// `profile_visible_colour` instead.
inline float colour_luminance(std::uint32_t rgb) {
    const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
    const float g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
    const float b = static_cast<float>(rgb & 0xFF) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}
inline bool avatar_ink_dark(std::uint32_t rgb) { return colour_luminance(rgb) > 0.38f; }
inline bool colour_needs_outline(std::uint32_t rgb) { return colour_luminance(rgb) < 0.14f; }
// The colour to draw a ring, a mark or a line in on the dark background: the colour itself, or a
// light grey for one that would vanish.
inline std::uint32_t profile_visible_colour(std::uint32_t rgb) {
    return colour_needs_outline(rgb) ? 0xC8C8CE : (rgb & 0xFFFFFF);
}

// ---- The chip in the top row --------------------------------------------------------------------

// The chip's width for a name `text_w` wide: the avatar, the name (cut to `kChipTextMax`), a
// chevron, and the padding between; never under `kChipMinW`.
inline float profile_chip_width(float text_w, float u) {
    const float text = std::min(text_w, profile_tok::kChipTextMax * u);
    const float w = (tok::kS3 + profile_tok::kChipAvatar + tok::kS2) * u + text +
                    (tok::kS2 + tok::kIconSm + tok::kS3) * u;
    return std::max(w, profile_tok::kChipMinW * u);
}

// ---- The panel ----------------------------------------------------------------------------------

inline constexpr std::size_t kMaxTiles = kMaxProfiles + 1; // every profile, then Add

struct ProfilePanelPlan {
    Box panel;
    Box title;      // "WHO'S PLAYING"
    Box edit;       // the Edit button (its hit area)
    // The tiles in content coordinates, which are the window's when the grid does not scroll.
    std::array<Box, kMaxTiles> tile{}; // the profiles in order, then the Add tile
    std::size_t tiles = 0;             // how many of `tile` are used (the Add tile included)
    bool add = false;                  // the last tile is Add (the household is not full)
    bool flat = false;                 // the tiles are rows: the avatar beside the name
    bool shorter = false;              // the big tiles on a short window
    float avatar = 0.0f;               // the avatar's diameter in a tile
    Box grid;                          // where the tiles show
    bool scrolls = false;              // more tiles than the window holds: the grid scrolls
    float content_h = 0.0f;            // the tiles' height together
    Box progress;                      // the "Progress and household" row (empty without it)
};

// The panel under the top row. `anchor_x` is the chip's left edge, `top_y` the top row's bottom,
// `display` the window, `profiles` how many there are. The tiles are big (the avatar over the name
// over the streak) when they fit, else flat rows; either way every profile and the Add tile show,
// and where even flat rows are too many for the window (sixteen people on a small one) the grid
// scrolls. `with_progress`: the "Progress and household" row under the tiles.
inline ProfilePanelPlan plan_profile_panel(float anchor_x, float top_y, const Box& display,
                                           float u, std::size_t profiles, bool with_progress) {
    namespace pt = profile_tok;
    ProfilePanelPlan p;
    const float margin = tok::kS3 * u;
    const float pad = pt::kPanelPad * u;
    const float gap = tok::kS2 * u;
    const float w = std::min(pt::kPanelW * u, display.w() - 2.0f * margin);
    const float x = std::clamp(anchor_x, margin, std::max(margin, display.x1 - margin - w));
    const float y = top_y + gap;
    const std::size_t n = std::min(profiles, kMaxProfiles);
    p.add = n < kMaxProfiles;
    p.tiles = n + (p.add ? 1 : 0);
    const float room = display.y1 - y - margin;
    const float row = with_progress ? gap + pt::kRowH * u : 0.0f;
    const float chrome = pad + pt::kHeadH * u + gap + row + pad;
    const auto cols_for = [&](float min_w) {
        return std::max<std::size_t>(
            1, static_cast<std::size_t>(std::floor((w - 2.0f * pad + gap) / (min_w * u + gap))));
    };
    const auto rows_for = [&](std::size_t cols) { return (p.tiles + cols - 1) / cols; };
    struct Fit {
        float tile_h;
        float min_w;
        float avatar;
        bool flat;
        bool shorter;
    };
    const std::array<Fit, 3> fits{{{pt::kTileH, pt::kTileMinW, pt::kAvatarBig, false, false},
                                   {pt::kTileShortH, pt::kTileMinW, pt::kAvatarShort, false, true},
                                   {pt::kTileFlatH, pt::kTileFlatMinW, pt::kAvatarFlat, true, false}}};
    Fit chosen = fits.back();
    std::size_t cols = std::min<std::size_t>(cols_for(chosen.min_w), 5);
    for (const Fit& f : fits) {
        const std::size_t c = std::min<std::size_t>(cols_for(f.min_w), 5);
        const float h = static_cast<float>(rows_for(c)) * (f.tile_h * u + gap) - gap;
        if (chrome + h <= room) {
            chosen = f;
            cols = c;
            break;
        }
    }
    p.flat = chosen.flat;
    p.shorter = chosen.shorter;
    p.avatar = chosen.avatar * u;
    const std::size_t rows = rows_for(cols);
    const float tile_w = (w - 2.0f * pad - static_cast<float>(cols - 1) * gap) / static_cast<float>(cols);
    const float tile_h = chosen.tile_h * u;
    p.content_h = static_cast<float>(rows) * (tile_h + gap) - gap;
    float grid_h = p.content_h;
    if (chrome + grid_h > room) {
        grid_h = std::max(tile_h, room - chrome); // the grid scrolls
        p.scrolls = true;
    }
    const float h = pad + pt::kHeadH * u + gap + grid_h + row + pad;
    p.panel = {x, y, x + w, y + h};
    p.title = {x + pad + tok::kS1 * u, y + pad, x + w - pad - pt::kEditW * u, y + pad + pt::kHeadH * u};
    p.edit = {x + w - pad - pt::kEditW * u, y + pad, x + w - pad, y + pad + pt::kHeadH * u};
    const float gy = y + pad + pt::kHeadH * u + gap;
    p.grid = {x + pad, gy, x + w - pad, gy + grid_h};
    for (std::size_t i = 0; i < p.tiles; ++i) {
        const float tx = x + pad + static_cast<float>(i % cols) * (tile_w + gap);
        const float ty = gy + static_cast<float>(i / cols) * (tile_h + gap);
        p.tile[i] = {tx, ty, tx + tile_w, ty + tile_h};
    }
    if (with_progress) {
        p.progress = {x + pad, gy + grid_h + gap, x + w - pad, gy + grid_h + gap + pt::kRowH * u};
    }
    return p;
}

// ---- The sheet ----------------------------------------------------------------------------------

// How much of the sheet there is room for, richest first: the title row and everything; no title
// row; and, where the keyboard leaves very little, the field with Cancel and Save on one row and the
// colours under it (the goal then stays as it is, and so does Delete, which is for an edit with the
// keyboard down: a tap on a colour or the keyboard's Done brings it back).
enum class SheetForm : std::uint8_t { Full, Compact, Tight };

// The colours: every one a hit area at least a hit wide and tall, a dead gap from the next, the
// cells of a row (a hit area and a gap) sharing the card's inner width evenly. One row when they
// all fit, else two or more rows of an even count.
struct SwatchGrid {
    int rows = 1;
    int per_row = 1;
    float cell_w = 0.0f; // a hit area and the gap after it
    bool hidden = false; // the Tight form on a window too short for the colours and a keyboard
};

inline SwatchGrid plan_swatch_grid(float inner_w, float u, std::size_t count) {
    SwatchGrid g;
    const int n = static_cast<int>(count);
    const float gap = tok::kHitGap * u;
    const int fit = std::max(
        1, static_cast<int>(std::floor((inner_w + gap + 1e-3f) / (tok::kCell * u))));
    g.rows = (n + fit - 1) / fit;
    g.per_row = (n + g.rows - 1) / g.rows;
    g.cell_w = (inner_w + gap) / static_cast<float>(g.per_row);
    return g;
}

// The sheet's card width and the padding each form keeps inside it.
inline float profile_sheet_width(float display_w, float u) {
    return std::min(profile_tok::kSheetW * u, display_w - 2.0f * tok::kS3 * u);
}
inline float profile_sheet_pad(SheetForm form, float u) {
    return (form == SheetForm::Tight ? tok::kS2 : tok::kS3) * u;
}
inline SwatchGrid profile_swatch_grid(float display_w, float u, SheetForm form) {
    return plan_swatch_grid(profile_sheet_width(display_w, u) - 2.0f * profile_sheet_pad(form, u), u,
                            kProfileColours.size());
}

struct ProfileSheetPlan {
    Box card;
    SheetForm form = SheetForm::Full;
    bool compact() const { return form != SheetForm::Full; }
    bool goal_row = false; // the goal's own row (an edit has Delete in the buttons' row; a narrow card)
    bool goal = false;     // the goal stepper is shown at all
    bool swatches = true;  // the colours are shown (Tight leaves them out when they need two rows)
    SwatchGrid grid;
    Box back;              // the back chevron (same as Cancel), left of the title (Full only)
    Box title;
    Box avatar;            // the disc, square
    Box field;
    std::array<Box, kProfileColours.size()> swatch{}; // the hit cells of the colours, in palette order
    Box goal_label;
    Box goal_minus;
    Box goal_value;
    Box goal_plus;
    Box cancel;
    Box save;
    Box del;               // Delete, at the left end of the buttons' row (an edit; empty otherwise)
};

// Whether the colours show in a form: always, but in Tight, where they need to fit one row.
inline bool profile_swatches_shown(SheetForm form, const SwatchGrid& grid) {
    return form != SheetForm::Tight || (grid.rows == 1 && !grid.hidden);
}

// The colours' rows together: a hit each, a dead gap between two.
inline float swatch_block_h(const SwatchGrid& grid, float u) {
    return static_cast<float>(grid.rows) * tok::kHit * u +
           static_cast<float>(grid.rows - 1) * tok::kHitGap * u;
}

// The height of the card for a form (design points times `u`).
inline float profile_sheet_height(float u, SheetForm form, bool goal_row, const SwatchGrid& grid = {}) {
    const float gap = tok::kS2 * u;
    const float hit = tok::kHit * u;
    const float field = 48.0f * u;
    if (form == SheetForm::Tight) {
        return 2.0f * gap + field + (profile_swatches_shown(form, grid) ? gap + hit : 0.0f);
    }
    const float pad = tok::kS3 * u;
    float h = 2.0f * pad + field + gap + swatch_block_h(grid, u) + gap + hit;
    if (form == SheetForm::Full) {
        h += hit + gap;
    }
    if (goal_row) {
        h += hit + gap;
    }
    return h;
}

// What the delete-profile confirm draws at `step` (`tok::wide_step`), in design points: the words
// that say what is lost, a line's height and the heading. Step 0 is what 800x480 always drew.
inline float profile_confirm_text(int step) {
    return tok::popup_type(profile_tok::kConfirmText, step, tok::PopupText::Text);
}
inline float profile_confirm_line_h(int step) {
    return profile_confirm_text(step) * profile_tok::kConfirmLineRatio;
}
inline float profile_confirm_title(int step) {
    return tok::popup_type(tok::kTitle, step, tok::PopupText::Title);
}
// The heading one step of the ladder down, never below the body step: what the tight confirm
// takes when its room is short.
inline float profile_confirm_title_down(float pt) {
    return pt > tok::kTitle ? tok::kTitle : pt > tok::kBody ? tok::kBody : pt;
}
// The tight confirm (over the keyboard): the heading with the warning at the right of the same row,
// the line of what is lost under them when `description`, then one row of Cancel, the field and
// Delete, each a hit tall. By default the least one: no description, the heading a step down.
inline float tight_confirm_height(float u, int step, bool description = false, float title_pt = 0.0f,
                                  int title_lines = 1) {
    const float title = title_pt > 0.0f ? title_pt : profile_confirm_title_down(profile_confirm_title(step));
    const float gap = tok::kS2 * u;
    const float heading = static_cast<float>(title_lines) * title * profile_tok::kConfirmLineRatio * u;
    return gap + std::max(heading, profile_confirm_line_h(step) * u) +
           (description ? profile_confirm_line_h(step) * u : 0.0f) + gap +
           tok::kHit * tok::popup_grow(step) * u;
}

// The keyboard under the sheet: the window's whole width, its keys leaving the tight sheet (or the
// confirm) its room (the sheet may reach over the top row when the keyboard is up) and shrinking
// toward a hit tall before they take more.
inline float profile_keyboard_height(float display_w, float display_h, float u) {
    const float tight_confirm = tight_confirm_height(u, tok::wide_step(display_w));
    // The least sheet leaves the colours out (the Tight form's colours go first); the keys never go
    // under a hit area and a gap, so the least keyboard is kept whatever the room.
    SwatchGrid none = profile_swatch_grid(display_w, u, SheetForm::Tight);
    none.hidden = true;
    const float least_sheet =
        std::max(profile_sheet_height(u, SheetForm::Tight, false, none), tight_confirm) +
        2.0f * tok::kS2 * u;
    const float room = display_h - least_sheet;
    return std::min(keyboard_height(display_w, u, room),
                    std::max(room, keyboard_height(display_w, u, 0.0f)));
}

// Which form the sheet takes and where its card starts, for a window `display` with the top row
// ending at `top_y` and a keyboard beginning at `keys_top` (`display.y1` with none up): the richest
// form that fits under the top row, else the richest that fits at all, reaching over the top row.
struct SheetFit {
    SheetForm form = SheetForm::Tight;
    float y = 0.0f;
    bool hide_swatches = false; // the Tight form has no room for its colours
};

inline SheetFit fit_profile_sheet(const Box& display, float top_y, float u, float keys_top,
                                  bool with_goal, bool narrow) {
    const float gap = tok::kS2 * u;
    const bool keys_up = keys_top < display.y1 - 0.5f;
    const float bottom = keys_top - (keys_up ? gap : 0.0f);
    const auto height = [&](SheetForm f, bool hide = false) {
        SwatchGrid grid = profile_swatch_grid(display.w(), u, f);
        grid.hidden = hide;
        return profile_sheet_height(u, f, with_goal && f != SheetForm::Tight && narrow, grid);
    };
    for (const SheetForm f : {SheetForm::Full, SheetForm::Compact, SheetForm::Tight}) {
        if (top_y + gap + height(f) <= bottom) {
            return {f, top_y + gap};
        }
    }
    SheetFit fit;
    for (const SheetForm f : {SheetForm::Compact, SheetForm::Tight}) {
        if (display.y0 + gap + height(f) <= bottom) {
            fit.form = f;
            break;
        }
    }
    fit.hide_swatches =
        fit.form == SheetForm::Tight && display.y0 + gap + height(fit.form) > bottom;
    fit.y = std::max(display.y0 + gap, bottom - height(fit.form, fit.hide_swatches));
    return fit;
}

// The goal stepper's four cells on the row at `row_y`, the label first.
inline void place_profile_goal(ProfileSheetPlan& p, float x, float row_y, float u) {
    namespace pt = profile_tok;
    const float hit = tok::kHit * u;
    p.goal_label = {x, row_y, x + pt::kGoalLabelW * u, row_y + hit};
    p.goal_minus = {p.goal_label.x1, row_y, p.goal_label.x1 + hit, row_y + hit};
    p.goal_value = {p.goal_minus.x1, row_y, p.goal_minus.x1 + pt::kGoalValueW * u, row_y + hit};
    p.goal_plus = {p.goal_value.x1, row_y, p.goal_value.x1 + hit, row_y + hit};
}

// The colours' cells, row by row across the card's inner width.
inline void place_profile_swatches(ProfileSheetPlan& p, float x0, float y0, float u) {
    const float hit = tok::kHit * u;
    const float gap = tok::kHitGap * u;
    for (std::size_t i = 0; i < p.swatch.size(); ++i) {
        const int row = static_cast<int>(i) / p.grid.per_row;
        const int col = static_cast<int>(i) % p.grid.per_row;
        const float cx = x0 + static_cast<float>(col) * p.grid.cell_w;
        const float cy = y0 + static_cast<float>(row) * (hit + gap);
        p.swatch[i] = {cx, cy, cx + p.grid.cell_w - gap, cy + hit};
    }
}

// The sheet for a window `display`, under the top row (`top_y`), clear of a keyboard that begins at
// `keys_top` (`display.y1` with none up). `with_goal`: the goal stepper shows (Tight leaves it out).
// `with_delete`: an edit, which has Delete at the left end of the buttons' row, a gap of the Delete
// rule (a few touches) from Cancel; the goal then has a row of its own.
inline ProfileSheetPlan plan_profile_sheet(float anchor_x, float top_y, const Box& display, float u,
                                           float keys_top, bool with_goal, bool with_delete = false) {
    namespace pt = profile_tok;
    ProfileSheetPlan p;
    const float margin = tok::kS3 * u;
    const float gap = tok::kS2 * u;
    const float hit = tok::kHit * u;
    const float w = profile_sheet_width(display.w(), u);
    const float x = std::clamp(anchor_x, margin, std::max(margin, display.x1 - margin - w));
    const bool narrow = w < pt::kSheetNeedW * u || with_delete;
    const SheetFit fit = fit_profile_sheet(display, top_y, u, keys_top, with_goal, narrow);
    p.form = fit.form;
    const bool tight = p.form == SheetForm::Tight;
    p.grid = profile_swatch_grid(display.w(), u, p.form);
    p.grid.hidden = fit.hide_swatches;
    p.swatches = profile_swatches_shown(p.form, p.grid);
    p.goal = with_goal && !tight;
    p.goal_row = p.goal && narrow;
    const float pad = profile_sheet_pad(p.form, u);
    p.card = {x, fit.y, x + w, fit.y + profile_sheet_height(u, p.form, p.goal_row, p.grid)};
    float cy = fit.y + pad;
    if (p.form == SheetForm::Full) {
        p.back = {x + gap / 2.0f, cy, x + gap / 2.0f + hit, cy + hit};
        p.title = {p.back.x1 + gap, cy, x + w - pad, cy + hit};
        cy += hit + gap;
    }
    const float field_h = 48.0f * u;
    const float av = std::min(pt::kAvatarSheet * u, field_h);
    p.avatar = {x + pad, cy + (field_h - av) / 2.0f, x + pad + av, cy + (field_h - av) / 2.0f + av};
    // Cancel and Save at the right end of their row: the field's own row in the Tight form.
    p.save = {x + w - pad - pt::kSaveW * u, 0.0f, x + w - pad, 0.0f};
    p.cancel = {p.save.x0 - gap - pt::kButtonW * u, 0.0f, p.save.x0 - gap, 0.0f};
    const auto set_buttons_row = [&](float row_y) {
        p.save.y0 = p.cancel.y0 = row_y;
        p.save.y1 = p.cancel.y1 = row_y + hit;
        if (with_delete) {
            p.del = {x + pad, row_y, x + pad + pt::kDeleteW * u, row_y + hit};
        }
    };
    p.field = {p.avatar.x1 + gap, cy, tight ? p.cancel.x0 - gap : x + w - pad, cy + field_h};
    if (tight) {
        set_buttons_row(cy + (field_h - hit) / 2.0f);
        p.del = {}; // no room beside the field: Delete is there once the keyboard is down
    }
    cy += field_h + gap;
    if (p.swatches) {
        place_profile_swatches(p, x + pad, cy, u);
        cy += swatch_block_h(p.grid, u) + gap;
    }
    if (tight) {
        return p;
    }
    if (p.goal) {
        place_profile_goal(p, x + pad, cy, u);
    }
    if (p.goal_row) {
        cy += hit + gap;
    }
    set_buttons_row(cy);
    return p;
}

// Why a profile cannot be deleted, as a sentence for the sheet (empty when it can).
inline const char* profile_delete_reason(ProfileResult result) {
    switch (result) {
    case ProfileResult::LastProfile:
        return "The only profile can't be deleted";
    case ProfileResult::FirstProfile:
        return "The first profile can't be deleted";
    default:
        return "";
    }
}

// ---- The confirm that deletes a profile -----------------------------------------------------------

// The card that replaces the sheet. The name has to be typed to enable Delete, so it holds a field,
// and the on-screen keyboard may be up under it. Roomy: the avatar and "Delete <name>?", what is lost
// in two lines across the whole card and, on a line of its own, that it can't be undone, the field, and Cancel at the left and Delete at the
// right of the last row. Tight, where the keyboard leaves little: no title row, one line and the warning, and one row
// of Cancel, the field and Delete, the field between the buttons. Either way the buttons are the
// whole card apart (a tap meant for one never lands on the other) and above the keyboard.
enum class ConfirmForm : std::uint8_t { Roomy, Tight };

struct ProfileConfirmPlan {
    Box card;
    ConfirmForm form = ConfirmForm::Roomy;
    Box avatar;                       // Roomy only
    Box title;                        // the heading, over the card's width less the avatar
    std::array<Box, profile_tok::kConfirmLines> line{};
    int lines = 0;                    // how many of `line` are used: the description (if any), then the warning
    Box field;
    Box cancel;
    Box remove;
    int step = 0;          // `tok::wide_step` of the window: the popup rule's larger words and buttons
    float title_pt = 0.0f; // the heading's size, design points
};

// The heading's row: a hit tall, or `title_lines` lines of the heading when it wraps.
inline float profile_confirm_title_row(float u, int step, int title_lines) {
    return std::max(tok::kHit * tok::popup_grow(step) * u,
                    static_cast<float>(title_lines) * profile_confirm_title(step) *
                        profile_tok::kConfirmLineRatio * u);
}

// One way to lay the card out; the plan takes the first that fits.
struct ConfirmVariant {
    ConfirmForm form;
    bool description; // the line of what is lost
    float title_pt;   // Tight only: the heading's size
};

inline float profile_confirm_height(float u, const ConfirmVariant& v, int step, int title_lines = 1) {
    namespace pt = profile_tok;
    if (v.form == ConfirmForm::Tight) {
        return tight_confirm_height(u, step, v.description, v.title_pt, title_lines);
    }
    const float gap = tok::kS2 * u;
    const float hit = tok::kHit * u * tok::popup_grow(step);
    return 2.0f * tok::kS3 * u + profile_confirm_title_row(u, step, title_lines) + gap +
           static_cast<float>(pt::kConfirmLines) * profile_confirm_line_h(step) * u + gap +
           pt::kConfirmFieldH * u + gap + hit;
}

// The ways to lay the card out, richest first: the whole card, then the tight one with its
// description, without it, and without it with the heading a step down.
inline std::array<ConfirmVariant, 4> confirm_variants(int step) {
    const float title = profile_confirm_title(step);
    return {{{ConfirmForm::Roomy, true, title},
             {ConfirmForm::Tight, true, title},
             {ConfirmForm::Tight, false, title},
             {ConfirmForm::Tight, false, profile_confirm_title_down(title)}}};
}

// The tight card's rows from its top: the heading at the left of the row and the warning (`warning_w`
// wide, 0: the whole row) at its right, the description when it stays, then Cancel, the field and
// Delete in one row a hit tall.
inline void lay_out_tight_confirm(ProfileConfirmPlan& p, const ConfirmVariant& v, float u, int title_lines,
                                  float warning_w) {
    namespace pt = profile_tok;
    const float gap = tok::kS2 * u;
    const float line_h = profile_confirm_line_h(p.step) * u;
    const float hit = tok::kHit * u * tok::popup_grow(p.step);
    const float x0 = p.card.x0 + gap;
    const float x1 = p.card.x1 - gap;
    float cy = p.card.y0 + gap;
    const float title_h = static_cast<float>(title_lines) * v.title_pt * pt::kConfirmLineRatio * u;
    const float row_h = std::max(title_h, line_h);
    const float warn_x = warning_w > 0.0f ? x1 - warning_w : x0;
    p.title = {x0, cy, warning_w > 0.0f ? warn_x - gap : x1, cy + row_h};
    p.lines = v.description ? 2 : 1;
    p.line[static_cast<std::size_t>(p.lines - 1)] = {warn_x, cy, x1, cy + row_h}; // the warning
    cy += row_h;
    if (v.description) {
        p.line[0] = {x0, cy, x1, cy + line_h};
        cy += line_h;
    }
    cy += gap;
    const float grow = tok::popup_grow(p.step);
    p.cancel = {x0, cy, x0 + pt::kButtonW * u * grow, cy + hit};
    p.remove = {x1 - pt::kRemoveW * u * grow, cy, x1, cy + hit};
    p.field = {p.cancel.x1 + gap, cy, p.remove.x0 - gap, cy + hit};
    p.title_pt = v.title_pt;
}

// The card for a window `display` with the top row ending at `top_y` and a keyboard beginning at
// `keys_top` (`display.y1` with none up): the richest variant that fits under the top row, else the
// richest that fits below the top of the window (reaching over the top row, which the host then
// leaves out), else the least one at the top of the window. The card ends at the keys' top at the
// lowest: the keys' own pad keeps it 2 mm from their hit areas. `title_lines` lines of heading; the
// tight card's warning is `warning_w` wide (measured by the caller).
inline ProfileConfirmPlan plan_profile_confirm(float anchor_x, float top_y, const Box& display, float u,
                                               float keys_top, int title_lines = 1, float warning_w = 0.0f) {
    namespace pt = profile_tok;
    ProfileConfirmPlan p;
    p.step = tok::wide_step(display.w());
    const float margin = tok::kS3 * u;
    const float gap = tok::kS2 * u;
    const float pad = tok::kS3 * u;
    const float grow = tok::popup_grow(p.step);
    const float hit = tok::kHit * u * grow;
    const float line_h = profile_confirm_line_h(p.step) * u;
    const float w = std::min(pt::kSheetW * u, display.w() - 2.0f * margin);
    const float x = std::clamp(anchor_x, margin, std::max(margin, display.x1 - margin - w));
    const std::array<ConfirmVariant, 4> variants = confirm_variants(p.step);
    ConfirmVariant v = variants.back();
    float y = keys_top - profile_confirm_height(u, v, p.step, title_lines);
    bool found = false;
    // Under the top row, then at the top of the window with air above, then with half of it.
    for (const float top : {top_y + gap, display.y0 + gap, display.y0 + gap * 0.5f}) {
        for (const ConfirmVariant& c : variants) {
            if (!found && top + profile_confirm_height(u, c, p.step, title_lines) <= keys_top) {
                v = c;
                y = top;
                found = true;
            }
        }
    }
    const float h = profile_confirm_height(u, v, p.step, title_lines);
    // A card the window's height can hardly hold reaches the top edge, never the keys.
    y = std::max(display.y0, y);
    p.form = v.form;
    p.title_pt = profile_confirm_title(p.step);
    p.card = {x, y, x + w, y + h};
    if (v.form == ConfirmForm::Tight) {
        lay_out_tight_confirm(p, v, u, title_lines, warning_w);
        return p;
    }
    const float inner0 = x + pad;
    const float inner1 = x + w - pad;
    float cy = y + pad;
    const float av = std::min(pt::kAvatarSheet * u, hit);
    p.avatar = {inner0, cy, inner0 + av, cy + av};
    const float title_row = profile_confirm_title_row(u, p.step, title_lines);
    p.title = {p.avatar.x1 + gap, cy, inner1, cy + title_row};
    cy += title_row + gap;
    p.lines = pt::kConfirmLines;
    for (int i = 0; i < p.lines; ++i) {
        p.line[static_cast<std::size_t>(i)] = {inner0, cy, inner1, cy + line_h};
        cy += line_h;
    }
    cy += gap;
    p.field = {inner0, cy, inner1, cy + pt::kConfirmFieldH * u};
    cy += pt::kConfirmFieldH * u + gap;
    p.cancel = {inner0, cy, inner0 + pt::kButtonW * u * grow, cy + hit};
    p.remove = {inner1 - pt::kRemoveW * u * grow, cy, inner1, cy + hit};
    return p;
}

} // namespace etude::app::ui
