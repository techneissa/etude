// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the musicxml_*test.cpp files (split from musicxml_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/musicxml.hpp"
#include "etude/library.hpp"
#include "etude/song_builder.hpp"
#include "zip_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace musicxml_test_support {

using namespace etude;

using Bytes = std::vector<std::uint8_t>;

inline Bytes bytes_of(std::string_view text) { return Bytes(text.begin(), text.end()); }

// A partwise score with one piano part (id P1) around `measures`.
inline std::string piano_score(std::string_view measures, std::string_view part_name = "Piano") {
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<score-partwise version=\"3.1\"><part-list><score-part id=\"P1\"><part-name>";
    out += part_name;
    out += "</part-name></score-part></part-list><part id=\"P1\">";
    out += measures;
    out += "</part></score-partwise>";
    return out;
}

inline ScoreLoadResult load(const std::string& xml) {
    const Bytes b = bytes_of(xml);
    return load_musicxml(b);
}

// <note> for a pitch such as "C4" or "F#5" (b for flat), `duration` in divisions.
inline std::string note(std::string_view name, int duration, std::string_view extra = "") {
    std::string out = "<note>";
    out += std::string(extra.substr(0, extra.find('|')));
    out += "<pitch><step>";
    out += name[0];
    out += "</step>";
    int octave_at = 1;
    if (name[1] == '#') {
        out += "<alter>1</alter>";
        octave_at = 2;
    } else if (name[1] == 'b') {
        out += "<alter>-1</alter>";
        octave_at = 2;
    }
    out += "<octave>";
    out += name[static_cast<std::size_t>(octave_at)];
    out += "</octave></pitch><duration>";
    out += std::to_string(duration);
    out += "</duration>";
    out += extra.find('|') == std::string_view::npos
               ? ""
               : std::string(extra.substr(extra.find('|') + 1));
    out += "</note>";
    return out;
}

constexpr std::string_view kFourFour =
    "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
    "</attributes>";

inline std::string measure(int number, std::string_view body) {
    return "<measure number=\"" + std::to_string(number) + "\">" + std::string(body) + "</measure>";
}

} // namespace musicxml_test_support
