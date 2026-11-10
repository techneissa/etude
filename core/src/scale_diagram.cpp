// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/scale_diagram.hpp"

#include "etude/exercises.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace etude {

namespace {

bool is_black(int note) {
    switch (((note % 12) + 12) % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

void set_name(DiagramKey& key, const SpelledNote& spelled) {
    std::string name;
    name += spelled.letter;
    for (int i = 0; i < std::abs(spelled.accidental); ++i) {
        name += spelled.accidental > 0 ? '#' : 'b';
    }
    std::snprintf(key.name.data(), key.name.size(), "%s", name.c_str());
}

} // namespace

ScaleDiagram scale_diagram(int root_pc, ScaleType type) {
    const int root = ((root_pc % 12) + 12) % 12;
    const int tonic = kMiddleC + root;
    const int top = tonic + 12;
    int first = tonic;
    while (is_black(first)) {
        --first;
    }
    int last = top;
    while (is_black(last)) {
        ++last;
    }
    ScaleDiagram d;
    for (int note = first; note <= last; ++note) {
        DiagramKey key;
        key.note = note;
        key.black = is_black(note);
        key.white = d.white_keys;
        d.keys.push_back(key);
        d.white_keys += key.black ? 0 : 1;
    }
    const std::span<const int> steps = scale_intervals(type);
    const std::vector<SpelledNote> names = spell_scale(default_tonic(root, type), type);
    const std::vector<int> right = scale_fingers(root, type, Hand::Right, 1);
    const std::vector<int> left = scale_fingers(root, type, Hand::Left, 1);
    d.fingering = right.size() == steps.size() + 1 && left.size() == steps.size() + 1;
    // The notes in order, the tonic again at the top: position i is steps[i], the last is 12.
    for (std::size_t i = 0; i <= steps.size(); ++i) {
        const int note = tonic + (i < steps.size() ? steps[i] : 12);
        DiagramKey& key = d.keys[static_cast<std::size_t>(note - first)];
        key.in_scale = true;
        key.root = i == 0 || i == steps.size();
        const std::size_t degree = i < names.size() ? i : 0;
        if (!names.empty()) {
            set_name(key, names[degree]);
        }
        if (d.fingering) {
            key.right = right[i];
            key.left = left[i];
        }
    }
    return d;
}

} // namespace etude
