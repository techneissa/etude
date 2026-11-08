// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/ui/about_layout.hpp"
#include "failure_samples.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace etude::app::ui;
using etude::app::FailureText;
using etude::app::every_failure_text;

namespace {

constexpr float kEps = 0.01f;
// The panel sizes the app is drawn at: the unit grows with the window, from 1.
constexpr float kUnits[] = {1.0f, 1.25f, 1.5f, 1.6f};
constexpr float kWidths[] = {420.0f, 560.0f, 760.0f, 1000.0f};

bool inside(const Rect& r, float w) { return r.x >= -kEps && r.right() <= w + kEps; }
bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

} // namespace

TEST_CASE("about layout: the links are touch targets, apart, inside the page, centred") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            for (const float name_w : {60.0f, 90.0f, 140.0f}) {
                const AboutLinks g = about_links(w, u, name_w * u);
                for (const Rect& r : {g.github, g.coffee}) {
                    CHECK(r.w >= tok::kHit * u - kEps);
                    CHECK(r.h >= tok::kHit * u - kEps);
                    CHECK(inside(r, w));
                }
                CHECK(g.coffee.x - g.github.right() >= tok::kHitGap * u - kEps);
                const float left = g.github.x;
                const float right = w - g.coffee.right();
                CHECK(left == doctest::Approx(right).epsilon(0.001));
                CHECK(g.github.w >= name_w * u + about_tok::kMark * u);
            }
        }
    }
}

TEST_CASE("about layout: the top is one row at every size, the links at its right end") {
    for (const float u : kUnits) {
        for (const float w : {560.0f * u, 760.0f * u, 1000.0f * u, 1500.0f * u}) {
            const float text_w = 110.0f * u;
            const float name_w = 80.0f * u;
            const AboutTop g = about_top(w, u, text_w, name_w);
            CAPTURE(w);
            CAPTURE(u);
            CHECK(inside(g.icon, w));
            CHECK(inside(g.name, w));
            CHECK(inside(g.version, w));
            for (const Rect& r : {g.github, g.coffee}) {
                CHECK(r.w >= tok::kHit * u - kEps);
                CHECK(r.h >= tok::kHit * u - kEps);
                CHECK(inside(r, w));
                CHECK(r.y >= -kEps);
                CHECK(r.bottom() <= g.height + kEps);
            }
            CHECK(g.coffee.x - g.github.right() >= tok::kHitGap * u - kEps);
            CHECK(w - g.coffee.right() == doctest::Approx(tok::kS3 * u));
            CHECK_FALSE(overlap(g.icon, g.github));
            CHECK_FALSE(overlap(g.name, g.github));
            CHECK_FALSE(overlap(g.version, g.coffee));
            CHECK(g.name.x >= g.icon.right());
            CHECK(g.name.right() <= g.github.x - tok::kS3 * u + kEps); // words clear of the links
            CHECK(g.icon.bottom() <= g.height + kEps);
            CHECK(g.version.bottom() <= g.height + kEps);
            // The row is the same height whatever the width.
            CHECK(g.height == doctest::Approx(about_top(1500.0f * u, u, text_w, name_w).height));
        }
    }
    // The same row at 800x480's page and at 1920x1080's: icon and words stay at the left.
    const AboutTop narrow = about_top(560.0f, 1.0f, 110.0f, 80.0f);
    const AboutTop wide = about_top(1500.0f, 1.0f, 110.0f, 80.0f);
    CHECK(narrow.icon.x == doctest::Approx(wide.icon.x));
    CHECK(narrow.name.x == doctest::Approx(wide.name.x));
    CHECK(narrow.name.w == doctest::Approx(110.0f));
    // A page too narrow for the words gives them what the links leave, never over a link.
    const AboutTop cramped = about_top(260.0f, 1.0f, 110.0f, 80.0f);
    CHECK(cramped.name.w < 110.0f);
    CHECK(cramped.name.right() <= cramped.github.x + kEps);
}

TEST_CASE("about layout: the status row keeps its action a touch target clear of the words") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            for (const bool bar : {false, true}) {
                for (const bool detail : {false, true}) {
                    const AboutStatus g = about_status(w, u, true, bar, detail);
                    CHECK(g.action.w >= tok::kHit * u - kEps);
                    CHECK(g.action.h >= tok::kHit * u - kEps);
                    CHECK(inside(g.action, w));
                    CHECK(inside(g.text, w));
                    CHECK(g.text.right() <= g.action.x - tok::kS2 * u + kEps);
                    CHECK(g.action.y >= -kEps);
                    CHECK(g.action.bottom() <= g.row.h + kEps);
                    CHECK(g.icon.right() <= g.text.x);
                    CHECK((g.bar.h > 0.0f) == bar);
                    CHECK((g.detail.h > 0.0f) == detail);
                    CHECK(g.detail.y >= g.bar.bottom() - kEps);
                    CHECK(g.height >= g.detail.bottom() - kEps);
                    CHECK(inside(g.bar, w));
                }
            }
            const AboutStatus none = about_status(w, u, false, false, false);
            CHECK(none.action.w == 0.0f);
            CHECK(none.text.right() == doctest::Approx(w - tok::kS3 * u));
            CHECK(none.height == doctest::Approx(about_tok::kStatusH * u));
        }
    }
}

TEST_CASE("about layout: Check now and the time of the last check share one row") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            const AboutCheckRow g = about_check_row(w, u);
            CHECK(g.button.w >= 100.0f * u - kEps);
            CHECK(g.button.h >= tok::kHit * u - kEps);
            CHECK(inside(g.button, w));
            CHECK(inside(g.text, w));
            CHECK_FALSE(overlap(g.text, g.button));
            CHECK(g.button.x - g.text.right() >= tok::kS2 * u - kEps);
        }
    }
}

TEST_CASE("about layout: an update adds its notes and a button") {
    for (const float u : kUnits) {
        for (const float w : kWidths) {
            for (const int lines : {0, 1, 4}) {
                const AboutUpdate g = about_update(w, u, 100.0f * u, lines);
                CHECK(g.button.h >= tok::kHit * u - kEps);
                CHECK(g.button.w == doctest::Approx(w - 2.0f * tok::kS3 * u));
                CHECK(inside(g.button, w));
                CHECK(inside(g.notes, w));
                CHECK(g.notes.h == doctest::Approx(static_cast<float>(lines) *
                                                   about_tok::kNotesLineH * u));
                CHECK(g.button.y >= g.notes.bottom() - kEps);
                CHECK(g.height >= g.button.bottom() - 100.0f * u);
                CHECK((g.caption.h > 0.0f) == (lines > 0));
            }
        }
    }
}

TEST_CASE("about layout: in two columns Update is as wide as its words, not the card") {
    for (const float u : kUnits) {
        for (const float w : {420.0f, 700.0f, 1100.0f}) {
            const AboutUpdate g = about_update(w * u, u, 60.0f * u, 3, 150.0f * u);
            CHECK(g.button.w == doctest::Approx(std::min(150.0f * u, w * u - 2.0f * tok::kS3 * u)));
            CHECK(g.button.h >= tok::kHit * u - kEps);
            CHECK(g.button.x == doctest::Approx(tok::kS3 * u));
            // A width under a touch target is raised to one.
            CHECK(about_update(w * u, u, 0.0f, 0, 10.0f * u).button.w >= tok::kHit * u - kEps);
        }
    }
}

TEST_CASE("about layout: wide windows put the two boxes side by side, narrow ones stack them") {
    CHECK(about_side_by_side(1100.0f, 1.0f));
    CHECK(about_side_by_side(kAboutWideW, 1.0f));
    CHECK_FALSE(about_side_by_side(kAboutWideW - 1.0f, 1.0f));
    CHECK_FALSE(about_side_by_side(560.0f, 1.0f));
    for (const float u : kUnits) {
        for (const float w : {kAboutWideW * u, 1000.0f * u, 1500.0f * u, 560.0f * u, 420.0f * u}) {
            CAPTURE(w);
            CAPTURE(u);
            const AboutSettings set = about_settings(w, u);
            const AboutBoxes g = about_boxes(w, u, 140.0f * u, set.height);
            CHECK(inside(g.status, w));
            CHECK(inside(g.settings, w));
            CHECK_FALSE(overlap(g.status, g.settings));
            if (g.side_by_side) {
                CHECK(g.settings.x >= g.status.right() + tok::kS3 * u - kEps);
                CHECK(g.settings.y == doctest::Approx(g.status.y));
                CHECK(g.settings.w >= 360.0f * u - kEps);
                CHECK(g.status.w >= 280.0f * u); // keeps room for the status words
                CHECK(g.status.h == doctest::Approx(g.settings.h));
                CHECK(g.height == doctest::Approx(std::max(140.0f * u, set.height)));
            } else {
                CHECK(g.settings.y >= g.status.bottom() + tok::kS3 * u - kEps);
                CHECK(g.status.w == doctest::Approx(w));
                CHECK(g.settings.w == doctest::Approx(w));
                CHECK(g.height == doctest::Approx(g.settings.bottom()));
            }
        }
    }
}

TEST_CASE("about layout: the settings box holds its two rows apart, each a touch target") {
    for (const float u : kUnits) {
        for (const float w : {360.0f * u, 440.0f * u, 560.0f * u}) {
            CAPTURE(w);
            const AboutSettings g = about_settings(w, u);
            for (const Rect& r : {g.switch_row, g.check_row}) {
                CHECK(r.h >= tok::kHit * u - kEps);
                CHECK(inside(r, w));
            }
            CHECK_FALSE(overlap(g.switch_row, g.check_row));
            CHECK(g.check_row.y - g.switch_row.bottom() >= tok::kHitGap * u - kEps);
            CHECK(g.height >= g.check_row.bottom());
            const AboutCheckRow row = about_check_row(w, u);
            CHECK(row.text.w >= 100.0f * u);
        }
    }
}

TEST_CASE("about layout: a failure's status icon is the reason's button, a touch target") {
    for (const float u : kUnits) {
        for (const float w : {320.0f * u, 420.0f * u, 560.0f * u, 800.0f * u}) {
            CAPTURE(w);
            const AboutStatus s = about_status(w, u, true, false, false, true);
            CHECK(s.icon.w >= tok::kHit * u - kEps);
            CHECK(s.icon.h >= tok::kHit * u - kEps);
            CHECK_FALSE(overlap(s.icon, s.action));
            CHECK_FALSE(overlap(s.icon, s.text));
            CHECK(s.text.x - s.icon.right() >= tok::kHitGap * u - kEps);
            CHECK(s.action.x - s.text.right() >= 0.0f);
            CHECK(s.text.w > 0.0f);
            // Without a reason to open it is a plain icon and the line keeps the room.
            const AboutStatus plain = about_status(w, u, true, false, false, false);
            CHECK(plain.icon.w < tok::kHit * u);
            CHECK(plain.text.w > s.text.w);
        }
    }
}

TEST_CASE("about layout: words wrap at spaces into at most the lines asked for") {
    const auto width = [](const std::string& s) { return static_cast<float>(s.size()) * 10.0f; };
    using Lines = std::vector<std::string>;
    CHECK(wrap_words("one two three", 1000.0f, 2, width) == Lines{"one two three"});
    CHECK(wrap_words("one two three", 80.0f, 2, width) == Lines{"one two", "three"});
    CHECK(wrap_words("one two three", 50.0f, 2, width) == Lines{"one", "two three"}); // the last takes the rest
    CHECK(wrap_words("one two three", 50.0f, 3, width) == Lines{"one", "two", "three"});
    CHECK(wrap_words("extraordinary x", 50.0f, 3, width) == Lines{"extraordinary", "x"});
    CHECK(wrap_words("one", 5.0f, 2, width) == Lines{"one"});
    CHECK(wrap_words("", 50.0f, 2, width).empty());
}

TEST_CASE("about layout: at the side-by-side break every short reason fits without cutting") {
    // The widest average glyph of the repo's fitting estimate, at the status font.
    for (const float u : kUnits) {
        const AboutBoxes boxes = about_boxes(kAboutWideW * u, u, 100.0f * u, 100.0f * u);
        REQUIRE(boxes.side_by_side);
        const AboutStatus s = about_status(boxes.status.w, u, true, false, false, true);
        const auto width = [&](const std::string& line) {
            return static_cast<float>(line.size()) * etude::kLabelCharWidthEm * tok::kBody * u;
        };
        const float line_h = tok::kBody * phone_tok::kLineRatio * u;
        CHECK(2.0f * line_h <= s.text.h); // two lines stand in the row
        for (const FailureText& t : every_failure_text()) {
            CAPTURE(t.summary);
            CAPTURE(u);
            const std::vector<std::string> lines = wrap_words(t.summary, s.text.w, 2, width);
            CHECK(lines.size() <= 2);
            for (const std::string& line : lines) {
                CAPTURE(line);
                CHECK(width(line) <= s.text.w);
            }
        }
    }
}

TEST_CASE("about layout: the sheet shows the code and the address inside the body") {
    struct Body {
        float w;
        float h;
    };
    for (const float u : kUnits) {
        for (const Body b : {Body{1000.0f, 560.0f}, Body{700.0f, 360.0f}, Body{420.0f, 300.0f},
                             Body{700.0f, 200.0f}, Body{300.0f, 500.0f}}) {
            const AboutSheet g = about_sheet(b.w * u, b.h * u, u);
            CHECK(g.head.back.h >= tok::kHit * u - kEps);
            CHECK(inside(g.qr, b.w * u));
            CHECK(inside(g.address, b.w * u));
            CHECK(g.qr.w == doctest::Approx(g.qr.h));
            CHECK(g.qr.bottom() <= g.head.body.bottom() + kEps);
            CHECK(g.address.bottom() <= g.head.body.bottom() + kEps);
            CHECK(g.qr.y >= g.head.body.y - kEps);
            CHECK(inside(g.hint, b.w * u));
            CHECK(g.hint.bottom() <= g.head.body.bottom() + kEps);
            CHECK_FALSE(overlap(g.qr, g.address));
            CHECK_FALSE(overlap(g.address, g.hint));
            CHECK(g.qr.w <= about_tok::kQrMax * u + kEps);
        }
    }
    // Roomy: the code is as large as it goes. Short and wide: the address goes beside it.
    const AboutSheet roomy = about_sheet(1000.0f, 560.0f, 1.0f);
    CHECK(roomy.qr.w == doctest::Approx(about_tok::kQrMax));
    CHECK_FALSE(roomy.side_by_side);
    const AboutSheet wide = about_sheet(700.0f, 200.0f, 1.0f);
    CHECK(wide.side_by_side);
    CHECK(wide.address.x >= wide.qr.right());
}

// The kit cannot be linked into the tests (ImGui), so the rule is held on the source: an ink given
// to `info_button` is also the tint of its state, so the open state keeps the glyph in that ink
// (`state_ink` mixes toward the tint, which is the ink) and lights the face in a faint tint of it,
// never the accent; and the status icon passes the warning red.
TEST_CASE("about: the failed status icon stays red in every state, open included") {
    const auto read = [](const char* name) {
        std::ifstream in(std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / "ui" / name);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    CHECK(read("kit_internal.cpp").find("item_state(k, open, true, false, ink)") !=
          std::string::npos);
    CHECK(read("pages_about.cpp").find("icon::kWarning, &col::kWarning") != std::string::npos);
}
