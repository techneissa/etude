// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/ui/icon_picker_facts.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude::app::ui;

TEST_CASE("icon picker facts: the medals, the next title and the colour it unlocks") {
    // 58 medals is Soloist (4); Pianist asks for 96 and unlocks Midnight.
    const IconPickerFacts f = icon_picker_facts(58);
    CHECK(f.title == 4);
    CHECK(f.title_name == "Soloist");
    CHECK(f.medals == "58 of 192 medals");
    CHECK(f.next == "Next: Pianist at 96");
    CHECK(f.unlock == "Midnight");
    CHECK(f.unlock_index == 4);
    CHECK(etude::kIconColours[static_cast<std::size_t>(f.unlock_index)].title == f.title + 1);
}

TEST_CASE("icon picker facts: the one-line wordings run from the fullest to the shortest") {
    const IconPickerFacts f = icon_picker_facts(72);
    CHECK(f.line[0] == "72 of 192 medals \xC2\xB7 Next: Pianist at 96 \xC2\xB7 unlocks Midnight");
    CHECK(f.line[1] == "Next: Pianist at 96 \xC2\xB7 unlocks Midnight");
    CHECK(f.line[2] == "72 of 192 medals \xC2\xB7 Next: Pianist at 96");
    CHECK(f.line[3] == "Next: Pianist at 96");
    CHECK(f.side[0] == "72 of 192 medals");
    CHECK(f.side[1] == "Next: Pianist at 96");
    CHECK(f.side[2] == "Unlocks Midnight");
}

TEST_CASE("icon picker facts: the words are said in one line, in two where they must, else as they are") {
    const auto width = [](const std::string& text) { return static_cast<float>(text.size()); };
    const std::vector<std::string> texts{"aaa bbb ccc", "aaa bbb", "aaa"};
    CHECK(icon_picker_lines(texts, 11.0f, width) == std::vector<std::string>{"aaa bbb ccc"});
    CHECK(icon_picker_lines(texts, 8.0f, width) == std::vector<std::string>{"aaa bbb"});
    // Two lines of the first text, where no text fits one line.
    CHECK(icon_picker_lines({"aaaa bbbb cccc"}, 9.0f, width) ==
          std::vector<std::string>{"aaaa bbbb", "cccc"});
    // A word wider than the room stays whole, for the caller to cut.
    CHECK(icon_picker_lines({"abcdefgh"}, 4.0f, width) == std::vector<std::string>{"abcdefgh"});
}

TEST_CASE("icon picker facts: the line for a locked colour") {
    CHECK(icon_unlock_hint(etude::kIconColours[5]) == "Aubergine unlocks at Concert Pianist");
    CHECK(icon_unlock_hint(etude::kIconColours[1]) == "Teal unlocks at Student");
}

TEST_CASE("icon picker facts: every colour is the one its title unlocks, for every medal count") {
    for (int points = 0; points <= etude::kMaxMedalPoints; ++points) {
        CAPTURE(points);
        const IconPickerFacts f = icon_picker_facts(points);
        CHECK(f.title == etude::title_for_points(points));
        if (f.title < etude::kTierCount) {
            CHECK(f.unlock_index == f.title);
            CHECK(etude::kIconColours[static_cast<std::size_t>(f.unlock_index)].title == f.title + 1);
            CHECK(f.next.find(" at " + std::to_string(etude::title_points(f.title + 1))) !=
                  std::string::npos);
        } else {
            CHECK(f.unlock_index == -1);
            CHECK(f.unlock.empty());
        }
    }
}

TEST_CASE("icon picker facts: the top title has nothing next") {
    const IconPickerFacts f = icon_picker_facts(etude::kMaxMedalPoints);
    CHECK(f.title == 8);
    CHECK(f.medals == "192 of 192 medals");
    CHECK(f.next == "Top title reached");
    CHECK(f.line[0] == "192 of 192 medals \xC2\xB7 Top title reached");
    CHECK(f.line[1] == "Top title reached");
    CHECK(f.line[3] == "192 of 192 medals");
    CHECK(f.side[2].empty());
}
