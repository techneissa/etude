// SPDX-License-Identifier: GPL-3.0-or-later
// What typing does to a field's text, for the on-screen keys and the computer keyboard alike. No
// ImGui here, so tests/osk_text_test.cpp checks it.
#pragma once

#include "tokens.hpp"

#include "etude/look.hpp"
#include "etude/sections.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace etude::app::ui::osk {

enum class FieldKind : unsigned char {
    Text,   // words: one space between them, none first
    Number, // digits only
};

struct FieldRules {
    FieldKind kind = FieldKind::Text;
    std::size_t max_bytes = static_cast<std::size_t>(osk_tok::kTextMax);
    long max_value = 0; // a number field: the largest value it takes (0: no limit); no leading zero
};

// What the core keeps of a name (UTF-8 bytes, at a character boundary): a field takes no more, so
// a long name is never cut after it was typed.
inline constexpr FieldRules kSectionNameRules{FieldKind::Text, kMaxSectionNameBytes, 0};
inline constexpr FieldRules kLookNameRules{FieldKind::Text, kMaxPresetNameBytes, 0};

// The length in bytes of the UTF-8 character that begins with `lead` (1 for a stray byte).
inline std::size_t utf8_length(unsigned char lead) {
    if (lead >= 0xF0) {
        return 4;
    }
    if (lead >= 0xE0) {
        return 3;
    }
    if (lead >= 0xC0) {
        return 2;
    }
    return 1;
}

enum class Take : unsigned char {
    Yes,  // the character goes in
    Skip, // this character does not (a control character, a letter in a number, a second space)
    Full, // nothing more fits
};

// Whether the one character `ch` (a whole UTF-8 character) would go at the end of `text`.
inline Take take(const std::string& text, std::string_view ch, const FieldRules& rules) {
    if (ch.empty()) {
        return Take::Skip;
    }
    const auto lead = static_cast<unsigned char>(ch.front());
    if (lead < 0x20 || lead == 0x7F) {
        return Take::Skip;
    }
    if (rules.kind == FieldKind::Number) {
        if (lead < '0' || lead > '9' || (lead == '0' && text.empty())) {
            return Take::Skip;
        }
        if (rules.max_value > 0) {
            long value = 0;
            for (const char c : text) {
                value = value * 10 + (c - '0');
            }
            value = value * 10 + (lead - '0');
            if (value > rules.max_value) {
                return Take::Skip; // no digit here makes a number the field takes
            }
        }
    } else if (lead == ' ' && (text.empty() || text.back() == ' ')) {
        return Take::Skip; // one space between words, none first
    }
    return text.size() + ch.size() > rules.max_bytes ? Take::Full : Take::Yes;
}

// Types `in` at the end of `text`: a character at a time, a multi-byte one whole or not at all,
// and nothing past `rules.max_bytes`. A control character is left out.
inline void type_text(std::string& text, std::string_view in, const FieldRules& rules) {
    std::size_t i = 0;
    while (i < in.size()) {
        const std::size_t n =
            std::min(utf8_length(static_cast<unsigned char>(in[i])), in.size() - i);
        const std::string_view ch = in.substr(i, n);
        i += n;
        const Take t = take(text, ch, rules);
        if (t == Take::Full) {
            break;
        }
        if (t == Take::Yes) {
            text.append(ch);
        }
    }
}

// `text` cut to at most `max_bytes`, at a character boundary.
inline std::string cut_utf8(std::string_view text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return std::string(text);
    }
    std::size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return std::string(text.substr(0, cut));
}

// Takes the last character off: a whole character, not a byte of one.
inline void erase_last(std::string& text) {
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
        text.pop_back();
    }
    if (!text.empty()) {
        text.pop_back();
    }
}

} // namespace etude::app::ui::osk
