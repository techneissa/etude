// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/accidental_text.hpp"

#include <array>
#include <cstddef>

namespace etude {
namespace {

bool is_letter_name(char c) { return c >= 'A' && c <= 'G'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
// A character of a name token in free text: what a chord name such as "Ebm7b5/Bb" is written with.
bool is_token_char(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '#' || c == '/';
}

// The code point that starts at `i` (0 for a stray byte) and its length.
std::size_t decode(std::string_view s, std::size_t i, char32_t& cp) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    const std::size_t n = b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : b0 >= 0xC0 ? 2 : 0;
    if (n == 0 || i + n > s.size()) {
        cp = 0;
        return 0;
    }
    cp = b0 & (0xFFu >> (n + 1));
    for (std::size_t k = 1; k < n; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
    }
    return n;
}

// Whether a code point is punctuation or a space that ends a word: dashes, quotes, guillemets,
// the ellipsis, bullets and the non-breaking space. Letters, accented ones too, are not.
bool is_boundary_cp(char32_t cp) {
    return (cp >= 0x2010 && cp <= 0x2015) || (cp >= 0x2018 && cp <= 0x201F) || cp == 0x2026 ||
           cp == 0x2022 || cp == 0x00AB || cp == 0x00BB || cp == 0x00B7 || cp == 0x00A0 ||
           cp == 0x2212 || cp == 0x266D || cp == 0x266F || (cp >= 0x3000 && cp <= 0x3003);
}

// Whether the byte at `i` (the first of a character) continues a word: a letter or digit, ASCII or
// not (an accented letter does), but not UTF-8 punctuation.
bool word_char_at(std::string_view s, std::size_t i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        return is_digit(s[i]) || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z');
    }
    char32_t cp = 0;
    return decode(s, i, cp) == 0 || !is_boundary_cp(cp);
}

// Whether the character that ends at `i` continues a word.
bool word_char_before(std::string_view s, std::size_t i) {
    if (i == 0) {
        return false;
    }
    std::size_t at = i - 1;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80) {
        --at;
    }
    return word_char_at(s, at);
}

// The length of the solfège syllable (or the letter) a name starts with at `i`; 0 for neither.
std::size_t name_length(std::string_view s, std::size_t i) {
    static constexpr std::array<std::string_view, 7> kSyllables{"Sol", "Do", "Re", "Mi",
                                                                "Fa",  "La", "Si"};
    for (const std::string_view syllable : kSyllables) {
        if (s.substr(i, syllable.size()) == syllable) {
            return syllable.size();
        }
    }
    return i < s.size() && is_letter_name(s[i]) ? 1 : 0;
}

bool is_accidental(char c) { return c == 'b' || c == '#'; }

// The accidentals after a name's letter as display text.
void append_accidentals(std::string& out, std::string_view run) {
    for (const char c : run) {
        out += c == 'b' ? kFlatSign : kSharpSign;
    }
}

} // namespace

std::string accidental_display(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    bool at_name = true;
    for (std::size_t i = 0; i < s.size();) {
        if (at_name) {
            const std::size_t n = name_length(s, i);
            if (n > 0) {
                std::size_t run = 0;
                while (i + n + run < s.size() && is_accidental(s[i + n + run])) {
                    ++run;
                }
                out.append(s.substr(i, n));
                append_accidentals(out, s.substr(i + n, run));
                i += n + run;
                at_name = false;
                continue;
            }
        }
        const char c = s[i];
        if (c == '#') {
            out += kSharpSign;
        } else if (c == 'b' && i + 1 < s.size() && is_digit(s[i + 1])) {
            out += kFlatSign;
        } else {
            out += c;
        }
        at_name = c == ' ' || c == '/' || c == '(';
        ++i;
    }
    return out;
}

namespace {

// Whether `t`, an ASCII run of token characters, is a note or chord name that holds an accidental
// to change: a letter A to G, accidentals, a quality ("m", "maj", "dim", ...), a number (an octave
// or a seventh), alterations ("b5", "#9", "#11", "b13") and a slash bass ("/D", "/G#"), and nothing
// else. "Bbq", "Abba" and "Bebop" end in letters that are none of these.
bool is_name_token(std::string_view t) {
    static constexpr std::array<std::string_view, 9> kQualities{"mMaj", "maj", "min", "dim", "aug",
                                                                "sus",  "add", "M",   "m"};
    if (t.empty() || !is_letter_name(t[0])) {
        return false;
    }
    std::size_t i = 1;
    bool changes = false;
    for (; i < t.size() && is_accidental(t[i]); ++i) {
        changes = true;
    }
    for (const std::string_view quality : kQualities) {
        if (t.substr(i, quality.size()) == quality) {
            i += quality.size();
            break;
        }
    }
    while (i < t.size() && is_digit(t[i])) {
        ++i;
    }
    while (i + 1 < t.size() && is_accidental(t[i]) && is_digit(t[i + 1])) {
        changes = true;
        i += 2;
        while (i < t.size() && is_digit(t[i])) {
            ++i;
        }
    }
    if (i < t.size() && t[i] == '/') {
        ++i;
        if (i >= t.size() || !is_letter_name(t[i])) {
            return false;
        }
        for (++i; i < t.size() && is_accidental(t[i]); ++i) {
            changes = true;
        }
    }
    return i == t.size() && changes;
}

// The length of the note or chord name that starts at `i` in free text, 0 for none: a whole word
// (not inside one, and not followed by a letter, accented ones included) that is_name_token reads.
std::size_t free_text_name(std::string_view s, std::size_t i) {
    if (!is_letter_name(s[i]) || word_char_before(s, i) || (i > 0 && is_token_char(s[i - 1]))) {
        return 0;
    }
    std::size_t end = i;
    while (end < s.size() && is_token_char(s[end])) {
        ++end;
    }
    if (end < s.size() && word_char_at(s, end)) {
        return 0;
    }
    return is_name_token(s.substr(i, end - i)) ? end - i : 0;
}

} // namespace

void accidental_display_text_into(std::string_view s, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t n = free_text_name(s, i);
        if (n == 0) {
            out += s[i];
            ++i;
            continue;
        }
        out += accidental_display(s.substr(i, n));
        i += n;
    }
}

std::string accidental_display_text(std::string_view s) {
    std::string out;
    accidental_display_text_into(s, out);
    return out;
}

bool has_accidental_spelling(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (is_letter_name(s[i]) && free_text_name(s, i) > 0) {
            return true;
        }
    }
    return false;
}

std::string plain_accidentals(std::string_view display) {
    std::string out;
    out.reserve(display.size());
    for (std::size_t i = 0; i < display.size();) {
        if (display.substr(i, kFlatSign.size()) == kFlatSign) {
            out += 'b';
            i += kFlatSign.size();
        } else if (display.substr(i, kSharpSign.size()) == kSharpSign) {
            out += '#';
            i += kSharpSign.size();
        } else {
            out += display[i];
            ++i;
        }
    }
    return out;
}

} // namespace etude
