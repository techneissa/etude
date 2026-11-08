// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/keyboard.hpp"

#include "etude/keyboard_identity.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace etude {

bool KeyRange::valid() const {
    return is_valid_note(lowest) && is_valid_note(highest) && lowest <= highest &&
           !is_black_key(lowest) && !is_black_key(highest);
}

namespace {

// White keys below each pitch class within its octave (a black key counts those below it).
constexpr std::array<int, 12> kWhitesBelow = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};

// Only entries with a source go here (the maker's manual or MIDI implementation chart). A model
// whose key range or names are not sourced is left to the setup wizard.
constexpr std::array kProfiles = {
    // 61 keys, 36..96: PSR-E383 Reference Manual, function 003. Yamaha's vendor id is 0499.
    // The PSR-E383's USB product id isn't known yet ({0x0499, 0x????}); `midi-ports` prints it when
    // run with the real keyboard. Until then it is matched by name only, and only if the name
    // names the model.
    KeyboardProfile{"Yamaha PSR-E383", kRange61, {}, {"PSR-E383"}},
};

} // namespace

std::span<const KeyboardProfile> builtin_keyboard_profiles() { return kProfiles; }

const KeyboardProfile* find_profile(std::string_view device_name) {
    KeyboardIdentity id;
    id.port_name = std::string(device_name);
    return match_keyboard(id).profile;
}

KeyboardLayout::KeyboardLayout(KeyRange requested, BlockLayout blocks)
    : range_(requested),
      blocks_(blocks == BlockLayout::EvenLanes ? BlockLayout::EvenLanes : BlockLayout::KeyWidth) {
    if (!range_.valid()) {
        if (is_valid_note(range_.lowest) && is_valid_note(range_.highest) &&
            range_.lowest <= range_.highest) {
            // Only a black-key end is wrong: the nearest white key outward is always one step
            // away and stays inside 0..127, because notes 0 and 127 are white.
            if (is_black_key(range_.lowest)) {
                --range_.lowest;
            }
            if (is_black_key(range_.highest)) {
                ++range_.highest;
            }
        } else {
            range_ = kRange88;
        }
    }
    adjusted_ = range_.lowest != requested.lowest || range_.highest != requested.highest;
    white_count_ = white_index(range_.highest);
    white_width_ = 1.0f / static_cast<float>(white_count_);
}

int KeyboardLayout::white_index(int note) const {
    int count = 0;
    for (int n = range_.lowest; n <= note; ++n) {
        if (!is_black_key(n)) {
            ++count;
        }
    }
    return count;
}

KeyRect KeyboardLayout::key(int note) const {
    if (!range_.contains(note)) {
        return {};
    }
    const int whites_up_to = white_index(note);
    if (!is_black_key(note)) {
        return {static_cast<float>(whites_up_to - 1) * white_width_, white_width_, false};
    }
    if (blocks_ == BlockLayout::EvenLanes) {
        return slot(note);
    }
    // A black key sits centred on the boundary after the white key below it.
    const float black_width = white_width_ * kBlackWidthRatio;
    const float boundary = static_cast<float>(whites_up_to) * white_width_;
    return {boundary - black_width / 2.0f, black_width, true};
}

KeyRect KeyboardLayout::lane(int note) const {
    return blocks_ == BlockLayout::EvenLanes ? slot(note) : key(note);
}

KeyRect KeyboardLayout::slot(int note) const {
    if (!range_.contains(note)) {
        return {};
    }
    // The key's group, counted in white keys from the first white key of the range, and its place
    // in the group. Edges are whole numbers over the slot count, computed the same way for both
    // neighbours, so a slot ends exactly where the next one begins.
    const int pc = pitch_class(note);
    const bool c_group = pc <= 4;
    const int slots = c_group ? kCGroupSlots : kFGroupSlots;
    const int whites = c_group ? kCGroupWhites : kFGroupWhites;
    const int place = c_group ? pc : pc - 5;
    // White keys from note 0's C up to this group's first key, less those below the range.
    const int group_first_white = 7 * (note / 12) + (c_group ? 0 : 3);
    const int range_first_white =
        7 * (range_.lowest / 12) +
        kWhitesBelow[static_cast<std::size_t>(pitch_class(range_.lowest))];
    const double base = static_cast<double>(group_first_white - range_first_white);
    const double left = base + static_cast<double>(whites * place) / slots;
    const double right = base + static_cast<double>(whites * (place + 1)) / slots;
    const double w = static_cast<double>(white_width_);
    const double lo = std::max(left, 0.0) * w;
    const double hi = std::min(right, static_cast<double>(white_count_)) * w;
    return {static_cast<float>(lo), static_cast<float>(std::max(hi - lo, 0.0)), is_black_key(note)};
}

} // namespace etude
