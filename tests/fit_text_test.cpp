// SPDX-License-Identifier: GPL-3.0-or-later
// FitText copies song names into a fixed buffer: it keeps a terminator, stops at the buffer and
// never splits a multi-byte character; cut_utf8 does the same for a typed name.
#include "../app/ui/fit_text.hpp"
#include "../app/ui/osk_text.hpp"

#include <doctest/doctest.h>

#include <string>
#include <string_view>

using etude::app::ui::FitCache;
using etude::app::ui::FitText;
using etude::app::ui::kFitMax;

namespace {

// A made-up font: every character is `size` wide, whatever its bytes.
float width_of(const char* text, float size) {
    float chars = 0.0f;
    for (const char* p = text; *p != '\0'; ++p) {
        if ((static_cast<unsigned char>(*p) & 0xC0) != 0x80) {
            chars += 1.0f;
        }
    }
    return chars * size;
}

} // namespace

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

TEST_CASE("cut_utf8: a name that was longer is cut at a character boundary") {
    using etude::app::ui::osk::cut_utf8;
    CHECK(cut_utf8("ab\xC3\xA9", 3) == "ab");
    CHECK(cut_utf8("ab\xC3\xA9", 4) == "ab\xC3\xA9");
    CHECK(cut_utf8("abc", 10) == "abc");
}
