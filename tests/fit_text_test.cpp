// SPDX-License-Identifier: GPL-3.0-or-later
// The UI's cached, cut-to-width labels: same result as cutting by hand, measured once, and no heap
// allocation once a label is cached.
#include "../app/ui/fit_text.hpp"

#include "alloc_count.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using etude::app::ui::FitCache;
using etude::app::ui::FitText;
using etude::app::ui::fit_with_name;
using etude::app::ui::kFitMax;

namespace {

// A made-up font: every character is `size` wide, whatever its bytes (so a 3-byte ellipsis counts
// as one character, as it draws).
float width_of(const char* text, float size) {
    float chars = 0.0f;
    for (const char* p = text; *p != '\0'; ++p) {
        if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) {
            chars += 1.0f;
        }
    }
    return chars * size;
}

// The cut the UI made before the cache: drop a character at a time until text + ellipsis fits.
std::string by_hand(const std::string& text, float size, float max_w) {
    if (width_of(text.c_str(), size) <= max_w) {
        return text;
    }
    std::string out = text;
    while (!out.empty()) {
        std::size_t cut = out.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
        const std::string candidate = out + "\xE2\x80\xA6";
        if (width_of(candidate.c_str(), size) <= max_w) {
            return candidate;
        }
    }
    return {};
}

} // namespace

TEST_CASE("fit: text that fits comes back whole") {
    FitCache cache;
    const auto m = [](const char* t) { return width_of(t, 10.0f); };
    const FitText f = cache.fit(nullptr, 10.0f, "Settings", 100.0f, m);
    CHECK(f.view() == "Settings");
    CHECK(std::string(f.c_str()) == "Settings");
}

TEST_CASE("fit: cut text matches the by-hand cut, also across UTF-8 characters") {
    FitCache cache;
    const auto m = [](const char* t) { return width_of(t, 10.0f); };
    const std::string texts[] = {std::string("A long song title here"),
                                 std::string("D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 caf\xC3\xA9"),
                                 std::string("x"), std::string()};
    for (const std::string& text : texts) {
        for (float w = 0.0f; w <= 260.0f; w += 10.0f) {
            const FitText f = cache.fit(nullptr, 10.0f, text, w, m);
            CHECK(std::string(f.view()) == by_hand(text, 10.0f, w));
        }
    }
}

TEST_CASE("fit: a cached label is not measured again and allocates nothing") {
    FitCache cache;
    int measured = 0;
    const auto m = [&measured](const char* t) {
        ++measured;
        return width_of(t, 10.0f);
    };
    const std::string text = "Practice list: left hand slowly";
    cache.fit(nullptr, 10.0f, text, 120.0f, m);
    const int first = measured;
    CHECK(first > 0);
    etude::test::start_counting_allocations();
    for (int i = 0; i < 1000; ++i) {
        const FitText f = cache.fit(nullptr, 10.0f, text, 120.0f, m);
        CHECK(f.size() > 0);
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    CHECK(measured == first);
}

TEST_CASE("fit: a new width, size, font or text is measured again") {
    FitCache cache;
    int measured = 0;
    const auto m = [&measured](const char* t) {
        ++measured;
        return width_of(t, 10.0f);
    };
    int font_a = 0;
    int font_b = 0;
    cache.fit(&font_a, 10.0f, "Names on blocks", 90.0f, m);
    int before = measured;
    cache.fit(&font_a, 10.0f, "Names on blocks", 91.0f, m);
    CHECK(measured > before);
    before = measured;
    cache.fit(&font_a, 12.0f, "Names on blocks", 90.0f, m);
    CHECK(measured > before);
    before = measured;
    cache.fit(&font_b, 10.0f, "Names on blocks", 90.0f, m);
    CHECK(measured > before);
    before = measured;
    cache.fit(&font_a, 10.0f, "Names on block", 90.0f, m);
    CHECK(measured > before);
    before = measured;
    cache.fit(&font_a, 10.0f, "Names on blocks", 90.0f, m); // still cached
    CHECK(measured == before);
}

TEST_CASE("fit: many labels, each cached after its first frame") {
    FitCache cache;
    int measured = 0;
    const auto m = [&measured](const char* t) {
        ++measured;
        return width_of(t, 10.0f);
    };
    std::vector<std::string> labels;
    for (int i = 0; i < 150; ++i) {
        labels.push_back("Label number " + std::to_string(i));
    }
    for (const std::string& l : labels) {
        cache.fit(nullptr, 10.0f, l, 100.0f, m);
    }
    // A second pass hits for nearly all of them (a set that holds more keys than ways may evict).
    const int after_first = measured;
    const int per_label = after_first / static_cast<int>(labels.size()) + 1;
    for (const std::string& l : labels) {
        cache.fit(nullptr, 10.0f, l, 100.0f, m);
    }
    CHECK(measured - after_first < 8 * per_label);
}

TEST_CASE("fit: text longer than the buffer is cut at a character and ends in an ellipsis") {
    FitCache cache;
    const auto m = [](const char* t) { return width_of(t, 1.0f); };
    const std::string text(kFitMax + 40, 'a');
    const FitText f = cache.fit(nullptr, 1.0f, text, 10000.0f, m);
    CHECK(f.size() <= kFitMax);
    CHECK(f.view().substr(f.size() - 3) == "\xE2\x80\xA6");
    std::string accents;
    for (std::size_t i = 0; i < kFitMax; ++i) {
        accents += "\xC3\xA9"; // two bytes each
    }
    const FitText g = cache.fit(nullptr, 1.0f, accents, 10000.0f, m);
    CHECK(g.size() <= kFitMax);
    // No half character: every byte after the first of a pair is a continuation byte.
    CHECK(by_hand(std::string(g.view()), 1.0f, 10000.0f) == std::string(g.view()));
    CHECK((static_cast<unsigned char>(g.view()[0]) & 0xE0) == 0xC0);
}

TEST_CASE("FitText: assign and append keep a terminator and stop at the buffer") {
    FitText t;
    t.assign("abc");
    t.append("def");
    CHECK(std::string(t.c_str()) == "abcdef");
    t.assign(std::string(kFitMax + 10, 'z'));
    CHECK(t.size() == kFitMax);
    t.append("more");
    CHECK(t.size() == kFitMax);
    CHECK(t.c_str()[kFitMax] == '\0');
    t.clear();
    CHECK(t.empty());
}

TEST_CASE("fit: a line exactly as wide as the room fits, one unit narrower is cut") {
    FitCache cache;
    const auto m = [](const char* t) { return width_of(t, 10.0f); };
    const FitText whole = cache.fit(nullptr, 10.0f, "abcde", 50.0f, m);
    CHECK(whole.view() == "abcde");
    const FitText cut = cache.fit(nullptr, 10.0f, "abcde", 49.0f, m);
    CHECK(cut.view() == "abc\xE2\x80\xA6");
}

TEST_CASE("fit: text of exactly the buffer size is kept, one byte more is cut with an ellipsis") {
    FitCache cache;
    const auto m = [](const char* t) { return width_of(t, 1.0f); };
    const std::string exact(kFitMax, 'a');
    CHECK(cache.fit(nullptr, 1.0f, exact, 100000.0f, m).view() == exact);
    const std::string over(kFitMax + 1, 'a');
    const FitText cut = cache.fit(nullptr, 1.0f, over, 100000.0f, m);
    CHECK(cut.size() <= kFitMax);
    CHECK(cut.view().substr(cut.size() - 3) == "\xE2\x80\xA6");
}

TEST_CASE("fit: a string longer than the buffer hits the cache on its second use") {
    FitCache cache;
    int measured = 0;
    const auto m = [&measured](const char* t) {
        ++measured;
        return width_of(t, 1.0f);
    };
    std::string title(300, 'a');
    title += " end";
    const FitText first = cache.fit(nullptr, 1.0f, title, 120.0f, m);
    const int after_first = measured;
    CHECK(after_first > 0);
    etude::test::start_counting_allocations();
    for (int i = 0; i < 50; ++i) {
        const FitText again = cache.fit(nullptr, 1.0f, title, 120.0f, m);
        CHECK(again.view() == first.view());
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    CHECK(measured == after_first);
    // Two long strings that agree in their first 255 bytes are not mistaken for each other.
    std::string other = title;
    other.back() = 'X';
    const int before_other = measured;
    cache.fit(nullptr, 1.0f, other, 120.0f, m);
    CHECK(measured > before_other);
}

TEST_CASE("FitText: a cut never splits a multi-byte character, whichever way it is made") {
    // A 2-byte (U+00E9) and a 3-byte (U+20AC) character placed so each straddles the buffer edge.
    const std::string units[] = {"\xC3\xA9", "\xE2\x82\xAC"};
    for (const std::string& unit : units) {
        for (std::size_t pad = 0; pad < 4; ++pad) {
            std::string text(kFitMax - 2 + pad, 'a');
            text += unit;
            text += unit;
            const auto valid = [](std::string_view s) {
                for (std::size_t i = 0; i < s.size();) {
                    const auto b = static_cast<unsigned char>(s[i]);
                    const std::size_t n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
                    if (b >= 0x80 && b < 0xC0) {
                        return false; // a continuation byte with no lead
                    }
                    if (i + n > s.size()) {
                        return false; // a lead byte whose character is cut short
                    }
                    for (std::size_t k = 1; k < n; ++k) {
                        if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                            return false;
                        }
                    }
                    i += n;
                }
                return true;
            };
            FitText assigned;
            assigned.assign(text);
            CHECK(valid(assigned.view()));
            FitText appended;
            appended.append(text);
            CHECK(valid(appended.view()));
            FitText formatted;
            formatted.format("%s", text.c_str());
            CHECK(valid(formatted.view()));
            CHECK(formatted.size() <= kFitMax);
            FitText ended = assigned;
            ended.end_with_ellipsis();
            CHECK(valid(ended.view()));
            CHECK(ended.view().substr(ended.size() - 3) == "\xE2\x80\xA6");
        }
    }
}

TEST_CASE("FitText: assign and append say when they cut") {
    FitText t;
    CHECK(t.assign("short"));
    CHECK(t.append(" more"));
    CHECK_FALSE(t.assign(std::string(kFitMax + 1, 'z')));
    CHECK_FALSE(t.append("x"));
    CHECK(t.size() == kFitMax);
}

TEST_CASE("fit_with_name: a long title is cut where it would be alone, the words round it stay") {
    const auto m = [](const char* t) { return width_of(t, 10.0f); };
    const std::string title = "Gymnopedie No. 1 (slow and sad)";
    const std::string prefix = "Opened ";
    const std::string suffix = ", hands split by pitch";
    const std::string line = prefix + title + suffix;
    // Whole when it fits.
    CHECK(fit_with_name(line, prefix.size(), title.size(), 1000.0f, m) == line);
    // Too long: the title is cut at its end, as the band cuts it alone in the same room.
    const float title_room = 12.0f * 10.0f; // twelve characters, the ellipsis one of them
    const float room = static_cast<float>(prefix.size() + suffix.size()) * 10.0f + title_room;
    const FitText cut = fit_with_name(line, prefix.size(), title.size(), room, m);
    FitCache cache;
    const FitText alone = cache.fit(nullptr, 10.0f, title, title_room, m);
    CHECK(cut.view() == prefix + std::string(alone.view()) + suffix);
    CHECK(std::string(alone.view()) == "Gymnopedie \xE2\x80\xA6");
    CHECK(width_of(cut.c_str(), 10.0f) <= room);
    // The words round it leave no room for the title: the line is cut at its end, as any line.
    const float tight = static_cast<float>(prefix.size()) * 10.0f;
    CHECK(fit_with_name(line, prefix.size(), title.size(), tight, m) ==
          cache.fit(nullptr, 10.0f, line, tight, m).view());
    // A span past the text, or none, is the plain cut.
    CHECK(fit_with_name(line, 500, 9, 100.0f, m) == cache.fit(nullptr, 10.0f, line, 100.0f, m).view());
    CHECK(fit_with_name(line, 0, 0, 100.0f, m) == cache.fit(nullptr, 10.0f, line, 100.0f, m).view());
}
