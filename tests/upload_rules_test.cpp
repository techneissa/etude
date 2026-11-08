// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/upload_rules.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;

namespace {

std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

} // namespace

TEST_CASE("upload names: ordinary names pass and keep their text") {
    CHECK(clean_file_name("Fur Elise.mid").name == "Fur Elise.mid");
    CHECK(clean_file_name("  spaced.mid  ").name == "spaced.mid");
    CHECK(clean_file_name("a.b.c.abc").name == "a.b.c.abc");
    CHECK(clean_file_name("Mondschein-Sonate (arr. 2).mxl").ok());
    // Names in other scripts are songs too.
    CHECK(clean_file_name("\xE6\x9C\x88\xE5\x85\x89.mid").ok());        // 月光
    CHECK(clean_file_name("Caf\xC3\xA9 \xE2\x80\x94 Take 1.mid").ok()); // é, em dash
    CHECK(clean_file_name("\xF0\x9F\x8E\xB9 piano.mid").ok());          // 🎹
}

TEST_CASE("upload names: path separators and traversal are refused, never repaired") {
    for (const char* hostile :
         {"../a.mid", "..\\a.mid", "a/../b.mid", "/etc/passwd.mid", "C:\\Users\\a.mid", "dir/a.mid",
          "a\\b.mid", "./a.mid", "....//a.mid", "%2e%2e/a.mid"}) {
        const CleanName c = clean_file_name(hostile);
        INFO(hostile);
        // "%2e%2e/a.mid" still holds a slash; none of these may come out as a name.
        CHECK_FALSE(c.ok());
        CHECK(c.name.empty());
    }
    CHECK(clean_file_name("..").problem == NameProblem::Dots);
    CHECK(clean_file_name(".").problem == NameProblem::Dots);
    CHECK(clean_file_name("../a.mid").problem == NameProblem::Separator);
    CHECK(clean_file_name("/abs.mid").problem == NameProblem::Separator);
}

TEST_CASE("upload names: hidden names, trailing dots and empty names") {
    CHECK(clean_file_name(".htaccess").problem == NameProblem::Dots);
    CHECK(clean_file_name(".incoming").problem == NameProblem::Dots);
    CHECK(clean_file_name("._song.mid").problem == NameProblem::Dots);
    CHECK(clean_file_name("song.mid.").problem == NameProblem::Dots);
    CHECK(clean_file_name("").problem == NameProblem::Empty);
    CHECK(clean_file_name("   ").problem == NameProblem::Empty);
}

TEST_CASE("upload names: NUL and control characters are refused") {
    CHECK(clean_file_name(std::string_view("a\0b.mid", 7)).problem == NameProblem::Control);
    CHECK(clean_file_name(std::string_view("a.mid\0.exe", 10)).problem == NameProblem::Control);
    CHECK(clean_file_name("a\nb.mid").problem == NameProblem::Control);
    CHECK(clean_file_name("a\tb.mid").problem == NameProblem::Control);
    CHECK(clean_file_name("a\x7f.mid").problem == NameProblem::Control);
    CHECK(clean_file_name("a\xC2\x85.mid").problem == NameProblem::Control); // U+0085 NEL
}

TEST_CASE("upload names: invisible characters that reorder or hide text are refused") {
    // U+202E RIGHT-TO-LEFT OVERRIDE makes "song\u202Eidm.exe"-style names read backwards.
    CHECK(clean_file_name("song\xE2\x80\xAE"
                          "dim.mid")
              .problem == NameProblem::Control);
    CHECK(clean_file_name("a\xE2\x80\x8B.mid").problem == NameProblem::Control); // zero width space
    CHECK(clean_file_name("a\xEF\xBB\xBF.mid").problem == NameProblem::Control); // BOM
    CHECK(clean_file_name("a\xE2\x80\xA8.mid").problem == NameProblem::Control); // line separator
    CHECK(clean_file_name("a\xC2\xAD.mid").problem == NameProblem::Control);     // soft hyphen
    CHECK(clean_file_name("a\xF3\xA0\x80\x81.mid").problem ==
          NameProblem::Control); // tag character
}

TEST_CASE("upload names: Unicode lookalikes of / \\ and . are refused") {
    CHECK(clean_file_name("a\xE2\x88\x95"
                          "b.mid")
              .problem == NameProblem::Separator); // U+2215 division slash
    CHECK(clean_file_name("a\xEF\xBC\x8F"
                          "b.mid")
              .problem == NameProblem::Separator); // U+FF0F fullwidth solidus
    CHECK(clean_file_name("a\xE2\x81\x84"
                          "b.mid")
              .problem == NameProblem::Separator); // U+2044 fraction slash
    CHECK(clean_file_name("a\xE2\xA7\xB8"
                          "b.mid")
              .problem == NameProblem::Separator); // U+29F8 big solidus
    CHECK(clean_file_name("a\xEF\xBC\xBC"
                          "b.mid")
              .problem == NameProblem::Separator); // U+FF3C fullwidth backslash
    CHECK(clean_file_name("a\xE2\x88\x96"
                          "b.mid")
              .problem == NameProblem::Separator); // U+2216 set minus
    CHECK(clean_file_name("\xE2\x80\xA4\xE2\x80\xA4"
                          "/a.mid")
              .problem == NameProblem::Dots); // ‥ before the slash is seen
    CHECK(clean_file_name("\xE2\x80\xA4\xE2\x80\xA4\xE2\x88\x95"
                          "a.mid")
              .problem == NameProblem::Dots);
    CHECK(clean_file_name("\xEF\xBC\x8E\xEF\xBC\x8E.mid").problem ==
          NameProblem::Dots); // U+FF0E fullwidth full stop
    CHECK(clean_file_name("a\xEF\xB9\x92mid").problem ==
          NameProblem::Dots); // U+FE52 small full stop
}

TEST_CASE("upload names: bad UTF-8 and over-long names are refused") {
    CHECK(clean_file_name("a\xFF.mid").problem == NameProblem::BadUtf8);
    CHECK(clean_file_name("a\xC0\xAF.mid").problem == NameProblem::BadUtf8); // overlong "/"
    CHECK(clean_file_name("a\xC0\xAE.mid").problem == NameProblem::BadUtf8); // overlong "."
    CHECK(clean_file_name("a\xE0\x80\xAF.mid").problem == NameProblem::BadUtf8);
    CHECK(clean_file_name("a\xED\xA0\x80.mid").problem == NameProblem::BadUtf8); // a surrogate
    CHECK(clean_file_name("a\xF4\x90\x80\x80.mid").problem ==
          NameProblem::BadUtf8);                                     // past U+10FFFF
    CHECK(clean_file_name("a\xC3").problem == NameProblem::BadUtf8); // cut short
    const std::string at_limit = std::string(kMaxNameBytes - 4, 'a') + ".mid";
    CHECK(at_limit.size() == kMaxNameBytes);
    CHECK(clean_file_name(at_limit).ok());
    CHECK(clean_file_name(at_limit + "x").problem == NameProblem::TooLong);
    CHECK(clean_file_name(std::string(5000, 'a') + ".mid").problem == NameProblem::TooLong);
}

TEST_CASE("upload names: characters the share cannot hold become underscores") {
    CHECK(clean_file_name("Why? (take 2): \"live\".mid").name == "Why_ (take 2)_ _live_.mid");
    CHECK(clean_file_name("a<b>c|d*e.mid").name == "a_b_c_d_e.mid");
}

TEST_CASE("upload types: the allow-list") {
    for (const char* ok :
         {"a.mid", "a.MID", "a.midi", "a.kar", "a.smf", "a.rmi", "a.musicxml", "a.xml", "a.mxl",
          "a.mscz", "a.mscx", "a.abc", "a.pdf", "a.png", "a.jpg", "a.JPEG", "a.heic", "a.heif"}) {
        INFO(ok);
        CHECK(check_upload_type(ok).ok());
    }
    for (const char* no : {"a.exe", "a.sh", "a.html", "a.svg", "a.php", "a.js", "a.zip", "a.txt",
                           "a.mid.exe", "a", "mid", "a.", "a.gif", "a.webp", "a.bmp", "a.tiff"}) {
        INFO(no);
        CHECK(check_upload_type(no).problem != NameProblem::None);
    }
    CHECK(check_upload_type("a.exe").problem == NameProblem::NotAllowed);
    CHECK(check_upload_type("a.mid.exe").problem == NameProblem::NotAllowed);
    CHECK(check_upload_type("../a.mid").problem == NameProblem::Separator);
    CHECK(*kind_of_name("a.mid") == FileKind::Song);
    CHECK(*kind_of_name("a.pdf") == FileKind::Document);
    CHECK(*kind_of_name("a.HEIC") == FileKind::Image);
    CHECK_FALSE(kind_of_name("a.txt").has_value());
    CHECK(lower_extension("A.B.MiD") == ".mid");
    CHECK(lower_extension("noext").empty());
}

TEST_CASE("upload types: the first bytes must fit the type") {
    CHECK(header_matches(".mid", bytes("MThd\0\0\0\6")));
    CHECK_FALSE(header_matches(".mid", bytes("MThx")));
    CHECK_FALSE(header_matches(".mid", bytes("MTh")));
    CHECK_FALSE(header_matches(".mid", bytes("<?xml version")));
    CHECK(header_matches(".pdf", bytes("%PDF-1.7\n")));
    CHECK_FALSE(header_matches(".pdf", bytes("MThd")));
    CHECK(header_matches(".png", bytes("\x89PNG\r\n\x1a\n....")));
    CHECK(header_matches(".jpg", bytes("\xFF\xD8\xFF\xE0")));
    CHECK(header_matches(".jpeg", bytes("\xFF\xD8\xFF\xE1")));
    CHECK_FALSE(header_matches(".jpg", bytes("\x89PNG")));
    CHECK(header_matches(".heic", bytes(std::string_view("\0\0\0\x18"
                                                         "ftypheic",
                                                         12))));
    CHECK_FALSE(header_matches(".heic", bytes("ftypheic")));
    CHECK(header_matches(".mxl", bytes("PK\x03\x04rest")));
    CHECK(header_matches(".mscz", bytes("PK\x03\x04rest")));
    CHECK_FALSE(header_matches(".mxl", bytes("<?xml")));
    CHECK(header_matches(".musicxml", bytes("<?xml version=\"1.0\"?>")));
    CHECK(header_matches(".xml", bytes("\xEF\xBB\xBF  \n<score-partwise>")));
    CHECK(header_matches(".xml", bytes("\xEF\xBB\xBF  \n<?xml version=\"1.0\"?>")));
    CHECK_FALSE(header_matches(".xml", bytes("MThd")));
    CHECK_FALSE(header_matches(".xml", bytes(std::string_view("<?xml\0", 6))));
    CHECK_FALSE(header_matches(".xml", bytes("")));
    CHECK(header_matches(".abc", bytes("X:1\nT:Tune\nK:C\n")));
    CHECK(header_matches(".abc", bytes("%abc-2.1\n% a comment\nK:G\nGABc|")));
    CHECK(header_matches(".abc", bytes("T:Tune\nX:3\nK:C\n")));
    CHECK_FALSE(header_matches(".abc", bytes("just some text, no tune")));
    CHECK_FALSE(header_matches(".abc", bytes("T:Title only\nM:4/4\n")));
    CHECK_FALSE(header_matches(".abc", bytes(" X:1\n")));         // the header starts its line
    CHECK_FALSE(header_matches(".abc", bytes("a.X:1 and aK:C"))); // not at the start of a line
    CHECK_FALSE(header_matches(".abc", bytes("X:1\nK:C\x01")));   // a control byte
    CHECK_FALSE(header_matches(".xml", bytes("<html><body>")));
    CHECK_FALSE(header_matches(".xml", bytes("<svg xmlns=\"http://www.w3.org/2000/svg\">")));
    CHECK_FALSE(header_matches(".musicxml", bytes("<museScore version=\"4\">")));
    CHECK(header_matches(".xml", bytes("<score-timewise>")));
    CHECK(header_matches(".musicxml", bytes("<score-partwise version=\"4.0\">")));
    CHECK(header_matches(".mscx", bytes("<museScore version=\"4.20\">")));
    CHECK(header_matches(".mscx", bytes("<?xml version=\"1.0\"?><museScore>")));
    CHECK_FALSE(header_matches(".mscx", bytes("<html>")));
    CHECK(sniff_window(".mid") == 12);
    CHECK(sniff_window(".pdf") == 12);
    CHECK(sniff_window(".abc") == kSniffBytes);
    CHECK(sniff_window(".xml") == kSniffBytes);
    CHECK_FALSE(header_matches(".abc", bytes(std::string_view("X:1\0", 4))));
    CHECK_FALSE(header_matches(".abc", bytes("")));
    CHECK(header_matches(".rmi", bytes(std::string_view("RIFF\0\0\0\0RMID", 12))));
    CHECK_FALSE(header_matches(".exe", bytes("MZ")));
    CHECK(kMaxUploadBytes == 32ull * 1024 * 1024);
}

TEST_CASE("relative paths: good paths are kept, hostile ones refused") {
    RelPath ok = clean_relative_path("Classical/Bach/prelude.mid", false);
    CHECK(ok.ok());
    CHECK(ok.path == "Classical/Bach/prelude.mid");
    CHECK(ok.parts.size() == 3);
    CHECK(clean_relative_path("", true).ok());
    CHECK(clean_relative_path("", true).parts.empty());
    CHECK(clean_relative_path("", false).problem == NameProblem::Empty);
    CHECK(clean_relative_path("Why? Not/a.mid", false).ok()); // an existing folder may hold '?'

    for (const char* hostile : {"/etc/passwd", "../a.mid", "a/../../b.mid", "a/..", "..", ".",
                                "a//b.mid", "a/", "a/./b.mid", "a\\..\\b.mid", ".hidden/a.mid",
                                "Inbox/.incoming/x.part", "a/.b/c.mid", "C:\\x.mid"}) {
        INFO(hostile);
        CHECK_FALSE(clean_relative_path(hostile, true).ok());
    }
    CHECK(clean_relative_path(std::string_view("a/b\0c.mid", 9), true).problem ==
          NameProblem::Control);
    CHECK(clean_relative_path("a/\xE2\x88\x95/b.mid", true).problem == NameProblem::Separator);
    CHECK(clean_relative_path("a/\xE2\x80\xA4\xE2\x80\xA4/b.mid", true).problem ==
          NameProblem::Dots);
    CHECK(clean_relative_path("a/\xFF/b.mid", true).problem == NameProblem::BadUtf8);
}

TEST_CASE("relative paths: depth and length limits are exact") {
    std::string deep = "a";
    for (std::size_t i = 1; i < kMaxDepth; ++i) {
        deep += "/a";
    }
    CHECK(clean_relative_path(deep, false).ok());
    CHECK(clean_relative_path(deep + "/a", false).problem == NameProblem::TooLong);
    CHECK(clean_relative_path(std::string(kMaxPathBytes + 1, 'a'), false).problem ==
          NameProblem::TooLong);
    CHECK(clean_relative_path(std::string(kMaxNameBytes, 'a'), false).ok());
    CHECK(clean_relative_path(std::string(kMaxNameBytes + 1, 'a'), false).problem ==
          NameProblem::TooLong);
}

TEST_CASE("json_escape: a file name cannot break out of its string or the page") {
    CHECK(json_escape("plain.mid") == "plain.mid");
    CHECK(json_escape("a\"b") == "a\\\"b");
    CHECK(json_escape("a\\b") == "a\\\\b");
    CHECK(json_escape("<script>&") == "\\u003cscript\\u003e\\u0026");
    CHECK(json_escape("a\nb\tc\r") == "a\\nb\\tc\\r");
    CHECK(json_escape(std::string_view("a\0b", 3)) == "a\\u0000b");
    CHECK(json_escape("a\x1f") == "a\\u001f");
    CHECK(json_escape("\xE2\x80\xA8") == "\\u2028");
    CHECK(json_escape("\xC3\xA9") == "\xC3\xA9"); // valid UTF-8 passes through
    CHECK(json_escape("a\xFF"
                      "b") == "a\\ufffdb"); // a stray byte cannot make the JSON invalid
}
