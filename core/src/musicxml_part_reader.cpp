// SPDX-License-Identifier: GPL-3.0-or-later
#include "musicxml_part_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace etude::musicxml_detail {

bool PartReader::read(const std::vector<MeasureRef>& measures) {
    first_note_ = raw_.notes.size();
    for (std::size_t m = 0; m < measures.size(); ++m) {
        if (!read_measure(measures[m], static_cast<int>(m))) {
            return false;
        }
    }
    finish_tracks();
    return true;
}

bool PartReader::read_measure(const MeasureRef& ref, int index) {
    RawMeasure& bar = raw_.measures[static_cast<std::size_t>(index)];
    if (ref.bar &&
        (std::strcmp(ref.bar.attribute("implicit").as_string(), "yes") == 0 ||
         (index == 0 && std::strcmp(ref.bar.attribute("number").as_string(), "0") == 0))) {
        bar.implicit = true;
    }
    pos_ = 0;
    last_start_ = 0;
    std::int64_t extent = 0;
    for (const pugi::xml_node& c : ref.content.children()) {
        if (is(c, "attributes")) {
            read_attributes(c, bar, index);
        } else if (is(c, "note")) {
            if (!read_note(c, index, extent)) {
                return false;
            }
        } else if (is(c, "backup")) {
            std::int64_t d = 0;
            if (duration_ticks(c, d)) {
                pos_ = std::max<std::int64_t>(0, pos_ - std::max<std::int64_t>(d, 0));
            }
        } else if (is(c, "forward")) {
            std::int64_t d = 0;
            if (duration_ticks(c, d)) {
                pos_ = std::min(pos_ + std::max<std::int64_t>(d, 0), kMaxEventTicks);
                extent = std::max(extent, pos_);
            }
        } else if (is(c, "direction")) {
            read_direction(c, index);
        } else if (is(c, "harmony")) {
            read_harmony(c, index);
        } else if (is(c, "sound")) {
            read_sound(c, index, pos_);
        } else if (is(c, "barline")) {
            read_barline(c, index);
        }
    }
    bar.extent = std::max(bar.extent, extent);
    return true;
}

void PartReader::read_barline(const pugi::xml_node& barline, int index) {
    const bool left = std::strcmp(barline.attribute("location").as_string("right"), "left") == 0;
    for (const pugi::xml_node& b : barline.children()) {
        const char* type = b.attribute("type").as_string();
        if (is(b, "repeat")) {
            const char* direction = b.attribute("direction").as_string();
            if (std::strcmp(direction, "forward") == 0) {
                if (BarMarks* m = marks_at(raw_, left ? index : index + 1)) {
                    m->forward_repeat = true;
                }
            } else if (std::strcmp(direction, "backward") == 0) {
                if (BarMarks* m = marks_at(raw_, left ? index - 1 : index)) {
                    long long times = 0;
                    parse_int(b.attribute("times").as_string(), times);
                    m->backward_repeat = true;
                    m->times =
                        std::max(m->times, static_cast<int>(std::clamp<long long>(times, 0, 1000)));
                }
            }
        } else if (is(b, "ending")) {
            if (std::strcmp(type, "start") == 0) {
                if (BarMarks* m = marks_at(raw_, left ? index : index + 1)) {
                    m->endings |= parse_ending_numbers(b.attribute("number").as_string());
                }
            } else if (std::strcmp(type, "stop") == 0 || std::strcmp(type, "discontinue") == 0) {
                if (BarMarks* m = marks_at(raw_, left ? index - 1 : index)) {
                    m->ending_stops = true;
                }
            }
        }
    }
}

void PartReader::read_key(const pugi::xml_node& attrs, int index) {
    pugi::xml_node chosen;
    long long chosen_number = 0;
    for (const pugi::xml_node& key : attrs.children("key")) {
        const pugi::xml_attribute number = key.attribute("number");
        long long n = 0;
        if (!number || !parse_int(number.as_string(), n)) {
            chosen = key;
            break;
        }
        if (!chosen || n < chosen_number) {
            chosen = key;
            chosen_number = n;
        }
    }
    long long fifths = 0;
    if (!chosen || !parse_int(chosen.child_value("fifths"), fifths) || fifths < -7 || fifths > 7) {
        return;
    }
    KeyMode mode = KeyMode::Major;
    key_mode_from_name(chosen.child_value("mode"), mode);
    // A transposing part writes a key `transpose_` semitones from the sounding one: a fifth
    // is seven semitones, so the signature moves by seven per semitone, round the circle.
    if (transpose_ % 12 != 0) {
        fifths =
            transposed_key(key_from_signature(0, static_cast<int>(fifths), mode), transpose_ % 12)
                .fifths;
    }
    add_key(raw_, index, pos_, fifths, mode);
}

void PartReader::read_attributes(const pugi::xml_node& attrs, RawMeasure& bar, int index) {
    long long v = 0;
    if (parse_int(attrs.child_value("divisions"), v) && v >= 1 && v <= kMaxDivisions) {
        divisions_ = v;
    }
    if (parse_int(attrs.child_value("staves"), v) && v >= 1 && v <= 16) {
        staves_ = std::max(staves_, static_cast<int>(v));
    }
    const pugi::xml_node time = attrs.child("time");
    if (time && !bar.has_signature) {
        read_time(time, bar);
    }
    const pugi::xml_node transpose = attrs.child("transpose");
    if (transpose) {
        long long chromatic = 0;
        long long octaves = 0;
        parse_int(transpose.child_value("chromatic"), chromatic);
        parse_int(transpose.child_value("octave-change"), octaves);
        transpose_ = static_cast<int>(std::clamp<long long>(chromatic, -48, 48) +
                                      12 * std::clamp<long long>(octaves, -4, 4));
    }
    read_key(attrs, index); // after `<transpose>`, which it needs
}

void PartReader::read_harmony(const pugi::xml_node& harmony, int index) {
    std::int64_t position = pos_;
    long long offset = 0;
    if (parse_int(harmony.child_value("offset"), offset)) {
        position = std::max<std::int64_t>(0, pos_ + ticks(offset));
    }
    Chord chord;
    ChordSpelling spelling;
    int root_line = 0;
    if (read_harmony_note(harmony.child("root"), "root-step", "root-alter", root_line)) {
        const ChordQuality quality = quality_of_kind(harmony);
        if (quality != ChordQuality::None) {
            int bass_line = root_line;
            read_harmony_note(harmony.child("bass"), "bass-step", "bass-alter", bass_line);
            chord = Chord{true, line_pitch_class(root_line), quality, line_pitch_class(bass_line)};
            spelling = ChordSpelling{root_line, bass_line};
        }
    }
    add_chord_symbol(raw_, index, position, chord, spelling,
                     is_keyboard_part(raw_.parts[static_cast<std::size_t>(part_)]));
}

void PartReader::read_direction(const pugi::xml_node& dir, int index) {
    std::int64_t position = pos_;
    long long offset = 0;
    if (parse_int(dir.child_value("offset"), offset)) {
        position = std::max<std::int64_t>(0, pos_ + ticks(offset));
    }
    const pugi::xml_node sound = dir.child("sound");
    bool tempo_set = sound && sound.attribute("tempo");
    read_jump_signs(dir, sound, index);
    for (const pugi::xml_node& type : dir.children("direction-type")) {
        for (const pugi::xml_node& item : type.children()) {
            if (is(item, "dynamics")) {
                for (const pugi::xml_node& mark : item.children()) {
                    const int v = velocity_of_mark(mark.name());
                    if (v > 0) {
                        raw_.dynamics.push_back({part_, index, position, v});
                    }
                }
            } else if (is(item, "metronome") && !tempo_set) {
                double bpm = 0.0;
                if (read_metronome(item, bpm)) {
                    raw_.tempos.push_back({part_, index, position, bpm});
                    tempo_set = true;
                }
            }
        }
    }
    if (sound) {
        read_sound(sound, index, position);
    }
}

void PartReader::read_jump_signs(const pugi::xml_node& dir, const pugi::xml_node& sound,
                                 int index) {
    BarMarks* marks = marks_at(raw_, index);
    if (marks == nullptr) {
        return;
    }
    const bool sound_jumps = sound && (sound.attribute("dacapo") || sound.attribute("dalsegno") ||
                                       sound.attribute("tocoda") || sound.attribute("fine"));
    bool to_coda_words = false;
    for (const pugi::xml_node& type : dir.children("direction-type")) {
        for (const pugi::xml_node& item : type.children()) {
            if (!is(item, "words") || sound_jumps) {
                continue;
            }
            switch (jump_of_words(item.child_value())) {
            case Jump::DaCapo:
                marks->dacapo = true;
                break;
            case Jump::DalSegno:
                marks->dalsegno = true;
                break;
            case Jump::ToCoda:
                marks->tocoda = true;
                to_coda_words = true;
                break;
            case Jump::Fine:
                marks->fine = true;
                break;
            case Jump::None:
                break;
            }
        }
    }
    for (const pugi::xml_node& type : dir.children("direction-type")) {
        if (type.child("segno") && !sound.attribute("segno")) {
            marks->segno = true;
        }
        if (type.child("coda") && !sound.attribute("coda") && !to_coda_words &&
            !sound.attribute("tocoda")) {
            marks->coda = true;
        }
    }
}

PartReader::Jump PartReader::jump_of_words(const char* text) {
    std::string t;
    for (const char* c = text == nullptr ? "" : text; *c != '\0'; ++c) {
        if (*c == '.' || *c == ',') {
            continue;
        }
        t += (*c >= 'A' && *c <= 'Z') ? static_cast<char>(*c - 'A' + 'a') : *c;
    }
    t = clean_name(t.c_str());
    for (const char* initials : {"d c", "d s"}) { // "D. C." and "D. S."
        if (t.rfind(initials, 0) == 0 && (t.size() == 3 || t[3] == ' ')) {
            t.erase(1, 1);
        }
    }
    // A prefix that ends a word: "dc al fine" starts with "dc", "dcresc" does not.
    auto starts = [&](const char* prefix) {
        const std::size_t len = std::strlen(prefix);
        return t.rfind(prefix, 0) == 0 && (t.size() == len || t[len] == ' ');
    };
    if (starts("dc") || starts("da capo")) {
        return Jump::DaCapo;
    }
    if (starts("ds") || starts("dal segno") || starts("dalsegno")) {
        return Jump::DalSegno;
    }
    if (starts("to coda") || starts("tocoda")) {
        return Jump::ToCoda;
    }
    if (t == "fine") {
        return Jump::Fine;
    }
    return Jump::None;
}

bool PartReader::read_metronome(const pugi::xml_node& m, double& bpm) {
    const pugi::xml_node unit = m.child("beat-unit");
    double per_minute = 0.0;
    if (!unit || !parse_double(m.child_value("per-minute"), per_minute, true)) {
        return false;
    }
    const double quarters = quarters_of_unit(unit.child_value());
    if (quarters <= 0.0 || !(per_minute > 0.0)) {
        return false;
    }
    int dots = 0;
    for (const pugi::xml_node& c : m.children("beat-unit-dot")) {
        (void)c;
        ++dots;
    }
    bpm = per_minute * quarters * (2.0 - std::pow(0.5, std::min(dots, 8)));
    return true;
}

void PartReader::read_sound_jumps(const pugi::xml_node& sound, int index) {
    BarMarks* m = marks_at(raw_, index);
    if (m == nullptr) {
        return;
    }
    if (std::strcmp(sound.attribute("dacapo").as_string(), "yes") == 0) {
        m->dacapo = true;
    }
    if (sound.attribute("dalsegno")) {
        m->dalsegno = true;
        m->dalsegno_label = sound.attribute("dalsegno").as_string();
    }
    if (sound.attribute("tocoda")) {
        m->tocoda = true;
        m->tocoda_label = sound.attribute("tocoda").as_string();
    }
    if (sound.attribute("segno")) {
        m->segno = true;
        m->segno_label = sound.attribute("segno").as_string();
    }
    if (sound.attribute("coda")) {
        m->coda = true;
        m->coda_label = sound.attribute("coda").as_string();
    }
    if (sound.attribute("fine") && std::strcmp(sound.attribute("fine").as_string(), "no") != 0) {
        m->fine = true;
    }
}

void PartReader::read_sound(const pugi::xml_node& sound, int index, std::int64_t position) {
    read_sound_jumps(sound, index);
    double tempo = 0.0;
    if (parse_double(sound.attribute("tempo").as_string(), tempo) && tempo > 0.0) {
        raw_.tempos.push_back({part_, index, position, tempo});
    }
    double dynamics = 0.0;
    if (parse_double(sound.attribute("dynamics").as_string(), dynamics) && dynamics > 0.0) {
        raw_.dynamics.push_back({part_, index, position, velocity_of_percent(dynamics)});
    }
}

bool PartReader::read_note(const pugi::xml_node& note, int index, std::int64_t& extent) {
    if (note.child("grace")) {
        ++raw_.stats.grace_notes_dropped;
        return true;
    }
    const bool chord = static_cast<bool>(note.child("chord"));
    std::int64_t duration = 0;
    const bool has_duration = duration_ticks(note, duration);
    duration = std::max<std::int64_t>(duration, 0);
    const std::int64_t start = chord ? last_start_ : pos_;
    if (!chord) {
        last_start_ = pos_;
        pos_ = std::min(pos_ + duration, kMaxEventTicks);
    }
    extent = std::min(std::max(extent, start + duration), kMaxEventTicks);

    if (note.child("rest")) {
        return true;
    }
    if (note.child("cue")) {
        ++raw_.stats.cue_notes_dropped;
        return true;
    }
    if (note.child("unpitched")) {
        ++raw_.stats.unpitched_dropped;
        return true;
    }
    const pugi::xml_node pitch = note.child("pitch");
    const char* step_text = pitch.child_value("step");
    long long octave = 0;
    double alter = 0.0;
    parse_double(pitch.child_value("alter"), alter);
    const int semitone = step_semitone(step_text[0]);
    if (!pitch || semitone < 0 || !parse_int(pitch.child_value("octave"), octave) ||
        !has_duration || duration <= 0) {
        ++raw_.stats.unreadable_notes;
        return true;
    }
    const long long midi = (octave + 1) * 12 + semitone + std::lround(alter) + transpose_;
    if (midi < 0 || midi > 127) {
        ++raw_.stats.unreadable_notes;
        return true;
    }

    long long staff = 1;
    parse_int(note.child_value("staff"), staff);
    staff = std::clamp<long long>(staff, 1, 16);
    max_staff_ = std::max(max_staff_, static_cast<int>(staff));

    bool tie_start = false;
    bool tie_stop = false;
    bool has_tie_element = false;
    for (const pugi::xml_node& t : note.children("tie")) {
        has_tie_element = true;
        const char* type = t.attribute("type").as_string();
        tie_start = tie_start || std::strcmp(type, "start") == 0;
        tie_stop = tie_stop || std::strcmp(type, "stop") == 0;
    }
    int finger = 0;
    for (const pugi::xml_node& n : note.children("notations")) {
        if (!has_tie_element) {
            for (const pugi::xml_node& t : n.children("tied")) {
                const char* type = t.attribute("type").as_string();
                tie_start = tie_start || std::strcmp(type, "start") == 0;
                tie_stop = tie_stop || std::strcmp(type, "stop") == 0;
            }
        }
        if (finger == 0) {
            const char* f = n.child("technical").child_value("fingering");
            for (; f != nullptr && *f != '\0'; ++f) {
                if (*f >= '1' && *f <= '5') {
                    finger = *f - '0';
                    break;
                }
                if (*f >= '0' && *f <= '9') {
                    break; // 0 or 6..9 are no finger of one hand
                }
            }
        }
    }

    if (raw_.notes.size() >= kMaxScoreNotes) {
        return false;
    }
    RawNote n;
    n.measure = index;
    n.offset = start;
    n.length = duration;
    n.pitch = static_cast<std::uint8_t>(midi);
    n.finger = static_cast<std::int8_t>(finger);
    n.track = static_cast<int>(staff); // a staff number for now; finish_tracks() maps it
    n.tie_start = tie_start;
    n.tie_stop = tie_stop;
    double percent = 0.0;
    if (parse_double(note.attribute("dynamics").as_string(), percent) && percent > 0.0) {
        n.velocity = static_cast<std::uint8_t>(velocity_of_percent(percent));
    }
    raw_.notes.push_back(n);
    return true;
}

void PartReader::finish_tracks() {
    std::vector<int> staves;
    for (std::size_t i = first_note_; i < raw_.notes.size(); ++i) {
        staves.push_back(raw_.notes[i].track);
    }
    std::sort(staves.begin(), staves.end());
    staves.erase(std::unique(staves.begin(), staves.end()), staves.end());
    const int staff_count = std::max(staves_, max_staff_);
    const RawPart& part = raw_.parts[static_cast<std::size_t>(part_)];
    const Hand named = hand_from_name(part.name);
    const std::size_t first_track = raw_.tracks.size();
    std::map<int, int> track_of_staff;
    for (int staff : staves) {
        RawTrack t;
        t.part = part_;
        t.name = part.name;
        if (staves.size() > 1) {
            t.name += " (staff " + std::to_string(staff) + ")";
        }
        t.hand = staff_count >= 2 ? (staff == 1 ? Hand::Right : Hand::Left) : named;
        track_of_staff[staff] = static_cast<int>(raw_.tracks.size());
        raw_.tracks.push_back(std::move(t));
    }
    (void)first_track;
    for (std::size_t i = first_note_; i < raw_.notes.size(); ++i) {
        raw_.notes[i].track = track_of_staff[raw_.notes[i].track];
    }
}

} // namespace etude::musicxml_detail
