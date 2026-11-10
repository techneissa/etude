// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace etude {

// The playable range of a physical keyboard, inclusive on both ends.
struct KeyRange {
    int lowest = 21;   // A0 on an 88-key piano
    int highest = 108; // C8

    int key_count() const { return highest - lowest + 1; }
    bool contains(int note) const { return note >= lowest && note <= highest; }
    // Both ends in 0..127, ordered, and on white keys, like every real keyboard.
    bool valid() const;
};

inline constexpr KeyRange kRange88{21, 108};
inline constexpr KeyRange kRange76{28, 103};
inline constexpr KeyRange kRange61{36, 96};
inline constexpr KeyRange kRange49{36, 84};

// A USB vendor and product id. Vendor 0 is not assigned to anyone, so {0, 0} means "none".
struct UsbId {
    std::uint16_t vendor = 0;
    std::uint16_t product = 0;

    bool operator==(const UsbId&) const = default;
};

// One known keyboard model. Matched by match_keyboard() in keyboard_identity.hpp: USB id first,
// then a model-specific name. Every entry needs a source: the maker's manual or MIDI implementation
// chart.
struct KeyboardProfile {
    std::string_view name; // shown in the UI, e.g. "Yamaha PSR-E383"
    KeyRange range;
    UsbId usb{}; // {0, 0} when the model's USB id is not known
    // Substrings that name this model and no other, e.g. "PSR-E383". Matched case-insensitively
    // on word boundaries. Generic names ("Digital Piano") are never matched. Empty = unused.
    std::array<std::string_view, 2> name_patterns{};
};

// The built-in profile table.
std::span<const KeyboardProfile> builtin_keyboard_profiles();

// The built-in profile a single port or device name names, or nullptr. Same name rules as
// match_keyboard(); use that with the full identity when it is available.
const KeyboardProfile* find_profile(std::string_view device_name);

// Black keys' length as a share of the white keys' length, measured from the back (top) of the key
// area. The keys are drawn this long, and a press in that top part of a black key's width plays
// the black key, below it the white key beside it.
inline constexpr float kBlackKeyLengthShare = 0.62f;

// Horizontal position of one key, in units where the whole keyboard is 1.0 wide.
struct KeyRect {
    float x = 0.0f;
    float width = 0.0f;
    bool black = false;
};

// How the keys and the falling blocks above them are laid out, a look setting.
//  * KeyWidth: each block is its key's width less a small inset, black keys centred on the edges
//    between white keys. A black key's block lies over the edges of the white keys' blocks beside
//    it (it is drawn over them).
//  * EvenLanes: the real piano's geometry (see KeyboardLayout): each block is its key's slot less a
//    gutter, so no two blocks ever overlap and every block is about the same width.
enum class BlockLayout : std::uint8_t { KeyWidth, EvenLanes };

// Maps notes to horizontal positions for drawing the keyboard and the falling notes above it.
//
// Slots: the real piano's geometry. The back of each octave is split into 12 nearly equal slots,
// one per key, that tile the keyboard with no gap and no overlap: the C-E group (3 white keys)
// into 5 equal slots, C, C#, D, D#, E, each 3/5 of a white key wide, and the F-B group (4 white
// keys) into 7, F, F#, G, G#, A, A#, B, each 4/7. A range that starts or ends inside a group still
// uses that group's slots, cut to the range.
//
// White keys are always a whole white key wide at the front. Black keys depend on the layout:
// with KeyWidth they are `kBlackWidthRatio` of a white key, centred on the edge between two white
// keys; with EvenLanes each black key is its slot, as on a real piano (C# left of the C-D edge, D#
// right of the D-E edge), so along the keyboard's top edge each key shows exactly its slot.
class KeyboardLayout {
  public:
    // With KeyWidth, black keys are this fraction of a white key's width.
    static constexpr float kBlackWidthRatio = 0.6f;
    // The slots of a group: the C-E group has `kCGroupSlots` over `kCGroupWhites` white keys, the
    // F-B group `kFGroupSlots` over `kFGroupWhites`.
    static constexpr int kCGroupWhites = 3;
    static constexpr int kCGroupSlots = 5;
    static constexpr int kFGroupWhites = 4;
    static constexpr int kFGroupSlots = 7;

    // A range with a black-key end widens outward to the nearest white key. A range outside
    // 0..127 or out of order falls back to 88 keys. `adjusted()` tells which happened. A layout
    // that is not one of the two is KeyWidth.
    explicit KeyboardLayout(KeyRange range, BlockLayout blocks = BlockLayout::KeyWidth);

    const KeyRange& range() const { return range_; }
    BlockLayout blocks() const { return blocks_; }
    // True when the layout's range differs from the requested one.
    bool adjusted() const { return adjusted_; }
    int white_key_count() const { return white_count_; }

    // The key's shape for drawing the keyboard and hitting it (see the class comment). Notes
    // outside the range return a zero-width rect.
    KeyRect key(int note) const;
    // The note's slot (see the class comment). Slots never overlap and tile the keyboard from the
    // first key's slot to the last key's. Notes outside the range return a zero-width rect.
    KeyRect slot(int note) const;
    // The column the note's falling block, its light and its effects use above the keyboard: its
    // `key()` with KeyWidth, its `slot()` with EvenLanes (where it is the part of the key that
    // shows along the keyboard's top edge). Everything above the keyboard takes its geometry from
    // here.
    KeyRect lane(int note) const;

  private:
    int white_index(int note) const; // white keys from `range_.lowest` up to and including `note`

    KeyRange range_;
    BlockLayout blocks_ = BlockLayout::KeyWidth;
    int white_count_ = 0;
    float white_width_ = 0.0f;
    bool adjusted_ = false;
};

} // namespace etude
