// SPDX-License-Identifier: GPL-3.0-or-later
// Where the profile switcher's parts go (profile_layout.hpp): the chip, the "Who's playing" panel
// and the add / edit sheet with the on-screen keyboard under it, at the sizes the app is audited at
// and the scales it draws them at. Every pressable is a hit tall and wide, inside the window,
// clear of every other and, for the sheet, above the keyboard.
#include <doctest/doctest.h>

#include "ui/profile_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;
namespace pt = etude::app::ui::profile_tok;

namespace {

struct Size {
    float w;
    float h;
};
constexpr std::array<Size, 5> kWindows{
    {{800, 480}, {1024, 600}, {1280, 800}, {1366, 768}, {1920, 1080}}};
constexpr std::array<float, 3> kScales{1.0f, 1.25f, 1.5f};
constexpr float kEps = 1e-3f;

// The window in ImGui points (the controls grow by the unit `u` inside it).
Box display_for(Size s) { return {0.0f, 0.0f, s.w, s.h}; }

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

void check_pressables(const std::vector<Box>& boxes, const Box& inside, float u) {
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        CAPTURE(i);
        CHECK(boxes[i].w() >= tok::kHit * u - kEps);
        CHECK(boxes[i].h() >= tok::kHit * u - kEps);
        CHECK(inside.holds(boxes[i], -kEps));
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            CAPTURE(j);
            CHECK_FALSE(overlap(boxes[i], boxes[j]));
        }
    }
}

} // namespace

TEST_CASE("profile avatar: the first letter of the name, whole, upper case when ASCII") {
    CHECK(profile_initial("anna") == "A");
    CHECK(profile_initial("Ben") == "B");
    CHECK(profile_initial("  mum") == "M");
    CHECK(profile_initial("") == "?");
    CHECK(profile_initial("   ") == "?");
    CHECK(profile_initial("\xC3\x85sa") == "\xC3\x85"); // a two-byte first letter stays whole
    CHECK(profile_initial("\xE6\x97\xA5\xE6\x9C\xAC") == "\xE6\x97\xA5");
    CHECK(profile_initial("7th") == "7");
}

// The contrast ratio of two colours (WCAG's relative luminance, sRGB linearised).
float linear_of(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float relative_luminance(std::uint32_t rgb) {
    return 0.2126f * linear_of(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f) +
           0.7152f * linear_of(static_cast<float>((rgb >> 8) & 0xFF) / 255.0f) +
           0.0722f * linear_of(static_cast<float>(rgb & 0xFF) / 255.0f);
}
float contrast(std::uint32_t a, std::uint32_t b) {
    const float la = relative_luminance(a);
    const float lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}
constexpr std::uint32_t kAppDark = 0x0B0D12; // about the app's own background

TEST_CASE("profile avatar: dark letters on light colours, light on dark") {
    CHECK(avatar_ink_dark(0xE0B83C));
    CHECK(avatar_ink_dark(0x5CBF72));
    CHECK(avatar_ink_dark(0xFFFFFF)); // white takes the dark letter
    CHECK_FALSE(avatar_ink_dark(0x202060));
    CHECK_FALSE(avatar_ink_dark(0x000000)); // black takes the light one
    // Every colour of the palette: the letter, dark or light as chosen, reads on its disc.
    for (const std::uint32_t c : etude::kProfileColours) {
        CAPTURE(c);
        const std::uint32_t letter = avatar_ink_dark(c) ? 0x0B0D12 : 0xFFFFFF;
        CHECK(contrast(letter, c) >= 4.5f);
    }
}

TEST_CASE("profile colours: white and black are in the palette, and both show on the dark background") {
    bool white = false;
    bool black = false;
    for (const std::uint32_t c : etude::kProfileColours) {
        white = white || c == 0xFFFFFF;
        black = black || c == 0x000000;
    }
    CHECK(white);
    CHECK(black);
    // Black would vanish: its disc gets a light edge, and what is drawn in the colour (a ring, the
    // points circle's line) takes the light stand-in. Every colour of the palette is then visible.
    CHECK(colour_needs_outline(0x000000));
    CHECK_FALSE(colour_needs_outline(0xFFFFFF));
    CHECK(contrast(0x000000, kAppDark) < 1.5f);
    for (const std::uint32_t c : etude::kProfileColours) {
        CAPTURE(c);
        CHECK(contrast(profile_visible_colour(c), kAppDark) >= 3.0f);
    }
    // Colours that were there before are drawn as they were.
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(profile_visible_colour(etude::kProfileColours[i]) == etude::kProfileColours[i]);
    }
    // The first eight keep their place and value, so a stored profile colour means what it did.
    CHECK(etude::kProfileColours[0] == 0x4C9BE8);
    CHECK(etude::kProfileColours[7] == 0xD9534F);
}

TEST_CASE("profile colours: all different, and far enough apart to tell at a glance") {
    const auto& c = etude::kProfileColours;
    for (std::size_t i = 0; i < c.size(); ++i) {
        for (std::size_t j = i + 1; j < c.size(); ++j) {
            const auto channel = [&](int shift, std::size_t k) { return static_cast<int>((c[k] >> shift) & 0xFF); };
            const int dr = channel(16, i) - channel(16, j);
            const int dg = channel(8, i) - channel(8, j);
            const int db = channel(0, i) - channel(0, j);
            CAPTURE(i);
            CAPTURE(j);
            CHECK(dr * dr + dg * dg + db * db >= 40 * 40);
        }
    }
}

TEST_CASE("profile chip: as wide as avatar, name and chevron need, between its least and most") {
    for (const float u : kScales) {
        CHECK(profile_chip_width(0.0f, u) == doctest::Approx(pt::kChipMinW * u));
        CHECK(profile_chip_width(40.0f * u, u) >= pt::kChipMinW * u);
        CHECK(profile_chip_width(500.0f * u, u) ==
              doctest::Approx(profile_chip_width(pt::kChipTextMax * u, u)));
        CHECK(profile_chip_width(90.0f * u, u) > profile_chip_width(60.0f * u, u));
    }
}

TEST_CASE("profile panel: every tile and the Edit button are a hit, inside the window, apart") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box display = display_for(s);
            for (const std::size_t n : {1u, 2u, 4u, 5u, 8u, 12u, 15u, 16u}) {
                for (const bool progress : {false, true}) {
                    CAPTURE(s.w);
                    CAPTURE(s.h);
                    CAPTURE(u);
                    CAPTURE(n);
                    const float top = tok::kHit * u;
                    const ProfilePanelPlan p =
                        plan_profile_panel(60.0f * u, top, display, u, n, progress);
                    CHECK(p.tiles == n + (n < etude::kMaxProfiles ? 1 : 0));
                    CHECK(p.add == (n < etude::kMaxProfiles));
                    CHECK(display.holds(p.panel, -kEps));
                    CHECK(p.panel.y0 >= top - kEps);
                    std::vector<Box> boxes{p.edit};
                    // A grid that scrolls shows its first rows; the tiles below are reached by
                    // scrolling, so only the ones inside the grid are checked against the panel.
                    CHECK(p.panel.holds(p.grid, -kEps));
                    for (std::size_t i = 0; i < p.tiles; ++i) {
                        if (!p.scrolls || p.tile[i].y1 <= p.grid.y1 + kEps) {
                            boxes.push_back(p.tile[i]);
                        } else {
                            CHECK(p.tile[i].h() >= tok::kHit * u - kEps);
                        }
                    }
                    if (progress) {
                        boxes.push_back(p.progress);
                    }
                    check_pressables(boxes, p.panel, u);
                }
            }
        }
    }
}

TEST_CASE("profile panel: a big tile holds avatar, name, title and streak with even padding") {
    // The streak row (the flame and the days) ends a padding above the tile's bottom edge, as the
    // avatar starts a padding below its top edge, at every audited size and density.
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box display = display_for(s);
            for (const std::size_t n : {1u, 2u, 4u, 5u}) {
                CAPTURE(s.w);
                CAPTURE(s.h);
                CAPTURE(u);
                CAPTURE(n);
                const ProfilePanelPlan p =
                    plan_profile_panel(60.0f * u, tok::kHit * u, display, u, n, true);
                if (p.flat) {
                    continue;
                }
                const float pad = tok::kS3 * u;
                for (std::size_t i = 0; i < p.tiles; ++i) {
                    const Box& t = p.tile[i];
                    const float streak_bottom = t.y0 + pad + p.avatar + tok::kS2 * u +
                                                pt::kTileNameH * u + pt::kTileTitleH * u +
                                                tok::kS1 * u + pt::kTileStreakH * u;
                    CHECK(streak_bottom + pad == doctest::Approx(t.y1));
                }
            }
        }
    }
}

TEST_CASE("profile panel: big tiles while they fit, flat rows for a big household") {
    const Box display = display_for({1366, 768});
    CHECK_FALSE(plan_profile_panel(60.0f, 44.0f, display, 1.0f, 4, false).flat);
    CHECK(plan_profile_panel(60.0f, 44.0f, display, 1.0f, 4, false).avatar ==
          doctest::Approx(pt::kAvatarBig));
    const Box small = display_for({800, 480});
    CHECK(plan_profile_panel(44.0f, 44.0f, small, 1.0f, 16, true).flat);
    // Four people at 800x480 still get big tiles (the shorter ones).
    const ProfilePanelPlan four = plan_profile_panel(44.0f, 44.0f, small, 1.0f, 4, true);
    CHECK_FALSE(four.flat);
}

TEST_CASE("profile sheet: every control a hit, inside the card, apart, and above the keyboard") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box display = display_for(s);
            for (const bool keys : {false, true}) {
                for (const bool goal : {false, true}) {
                  for (const bool del : {false, true}) {
                    CAPTURE(del);
                    CAPTURE(s.w);
                    CAPTURE(s.h);
                    CAPTURE(u);
                    CAPTURE(keys);
                    CAPTURE(goal);
                    const float top = tok::kHit * u;
                    const float kb = profile_keyboard_height(display.x1, display.y1, u);
                    const float keys_top = keys ? display.y1 - kb : display.y1;
                    const ProfileSheetPlan p =
                        plan_profile_sheet(60.0f * u, top, display, u, keys_top, goal, del);
                    CHECK(display.holds(p.card, -kEps));
                    CHECK(p.card.y1 <= keys_top + kEps);
                    // With the keyboard up the sheet may reach over the top row, but never off the window.
                    CHECK(p.card.y0 >= (p.form == SheetForm::Full ? top : 0.0f) - kEps);
                    std::vector<Box> boxes{p.field, p.cancel, p.save};
                    if (p.swatches) {
                        for (const Box& sw : p.swatch) {
                            boxes.push_back(sw);
                        }
                    }
                    if (p.form == SheetForm::Full) {
                        boxes.push_back(p.back);
                    }
                    if (del && p.form != SheetForm::Tight) {
                        // Delete at the left end of Cancel and Save's row, the Delete gap from Cancel.
                        boxes.push_back(p.del);
                        CHECK(p.del.y0 == doctest::Approx(p.cancel.y0));
                        CHECK(p.del.x0 < p.cancel.x0);
                        CHECK(p.cancel.x0 - p.del.x1 >= profile_tok::kDeleteGap * u - kEps);
                        CHECK(p.save.x0 - p.del.x1 >= 4.0f * tok::kHit * u);
                    } else {
                        CHECK(p.del.w() == 0.0f); // nothing to delete, or the keyboard has the row
                    }
                    // The goal has its own row when there is a Delete in the buttons' row.
                    if (del && p.goal) {
                        CHECK(p.goal_row);
                    }
                    CHECK(p.goal == (goal && p.form != SheetForm::Tight));
                    if (p.goal) {
                        boxes.push_back(p.goal_minus);
                        boxes.push_back(p.goal_plus);
                    }
                    check_pressables(boxes, p.card, u);
                    CHECK(p.avatar.w() == doctest::Approx(p.avatar.h()));
                    CHECK(p.field.x0 >= p.avatar.x1);
                  }
                }
            }
        }
    }
}

TEST_CASE("profile delete confirm: field, Cancel and Delete are a hit each, Cancel and Delete far apart, "
          "inside the window and above the keyboard") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            for (const bool keys : {false, true}) {
                CAPTURE(s.w);
                CAPTURE(s.h);
                CAPTURE(u);
                CAPTURE(keys);
                if (keys && s.h < 600.0f && u > 1.0f) {
                    continue; // a 7 inch panel at a taste scale above 1: no room for 12 mm keys and a card
                }
                const Box display = display_for(s);
                const float kb = profile_keyboard_height(display.x1, display.y1, u);
                const float keys_top = keys ? display.y1 - kb : display.y1;
                const ProfileConfirmPlan p =
                    plan_profile_confirm(60.0f * u, tok::kHit * u, display, u, keys_top);
                CHECK(display.holds(p.card, -kEps));
                CHECK(p.card.y1 <= keys_top + kEps);
                CHECK(p.card.holds(p.field, -kEps));
                CHECK(p.field.h() >= tok::kHit * u - kEps);
                check_pressables({p.field, p.cancel, p.remove}, p.card, u);
                // The buttons are most of the card apart: a tap meant for one cannot land on the other.
                CHECK(p.remove.x0 - p.cancel.x1 >= 3.0f * tok::kHit * u);
                CHECK(p.cancel.x0 < p.card.cx());
                CHECK(p.remove.x1 > p.card.cx());
                // The words stay inside the card, each line apart, above the field's row.
                REQUIRE(p.lines >= 1); // the warning, and the description unless the room is short
                for (int i = 0; i < p.lines; ++i) {
                    const Box& line = p.line[static_cast<std::size_t>(i)];
                    CHECK(p.card.holds(line, -kEps));
                    CHECK(line.y1 <= p.field.y0 + kEps);
                    if (i > 0 && p.form == ConfirmForm::Roomy) { // the tight card's warning shares the heading's row
                        CHECK(line.y0 >= p.line[static_cast<std::size_t>(i - 1)].y1 - kEps);
                    }
                    // Across the card: the line is as wide as the card less its padding.
                    CHECK(line.w() >= p.card.w() - 3.0f * tok::kS3 * u);
                }
                if (p.form == ConfirmForm::Roomy) {
                    CHECK(p.card.holds(p.title, -kEps));
                    CHECK(p.card.holds(p.avatar, -kEps));
                    CHECK(p.field.y1 <= p.cancel.y0 + kEps); // the buttons' row under the field
                    CHECK(p.lines == profile_tok::kConfirmLines);
                } else {
                    CHECK(p.cancel.x1 <= p.field.x0 + kEps); // one row: Cancel, the field, Delete
                    CHECK(p.field.x1 <= p.remove.x0 + kEps);
                }
                // With no keyboard the whole card shows; a window too short for it with the keyboard up
                // gets the tight one.
                if (!keys) {
                    CHECK(p.form == ConfirmForm::Roomy);
                }
            }
        }
    }
    // 800x480: the card keeps its words at over body size, so the tight form still reads at a glance.
    CHECK(profile_tok::kConfirmText > tok::kBody);
}

TEST_CASE("profile delete confirm: at 800x480 with the keyboard up the card and its buttons stay on screen") {
    const Box display = display_for({800, 480});
    const float kb = profile_keyboard_height(display.x1, display.y1, 1.0f);
    const ProfileConfirmPlan p = plan_profile_confirm(60.0f, tok::kHit, display, 1.0f, display.y1 - kb);
    CHECK(p.card.y1 <= display.y1 - kb + kEps);
    CHECK(p.cancel.y1 <= display.y1 - kb + kEps);
    CHECK(p.remove.y1 <= display.y1 - kb + kEps);
    CHECK(p.card.y0 >= -kEps);
}

TEST_CASE("profile delete: the reasons a profile stays are sentences, and a deletable one has none") {
    CHECK(std::string(profile_delete_reason(etude::ProfileResult::Ok)) == "");
    CHECK(std::string(profile_delete_reason(etude::ProfileResult::LastProfile)).find("only") !=
          std::string::npos);
    CHECK(std::string(profile_delete_reason(etude::ProfileResult::FirstProfile)).find("first") !=
          std::string::npos);
}

TEST_CASE("profile sheet: with the keyboard up on a short window it drops its title row") {
    const Box small = display_for({800, 480});
    const float kb = profile_keyboard_height(small.x1, small.y1, 1.0f);
    const ProfileSheetPlan up = plan_profile_sheet(44.0f, tok::kHit, small, 1.0f, small.y1 - kb, true);
    const ProfileSheetPlan down = plan_profile_sheet(44.0f, tok::kHit, small, 1.0f, small.y1, true);
    CHECK(up.compact());
    CHECK_FALSE(down.compact());
    CHECK(up.card.h() < down.card.h());
    // 1366x768 has room for the whole sheet over the keyboard.
    const Box big = display_for({1366, 768});
    const float bkb = profile_keyboard_height(big.x1, big.y1, 1.0f);
    CHECK_FALSE(plan_profile_sheet(60.0f, tok::kHit, big, 1.0f, big.y1 - bkb, true).compact());
}

TEST_CASE("profile sheet: the keyboard spans the window and its keys keep a hit") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box display = display_for(s);
            const float kb = profile_keyboard_height(display.x1, display.y1, u);
            const Box area{0.0f, display.y1 - kb, display.x1, display.y1};
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            if (s.h * 1.0f < 600.0f && u > 1.0f) {
                continue; // a 7 inch panel at a taste scale above 1 has no room for 12 mm keys
            }
            for (const Layer layer : {Layer::Letters, Layer::Symbols}) {
                const auto rows = keyboard_rows(layer);
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    const Box first = keyboard_key(area, u, layer, r, 0);
                    const Box last = keyboard_key(area, u, layer, r, rows[r].count - 1);
                    CHECK(first.x0 == doctest::Approx(osk_tok::kKeyPad * u));
                    CHECK(last.x1 == doctest::Approx(display.x1 - osk_tok::kKeyPad * u));
                    CHECK(first.h() >= tok::kHit * u - kEps);
                }
            }
        }
    }
}

TEST_CASE("profile sheet: the colours share the card's width evenly, at 800, 1366 and 1920") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            for (const SheetForm form : {SheetForm::Full, SheetForm::Compact, SheetForm::Tight}) {
                CAPTURE(s.w);
                CAPTURE(s.h);
                CAPTURE(u);
                const float w = profile_sheet_width(s.w, u);
                const float inner = w - 2.0f * profile_sheet_pad(form, u);
                const SwatchGrid g = profile_swatch_grid(s.w, u, form);
                const int n = static_cast<int>(etude::kProfileColours.size());
                // Every cell a hit area and a gap wide at least, the rows full to within one cell
                // of each other.
                const float gap = tok::kHitGap * u;
                CHECK(g.cell_w >= tok::kCell * u - kEps);
                CHECK(g.per_row * g.rows >= n);
                CHECK((g.rows - 1) * g.per_row < n);
                // The cells of a row fill the inner width exactly (the last one's gap is outside it).
                CHECK(g.cell_w * static_cast<float>(g.per_row) - gap == doctest::Approx(inner));
                // One row when the card holds them all; two or more only when it cannot.
                if (inner + gap >= static_cast<float>(n) * tok::kCell * u - kEps) {
                    CHECK(g.rows == 1);
                } else {
                    CHECK(g.rows >= 2);
                    CHECK(g.cell_w < static_cast<float>(n) * tok::kCell * u); // never shrunk below a hit
                }
            }
        }
    }
    // At 1.0 the 800x480 window's card holds all fourteen, a hit and a gap each, in one row; at 1.5
    // they take two rows.
    CHECK(profile_swatch_grid(800, 1.0f, SheetForm::Full).rows == 1);
    CHECK(profile_swatch_grid(1366, 1.0f, SheetForm::Full).rows == 1);
    CHECK(profile_swatch_grid(1920, 1.0f, SheetForm::Full).rows == 1);
    CHECK(profile_swatch_grid(800, 1.5f, SheetForm::Full).rows == 2);
}

TEST_CASE("profile sheet: the placed colours' cells tile the card's inner width, row by row") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box display = display_for(s);
            const ProfileSheetPlan p = plan_profile_sheet(60.0f * u, tok::kHit * u, display, u, display.y1, false, true);
            if (!p.swatches) {
                continue;
            }
            CAPTURE(s.w);
            CAPTURE(u);
            const float pad = profile_sheet_pad(p.form, u);
            for (int row = 0; row < p.grid.rows; ++row) {
                const std::size_t first = static_cast<std::size_t>(row * p.grid.per_row);
                const std::size_t last =
                    std::min(p.swatch.size(), static_cast<std::size_t>((row + 1) * p.grid.per_row)) - 1;
                CHECK(p.swatch[first].x0 == doctest::Approx(p.card.x0 + pad));
                // Full rows end at the card's inner edge; the last row may stop short, evenly.
                if (row + 1 < p.grid.rows) {
                    CHECK(p.swatch[last].x1 == doctest::Approx(p.card.x1 - pad));
                }
                for (std::size_t i = first; i < last; ++i) {
                    // Evenly, a dead gap between two hit areas.
                    CHECK(p.swatch[i + 1].x0 - p.swatch[i].x1 == doctest::Approx(tok::kHitGap * u));
                }
            }
        }
    }
}
