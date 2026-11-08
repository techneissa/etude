// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to core: what the MusicXML and MuseScore readers hand to one shared assembler, which
// builds the Song. Positions are bar index plus tick offset inside the bar, because a bar's start
// is only known once every part has been read (a bar is as long as its longest part says).
#pragma once

#include "etude/chord_spelling.hpp"
#include "etude/key_signature.hpp"
#include "etude/musicxml.hpp"
#include "etude/repeat_order.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace pugi {
class xml_document;
}

namespace etude::score_detail {

inline constexpr std::int64_t kMaxEventTicks = 1'000'000'000; // one duration or offset, at most
inline constexpr int kMaxTracks = 4000;
inline constexpr int kDefaultVelocity = 80; // mf

struct RawPart {
    std::string name;
    std::string instrument_name;
    std::string instrument_id; // MuseScore instrument id, or a MusicXML instrument-sound
    int program = -1;          // 0-based General MIDI program, -1 for none given
};

// One staff that has notes: a Song part (track).
struct RawTrack {
    int part = 0;
    std::string name;
    Hand hand = Hand::Unknown;
};

struct RawNote {
    int measure = 0;
    std::int64_t offset = 0; // ticks from the bar start
    std::int64_t length = 0; // ticks, ties already joined
    std::uint8_t pitch = 60;
    std::uint8_t velocity = 0; // 0: take it from the dynamics of the part
    std::int8_t finger = 0;
    int track = 0; // index into RawScore::tracks
    // One piece of a possibly tied note, as written. assemble_score() joins the pieces along the
    // order the bars play, so a tie only joins what plays right after it.
    bool tie_start = false;
    bool tie_stop = false;
};

struct RawMeasure {
    std::int64_t extent = 0; // longest position any part reached in this bar
    bool implicit = false;
    bool has_signature = false;
    int numerator = 4;
    int denominator = 4;
    // The key signature from this bar on, as sharps (positive) or flats (negative); only for the
    // written score (sheet music), the notes already carry their pitch.
    bool has_key = false;
    int key_fifths = 0;
};

// A key the score names: its signature and mode from bar `measure` (a written bar), `offset` ticks
// into it, for the key label. Apart from `RawMeasure::has_key`, which the sheet music writes: a
// mode changes the label, not the staff.
struct RawKey {
    int measure = 0;
    std::int64_t offset = 0;
    int fifths = 0;
    KeyMode mode = KeyMode::Major;
};

// A chord symbol the score names (MusicXML `<harmony>`) at bar `measure` (a written bar), `offset`
// ticks into it. `chord.found` is false for one that names no chord or a kind the app cannot name.
struct RawChordSymbol {
    int measure = 0;
    std::int64_t offset = 0;
    Chord chord;
    ChordSpelling spelling;
    bool keyboard = false; // written in a keyboard part; those win over another part's symbols
};

struct RawTempo {
    int part = 0;
    int measure = 0;
    std::int64_t offset = 0;
    double bpm = 120.0; // quarter notes per minute
};

struct RawDynamic {
    int part = 0;
    int measure = 0;
    std::int64_t offset = 0;
    int velocity = kDefaultVelocity;
};

struct RawScore {
    int ppq = 480;
    std::string title;
    std::string composer; // the composer the file names, may be empty
    std::vector<RawPart> parts;
    std::vector<RawTrack> tracks;
    std::vector<RawNote> notes;
    std::vector<RawMeasure> measures;
    std::vector<BarMarks> marks; // repeat signs per written bar, merged over parts; may be short
    std::vector<RawTempo> tempos;
    std::vector<RawDynamic> dynamics;
    std::vector<RawKey> keys; // in the order read; assemble_score sorts them by place
    std::vector<RawChordSymbol> chord_symbols; // in the order read; the first at a place wins
    ScoreLoadStats stats;
    std::vector<std::string> warnings; // what the reader had to guess
};

// Adds a key the score names, unless its signature has more than seven accidentals or another key
// is named at the same place already (the first part read wins). False when it was not added.
bool add_key(RawScore& raw, int measure, std::int64_t offset, long long fifths, KeyMode mode);

// Whether a part is a keyboard: a General MIDI piano, keyboard or organ program, a sound named
// keyboard, or a name such as Piano, Klavier, Organ, Harpsichord.
bool is_keyboard_part(const RawPart& part);

// Adds a chord symbol, unless another is named at the same place already: the first part read
// wins, except that a keyboard part's symbol replaces another part's. False when it was not added.
bool add_chord_symbol(RawScore& raw, int measure, std::int64_t offset, const Chord& chord,
                      const ChordSpelling& spelling, bool keyboard);

// The repeat marks of a written bar, grown to fit; null for a bar number below 0 or past
// kMaxSongMeasures.
BarMarks* marks_at(RawScore& raw, long long measure);

ScoreLoadResult make_error(ScoreFileError error, std::string message);

// Builds the Song: bar starts (in the order repeats play them), tempo map, velocities from
// dynamics, overlap repair, parts.
ScoreLoadResult assemble_score(RawScore& raw);

// The written score as the assembler saw it, for sheet music: the reader's bars (before repeats
// are expanded), each bar's length as the assembler laid it out, and whether the first is a pickup.
struct WrittenScore {
    RawScore raw;
    std::vector<std::int64_t> bar_length;
    bool pickup = false;
};

// While one is alive on a thread, assemble_score() on that thread copies the score it is given
// into `*out` (the last one assembled wins). For sheet music, which needs the written score of a
// file the loaders read the usual way.
class WrittenScoreCapture {
  public:
    explicit WrittenScoreCapture(WrittenScore* out);
    ~WrittenScoreCapture();
    WrittenScoreCapture(const WrittenScoreCapture&) = delete;
    WrittenScoreCapture& operator=(const WrittenScoreCapture&) = delete;

  private:
    WrittenScore* previous_;
};

// MusicXML (partwise, one part, one or two staves) for a written score; see sheet_source.hpp.
std::string write_musicxml(const WrittenScore& score);

// The score XML inside a zip (.mxl or .mscz) or the bytes themselves for XML. False with an error
// when there is none.
bool score_xml_bytes(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                     ScoreLoadResult& error);

// Readers. Each returns a finished result.
ScoreLoadResult score_from_musicxml(const pugi::xml_document& doc);
ScoreLoadResult score_from_mscx(const pugi::xml_document& doc);

// A name on one line: runs of white space (line breaks too) become one space, the ends are trimmed.
std::string clean_name(const char* text);

// Text to numbers without locale or exceptions. Both skip leading white space; the double reader
// takes a sign, digits and an optional fraction. False when there is no number.
bool parse_int(const char* text, long long& out);
bool parse_double(const char* text, double& out, bool first_number_in_text = false);

// Parses XML bytes. Returns false with a NotXml or TooLarge error in `error` when they are no use.
bool parse_xml_bytes(const std::uint8_t* data, std::size_t size, pugi::xml_document& doc,
                     ScoreLoadResult& error);

} // namespace etude::score_detail
