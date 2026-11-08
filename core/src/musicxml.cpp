// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/musicxml.hpp"

#include "musicxml_part_reader.hpp"
#include "score_raw.hpp"

#include "etude/hand.hpp"
#include "etude/song_builder.hpp"

#include "pugixml.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numeric>
#include <tuple>

namespace etude {

using namespace score_detail;

namespace {

using namespace musicxml_detail;


// Reads divisions of every <attributes> to find a tick length all of them divide.
int choose_ppq(const std::vector<std::vector<MeasureRef>>& parts,
               const std::vector<long long>& inferred) {
    std::int64_t ppq = 480;
    std::vector<long long> seen;
    for (long long v : inferred) {
        if (v >= 1 && v <= kMaxDivisions && std::find(seen.begin(), seen.end(), v) == seen.end()) {
            seen.push_back(v);
            ppq = std::lcm<std::int64_t>(ppq, v) <= kMaxPpq ? std::lcm<std::int64_t>(ppq, v) : ppq;
        }
    }
    for (const auto& part : parts) {
        for (const MeasureRef& ref : part) {
            for (const pugi::xml_node& attrs : ref.content.children("attributes")) {
                long long v = 0;
                if (parse_int(attrs.child_value("divisions"), v) && v >= 1 && v <= kMaxDivisions &&
                    std::find(seen.begin(), seen.end(), v) == seen.end()) {
                    seen.push_back(v);
                    if (seen.size() > 64) {
                        return static_cast<int>(ppq);
                    }
                    const std::int64_t next = std::lcm<std::int64_t>(ppq, v);
                    if (next <= kMaxPpq) {
                        ppq = next;
                    }
                }
            }
        }
    }
    return static_cast<int>(ppq);
}

} // namespace

namespace score_detail {

ScoreLoadResult score_from_musicxml(const pugi::xml_document& doc) {
    const pugi::xml_node root = doc.document_element();
    const bool partwise = is(root, "score-partwise");
    const bool timewise = is(root, "score-timewise");
    if (!partwise && !timewise) {
        return make_error(ScoreFileError::NotScore,
                          std::string("the XML root is <") + root.name() +
                              ">, not a MusicXML score (score-partwise or score-timewise)");
    }

    RawScore raw;
    std::map<std::string, int> part_of_id;
    for (const pugi::xml_node& sp : root.child("part-list").children("score-part")) {
        RawPart p;
        p.name = clean_name(sp.child_value("part-name"));
        if (p.name.empty()) {
            p.name = clean_name(sp.child_value("part-abbreviation"));
        }
        p.instrument_name = clean_name(sp.child("score-instrument").child_value("instrument-name"));
        // MusicXML 3.1 names the sound ("keyboard.piano", "voice.soprano").
        p.instrument_id = clean_name(sp.child("score-instrument").child_value("instrument-sound"));
        long long program = 0;
        if (parse_int(sp.child("midi-instrument").child_value("midi-program"), program) &&
            program >= 1 && program <= 128) {
            p.program = static_cast<int>(program - 1);
        }
        part_of_id.emplace(sp.attribute("id").as_string(), static_cast<int>(raw.parts.size()));
        raw.parts.push_back(std::move(p));
    }
    auto part_for = [&](const char* id) {
        const auto it = part_of_id.find(id);
        if (it != part_of_id.end()) {
            return it->second;
        }
        RawPart p;
        p.name = *id != '\0' ? id : "Part " + std::to_string(raw.parts.size() + 1);
        part_of_id.emplace(id, static_cast<int>(raw.parts.size()));
        raw.parts.push_back(std::move(p));
        return static_cast<int>(raw.parts.size()) - 1;
    };

    // Per part, its bars in order. Timewise files list each bar with its parts inside it. The bars
    // of all parts together are capped, so a hostile file cannot make this table huge.
    std::vector<std::vector<MeasureRef>> bars_of_part(raw.parts.size());
    std::size_t bar_count = 0;
    std::size_t refs = 0;
    bool too_large = false;
    auto bars_for = [&](int part) -> std::vector<MeasureRef>& {
        if (static_cast<std::size_t>(part) >= bars_of_part.size()) {
            bars_of_part.resize(static_cast<std::size_t>(part) + 1);
        }
        return bars_of_part[static_cast<std::size_t>(part)];
    };
    if (partwise) {
        for (const pugi::xml_node& p : root.children("part")) {
            if (raw.parts.size() > static_cast<std::size_t>(kMaxTracks)) {
                too_large = true;
                break;
            }
            const int part = part_for(p.attribute("id").as_string());
            auto& bars = bars_for(part);
            for (const pugi::xml_node& m : p.children("measure")) {
                if (bars.size() >= kMaxSongMeasures || ++refs > kMaxBarRefs) {
                    too_large = true;
                    break;
                }
                bars.push_back({m, m});
            }
            if (too_large) {
                break;
            }
            bar_count = std::max(bar_count, bars.size());
        }
    } else {
        std::size_t index = 0;
        for (const pugi::xml_node& m : root.children("measure")) {
            if (index >= kMaxSongMeasures) {
                too_large = true;
                break;
            }
            for (const pugi::xml_node& p : m.children("part")) {
                if (raw.parts.size() > static_cast<std::size_t>(kMaxTracks)) {
                    too_large = true;
                    break;
                }
                const int part = part_for(p.attribute("id").as_string());
                auto& bars = bars_for(part);
                refs += index + 1 - std::min(index + 1, bars.size());
                if (refs > kMaxBarRefs) {
                    too_large = true;
                    break;
                }
                bars.resize(std::max(bars.size(), index + 1));
                bars[index] = {p, m};
            }
            if (too_large) {
                break;
            }
            ++index;
        }
        bar_count = index;
    }
    if (too_large || bar_count > kMaxSongMeasures) {
        return make_error(ScoreFileError::TooLarge,
                          "the score has more parts or bars than any song needs");
    }
    if (raw.parts.size() > static_cast<std::size_t>(kMaxTracks)) {
        return make_error(ScoreFileError::TooLarge, "the score has too many parts");
    }
    bars_of_part.resize(raw.parts.size());
    if (bar_count == 0) {
        return make_error(ScoreFileError::NoNotes, "the score has no bars");
    }

    raw.title = clean_name(root.child("work").child_value("work-title"));
    const std::string movement = clean_name(root.child_value("movement-title"));
    if (raw.title.empty()) {
        raw.title = movement;
    } else if (!movement.empty() && movement != raw.title) {
        raw.title += ": " + movement; // "6 Lieder, Op.48: Die Ehre Gottes aus der Natur"
    }
    // The words printed on the page: the title credit when the work names none, the composer
    // credit when the identification names none.
    std::string credit_title;
    std::string credit_composer;
    std::string first_credit;
    for (const pugi::xml_node& credit : root.children("credit")) {
        std::string words;
        for (const pugi::xml_node& w : credit.children("credit-words")) {
            const std::string part = clean_name(w.child_value());
            if (!part.empty()) {
                words += words.empty() ? part : " " + part;
            }
        }
        if (words.empty()) {
            continue;
        }
        bool typed = false;
        for (const pugi::xml_node& type : credit.children("credit-type")) {
            const std::string_view t = type.child_value();
            typed = true;
            if (t == "title" && credit_title.empty()) {
                credit_title = words;
            } else if (t == "composer" && credit_composer.empty()) {
                credit_composer = words;
            }
        }
        if (!typed && first_credit.empty()) {
            first_credit = words;
        }
    }
    if (raw.title.empty()) {
        raw.title = !credit_title.empty() ? credit_title : first_credit;
    }
    for (const pugi::xml_node& creator : root.child("identification").children("creator")) {
        if (std::string_view(creator.attribute("type").as_string()) == "composer") {
            raw.composer = clean_name(creator.child_value());
            if (!raw.composer.empty()) {
                break;
            }
        }
    }
    if (raw.composer.empty()) {
        raw.composer = credit_composer;
    }
    std::vector<long long> inferred(bars_of_part.size(), 0);
    for (std::size_t p = 0; p < bars_of_part.size(); ++p) {
        inferred[p] = missing_divisions(bars_of_part[p], raw.parts[p].name, raw.warnings);
    }
    raw.ppq = choose_ppq(bars_of_part, inferred);
    raw.measures.resize(bar_count);
    raw.marks.resize(bar_count);

    for (std::size_t p = 0; p < bars_of_part.size(); ++p) {
        PartReader reader(raw, static_cast<int>(p), raw.ppq, inferred[p]);
        if (!reader.read(bars_of_part[p])) {
            return make_error(ScoreFileError::TooLarge,
                              "the score has more notes than any song needs");
        }
        if (raw.tracks.size() > static_cast<std::size_t>(kMaxTracks)) {
            return make_error(ScoreFileError::TooLarge, "the score has too many staves");
        }
    }
    return assemble_score(raw);
}

} // namespace score_detail

ScoreLoadResult load_musicxml(const std::uint8_t* data, std::size_t size) {
    pugi::xml_document doc;
    ScoreLoadResult error;
    if (!parse_xml_bytes(data, size, doc, error)) {
        return error;
    }
    return score_from_musicxml(doc);
}

const char* to_string(ScoreFileError error) {
    switch (error) {
    case ScoreFileError::None:
        return "none";
    case ScoreFileError::NotXml:
        return "not XML";
    case ScoreFileError::NotZip:
        return "not a zip file";
    case ScoreFileError::NotScore:
        return "not a score";
    case ScoreFileError::Unsupported:
        return "unsupported";
    case ScoreFileError::Corrupt:
        return "corrupt";
    case ScoreFileError::NoNotes:
        return "no notes";
    case ScoreFileError::TooLarge:
        return "too large";
    }
    return "unknown";
}

} // namespace etude

