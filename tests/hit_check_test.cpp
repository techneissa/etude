// SPDX-License-Identifier: GPL-3.0-or-later
// The audit's touch rules on made-up screens (app/ui/hit_check.hpp): each of the three failures is
// caught by a deliberately bad fixture, and a good screen passes.
#include "ui/hit_check.hpp"

#include "etude/panel_size.hpp"

#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr float kPtPerMm = 5.0f; // a panel of 5 points to the millimetre

HitItem item(const char* name, float x_mm, float y_mm, float w_mm, float h_mm) {
    HitItem i;
    i.name = name;
    i.box = {x_mm * kPtPerMm, y_mm * kPtPerMm, w_mm * kPtPerMm, h_mm * kPtPerMm};
    i.visible = i.box;
    return i;
}

int count(const HitReport& r, HitFault fault) {
    int n = 0;
    for (const HitFinding& f : r.findings) {
        n += f.fault == fault ? 1 : 0;
    }
    return n;
}

} // namespace

TEST_CASE("hit check: a screen of 12 mm areas 2 mm apart passes") {
    const std::vector<HitItem> good{item("a", 0, 0, 12, 12), item("b", 14, 0, 12, 12),
                                    item("c", 0, 14, 40, 12)};
    const HitReport r = check_hit_areas(good, kPtPerMm);
    CHECK(r.findings.empty());
    CHECK(r.sized == 3);
    CHECK(r.pairs == 3);
}

TEST_CASE("hit check: an area under 12 mm is caught, in either direction") {
    const std::vector<HitItem> bad{item("narrow", 0, 0, 11, 14), item("short", 30, 0, 14, 11)};
    const HitReport r = check_hit_areas(bad, kPtPerMm);
    REQUIRE(count(r, HitFault::TooSmall) == 2);
    CHECK(r.findings[0].a == "narrow");
    CHECK(r.findings[0].mm == doctest::Approx(11.0f));
    CHECK(r.findings[1].a == "short");
}

TEST_CASE("hit check: two overlapping areas are caught") {
    const std::vector<HitItem> bad{item("left", 0, 0, 14, 14), item("right", 10, 0, 14, 14)};
    const HitReport r = check_hit_areas(bad, kPtPerMm);
    REQUIRE(count(r, HitFault::Overlap) == 1);
    CHECK(count(r, HitFault::TooClose) == 0);
    CHECK(r.findings[0].mm == doctest::Approx(4.0f)); // overlap depth
    CHECK(r.findings[0].where.w == doctest::Approx(4.0f * kPtPerMm));
}

TEST_CASE("hit check: an area inside another is an overlap") {
    const std::vector<HitItem> bad{item("card", 0, 0, 60, 40), item("play", 40, 20, 12, 12)};
    CHECK(count(check_hit_areas(bad, kPtPerMm), HitFault::Overlap) == 1);
}

TEST_CASE("hit check: areas under 2 mm apart are caught, touching ones too") {
    const std::vector<HitItem> close{item("a", 0, 0, 12, 12), item("b", 13, 0, 12, 12)};
    const HitReport r = check_hit_areas(close, kPtPerMm);
    REQUIRE(count(r, HitFault::TooClose) == 1);
    CHECK(r.findings[0].mm == doctest::Approx(1.0f));
    const std::vector<HitItem> touching{item("a", 0, 0, 12, 12), item("b", 0, 12, 12, 12)};
    const HitReport t = check_hit_areas(touching, kPtPerMm);
    REQUIRE(count(t, HitFault::TooClose) == 1);
    CHECK(t.findings[0].mm == doctest::Approx(0.0f));
}

TEST_CASE("hit check: a pixel of rounding is not a finding, a millimetre is") {
    // At 5 points to the millimetre, one point is 0.2 mm.
    const std::vector<HitItem> rounded{item("a", 0, 0, 12, 12), item("b", 0, 13.85f, 12, 12)};
    CHECK(check_hit_areas(rounded, kPtPerMm).findings.empty()); // 1.85 mm
    CHECK_FALSE(check_hit_areas(rounded, kPtPerMm, 0.0f).findings.empty());
    const std::vector<HitItem> short_hit{item("a", 0, 0, 11.85f, 14)};
    CHECK(check_hit_areas(short_hit, kPtPerMm).findings.empty());
    const std::vector<HitItem> bad{item("a", 0, 0, 12, 12), item("b", 0, 13, 12, 12)};
    CHECK_FALSE(check_hit_areas(bad, kPtPerMm).findings.empty()); // 1 mm
}

TEST_CASE("hit check: exactly 2 mm apart is allowed") {
    const std::vector<HitItem> ok{item("a", 0, 0, 12, 12), item("b", 0, 14, 12, 12)};
    CHECK(check_hit_areas(ok, kPtPerMm).findings.empty());
}

TEST_CASE("hit check: corner to corner counts the diagonal") {
    // 1.5 mm across and 1.5 mm down: 2.12 mm apart, enough; 1 and 1: 1.41, not enough.
    const std::vector<HitItem> far{item("a", 0, 0, 12, 12), item("b", 13.5f, 13.5f, 12, 12)};
    CHECK(check_hit_areas(far, kPtPerMm).findings.empty());
    const std::vector<HitItem> near{item("a", 0, 0, 12, 12), item("b", 13, 13, 12, 12)};
    CHECK(count(check_hit_areas(near, kPtPerMm), HitFault::TooClose) == 1);
}

TEST_CASE("hit check: a covered area, a surface and an area off screen take no part in a pair") {
    std::vector<HitItem> items{item("under-card", 0, 0, 14, 14), item("card-button", 5, 0, 14, 14)};
    items[0].covered = true;
    CHECK(check_hit_areas(items, kPtPerMm).findings.empty());
    items[0].covered = false;
    items[0].surface = true;
    CHECK(check_hit_areas(items, kPtPerMm).findings.empty());
    items[0].surface = false;
    items[0].visible = {};
    CHECK(check_hit_areas(items, kPtPerMm).findings.empty());
    items[0].visible = items[0].box;
    CHECK(count(check_hit_areas(items, kPtPerMm), HitFault::Overlap) == 1);
}

TEST_CASE("hit check: a pair is judged on what its windows show") {
    // A row scrolled half out of its window: the shown half is what meets the next row.
    std::vector<HitItem> rows{item("row-1", 0, 0, 40, 14), item("row-2", 0, 16, 40, 14)};
    rows[0].visible = {rows[0].box.x, rows[0].box.y + 9 * kPtPerMm, rows[0].box.w,
                       5 * kPtPerMm};
    rows[0].cut = true;
    const HitReport r = check_hit_areas(rows, kPtPerMm);
    CHECK(r.findings.empty()); // a cut item is not held to the size rule, and the gap is 2 mm
}

TEST_CASE("hit check: a screen with no scale gives nothing to judge") {
    const std::vector<HitItem> bad{item("a", 0, 0, 5, 5)};
    CHECK(check_hit_areas(bad, 0.0f).findings.empty());
    CHECK(check_hit_areas(bad, -1.0f).sized == 0);
}

TEST_CASE("hit check: the faults are named for the report") {
    CHECK(std::string(hit_fault_name(HitFault::TooSmall)) == "small");
    CHECK(std::string(hit_fault_name(HitFault::Overlap)) == "overlap");
    CHECK(std::string(hit_fault_name(HitFault::TooClose)) == "gap");
}
