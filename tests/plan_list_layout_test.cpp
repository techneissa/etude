// SPDX-License-Identifier: GPL-3.0-or-later
// The practice list's card (app/ui/plan_list_layout.hpp) at the three window sizes and two UI
// scales, with 8, 16 and 32 items: the list always keeps one whole row, the title shows exactly
// when it and a row fit, and the card's footer is drawn outside the scrolling list.
#include "ui/plan_list_layout.hpp"
#include "ui/tokens.hpp"

#include "etude/screen_layout.hpp"
#include "etude/session_state.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr float kEps = 1e-3f;

// Assumptions the layout cannot measure (the font and ImGui's spacing are not pure): the title's
// text line in design points, and the scrolling part's height below the rows (the list's buttons,
// the item's settings with the speed-up open and the hand path, in design points).
constexpr float kAssumedTitlePt = 16.0f;
constexpr float kAssumedBelowRowsPt = 480.0f;

// The room under the band as the app computes it for a pinned bar (the dock style) over a song:
// from the screen layout, `host_band.cpp`'s band place and `list_popover`'s margins.
struct Room {
    const char* name;
    float u;
    float max_h;
};

Room room_of(const char* name, float w, float h, float scale) {
    const float px_per_mm = kDefaultPxPerMm * scale;
    PinnedBar bar;
    bar.pinned = true;
    bar.style = BarStyle::Dock;
    bar.has_song = true;
    const ScreenLayout lay = compute_layout(w, h, px_per_mm, LookSettings{}, bar);
    const float u = ui_scale(w, h, px_per_mm);
    const float y = lay.bar_under.bottom() + tok::kS2 * u;
    return {name, u, lay.keyboard.y - y - tok::kS2 * u};
}

const std::vector<Room>& rooms() {
    static const std::vector<Room> r{
        room_of("1920x1080 x1.0", 1920.0f, 1080.0f, 1.0f),
        room_of("1920x1080 x1.5", 1920.0f, 1080.0f, 1.5f),
        room_of("1366x768 x1.0", 1366.0f, 768.0f, 1.0f),
        room_of("1366x768 x1.5", 1366.0f, 768.0f, 1.5f),
        room_of("800x480 x1.0", 800.0f, 480.0f, 1.0f),
        room_of("800x480 x1.5", 800.0f, 480.0f, 1.5f),
    };
    return r;
}

PlanListIn card_in(float u, float max_h, std::size_t rows, bool known) {
    PlanListIn in;
    in.max_h = max_h;
    in.pad = tok::kS3 * u;
    in.title_h = kAssumedTitlePt * u;
    in.gap = tok::kS2 * u;
    in.hit = tok::kHit * u;
    // A row and the item spacing under it, then what sits below the rows.
    const float rows_h = static_cast<float>(rows) * (in.hit + tok::kS2 * u);
    in.content_h = known ? rows_h + kAssumedBelowRowsPt * u : 0.0f;
    return in;
}

std::string source_of(const char* name) {
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/app/ui/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("plan list: at every size and scale the card stays in its room and keeps a whole row") {
    for (const Room& room : rooms()) {
        for (const std::size_t rows : {std::size_t{8}, std::size_t{16}, std::size_t{32}}) {
            for (const bool known : {true, false}) {
                CAPTURE(room.name);
                CAPTURE(rows);
                CAPTURE(known);
                const PlanListIn in = card_in(room.u, room.max_h, rows, known);
                REQUIRE(room.max_h >= plan_list_min_h(in.pad, in.gap, in.hit));
                const PlanListLayout l = plan_list_layout(in);
                CHECK(l.h <= in.max_h + kEps);
                CHECK(l.scroll_h >= in.hit - kEps);
                CHECK(l.footer_h >= in.hit - kEps);
                // A long list fills the room.
                CHECK(l.h == doctest::Approx(in.max_h).epsilon(1e-4));
            }
        }
    }
}

TEST_CASE("plan list: any room the list opens in keeps one whole row and shows the title only "
          "when it and a row fit") {
    for (const float u : {1.0f, 1.4056f, 1.5f}) {
        const float pad = tok::kS3 * u;
        const float gap = tok::kS2 * u;
        const float hit = tok::kHit * u;
        const float least = plan_list_min_h(pad, gap, hit);
        // From the least room up to well past where the title comes in, a pixel at a time.
        for (float max_h = least; max_h < least + 300.0f * u; max_h += 1.0f) {
            for (const std::size_t rows : {std::size_t{8}, std::size_t{16}, std::size_t{32}}) {
                CAPTURE(u);
                CAPTURE(max_h);
                CAPTURE(rows);
                const PlanListIn in = card_in(u, max_h, rows, true);
                const PlanListLayout l = plan_list_layout(in);
                CHECK(l.scroll_h >= hit - kEps); // never less than one row
                CHECK(l.h <= max_h + kEps);
                // The title shows exactly when it, the footer and one row fit inside the padding.
                const float title_and_row = in.title_h + gap + hit;
                const float rest = max_h - 2.0f * pad - (gap + hit);
                CHECK(l.title == (rest >= title_and_row));
            }
        }
    }
}

TEST_CASE("plan list: a short list makes a short card") {
    for (const Room& room : rooms()) {
        CAPTURE(room.name);
        PlanListIn in = card_in(room.u, room.max_h, 1, true);
        in.content_h = in.hit; // one row, nothing else
        const PlanListLayout l = plan_list_layout(in);
        CHECK(l.scroll_h == doctest::Approx(in.hit));
        CHECK(l.h < in.max_h);
    }
}

TEST_CASE("plan list: the list closes only under the room the layout needs") {
    const std::string src = source_of("section_band.cpp");
    CHECK(src.find("max_h < plan_list_min_h(") != std::string::npos);
    CHECK(src.find("max_h < k.s(tok::kHit) * 2.0f") == std::string::npos);
}

TEST_CASE("plan list: the footer is drawn after the scrolling list's child ends, never inside it") {
    const std::string src = source_of("section_band.cpp");
    const std::size_t begin = src.find("BeginChild(\"##band-list-scroll\"");
    REQUIRE(begin != std::string::npos);
    const std::size_t end = src.find("ImGui::EndChild();", begin);
    REQUIRE(end != std::string::npos);
    const std::string scrolling = src.substr(begin, end - begin);
    CHECK(scrolling.find("plan_footer(") == std::string::npos);
    CHECK(scrolling.find("##plan-start") == std::string::npos);
    const std::size_t footer = src.find("plan_footer(k, sp, v, inner);", end);
    CHECK(footer != std::string::npos); // called once the child has ended
    // Not drawn from any of the pieces inside the scrolling child either.
    const std::size_t def = src.find("void plan_footer(");
    REQUIRE(def != std::string::npos);
    for (const char* piece : {"plan_rows(", "plan_list_buttons(", "plan_item_editor(", "plan_hand_path("}) {
        const std::size_t at = src.find(std::string("void ") + piece);
        REQUIRE(at != std::string::npos);
        const std::size_t stop = src.find("\n}\n", at);
        CHECK(src.substr(at, stop - at).find("plan_footer(") == std::string::npos);
    }
}
