// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Ear card's parts go (app/ui/ear_layout.hpp), at every window size the app runs in: every
// control at least a hit, inside the screen, none touching another; the keyboard's keys natural and
// each at least a hit wide; the columns sharing one block.
#include "ui/ear_layout.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

using namespace etude;
using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;
namespace lt = etude::app::ui::learn_tok;

namespace {

struct Window {
    float w;
    float h;
    float u;
};

// A window smaller in design points than 800x480 (a 7 inch panel at a taste scale above 1) cannot keep
// 12 mm controls with 2 mm gaps on this card, and is not supported.
constexpr std::array<Window, 7> kWindows{{{800, 480, 1.0f},
                                          {1024, 600, 1.0f},
                                          {1280, 720, 1.0f},
                                          {1366, 768, 1.0f},
                                          {1366, 768, 1.25f},
                                          {1920, 1080, 1.4f},
                                          {1920, 1080, 1.0f}}};
constexpr float kEps = 1e-3f;

Box area_for(const Window& s) { return {0.0f, 57.0f * s.u, s.w, s.h}; }

bool inside(const Box& inner, const Box& outer) {
    return inner.x0 >= outer.x0 - kEps && inner.y0 >= outer.y0 - kEps &&
           inner.x1 <= outer.x1 + kEps && inner.y1 <= outer.y1 + kEps;
}

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

// Whether two boxes are at least the dead gap apart (both directions count: the larger gap).
bool apart(const Box& a, const Box& b, float u) {
    const float dx = std::max(a.x0 - b.x1, b.x0 - a.x1);
    const float dy = std::max(a.y0 - b.y1, b.y0 - a.y1);
    return std::max(dx, dy) >= tok::kHitGap * u - kEps;
}

// Every pressable of the card, by name.
std::vector<std::pair<std::string, Box>> pressables(const EarLayout& e) {
    std::vector<std::pair<std::string, Box>> out;
    out.push_back({"back", e.back});
    for (std::size_t i = 0; i < e.tabs.size(); ++i) {
        out.push_back({"tab" + std::to_string(i), e.tabs[i]});
    }
    out.push_back({"disc", e.disc});
    if (e.short_form) {
        out.push_back({"settings", e.settings}); // the rows are in its card, over the rest
    } else {
        out.push_back({"range", e.range});
        out.push_back({"any", e.any});
        out.push_back({"size", e.size});
    }
    if (e.strip) {
        out.push_back({"modes", e.mode_strip});
    } else {
        for (std::size_t i = 0; i < e.mode.size(); ++i) {
            out.push_back({"mode" + std::to_string(i), e.mode[i]});
        }
    }
    out.push_back({"hint", e.hint});
    out.push_back({"next", e.next});
    return out;
}

} // namespace

TEST_CASE("ear card: every control is at least a hit, inside the screen, and none touches another") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const Box area = area_for(s);
        const EarLayout e = ear_layout(area, s.u);
        const std::vector<std::pair<std::string, Box>> controls = pressables(e);
        for (std::size_t i = 0; i < controls.size(); ++i) {
            const auto& [name, box] = controls[i];
            CAPTURE(name);
            CHECK(inside(box, area));
            CHECK(box.w() >= tok::kHit * s.u - kEps);
            CHECK(box.h() >= tok::kHit * s.u - kEps);
            for (std::size_t j = i + 1; j < controls.size(); ++j) {
                CAPTURE(controls[j].first);
                // The header is in the window's unit, the body in its own.
                const auto head = [](const std::string& n) { return n == "back" || n.rfind("tab", 0) == 0; };
                const float unit = head(name) || head(controls[j].first) ? s.u : e.ub;
                CHECK(apart(box, controls[j].second, unit));
            }
        }
        // The keyboard's keys: natural, a hit wide, on the screen, clear of the controls.
        CHECK(inside(e.keyboard, area));
        CHECK(e.whites >= app::kEarMinWhites);
        CHECK(e.whites <= app::kEarMaxWhites);
        // Each key a hit wide: the left column gives way to them, down to its three buttons in a row.
        CHECK(e.white_w >= tok::kHit * e.ub - kEps);
        CHECK(e.keyboard.h() <= e.white_w * lt::kKeyAspect + kEps);
        CHECK(e.keyboard.w() == doctest::Approx(e.white_w * static_cast<float>(e.whites)));
        for (const auto& [name, box] : controls) {
            if (name != "disc" && name != "range" && name != "any" && name != "size" &&
                name != "settings" && name != "back" && name.rfind("tab", 0) != 0) {
                CHECK_FALSE(overlap(box, e.keyboard));
            }
        }
        // The display parts are inside and clear of the pressables.
        for (const Box& part : {e.streak, e.dots, e.pill, e.ring}) {
            CHECK(inside(part, area));
            for (const auto& [name, box] : controls) {
                CAPTURE(name);
                if (name != "disc") { // the disc is in its ring
                    CHECK_FALSE(overlap(part, box));
                }
            }
        }
        CHECK(inside(e.disc, e.ring));
        CHECK_FALSE(overlap(e.streak, e.pill));
        CHECK_FALSE(overlap(e.dots, e.pill));
        CHECK_FALSE(overlap(e.streak, e.dots));
        CHECK_FALSE(overlap(e.pill, e.ring));
        CHECK_FALSE(overlap(e.dots, e.ring));
    }
}

TEST_CASE("ear card: the right column's stack is centred in the block of the left one") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const EarLayout e = ear_layout(area_for(s), s.u);
        const float stack_top = (e.strip ? e.mode_strip.y0 : e.mode[0].y0);
        CHECK(stack_top - e.column.y0 == doctest::Approx(e.column.y1 - e.hint.y1).epsilon(0.01).scale(1.0));
        CHECK(e.hint.y1 == doctest::Approx(e.next.y1));
        CHECK(e.hint.y0 == doctest::Approx(e.next.y0));
        // The left column is left of the right one.
        CHECK(e.column.x1 < e.keyboard.x0);
        // The foot row is centred under the keyboard.
        CHECK((e.hint.x0 + e.next.x1) * 0.5f == doctest::Approx(e.keyboard.cx()).epsilon(0.01));
    }
}

TEST_CASE("ear card: the interval size has a row of its own under the range and any-octave "
          "buttons wherever the window is tall enough, and a Settings card in a short one") {
    const EarLayout tall = ear_layout(area_for({1366, 768, 1.0f}), 1.0f);
    CHECK_FALSE(tall.short_form);
    CHECK(tall.size.y0 >= tall.range.y1);
    CHECK(tall.size.y0 >= tall.any.y1);
    CHECK(tall.range.y0 == doctest::Approx(tall.any.y0));
    CHECK(tall.range.x1 < tall.any.x0);
    // 12 mm hits leave the 800x480 body no room for the disc and two rows of settings: it has the
    // Settings button and its card.
    const EarLayout low = ear_layout(area_for({800, 480, 1.0f}), 1.0f);
    CHECK(low.short_form);
    // One button in the column; the three controls are in a card over the button, each a hit, the
    // size on its own row under the range and the switch.
    const Box area = area_for({800, 480, 1.0f});
    CHECK(inside(low.settings, area));
    CHECK(low.settings.w() >= tok::kHit - kEps);
    CHECK(inside(low.popover, area));
    CHECK_FALSE(overlap(low.popover, low.settings));
    for (const Box& b : {low.range, low.any, low.size}) {
        CHECK(inside(b, low.popover));
        CHECK(b.w() >= tok::kHit - kEps);
        CHECK(b.h() >= tok::kHit - kEps);
    }
    CHECK_FALSE(overlap(low.range, low.any));
    CHECK(low.size.y0 >= low.range.y1);
}

TEST_CASE("ear card: the replay disc is as large as the room allows, at most the column wide") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.u);
        const EarLayout e = ear_layout(area_for(s), s.u);
        CHECK(e.ring.w() <= e.column.w() + kEps);
        CHECK(e.disc.w() >= tok::kHit * e.ub * 1.2f * lt::kEarDiscShare - kEps);
        CHECK(e.disc.w() == doctest::Approx(e.disc.h()));
        if (!e.short_form) {
            CHECK(e.ring.w() >= lt::kEarDiscMin * e.ub - kEps);
        }
    }
}

TEST_CASE("ear card: the modes are a row of chips filling the column, or a strip when their words "
          "do not fit") {
    const EarLayout wide = ear_layout(area_for({1366, 768, 1.0f}), 1.0f);
    REQUIRE_FALSE(wide.strip);
    CHECK(wide.mode[0].x0 == doctest::Approx(wide.keyboard.x0));
    CHECK(wide.mode[3].x1 == doctest::Approx(wide.keyboard.x1));
    for (std::size_t i = 1; i < wide.mode.size(); ++i) {
        CHECK(wide.mode[i - 1].x1 < wide.mode[i].x0);
        CHECK(wide.mode[i].y0 == doctest::Approx(wide.mode[0].y0));
    }
    // Words that need more than the column holds: a strip over the same room.
    EarNeeds long_words;
    for (float& n : long_words.mode) {
        n = 400.0f;
    }
    const EarLayout strip = ear_layout(area_for({1366, 768, 1.0f}), 1.0f, long_words);
    CHECK(strip.strip);
    CHECK(strip.mode_strip.w() == doctest::Approx(strip.keyboard.w()));
}

TEST_CASE("ear card: more room shows more keys, never taller ones") {
    const EarLayout small = ear_layout(area_for({800, 480, 1.0f}), 1.0f);
    const EarLayout big = ear_layout(area_for({1920, 1080, 1.0f}), 1.0f);
    CHECK(small.whites < big.whites);
    CHECK(big.whites == app::kEarMaxWhites);
    CHECK(small.keyboard.h() <= small.white_w * lt::kKeyAspect + kEps);
    CHECK(big.keyboard.h() <= big.white_w * lt::kKeyAspect + kEps);
}

TEST_CASE("ear card: the keyboard is one surface that gives a press to one key: no two hit areas "
          "overlap, and every key is a hit wide and, below the black keys, a hit tall") {
    for (const Window& s : kWindows) {
        CAPTURE(s.w);
        CAPTURE(s.h);
        CAPTURE(s.u);
        const EarLayout e = ear_layout(area_for(s), s.u);
        const float hit = tok::kHit * e.ub;
        const EarKeys keys =
            ear_keys_layout(e.keyboard, e.white_w, 60, app::ear_white_note(60, e.whites - 1), hit);
        int blacks = 0;
        for (std::size_t i = 0; i < keys.count; ++i) {
            const EarKeyBox& k = keys.key[i];
            CAPTURE(k.note);
            CHECK(inside(k.hit, e.keyboard));
            CHECK(k.hit.w() >= hit - kEps);
            if (k.black) {
                ++blacks;
                CHECK(k.hit.h() >= hit - kEps);
                // A black key's middle gives its own note, over the white keys under it.
                CHECK(ear_key_at(keys, k.hit.cx(), k.hit.cy()) == k.note);
            } else {
                // The white key's part below the black keys is a hit tall, and gives its note.
                const float black_h = e.keyboard.h() * lt::kBlackHeightShare;
                CHECK(e.keyboard.h() - black_h >= hit - kEps);
                CHECK(ear_key_at(keys, k.box.cx(), k.box.y1 - 1.0f) == k.note);
            }
        }
        CHECK(blacks > 0);
        // Outside the keyboard nothing is a key.
        CHECK(ear_key_at(keys, e.keyboard.x0 - 1.0f, e.keyboard.cy()) == -1);
        CHECK(ear_key_at(keys, e.keyboard.cx(), e.keyboard.y1 + 1.0f) == -1);
    }
}
