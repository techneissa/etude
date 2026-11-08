// SPDX-License-Identifier: GPL-3.0-or-later
// A guard on what reaches the screen: a flat is "♭" and a sharp "♯", never the letter b or a number
// sign. Every core function that makes a note, chord, key or exercise name for display is run over
// every pitch class, quality, key and spelling and its text is checked; and the source of the app
// is scanned so a name builder cannot be drawn without going through `accidental_display`.
#include "etude/accidental_text.hpp"
#include "etude/chord.hpp"
#include "etude/exercises.hpp"
#include "etude/key_signature.hpp"
#include "etude/look.hpp"
#include "etude/pitch.hpp"
#include "etude/scales.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace etude;

namespace {

bool is_note_letter(char c) { return c >= 'A' && c <= 'G'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Whether `text` still holds a flat written as the letter b (after a note's letter or before a
// chord degree's number) or a sharp written as '#'.
bool plain_accidental_in(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '#') {
            return true;
        }
        if (text[i] == 'b' && ((i > 0 && (is_note_letter(text[i - 1]) || text[i - 1] == 'b')) ||
                               (i + 1 < text.size() && is_digit(text[i + 1])))) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("the guard itself: it finds a plain flat and a plain sharp") {
    CHECK(plain_accidental_in("Bb"));
    CHECK(plain_accidental_in("Ebm7b5"));
    CHECK(plain_accidental_in("C7#11"));
    CHECK(plain_accidental_in("F# minor"));
    CHECK_FALSE(plain_accidental_in("B minor"));
    CHECK_FALSE(plain_accidental_in("Bebop"));
    CHECK_FALSE(plain_accidental_in(accidental_display("Ebm7b5/Bb")));
}

TEST_CASE("chord labels: no plain flat or sharp for any root, quality or spelling") {
    struct Shape {
        const char* name;
        std::vector<int> intervals;
    };
    const std::vector<Shape> shapes{
        {"major", {0, 4, 7}},     {"minor", {0, 3, 7}},        {"dim", {0, 3, 6}},
        {"aug", {0, 4, 8}},       {"sus2", {0, 2, 7}},         {"sus4", {0, 5, 7}},
        {"7", {0, 4, 7, 10}},     {"maj7", {0, 4, 7, 11}},     {"m7", {0, 3, 7, 10}},
        {"m7b5", {0, 3, 6, 10}},  {"dim7", {0, 3, 6, 9}},      {"mMaj7", {0, 3, 7, 11}},
        {"6", {0, 4, 7, 9}},      {"m6", {0, 3, 7, 9}},        {"add9", {0, 4, 7, 14}},
        {"9", {0, 4, 7, 10, 14}}, {"maj9", {0, 4, 7, 11, 14}}, {"m9", {0, 3, 7, 10, 14}},
    };
    int named = 0;
    int with_sign = 0;
    for (const bool flats : {false, true}) {
        for (int root = 0; root < 12; ++root) {
            for (const Shape& shape : shapes) {
                for (const int inversion : {0, 1}) {
                    std::vector<int> notes;
                    for (const int iv : shape.intervals) {
                        notes.push_back(48 + root + iv);
                    }
                    if (inversion == 1) {
                        notes.front() += 12;
                    }
                    const Chord chord = detect_chord(notes);
                    const std::string text =
                        chord.found ? accidental_display(chord_name(chord, flats)) : std::string();
                    if (text.empty()) {
                        continue;
                    }
                    ++named;
                    with_sign += text.find("\xE2\x99") != std::string::npos ? 1 : 0;
                    INFO(shape.name << " on " << root << (flats ? " flats: " : " sharps: ")
                                    << text);
                    CHECK_FALSE(plain_accidental_in(text));
                }
            }
        }
    }
    CHECK(named > 200);
    CHECK(with_sign > 100); // the spellings with signs were really met
}

TEST_CASE("the half-diminished chord's flat fifth is a sign") {
    const std::vector<int> c_m7b5{60, 63, 66, 70};
    CHECK(accidental_display(chord_name(detect_chord(c_m7b5), false)) == "Cm7\xE2\x99\xAD"
                                                                         "5");
}

TEST_CASE("key labels: no plain flat or sharp in any key, mode, transposition or spelling") {
    int checked = 0;
    for (int fifths = -7; fifths <= 7; ++fifths) {
        for (int mode = 0; mode < kKeyModeCount; ++mode) {
            for (const int transpose : {-12, -5, -1, 0, 1, 3, 6, 12}) {
                for (const bool solfege : {false, true}) {
                    for (const bool flats : {false, true}) {
                        KeyChange key;
                        key.fifths = static_cast<std::int8_t>(fifths);
                        key.mode = static_cast<KeyMode>(mode);
                        key.tonic = static_cast<std::uint8_t>(((fifths * 7) % 12 + 12) % 12);
                        const std::string text =
                            key_label_display(key_label_parts(key, solfege, transpose, flats));
                        INFO(text);
                        CHECK_FALSE(plain_accidental_in(text));
                        ++checked;
                    }
                }
            }
        }
    }
    CHECK(checked > 1500);
    KeyChange b_flat;
    b_flat.fifths = -2;
    b_flat.tonic = 10;
    b_flat.mode = KeyMode::Major;
    CHECK(key_label_display(key_label_parts(b_flat, false)) == "B\xE2\x99\xAD major 2\xE2\x99\xAD");
    KeyChange f_sharp;
    f_sharp.fifths = 6;
    f_sharp.tonic = 6;
    f_sharp.mode = KeyMode::Minor;
    CHECK(key_label_display(key_label_parts(f_sharp, false, 0, false)).find("#") ==
          std::string::npos);
}

TEST_CASE("exercise scale titles carry signs, never b or #") {
    int checked = 0;
    for (int root = 0; root < 12; ++root) {
        for (const ScaleType type :
             {ScaleType::Major, ScaleType::NaturalMinor, ScaleType::Dorian}) {
            ScaleSpec spec;
            spec.root_pc = root;
            spec.type = type;
            const ExerciseResult r = make_scale_exercise(spec);
            if (!r.ok) {
                continue;
            }
            INFO(r.title);
            CHECK_FALSE(plain_accidental_in(r.title));
            ++checked;
        }
    }
    CHECK(checked > 20);
}

TEST_CASE("exercise chord-drill labels carry signs, never b or #") {
    int checked = 0;
    int with_sign = 0;
    for (const bool flats : {false, true}) {
        for (int root = 0; root < 12; ++root) {
            for (const ChordQuality quality :
                 {ChordQuality::Major, ChordQuality::Minor, ChordQuality::Dominant7,
                  ChordQuality::HalfDiminished7, ChordQuality::Minor7, ChordQuality::Major9}) {
                ChordDrillSpec spec;
                spec.prefer_flats = flats;
                spec.items.push_back({root, quality, 0});
                const ExerciseResult r = make_chord_drill(spec);
                if (!r.ok || r.steps.empty()) {
                    continue;
                }
                INFO(r.title << " / " << r.steps[0].label);
                CHECK_FALSE(plain_accidental_in(r.steps[0].label));
                CHECK_FALSE(plain_accidental_in(r.title));
                ++checked;
                with_sign += r.steps[0].label.find("\xE2\x99") != std::string::npos ? 1 : 0;
            }
        }
    }
    CHECK(checked > 100);
    CHECK(with_sign > 40);
}

TEST_CASE("exercise chord-drill errors name the chord with signs") {
    // F# sixth in first inversion reads as another chord; the message names both.
    ChordDrillSpec spec;
    spec.prefer_flats = true;
    spec.items.push_back({6, ChordQuality::Major6, 1});
    const ExerciseResult r = make_chord_drill(spec);
    REQUIRE_FALSE(r.ok);
    INFO(r.message);
    CHECK_FALSE(plain_accidental_in(r.message));
    CHECK(r.message.find("\xE2\x99") != std::string::npos);
}

namespace {

// The files of `dir` that are source (cpp, hpp), read whole, with their names.
std::vector<std::pair<std::string, std::string>> sources_in(const std::filesystem::path& dir) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        const std::string ext = e.path().extension().string();
        if (!e.is_regular_file() || (ext != ".cpp" && ext != ".hpp")) {
            continue;
        }
        std::ifstream in(e.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        out.emplace_back(e.path().filename().string(), text.str());
    }
    return out;
}

} // namespace

TEST_CASE("guard: the app never draws a name builder's plain spelling") {
    // The builders spell with b and #; a draw path wraps each in accidental_display (or draws the
    // sign as a shape from the name's parts). Each exemption below is one exact line, and each must
    // still be there: the MIDI log of midi_log_mode.cpp (a developer's terminal), the scene's store
    // of the names it compares, and the colour page's own helper, whose text is converted where it
    // is made.
    struct Exempt {
        const char* file;
        const char* line; // the line, trimmed
    };
    const std::vector<Exempt> exempt{
        {"midi_log_mode.cpp", "etude::note_name(t.note, false, g_octave_offset).c_str(),"},
        {"midi_log_mode.cpp", "etude::note_name(ev.data1, false, g_octave_offset).c_str());"},
        {"midi_log_mode.cpp",
         "m.guess >= 0 ? etude::note_name(m.guess, false, g_octave_offset).c_str()"},
        {"scene.cpp", "next.names[i] = note_label(note, settings_);"},
        {"colour_widgets_internal.hpp", "const char* note_name(const LookSettings& look, int pc);"},
        {"colour_widgets_internal.hpp", "put(inside, note_name(look, i));"},
        {"colour_widgets_internal.hpp",
         "std::snprintf(name.data(), name.size(), \"Note %s\", note_name(look, i));"},
        {"colour_widgets_palette.cpp", "const char* note_name(const LookSettings& look, int pc) {"},
    };
    std::vector<int> used(exempt.size(), 0);
    const std::vector<std::string_view> builders{"note_name(",        "note_label(",
                                                 "note_short_label(", "chord_name(",
                                                 "spelled_name(",     "key_label_text("};
    const auto trimmed = [](const std::string& l) {
        const std::size_t first = l.find_first_not_of(" \t");
        const std::size_t last = l.find_last_not_of(" \t\r");
        return first == std::string::npos ? std::string() : l.substr(first, last - first + 1);
    };
    const std::filesystem::path root = std::filesystem::path(ETUDE_SOURCE_DIR) / "app";
    int calls = 0;
    int wrapped = 0;
    for (const char* sub : {"", "ui", "sheet"}) {
        for (const auto& [name, text] : sources_in(root / sub)) {
            std::istringstream lines(text);
            std::string line;
            std::string previous;
            int number = 0;
            while (std::getline(lines, line)) {
                ++number;
                for (const std::string_view b : builders) {
                    const std::size_t at = line.find(b);
                    // A member of another name (`.note_name(`, `_note_name(`) is not a call.
                    if (at == std::string::npos ||
                        (at > 0 && (std::isalnum(static_cast<unsigned char>(line[at - 1])) != 0 ||
                                    line[at - 1] == '_'))) {
                        continue;
                    }
                    ++calls;
                    if (line.find("accidental_display") != std::string::npos ||
                        previous.find("accidental_display") != std::string::npos) {
                        ++wrapped;
                        continue;
                    }
                    bool allowed = false;
                    for (std::size_t e = 0; e < exempt.size(); ++e) {
                        if (name == exempt[e].file && trimmed(line) == exempt[e].line) {
                            ++used[e];
                            allowed = true;
                        }
                    }
                    INFO(name << ":" << number << ": " << line);
                    CHECK(allowed);
                }
                previous = line;
            }
        }
    }
    for (std::size_t e = 0; e < exempt.size(); ++e) {
        INFO("an exemption no line uses any more: " << exempt[e].file << ": " << exempt[e].line);
        CHECK(used[e] > 0);
    }
    CHECK(calls > 5);
    CHECK(wrapped > 4);
}
