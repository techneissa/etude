// SPDX-License-Identifier: GPL-3.0-or-later
// The app icon's colours: eight, one for each player title (medals.hpp). Beginner's wine is the
// default; each later title unlocks the next colour, Virtuoso's gradient black last. Pure data: the
// pictures are app/assets/icon/<key>/ (built by scripts/make-icons.py), and the key is also the
// stored setting's value.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace etude {

enum class IconColour : std::uint8_t { Wine, Teal, Petrol, Cobalt, Midnight, Aubergine, Graphite, Black };

inline constexpr int kIconColourCount = 8;
inline constexpr IconColour kDefaultIconColour = IconColour::Wine;

struct IconColourInfo {
    IconColour id;
    std::string_view key;  // the folder in app/assets/icon, the stored value, the Linux icon name suffix
    std::string_view name; // "Wine" (a view of a literal: null-terminated, for drawing)
    int title;             // the title (1 Beginner .. 8 Virtuoso) that unlocks it
    // The colour's highlight when it is the one chosen, 0xRRGGBB, taken from the icon's own palette
    // (scripts/make-icons.py PLAIN and the gold of the clef): `rim` for the rim, the tint and the glow, `ink` a lighter shade for its name.
    std::uint32_t rim;
    std::uint32_t ink;
};

inline constexpr std::array<IconColourInfo, kIconColourCount> kIconColours{{
    {IconColour::Wine, "wine", "Wine", 1, 0xcf2d46, 0xf4a9b9},
    {IconColour::Teal, "teal", "Teal", 2, 0x22c4b0, 0xa6f2e4},
    {IconColour::Petrol, "petrol", "Petrol", 3, 0x4fa0a5, 0x8fd8d8},
    {IconColour::Cobalt, "cobalt", "Cobalt", 4, 0x3a62ff, 0x8fa8ff},
    {IconColour::Midnight, "midnight", "Midnight", 5, 0x6375ab, 0x9db0e6},
    {IconColour::Aubergine, "aubergine", "Aubergine", 6, 0x975f8e, 0xe0a8d8},
    {IconColour::Graphite, "graphite", "Graphite", 7, 0xcdd3e4, 0xcdd3e4},
    {IconColour::Black, "black", "Black", 8, 0xffd04a, 0xfff5c8},
}};

// The sizes of every colour's PNG set, 16 to 1024, and the ones of them the Linux icon theme takes
// (hicolor stops at 512).
inline constexpr std::array<int, 9> kIconSizes{16, 24, 32, 48, 64, 128, 256, 512, 1024};
inline constexpr std::array<int, 8> kIconHicolorSizes{16, 24, 32, 48, 64, 128, 256, 512};

inline const IconColourInfo& icon_colour_info(IconColour colour) {
    return kIconColours[static_cast<std::size_t>(colour)];
}

// The colour whose key is `key`.
std::optional<IconColour> icon_colour_from_key(std::string_view key);

// Whether a player whose best title among the device's profiles is `best_title` (1 .. 8; 0: not
// known yet) has the colour. The default one is always theirs.
bool icon_colour_unlocked(IconColour colour, int best_title);

// Whether `colour` is shown as available when the player has chosen `chosen`: the colour in use is
// always theirs, at full colour and not locked, even when the profile that earned it is gone; it is
// locked again only once they pick another one.
bool icon_colour_available(IconColour colour, IconColour chosen, int best_title);

// The Linux icon name of a colour's set: "etude-wine".
std::string icon_name(IconColour colour);

} // namespace etude
