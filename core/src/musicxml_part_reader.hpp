// SPDX-License-Identifier: GPL-3.0-or-later
// Internal to the MusicXML reader: reads one part's measures into the raw score.
#pragma once

#include "musicxml_support.hpp"

namespace etude::musicxml_detail {

class PartReader {
  public:
    // `divisions`: units to the quarter note until the part says otherwise (0: 1).
    PartReader(RawScore& raw, int part_index, int ppq, long long divisions)
        : raw_(raw), part_(part_index), ppq_(ppq), divisions_(std::max<long long>(divisions, 1)) {}

    // Returns false when the score is too large.
    bool read(const std::vector<MeasureRef>& measures);

  private:
    std::int64_t ticks(long long divisions_value) const {
        // divisions_value / divisions_ quarter notes, in ticks, rounded.
        const long long d = std::clamp<long long>(divisions_value, -kMaxEventTicks, kMaxEventTicks);
        const std::int64_t num = d * ppq_;
        const std::int64_t den = divisions_;
        const std::int64_t half = den / 2;
        const std::int64_t t = num >= 0 ? (num + half) / den : -((-num + half) / den);
        return std::clamp<std::int64_t>(t, -kMaxEventTicks, kMaxEventTicks);
    }

    bool duration_ticks(const pugi::xml_node& parent, std::int64_t& out) const {
        long long v = 0;
        if (!parse_int(parent.child_value("duration"), v)) {
            return false;
        }
        out = ticks(v);
        return true;
    }

    bool read_measure(const MeasureRef& ref, int index);

    // Repeat barlines and ending brackets. A barline on the left edge belongs to this bar's start,
    // one on the right edge to its end; a sign on the other edge belongs to the neighbouring bar.
    void read_barline(const pugi::xml_node& barline, int index);

    // The song's key from an `<attributes>`: its `<key>` (`<fifths>` and `<mode>`) from where it
    // stands in the bar, in concert pitch (a transposing part writes its own signature, and
    // `transpose_` moves it). An `<attributes>` may hold a `<key>` for each staff (`number`): the
    // one with no number, which is for every staff, wins, else the lowest number, so the first
    // staff's. A key made of single steps (`<key-step>`) has no signature and is left; so is any
    // whose signature, written or concert, has more than seven accidentals.
    void read_key(const pugi::xml_node& attrs, int index);

    void read_attributes(const pugi::xml_node& attrs, RawMeasure& bar, int index);

    // A chord symbol (`<harmony>`): the root, the kind, an optional bass, at the bar position (and
    // `<offset>`). One whose kind this app cannot name, "none" (N.C.), or with no letter root (a
    // numeral) is kept as a symbol that names no chord: it ends the chord before it.
    void read_harmony(const pugi::xml_node& harmony, int index);

    void read_direction(const pugi::xml_node& dir, int index);

    // Segno and coda signs and the words D.C., D.S., To Coda and Fine, for a direction whose
    // `<sound>` does not already say it. A coda sign beside the words "To Coda" is the jump, not
    // the place it goes to.
    void read_jump_signs(const pugi::xml_node& dir, const pugi::xml_node& sound, int index);

    enum class Jump : std::uint8_t { None, DaCapo, DalSegno, ToCoda, Fine };

    // What a direction's words say, if they are a jump. Matches the whole words, not a part of a
    // longer sentence: "D.C. al Fine" is a D.C., not a Fine.
    static Jump jump_of_words(const char* text);

    static bool read_metronome(const pugi::xml_node& m, double& bpm);

    // Jumps and the places they go to, from the attributes of `<sound>`.
    void read_sound_jumps(const pugi::xml_node& sound, int index);

    void read_sound(const pugi::xml_node& sound, int index, std::int64_t position);

    bool read_note(const pugi::xml_node& note, int index, std::int64_t& extent);

    // Turns the staff numbers of this part's notes into tracks, one per staff with notes.
    void finish_tracks();

    RawScore& raw_;
    int part_;
    int ppq_;
    long long divisions_;
    int transpose_ = 0;
    int staves_ = 1;
    int max_staff_ = 1;
    std::int64_t pos_ = 0;
    std::int64_t last_start_ = 0;
    std::size_t first_note_ = 0;
};

} // namespace etude::musicxml_detail
