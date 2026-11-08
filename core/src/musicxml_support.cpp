// SPDX-License-Identifier: GPL-3.0-or-later
#include "musicxml_support.hpp"

#include <algorithm>
#include <cstring>

namespace etude::musicxml_detail {

int velocity_of_mark(const char* name) {
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

double quarters_of_unit(const char* unit) {
    struct Unit {
        const char* name;
        double quarters;
    };
    static constexpr Unit units[] = {
        {"maxima", 32.0},     {"long", 16.0},         {"breve", 8.0},     {"whole", 4.0},
        {"half", 2.0},        {"quarter", 1.0},       {"eighth", 0.5},    {"16th", 0.25},
        {"32nd", 0.125},      {"64th", 0.0625},       {"128th", 0.03125}, {"256th", 0.015625},
        {"512th", 0.0078125}, {"1024th", 0.00390625},
    };
    for (const Unit& u : units) {
        if (std::strcmp(u.name, unit) == 0) {
            return u.quarters;
        }
    }
    return 0.0;
}

int step_semitone(char step) {
    switch (step) {
    case 'C':
        return 0;
    case 'D':
        return 2;
    case 'E':
        return 4;
    case 'F':
        return 5;
    case 'G':
        return 7;
    case 'A':
        return 9;
    case 'B':
        return 11;
    default:
        return -1;
    }
}

void read_time(const pugi::xml_node& time, RawMeasure& bar) {
    const pugi::xml_node beats = time.child("beats");
    const pugi::xml_node type = time.child("beat-type");
    if (!beats || !type) {
        return; // senza-misura
    }
    // "3+2" is five beats of the same unit.
    long long sum = 0;
    const char* s = beats.child_value();
    while (*s != '\0') {
        long long part = 0;
        if (parse_int(s, part) && part > 0) {
            sum += part;
        }
        while (*s != '\0' && *s != '+') {
            ++s;
        }
        if (*s == '+') {
            ++s;
        }
    }
    long long den = 0;
    if (!parse_int(type.child_value(), den)) {
        return;
    }
    const bool power_of_two = den >= 1 && den <= 64 && (den & (den - 1)) == 0;
    if (sum < 1 || sum > 255 || !power_of_two) {
        return;
    }
    bar.has_signature = true;
    bar.numerator = static_cast<int>(sum);
    bar.denominator = static_cast<int>(den);
}

bool line_of_step(char step, long long alter, int& line) {
    int base = 0;
    switch (step) {
    case 'C':
        base = 0;
        break;
    case 'G':
        base = 1;
        break;
    case 'D':
        base = 2;
        break;
    case 'A':
        base = 3;
        break;
    case 'E':
        base = 4;
        break;
    case 'B':
        base = 5;
        break;
    case 'F':
        base = -1;
        break;
    default:
        return false;
    }
    if (alter < -2 || alter > 2) {
        return false;
    }
    line = base + 7 * static_cast<int>(alter);
    return true;
}

bool read_harmony_note(const pugi::xml_node& node, const char* step_name, const char* alter_name,
                       int& line) {
    if (!node) {
        return false;
    }
    const char* step = node.child_value(step_name);
    long long alter = 0;
    parse_int(node.child_value(alter_name), alter); // a fraction (a quarter tone) reads as none
    return step[0] != '\0' && step[1] == '\0' && line_of_step(step[0], alter, line);
}

ChordQuality quality_of_kind(const pugi::xml_node& harmony) {
    const std::string kind = harmony.child_value("kind");
    struct Degree {
        long long value = 0;
        long long alter = 0;
        bool add = false;
    };
    std::vector<Degree> degrees;
    for (const pugi::xml_node& d : harmony.children("degree")) {
        Degree degree;
        parse_int(d.child_value("degree-value"), degree.value);
        parse_int(d.child_value("degree-alter"), degree.alter);
        const std::string type = d.child_value("degree-type");
        degree.add = type == "add";
        if (type != "add" && type != "alter") {
            return ChordQuality::None; // a tone taken out
        }
        degrees.push_back(degree);
    }
    const auto only = [&degrees](long long value, long long alter) {
        return degrees.size() == 1 && degrees[0].value == value && degrees[0].alter == alter;
    };
    if (!degrees.empty()) {
        if (kind == "dominant" && only(9, -1)) {
            return ChordQuality::Dominant7Flat9;
        }
        if (kind == "suspended-fourth" && (only(7, 0) || only(7, -1))) {
            return ChordQuality::Dominant7Sus4;
        }
        if (kind == "major-sixth" && only(9, 0)) {
            return ChordQuality::Major69;
        }
        if (kind == "major" && only(9, 0) && degrees[0].add) {
            return ChordQuality::Add9;
        }
        return ChordQuality::None;
    }
    static constexpr struct {
        const char* kind;
        ChordQuality quality;
    } kKinds[] = {
        {"major", ChordQuality::Major},
        {"minor", ChordQuality::Minor},
        {"augmented", ChordQuality::Augmented},
        {"diminished", ChordQuality::Diminished},
        {"dominant", ChordQuality::Dominant7},
        {"major-seventh", ChordQuality::Major7},
        {"minor-seventh", ChordQuality::Minor7},
        {"diminished-seventh", ChordQuality::Diminished7},
        {"half-diminished", ChordQuality::HalfDiminished7},
        {"major-minor", ChordQuality::MinorMajor7},
        {"major-sixth", ChordQuality::Major6},
        {"minor-sixth", ChordQuality::Minor6},
        {"dominant-ninth", ChordQuality::Dominant9},
        {"major-ninth", ChordQuality::Major9},
        {"minor-ninth", ChordQuality::Minor9},
        {"dominant-13th", ChordQuality::Dominant13},
        {"suspended-second", ChordQuality::Sus2},
        {"suspended-fourth", ChordQuality::Sus4},
        {"power", ChordQuality::Power},
    };
    for (const auto& k : kKinds) {
        if (kind == k.kind) {
            return k.quality;
        }
    }
    return ChordQuality::None;
}

std::int64_t bar_units(const pugi::xml_node& content, bool& sets_divisions) {
    std::int64_t pos = 0;
    std::int64_t last_start = 0;
    std::int64_t extent = 0;
    auto duration = [](const pugi::xml_node& n) {
        long long v = 0;
        parse_int(n.child_value("duration"), v);
        return std::clamp<long long>(v, 0, kMaxEventTicks);
    };
    for (const pugi::xml_node& c : content.children()) {
        if (is(c, "attributes")) {
            sets_divisions = sets_divisions || has_valid_divisions(c);
        } else if (is(c, "note")) {
            if (c.child("grace")) {
                continue;
            }
            const std::int64_t d = duration(c);
            const std::int64_t start = c.child("chord") ? last_start : pos;
            if (!c.child("chord")) {
                last_start = pos;
                pos = std::min(pos + d, kMaxEventTicks);
            }
            extent = std::max(extent, start + d);
        } else if (is(c, "backup")) {
            pos = std::max<std::int64_t>(0, pos - duration(c));
        } else if (is(c, "forward")) {
            pos = std::min<std::int64_t>(pos + duration(c), kMaxEventTicks);
            extent = std::max(extent, pos);
        }
    }
    return extent;
}

long long missing_divisions(const std::vector<MeasureRef>& bars, const std::string& name,
                            std::vector<std::string>& warnings) {
    constexpr std::size_t kBarsToTry = 16;
    struct Bar {
        std::int64_t units;
        std::int64_t beats_x4; // four times the numerator of the bar's signature
        std::int64_t den;      // its denominator
    };
    std::vector<Bar> candidates;
    bool needs = false;
    RawMeasure signature; // the signature in force while scanning
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const MeasureRef& ref = bars[i];
        // Is `<divisions>` set before the first duration of this bar? The bar's own signature is
        // the last `<time>` in its attributes.
        bool seen_duration = false;
        bool divisions_first = false;
        for (const pugi::xml_node& c : ref.content.children()) {
            if (is(c, "attributes")) {
                if (!seen_duration && has_valid_divisions(c)) {
                    divisions_first = true;
                    break;
                }
                if (c.child("time")) {
                    RawMeasure now;
                    read_time(c.child("time"), now);
                    if (now.has_signature) {
                        signature = now;
                    }
                }
            } else if (is(c, "note") || is(c, "backup") || is(c, "forward")) {
                seen_duration = seen_duration || c.child("duration");
            }
        }
        if (divisions_first) {
            break;
        }
        bool sets_divisions = false;
        const std::int64_t units = bar_units(ref.content, sets_divisions);
        if (units > 0) {
            needs = true;
        }
        const bool pickup = !ref.bar ||
                            std::strcmp(ref.bar.attribute("implicit").as_string(), "yes") == 0 ||
                            std::strcmp(ref.bar.attribute("number").as_string(), "0") == 0;
        if (units > 0 && signature.has_signature && !pickup && candidates.size() < kBarsToTry) {
            candidates.push_back(
                {units, std::int64_t{4} * signature.numerator, signature.denominator});
        }
        if (sets_divisions) {
            break; // a later `<divisions>` takes over; the bars so far were all it can tell
        }
    }
    if (!needs) {
        return 0;
    }
    // A bar of `units` units in a signature is complete at d units to the quarter note when
    // units * den == d * 4 * numerator.
    long long best = 0;
    int best_votes = 0;
    for (const Bar& c : candidates) {
        const std::int64_t scaled = c.units * c.den;
        if (scaled % c.beats_x4 != 0) {
            continue;
        }
        const long long d = scaled / c.beats_x4;
        if (d < 1 || d > kMaxDivisions) {
            continue;
        }
        int votes = 0;
        for (const Bar& other : candidates) {
            votes += other.units * other.den == d * other.beats_x4 ? 1 : 0;
        }
        if (votes > best_votes || (votes == best_votes && d > best)) {
            best = d;
            best_votes = votes;
        }
    }
    const std::string who = name.empty() ? "A part" : "Part \"" + name + "\"";
    if (best > 0) {
        warnings.push_back(who + " has no <divisions> before its first notes; read as " +
                           std::to_string(best) + " to the quarter note, the number that makes " +
                           std::to_string(best_votes) + " of its first bars whole bars of their " +
                           "time signatures.");
        return best;
    }
    warnings.push_back(who + " has no <divisions> before its first notes and its bars do not " +
                       "say what they are; read as 1 to the quarter note.");
    return 1;
}

} // namespace etude::musicxml_detail
