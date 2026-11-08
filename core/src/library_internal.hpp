// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of library.cpp share: the text decoding and folding (library_text.cpp), the title
// rules (library_titles.cpp) and the entries built from both (library.cpp). Not part of the
// library's interface.
#pragma once

#include <string>
#include <string_view>

namespace etude {

// ---- library_text.cpp

constexpr char32_t kReplacement = 0xFFFD;

// A white space or control byte: what ends a word in a search and in a cleaned title.
inline bool is_space_or_control(unsigned char c) { return c <= 0x20 || c == 0x7F; }

// The code points of well-formed UTF-8.
std::u32string code_points(std::string_view valid_utf8);

// ---- library_titles.cpp

// Underscores to spaces, white space collapsed, the ends trimmed, capped at kMaxLibraryTitleBytes.
std::string clean_text(std::string_view valid_utf8);

// A title that has a letter or digit of some script (not only punctuation or U+FFFD).
bool usable_title(std::string_view title);

} // namespace etude
