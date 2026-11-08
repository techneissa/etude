// SPDX-License-Identifier: GPL-3.0-or-later
#include "score_raw.hpp"
#include "sheet_source_internal.hpp"
#include "etude/instrument.hpp"
#include "etude/sheet_source.hpp"
#include "etude/staff_plan.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <numeric>
#include <vector>

namespace etude {

using score_detail::RawNote;
using score_detail::RawScore;
using score_detail::WrittenScore;

namespace {

// ---- The written score -----------------------------------------------------------------------

struct ScoreChord {
    std::int64_t offset = 0;
    std::int64_t length = 0;
    std::vector<const RawNote*> notes;
};

struct Event { // a chord piece or a rest, in one voice
    std::int64_t offset = 0;
    Value value;
    const ScoreChord* chord = nullptr; // null for a rest
    bool tie_in = false;          // continues the piece before it
    bool tie_out = false;         // goes on in the next piece
    bool first_piece = true;
    bool last_piece = true;
    bool hidden = false; // a rest that only fills time in a voice after the first
    int beam = 0;        // 0 none, 1 begin, 2 continue, 3 end
    int tuplet = 0;      // bit 1 start, bit 2 stop
};

// The beat a beam stays within: a dotted quarter in 6/8, 9/8 and 12/8, else the meter's beat (a
// quarter for 2/8, 3/8 and 5/8).
std::int64_t beam_span(std::int64_t ppq, int numerator, int denominator) {
    const std::int64_t beat = 4 * ppq / std::max(1, denominator);
    if (denominator == 8 && numerator % 3 == 0 && numerator > 3) {
        return beat * 3;
    }
    if (denominator >= 8) {
        return ppq;
    }
    return std::max<std::int64_t>(beat, ppq / 2);
}

std::string ending_numbers(std::uint64_t bits) {
    std::string out;
    for (int n = 1; n < 64; ++n) {
        if ((bits >> n) & 1u) {
            if (!out.empty()) {
                out += ", ";
            }
            append_int(out, n);
        }
    }
    return out;
}

class Writer {
  public:
    explicit Writer(const WrittenScore& s) : s_(s), raw_(s.raw) {}
    std::string run();

  private:
    void choose_staves();
    void write_attributes(std::size_t bar);
    void write_staff(std::size_t bar, int staff, std::int64_t length, bool last_staff);
    std::vector<Event> voice_events(std::size_t bar, const std::vector<ScoreChord>& chords,
                                    std::int64_t length, bool first_voice) const;
    void write_events(const std::vector<Event>& events, int staff, int voice, std::size_t bar,
                      std::map<std::pair<int, int>, int>& alters);

    const WrittenScore& s_;
    const RawScore& raw_;
    std::string out_;
    std::vector<int> staff_of_track_; // 0: left out, 1 treble, 2 bass
    int staves_ = 1;
    bool single_bass_ = false;
    std::vector<int> fifths_;
    std::vector<std::pair<int, int>> sig_;
    std::vector<std::vector<const RawNote*>> notes_in_bar_;
    std::vector<int> ending_end_; // per bar holding an ending's start: its last bar
};

void Writer::choose_staves() {
    const std::size_t tracks = raw_.tracks.size();
    // Accompaniment parts beside a keyboard part are not practised: left out.
    std::vector<Part> parts(raw_.parts.size());
    for (std::size_t p = 0; p < raw_.parts.size(); ++p) {
        parts[p].name = raw_.parts[p].name;
        parts[p].instrument_name = raw_.parts[p].instrument_name;
        parts[p].instrument_id = raw_.parts[p].instrument_id;
        parts[p].has_program = raw_.parts[p].program >= 0;
        parts[p].program = static_cast<std::uint8_t>(std::max(0, raw_.parts[p].program));
    }
    const bool keyboard = std::any_of(parts.begin(), parts.end(), [](const Part& p) {
        return classify_instrument(p) == InstrumentKind::Keyboard;
    });
    std::vector<StaffTrack> summary(tracks);
    for (std::size_t t = 0; t < tracks; ++t) {
        const int part = raw_.tracks[t].part;
        summary[t].hand = raw_.tracks[t].hand;
        summary[t].accompaniment =
            part >= 0 && static_cast<std::size_t>(part) < parts.size() &&
            is_accompaniment_part(parts[static_cast<std::size_t>(part)], keyboard);
    }
    for (const RawNote& n : raw_.notes) {
        if (n.track >= 0 && static_cast<std::size_t>(n.track) < tracks) {
            summary[static_cast<std::size_t>(n.track)].pitch_sum += n.pitch;
            ++summary[static_cast<std::size_t>(n.track)].notes;
        }
    }
    // The rule that gives the hands their staves (staff_plan.hpp): the hand split reads the same.
    const StaffPlan plan = plan_staves(summary);
    staves_ = plan.staves;
    single_bass_ = plan.single_bass;
    staff_of_track_.assign(tracks, 1);
    for (std::size_t t = 0; t < tracks; ++t) {
        staff_of_track_[t] = plan.staff[t] == Staff::None ? 0 : plan.staff[t] == Staff::Bass && staves_ == 2 ? 2 : 1;
    }
}

void Writer::write_attributes(std::size_t bar) {
    const bool first = bar == 0;
    const bool key = first || fifths_[bar] != fifths_[bar - 1];
    const bool time = first || sig_[bar] != sig_[bar - 1];
    if (!key && !time) {
        return;
    }
    out_ += "<attributes>";
    if (first) {
        out_ += "<divisions>";
        append_int(out_, raw_.ppq);
        out_ += "</divisions>";
    }
    if (key) {
        out_ += "<key><fifths>";
        append_int(out_, fifths_[bar]);
        out_ += "</fifths></key>";
    }
    if (time) {
        out_ += "<time><beats>";
        append_int(out_, sig_[bar].first);
        out_ += "</beats><beat-type>";
        append_int(out_, sig_[bar].second);
        out_ += "</beat-type></time>";
    }
    if (first) {
        out_ += "<staves>";
        append_int(out_, staves_);
        out_ += "</staves>";
        if (staves_ == 2) {
            out_ += "<clef number=\"1\"><sign>G</sign><line>2</line></clef>"
                    "<clef number=\"2\"><sign>F</sign><line>4</line></clef>";
        } else if (single_bass_) {
            out_ += "<clef><sign>F</sign><line>4</line></clef>";
        } else {
            out_ += "<clef><sign>G</sign><line>2</line></clef>";
        }
    }
    out_ += "</attributes>";
}

std::vector<Event> Writer::voice_events(std::size_t bar, const std::vector<ScoreChord>& chords,
                                        std::int64_t length, bool first_voice) const {
    std::vector<Event> events;
    const std::int64_t ppq = raw_.ppq;
    std::int64_t at = 0;
    auto add_rest = [&](std::int64_t from, std::int64_t to) {
        for (const Value& v : values_for(to - from, ppq)) {
            Event e;
            e.offset = from;
            e.value = v;
            e.hidden = !first_voice;
            events.push_back(e);
            from += v.ticks;
        }
    };
    for (const ScoreChord& c : chords) {
        if (c.offset > at) {
            add_rest(at, c.offset);
        }
        std::int64_t from = c.offset;
        const std::vector<Value> values = values_for(c.length, ppq);
        for (std::size_t i = 0; i < values.size(); ++i) {
            Event e;
            e.offset = from;
            e.value = values[i];
            e.chord = &c;
            e.tie_in = i > 0;
            e.tie_out = i + 1 < values.size();
            e.first_piece = i == 0;
            e.last_piece = i + 1 == values.size();
            events.push_back(e);
            from += values[i].ticks;
        }
        at = std::max(at, c.offset + c.length);
    }
    if (at < length) {
        add_rest(at, length);
    }
    // Beams: runs of notes of an eighth or shorter inside one beat, no rest between, no tuplet.
    const std::int64_t span = beam_span(ppq, sig_[bar].first, sig_[bar].second);
    const auto beamable = [&](const Event& e) {
        return e.chord != nullptr && e.value.type >= 3 && e.value.actual == 0;
    };
    for (std::size_t k = 0; k < events.size();) {
        if (!beamable(events[k])) {
            ++k;
            continue;
        }
        std::size_t j = k + 1;
        while (j < events.size() && beamable(events[j]) &&
               events[j].offset / span == events[k].offset / span) {
            ++j;
        }
        if (j - k >= 2) {
            events[k].beam = 1;
            for (std::size_t m = k + 1; m + 1 < j; ++m) {
                events[m].beam = 2;
            }
            events[j - 1].beam = 3;
        }
        k = j;
    }
    // Tuplet brackets: runs of one ratio, each closed when its group (normal x the first value) is
    // full. A run of one note gets no bracket.
    for (std::size_t k = 0; k < events.size();) {
        if (events[k].value.actual == 0) {
            ++k;
            continue;
        }
        const int actual = events[k].value.actual;
        const int normal = events[k].value.normal;
        const std::int64_t group = events[k].value.nominal * normal;
        std::size_t j = k;
        std::int64_t filled = 0;
        while (j < events.size() && events[j].value.actual == actual &&
               events[j].value.normal == normal && filled < group) {
            filled += events[j].value.ticks;
            ++j;
        }
        if (j - k >= 2) {
            events[k].tuplet |= 1;
            events[j - 1].tuplet |= 2;
        }
        k = j;
    }
    return events;
}

void Writer::write_events(const std::vector<Event>& events, int staff, int voice, std::size_t bar,
                          std::map<std::pair<int, int>, int>& alters) {
    const std::array<int, 7> key = key_alters(fifths_[bar]);
    for (const Event& e : events) {
        const std::size_t count = e.chord != nullptr ? e.chord->notes.size() : 1;
        for (std::size_t i = 0; i < count; ++i) {
            out_ += "<note";
            if (e.hidden) {
                out_ += " print-object=\"no\"";
            }
            out_ += ">";
            if (i > 0) {
                out_ += "<chord/>";
            }
            bool tie_stop = false;
            bool tie_start = false;
            const char* accidental = nullptr;
            if (e.chord == nullptr) {
                out_ += "<rest/>";
            } else {
                const RawNote& n = *e.chord->notes[i];
                const Spelled sp = spell(n.pitch, fifths_[bar]);
                out_ += "<pitch><step>";
                out_ += kSteps[sp.letter];
                out_ += "</step>";
                if (sp.alter != 0) {
                    out_ += "<alter>";
                    append_int(out_, sp.alter);
                    out_ += "</alter>";
                }
                out_ += "<octave>";
                append_int(out_, sp.octave);
                out_ += "</octave></pitch>";
                tie_stop = e.tie_in || (e.first_piece && n.tie_stop);
                tie_start = e.tie_out || (e.last_piece && n.tie_start);
                // An accidental where the bar's own state (from the key) differs; a note tied over
                // from the bar before shows none and leaves the state alone.
                const std::pair<int, int> slot{sp.letter, sp.octave};
                const auto it = alters.find(slot);
                const int current = it != alters.end() ? it->second : key[static_cast<std::size_t>(sp.letter)];
                const bool carried = e.first_piece && n.tie_stop && e.offset == 0;
                if (sp.alter != current && !carried && !e.tie_in) {
                    accidental = accidental_name(sp.alter);
                    alters[slot] = sp.alter;
                }
            }
            out_ += "<duration>";
            append_int(out_, e.value.ticks);
            out_ += "</duration>";
            if (tie_stop) {
                out_ += "<tie type=\"stop\"/>";
            }
            if (tie_start) {
                out_ += "<tie type=\"start\"/>";
            }
            out_ += "<voice>";
            append_int(out_, voice);
            out_ += "</voice><type>";
            out_ += kTypes[e.value.type];
            out_ += "</type>";
            for (int d = 0; d < e.value.dots; ++d) {
                out_ += "<dot/>";
            }
            if (accidental != nullptr) {
                out_ += "<accidental>";
                out_ += accidental;
                out_ += "</accidental>";
            }
            if (e.value.actual != 0) {
                out_ += "<time-modification><actual-notes>";
                append_int(out_, e.value.actual);
                out_ += "</actual-notes><normal-notes>";
                append_int(out_, e.value.normal);
                out_ += "</normal-notes></time-modification>";
            }
            out_ += "<staff>";
            append_int(out_, staff);
            out_ += "</staff>";
            if (e.beam != 0 && i == 0) {
                constexpr const char* kBeam[] = {"", "begin", "continue", "end"};
                out_ += "<beam number=\"1\">";
                out_ += kBeam[e.beam];
                out_ += "</beam>";
            }
            const bool tuplet = i == 0 && e.tuplet != 0;
            if (tie_stop || tie_start || tuplet) {
                out_ += "<notations>";
                if (tie_stop) {
                    out_ += "<tied type=\"stop\"/>";
                }
                if (tie_start) {
                    out_ += "<tied type=\"start\"/>";
                }
                if (tuplet && (e.tuplet & 1)) {
                    out_ += "<tuplet type=\"start\" bracket=\"yes\"/>";
                }
                if (tuplet && (e.tuplet & 2)) {
                    out_ += "<tuplet type=\"stop\"/>";
                }
                out_ += "</notations>";
            }
            out_ += "</note>";
        }
    }
}

void Writer::write_staff(std::size_t bar, int staff, std::int64_t length, bool last_staff) {
    // Chords: notes of one onset and length on this staff.
    std::vector<ScoreChord> chords;
    for (const RawNote* n : notes_in_bar_[bar]) {
        if (staff_of_track_[static_cast<std::size_t>(n->track)] != staff || n->offset >= length) {
            continue;
        }
        const std::int64_t len = std::min(n->length, length - n->offset);
        auto it = std::find_if(chords.begin(), chords.end(), [&](const ScoreChord& c) {
            return c.offset == n->offset && c.length == len;
        });
        if (it == chords.end()) {
            chords.push_back({n->offset, len, {}});
            it = chords.end() - 1;
        }
        if (std::none_of(it->notes.begin(), it->notes.end(),
                         [&](const RawNote* o) { return o->pitch == n->pitch; })) {
            it->notes.push_back(n);
        }
    }
    for (ScoreChord& c : chords) {
        std::sort(c.notes.begin(), c.notes.end(),
                  [](const RawNote* a, const RawNote* b) { return a->pitch < b->pitch; });
    }
    std::sort(chords.begin(), chords.end(), [](const ScoreChord& a, const ScoreChord& b) {
        return a.offset != b.offset ? a.offset < b.offset : a.length > b.length;
    });
    // Voices: each chord goes to the first voice free at its onset; at most four a staff. With
    // none free it joins the voice that frees first, whose last chord is cut short there (or, at
    // the same onset, takes the chord's notes).
    constexpr std::size_t kMaxVoices = 4;
    std::vector<std::vector<ScoreChord>> voices;
    for (ScoreChord& c : chords) {
        std::size_t v = 0;
        while (v < voices.size() &&
               voices[v].back().offset + voices[v].back().length > c.offset) {
            ++v;
        }
        if (v == voices.size() && voices.size() < kMaxVoices) {
            voices.emplace_back();
        } else if (v == voices.size()) {
            v = 0;
            for (std::size_t w = 1; w < voices.size(); ++w) {
                const ScoreChord& a = voices[w].back();
                const ScoreChord& b = voices[v].back();
                if (a.offset + a.length < b.offset + b.length) {
                    v = w;
                }
            }
            ScoreChord& last = voices[v].back();
            if (last.offset == c.offset) {
                for (const RawNote* n : c.notes) {
                    last.notes.push_back(n);
                }
                continue;
            }
            last.length = c.offset - last.offset;
        }
        voices[v].push_back(std::move(c));
    }
    std::map<std::pair<int, int>, int> alters;
    if (voices.empty()) {
        // A bar of rest on this staff.
        out_ += "<note><rest measure=\"yes\"/><duration>";
        append_int(out_, length);
        out_ += "</duration><voice>";
        append_int(out_, (staff - 1) * 4 + 1);
        out_ += "</voice><staff>";
        append_int(out_, staff);
        out_ += "</staff></note>";
        if (!last_staff) {
            out_ += "<backup><duration>";
            append_int(out_, length);
            out_ += "</duration></backup>";
        }
        return;
    }
    for (std::size_t v = 0; v < voices.size(); ++v) {
        const std::vector<Event> events = voice_events(bar, voices[v], length, v == 0);
        write_events(events, staff, (staff - 1) * 4 + static_cast<int>(v) + 1, bar, alters);
        if (!(last_staff && v + 1 == voices.size())) {
            out_ += "<backup><duration>";
            append_int(out_, length);
            out_ += "</duration></backup>";
        }
    }
}

std::string Writer::run() {
    const std::size_t bars = raw_.measures.size();
    if (bars == 0 || s_.bar_length.size() != bars) {
        return {};
    }
    choose_staves();
    fifths_.assign(bars, 0);
    sig_.assign(bars, {4, 4});
    int fifths = 0;
    std::pair<int, int> sig{4, 4};
    for (std::size_t i = 0; i < bars; ++i) {
        const score_detail::RawMeasure& m = raw_.measures[i];
        if (m.has_key) {
            fifths = std::clamp(m.key_fifths, -7, 7);
        }
        if (m.has_signature) {
            sig = {std::clamp(m.numerator, 1, 99), std::clamp(m.denominator, 1, 64)};
        }
        fifths_[i] = fifths;
        sig_[i] = sig;
    }
    notes_in_bar_.assign(bars, {});
    for (const RawNote& n : raw_.notes) {
        if (n.measure >= 0 && static_cast<std::size_t>(n.measure) < bars && n.length > 0 &&
            n.track >= 0 && static_cast<std::size_t>(n.track) < staff_of_track_.size() &&
            staff_of_track_[static_cast<std::size_t>(n.track)] != 0) {
            notes_in_bar_[static_cast<std::size_t>(n.measure)].push_back(&n);
        }
    }
    // Where each ending bracket ends: its own close, a backward repeat, the next bracket's start or
    // the last bar.
    ending_end_.assign(bars, -1);
    const auto marks = [&](std::size_t i) -> const BarMarks* {
        return i < raw_.marks.size() ? &raw_.marks[i] : nullptr;
    };
    for (std::size_t i = 0; i < bars; ++i) {
        const BarMarks* m = marks(i);
        if (m == nullptr || m->endings == 0) {
            continue;
        }
        std::size_t j = i;
        while (j + 1 < bars) {
            const BarMarks* mj = marks(j);
            if (mj != nullptr && (mj->ending_stops || mj->backward_repeat)) {
                break;
            }
            const BarMarks* next = marks(j + 1);
            if (next != nullptr && next->endings != 0) {
                break;
            }
            ++j;
        }
        ending_end_[i] = static_cast<int>(j);
    }

    out_.reserve(raw_.notes.size() * 260 + bars * 200);
    out_ += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<score-partwise version=\"3.1\">";
    out_ += "<part-list><score-part id=\"P1\"><part-name print-object=\"no\">";
    append_escaped(out_, raw_.title.empty() ? std::string_view("Piano") : std::string_view(raw_.title));
    out_ += "</part-name></score-part></part-list><part id=\"P1\">";
    const int first_number = s_.pickup ? 0 : 1;
    std::uint64_t open_ending = 0;
    std::size_t open_until = 0;
    for (std::size_t i = 0; i < bars; ++i) {
        const BarMarks* m = marks(i);
        out_ += "<measure number=\"";
        append_int(out_, first_number + static_cast<long long>(i));
        out_ += "\"";
        if ((i == 0 && s_.pickup) || raw_.measures[i].implicit) {
            out_ += " implicit=\"yes\"";
        }
        out_ += ">";
        write_attributes(i);
        if (m != nullptr && (m->forward_repeat || m->endings != 0)) {
            out_ += "<barline location=\"left\">";
            if (m->forward_repeat) {
                out_ += "<bar-style>heavy-light</bar-style>";
            }
            if (m->endings != 0) {
                open_ending = m->endings;
                open_until = static_cast<std::size_t>(std::max(ending_end_[i], static_cast<int>(i)));
                out_ += "<ending number=\"";
                out_ += ending_numbers(m->endings);
                out_ += "\" type=\"start\">";
                out_ += ending_numbers(m->endings);
                out_ += ".</ending>";
            }
            if (m->forward_repeat) {
                out_ += "<repeat direction=\"forward\"/>";
            }
            out_ += "</barline>";
        }
        if (m != nullptr) {
            if (m->segno) {
                out_ += "<direction placement=\"above\"><direction-type><segno/></direction-type>"
                        "</direction>";
            }
            if (m->coda) {
                out_ += "<direction placement=\"above\"><direction-type><coda/></direction-type>"
                        "</direction>";
            }
        }
        const std::int64_t length = s_.bar_length[i];
        for (int staff = 1; staff <= staves_; ++staff) {
            write_staff(i, staff, length, staff == staves_);
        }
        if (m != nullptr) {
            const auto words = [&](const char* text) {
                out_ += "<direction placement=\"above\"><direction-type><words>";
                out_ += text;
                out_ += "</words></direction-type></direction>";
            };
            if (m->fine) {
                words("Fine");
            }
            if (m->tocoda) {
                words("To Coda");
            }
            if (m->dacapo) {
                words("D.C.");
            }
            if (m->dalsegno) {
                words("D.S.");
            }
        }
        const bool closes_ending = open_ending != 0 && i == open_until;
        const bool backward = m != nullptr && m->backward_repeat;
        if (closes_ending || backward) {
            out_ += "<barline location=\"right\"><bar-style>";
            out_ += backward ? "light-heavy" : "light-light";
            out_ += "</bar-style>";
            if (closes_ending) {
                out_ += "<ending number=\"";
                out_ += ending_numbers(open_ending);
                out_ += backward || (m != nullptr && m->ending_stops) ? "\" type=\"stop\"/>"
                                                                      : "\" type=\"discontinue\"/>";
                open_ending = 0;
            }
            if (backward) {
                out_ += "<repeat direction=\"backward\"";
                if (m->times > 2) {
                    out_ += " times=\"";
                    append_int(out_, m->times);
                    out_ += "\"";
                }
                out_ += "/>";
            }
            out_ += "</barline>";
        } else if (i + 1 == bars) {
            out_ += "<barline location=\"right\"><bar-style>light-heavy</bar-style></barline>";
        }
        out_ += "</measure>";
    }
    out_ += "</part></score-partwise>\n";
    return std::move(out_);
}

// ---- MusicXML files: the practised parts only -------------------------------------------------

} // namespace

namespace score_detail {
std::string write_musicxml(const WrittenScore& score) { return Writer(score).run(); }
} // namespace score_detail

} // namespace etude
