// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/sheet_source.hpp"

#include "score_raw.hpp"
#include "sheet_source_internal.hpp"
#include "etude/instrument.hpp"
#include "etude/musicxml.hpp"
#include "etude/staff_plan.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>
#include <vector>

namespace etude {

using score_detail::RawNote;
using score_detail::RawScore;
using score_detail::WrittenScore;

namespace {

struct StringWriter : pugi::xml_writer {
    std::string text;
    void write(const void* data, std::size_t size) override {
        text.append(static_cast<const char*>(data), size);
    }
};

bool has_pitched_note(const pugi::xml_node& part) {
    for (const pugi::xml_node& m : part.children("measure")) {
        for (const pugi::xml_node& n : m.children("note")) {
            if (n.child("pitch") && !n.child("grace") && !n.child("cue")) {
                return true;
            }
        }
    }
    return false;
}

SheetSource musicxml_practised_parts(pugi::xml_document& doc) {
    SheetSource out;
    pugi::xml_node root = doc.document_element();
    pugi::xml_node list = root.child("part-list");
    // Each score part as the song's loader sees it, to tell accompaniment from the keyboard.
    std::map<std::string, Part> part_of;
    bool keyboard = false;
    for (const pugi::xml_node& sp : list.children("score-part")) {
        Part p;
        p.name = score_detail::clean_name(sp.child_value("part-name"));
        if (p.name.empty()) {
            p.name = score_detail::clean_name(sp.child_value("part-abbreviation"));
        }
        p.instrument_name =
            score_detail::clean_name(sp.child("score-instrument").child_value("instrument-name"));
        p.instrument_id =
            score_detail::clean_name(sp.child("score-instrument").child_value("instrument-sound"));
        long long program = 0;
        if (score_detail::parse_int(sp.child("midi-instrument").child_value("midi-program"), program) &&
            program >= 1 && program <= 128) {
            p.program = static_cast<std::uint8_t>(program - 1);
            p.has_program = true;
        }
        keyboard = keyboard || classify_instrument(p) == InstrumentKind::Keyboard;
        part_of.emplace(sp.attribute("id").as_string(), std::move(p));
    }
    std::vector<std::string> dropped;
    int kept = 0;
    for (const pugi::xml_node& part : root.children("part")) {
        const std::string id = part.attribute("id").as_string();
        const auto it = part_of.find(id);
        const bool accompaniment = it != part_of.end() && is_accompaniment_part(it->second, keyboard);
        if (accompaniment || !has_pitched_note(part)) {
            dropped.push_back(id);
        } else {
            ++kept;
        }
    }
    if (kept == 0) {
        out.reason = "the score has no part with notes to practise";
        return out;
    }
    for (const std::string& id : dropped) {
        root.remove_child(root.find_child_by_attribute("part", "id", id.c_str()));
        list.remove_child(list.find_child_by_attribute("score-part", "id", id.c_str()));
    }
    // Part names take a margin of their own; the practice view shows none.
    for (pugi::xml_node sp : list.children("score-part")) {
        pugi::xml_node name = sp.child("part-name");
        if (!name) {
            name = sp.prepend_child("part-name");
        }
        name.remove_attribute("print-object");
        name.append_attribute("print-object") = "no";
        while (sp.child("part-abbreviation")) {
            sp.remove_child("part-abbreviation");
        }
        while (sp.child("part-name-display")) {
            sp.remove_child("part-name-display");
        }
        while (sp.child("part-abbreviation-display")) {
            sp.remove_child("part-abbreviation-display");
        }
    }
    // Groups name parts that may be gone.
    while (list.child("part-group")) {
        list.remove_child("part-group");
    }
    StringWriter w;
    doc.save(w, "", pugi::format_raw);
    out.musicxml = std::move(w.text);
    out.status = SheetSourceStatus::Ok;
    return out;
}

SheetSource from_written(const WrittenScore& written, bool loaded) {
    SheetSource out;
    if (!loaded || written.raw.measures.empty()) {
        out.reason = "the score could not be read";
        return out;
    }
    out.musicxml = score_detail::write_musicxml(written);
    if (out.musicxml.empty()) {
        out.reason = "the score could not be written out";
        return out;
    }
    out.status = SheetSourceStatus::Ok;
    return out;
}

} // namespace


SheetSource sheet_source_for_score(std::span<const std::uint8_t> bytes, const AbcOptions& abc) {
    const SongFileFormat format = sniff_song_format(bytes.data(), bytes.size());
    WrittenScore written;
    if (format == SongFileFormat::Abc) {
        bool ok = false;
        {
            const score_detail::WrittenScoreCapture capture(&written);
            ok = load_abc(bytes.data(), bytes.size(), abc).ok;
        }
        return from_written(written, ok);
    }
    if (format != SongFileFormat::Xml && format != SongFileFormat::Zip) {
        SheetSource out;
        out.reason = "the file is not a score";
        return out;
    }
    std::vector<std::uint8_t> xml;
    ScoreLoadResult error;
    if (!score_detail::score_xml_bytes(bytes.data(), bytes.size(), xml, error)) {
        SheetSource out;
        out.reason = error.message;
        return out;
    }
    pugi::xml_document doc;
    if (!score_detail::parse_xml_bytes(xml.data(), xml.size(), doc, error)) {
        SheetSource out;
        out.reason = error.message;
        return out;
    }
    const char* root = doc.document_element().name();
    if (std::strcmp(root, "score-partwise") == 0) {
        return musicxml_practised_parts(doc);
    }
    bool ok = false;
    {
        const score_detail::WrittenScoreCapture capture(&written);
        ok = std::strcmp(root, "museScore") == 0 ? score_detail::score_from_mscx(doc).ok
                                                 : score_detail::score_from_musicxml(doc).ok;
    }
    return from_written(written, ok);
}

MidiGrid midi_grid(const Song& song) {
    MidiGrid g;
    const std::int64_t tpq = std::max(1, song.tempo.ticks_per_quarter);
    if (song.measures.empty()) {
        return g;
    }
    const std::int64_t thirty_second = tpq / 8;
    const std::int64_t triplet = tpq / 6;
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment) {
            continue;
        }
        ++g.notes;
        const std::size_t k = static_cast<std::size_t>(std::clamp<long long>(
            static_cast<long long>(n.measure) - song.measures.front().number, 0,
            static_cast<long long>(song.measures.size()) - 1));
        const std::int64_t offset = n.start_tick - song.measures[k].start_tick;
        const auto near = [&](std::int64_t grid) {
            if (grid <= 0) {
                return false;
            }
            const std::int64_t r = ((offset % grid) + grid) % grid;
            return std::min(r, grid - r) <= 1;
        };
        if (near(thirty_second) || near(triplet)) {
            ++g.on_grid;
        }
    }
    g.clean = g.notes > 0 && g.on_grid == g.notes;
    return g;
}

SheetSource sheet_source_for_midi(const Song& song) {
    SheetSource out;
    const MidiGrid grid = midi_grid(song);
    if (!grid.clean) {
        out.status = SheetSourceStatus::MidiOffGrid;
        if (grid.notes == 0) {
            out.reason = "the song has no notes to practise";
            out.status = SheetSourceStatus::Unreadable;
        } else {
            char buf[160];
            std::snprintf(buf, sizeof buf,
                          "%d of its %d notes are off the beat grid, so there is no score to show",
                          grid.notes - grid.on_grid, grid.notes);
            out.reason = buf;
        }
        return out;
    }
    // The song as a written score: its bars, its notes cut at the bar lines and tied, ends on the
    // grid (at least one step), Left on the bass staff and the rest on the treble.
    const std::int64_t tpq = std::max(1, song.tempo.ticks_per_quarter);
    WrittenScore w;
    RawScore& raw = w.raw;
    raw.ppq = static_cast<int>(tpq);
    raw.parts.push_back({"Piano", {}, {}, 0});
    raw.tracks.push_back({0, "Right", Hand::Right});
    raw.tracks.push_back({0, "Left", Hand::Left});
    const std::size_t bars = song.measures.size();
    raw.measures.resize(bars);
    w.bar_length.resize(bars);
    for (std::size_t k = 0; k < bars; ++k) {
        const Measure& m = song.measures[k];
        const std::int64_t end = k + 1 < bars ? song.measures[k + 1].start_tick
                                              : std::max(song.end_tick, m.start_tick + 1);
        w.bar_length[k] = end - m.start_tick;
        raw.measures[k].extent = w.bar_length[k];
        raw.measures[k].numerator = m.numerator;
        raw.measures[k].denominator = m.denominator;
        raw.measures[k].has_signature = k == 0 || m.numerator != song.measures[k - 1].numerator ||
                                        m.denominator != song.measures[k - 1].denominator;
    }
    // The key: the signature (-6..6) that leaves the fewest notes outside it.
    std::array<int, 12> pc_count{};
    for (const Note& n : song.notes) {
        if (!n.drum && !n.accompaniment) {
            ++pc_count[n.pitch % 12];
        }
    }
    int best_fifths = 0;
    int best_out = -1;
    for (int f = 0; f <= 12; ++f) {
        const int fifths = f % 2 == 0 ? f / 2 : -(f + 1) / 2; // 0, -1, 1, -2, 2 ...
        if (fifths < -6 || fifths > 6) {
            continue;
        }
        const std::array<int, 7> alters = key_alters(fifths);
        std::array<bool, 12> in{};
        for (int l = 0; l < 7; ++l) {
            in[static_cast<std::size_t>(((kNaturalPc[static_cast<std::size_t>(l)] + alters[static_cast<std::size_t>(l)]) % 12 + 12) % 12)] = true;
        }
        int outside = 0;
        for (int pc = 0; pc < 12; ++pc) {
            if (!in[static_cast<std::size_t>(pc)]) {
                outside += pc_count[static_cast<std::size_t>(pc)];
            }
        }
        if (best_out < 0 || outside < best_out) {
            best_out = outside;
            best_fifths = fifths;
        }
    }
    raw.measures[0].has_key = true;
    raw.measures[0].key_fifths = best_fifths;
    const auto snap = [&](std::int64_t t) {
        // Nearest point of the sixteenth or the triplet grid.
        const auto nearest = [&](std::int64_t step) {
            return step <= 0 ? t : (t + step / 2) / step * step;
        };
        const std::int64_t a = nearest(tpq / 8);
        const std::int64_t b = nearest(tpq / 6);
        return std::abs(a - t) <= std::abs(b - t) ? a : b;
    };
    // A played note often ends a little before the next one starts: on each staff, an end that
    // falls short of the next onset by a sixteenth or less is written to reach it.
    std::array<std::vector<std::int64_t>, 2> onsets;
    for (const Note& n : song.notes) {
        if (!n.drum && !n.accompaniment) {
            onsets[n.hand == Hand::Left ? 1 : 0].push_back(n.start_tick);
        }
    }
    for (auto& list : onsets) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment) {
            continue;
        }
        const std::size_t k0 = static_cast<std::size_t>(std::clamp<long long>(
            static_cast<long long>(n.measure) - song.measures.front().number, 0,
            static_cast<long long>(bars) - 1));
        const std::int64_t start = song.measures[k0].start_tick + snap(n.start_tick - song.measures[k0].start_tick);
        std::int64_t end = song.measures[k0].start_tick + snap(n.end_tick - song.measures[k0].start_tick);
        end = std::max(end, start + std::max<std::int64_t>(1, tpq / 8));
        const auto& staff_onsets = onsets[n.hand == Hand::Left ? 1 : 0];
        const auto next = std::upper_bound(staff_onsets.begin(), staff_onsets.end(), n.start_tick);
        if (next != staff_onsets.end()) {
            // The next onset as it is written, on the grid: a raw one a tick off would leave a
            // one-tick piece.
            const std::int64_t to = song.measures[k0].start_tick + snap(*next - song.measures[k0].start_tick);
            if (to > end && to - end <= tpq / 4) {
                end = to;
            }
        }
        // Cut at the bar lines, tied.
        std::size_t k = k0;
        std::int64_t from = start;
        bool first = true;
        while (from < end && k < bars) {
            const std::int64_t bar_end = song.measures[k].start_tick + w.bar_length[k];
            const std::int64_t to = std::min(end, bar_end);
            if (to > from) {
                RawNote r;
                r.measure = static_cast<int>(k);
                r.offset = from - song.measures[k].start_tick;
                r.length = to - from;
                r.pitch = n.pitch;
                r.track = n.hand == Hand::Left ? 1 : 0;
                r.tie_stop = !first;
                r.tie_start = to < end && k + 1 < bars;
                raw.notes.push_back(r);
                first = false;
            }
            from = to;
            ++k;
        }
    }
    return from_written(w, true);
}

} // namespace etude
