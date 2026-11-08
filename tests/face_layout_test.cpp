// SPDX-License-Identifier: GPL-3.0-or-later
// Every icon control's face (where its selected, hover and pressed states are drawn) stays inside
// the card or pill it sits on, and every caption fits inside the card, under its own face and
// between its cell's sides, at the supported window sizes and UI scales. The cards and pills are
// the bars' own (app/ui/bar_metrics.hpp), the top row the real layout's (compute_layout), and the
// face and caption the kit's (app/ui/face_layout.hpp).
#include "ui/bar_metrics.hpp"
#include "ui/face_layout.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

using namespace etude;
using namespace etude::app::ui;
namespace bm = etude::app::ui::bar_metrics;

namespace {

struct Size {
    float w;
    float h;
};
constexpr Size kSizes[] = {{800.0f, 480.0f}, {1366.0f, 768.0f}, {1920.0f, 1080.0f}};
constexpr float kScales[] = {1.0f, 1.25f, 1.5f};
constexpr float kEps = 1e-3f;

// The top row of a window with the bar pinned and a song loaded, in points (a headless window has
// one pixel per point; the UI scale makes the screen denser, as the app's --ui-scale does).
RectF top_row(BarStyle style, Size size, float scale) {
    PinnedBar bar;
    bar.pinned = true;
    bar.style = style;
    bar.has_song = true;
    return compute_layout(size.w, size.h, kDefaultPxPerMm * scale, LookSettings{}, bar).top_row;
}

// The kit's unit in that window: core's one scale.
float unit(Size size, float scale) { return ui_scale(size.w, size.h, kDefaultPxPerMm * scale); }

// A caption's width at `tok::kCaption`, from the widest average advance (wider than the real font,
// so the check is on the safe side).
float caption_width(std::string_view text, float u) {
    int chars = 0;
    for (const char c : text) {
        chars += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0; // UTF-8 lead bytes
    }
    return static_cast<float>(chars) * kLabelCharWidthEm * tok::kCaption * u;
}

// The groups bar's controls: the cell and the face each asks for, and its caption.
struct Control {
    const char* name;
    float cell;   // design points
    float face_w; // asked for
    float face_h;
    const char* caption;
};

Control groups_control(bm::Ctl c) {
    const bm::StyleCells& cells = bm::kGroups;
    const float cell = bm::cell_of(c, cells, BarForm::Full, {true, true});
    float face = bm::kGroupsFace;
    switch (c) {
    case bm::Ctl::Replay:
    case bm::Ctl::Back:
    case bm::Ctl::Play:
    case bm::Ctl::Forward:
    case bm::Ctl::Sound:
    case bm::Ctl::LeftHand:
    case bm::Ctl::RightHand:
        face = bm::kGroupsTransportFace;
        break;
    default:
        break;
    }
    // The tempo chip asks for its whole cell; like every face it is kept kFaceInset inside it.
    const float face_w = c == bm::Ctl::Tempo ? cell : face;
    return {bm::name_of(c), cell, face_w, face, bm::caption_of(c)};
}

std::string where(const char* bar, Size size, float scale, const char* control,
                  std::string_view caption) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s %.0fx%.0f x%.2f %s \"%.*s\"", bar, size.w, size.h,
                  static_cast<double>(scale), control, static_cast<int>(caption.size()),
                  caption.data());
    return buf;
}

} // namespace

TEST_CASE("groups bar: every face and caption stays inside its group's card, captions under "
          "their face and inside their cell") {
    for (const Size size : kSizes) {
        for (const float scale : kScales) {
            const float u = unit(size, scale);
            const RectF row = top_row(BarStyle::Groups, size, scale);
            REQUIRE(row.h > 0.0f);
            const float card_h = row.h - bm::kGroupsCardInset * u;
            const float card_y = row.y + (row.h - card_h) * 0.5f;
            for (int i = 0; i <= bm::kCtlCount; ++i) {
                // Every control, and the Flow/Wait chip once more with its other state's caption.
                const bool wait = i == bm::kCtlCount;
                const Control c = groups_control(wait ? bm::Ctl::Mode : static_cast<bm::Ctl>(i));
                {
                    const char* const text = wait ? bm::mode_caption(true) : c.caption;
                    CAPTURE(where("groups", size, u, c.name, text));
                    const Box area{0.0f, card_y, c.cell * u, card_y + card_h};
                    const float w = caption_width(text, u);
                    const float room = area.w() - 2.0f * kFaceInset * u;
                    const float cs = fit_caption_size(tok::kCaption * u, w, room, u);
                    const FaceLayout f =
                        lay_face(area, area.cx(), c.face_w * u, c.face_h * u, tok::kIcon * u,
                                 cs > 0.0f ? w * cs / (tok::kCaption * u) : 0.0f, cs, u);
                    // The face, with every state drawn on it, clear of the card's edge.
                    CHECK(area.holds(f.face, kFaceInset * u - kEps));
                    // Every caption is shown at these sizes, inside the card, under the face and
                    // between the cell's sides.
                    REQUIRE(f.caption);
                    CHECK(area.holds(f.caption_box, kFaceInset * u - kEps));
                    CHECK(f.caption_box.y0 >= f.face.y1 - kEps);
                    // The icon still fits its face.
                    CHECK(f.face.h() >= tok::kIcon * u + kFaceMinOverIcon * u - kEps);
                }
            }
        }
    }
}

TEST_CASE("lay_face: an uncaptioned face is never smaller than its icon and its inset") {
    const float u = 1.0f;
    const float icon = 20.0f;
    const float least = icon + 2.0f * kFaceInset;
    // Exactly room for it: a face of its own, inset.
    const Box fits{0.0f, 0.0f, 44.0f, least + 2.0f * kFaceInset};
    const FaceLayout a = lay_face(fits, 22.0f, 36.0f, 36.0f, icon, 0.0f, 0.0f, u);
    CHECK_FALSE(a.band);
    CHECK(a.face.h() == doctest::Approx(least));
    // A hair less: the area's band.
    const Box short_area{0.0f, 0.0f, 44.0f, least + 2.0f * kFaceInset - 0.01f};
    const FaceLayout b = lay_face(short_area, 22.0f, 36.0f, 36.0f, icon, 0.0f, 0.0f, u);
    CHECK(b.band);
    CHECK_FALSE(b.hidden);
    CHECK(b.face.h() == doctest::Approx(short_area.h()));
    CHECK(b.face.w() == doctest::Approx(36.0f));
    // As tall as the icon: still shown; a hair shorter: hidden.
    CHECK_FALSE(lay_face({0.0f, 0.0f, 44.0f, icon}, 22.0f, 36.0f, 36.0f, icon, 0.0f, 0.0f, u).hidden);
    CHECK(lay_face({0.0f, 0.0f, 44.0f, icon - 0.01f}, 22.0f, 36.0f, 36.0f, icon, 0.0f, 0.0f, u)
              .hidden);
}

TEST_CASE("dock: every face stays inside the dock") {
    for (const Size size : kSizes) {
        for (const float scale : kScales) {
            const float u = unit(size, scale);
            const RectF row = top_row(BarStyle::Dock, size, scale);
            const float dock_h = bm::dock_height(row.h / u) * u;
            const float dock_y = row.y + (row.h - dock_h) * 0.5f;
            const Box area{0.0f, dock_y, bm::kDockCell * u, dock_y + dock_h};
            // As bar_dock.cpp asks: the kit's face, or less where the dock is shorter.
            const float asked = std::min(tok::kFace * u, dock_h - bm::kDockFaceInset * u);
            CAPTURE(where("dock", size, u, "button", ""));
            const FaceLayout f =
                lay_face(area, area.cx(), asked, asked, tok::kIcon * u, 0.0f, 0.0f, u);
            CHECK(area.holds(f.face, kFaceInset * u - kEps));
            CHECK(f.face.h() == doctest::Approx(asked)); // the dock's faces have room as asked
        }
    }
}

TEST_CASE("lay_face: a caption that cannot fit is dropped, never crossed") {
    const float u = 1.0f;
    // Room for the face alone: the caption goes and the face is centred.
    const Box short_area{0.0f, 0.0f, 48.0f, 40.0f};
    const FaceLayout a = lay_face(short_area, 24.0f, 32.0f, 32.0f, 20.0f, 30.0f, 12.0f, u);
    CHECK_FALSE(a.caption);
    CHECK(short_area.holds(a.face, kFaceInset - kEps));
    CHECK(a.face.cy() == doctest::Approx(short_area.cy()));
    // Exactly enough for the smallest face with its caption: kept.
    const float just = 2.0f * kFaceInset + 20.0f + kFaceMinOverIcon + kCaptionGap + 12.0f;
    const FaceLayout b =
        lay_face({0.0f, 0.0f, 48.0f, just}, 24.0f, 32.0f, 32.0f, 20.0f, 30.0f, 12.0f, u);
    CHECK(b.caption);
    CHECK(b.face.h() == doctest::Approx(24.0f));
    // A hair less: dropped.
    const FaceLayout c =
        lay_face({0.0f, 0.0f, 48.0f, just - 0.01f}, 24.0f, 32.0f, 32.0f, 20.0f, 30.0f, 12.0f, u);
    CHECK_FALSE(c.caption);
    // A caption wider than the cell is dropped, never run over its sides.
    const FaceLayout d =
        lay_face({0.0f, 0.0f, 40.0f, 60.0f}, 20.0f, 28.0f, 28.0f, 20.0f, 35.0f, 12.0f, u);
    CHECK_FALSE(d.caption);
    // Exactly as wide as the cell less its insets: kept; a point wider: dropped.
    const float room = 40.0f - 2.0f * kFaceInset;
    CHECK(lay_face({0.0f, 0.0f, 40.0f, 60.0f}, 20.0f, 28.0f, 28.0f, 20.0f, room, 12.0f, u).caption);
    CHECK_FALSE(
        lay_face({0.0f, 0.0f, 40.0f, 60.0f}, 20.0f, 28.0f, 28.0f, 20.0f, room + 1.0f, 12.0f, u)
            .caption);
}

TEST_CASE("fit_caption_size: shrinks to fit, down to the smallest caption, then drops it") {
    CHECK(fit_caption_size(12.0f, 40.0f, 40.0f, 1.0f) == 12.0f);
    CHECK(fit_caption_size(12.0f, 48.0f, 40.0f, 1.0f) == doctest::Approx(10.0f));
    CHECK(fit_caption_size(12.0f, 48.1f, 40.0f, 1.0f) == 0.0f);
    // A point over the room: drawn smaller, in proportion.
    CHECK(fit_caption_size(12.0f, 41.0f, 40.0f, 1.0f) == doctest::Approx(12.0f * 40.0f / 41.0f));
    CHECK(fit_caption_size(18.0f, 72.0f, 60.0f, 1.5f) == doctest::Approx(15.0f));
}

TEST_CASE("focus_ring_plan: the ring stays inside the face, a face too thin for it is filled") {
    // A face with room: the stroke's middle is a hairline and half the ring in, as always.
    const FocusRing roomy = focus_ring_plan(60.0f, 40.0f, 4.0f, 2.0f);
    CHECK_FALSE(roomy.fill);
    CHECK(roomy.inset == doctest::Approx(4.0f));
    // A thin section of the band (a 32-section song at 800x480 x1.5: faces 6 px wide, ring 4 px):
    // the inset never passes the middle, so the stroke (ring/2 each side of it) stays inside.
    const FocusRing thin = focus_ring_plan(6.0f, 60.0f, 4.0f, 2.0f);
    CHECK_FALSE(thin.fill);
    CHECK(thin.inset == doctest::Approx(3.0f));
    CHECK(thin.inset - 4.0f * 0.5f >= 0.0f);     // the stroke's outer edge is at the face's edge
    CHECK(thin.inset + 4.0f * 0.5f <= 6.0f);     // the far edge inside the face
    // Narrower than the ring: filled whole, no stroke.
    const FocusRing sliver = focus_ring_plan(3.0f, 60.0f, 4.0f, 2.0f);
    CHECK(sliver.fill);
    // The height counts the same.
    CHECK(focus_ring_plan(60.0f, 3.0f, 4.0f, 2.0f).fill);
    for (const float w : {4.0f, 5.0f, 6.0f, 8.0f, 9.5f, 30.0f}) {
        const FocusRing r = focus_ring_plan(w, 64.0f, 4.0f, 2.0f);
        CAPTURE(w);
        if (!r.fill) {
            CHECK(r.inset - 2.0f >= 0.0f);
            CHECK(r.inset + 2.0f <= w);
        }
    }
}
