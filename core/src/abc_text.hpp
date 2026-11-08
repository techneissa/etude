// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the ABC reader: constants and the text and line helpers its parsers share.
#pragma once

#include "score_raw.hpp"
#include "etude/library.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace etude::abc_detail {

using score_detail::RawMeasure;
using score_detail::RawNote;
using score_detail::RawPart;
using score_detail::RawScore;
using score_detail::RawTempo;
using score_detail::RawTrack;

// Ticks per quarter note: divisible by 2^5, 3^2, 5 and 7, so triplets, quintuplets, septuplets and
// nonuplets of the usual note values land on whole ticks.
constexpr int kPpq = 10080;
constexpr std::int64_t kWholeTicks = std::int64_t{4} * kPpq;
constexpr std::int64_t kMaxNoteTicks = 16 * kWholeTicks;
constexpr int kMaxVoices = 64;
constexpr long long kMaxLengthPart = 1000;
constexpr long long kMaxRestBars = 1000;
constexpr int kLookahead = 48;         // longest !decoration! or +decoration+ that is looked for
constexpr int kChordSymbolOctave = 36; // MIDI note of the C that chord roots count up from (C2)
constexpr int kLeftHandVelocity = 0;   // 0: the part's dynamics, else the default
constexpr int kNoVoice = -2;           // the voice limit was passed: music is skipped

// ---- Text

inline bool is_space(char c) { return c == ' ' || c == '\t'; }
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

inline char lower_of(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (is_space(s.front()) || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (is_space(s.back()) || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

bool equals_nocase(std::string_view a, std::string_view b);

inline bool starts_with_nocase(std::string_view a, std::string_view prefix) {
    return a.size() >= prefix.size() && equals_nocase(a.substr(0, prefix.size()), prefix);
}

// Text for display: valid UTF-8 (Latin-1 otherwise), on one line.
inline std::string clean_text(std::string_view s) {
    const std::string utf8 = utf8_from_midi_text(s);
    return score_detail::clean_name(utf8.c_str());
}

// The UTF-8 byte order mark some editors put in front of a text file.
inline std::size_t bom_length(const char* text, std::size_t size) {
    return size >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                   static_cast<unsigned char>(text[1]) == 0xBB &&
                   static_cast<unsigned char>(text[2]) == 0xBF
               ? 3
               : 0;
}

// A line of the file, without its end of line.
struct LineSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t next = 0; // where the following line starts
};

LineSpan line_at(const char* text, std::size_t size, std::size_t begin);

inline bool blank_line(const char* text, const LineSpan& l) {
    for (std::size_t i = l.begin; i < l.end; ++i) {
        if (!is_space(text[i])) {
            return false;
        }
    }
    return true;
}

// A field line: a letter, a colon, and not the start of a bar line like `A:|` or `e::`.
inline bool is_field_line(std::string_view s) {
    if (s.size() < 2 || !is_letter(s[0]) || s[1] != ':') {
        return false;
    }
    if (s.size() >= 3 && (s[2] == '|' || s[2] == ':' || s[2] == ']')) {
        return false;
    }
    return true;
}

// The value of a field line: the text after the colon, a trailing comment cut off.
std::string_view field_value(std::string_view s);

inline bool x_line(const char* text, const LineSpan& l) {
    return l.end - l.begin >= 2 && text[l.begin] == 'X' && text[l.begin + 1] == ':';
}

inline long long read_number(std::string_view s, std::size_t& i, long long limit) {
    long long v = 0;
    while (i < s.size() && is_digit(s[i])) {
        v = std::min(v * 10 + (s[i] - '0'), limit);
        ++i;
    }
    return v;
}

} // namespace etude::abc_detail
