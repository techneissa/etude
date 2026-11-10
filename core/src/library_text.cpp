// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library.hpp"

#include "library_internal.hpp"

#include <array>
#include <cstring>

namespace etude {

namespace {

// ---- UTF-8

// Decodes one code point at `i`; returns its length in bytes, or 0 when the bytes there are not
// well-formed UTF-8.
std::size_t decode_utf8(std::string_view s, std::size_t i, char32_t& out) {
    auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    unsigned char b0 = byte(i);
    if (b0 < 0x80) {
        out = b0;
        return 1;
    }
    std::size_t length = 0;
    char32_t cp = 0;
    char32_t min = 0;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        length = 2;
        cp = b0 & 0x1Fu;
        min = 0x80;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        length = 3;
        cp = b0 & 0x0Fu;
        min = 0x800;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        length = 4;
        cp = b0 & 0x07u;
        min = 0x10000;
    } else {
        return 0;
    }
    if (i + length > s.size()) {
        return 0;
    }
    for (std::size_t k = 1; k < length; ++k) {
        unsigned char b = byte(i + k);
        if ((b & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    out = cp;
    return length;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Code points of well-formed UTF-8 text.

} // namespace

std::u32string code_points(std::string_view valid_utf8) {
    std::u32string out;
    for (std::size_t i = 0; i < valid_utf8.size();) {
        char32_t cp = kReplacement;
        std::size_t n = decode_utf8(valid_utf8, i, cp);
        out.push_back(n == 0 ? kReplacement : cp);
        i += n == 0 ? 1 : n;
    }
    return out;
}

namespace {

// ---- Legacy encodings

// Windows-1252 for 0x80..0x9F; 0 where the encoding leaves the byte undefined.
constexpr std::array<char32_t, 32> kCp1252High = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

std::string from_windows_1252(std::string_view bytes) {
    std::string out;
    for (char c : bytes) {
        unsigned char b = static_cast<unsigned char>(c);
        char32_t cp = b;
        if (b >= 0x80 && b <= 0x9F) {
            cp = kCp1252High[b - 0x80];
            if (cp == 0) {
                cp = kReplacement;
            }
        }
        append_utf8(out, cp);
    }
    return out;
}

bool sjis_lead(unsigned char b) { return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC); }
bool sjis_trail(unsigned char b) { return (b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC); }

// What a Shift-JIS pair is, when it is one of the groups core can decode without a kanji table.
char32_t decode_sjis_pair(unsigned char lead, unsigned char trail) {
    unsigned code = (static_cast<unsigned>(lead) << 8) | trail;
    if (code == 0x8140) {
        return 0x3000; // ideographic space
    }
    if (code == 0x8141) {
        return 0x3001;
    }
    if (code == 0x8142) {
        return 0x3002;
    }
    if (code == 0x815B) {
        return 0x30FC; // prolonged sound mark
    }
    if (code >= 0x824F && code <= 0x8258) {
        return 0xFF10 + (code - 0x824F); // full-width digits
    }
    if (code >= 0x8260 && code <= 0x8279) {
        return 0xFF21 + (code - 0x8260); // full-width capitals
    }
    if (code >= 0x8281 && code <= 0x829A) {
        return 0xFF41 + (code - 0x8281); // full-width small letters
    }
    if (code >= 0x829F && code <= 0x82F1) {
        return 0x3041 + (code - 0x829F); // hiragana
    }
    if (code >= 0x8340 && code <= 0x8396 && trail != 0x7F) {
        return 0x30A1 + (code - 0x8340) - (trail > 0x7F ? 1 : 0); // katakana
    }
    return 0;
}

// Decodes `bytes` as Shift-JIS when every high byte is part of it and at least one is decodable
// kana or full-width Latin; false otherwise.
bool decode_shift_jis(std::string_view bytes, std::string& out) {
    std::string result;
    bool evidence = false;
    for (std::size_t i = 0; i < bytes.size();) {
        unsigned char b = static_cast<unsigned char>(bytes[i]);
        if (b < 0x80) {
            result.push_back(static_cast<char>(b));
            ++i;
        } else if (b >= 0xA1 && b <= 0xDF) {
            append_utf8(result, 0xFF61 + (b - 0xA1u)); // half-width katakana
            evidence = true;
            ++i;
        } else if (sjis_lead(b) && i + 1 < bytes.size() &&
                   sjis_trail(static_cast<unsigned char>(bytes[i + 1]))) {
            char32_t cp = decode_sjis_pair(b, static_cast<unsigned char>(bytes[i + 1]));
            evidence = evidence || cp != 0;
            append_utf8(result, cp != 0 ? cp : kReplacement);
            i += 2;
        } else {
            return false;
        }
    }
    if (!evidence) {
        return false;
    }
    out = std::move(result);
    return true;
}

// ---- Search folding

// For U+00C0..U+00FF: the folded letter; '0' keeps the character, '1' is "ae", '2' "th", '3' "ss".
constexpr std::string_view kLatin1Fold = "aaaaaa1ceeeeiiiidnooooo0ouuuuy23"
                                         "aaaaaa1ceeeeiiiidnooooo0ouuuuy2y";
// For U+0100..U+017F; '4' is "oe", '5' is "ij".
constexpr std::string_view kLatinAFold = "aaaaaa"       // 0100-0105
                                         "cccccccc"     // 0106-010D
                                         "dddd"         // 010E-0111
                                         "eeeeeeeeee"   // 0112-011B
                                         "gggggggg"     // 011C-0123
                                         "hhhh"         // 0124-0127
                                         "iiiiiiiiii"   // 0128-0131
                                         "55"           // 0132-0133
                                         "jj"           // 0134-0135
                                         "kkk"          // 0136-0138
                                         "llllllllll"   // 0139-0142
                                         "nnnnnnnnn"    // 0143-014B
                                         "oooooo"       // 014C-0151
                                         "44"           // 0152-0153
                                         "rrrrrr"       // 0154-0159
                                         "ssssssss"     // 015A-0161
                                         "tttttt"       // 0162-0167
                                         "uuuuuuuuuuuu" // 0168-0173
                                         "ww"           // 0174-0175
                                         "yyy"          // 0176-0178
                                         "zzzzzz"       // 0179-017E
                                         "s";           // 017F
static_assert(kLatin1Fold.size() == 64);
static_assert(kLatinAFold.size() == 128);

void append_folded_letter(std::string& out, char code, char32_t original) {
    switch (code) {
    case '0':
        append_utf8(out, original);
        break;
    case '1':
        out += "ae";
        break;
    case '2':
        out += "th";
        break;
    case '3':
        out += "ss";
        break;
    case '4':
        out += "oe";
        break;
    case '5':
        out += "ij";
        break;
    default:
        out.push_back(code);
        break;
    }
}

void append_folded(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(cp >= 'A' && cp <= 'Z' ? static_cast<char>(cp + 32) : static_cast<char>(cp));
    } else if (cp >= 0xC0 && cp <= 0xFF) {
        append_folded_letter(out, kLatin1Fold[cp - 0xC0], cp);
    } else if (cp >= 0x100 && cp <= 0x17F) {
        append_folded_letter(out, kLatinAFold[cp - 0x100], cp);
    } else if (cp >= 0x300 && cp <= 0x36F) {
        // combining mark: dropped
    } else if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) {
        append_utf8(out, cp + 0x20); // Greek capitals
    } else if (cp >= 0x410 && cp <= 0x42F) {
        append_utf8(out, cp + 0x20); // Cyrillic capitals
    } else if (cp >= 0x400 && cp <= 0x40F) {
        append_utf8(out, cp + 0x50);
    } else if (cp >= 0xFF21 && cp <= 0xFF3A) {
        out.push_back(static_cast<char>('a' + (cp - 0xFF21)));
    } else if (cp >= 0xFF41 && cp <= 0xFF5A) {
        out.push_back(static_cast<char>('a' + (cp - 0xFF41)));
    } else if (cp >= 0xFF10 && cp <= 0xFF19) {
        out.push_back(static_cast<char>('0' + (cp - 0xFF10)));
    } else if (cp == 0x266D) {
        out.push_back('b'); // the flat sign: a search for "B-flat sign" and for "Bb" is one search
    } else if (cp == 0x266F) {
        out.push_back('#'); // the sharp sign
    } else if (cp == kReplacement) {
        // dropped
    } else {
        append_utf8(out, cp);
    }
}

} // namespace

// ---- Public text functions

bool is_valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        char32_t cp = 0;
        std::size_t n = decode_utf8(text, i, cp);
        if (n == 0) {
            return false;
        }
        i += n;
    }
    return true;
}

std::string utf8_from_midi_text(std::string_view bytes) {
    if (is_valid_utf8(bytes)) {
        return std::string(bytes);
    }
    std::string decoded;
    if (decode_shift_jis(bytes, decoded)) {
        return decoded;
    }
    return from_windows_1252(bytes);
}

std::string fold_for_search(std::string_view text) {
    std::string valid = utf8_from_midi_text(text);
    std::string out;
    for (char32_t cp : code_points(valid)) {
        append_folded(out, cp);
    }
    return out;
}

void fold_for_search_into(std::string_view text, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < text.size();) {
        char32_t cp = 0;
        const std::size_t n = decode_utf8(text, i, cp);
        if (n == 0) {
            append_folded(out, static_cast<unsigned char>(text[i])); // a stray byte, as Latin-1
            ++i;
        } else {
            append_folded(out, cp);
            i += n;
        }
    }
}

} // namespace etude
