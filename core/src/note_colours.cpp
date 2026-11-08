// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/note_colours.hpp"

#include <algorithm>

#include "etude/note_style.hpp"
#include "etude/pitch.hpp"

namespace etude {

std::size_t NoteColourTable::slot(int note, std::uint8_t velocity, Hand hand) const {
    std::size_t within = 0;
    switch (scheme_) {
    case ColorScheme::ByHand:
        within = static_cast<std::size_t>(hand);
        break;
    case ColorScheme::ByPitchClass:
        within = static_cast<std::size_t>(pitch_class(note));
        break;
    case ColorScheme::ByOctave:
        within = static_cast<std::size_t>(
            std::clamp(octave(note), 0, static_cast<int>(kOctaveColorCount) - 1));
        break;
    case ColorScheme::ByVelocity:
        within = std::min<std::size_t>(velocity, 127);
        break;
    case ColorScheme::Single:
        break;
    }
    return (is_black_key(note) ? kPerKind : 0) + std::min<std::size_t>(within, kPerKind - 1);
}

NoteColourTable::NoteColourTable(const LookSettings& look) : scheme_(look.color_scheme) {
    const ContrastTargets targets = high_contrast_targets(look);
    for (std::size_t black = 0; black < 2; ++black) {
        for (std::size_t i = 0; i < kPerKind; ++i) {
            // A note, velocity and hand that land in this slot.
            int note = 60 + static_cast<int>(black);
            std::uint8_t velocity = 100;
            Hand hand = Hand::Right;
            switch (scheme_) {
            case ColorScheme::ByHand:
                hand = static_cast<Hand>(std::min<std::size_t>(i, 2));
                break;
            case ColorScheme::ByPitchClass:
                note = 60 + static_cast<int>(i % 12); // its own colour; a wrong-colour slot is unread
                break;
            case ColorScheme::ByOctave:
                note = 12 * (static_cast<int>(std::min<std::size_t>(i, kOctaveColorCount - 1)) + 1) +
                       static_cast<int>(black);
                break;
            case ColorScheme::ByVelocity:
                velocity = static_cast<std::uint8_t>(i);
                break;
            case ColorScheme::Single:
                break;
            }
            const RGBA c = note_color(look, targets, note, velocity, hand);
            const StyleRecipe r = note_recipe(look, note);
            const std::size_t at = black * kPerKind + i;
            colours_[at] = c;
            labels_[at] = label_color_for(style_surface_color(r, c, kBackgroundColor));
            key_labels_[at] = label_color_for(style_surface_color(r, c, key_underlay_color(r, c)));
        }
    }
}

} // namespace etude
