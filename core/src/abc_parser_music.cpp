// SPDX-License-Identifier: GPL-3.0-or-later
#include "abc_parser.hpp"

#include <algorithm>
#include <cstring>

namespace etude::abc_detail {

void TuneParser::ending_marker(Voice& v, std::string_view s, std::size_t& i, bool after_bar) {
    // s[i] is the first digit of the ending numbers: "1", "1,2", "1-3".
    std::string text;
    while (i < s.size() && (is_digit(s[i]) || s[i] == ',' || s[i] == '-') && text.size() < 24) {
        text += s[i];
        ++i;
    }
    if (!after_bar && v.content) {
        // An ending bracket starts a bar of its own.
        if (!next_bar(v)) {
            return;
        }
    }
    const std::uint64_t bits = parse_ending_numbers(text.c_str());
    if (bits == 0) {
        return;
    }
    if (BarMarks* m = score_detail::marks_at(raw_, v.bar)) {
        m->endings |= bits;
        v.ending_open = true;
    }
}

void TuneParser::bar_line(Voice& v, std::string_view s, std::size_t& i) {
    const std::size_t start = i;
    if (s[i] == '[' && i + 1 < s.size() && s[i + 1] == '|') {
        ++i;
    }
    while (i < s.size() && (s[i] == '|' || s[i] == ':')) {
        ++i;
    }
    if (i < s.size() && s[i] == ']' && s[i - 1] == '|') {
        ++i;
    }
    const std::string_view token = s.substr(start, i - start);
    const bool back = !token.empty() && token.front() == ':';
    const bool forward = token.size() > 1 && token.back() == ':';
    const bool thick = token.find("||") != std::string_view::npos ||
                       token.find("|]") != std::string_view::npos ||
                       token.find("[|") != std::string_view::npos;
    const bool has_bar = token.find('|') != std::string_view::npos;
    if (!has_bar && !back && !forward) {
        skipped(line_);
        return;
    }
    if (!has_bar) {
        // "::" is a repeat sign with no bar between its colons.
        if (token.size() < 2) {
            skipped(line_);
            return;
        }
    }
    // ":|1" and ":|[1" open a first ending after a plain bar line, as "|1" does: that colon-bar is
    // no repeat sign, the first ending's own ":|" closes the repeat. A ":|2" after an open ending
    // is the usual closing one.
    bool opens_first_ending = false;
    if (back && !forward && !v.ending_open) {
        std::size_t j = i;
        if (j + 1 < s.size() && s[j] == '[' && is_digit(s[j + 1])) {
            ++j;
        }
        if (j < s.size() && is_digit(s[j])) {
            std::string text;
            while (j < s.size() && (is_digit(s[j]) || s[j] == ',' || s[j] == '-') &&
                   text.size() < 24) {
                text += s[j++];
            }
            opens_first_ending = (parse_ending_numbers(text.c_str()) & 2u) != 0;
        }
    }
    const int ended = bar_for_marks(v);
    if (back && !opens_first_ending && (v.content || v.bar > 0)) {
        if (BarMarks* m = score_detail::marks_at(raw_, ended)) {
            m->backward_repeat = true;
        }
        v.ending_open = false;
    } else if (thick && v.ending_open) {
        if (BarMarks* m = score_detail::marks_at(raw_, ended)) {
            m->ending_stops = true;
        }
        v.ending_open = false;
    }
    if (v.content && !next_bar(v)) {
        return;
    }
    if (forward) {
        if (BarMarks* m = score_detail::marks_at(raw_, v.bar)) {
            m->forward_repeat = true;
        }
    }
    if (i < s.size() && is_digit(s[i])) {
        ending_marker(v, s, i, true);
    }
}

void TuneParser::decoration(Voice& v, std::string_view name) {
    struct Dynamic {
        std::string_view name;
        int velocity;
    };
    static constexpr Dynamic kDynamics[] = {{"ppp", 16}, {"pp", 33},   {"p", 49},
                                            {"mp", 64},  {"mf", 80},   {"f", 96},
                                            {"ff", 112}, {"fff", 126}, {"sfz", 112}};
    for (const Dynamic& d : kDynamics) {
        if (name == d.name) {
            v.velocity = d.velocity;
            return;
        }
    }
    const int before = bar_for_marks(v);
    BarMarks* ended = score_detail::marks_at(raw_, before);
    BarMarks* here = score_detail::marks_at(raw_, v.bar);
    if (ended == nullptr || here == nullptr) {
        return;
    }
    if (equals_nocase(name, "D.C.") || equals_nocase(name, "dacapo")) {
        ended->dacapo = true;
    } else if (equals_nocase(name, "D.S.") || equals_nocase(name, "dalsegno")) {
        ended->dalsegno = true;
    } else if (equals_nocase(name, "segno")) {
        here->segno = true;
    } else if (equals_nocase(name, "coda")) {
        here->coda = true;
    } else if (equals_nocase(name, "dacoda")) {
        ended->tocoda = true;
    } else if (equals_nocase(name, "fine")) {
        ended->fine = true;
    }
}

void TuneParser::chord_text(Voice& v, std::string_view text) {
    if (text.empty() || options_.chords_as_left_hand == false) {
        return;
    }
    if (std::strchr("^_<>@", text[0]) != nullptr) {
        return; // an annotation placed above or below the staff
    }
    ChordSymbol c;
    if (!parse_chord_symbol(text, c)) {
        return;
    }
    c.bar = v.bar;
    c.pos = v.pos;
    chords_.push_back(c);
}

void TuneParser::grace(std::string_view s, std::size_t& i) {
    // s[i] is '{'.
    const std::size_t close = s.find('}', i);
    const std::size_t stop = close == std::string_view::npos ? s.size() : close;
    for (std::size_t k = i + 1; k < stop; ++k) {
        if (letter_index(s[k]) >= 0) {
            ++raw_.stats.grace_notes_dropped;
        }
    }
    i = close == std::string_view::npos ? s.size() : close + 1;
}

void TuneParser::music_line(std::string_view s, std::size_t line) {
    line_ = line;
    ensure_voice();
    Voice* vp = nullptr;
    std::size_t i = 0;
    while (i < s.size() && !stopped_) {
        // The voice can change in the middle of the line with an inline [V:..].
        vp = voice_ >= 0 ? &voices_[static_cast<std::size_t>(voice_)] : nullptr;
        const char c = s[i];
        if (is_space(c) || c == '\\' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (c == '%') {
            return;
        }
        if (c == '[' && i + 2 < s.size() && is_letter(s[i + 1]) && s[i + 2] == ':') {
            const std::size_t close = s.find(']', i);
            const std::size_t stop = close == std::string_view::npos ? s.size() : close;
            field(s[i + 1], trim(s.substr(i + 3, stop - i - 3)), line);
            i = close == std::string_view::npos ? s.size() : close + 1;
            continue;
        }
        if (vp == nullptr) {
            // Music of a voice that was left out: skip the line.
            return;
        }
        Voice& v = *vp;
        switch (c) {
        case '"': {
            const std::size_t close = s.find('"', i + 1);
            if (close == std::string_view::npos) {
                skipped(line);
                i = s.size();
            } else {
                chord_text(v, s.substr(i + 1, close - i - 1));
                i = close + 1;
            }
            continue;
        }
        case '!': {
            const std::size_t close = s.find('!', i + 1);
            if (close != std::string_view::npos && close - i <= kLookahead) {
                decoration(v, s.substr(i + 1, close - i - 1));
                i = close + 1;
            } else {
                ++i;
            }
            continue;
        }
        case '+': {
            const std::size_t close = s.find('+', i + 1);
            if (close != std::string_view::npos && close - i <= kLookahead) {
                decoration(v, s.substr(i + 1, close - i - 1));
                i = close + 1;
            } else {
                ++i;
            }
            continue;
        }
        case '{':
            grace(s, i);
            continue;
        case '(':
            ++i;
            if (i < s.size() && is_digit(s[i])) {
                tuplet(v, s, i);
            }
            continue;
        case ')':
        case '.':
        case '~':
        case 'H':
        case 'L':
        case 'M':
        case 'O':
        case 'P':
        case 'S':
        case 'T':
        case 'u':
        case 'v':
        case '*':
        case '$':
        case '`':
        case '\'':
            ++i;
            continue;
        case '>':
        case '<':
            broken_rhythm(v, c, s, i);
            continue;
        case '-':
            ++i;
            // A tie written after a space still ties the note just before it.
            if (v.last.valid && v.ties.empty() && v.pos == v.last.start + v.last.length) {
                for (std::size_t k = v.last.first_note;
                     k < v.last.note_end && k < raw_.notes.size(); ++k) {
                    PendingTie t;
                    t.letter = -1; // the accidental carries over only from a tie written as usual
                    t.pitch = raw_.notes[k].pitch;
                    t.note = k;
                    v.ties.push_back(t);
                }
            }
            continue;
        case '&':
            ++i;
            v.pos = 0;
            v.last.valid = false;
            v.ties.clear();
            continue;
        case 'y':
            ++i;
            if (i < s.size() && is_digit(s[i])) {
                read_number(s, i, 1000);
            }
            continue;
        case 'z':
        case 'x':
        case 'Z':
        case 'X':
            rest(v, s, i);
            continue;
        case '|':
        case ':':
            bar_line(v, s, i);
            continue;
        case ']':
            ++i;
            continue;
        case '[':
            if (i + 1 < s.size() && s[i + 1] == '|') {
                bar_line(v, s, i);
            } else if (i + 1 < s.size() && is_digit(s[i + 1])) {
                ++i;
                ending_marker(v, s, i, false);
            } else if (!note_or_chord(v, s, i)) {
                skipped(line);
                ++i;
            }
            continue;
        default:
            break;
        }
        if (letter_index(c) >= 0 || c == '^' || c == '_' || c == '=') {
            const std::size_t before = i;
            if (!note_or_chord(v, s, i)) {
                skipped(line);
                if (i == before) {
                    ++i;
                }
            }
            continue;
        }
        if (!is_digit(c)) {
            skipped(line);
        }
        ++i;
    }
}

ScoreLoadResult TuneParser::run(const char* text, const AbcTune& tune) {
    if (tune.number_replaced) {
        warn("the X: number is too large; the tune is addressed by its place in the file, " +
             tune.address);
    }
    if (tune.more_tunes_follow) {
        warn("Only the first " + std::to_string(kMaxAbcTunes) + " tunes of the file are listed");
    }
    // The file header sets the defaults of every tune in the file.
    std::size_t at = bom_length(text, tune.header_end);
    std::size_t line = 1;
    while (at < tune.header_end) {
        const LineSpan l = line_at(text, tune.header_end, at);
        const std::string_view s(text + l.begin, l.end - l.begin);
        if (is_field_line(s)) {
            const char name = s[0];
            if (name == 'L' || name == 'M' || name == 'Q' || name == 'K') {
                field(name, field_value(s), line);
            }
        }
        at = l.next;
        ++line;
    }
    // A K: in the file header only sets the default key; the tune header is still to come.
    tune_.header_done = false;
    voices_.clear();
    raw_.parts.clear();
    voice_ = -1;

    std::size_t pos = tune.begin;
    line = tune.line;
    while (pos < tune.end && !stopped_) {
        const LineSpan l = line_at(text, tune.end, pos);
        const std::string_view s(text + l.begin, l.end - l.begin);
        if (blank_line(text, l)) {
            break; // a blank line ends the tune; free text may follow until the next X:
        }
        pos = l.next;
        const std::size_t this_line = line++;
        if (s.front() == '%' || (s.size() >= 2 && s[0] == '+' && s[1] == ':')) {
            continue;
        }
        if (is_field_line(s)) {
            if (s[0] != 'X') {
                field(s[0], field_value(s), this_line);
            }
            continue;
        }
        if (!tune_.header_done) {
            warn("line " + std::to_string(this_line) +
                 ": no K: field before the music; the key of the file header (or C) is used");
            end_header();
        }
        music_line(s, this_line);
    }
    end_header();
    return finish();
}


} // namespace etude::abc_detail
