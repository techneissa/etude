// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the ABC reader: the per-tune parser. Its members are defined in the abc_parser_*.cpp
// files.
#pragma once

#include "abc_fields.hpp"
#include "abc_text.hpp"
#include "score_raw.hpp"
#include "etude/abc.hpp"
#include "etude/song_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace etude::abc_detail {

struct PendingTie {
    int letter = 0;
    int octave = 0;
    int accidental = 0;
    int pitch = 0;
    std::size_t note = 0;
};

struct LastElement {
    bool valid = false;
    std::size_t first_note = 0;
    std::size_t note_end = 0;
    int bar = 0;
    std::int64_t start = 0;
    std::int64_t length = 0;
    std::int64_t prev_max = 0;
};

struct Voice {
    std::string id;
    std::string name;
    KeyInfo key;
    int part = 0;
    int track = -1;
    int bar = 0;
    std::int64_t pos = 0;
    std::int64_t max_pos = 0;
    bool content = false; // something was placed in the current bar
    int notes = 0;
    int velocity = 0;
    std::vector<std::pair<int, int>> carry; // (octave * 7 + letter, accidental) in this bar
    std::vector<PendingTie> ties;
    LastElement last;
    long long tuplet_left = 0;
    Frac tuplet; // q/p
    Frac broken; // multiplier of the next element
    bool ending_open = false;
};

// What the tune header and body say, shared by every voice.
struct Tune {
    std::string title;
    std::string composer;
    Meter meter;
    bool unit_given = false;
    std::int64_t unit_ticks = kWholeTicks / 8;
    KeyInfo key;
    bool header_done = false;
};

class TuneParser {
  public:
    explicit TuneParser(const AbcOptions& options) : options_(options) {}

    ScoreLoadResult run(const char* text, const AbcTune& tune);

  private:
    // Fields
    void field(char name, std::string_view value, std::size_t line);
    void select_voice(std::string_view value);
    Voice* ensure_voice();
    void end_header();

    // Music
    void music_line(std::string_view s, std::size_t line);
    bool note_or_chord(Voice& v, std::string_view s, std::size_t& i);
    bool rest(Voice& v, std::string_view s, std::size_t& i);
    void tuplet(Voice& v, std::string_view s, std::size_t& i);
    void broken_rhythm(Voice& v, char c, std::string_view s, std::size_t& i);
    void bar_line(Voice& v, std::string_view s, std::size_t& i);
    void ending_marker(Voice& v, std::string_view s, std::size_t& i, bool after_bar);
    void decoration(Voice& v, std::string_view name);
    void chord_text(Voice& v, std::string_view text);
    void grace(std::string_view s, std::size_t& i);

    // Placing
    Frac multiplier(Voice& v);
    std::int64_t ticks_of(const Frac& f, const Frac& mult) const;
    int pitch_of(Voice& v, int letter, int octave, bool has_accidental, int accidental);
    int add_note(Voice& v, int pitch, std::int64_t offset, std::int64_t length);
    void place(Voice& v, std::size_t first_note, std::int64_t length);
    void commit_bar(Voice& v);
    bool next_bar(Voice& v);
    int bar_for_marks(const Voice& v) const { return v.content ? v.bar : std::max(0, v.bar - 1); }
    score_detail::RawMeasure& measure(int bar);
    std::int64_t bar_ticks() const;
    void set_meter_here(Voice& v);
    void skipped(std::size_t line) {
        if (skipped_ == 0) {
            first_skipped_line_ = line;
        }
        ++skipped_;
    }
    void warn(std::string text) { raw_.warnings.push_back(std::move(text)); }

    ScoreLoadResult finish();

    const AbcOptions& options_;
    RawScore raw_;
    Tune tune_;
    std::vector<Voice> voices_;
    int voice_ = -1;
    std::vector<ChordSymbol> chords_;
    std::size_t line_ = 0;
    std::size_t skipped_ = 0;
    std::size_t first_skipped_line_ = 0;
    bool stopped_ = false;
    bool notes_capped_ = false;
    bool bars_capped_ = false;
    int out_of_range_ = 0;
    bool voices_capped_ = false;
};

} // namespace etude::abc_detail
