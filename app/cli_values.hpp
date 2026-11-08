// SPDX-License-Identifier: GPL-3.0-or-later
// The names the command line takes for the look's enums: a colour scheme, a key size and a hit
// effect. Each returns false (and leaves `out` alone) for a name it does not know.
#pragma once

#include "etude/hit_effects.hpp"
#include "etude/look.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace etude::app {

// Parses `text` with one of the parsers below (or any `bool (const char*, T*)` /
// `bool (std::string_view, T*)`) into `out`; false, after printing "unknown WHAT: TEXT", when it
// does not know the name.
template <class T, class Parse>
bool parse_option(Parse parse, const char* what, const char* text, T* out) {
    if (parse(text, out)) {
        return true;
    }
    std::fprintf(stderr, "unknown %s: %s\n", what, text);
    return false;
}

// Colour scheme names as typed on the command line.
inline bool parse_colors(const char* text, etude::ColorScheme* out) {
    struct Entry {
        const char* name;
        etude::ColorScheme scheme;
    };
    static const Entry kEntries[] = {
        {"hand", etude::ColorScheme::ByHand},     {"pitch", etude::ColorScheme::ByPitchClass},
        {"octave", etude::ColorScheme::ByOctave}, {"velocity", etude::ColorScheme::ByVelocity},
        {"single", etude::ColorScheme::Single},
    };
    for (const Entry& e : kEntries) {
        if (std::strcmp(text, e.name) == 0) {
            *out = e.scheme;
            return true;
        }
    }
    return false;
}

// Key size names as typed on the command line.
inline bool parse_key_size(const char* text, etude::KeySize* out) {
    struct Entry {
        const char* name;
        etude::KeySize size;
    };
    static const Entry kEntries[] = {
        {"fit-keyboard", etude::KeySize::FitKeyboard},
        {"fit-song", etude::KeySize::FitSong},
        {"auto", etude::KeySize::FitSong},
        {"small", etude::KeySize::Small},
        {"medium", etude::KeySize::Medium},
        {"large", etude::KeySize::Large},
        {"extra-large", etude::KeySize::ExtraLarge},
    };
    for (const Entry& e : kEntries) {
        if (std::strcmp(text, e.name) == 0) {
            *out = e.size;
            return true;
        }
    }
    return false;
}

// Hit effect names as typed on the command line: the labels in lower case.
inline bool parse_hit_effect(const char* text, etude::HitEffect* out) {
    for (const etude::HitEffect e : etude::kAllHitEffects) {
        const std::string_view label = etude::hit_effect_label(e);
        if (std::strlen(text) == label.size() &&
            std::equal(label.begin(), label.end(), text, [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == b;
            })) {
            *out = e;
            return true;
        }
    }
    return false;
}

} // namespace etude::app
