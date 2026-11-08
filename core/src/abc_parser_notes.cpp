// SPDX-License-Identifier: GPL-3.0-or-later
#include "abc_parser.hpp"

#include <algorithm>

namespace etude::abc_detail {

Frac TuneParser::multiplier(Voice& v) {
    Frac m = v.broken;
    v.broken = Frac{};
    if (v.tuplet_left > 0) {
        m.num *= v.tuplet.num;
        m.den *= v.tuplet.den;
        --v.tuplet_left;
    }
    return m;
}

std::int64_t TuneParser::ticks_of(const Frac& f, const Frac& mult) const {
    const std::int64_t num = f.num * mult.num;
    const std::int64_t den = f.den * mult.den;
    if (num <= 0 || den <= 0) {
        return 0;
    }
    return std::clamp<std::int64_t>(round_div(tune_.unit_ticks * num, den), 0, kMaxNoteTicks);
}

int TuneParser::pitch_of(Voice& v, int letter, int octave, bool has_accidental, int accidental) {
    const int key = octave * 7 + letter;
    int acc = v.key.sig[static_cast<std::size_t>(letter)];
    if (has_accidental) {
        acc = accidental;
        bool found = false;
        for (auto& c : v.carry) {
            if (c.first == key) {
                c.second = accidental;
                found = true;
            }
        }
        if (!found) {
            v.carry.emplace_back(key, accidental);
        }
    } else {
        bool found = false;
        for (const auto& c : v.carry) {
            if (c.first == key) {
                acc = c.second;
                found = true;
            }
        }
        if (!found) {
            // A tie into this bar keeps the accidental of the note it ties from.
            for (const PendingTie& t : v.ties) {
                if (t.letter == letter && t.octave == octave) {
                    acc = t.accidental;
                }
            }
        }
    }
    return 12 * (octave + 1) + kLetterPitchClass[letter] + acc + v.key.shift;
}

int TuneParser::add_note(Voice& v, int pitch, std::int64_t offset, std::int64_t length) {
    if (pitch < 0 || pitch > 127) {
        ++out_of_range_;
        ++raw_.stats.unreadable_notes;
        return -1;
    }
    if (length <= 0) {
        ++raw_.stats.unreadable_notes;
        return -1;
    }
    if (raw_.notes.size() >= kMaxScoreNotes) {
        notes_capped_ = true;
        stopped_ = true;
        return -1;
    }
    if (v.track < 0) {
        RawTrack t;
        t.part = v.part;
        t.name = v.name;
        raw_.tracks.push_back(std::move(t));
        v.track = static_cast<int>(raw_.tracks.size()) - 1;
    }
    RawNote n;
    n.measure = v.bar;
    n.offset = offset;
    n.length = length;
    n.pitch = static_cast<std::uint8_t>(pitch);
    n.velocity = static_cast<std::uint8_t>(v.velocity);
    n.track = v.track;
    raw_.notes.push_back(n);
    ++v.notes;
    return static_cast<int>(raw_.notes.size()) - 1;
}

void TuneParser::place(Voice& v, std::size_t first_note, std::int64_t length) {
    v.last.valid = true;
    v.last.first_note = first_note;
    v.last.note_end = raw_.notes.size();
    v.last.bar = v.bar;
    v.last.start = v.pos;
    v.last.length = length;
    v.last.prev_max = v.max_pos;
    v.pos += length;
    v.max_pos = std::max(v.max_pos, v.pos);
    v.content = true;
}

void TuneParser::commit_bar(Voice& v) {
    if (v.content || v.max_pos > 0) {
        RawMeasure& m = measure(v.bar);
        m.extent = std::max(m.extent, v.max_pos);
    }
}

bool TuneParser::next_bar(Voice& v) {
    commit_bar(v);
    if (v.bar + 1 >= static_cast<int>(kMaxSongMeasures)) {
        bars_capped_ = true;
        stopped_ = true;
        return false;
    }
    ++v.bar;
    v.pos = 0;
    v.max_pos = 0;
    v.content = false;
    v.carry.clear();
    return true;
}

// Notes of a pitch tied to the next note of the same pitch: matches the earlier pending ties.
bool TuneParser::note_or_chord(Voice& v, std::string_view s, std::size_t& i) {
    struct Head {
        int letter = 0;
        int octave = 0;
        int accidental = 0;
        int pitch = 0;
        Frac length;
        bool tie = false;
    };
    std::vector<Head> heads;
    bool chord = false;
    if (s[i] == '[') {
        chord = true;
        ++i;
    }
    auto read_head = [&](Head& h) -> bool {
        bool has_acc = false;
        int acc = 0;
        while (i < s.size() && (s[i] == '^' || s[i] == '_' || s[i] == '=')) {
            const char c = s[i];
            has_acc = true;
            if (c == '=') {
                acc = 0;
                ++i;
            } else {
                int n = 0;
                while (i < s.size() && s[i] == c) {
                    ++n;
                    ++i;
                }
                acc = c == '^' ? std::min(n, 2) : -std::min(n, 2);
            }
        }
        if (i >= s.size() || letter_index(s[i]) < 0) {
            return false;
        }
        const char c = s[i];
        h.letter = letter_index(c);
        h.octave = (c >= 'a' && c <= 'g') ? 5 : 4;
        ++i;
        while (i < s.size() && (s[i] == '\'' || s[i] == ',')) {
            h.octave += s[i] == '\'' ? 1 : -1;
            ++i;
        }
        h.octave = std::clamp(h.octave, -2, 12);
        h.pitch = pitch_of(v, h.letter, h.octave, has_acc, acc);
        h.accidental = h.pitch - (12 * (h.octave + 1) + kLetterPitchClass[h.letter] + v.key.shift);
        h.length = read_length(s, i);
        return true;
    };
    if (chord) {
        while (i < s.size() && s[i] != ']') {
            if (s[i] == '-') {
                if (!heads.empty()) {
                    heads.back().tie = true;
                }
                ++i;
                continue;
            }
            Head h;
            const std::size_t before = i;
            if (read_head(h)) {
                heads.push_back(h);
                if (i < s.size() && s[i] == '-') {
                    heads.back().tie = true;
                    ++i;
                }
            } else {
                if (i == before) {
                    ++i; // a character the chord does not use
                }
            }
        }
        if (i < s.size() && s[i] == ']') {
            ++i;
        }
    } else {
        Head h;
        if (!read_head(h)) {
            return false;
        }
        heads.push_back(h);
    }
    Frac outer;
    if (chord) {
        outer = read_length(s, i);
    }
    bool tie_after = false;
    if (i < s.size() && s[i] == '-') {
        tie_after = true;
        ++i;
    }
    if (heads.empty()) {
        return chord;
    }
    const Frac mult = multiplier(v);
    // The element lasts as long as its first note; every note has its own length times the outer.
    std::int64_t element_length = 0;
    const std::size_t first_note = raw_.notes.size();
    std::vector<PendingTie> matches = std::move(v.ties);
    v.ties.clear();
    for (std::size_t k = 0; k < heads.size(); ++k) {
        Head& h = heads[k];
        Frac combined;
        combined.num = h.length.num * outer.num;
        combined.den = h.length.den * outer.den;
        const std::int64_t len = ticks_of(combined, mult);
        if (k == 0) {
            element_length = len;
        }
        const int idx = add_note(v, h.pitch, v.pos, len);
        if (idx < 0) {
            continue;
        }
        for (PendingTie& t : matches) {
            if (t.pitch == h.pitch) {
                raw_.notes[t.note].tie_start = true;
                raw_.notes[static_cast<std::size_t>(idx)].tie_stop = true;
                break;
            }
        }
        if (h.tie || tie_after) {
            PendingTie t;
            t.letter = h.letter;
            t.octave = h.octave;
            t.accidental = h.accidental;
            t.pitch = h.pitch;
            t.note = static_cast<std::size_t>(idx);
            v.ties.push_back(t);
        }
    }
    place(v, first_note, element_length);
    return true;
}

bool TuneParser::rest(Voice& v, std::string_view s, std::size_t& i) {
    const char c = s[i];
    ++i;
    if (c == 'Z' || c == 'X') {
        long long bars = 1;
        if (i < s.size() && is_digit(s[i])) {
            bars = std::max<long long>(read_number(s, i, kMaxRestBars), 1);
        }
        v.ties.clear();
        v.broken = Frac{};
        for (long long b = 0; b < bars && !stopped_; ++b) {
            if (b > 0 && !next_bar(v)) {
                break;
            }
            const std::size_t first = raw_.notes.size();
            place(v, first, bar_ticks());
        }
        return true;
    }
    const Frac len = read_length(s, i);
    const Frac mult = multiplier(v);
    const std::int64_t ticks = ticks_of(len, mult);
    v.ties.clear();
    place(v, raw_.notes.size(), ticks);
    return true;
}

void TuneParser::tuplet(Voice& v, std::string_view s, std::size_t& i) {
    // s[i] is the digit after '('.
    const long long p = read_number(s, i, 99);
    long long q = 0;
    long long r = p;
    if (i < s.size() && s[i] == ':') {
        ++i;
        if (i < s.size() && is_digit(s[i])) {
            q = read_number(s, i, 99);
        }
        if (i < s.size() && s[i] == ':') {
            ++i;
            if (i < s.size() && is_digit(s[i])) {
                r = read_number(s, i, 99);
            }
        }
    }
    if (p < 2) {
        return;
    }
    if (q < 1) {
        const bool compound = tune_.meter.denominator == 8 && tune_.meter.numerator >= 6 &&
                              tune_.meter.numerator % 3 == 0;
        switch (p) {
        case 2:
            q = 3;
            break;
        case 3:
            q = 2;
            break;
        case 4:
            q = 3;
            break;
        case 5:
        case 7:
        case 9:
            q = compound ? 3 : 2;
            break;
        case 6:
            q = 2;
            break;
        case 8:
            q = 3;
            break;
        default:
            q = 2;
            break;
        }
    }
    v.tuplet.num = q;
    v.tuplet.den = p;
    v.tuplet_left = std::max<long long>(r, 1);
}

void TuneParser::broken_rhythm(Voice& v, char c, std::string_view s, std::size_t& i) {
    int n = 0;
    while (i < s.size() && s[i] == c) {
        ++n;
        ++i;
    }
    n = std::min(n, 3);
    const long long d = 1LL << n;
    const bool longer_first = c == '>';
    // The element before is changed in place, when it is still the last thing in this bar.
    LastElement& last = v.last;
    if (last.valid && last.bar == v.bar && v.pos == last.start + last.length) {
        const std::int64_t len = last.length;
        const std::int64_t updated =
            longer_first ? round_div(len * (2 * d - 1), d) : round_div(len, d);
        for (std::size_t k = last.first_note; k < last.note_end && k < raw_.notes.size(); ++k) {
            RawNote& note = raw_.notes[k];
            note.length = std::clamp<std::int64_t>(
                round_div(note.length * updated, std::max<std::int64_t>(len, 1)), 1, kMaxNoteTicks);
        }
        last.length = updated;
        v.pos = last.start + updated;
        v.max_pos = std::max(last.prev_max, v.pos);
    }
    v.broken = longer_first ? Frac{1, d} : Frac{2 * d - 1, d};
}


} // namespace etude::abc_detail
