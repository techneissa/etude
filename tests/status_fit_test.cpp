// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/status_fit.hpp"

#include <doctest/doctest.h>

#include <array>
#include <iterator>
#include <string>
#include <vector>

using namespace etude::app;
using namespace etude::app::ui;

namespace {

// The song's line as the app builds it, plus the other notes, with widths of 6 per character.
struct Line {
    std::vector<StatusPart> parts;
    std::vector<float> widths;
    std::vector<std::string> texts;

    void add(const char* text, StatusRank rank, std::uint8_t group) {
        texts.emplace_back(text);
        widths.push_back(6.0f * static_cast<float>(texts.back().size()));
        parts.push_back({{}, rank, group});
    }
    StatusFitInput input(float room, float label_w) const {
        StatusFitInput in;
        in.room = room;
        in.label_w = label_w;
        in.gap = 16.0f;
        in.group_gap = 18.0f;
        in.dot_gap = 18.0f;
        in.min_cut = 80.0f;
        in.parts = parts;
        in.part_w = widths;
        return in;
    }
};

Line song_line() {
    Line l;
    l.add("A song title", StatusRank::Title, 1);
    l.add("Flow", StatusRank::Mode, 1);
    l.add("Both hands", StatusRank::Hands, 1);
    l.add("100%", StatusRank::Speed, 1);
    l.add("Bar 3/47", StatusRank::Bar, 1);
    l.add("Sound: Mute", StatusRank::Sound, 1);
    l.add("Combo 0", StatusRank::Combo, 1);
    l.add("Paused", StatusRank::State, 1);
    return l;
}

// The width the fit draws: the name, then the notes it shows (the cut one at its cut width).
float drawn_width(const Line& l, const StatusFitInput& in, const StatusFit& f) {
    float w = f.label ? f.label_w : 0.0f;
    float notes = 0.0f;
    int prev = -1;
    for (int i = 0; i < static_cast<int>(l.parts.size()); ++i) {
        if (!f.shows(i)) {
            continue;
        }
        if (prev >= 0) {
            notes += l.parts[static_cast<std::size_t>(i)].group ==
                             l.parts[static_cast<std::size_t>(prev)].group
                         ? in.dot_gap
                         : in.group_gap;
        }
        notes += i == f.cut ? f.cut_w : l.widths[static_cast<std::size_t>(i)];
        prev = i;
    }
    return notes > 0.0f ? w + in.gap + notes : w;
}

} // namespace

TEST_CASE("status fit: with room for everything every part shows, none cut") {
    const Line l = song_line();
    const StatusFit f = fit_status(l.input(2000.0f, 75.0f));
    CHECK(f.label);
    CHECK(f.shown == all_status_parts(l.parts.size()));
    CHECK(f.cut == -1);
}

TEST_CASE("status fit: a shrinking row gives the notes up whole in the fixed order") {
    const Line l = song_line();
    // The order they go in: sound, combo, hands, mode, speed, state, bar, then the title.
    const StatusRank order[] = {StatusRank::Sound, StatusRank::Combo, StatusRank::Hands,
                                StatusRank::Mode,  StatusRank::Speed, StatusRank::State,
                                StatusRank::Bar};
    std::vector<StatusRank> gone;
    std::uint32_t before = all_status_parts(l.parts.size());
    for (float room = 1000.0f; room >= 0.0f; room -= 1.0f) {
        const StatusFitInput in = l.input(room, 75.0f);
        const StatusFit f = fit_status(in);
        // Never wider than the room, never a part back that has gone.
        CHECK(drawn_width(l, in, f) <= room + 0.001f);
        CHECK((f.shown & ~before) == 0);
        const std::uint32_t lost = before & ~f.shown;
        for (int i = 0; i < static_cast<int>(l.parts.size()); ++i) {
            if ((lost >> i & 1u) != 0) {
                gone.push_back(l.parts[static_cast<std::size_t>(i)].rank);
            }
        }
        before = f.shown;
    }
    REQUIRE(gone.size() == l.parts.size());
    for (std::size_t i = 0; i < std::size(order); ++i) {
        CHECK(gone[i] == order[i]);
    }
    CHECK(gone.back() == StatusRank::Title);
}

TEST_CASE("status fit: the title is cut with an ellipsis, never below the floor, then dropped") {
    Line l;
    l.add("A very long song title indeed", StatusRank::Title, 1);
    l.add("Bar 3/47", StatusRank::Bar, 1);
    for (float room = 400.0f; room >= 0.0f; room -= 1.0f) {
        const StatusFitInput in = l.input(room, 75.0f);
        const StatusFit f = fit_status(in);
        CHECK(drawn_width(l, in, f) <= room + 0.001f);
        if (f.cut >= 0) {
            CHECK(f.cut == 0);
            CHECK(f.cut_w >= in.min_cut);
            CHECK(f.cut_w < l.widths[0]);
        }
    }
    // Room for the name, a gap and a title cut to the floor: the title shows cut, the bar is gone.
    const StatusFit f = fit_status(l.input(75.0f + 16.0f + 80.0f, 75.0f));
    CHECK(f.cut == 0);
    CHECK(f.shown == 1u);
    // A little less and no title shows at all, not a few letters.
    const StatusFit g = fit_status(l.input(75.0f + 16.0f + 79.0f, 75.0f));
    CHECK(g.cut == -1);
    CHECK(g.shown == 0u);
    CHECK(g.label);
}

TEST_CASE("status fit: the keyboard's name goes last, and is cut only to the floor") {
    Line l;
    l.add("Out of range", StatusRank::OutOfRange, 4);
    // A name that fits is shown whole; the notes beside it need what is left.
    StatusFit f = fit_status(l.input(75.0f + 16.0f + 72.0f, 75.0f));
    CHECK(f.label);
    CHECK(f.label_w == 75.0f);
    CHECK(f.shown == 1u);
    // Without room for the note, the name stays.
    f = fit_status(l.input(100.0f, 75.0f));
    CHECK(f.label);
    CHECK(f.shown == 0u);
    // A name longer than its room is cut to the room down to the floor, with no notes.
    f = fit_status(l.input(120.0f, 300.0f));
    CHECK(f.label);
    CHECK(f.label_w == 120.0f);
    CHECK(f.shown == 0u);
    f = fit_status(l.input(80.0f, 300.0f));
    CHECK(f.label);
    CHECK(f.label_w == 80.0f);
    // Below the floor nothing shows, name included (a short name that does not fit goes too).
    f = fit_status(l.input(79.0f, 300.0f));
    CHECK_FALSE(f.label);
    CHECK(f.shown == 0u);
    f = fit_status(l.input(60.0f, 75.0f));
    CHECK_FALSE(f.label);
    f = fit_status(l.input(60.0f, 40.0f));
    CHECK(f.label);
}

TEST_CASE("status fit: the notes of one group join with a dot, another group is further apart") {
    Line l;
    l.add("One", StatusRank::Title, 1);
    l.add("Two", StatusRank::Bar, 1);
    l.add("Three", StatusRank::Plan, 2);
    StatusFitInput in = l.input(2000.0f, 0.0f);
    in.dot_gap = 10.0f;
    in.group_gap = 30.0f;
    CHECK(status_notes_width(in, all_status_parts(3)) == doctest::Approx(18 + 10 + 18 + 30 + 30));
    // Without a name there is no gap to pay for before the notes: they show in their own width.
    in.room = 106.0f;
    const StatusFit f = fit_status(in);
    CHECK_FALSE(f.label);
    CHECK(f.shown == all_status_parts(3));
}
