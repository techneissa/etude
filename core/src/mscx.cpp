// SPDX-License-Identifier: GPL-3.0-or-later
// MuseScore 3 and 4 files (.mscx, or inside a .mscz): notes, lengths, ties, chords, voices, staves,
// tempo, time signatures, fingering and dynamics. Best effort: MuseScore's own format changes
// between versions, so anything that does not look like a score this reader knows ends in an
// error that says to export MusicXML from MuseScore.
#include "score_raw.hpp"

#include "etude/hand.hpp"
#include "etude/song_builder.hpp"

#include "pugixml.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace etude::score_detail {

namespace {

constexpr const char* kExportHint =
    "export the score to MusicXML from MuseScore (File > Export) and import that file instead";
constexpr int kMaxPpq = 30240;

bool is(const pugi::xml_node& n, const char* name) { return std::strcmp(n.name(), name) == 0; }

ScoreLoadResult unreadable(const std::string& why) {
    return make_error(ScoreFileError::Unsupported,
                      "this MuseScore file cannot be read (" + why + "); " + kExportHint);
}

// "3/4" or "-1/4" as a number of whole notes; false when it is no fraction.
bool parse_fraction(const char* text, double& whole_notes) {
    long long num = 0;
    if (!parse_int(text, num)) {
        return false;
    }
    const char* slash = std::strchr(text, '/');
    long long den = 1;
    if (slash != nullptr && (!parse_int(slash + 1, den) || den < 1)) {
        return false;
    }
    whole_notes = static_cast<double>(num) / static_cast<double>(den);
    return true;
}

// Whole notes in a duration type; 0 for a name we don't know.
double whole_notes_of(const char* type) {
    struct Type {
        const char* name;
        double whole;
    };
    static constexpr Type types[] = {
        {"long", 4.0},
        {"breve", 2.0},
        {"whole", 1.0},
        {"half", 0.5},
        {"quarter", 0.25},
        {"eighth", 0.125},
        {"16th", 0.0625},
        {"32nd", 0.03125},
        {"64th", 0.015625},
        {"128th", 0.0078125},
        {"256th", 0.00390625},
        {"512th", 0.001953125},
        {"1024th", 0.0009765625},
    };
    for (const Type& t : types) {
        if (std::strcmp(t.name, type) == 0) {
            return t.whole;
        }
    }
    return 0.0;
}

int velocity_of_subtype(const char* name) {
    struct Mark {
        const char* name;
        int velocity;
    };
    static constexpr Mark marks[] = {
        {"pppp", 8}, {"ppp", 16},  {"pp", 33},    {"p", 49},     {"mp", 64},  {"mf", 80},
        {"f", 96},   {"ff", 112},  {"fff", 126},  {"ffff", 127}, {"fp", 96},  {"sf", 112},
        {"sfp", 96}, {"sfz", 112}, {"sffz", 126}, {"fz", 112},   {"rf", 112}, {"rfz", 112},
    };
    for (const Mark& m : marks) {
        if (std::strcmp(m.name, name) == 0) {
            return m.velocity;
        }
    }
    return 0;
}

// Jumps and markers seen while reading, resolved to bar marks once every staff is read: a jump
// names its targets by label, and the markers that carry those labels may come later in the file.
struct Signs {
    struct Marker {
        int bar = 0;
        std::string label;
    };
    struct JumpTo {
        int bar = 0;
        std::string jump_to;     // "start" for D.C., else a segno's label
        std::string play_until;  // "end", "fine", or the label of a To Coda marker
        std::string continue_at; // the label of the coda marker to go on from, may be empty
    };
    std::vector<Marker> markers;
    std::vector<JumpTo> jumps;
};

struct StaffInfo {
    int part = 0;
    int index_in_part = 0;
    bool percussion = false;
};

class StaffReader {
  public:
    StaffReader(RawScore& raw, int part, int ppq, bool& saw_music, Signs& signs)
        : raw_(raw), part_(part), ppq_(ppq), saw_music_(saw_music), signs_(signs) {}

    // False when the score is too large.
    bool read(const pugi::xml_node& staff_node) {
        first_note_ = raw_.notes.size();
        int index = 0;
        for (const pugi::xml_node& m : staff_node.children("Measure")) {
            if (static_cast<std::size_t>(index) >= kMaxSongMeasures) {
                return false;
            }
            if (static_cast<std::size_t>(index) >= raw_.measures.size()) {
                raw_.measures.resize(static_cast<std::size_t>(index) + 1);
            }
            if (!read_measure(m, index)) {
                return false;
            }
            ++index;
        }
        return true;
    }

    std::size_t first_note() const { return first_note_; }

  private:
    std::int64_t bar_ticks() const { return std::int64_t{4} * ppq_ * sig_n_ / sig_d_; }

    bool read_measure(const pugi::xml_node& m, int index) {
        RawMeasure& bar = raw_.measures[static_cast<std::size_t>(index)];
        double len = 0.0;
        if (parse_fraction(m.attribute("len").as_string(), len) && len > 0.0) {
            const std::int64_t ticks = std::llround(len * 4.0 * ppq_);
            if (ticks != bar_ticks()) {
                bar.implicit = true;
            }
            bar.extent = std::max(bar.extent, std::min(ticks, kMaxEventTicks));
        }
        measure_ticks_ = bar_ticks();
        measure_has_len_ = static_cast<bool>(m.attribute("len"));
        if (measure_has_len_) {
            measure_ticks_ = std::max<std::int64_t>(bar.extent, 1);
        }
        if (BarMarks* marks = marks_at(raw_, index)) {
            if (m.child("startRepeat")) {
                marks->forward_repeat = true;
            }
            if (m.child("endRepeat")) {
                long long plays = 0;
                parse_int(m.child_value("endRepeat"), plays);
                marks->backward_repeat = true;
                marks->times =
                    std::max(marks->times, static_cast<int>(std::clamp<long long>(plays, 0, 1000)));
            }
        }
        if (!read_voice(m, index, bar)) {
            return false;
        }
        for (const pugi::xml_node& v : m.children("voice")) {
            if (!read_voice(v, index, bar)) {
                return false;
            }
        }
        return true;
    }

    bool read_voice(const pugi::xml_node& voice_node, int index, RawMeasure& bar) {
        double pos = 0.0;
        std::vector<double> tuplets; // normal / actual, one per open tuplet
        double factor = 1.0;
        auto recompute = [&] {
            factor = 1.0;
            for (double t : tuplets) {
                factor *= t;
            }
        };
        for (const pugi::xml_node& e : voice_node.children()) {
            if (is(e, "TimeSig")) {
                long long n = 0;
                long long d = 0;
                if (parse_int(e.child_value("sigN"), n) && parse_int(e.child_value("sigD"), d) &&
                    n >= 1 && n <= 255 && d >= 1 && d <= 64 && (d & (d - 1)) == 0) {
                    sig_n_ = static_cast<int>(n);
                    sig_d_ = static_cast<int>(d);
                    if (!bar.has_signature) {
                        bar.has_signature = true;
                        bar.numerator = sig_n_;
                        bar.denominator = sig_d_;
                    }
                    if (!measure_has_len_) {
                        measure_ticks_ = bar_ticks();
                    }
                }
            } else if (is(e, "KeySig")) {
                // MuseScore 4 writes the sounding key as <concertKey>, MuseScore 3 <accidental>.
                long long fifths = 0;
                const bool found = parse_int(e.child_value("concertKey"), fifths) ||
                                   parse_int(e.child_value("accidental"), fifths);
                if (found) {
                    KeyMode mode = KeyMode::Major;
                    key_mode_from_name(e.child_value("mode"), mode);
                    // Where it stands in the bar: a key named mid-bar changes the key mid-bar.
                    add_key(raw_, index, std::llround(pos), fifths, mode);
                }
            } else if (is(e, "Tempo")) {
                double qn_per_second = 0.0;
                if (parse_double(e.child_value("tempo"), qn_per_second) && qn_per_second > 0.0) {
                    raw_.tempos.push_back({part_, index, std::llround(pos), qn_per_second * 60.0});
                }
            } else if (is(e, "Dynamic")) {
                long long explicit_velocity = 0;
                int v = 0;
                if (parse_int(e.child_value("velocity"), explicit_velocity) &&
                    explicit_velocity >= 1 && explicit_velocity <= 127) {
                    v = static_cast<int>(explicit_velocity);
                } else {
                    v = velocity_of_subtype(e.child_value("subtype"));
                }
                if (v > 0) {
                    raw_.dynamics.push_back({part_, index, std::llround(pos), v});
                }
            } else if (is(e, "Marker")) {
                std::string label = clean_name(e.child_value("label"));
                if (label.empty()) {
                    label = clean_name(e.child_value("subtype"));
                }
                signs_.markers.push_back({index, std::move(label)});
            } else if (is(e, "Jump")) {
                signs_.jumps.push_back({index, clean_name(e.child_value("jumpTo")),
                                        clean_name(e.child_value("playUntil")),
                                        clean_name(e.child_value("continueAt"))});
            } else if (is(e, "Spanner") &&
                       std::strcmp(e.attribute("type").as_string(), "Volta") == 0) {
                read_volta(e, index);
            } else if (is(e, "Tuplet")) {
                long long normal = 0;
                long long actual = 0;
                if (parse_int(e.child_value("normalNotes"), normal) &&
                    parse_int(e.child_value("actualNotes"), actual) && normal >= 1 && actual >= 1 &&
                    tuplets.size() < 8) {
                    tuplets.push_back(static_cast<double>(normal) / static_cast<double>(actual));
                    recompute();
                }
            } else if (is(e, "endTuplet")) {
                if (!tuplets.empty()) {
                    tuplets.pop_back();
                    recompute();
                }
            } else if (is(e, "location")) {
                double whole = 0.0;
                if (parse_fraction(e.child_value("fractions"), whole)) {
                    pos = std::clamp(pos + whole * 4.0 * ppq_, 0.0,
                                     static_cast<double>(kMaxEventTicks));
                }
            } else if (is(e, "Rest")) {
                saw_music_ = true;
                double ticks = 0.0;
                if (std::strcmp(e.child_value("durationType"), "measure") == 0) {
                    double whole = 0.0;
                    ticks = parse_fraction(e.child_value("duration"), whole)
                                ? whole * 4.0 * ppq_
                                : static_cast<double>(measure_ticks_);
                } else {
                    ticks = chord_ticks(e, factor);
                }
                pos = std::min(pos + ticks, static_cast<double>(kMaxEventTicks));
                bar.extent = std::max<std::int64_t>(bar.extent, std::llround(pos));
            } else if (is(e, "Chord")) {
                saw_music_ = true;
                if (!read_chord(e, index, pos, factor, bar)) {
                    return false;
                }
            }
        }
        return true;
    }

    // The start of an ending bracket: its numbers, and how many bars it spans (the closing end of
    // the spanner is not read).
    void read_volta(const pugi::xml_node& spanner, int index) {
        const pugi::xml_node volta = spanner.child("Volta");
        if (!volta) {
            return;
        }
        const std::uint64_t numbers = parse_ending_numbers(volta.child_value("endings"));
        long long bars = 1;
        parse_int(spanner.child("next").child("location").child_value("measures"), bars);
        bars = std::clamp<long long>(bars, 1, static_cast<long long>(kMaxSongMeasures));
        if (BarMarks* start = marks_at(raw_, index)) {
            start->endings |= numbers;
        }
        if (BarMarks* stop = marks_at(raw_, index + bars - 1)) {
            stop->ending_stops = true;
        }
    }

    // Ticks of a Chord or Rest from durationType, dots and the tuplet factor; 0 when unreadable.
    double chord_ticks(const pugi::xml_node& e, double factor) const {
        const double whole = whole_notes_of(e.child_value("durationType"));
        if (whole <= 0.0) {
            return 0.0;
        }
        long long dots = 0;
        parse_int(e.child_value("dots"), dots);
        dots = std::clamp<long long>(dots, 0, 8);
        const double dotted = 2.0 - std::pow(0.5, static_cast<double>(dots));
        return std::min(whole * dotted * factor * 4.0 * ppq_, static_cast<double>(kMaxEventTicks));
    }

    bool read_chord(const pugi::xml_node& chord, int index, double& pos, double factor,
                    RawMeasure& bar) {
        for (const pugi::xml_node& c : chord.children()) {
            if (std::strncmp(c.name(), "grace", 5) == 0 || is(c, "acciaccatura") ||
                is(c, "appoggiatura")) {
                ++raw_.stats.grace_notes_dropped;
                return true;
            }
        }
        const double ticks = chord_ticks(chord, factor);
        const std::int64_t start = std::llround(pos);
        const std::int64_t end = std::llround(pos + ticks);
        const std::int64_t length = end - start;
        pos = std::min(pos + ticks, static_cast<double>(kMaxEventTicks));
        bar.extent = std::max(bar.extent, std::min(end, kMaxEventTicks));
        if (length <= 0) {
            ++raw_.stats.unreadable_notes;
            return true;
        }
        for (const pugi::xml_node& n : chord.children("Note")) {
            long long pitch = 0;
            if (!parse_int(n.child_value("pitch"), pitch) || pitch < 0 || pitch > 127) {
                ++raw_.stats.unreadable_notes;
                continue;
            }
            bool tie_start = static_cast<bool>(n.child("tieFor"));
            bool tie_stop = static_cast<bool>(n.child("tieBack"));
            for (const pugi::xml_node& sp : n.children("Spanner")) {
                if (std::strcmp(sp.attribute("type").as_string(), "Tie") == 0) {
                    tie_start = tie_start || sp.child("next");
                    tie_stop = tie_stop || sp.child("prev");
                }
            }
            int finger = 0;
            for (const char* f = n.child("Fingering").child_value("text");
                 f != nullptr && *f != '\0'; ++f) {
                if (*f >= '1' && *f <= '5') {
                    finger = *f - '0';
                    break;
                }
                if (*f >= '0' && *f <= '9') {
                    break;
                }
            }
            if (raw_.notes.size() >= kMaxScoreNotes) {
                return false;
            }
            RawNote note;
            note.measure = index;
            note.offset = start;
            note.length = length;
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.finger = static_cast<std::int8_t>(finger);
            note.tie_start = tie_start;
            note.tie_stop = tie_stop;
            raw_.notes.push_back(note);
        }
        return true;
    }

    RawScore& raw_;
    int part_;
    int ppq_;
    bool& saw_music_;
    Signs& signs_;
    int sig_n_ = 4;
    int sig_d_ = 4;
    std::int64_t measure_ticks_ = 0;
    bool measure_has_len_ = false;
    std::size_t first_note_ = 0;
};

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return !value.empty() && std::find(list.begin(), list.end(), value) != list.end();
}

// Turns jumps and markers into bar marks. A marker is what a jump calls it: the label a jump goes
// to is a segno, the label it plays until (other than fine) is a To Coda, the label it continues
// at is a coda, and the label "fine" ends the piece after a jump.
void resolve_signs(const Signs& signs, RawScore& raw) {
    std::vector<std::string> segnos;
    std::vector<std::string> to_codas;
    std::vector<std::string> codas;
    for (const Signs::JumpTo& j : signs.jumps) {
        BarMarks* m = marks_at(raw, j.bar);
        if (m == nullptr) {
            continue;
        }
        if (j.jump_to == "start" || j.jump_to.empty()) {
            m->dacapo = true;
        } else {
            m->dalsegno = true;
            m->dalsegno_label = j.jump_to;
            segnos.push_back(j.jump_to);
        }
        if (!j.play_until.empty() && j.play_until != "fine" && j.play_until != "end") {
            to_codas.push_back(j.play_until);
        }
        if (!j.continue_at.empty()) {
            codas.push_back(j.continue_at);
        }
    }
    for (const Signs::Marker& mk : signs.markers) {
        BarMarks* m = marks_at(raw, mk.bar);
        if (m == nullptr || mk.label.empty()) {
            continue;
        }
        if (mk.label == "fine") {
            m->fine = true;
        } else if (contains(to_codas, mk.label)) {
            m->tocoda = true;
            for (const Signs::JumpTo& j : signs.jumps) {
                if (j.play_until == mk.label && !j.continue_at.empty()) {
                    m->tocoda_label = j.continue_at;
                    break;
                }
            }
        } else if (contains(codas, mk.label)) {
            m->coda = true;
            m->coda_label = mk.label;
        } else if (contains(segnos, mk.label)) {
            m->segno = true;
            m->segno_label = mk.label;
        }
    }
}

} // namespace

ScoreLoadResult score_from_mscx(const pugi::xml_document& doc) {
    const pugi::xml_node root = doc.document_element();
    if (!is(root, "museScore")) {
        return make_error(ScoreFileError::NotScore, std::string("the XML root is <") + root.name() +
                                                        ">, not a MuseScore file (museScore)");
    }
    double version = 0.0;
    parse_double(root.attribute("version").as_string(), version);
    if (version < 3.0) {
        return unreadable(std::string("file version ") + root.attribute("version").as_string() +
                          " is older than MuseScore 3");
    }
    const pugi::xml_node score = root.child("Score");
    if (!score) {
        return unreadable("no Score element");
    }
    long long division = 480;
    if (score.child("Division")) {
        parse_int(score.child_value("Division"), division);
    }
    if (division < 1 || division > kMaxPpq) {
        return unreadable("an unusual Division");
    }

    RawScore raw;
    raw.ppq = static_cast<int>(division);
    std::string movement;
    for (const pugi::xml_node& tag : score.children("metaTag")) {
        const char* name = tag.attribute("name").as_string();
        if (std::strcmp(name, "workTitle") == 0 && raw.title.empty()) {
            raw.title = clean_name(tag.child_value());
        } else if (std::strcmp(name, "movementTitle") == 0 && movement.empty()) {
            movement = clean_name(tag.child_value());
        } else if (std::strcmp(name, "composer") == 0 && raw.composer.empty()) {
            raw.composer = clean_name(tag.child_value());
        }
    }
    if (raw.title.empty()) {
        raw.title = movement;
    }

    std::map<int, StaffInfo> staves;
    std::vector<int> staves_in_part;
    for (const pugi::xml_node& p : score.children("Part")) {
        RawPart part;
        part.name = clean_name(p.child_value("trackName"));
        const pugi::xml_node instrument = p.child("Instrument");
        if (part.name.empty()) {
            part.name = clean_name(instrument.child_value("trackName"));
        }
        if (part.name.empty()) {
            part.name = clean_name(instrument.child_value("longName"));
        }
        part.instrument_name = clean_name(instrument.child_value("trackName"));
        part.instrument_id = clean_name(instrument.attribute("id").as_string());
        long long program = 0;
        const pugi::xml_node channel = instrument.child("Channel");
        if (parse_int(channel.child("program").attribute("value").as_string(), program) &&
            program >= 0 && program <= 127) {
            part.program = static_cast<int>(program);
        }
        const int part_index = static_cast<int>(raw.parts.size());
        int in_part = 0;
        for (const pugi::xml_node& s : p.children("Staff")) {
            StaffInfo info;
            info.part = part_index;
            info.index_in_part = in_part++;
            info.percussion =
                std::strcmp(s.child("StaffType").attribute("group").as_string(), "percussion") == 0;
            staves[s.attribute("id").as_int(static_cast<int>(staves.size()) + 1)] = info;
        }
        staves_in_part.push_back(in_part);
        raw.parts.push_back(std::move(part));
    }

    bool saw_staff = false;
    bool saw_music = false;
    Signs signs;
    for (const pugi::xml_node& s : score.children("Staff")) {
        if (!s.child("Measure")) {
            continue; // a Staff definition inside a Part has no bars
        }
        saw_staff = true;
        const int id = s.attribute("id").as_int(1);
        auto it = staves.find(id);
        if (it == staves.end()) {
            RawPart part;
            part.name = "Staff " + std::to_string(id);
            StaffInfo info;
            info.part = static_cast<int>(raw.parts.size());
            raw.parts.push_back(std::move(part));
            staves_in_part.push_back(1);
            it = staves.emplace(id, info).first;
        }
        const StaffInfo& info = it->second;
        if (info.percussion) {
            continue;
        }
        StaffReader reader(raw, info.part, raw.ppq, saw_music, signs);
        if (!reader.read(s)) {
            return make_error(ScoreFileError::TooLarge,
                              "the score has more notes or bars than any song needs");
        }
        if (raw.notes.size() > reader.first_note()) {
            RawTrack t;
            t.part = info.part;
            const RawPart& part = raw.parts[static_cast<std::size_t>(info.part)];
            t.name = part.name;
            const int staff_count = staves_in_part[static_cast<std::size_t>(info.part)];
            if (staff_count >= 2) {
                t.name += " (staff " + std::to_string(info.index_in_part + 1) + ")";
                t.hand = info.index_in_part == 0 ? Hand::Right : Hand::Left;
            } else {
                t.hand = hand_from_name(part.name);
            }
            if (raw.tracks.size() >= static_cast<std::size_t>(kMaxTracks)) {
                return make_error(ScoreFileError::TooLarge, "the score has too many staves");
            }
            const int track = static_cast<int>(raw.tracks.size());
            raw.tracks.push_back(std::move(t));
            for (std::size_t i = reader.first_note(); i < raw.notes.size(); ++i) {
                raw.notes[i].track = track;
            }
        }
    }
    if (!saw_staff || (!saw_music && raw.notes.empty())) {
        return unreadable("no staves with music in them");
    }
    resolve_signs(signs, raw);
    return assemble_score(raw);
}

} // namespace etude::score_detail
