// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/editor_layout.hpp"

#include "section_fixtures.hpp"
#include "ui/face_layout.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <vector>

using namespace etude;
using namespace etude::testing;

namespace {

bool overlap(const RectF& a, const RectF& b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    return a.x < b.right() - 1e-3f && b.x < a.right() - 1e-3f && a.y < b.bottom() - 1e-3f &&
           b.y < a.bottom() - 1e-3f;
}

bool inside(const RectF& r, const RectF& outer) {
    return r.x >= outer.x - 1e-3f && r.y >= outer.y - 1e-3f && r.right() <= outer.right() + 1e-3f &&
           r.bottom() <= outer.bottom() + 1e-3f;
}

struct Size {
    float w;
    float h;
};

// Window sizes from the smallest supported up to the wide ones: 5:3, 16:10, 16:9, 4:3, 21:9, 32:9.
constexpr std::array<Size, 9> kWindows{{{800, 480},
                                        {1024, 600},
                                        {1280, 720},
                                        {1280, 800},
                                        {1024, 768},
                                        {1920, 1080},
                                        {2560, 1080},
                                        {3440, 1440},
                                        {3840, 1080}}};

// The editor's area: under a top row of about 15 mm, a margin around.
RectF area_for(const Size& s, float u) {
    const float top = 44.0f * u + 8.0f;
    const float margin = 12.0f * u;
    return {margin, top, s.w - 2.0f * margin, s.h - top - margin};
}

} // namespace

TEST_CASE("the editor's regions tile its area without overlap, from 800x480 to ultra-wide") {
    for (const Size& s : kWindows) {
        for (const float u : {0.75f, 1.0f, 1.5f}) {
            // A larger scale means more points per design point; the window shrinks to match.
            const Size win{s.w / u, s.h / u};
            if (win.w < 800.0f / 1.0f && u > 1.0f) {
                continue; // below the supported size in points
            }
            const EditorLayout l = editor_layout(area_for(win, 1.0f), 1.0f);
            INFO(s.w << "x" << s.h << " u " << u);
            const std::array<RectF, 6> rows{l.toolbar, l.ruler, l.roll, l.bands, l.loupe,
                                            l.actions};
            for (std::size_t i = 0; i < rows.size(); ++i) {
                CHECK(inside(rows[i], l.area));
                CHECK_FALSE(rows[i].empty());
                for (std::size_t j = i + 1; j < rows.size(); ++j) {
                    CHECK_FALSE(overlap(rows[i], rows[j]));
                }
            }
            CHECK(l.fits);
            CHECK(l.roll.h >= kEditorMinRoll);
        }
    }
}

TEST_CASE("at 800x480 the note area keeps a usable height and the loupe its full size") {
    const EditorLayout l = editor_layout(area_for({800, 480}, 1.0f), 1.0f);
    CHECK(l.fits);
    CHECK(l.roll.h >= 120.0f);
    CHECK(l.loupe.h == doctest::Approx(kEditorLoupe));
    CHECK(l.actions.bottom() <= 480.0f);
}

TEST_CASE("a short area shrinks the loupe before the note area, and reports when it cannot fit") {
    const EditorLayout tight = editor_layout({0, 0, 800, 320}, 1.0f);
    CHECK(tight.loupe.h < kEditorLoupe);
    CHECK(tight.loupe.h >= kEditorLoupeMin);
    CHECK(tight.fits);
    const EditorLayout hopeless = editor_layout({0, 0, 800, 200}, 1.0f);
    CHECK_FALSE(hopeless.fits);
    CHECK(hopeless.roll.h >= 0.0f);
}

TEST_CASE("the rows follow one another in order, gaps between loupe and neighbours") {
    const EditorLayout l = editor_layout({10, 50, 780, 400}, 1.0f);
    CHECK(l.toolbar.y == 50.0f);
    CHECK(l.ruler.y >= l.toolbar.bottom());
    CHECK(l.roll.y == doctest::Approx(l.ruler.bottom()));
    CHECK(l.bands.y == doctest::Approx(l.roll.bottom()));
    CHECK(l.loupe.y > l.bands.bottom());
    CHECK(l.actions.y > l.loupe.bottom());
    CHECK(l.actions.bottom() == doctest::Approx(450.0f));
}

TEST_CASE("toolbar buttons are touch-sized and do not overlap") {
    for (const Size& s : kWindows) {
        const EditorLayout l = editor_layout(area_for(s, 1.0f), 1.0f);
        const ToolbarSlots t = toolbar_slots(l.toolbar, 1.0f);
        const std::array<RectF, 7> slots{t.close, t.title, t.fit, t.zoom_out,
                                         t.zoom_in, t.undo, t.suggest};
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i].empty()) {
                CHECK(inside(slots[i], l.toolbar));
            }
            if (i != 1) {
                CHECK(slots[i].w >= kEditorHit);
                CHECK(slots[i].h >= kEditorHit);
            }
            for (std::size_t j = i + 1; j < slots.size(); ++j) {
                CHECK_FALSE(overlap(slots[i], slots[j]));
            }
        }
        CHECK(t.title.w > 0.0f);
    }
}

TEST_CASE("action buttons are touch-sized and do not overlap") {
    for (const Size& s : kWindows) {
        const EditorLayout l = editor_layout(area_for(s, 1.0f), 1.0f);
        const ActionSlots a = action_slots(l.actions, 1.0f);
        const std::array<RectF, 7> slots{a.split,       a.play,       a.play_across, a.band_chip,
                                         a.band_rename, a.band_merge, a.band_delete};
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i].empty()) {
                CHECK(inside(slots[i], l.actions));
                CHECK(slots[i].h >= kEditorHit);
            }
            if (i != 3) {
                CHECK(slots[i].w >= kEditorHit);
            }
            for (std::size_t j = i + 1; j < slots.size(); ++j) {
                CHECK_FALSE(overlap(slots[i], slots[j]));
            }
        }
        CHECK(a.band_chip.w > 0.0f);  // the band's name fits at every supported size
    }
}

TEST_CASE("close is the toolbar's last button, in the top right corner like the settings sheet's") {
    for (const Size& s : kWindows) {
        for (const float u : {1.0f, 1.25f, 1.5f}) {
            const EditorLayout l = editor_layout(area_for(s, u), u);
            const ToolbarSlots t = toolbar_slots(l.toolbar, u);
            // The close button ends where the toolbar does, and no other button is right of it.
            CHECK(t.close.right() == doctest::Approx(l.toolbar.right()));
            for (const RectF& other : {t.fit, t.zoom_out, t.zoom_in, t.undo, t.suggest}) {
                CHECK(other.right() <= t.close.x + 1e-3f);
            }
            // The tools keep a gap from it wider than the one between the tools.
            CHECK(t.close.x - t.suggest.right() >= (kEditorCloseGap + 2.0f) * u - 1e-3f);
            // The title takes the left, from the toolbar's own edge.
            CHECK(t.title.x < t.fit.x);
            CHECK(t.title.x <= l.toolbar.x + 4.0f * u + 1e-3f);
            CHECK(t.close.w >= kEditorHit * u - 1e-3f);
        }
    }
}

TEST_CASE("Delete keeps the destructive gap from every other action, in every window and scale") {
    for (const Size& s : kWindows) {
        for (const float u : {1.0f, 1.25f, 1.5f}) {
            const EditorLayout l = editor_layout(area_for(s, u), u);
            const ActionSlots a = action_slots(l.actions, u);
            CAPTURE(s.w);
            CAPTURE(u);
            // Merge is the button beside it; the rest are further.
            CHECK(a.band_delete.x - a.band_merge.right() >= kDestructiveGap * u - 1e-3f);
            for (const RectF& other : {a.split, a.play, a.play_across, a.band_chip, a.band_rename,
                                       a.band_merge}) {
                if (!other.empty()) {
                    CHECK(a.band_delete.x - other.right() >= kDestructiveGap * u - 1e-3f);
                }
            }
            CHECK(inside(a.band_delete, l.actions));
            CHECK(a.band_delete.right() <= l.actions.right() + 1e-3f);
        }
    }
    // Icon-only too: the gap does not depend on the captions.
    const ActionSlots narrow = action_slots({0.0f, 0.0f, 400.0f, 52.0f}, 1.0f);
    CHECK_FALSE(narrow.captions);
    CHECK(narrow.band_delete.x - narrow.band_merge.right() >= kDestructiveGap - 1e-3f);
}

TEST_CASE("the action bar has one Play, between Split and Across") {
    for (const Size& s : kWindows) {
        const EditorLayout l = editor_layout(area_for(s, 1.0f), 1.0f);
        const ActionSlots a = action_slots(l.actions, 1.0f);
        CHECK(a.split.right() <= a.play.x);
        CHECK(a.play.right() <= a.play_across.x);
        // Nothing of the band's own bar is a second Play: the chip, then rename, join, delete.
        CHECK(a.band_chip.right() <= a.band_rename.x + 1e-3f);
        CHECK(a.band_rename.right() <= a.band_merge.x + 1e-3f);
        CHECK(a.band_merge.right() <= a.band_delete.x + 1e-3f);
    }
}

TEST_CASE("a captioned action keeps a face of at least its icon and 10 pt") {
    // The caption under a face takes its height out of the face. The real face is `tok::kFace`
    // asked for in the real caption line (`tok::kCaption`, the line is its size in the kit's font):
    // in a 48 pt bar it was 26 pt (the icon and 6), in 52 pt it is 30 (the icon and 10), as the
    // kit's own `plan_face` measured it.
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        const EditorLayout l = editor_layout(area_for({800, 480}, u), u);
        const ActionSlots a = action_slots(l.actions, u);
        REQUIRE(a.captions);
        for (const RectF& slot :
             {a.split, a.play, a.play_across, a.band_rename, a.band_merge, a.band_delete}) {
            const etude::app::ui::Box area{slot.x, slot.y, slot.right(), slot.bottom()};
            const etude::app::ui::FaceLayout f = etude::app::ui::lay_face(
                area, slot.x + slot.w * 0.5f, etude::app::ui::tok::kFace * u,
                etude::app::ui::tok::kFace * u, etude::app::ui::tok::kIcon * u, 36.0f * u,
                etude::app::ui::tok::kCaption * u, u);
            CHECK(f.caption);
            CHECK(f.face.h() >= (etude::app::ui::tok::kIcon + 10.0f) * u - 1e-3f);
            CHECK(f.face.w() == doctest::Approx(f.face.h())); // the face stays square
        }
    }
}

TEST_CASE("the bars of the layout scale with the density") {
    const EditorLayout one = editor_layout({0, 0, 800, 600}, 1.0f);
    const EditorLayout two = editor_layout({0, 0, 1600, 1200}, 2.0f);
    CHECK(two.toolbar.h == doctest::Approx(one.toolbar.h * 2.0f));
    CHECK(two.loupe.h == doctest::Approx(one.loupe.h * 2.0f));
    CHECK(two.roll.h == doctest::Approx(one.roll.h * 2.0f));
}

namespace {

struct EditorFixture {
    Song song = bars_song(16);  // 32 s
    SongSections sections;
    EditorLayout layout = editor_layout({0, 0, 800, 420}, 1.0f);
    RollView view = whole_song_view(song);
    std::vector<float> name_w;

    EditorFixture() {
        sections.song_hash = 1;
        REQUIRE(sections.add_marker(song, 8.0).ok);
        REQUIRE(sections.add_marker(song, 16.0).ok);
        name_w.assign(sections.sections.size(), 60.0f);
    }
    float x(double t) const { return editor_x(layout, view, t); }
    EditorHit hit(float px, float py, double cursor = 4.0, bool cursor_visible = true) const {
        return editor_hit(layout, view, sections, cursor, cursor_visible, px, py, 22.0f,
                          name_w.data(), name_w.size());
    }
    float roll_y() const { return layout.roll.y + layout.roll.h * 0.5f; }
    float band_y() const { return layout.bands.y + layout.bands.h * 0.5f; }
};

} // namespace

TEST_CASE("the editor's time axis maps both ways") {
    EditorFixture f;
    CHECK(f.x(0.0) == doctest::Approx(0.0f));
    CHECK(f.x(32.0) == doctest::Approx(800.0f));
    CHECK(editor_time(f.layout, f.view, f.x(13.0)) == doctest::Approx(13.0));
    f.view = RollView{8.0, 16.0};
    CHECK(f.x(8.0) == doctest::Approx(0.0f));
    CHECK(f.x(12.0) == doctest::Approx(400.0f));
    CHECK(editor_time(f.layout, f.view, 200.0f) == doctest::Approx(10.0));
}

TEST_CASE("a boundary handle wins a tie with the cursor, and the bands") {
    EditorFixture f;
    // Boundary at 8 s; the cursor is placed on it too.
    EditorHit h = f.hit(f.x(8.0), f.roll_y(), 8.0);
    CHECK(h.target == EditorTarget::Handle);
    CHECK(h.handle == 0);
    // The foot of the handle in the band row works as well.
    h = f.hit(f.x(8.0) - 15.0f, f.band_y());
    CHECK(h.target == EditorTarget::Handle);
    // Just outside the reach it is the band again.
    h = f.hit(f.x(8.0) + 30.0f, f.band_y());
    CHECK(h.target != EditorTarget::Handle);
}

TEST_CASE("when the grab zones of a handle and the cursor overlap the nearer one wins") {
    EditorFixture f;
    // Cursor 20 points right of the handle at 8 s; a touch 14 points from the handle is 6 from the
    // cursor: the cursor, though the handle is first in draw order.
    const double cursor = editor_time(f.layout, f.view, f.x(8.0) + 20.0f);
    EditorHit h = f.hit(f.x(8.0) + 14.0f, f.roll_y(), cursor);
    CHECK(h.target == EditorTarget::Cursor);
    // Nearer the handle: the handle.
    h = f.hit(f.x(8.0) + 4.0f, f.roll_y(), cursor);
    CHECK(h.target == EditorTarget::Handle);
}

TEST_CASE("the grab zone is about 15 mm wide, much more than the thin line drawn") {
    CHECK(kEditorGrabReach * 2.0f >= 55.0f);
    EditorFixture f;
    EditorHit h = f.hit(f.x(8.0) + 27.0f, f.roll_y(), 4.0, false);
    // hit() uses a 22-point reach of its own; ask for the real one.
    h = editor_hit(f.layout, f.view, f.sections, 4.0, false, f.x(8.0) + 27.0f, f.roll_y(),
                   kEditorGrabReach, f.name_w.data(), f.name_w.size());
    CHECK(h.target == EditorTarget::Handle);
    h = editor_hit(f.layout, f.view, f.sections, 4.0, false, f.x(8.0) + 29.0f, f.roll_y(),
                   kEditorGrabReach, f.name_w.data(), f.name_w.size());
    CHECK(h.target != EditorTarget::Handle);
}

TEST_CASE("the cursor is grabbed in the roll within its reach") {
    EditorFixture f;
    EditorHit h = f.hit(f.x(4.0) + 20.0f, f.roll_y(), 4.0);
    CHECK(h.target == EditorTarget::Cursor);
    h = f.hit(f.x(4.0) + 40.0f, f.roll_y(), 4.0);
    CHECK(h.target == EditorTarget::Roll);
    CHECK(h.section == f.sections.sections[0].id);
    // A hidden cursor grabs nothing.
    h = f.hit(f.x(4.0), f.roll_y(), 4.0, false);
    CHECK(h.target == EditorTarget::Roll);
    // The cursor's line does not reach into the band row.
    h = f.hit(f.x(4.0), f.band_y(), 4.0);
    CHECK(h.target != EditorTarget::Cursor);
}

TEST_CASE("band names rename, the rest of the band selects") {
    EditorFixture f;
    const Section& second = f.sections.sections[1];
    const float left = band_name_left(f.layout, f.view, second.start_s, 22.0f);
    EditorHit h = f.hit(left + 10.0f, f.band_y());
    CHECK(h.target == EditorTarget::BandName);
    CHECK(h.section == second.id);
    h = f.hit(left + 100.0f, f.band_y());  // past the 60-point pill
    CHECK(h.target == EditorTarget::Band);
    CHECK(h.section == second.id);
    // A band with no pill (too narrow for a name) only selects.
    f.name_w[1] = 0.0f;
    h = f.hit(left + 10.0f, f.band_y());
    CHECK(h.target == EditorTarget::Band);
}

TEST_CASE("a band that starts off screen puts its name at the axis start") {
    EditorFixture f;
    f.view = RollView{10.0, 20.0};
    const Section& second = f.sections.sections[1];  // 8 - 16 s, starts off screen
    CHECK(band_name_left(f.layout, f.view, second.start_s, 22.0f) == doctest::Approx(22.0f));
    const EditorHit h = f.hit(30.0f, f.band_y());
    CHECK(h.target == EditorTarget::BandName);
}

TEST_CASE("touches outside the roll and band rows hit nothing") {
    EditorFixture f;
    CHECK(f.hit(100.0f, f.layout.toolbar.y + 5.0f).target == EditorTarget::None);
    CHECK(f.hit(100.0f, f.layout.loupe.y + 5.0f).target == EditorTarget::None);
    CHECK(f.hit(-5.0f, f.roll_y()).target == EditorTarget::None);
    CHECK(f.hit(900.0f, f.roll_y()).target == EditorTarget::None);
    // The ruler counts as the roll: a tap there places the cursor.
    CHECK(f.hit(300.0f, f.layout.ruler.y + 3.0f).target == EditorTarget::Roll);
}

TEST_CASE("a song with no sections has no handles or bands") {
    EditorFixture f;
    f.sections.sections.clear();
    EditorHit h = f.hit(f.x(8.0), f.roll_y(), 20.0);
    CHECK(h.target == EditorTarget::Roll);
    CHECK(h.section == kNoSection);
    h = f.hit(f.x(8.0), f.band_y(), 20.0);
    CHECK(h.target == EditorTarget::None);
}


TEST_CASE("the magnifier floats above the touch, clear of the finger") {
    const RectF area{0, 60, 800, 400};
    const RectF r = loupe_bubble_rect(400, 300, area, 220, 90, 70, 8);
    CHECK(r.bottom() == doctest::Approx(300.0f - 70.0f));
    CHECK(r.x == doctest::Approx(290.0f));
    // The finger (a 30-point disc) is never inside it.
    CHECK(r.bottom() < 300.0f - 30.0f);
}

TEST_CASE("the magnifier stays inside the area, sliding sideways at the edges") {
    const RectF area{10, 60, 780, 400};
    RectF r = loupe_bubble_rect(15, 300, area, 220, 90, 70, 8);
    CHECK(r.x == doctest::Approx(18.0f));
    r = loupe_bubble_rect(795, 300, area, 220, 90, 70, 8);
    CHECK(r.right() == doctest::Approx(790.0f - 8.0f));
    CHECK(r.y >= area.y + 8.0f);
}

TEST_CASE("with no room above, the magnifier goes below the touch, else to the top edge") {
    const RectF area{0, 60, 800, 400};
    RectF r = loupe_bubble_rect(400, 120, area, 220, 90, 70, 8);
    CHECK(r.y == doctest::Approx(190.0f));
    CHECK(r.bottom() <= area.bottom() - 8.0f);
    const RectF small{0, 60, 800, 150};
    r = loupe_bubble_rect(400, 130, small, 220, 90, 70, 8);
    CHECK(r.y == doctest::Approx(68.0f));
}

TEST_CASE("the time axis runs across the roll, the ruler and the bands alike") {
    for (const Size& w : kWindows) {
        const EditorLayout l = editor_layout({10.0f, 60.0f, w.w - 20.0f, w.h - 70.0f}, 1.0f);
        CHECK(l.axis_x0() == l.roll.x);
        CHECK(l.axis_x1() == l.roll.right());
        CHECK(l.ruler.x == doctest::Approx(l.axis_x0()));
        CHECK(l.ruler.right() == doctest::Approx(l.axis_x1()));
        CHECK(l.bands.x == doctest::Approx(l.axis_x0()));
        CHECK(l.bands.right() == doctest::Approx(l.axis_x1()));
        const RollView view{0.0, 10.0};
        CHECK(editor_x(l, view, view.start_s) == doctest::Approx(l.axis_x0()));
        CHECK(editor_x(l, view, view.end_s) == doctest::Approx(l.axis_x1()));
    }
}

TEST_CASE("a loupe's label band and note area never intersect") {
    // The panel under the roll at every window size, and the magnifier at several scales.
    std::vector<RectF> rects;
    for (const Size& w : kWindows) {
        rects.push_back(editor_layout({10.0f, 60.0f, w.w - 20.0f, w.h - 70.0f}, 1.0f).loupe);
    }
    for (const float u : {0.8f, 1.0f, 1.5f, 2.0f}) {
        rects.push_back({100.0f, 80.0f, 250.0f * u, 120.0f * u});
    }
    rects.push_back({0.0f, 0.0f, 40.0f, 30.0f}); // far too small to be useful, still sound
    for (const RectF& r : rects) {
        for (const float inset : {0.0f, 6.0f, 12.0f}) {
            for (const float header_h : {0.0f, 14.0f, 22.0f, 400.0f}) {
                const LoupeRegions g = loupe_regions(r, inset, header_h);
                CHECK_FALSE(overlap(g.header, g.notes));
                CHECK(g.header.bottom() <= g.notes.y + 1e-3f);
                CHECK(inside(g.header, r));
                CHECK(inside(g.notes, r));
                CHECK(g.header.x == doctest::Approx(g.notes.x));
                CHECK(g.header.w == doctest::Approx(g.notes.w));
                CHECK(g.mid_x == doctest::Approx(g.notes.x + g.notes.w * 0.5f));
                // The notes keep at least 60% of what is inside the inset.
                CHECK(g.notes.h >= (r.h - 2.0f * inset) * 0.6f - 1e-3f);
            }
        }
    }
    // A header that fits is exactly as tall as asked.
    const LoupeRegions g = loupe_regions({0.0f, 0.0f, 250.0f, 120.0f}, 6.0f, 20.0f);
    CHECK(g.header.h == doctest::Approx(20.0f));
    CHECK(g.header.y == doctest::Approx(6.0f));
    CHECK(g.notes.y == doctest::Approx(26.0f));
    CHECK(g.notes.bottom() == doctest::Approx(114.0f));
}

TEST_CASE("the lane: four 44 pt tools at the left, the strip in the rest, nothing overlapping") {
    for (const float u : {1.0f, 1.25f, 1.41f, 1.5f}) {
        for (const float lane_pt : {kLaneMinPt, 112.0f, kLanePt}) {
            for (const float w : {800.0f, 1366.0f, 1920.0f}) {
                CAPTURE(u);
                CAPTURE(lane_pt);
                CAPTURE(w);
                // The lane less a margin of a dead gap on top, 4 pt below and 8 pt each side, as drawn.
                const RectF area{8.0f * u, 100.0f, w - 16.0f * u, (lane_pt - 12.0f) * u};
                const LaneLayout l = lane_layout(area, u);
                const RectF tools[] = {l.split, l.across, l.undo, l.fit};
                for (const RectF& t : tools) {
                    CHECK(t.w >= 44.0f * u - 1e-3f);
                    CHECK(t.h >= 44.0f * u - 1e-3f);
                    CHECK(t.x >= area.x - 1e-3f);
                    CHECK(t.bottom() <= area.bottom() + 1e-3f);
                    CHECK(t.right() <= l.strip.area.x);
                }
                for (std::size_t i = 0; i < 4; ++i) {
                    for (std::size_t j = i + 1; j < 4; ++j) {
                        const RectF& a = tools[i];
                        const RectF& b = tools[j];
                        CHECK_FALSE((a.x < b.right() - 1e-3f && b.x < a.right() - 1e-3f &&
                                     a.y < b.bottom() - 1e-3f && b.y < a.bottom() - 1e-3f));
                    }
                }
                const EditorLayout& e = l.strip;
                CHECK(e.fits);
                CHECK(e.ruler.bottom() == doctest::Approx(e.roll.y));
                CHECK(e.roll.bottom() == doctest::Approx(e.bands.y));
                CHECK(e.bands.bottom() == doctest::Approx(area.bottom()));
                CHECK(e.bands.h >= 44.0f * u - 1e-3f); // the names keep a touch target
                CHECK(e.roll.h > 0.0f);
                CHECK(e.toolbar.empty());
                CHECK(e.loupe.empty());
                CHECK(e.actions.empty());
                // A touch on the strip's notes finds the roll, as in the editor.
                const RollView view{0.0, 60.0};
                const EditorHit hit =
                    editor_hit(e, view, SongSections{}, 0.0, false, e.roll.x + e.roll.w * 0.5f,
                               e.roll.y + e.roll.h * 0.5f, 28.0f * u, nullptr, 0);
                CHECK(hit.target == EditorTarget::Roll);
            }
        }
    }
}

TEST_CASE("the action row has one caption rule: all six captioned when they fit with the band's name, else none") {
    for (const Size& sz : kWindows) {
        for (const float u : {1.0f, 1.25f, 1.5f}) {
            const EditorLayout l = editor_layout(area_for(sz, u), u);
            const ActionSlots a = action_slots(l.actions, u);
            CAPTURE(sz.w);
            CAPTURE(u);
            CHECK(a.captions); // every supported window has the room
            for (const RectF& r : {a.split, a.play, a.play_across, a.band_rename, a.band_merge,
                                   a.band_delete}) {
                CHECK(r.w == doctest::Approx((kEditorHit + kEditorCaptionExtra) * u));
            }
        }
    }
    // A row too narrow for six captioned buttons and the name: every button icon-only, a hit area.
    const float u = 1.0f;
    const float need = 6.0f * (kEditorHit + kEditorCaptionExtra) + 8.0f * kMinHitGapPt +
                       (kDestructiveGap - kMinHitGapPt) + 40.0f;
    const ActionSlots at = action_slots({0.0f, 0.0f, need, 52.0f}, u);
    CHECK(at.captions); // exactly at
    const ActionSlots under = action_slots({0.0f, 0.0f, need - 1.0f, 52.0f}, u);
    CHECK_FALSE(under.captions);
    for (const RectF& r : {under.split, under.play, under.play_across, under.band_rename,
                           under.band_merge, under.band_delete}) {
        CHECK(r.w == doctest::Approx(kEditorHit));
    }
    CHECK_FALSE(under.band_chip.empty()); // the name keeps its room
}

TEST_CASE("a hold on a boundary the playhead stands beside takes the handle, never the cursor that cuts") {
    EditorFixture f;
    const float slop = 9.0f;
    const float hx = f.x(8.0);
    const auto hit = [&](double cursor_x_offset, float touch_x_offset, float margin) {
        const double cursor = editor_time(f.layout, f.view, hx + static_cast<float>(cursor_x_offset));
        return editor_hit(f.layout, f.view, f.sections, cursor, true, hx + touch_x_offset,
                          f.roll_y(), 22.0f, f.name_w.data(), f.name_w.size(), margin);
    };
    // The playhead 5 pt right of the boundary, a touch on the cursor line: nearer to the cursor
    // than to the boundary, so the old rule gave the cursor, and a hold there cut the section.
    CHECK(hit(5.0, 5.0, 0.0f).target == EditorTarget::Cursor);
    EditorHit slip = hit(5.0, 5.0, slop);
    CHECK(slip.target == EditorTarget::Handle);
    CHECK(slip.handle == 0);
    // Between the two, and on the boundary itself: the handle.
    CHECK(hit(5.0, 2.0, slop).target == EditorTarget::Handle);
    CHECK(hit(5.0, 0.0, slop).target == EditorTarget::Handle);
    // The playhead exactly a slop away still gives way to the handle; one point more, and the
    // cursor is grabbed where it is nearer, as before.
    CHECK(hit(slop, slop, slop).target == EditorTarget::Handle);
    CHECK(hit(slop + 1.0, slop + 1.0, slop).target == EditorTarget::Cursor);
    CHECK(hit(20.0, 20.0, slop).target == EditorTarget::Cursor);
    // The other side of the boundary alike.
    CHECK(hit(-5.0, -5.0, slop).target == EditorTarget::Handle);
    // The cursor alone, with no handle near it, is untouched by the margin.
    const double far = editor_time(f.layout, f.view, f.x(4.0));
    CHECK(editor_hit(f.layout, f.view, f.sections, far, true, f.x(4.0), f.roll_y(), 22.0f,
                     f.name_w.data(), f.name_w.size(), slop)
              .target == EditorTarget::Cursor);
}

TEST_CASE("the readout pill is centred on its time, kept inside the axis, and hides the ruler numbers it touches") {
    // Centred, and held inside [min_x, max_x] at either end.
    const RectF mid = readout_pill_rect(200.0f, 100.0f, 0.0f, 400.0f, 80.0f, 12.0f, 6.0f, 3.0f);
    CHECK(mid.w == doctest::Approx(92.0f));
    CHECK(mid.h == doctest::Approx(18.0f));
    CHECK(mid.x + mid.w * 0.5f == doctest::Approx(200.0f));
    CHECK(mid.y + mid.h * 0.5f == doctest::Approx(100.0f));
    CHECK(readout_pill_rect(2.0f, 100.0f, 0.0f, 400.0f, 80.0f, 12.0f, 6.0f, 3.0f).x == 0.0f);
    CHECK(readout_pill_rect(399.0f, 100.0f, 0.0f, 400.0f, 80.0f, 12.0f, 6.0f, 3.0f).right() ==
          doctest::Approx(400.0f));

    // A number inside, touching or within the margin of the pill gives way; one clear of it stays.
    const RectF pill{100.0f, 50.0f, 90.0f, 18.0f};
    CHECK(ruler_label_hidden({120.0f, 55.0f, 6.0f, 10.0f}, pill, 2.0f));
    CHECK(ruler_label_hidden({191.0f, 55.0f, 6.0f, 10.0f}, pill, 2.0f)); // 1 pt right: within margin
    CHECK_FALSE(ruler_label_hidden({192.0f, 55.0f, 6.0f, 10.0f}, pill, 2.0f));
    CHECK_FALSE(ruler_label_hidden({120.0f, 71.0f, 6.0f, 10.0f}, pill, 2.0f)); // below it
    CHECK_FALSE(ruler_label_hidden({120.0f, 55.0f, 6.0f, 10.0f}, RectF{}, 2.0f)); // no readout
}

TEST_CASE("in the lane at every supported size the readout's pill never overlaps a ruler number left standing") {
    for (const Size& sz : kWindows) {
        for (const float u : {1.0f, 1.25f, 1.5f}) {
            const LaneLayout lane = lane_layout(area_for(sz, u), u);
            const EditorLayout& e = lane.strip;
            // The cursor at every x of the axis; numbers every 34 pt, 7 pt wide, on the ruler.
            for (float cx = e.axis_x0(); cx <= e.axis_x1(); cx += 13.0f) {
                const RectF pill = readout_pill_rect(cx, e.roll.bottom() - 14.0f * u, e.axis_x0(),
                                                     e.axis_x1(), 90.0f * u, 12.0f * u, 6.0f * u, 3.0f * u);
                for (float nx = e.axis_x0(); nx < e.axis_x1(); nx += 34.0f * u) {
                    const RectF n{nx + 3.0f * u, e.ruler.y, 7.0f * u, e.ruler.h};
                    if (!ruler_label_hidden(n, pill, 2.0f * u)) {
                        const bool apart = n.right() <= pill.x || n.x >= pill.right() ||
                                           n.bottom() <= pill.y || n.y >= pill.bottom();
                        CHECK(apart);
                    }
                }
            }
        }
    }
}
