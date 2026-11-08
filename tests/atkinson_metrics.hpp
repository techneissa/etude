// SPDX-License-Identifier: GPL-3.0-or-later
// The advance widths of the app's two fonts (Atkinson Hyperlegible Regular and Bold), in thousandths
// of an em, for the characters the UI's words use: ASCII 32 to 126 and the middle dot. A layout test
// measures a label at its real font with `text_em` instead of guessing a width. Generated once from
// the font files with Pillow (`ImageFont.truetype(file, 1000).getlength(char)`); the fonts do not
// change.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace etude::test {

inline constexpr std::array<int, 95> kBoldAdvance{
    320, 283, 410, 783, 619, 977, 702, 222, 366, 366, 434, 618,
    243, 374, 243, 442, 652, 448, 586, 605, 626, 612, 632, 543,
    625, 561, 243, 243, 573, 618, 573, 614, 754, 690, 639, 673,
    686, 580, 544, 719, 679, 458, 560, 659, 537, 842, 700, 764,
    626, 781, 654, 619, 617, 663, 656, 883, 699, 687, 621, 333,
    442, 333, 589, 430, 314, 553, 596, 520, 596, 566, 367, 597,
    572, 315, 259, 567, 314, 886, 572, 573, 596, 601, 372, 534,
    379, 570, 526, 723, 533, 518, 516, 370, 234, 370, 533,
};
inline constexpr int kBoldMiddleDot = 301;

inline constexpr std::array<int, 95> kRegularAdvance{
    280, 281, 301, 655, 594, 927, 705, 160, 300, 300, 433, 602,
    205, 368, 205, 377, 648, 402, 549, 574, 614, 583, 598, 510,
    615, 598, 205, 205, 553, 602, 553, 520, 780, 626, 619, 656,
    673, 567, 548, 710, 690, 414, 510, 627, 539, 820, 689, 727,
    601, 753, 618, 596, 558, 690, 589, 840, 623, 595, 607, 315,
    377, 315, 552, 396, 268, 526, 566, 500, 564, 539, 311, 560,
    546, 278, 254, 490, 239, 841, 546, 552, 566, 576, 340, 471,
    324, 536, 441, 654, 461, 429, 460, 319, 233, 319, 555,
};
inline constexpr int kRegularMiddleDot = 235;

// The width of `text` (UTF-8) in ems; a character the tables lack counts as the widest digit.
inline float text_em(bool bold, std::string_view text) {
    const auto& table = bold ? kBoldAdvance : kRegularAdvance;
    const int dot = bold ? kBoldMiddleDot : kRegularMiddleDot;
    int sum = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xB7) {
            sum += dot;
            ++i;
        } else if (c >= 32 && c <= 126) {
            sum += table[static_cast<std::size_t>(c - 32)];
        } else {
            sum += table['8' - 32];
        }
    }
    return static_cast<float>(sum) / 1000.0f;
}

} // namespace etude::test
