// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the musicxml_repeats_*test.cpp files (split from musicxml_repeats_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/musicxml.hpp"
#include "etude/midi_file.hpp"
#include "etude/repeat_order.hpp"
#include "etude/sections.hpp"
#include "etude/transport.hpp"
#include "zip_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace musicxml_repeats_test_support {

using namespace etude;

using Bytes = std::vector<std::uint8_t>;

inline Bytes bytes_of(std::string_view text) { return Bytes(text.begin(), text.end()); }

// The letter of a pitch the scores here use.
inline char letter_of(int pitch) {
    switch (pitch) {
    case 60:
        return 'C';
    case 62:
        return 'D';
    case 64:
        return 'E';
    case 65:
        return 'F';
    case 67:
        return 'G';
    case 69:
        return 'A';
    case 71:
        return 'B';
    default:
        return '?';
    }
}

// The notes in playing order as a word.
inline std::string played(const ScoreLoadResult& r) {
    std::string out;
    for (const Note& n : r.song.notes) {
        out += letter_of(n.pitch);
    }
    return out;
}

// The written bar number of each played bar.
inline std::vector<int> written(const ScoreLoadResult& r) {
    std::vector<int> out;
    for (const Measure& m : r.song.measures) {
        out.push_back(m.written_number());
    }
    return out;
}

inline std::vector<int> numbers(const ScoreLoadResult& r) {
    std::vector<int> out;
    for (const Measure& m : r.song.measures) {
        out.push_back(m.number);
    }
    return out;
}

constexpr std::string_view kHead =
    "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
    "</attributes>";

inline std::string whole(char letter) {
    return std::string("<note><pitch><step>") + letter +
           "</step><octave>4</octave></pitch><duration>4</duration><type>whole</type></note>";
}

// One bar: a whole note, with barline elements before it (left) and after it (right), and
// directions before it. The first bar carries the 4/4 attributes.
inline std::string bar(int number, char letter, std::string_view left = "", std::string_view right = "",
                std::string_view directions = "") {
    std::string out = "<measure number=\"" + std::to_string(number) + "\">";
    if (number == 1) {
        out += kHead;
    }
    out += left;
    out += directions;
    out += whole(letter);
    out += right;
    out += "</measure>";
    return out;
}

inline std::string score(std::string_view bars) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<score-partwise version=\"3.1\"><part-list><score-part id=\"P1\"><part-name>Piano"
           "</part-name></score-part></part-list><part id=\"P1\">" +
           std::string(bars) + "</part></score-partwise>";
}

inline ScoreLoadResult load(const std::string& xml) {
    const Bytes b = bytes_of(xml);
    return load_musicxml(b);
}

// The bars A, B, C, ... of a piece, each given its extra XML by `extra(i)` as {left, right,
// directions}.
struct Extra {
    std::string left;
    std::string right;
    std::string directions;
};

inline std::string piece(const std::vector<Extra>& extras) {
    std::string bars;
    for (std::size_t i = 0; i < extras.size(); ++i) {
        bars += bar(static_cast<int>(i) + 1, static_cast<char>('A' + i), extras[i].left,
                    extras[i].right, extras[i].directions);
    }
    return score(bars);
}

constexpr const char* kForward = "<barline location=\"left\"><bar-style>heavy-light</bar-style>"
                                 "<repeat direction=\"forward\"/></barline>";

constexpr const char* kRepeatBack = "<repeat direction=\"backward\"/>";

constexpr const char* kBackward = "<barline location=\"right\"><bar-style>light-heavy</bar-style>"
                                  "<repeat direction=\"backward\"/></barline>";

inline std::string ending_start(std::string_view numbers) {
    return "<barline location=\"left\"><ending number=\"" + std::string(numbers) +
           "\" type=\"start\"/></barline>";
}

inline std::string ending_stop(std::string_view numbers, std::string_view type = "stop",
                        std::string_view repeat = "") {
    return "<barline location=\"right\"><ending number=\"" + std::string(numbers) + "\" type=\"" +
           std::string(type) + "\"/>" + std::string(repeat) + "</barline>";
}

inline std::string sound(std::string_view attributes, std::string_view words = "",
                  std::string_view glyph = "") {
    std::string out = "<direction placement=\"above\">";
    if (!words.empty()) {
        out += "<direction-type><words>" + std::string(words) + "</words></direction-type>";
    }
    if (!glyph.empty()) {
        out += "<direction-type><" + std::string(glyph) + "/></direction-type>";
    }
    out += "<sound " + std::string(attributes) + "/></direction>";
    return out;
}

} // namespace musicxml_repeats_test_support
