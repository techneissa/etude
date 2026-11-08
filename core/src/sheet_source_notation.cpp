// SPDX-License-Identifier: GPL-3.0-or-later
#include "sheet_source_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace etude {

void append_int(std::string& out, long long v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%lld", v);
    out += buf;
}

void append_escaped(std::string& out, std::string_view text) {
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
}


// The alteration the key signature gives each letter.
std::array<int, 7> key_alters(int fifths) {
    constexpr int kSharpOrder[7] = {3, 0, 4, 1, 5, 2, 6};
    constexpr int kFlatOrder[7] = {6, 2, 5, 1, 4, 0, 3};
    std::array<int, 7> a{};
    fifths = std::clamp(fifths, -7, 7);
    for (int k = 0; k < std::abs(fifths); ++k) {
        a[static_cast<std::size_t>(fifths > 0 ? kSharpOrder[k] : kFlatOrder[k])] = fifths > 0 ? 1 : -1;
    }
    return a;
}


// A key's own notes keep the key's spelling; a white key outside the key is its natural; a black
// key outside it is a sharp in a sharp key (and C major), else a flat.
Spelled spell(int pitch, int fifths) {
    const std::array<int, 7> alters = key_alters(fifths);
    const int pc = ((pitch % 12) + 12) % 12;
    Spelled s;
    bool found = false;
    for (int l = 0; l < 7 && !found; ++l) {
        const int scale_pc = ((kNaturalPc[static_cast<std::size_t>(l)] + alters[static_cast<std::size_t>(l)]) % 12 + 12) % 12;
        if (scale_pc == pc) {
            s.letter = l;
            s.alter = alters[static_cast<std::size_t>(l)];
            found = true;
        }
    }
    if (!found) {
        for (int l = 0; l < 7 && !found; ++l) {
            if (kNaturalPc[static_cast<std::size_t>(l)] == pc) {
                s.letter = l;
                s.alter = 0;
                found = true;
            }
        }
    }
    if (!found) {
        const int dir = fifths >= 0 ? 1 : -1;
        for (int l = 0; l < 7; ++l) {
            if ((kNaturalPc[static_cast<std::size_t>(l)] + dir + 12) % 12 == pc) {
                s.letter = l;
                s.alter = dir;
                break;
            }
        }
    }
    const int natural = pitch - s.alter;
    s.octave = natural / 12 - 1;
    return s;
}

const char* accidental_name(int alter) {
    switch (alter) {
    case 2:
        return "double-sharp";
    case 1:
        return "sharp";
    case 0:
        return "natural";
    case -1:
        return "flat";
    default:
        return "flat-flat";
    }
}


namespace {

// Plain values (whole down to 64th, plain and dotted) that sum to `length` written ticks, largest
// first. False when they cannot (a length off the 64th-note grid).
bool plain_values(std::int64_t length, std::int64_t ppq, int actual, int normal,
                  std::vector<Value>& out) {
    std::int64_t left = length;
    while (left > 0) {
        bool placed = false;
        for (int t = 0; t < kTypeCount && !placed; ++t) {
            // 4 quarters for a whole, halved each type.
            if ((4 * ppq) % (std::int64_t{1} << t) != 0) {
                break;
            }
            const std::int64_t base = (4 * ppq) >> t;
            for (int dots = 1; dots >= 0 && !placed; --dots) {
                if (dots == 1 && (base % 2 != 0 || t + 1 >= kTypeCount)) {
                    continue;
                }
                const std::int64_t v = dots == 1 ? base + base / 2 : base;
                if (v <= left) {
                    out.push_back({v, t, dots, actual, normal, base});
                    left -= v;
                    placed = true;
                }
            }
        }
        if (!placed) {
            return false;
        }
    }
    return true;
}

} // namespace

// Values for `length` real ticks: plain, else in a tuplet of 3, 5, 7 or 9 (written longer by
// actual/normal). Falls back to the nearest 64th-note values when nothing is exact.
std::vector<Value> values_for(std::int64_t length, std::int64_t ppq) {
    std::vector<Value> out;
    if (length <= 0) {
        return out;
    }
    if (plain_values(length, ppq, 0, 0, out)) {
        return out;
    }
    constexpr std::array<std::pair<int, int>, 5> kTuplets{{{3, 2}, {6, 4}, {5, 4}, {7, 4}, {9, 8}}};
    for (const auto& [actual, normal] : kTuplets) {
        if ((length * actual) % normal != 0) {
            continue;
        }
        out.clear();
        if (plain_values(length * actual / normal, ppq, actual, normal, out)) {
            bool exact = true;
            for (Value& v : out) {
                if ((v.ticks * normal) % actual != 0) {
                    exact = false;
                    break;
                }
                v.ticks = v.ticks * normal / actual; // `nominal` stays the written value
            }
            if (exact) {
                return out;
            }
        }
    }
    out.clear();
    const std::int64_t grid = std::max<std::int64_t>(1, ppq / 16);
    const std::int64_t rounded = std::max(grid, (length + grid / 2) / grid * grid);
    plain_values(rounded, ppq, 0, 0, out);
    std::int64_t sum = 0;
    for (const Value& v : out) {
        sum += v.ticks;
    }
    // The last piece keeps the real end so the voice still adds up.
    if (!out.empty()) {
        out.back().ticks += length - sum;
        if (out.back().ticks <= 0) {
            out.pop_back();
        }
    }
    return out;
}

} // namespace etude
