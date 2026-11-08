// SPDX-License-Identifier: GPL-3.0-or-later
// The Add songs page's geometry (app/ui/add_songs_layout.hpp) in the page body the sheet really
// gives it: the switch sits beside the words, the code is a big share of the card, everything a
// finger may touch is a full touch target, and nothing leaves the panel at 800x480 and 1366x768.
#include "ui/add_songs_layout.hpp"
#include "ui/sheet_layout.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct Size {
    float w;
    float h;
};
constexpr float kEps = 1e-3f;

// The page body as the sheet gives it in a window `size` at `--ui-scale` `scale`.
struct Body {
    float u = 1.0f;
    float w = 0.0f;
    float h = 0.0f;
};

Body body_for(Size size, float scale) {
    Body b;
    b.u = ui_scale(size.w, size.h, kDefaultPxPerMm * scale);
    const ScreenLayout lay =
        compute_layout(size.w, size.h, kDefaultPxPerMm * scale, LookSettings{});
    const SheetFrame sheet =
        sheet_frame(size.w, size.h, lay.top_row.bottom() + tok::kS2 * b.u, b.u);
    const RailLayout rail = rail_layout(sheet.w, sheet.h, kRailPages, b.u);
    const PageArea area = page_area(sheet, rail, b.u);
    b.w = area.w;
    b.h = area.body_h;
    return b;
}

std::string where(Size size, float scale) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0fx%.0f x%.2f", static_cast<double>(size.w),
                  static_cast<double>(size.h), static_cast<double>(scale));
    return buf;
}

bool inside(const Rect& r, const Rect& outer) {
    return r.x >= outer.x - kEps && r.y >= outer.y - kEps && r.right() <= outer.right() + kEps &&
           r.bottom() <= outer.bottom() + kEps;
}

bool apart(const Rect& a, const Rect& b) {
    return a.right() <= b.x + kEps || b.right() <= a.x + kEps || a.bottom() <= b.y + kEps ||
           b.bottom() <= a.y + kEps;
}

} // namespace

TEST_CASE("add songs page: the switch sits right after the words, as a full touch target") {
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        const ToggleRowGeometry g = toggle_row_geometry(u);
        CAPTURE(u);
        // Beside the words: nothing but one gap between the text column and the switch.
        CHECK(g.toggle.x - g.text.right() <= tok::kS4 * u + kEps);
        CHECK(g.toggle.x >= g.text.right());
        // The switch and the row it is in are touch targets on both sides.
        CHECK(g.toggle.w >= tok::kHit * u - kEps);
        CHECK(g.toggle.h >= tok::kHit * u - kEps);
        CHECK(g.row.h >= tok::kHit * u - kEps);
        CHECK(g.track.h >= tok::kFace * u - kEps);
        CHECK(inside(g.track, g.toggle));
        CHECK(inside(g.toggle, g.row));
        CHECK(inside(g.icon, g.row));
        CHECK(inside(g.text, g.row));
        CHECK(apart(g.icon, g.text));
        CHECK(apart(g.text, g.toggle));
        // And the whole cluster is far narrower than the page: it is not at the far edge.
        CHECK(g.row.w < 400.0f * u);
    }
}

TEST_CASE("add songs page: the card fills the width, the code is a big share of it, nothing leaves "
          "the panel") {
    for (const Size size : {Size{800.0f, 480.0f}, Size{1366.0f, 768.0f}, Size{1920.0f, 1080.0f}}) {
        const Body b = body_for(size, 1.0f);
        CAPTURE(where(size, 1.0f));
        REQUIRE(b.w > 0.0f);
        const AddSongsGeometry g = add_songs_geometry(b.w, b.h, b.u, true);
        const Rect page{0.0f, 0.0f, b.w, b.h};
        REQUIRE(g.card_shown);
        CHECK_FALSE(g.stacked);
        CHECK(g.card.x == 0.0f);
        CHECK(g.card.w == doctest::Approx(b.w));
        // The code: square, 30% of the card at the smallest window (less when the page's height
        // leaves the code no more: its own height less the card's padding and the rows around it),
        // 40% where the height allows.
        CHECK(g.qr.w == doctest::Approx(g.qr.h));
        const float height_room = b.h - g.head.row.h - 2.0f * tok::kS2 * b.u -
                                  phone_tok::kTileH * b.u - 2.0f * tok::kS4 * b.u;
        CHECK(g.qr.w >= std::min(0.30f * g.card.w, height_room) - kEps);
        if (size.w >= 1366.0f) {
            CHECK(g.qr.w >= 0.39f * g.card.w);
        }
        CHECK(g.qr.w >= phone_tok::kQrMin * b.u);
        // The right side as tall as the code, the code on the left, the text on the right of it.
        CHECK(g.info.name.x >= g.qr.right());
        CHECK(g.info.status.bottom() <= g.qr.bottom() + kEps);
        CHECK(g.info.name.y >= g.qr.y - kEps);
        // Everything is inside the card, and the card inside the page.
        for (const Rect* r :
             {&g.qr, &g.info.name, &g.info.number, &g.info.pin_label, &g.info.status}) {
            CHECK(inside(*r, g.card));
        }
        for (const Rect& pin : g.info.pin) {
            CHECK(inside(pin, g.card));
        }
        // Top to bottom, none over another.
        CHECK(g.info.name.bottom() <= g.info.number.y + kEps);
        CHECK(g.info.number.bottom() <= g.info.pin[0].y + kEps);
        CHECK(g.info.pin[0].bottom() <= g.info.status.y + kEps);
        for (int i = 0; i < 4; ++i) {
            // The PIN's boxes are well past the touch minimum, and the digits are display size.
            CHECK(g.info.pin[i].w >= tok::kHit * b.u);
            CHECK(g.info.pin[i].h >= (tok::kHit + tok::kS2) * b.u - kEps);
            if (i > 0) {
                CHECK(apart(g.info.pin[i - 1], g.info.pin[i]));
            }
        }
        CHECK(g.info.name_size >= tok::kTitle * b.u - kEps);
        CHECK(g.info.status.h >= tok::kHit * b.u - kEps);
        // The tiles: side by side under the card, each a touch target, together as wide as it.
        CHECK(g.tiles[0].h >= tok::kHit * b.u - kEps);
        CHECK(g.tiles[1].h >= tok::kHit * b.u - kEps);
        CHECK(g.tiles[0].y == doctest::Approx(g.tiles[1].y));
        CHECK(g.tiles[0].y >= g.card.bottom());
        CHECK(apart(g.tiles[0], g.tiles[1]));
        CHECK(g.tiles[0].x == 0.0f);
        CHECK(g.tiles[1].right() == doctest::Approx(b.w));
        CHECK(apart(g.head.row, g.card));
        // No scrolling: all of it is inside the page body, and no right half is left empty.
        CHECK(g.height <= b.h + kEps);
        CHECK(inside(g.tiles[0], page));
        CHECK(inside(g.tiles[1], page));
        CHECK(inside(g.card, page));
        CHECK(g.info.pin[3].right() >= 0.85f * g.card.right());
    }
}

TEST_CASE("add songs page: without the card the tiles follow the hint, inside the page") {
    for (const Size size : {Size{800.0f, 480.0f}, Size{1366.0f, 768.0f}}) {
        const Body b = body_for(size, 1.0f);
        CAPTURE(where(size, 1.0f));
        const float hint = 3.0f * 20.0f * b.u;
        const AddSongsGeometry g = add_songs_geometry(b.w, b.h, b.u, false, hint);
        CHECK_FALSE(g.card_shown);
        CHECK(g.tiles[0].y >= g.head.row.bottom() + hint - kEps);
        CHECK(g.height <= b.h + kEps);
        CHECK(g.tiles[0].h >= tok::kHit * b.u - kEps);
    }
}

TEST_CASE("add songs page: at other scales and in a narrow page every touch target holds and "
          "nothing leaves the width") {
    for (const Size size : {Size{800.0f, 480.0f}, Size{1024.0f, 600.0f}, Size{1920.0f, 1080.0f}}) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            const Body b = body_for(size, scale);
            CAPTURE(where(size, scale));
            const AddSongsGeometry g = add_songs_geometry(b.w, b.h, b.u, true);
            CHECK(g.qr.w >= phone_tok::kQrMin * b.u - kEps);
            CHECK(g.info.status.h >= tok::kHit * b.u - kEps);
            for (const Rect& pin : g.info.pin) {
                CHECK(pin.h >= tok::kHit * b.u);
                CHECK(pin.right() <= g.card.right() + kEps);
            }
            for (const Rect& tile : g.tiles) {
                CHECK(tile.h >= tok::kHit * b.u - kEps);
                CHECK(tile.right() <= b.w + kEps);
            }
            CHECK(g.card.right() <= b.w + kEps);
        }
    }
    // A card too narrow for both sides puts the code above the text, still inside the card.
    const AddSongsGeometry narrow = add_songs_geometry(300.0f, 700.0f, 1.0f, true);
    CHECK(narrow.stacked);
    CHECK(inside(narrow.qr, narrow.card));
    CHECK(narrow.info.name.y >= narrow.qr.bottom());
    CHECK(inside(narrow.info.status, narrow.card));
}

TEST_CASE("add songs page: the views over the page hold their touch targets inside the body") {
    for (const Size size : {Size{800.0f, 480.0f}, Size{1366.0f, 768.0f}, Size{1920.0f, 1080.0f}}) {
        for (const float scale : {1.0f, 1.5f}) {
            const Body b = body_for(size, scale);
            CAPTURE(where(size, scale));
            const SubviewGeometry g = subview_geometry(b.w, b.h, b.u, true);
            const Rect body{0.0f, 0.0f, b.w, b.h};
            for (const Rect& r : {g.back, g.title, g.action, g.body}) {
                CHECK(inside(r, body));
            }
            CHECK(g.back.h >= tok::kHit * b.u - kEps);
            CHECK(g.back.w >= tok::kHit * b.u - kEps);
            CHECK(g.action.h >= tok::kHit * b.u - kEps);
            CHECK(g.action.w >= tok::kHit * b.u - kEps);
            CHECK(apart(g.back, g.title));
            CHECK(apart(g.title, g.action));
            CHECK(apart(g.back, g.action));
            CHECK(g.body.y >= g.back.bottom() - kEps);
            CHECK(g.body.bottom() <= b.h + kEps);
            // A row of the list: the button is a full touch target, the words do not run into it.
            const UsbRowGeometry row = usb_row_geometry(g.body.w, 0.0f, b.u);
            CHECK(row.button.w >= tok::kHit * b.u - kEps);
            CHECK(row.button.h >= tok::kHit * b.u - kEps);
            CHECK(row.step >= row.button.h - kEps);
            CHECK(inside(row.button, {0.0f, 0.0f, g.body.w, row.step}));
            CHECK(apart(row.name, row.size));
            CHECK(apart(row.size, row.button));
            CHECK(row.name.w > 0.0f);
        }
    }
    // Without an action the title takes the width.
    const SubviewGeometry plain = subview_geometry(600.0f, 400.0f, 1.0f, false);
    CHECK(plain.action.w == 0.0f);
    CHECK(plain.title.right() <= 600.0f + kEps);
}
