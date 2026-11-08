// SPDX-License-Identifier: GPL-3.0-or-later
#include "score_raw.hpp"

#include "etude/song_builder.hpp"

#include "pugixml.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <cmath>
#include <cstdlib>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace etude::score_detail {
namespace {

constexpr int kDrumChannelIndex = 9;

std::int64_t nominal_bar_ticks(int ppq, int numerator, int denominator) {
    return std::int64_t{4} * ppq * numerator / denominator;
}

// MIDI channel for the n-th part: 0..8, then 10..15 (channel 10 is percussion).
std::uint8_t channel_of_part(int part) {
    const int c = part % 15;
    return static_cast<std::uint8_t>(c >= kDrumChannelIndex ? c + 1 : c);
}

struct BarSignature {
    int numerator = 4;
    int denominator = 4;
};

} // namespace

bool add_key(RawScore& raw, int measure, std::int64_t offset, long long fifths, KeyMode mode) {
    if (measure < 0 || fifths < -7 || fifths > 7) {
        return false;
    }
    for (const RawKey& k : raw.keys) {
        if (k.measure == measure && k.offset == offset) {
            return false;
        }
    }
    raw.keys.push_back({measure, offset, static_cast<int>(fifths), mode});
    return true;
}

bool is_keyboard_part(const RawPart& part) {
    if ((part.program >= 0 && part.program <= 7) || (part.program >= 16 && part.program <= 23)) {
        return true;
    }
    std::string text = part.name + " " + part.instrument_name + " " + part.instrument_id;
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const char* word : {"piano", "keyboard", "klavier", "clavier", "organ", "harpsichord",
                             "cembalo", "pianoforte"}) {
        if (text.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool add_chord_symbol(RawScore& raw, int measure, std::int64_t offset, const Chord& chord,
                      const ChordSpelling& spelling, bool keyboard) {
    if (measure < 0 || offset < 0) {
        return false;
    }
    for (RawChordSymbol& c : raw.chord_symbols) {
        if (c.measure == measure && c.offset == offset) {
            if (keyboard && !c.keyboard) {
                c = {measure, offset, chord, spelling, true};
                return true;
            }
            return false;
        }
    }
    raw.chord_symbols.push_back({measure, offset, chord, spelling, keyboard});
    return true;
}

ScoreLoadResult make_error(ScoreFileError error, std::string message) {
    ScoreLoadResult r;
    r.ok = false;
    r.error = error;
    r.message = std::move(message);
    return r;
}

BarMarks* marks_at(RawScore& raw, long long measure) {
    if (measure < 0 || measure >= static_cast<long long>(kMaxSongMeasures)) {
        return nullptr;
    }
    if (raw.marks.size() <= static_cast<std::size_t>(measure)) {
        raw.marks.resize(static_cast<std::size_t>(measure) + 1);
    }
    return &raw.marks[static_cast<std::size_t>(measure)];
}

std::string clean_name(const char* text) {
    std::string out;
    bool pending_space = false;
    for (const char* c = text == nullptr ? "" : text; *c != '\0'; ++c) {
        if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out += ' ';
            pending_space = false;
        }
        out += *c;
    }
    return out;
}

bool parse_int(const char* text, long long& out) {
    if (text == nullptr) {
        return false;
    }
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        ++text;
    }
    bool negative = false;
    if (*text == '-' || *text == '+') {
        negative = *text == '-';
        ++text;
    }
    if (*text < '0' || *text > '9') {
        return false;
    }
    long long v = 0;
    for (; *text >= '0' && *text <= '9'; ++text) {
        if (v < 100'000'000'000'000LL) { // saturate far above anything a score holds
            v = v * 10 + (*text - '0');
        }
    }
    out = negative ? -v : v;
    return true;
}

bool parse_double(const char* text, double& out, bool first_number_in_text) {
    if (text == nullptr) {
        return false;
    }
    if (first_number_in_text) {
        while (*text != '\0' && !(*text >= '0' && *text <= '9')) {
            ++text;
        }
    } else {
        while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
            ++text;
        }
    }
    bool negative = false;
    if (*text == '-' || *text == '+') {
        negative = *text == '-';
        ++text;
    }
    if ((*text < '0' || *text > '9') && *text != '.') {
        return false;
    }
    double v = 0.0;
    bool any = false;
    for (; *text >= '0' && *text <= '9'; ++text) {
        any = true;
        v = std::min(v * 10.0 + (*text - '0'), 1e15);
    }
    if (*text == '.') {
        ++text;
        double scale = 0.1;
        for (; *text >= '0' && *text <= '9'; ++text) {
            any = true;
            v += (*text - '0') * scale;
            scale *= 0.1;
        }
    }
    if (!any) {
        return false;
    }
    out = negative ? -v : v;
    return true;
}

bool parse_xml_bytes(const std::uint8_t* data, std::size_t size, pugi::xml_document& doc,
                     ScoreLoadResult& error) {
    if (data == nullptr || size == 0) {
        error = make_error(ScoreFileError::NotXml, "the file is empty");
        return false;
    }
    if (size > kMaxScoreXmlBytes) {
        error = make_error(ScoreFileError::TooLarge, "the score file is larger than 64 MiB");
        return false;
    }
    const pugi::xml_parse_result parsed = doc.load_buffer(data, size, pugi::parse_default);
    if (!parsed) {
        error = make_error(ScoreFileError::NotXml, std::string("not readable XML: ") +
                                                       parsed.description() + " at byte " +
                                                       std::to_string(parsed.offset));
        return false;
    }
    return true;
}

namespace {
thread_local WrittenScore* t_written_sink = nullptr;
} // namespace

WrittenScoreCapture::WrittenScoreCapture(WrittenScore* out) : previous_(t_written_sink) {
    t_written_sink = out;
}

WrittenScoreCapture::~WrittenScoreCapture() { t_written_sink = previous_; }

ScoreLoadResult assemble_score(RawScore& raw) {
    const int ppq = raw.ppq;
    const std::size_t bar_count = raw.measures.size();
    if (raw.notes.empty() || bar_count == 0) {
        return make_error(ScoreFileError::NoNotes, "the score has no playable notes");
    }
    if (bar_count > kMaxSongMeasures) {
        return make_error(ScoreFileError::TooLarge, "the score has more bars than any song needs");
    }

    // The signature in force in each written bar.
    std::vector<BarSignature> sig(bar_count);
    BarSignature current;
    for (std::size_t i = 0; i < bar_count; ++i) {
        if (raw.measures[i].has_signature) {
            current = {raw.measures[i].numerator, raw.measures[i].denominator};
        }
        sig[i] = current;
    }

    // Written bar lengths: as long as the music in them, but never shorter than the signature,
    // except for a pickup and for bars the file marks implicit.
    const std::int64_t first_nominal = nominal_bar_ticks(ppq, sig[0].numerator, sig[0].denominator);
    FirstBar first_bar;
    first_bar.length = raw.measures[0].extent;
    first_bar.nominal = first_nominal;
    first_bar.numerator = sig[0].numerator;
    first_bar.denominator = sig[0].denominator;
    first_bar.next_signature_differs = bar_count >= 2 && (sig[1].numerator != sig[0].numerator ||
                                                          sig[1].denominator != sig[0].denominator);
    first_bar.marked_pickup = raw.measures[0].implicit;
    const bool pickup =
        bar_count >= 2 && raw.measures[0].extent > 0 && first_bar_is_pickup(first_bar);
    std::vector<std::int64_t> length(bar_count);
    for (std::size_t i = 0; i < bar_count; ++i) {
        const RawMeasure& m = raw.measures[i];
        const std::int64_t nominal = nominal_bar_ticks(ppq, sig[i].numerator, sig[i].denominator);
        std::int64_t len = nominal;
        if (m.extent > 0) {
            len = ((i == 0 && pickup) || m.implicit) ? m.extent : std::max(m.extent, nominal);
        }
        length[i] = std::max<std::int64_t>(len, 1);
    }
    if (t_written_sink != nullptr) {
        t_written_sink->raw = raw;
        t_written_sink->bar_length = length;
        t_written_sink->pickup = pickup;
    }

    // The order the bars play in. Repeats are expanded here, once: every played bar is a copy of a
    // written bar with its own place on the timeline.
    raw.marks.resize(bar_count);
    std::vector<std::size_t> notes_in_bar(bar_count, 0);
    for (const RawNote& rn : raw.notes) {
        if (rn.measure >= 0 && static_cast<std::size_t>(rn.measure) < bar_count) {
            ++notes_in_bar[static_cast<std::size_t>(rn.measure)];
        }
    }
    RepeatPlan plan = plan_repeats(raw.marks);
    raw.stats.repeats_followed = plan.repeats_followed;
    raw.stats.repeats_ignored = plan.fallbacks;
    raw.stats.repeats_implied = plan.repeats_implied;
    {
        std::size_t played_notes = 0;
        for (std::size_t s : plan.order) {
            played_notes += notes_in_bar[s];
        }
        if (played_notes > kMaxScoreNotes && !plan.capped) {
            plan = RepeatPlan{};
            plan.capped = true;
            for (std::size_t i = 0; i < bar_count; ++i) {
                plan.order.push_back(i);
            }
        }
    }
    raw.stats.repeats_capped = plan.capped;
    if (plan.capped) {
        raw.stats.repeats_followed = 0;
    }
    const std::vector<std::size_t>& order = plan.order;
    const std::size_t played = order.size();
    bool expanded = played != bar_count;
    for (std::size_t p = 0; p < played && !expanded; ++p) {
        expanded = order[p] != p;
    }
    raw.stats.bars_written = static_cast<int>(bar_count);
    raw.stats.bars_played = static_cast<int>(played);

    // Where each played bar starts, and the played bars of each written bar.
    std::vector<std::int64_t> start(played + 1, 0);
    std::vector<std::size_t> first_play(bar_count + 1, 0);
    for (std::size_t p = 0; p < played; ++p) {
        start[p + 1] = start[p] + length[order[p]];
        ++first_play[order[p] + 1];
    }
    for (std::size_t i = 0; i < bar_count; ++i) {
        first_play[i + 1] += first_play[i];
    }
    std::vector<std::size_t> plays_of(played); // played bars, grouped by written bar
    {
        std::vector<std::size_t> fill(first_play.begin(), first_play.end() - 1);
        for (std::size_t p = 0; p < played; ++p) {
            plays_of[fill[order[p]]++] = p;
        }
    }
    auto for_each_play = [&](int measure, auto&& fn) {
        if (measure < 0 || static_cast<std::size_t>(measure) >= bar_count) {
            return;
        }
        const auto written = static_cast<std::size_t>(measure);
        for (std::size_t k = first_play[written]; k < first_play[written + 1]; ++k) {
            fn(plays_of[k]);
        }
    };

    Song song;
    song.tempo.ticks_per_quarter = ppq;
    song.tempo.changes.clear();

    // Tempo comes from the first part that marks any.
    int tempo_part = -1;
    for (const RawTempo& t : raw.tempos) {
        tempo_part = tempo_part < 0 ? t.part : std::min(tempo_part, t.part);
    }
    for (const RawTempo& t : raw.tempos) {
        if (t.part != tempo_part || !(t.bpm > 0.0)) {
            continue;
        }
        for_each_play(t.measure, [&](std::size_t p) {
            song.tempo.changes.push_back(
                {start[p] + t.offset, 0.0, us_per_quarter_from_bpm(t.bpm)});
        });
    }
    std::stable_sort(song.tempo.changes.begin(), song.tempo.changes.end(),
                     [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });
    if (song.tempo.changes.empty()) {
        song.tempo.changes.push_back({});
    }

    // Signatures where they change.
    for (std::size_t p = 0; p < played; ++p) {
        const BarSignature& now = sig[order[p]];
        if (p == 0 || now.numerator != sig[order[p - 1]].numerator ||
            now.denominator != sig[order[p - 1]].denominator) {
            song.time_signatures.push_back({start[p], static_cast<std::uint8_t>(now.numerator),
                                            static_cast<std::uint8_t>(now.denominator)});
        }
    }

    // Dynamics per part, by tick.
    struct Level {
        std::int64_t tick;
        int velocity;
    };
    std::vector<std::vector<Level>> levels(raw.parts.size());
    for (const RawDynamic& d : raw.dynamics) {
        if (d.part < 0 || static_cast<std::size_t>(d.part) >= levels.size()) {
            continue;
        }
        for_each_play(d.measure, [&](std::size_t p) {
            levels[static_cast<std::size_t>(d.part)].push_back({start[p] + d.offset, d.velocity});
        });
    }
    for (auto& l : levels) {
        std::stable_sort(l.begin(), l.end(),
                         [](const Level& a, const Level& b) { return a.tick < b.tick; });
    }

    // Notes, bar by bar in the order the bars play, each bar's pieces by onset. A tie stop joins
    // the latest open tie start of the same pitch in the same part, whatever the staff or voice,
    // that ends where the stop begins (to a tick), in the same bar or in the bar that follows it in
    // the written score. After a jump back or on to an ending, a stop is a fresh attack.
    const int first_number = pickup ? 0 : 1;
    song.notes.reserve(raw.notes.size());
    {
        std::vector<std::size_t> first_piece(bar_count + 1, 0);
        for (const RawNote& rn : raw.notes) {
            if (rn.measure >= 0 && static_cast<std::size_t>(rn.measure) < bar_count) {
                ++first_piece[static_cast<std::size_t>(rn.measure) + 1];
            }
        }
        for (std::size_t i = 0; i < bar_count; ++i) {
            first_piece[i + 1] += first_piece[i];
        }
        std::vector<std::size_t> pieces(first_piece[bar_count]);
        {
            std::vector<std::size_t> fill(first_piece.begin(), first_piece.end() - 1);
            for (std::size_t k = 0; k < raw.notes.size(); ++k) {
                const int m = raw.notes[k].measure;
                if (m >= 0 && static_cast<std::size_t>(m) < bar_count) {
                    pieces[fill[static_cast<std::size_t>(m)]++] = k;
                }
            }
        }
        // Within a bar the pieces go by onset, not by the order of the file: a tie from a lower
        // staff to a higher one, or from one voice to another, may be written in either order.
        for (std::size_t i = 0; i < bar_count; ++i) {
            std::stable_sort(pieces.begin() + static_cast<std::ptrdiff_t>(first_piece[i]),
                             pieces.begin() + static_cast<std::ptrdiff_t>(first_piece[i + 1]),
                             [&](std::size_t a, std::size_t b) {
                                 return raw.notes[a].offset < raw.notes[b].offset;
                             });
        }
        struct OpenTie {
            std::size_t note = 0;
            std::size_t bar = 0;       // played bar the last piece is in
            std::int64_t end_tick = 0; // where the held note ends so far
        };
        // The open tie starts of each pitch in each part, oldest first. A bar later than the one
        // before the current one can no longer be joined, so those are dropped on the way.
        std::map<std::pair<int, int>, std::vector<OpenTie>> open;
        int tie_starts = 0;
        int joined = 0;
        for (std::size_t p = 0; p < played; ++p) {
            const std::size_t written_bar = order[p];
            for (std::size_t k = first_piece[written_bar]; k < first_piece[written_bar + 1]; ++k) {
                const RawNote& rn = raw.notes[pieces[k]];
                if (rn.length <= 0 || rn.track < 0 ||
                    static_cast<std::size_t>(rn.track) >= raw.tracks.size()) {
                    continue;
                }
                const RawTrack& track = raw.tracks[static_cast<std::size_t>(rn.track)];
                const std::pair<int, int> key{track.part, rn.pitch};
                if (rn.tie_stop) {
                    const auto it = open.find(key);
                    if (it != open.end()) {
                        std::vector<OpenTie>& starts = it->second;
                        std::erase_if(starts, [p](const OpenTie& o) { return o.bar + 1 < p; });
                        std::size_t at = starts.size();
                        const std::int64_t onset = start[p] + rn.offset;
                        while (at > 0) {
                            // The stop joins the latest start that ends where it begins (to a
                            // tick) and plays right before it: in this bar, or in the bar that
                            // follows it in the written score.
                            const OpenTie& o = starts[at - 1];
                            const bool adjacent = o.bar == p || (p > 0 && o.bar == p - 1 &&
                                                                 order[p] == order[o.bar] + 1);
                            if (adjacent && std::llabs(onset - o.end_tick) <= 1) {
                                break;
                            }
                            --at;
                        }
                        if (at > 0) {
                            OpenTie& o = starts[at - 1];
                            song.notes[o.note].end_tick += rn.length;
                            ++raw.stats.ties_merged;
                            ++joined;
                            if (rn.tie_start) {
                                ++tie_starts;
                                o.bar = p;
                                o.end_tick = onset + rn.length;
                            } else {
                                starts.erase(starts.begin() + static_cast<std::ptrdiff_t>(at - 1));
                            }
                            continue;
                        }
                    }
                    ++raw.stats.unmatched_ties;
                }
                Note n;
                n.start_tick = start[p] + rn.offset;
                n.end_tick = n.start_tick + rn.length;
                n.pitch = rn.pitch;
                n.hand = track.hand;
                n.finger = rn.finger;
                n.track = static_cast<std::uint16_t>(rn.track);
                n.channel = channel_of_part(track.part);
                if (expanded) {
                    n.source_measure = first_number + static_cast<int>(written_bar);
                }
                int velocity = rn.velocity;
                if (velocity == 0) {
                    velocity = kDefaultVelocity;
                    const auto& l = levels[static_cast<std::size_t>(track.part)];
                    const auto at =
                        std::upper_bound(l.begin(), l.end(), n.start_tick,
                                         [](std::int64_t t, const Level& v) { return t < v.tick; });
                    if (at != l.begin()) {
                        velocity = (at - 1)->velocity;
                    }
                }
                n.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
                song.notes.push_back(n);
                if (rn.tie_start) {
                    ++tie_starts;
                    std::vector<OpenTie>& starts = open[key];
                    std::erase_if(starts, [p](const OpenTie& o) { return o.bar + 1 < p; });
                    starts.push_back({song.notes.size() - 1, p, n.end_tick});
                }
            }
        }
        raw.stats.ties_not_continued = tie_starts - joined;
    }

    // One note per pitch and part sounds at a time, as in a MIDI file. The part decides, not the
    // MIDI channel: a score of sixteen or more parts shares channels, and parts do not cut each
    // other's notes.
    {
        std::vector<int> part_of_note(song.notes.size(), 0);
        for (std::size_t i = 0; i < song.notes.size(); ++i) {
            part_of_note[i] = raw.tracks[song.notes[i].track].part;
        }
        std::vector<std::size_t> by_pitch(song.notes.size());
        for (std::size_t i = 0; i < by_pitch.size(); ++i) {
            by_pitch[i] = i;
        }
        std::sort(by_pitch.begin(), by_pitch.end(), [&](std::size_t x, std::size_t y) {
            const Note& a = song.notes[x];
            const Note& b = song.notes[y];
            return std::tie(part_of_note[x], a.pitch, a.start_tick, b.end_tick, x) <
                   std::tie(part_of_note[y], b.pitch, b.start_tick, a.end_tick, y);
        });
        std::vector<bool> drop(song.notes.size(), false);
        std::size_t cur = by_pitch.empty() ? 0 : by_pitch[0];
        for (std::size_t k = 1; k < by_pitch.size(); ++k) {
            Note& c = song.notes[cur];
            Note& n = song.notes[by_pitch[k]];
            if (part_of_note[cur] != part_of_note[by_pitch[k]] || c.pitch != n.pitch) {
                cur = by_pitch[k];
            } else if (n.start_tick == c.start_tick) {
                c.end_tick = std::max(c.end_tick, n.end_tick);
                if (c.hand == Hand::Unknown) {
                    c.hand = n.hand;
                }
                if (c.finger == 0) {
                    c.finger = n.finger;
                }
                drop[by_pitch[k]] = true;
                ++raw.stats.duplicates_merged;
            } else {
                if (n.start_tick < c.end_tick) {
                    c.end_tick = n.start_tick;
                    ++raw.stats.overlaps_split;
                }
                cur = by_pitch[k];
            }
        }
        std::size_t out = 0;
        for (std::size_t i = 0; i < song.notes.size(); ++i) {
            if (!drop[i]) {
                song.notes[out++] = song.notes[i];
            }
        }
        song.notes.resize(out);
    }

    // Parts, with the instrument a file names.
    std::vector<int> first_track_of_part(raw.parts.size(), -1);
    for (std::size_t i = 0; i < raw.tracks.size(); ++i) {
        const RawTrack& t = raw.tracks[i];
        const RawPart& p = raw.parts[static_cast<std::size_t>(t.part)];
        Part part;
        part.track = static_cast<std::uint16_t>(i);
        part.channel = channel_of_part(t.part);
        part.name = t.name;
        part.instrument_name = p.instrument_name;
        part.instrument_id = p.instrument_id;
        part.hand = t.hand;
        if (p.program >= 0) {
            part.program = static_cast<std::uint8_t>(p.program);
            part.has_program = true;
        }
        song.parts.push_back(std::move(part));
        int& first = first_track_of_part[static_cast<std::size_t>(t.part)];
        if (first < 0) {
            first = static_cast<int>(i);
        }
    }
    for (std::size_t p = 0; p < raw.parts.size(); ++p) {
        if (raw.parts[p].program >= 0 && first_track_of_part[p] >= 0) {
            ChannelEvent e;
            e.track = static_cast<std::uint16_t>(first_track_of_part[p]);
            e.channel = channel_of_part(static_cast<int>(p));
            e.kind = ChannelEventKind::Program;
            e.value = static_cast<std::uint16_t>(raw.parts[p].program);
            song.events.push_back(e);
        }
    }

    if (!finish_song(song)) {
        return make_error(ScoreFileError::TooLarge, "the score has more bars than any song needs");
    }
    if (song.notes.empty()) {
        return make_error(ScoreFileError::NoNotes, "the score has no playable notes");
    }
    std::erase_if(song.parts, [](const Part& p) { return p.note_count == 0; });

    // finish_song built bars from the signatures alone; the score knows its real bars, in the
    // order they play. `number` counts played bars; `source_number` is the written bar.
    song.measures.clear();
    for (std::size_t p = 0; p < played; ++p) {
        if (p > 0 && start[p] >= song.end_tick) {
            break;
        }
        const BarSignature& bar_sig = sig[order[p]];
        Measure m;
        m.number = first_number + static_cast<int>(p);
        m.start_tick = start[p];
        m.start_s = song.tempo.seconds_at(start[p]);
        m.numerator = static_cast<std::uint8_t>(bar_sig.numerator);
        m.denominator = static_cast<std::uint8_t>(bar_sig.denominator);
        if (expanded) {
            m.source_number = first_number + static_cast<int>(order[p]);
        }
        song.measures.push_back(m);
    }
    for (Note& n : song.notes) {
        n.measure = song.measure_at(n.start_tick);
    }

    // The key where it changes, along the order the bars play. A written bar starts in the key the
    // bars before it left (so a repeat back to an earlier bar is a change again), and its own keys
    // come at their offsets into it, so a key named mid-bar changes mid-bar.
    {
        std::vector<RawKey> keys;
        for (const RawKey& k : raw.keys) {
            if (k.measure >= 0 && static_cast<std::size_t>(k.measure) < bar_count) {
                keys.push_back(k);
            }
        }
        std::stable_sort(keys.begin(), keys.end(), [](const RawKey& a, const RawKey& b) {
            return std::tie(a.measure, a.offset) < std::tie(b.measure, b.offset);
        });
        std::vector<KeyChange> before(bar_count);
        std::vector<bool> known(bar_count, false);
        std::vector<std::size_t> first_key(bar_count + 1, keys.size());
        KeyChange now;
        bool have = false;
        std::size_t next = 0;
        for (std::size_t i = 0; i < bar_count; ++i) {
            before[i] = now;
            known[i] = have;
            first_key[i] = next;
            while (next < keys.size() && static_cast<std::size_t>(keys[next].measure) == i) {
                now = key_from_signature(0, keys[next].fifths, keys[next].mode);
                have = true;
                ++next;
            }
        }
        first_key[bar_count] = next;
        const auto push = [&song](KeyChange k, std::int64_t tick) {
            k.tick = tick;
            if (song.keys.empty() || song.keys.back().tonic != k.tonic ||
                song.keys.back().mode != k.mode || song.keys.back().fifths != k.fifths) {
                song.keys.push_back(k);
            }
        };
        for (std::size_t p = 0; p < played; ++p) {
            if (p > 0 && start[p] >= song.end_tick) {
                break;
            }
            const std::size_t bar = order[p];
            if (known[bar]) {
                push(before[bar], start[p]);
            }
            for (std::size_t k = first_key[bar]; k < first_key[bar + 1]; ++k) {
                const std::int64_t offset =
                    std::clamp<std::int64_t>(keys[k].offset, 0, length[bar] - 1);
                push(key_from_signature(0, keys[k].fifths, keys[k].mode), start[p] + offset);
            }
        }
        complete_keys(song);
    }

    // The chord symbols, along the order the bars play, each at its offset into its bar.
    {
        // A bar where a keyboard part writes symbols takes only those; other parts (a vocal line)
        // fill the bars where the keyboard part writes none.
        std::set<int> keyboard_bars;
        for (const RawChordSymbol& c : raw.chord_symbols) {
            if (c.keyboard) {
                keyboard_bars.insert(c.measure);
            }
        }
        std::vector<RawChordSymbol> symbols;
        for (const RawChordSymbol& c : raw.chord_symbols) {
            if (c.measure >= 0 && static_cast<std::size_t>(c.measure) < bar_count &&
                (c.keyboard || !keyboard_bars.contains(c.measure))) {
                symbols.push_back(c);
            }
        }
        std::stable_sort(symbols.begin(), symbols.end(),
                         [](const RawChordSymbol& a, const RawChordSymbol& b) {
                             return std::tie(a.measure, a.offset) < std::tie(b.measure, b.offset);
                         });
        for (std::size_t p = 0; p < played; ++p) {
            if (p > 0 && start[p] >= song.end_tick) {
                break;
            }
            const std::size_t bar = order[p];
            const auto first = std::lower_bound(symbols.begin(), symbols.end(), bar,
                                                [](const RawChordSymbol& c, std::size_t m) {
                                                    return static_cast<std::size_t>(c.measure) < m;
                                                });
            for (auto it = first;
                 it != symbols.end() && static_cast<std::size_t>(it->measure) == bar; ++it) {
                ChordSymbol symbol;
                symbol.tick = start[p] + std::clamp<std::int64_t>(it->offset, 0, length[bar] - 1);
                symbol.seconds = song.tempo.seconds_at(symbol.tick);
                symbol.chord = it->chord;
                symbol.spelling = it->spelling;
                if (song.chord_symbols.empty() || song.chord_symbols.back().tick < symbol.tick) {
                    song.chord_symbols.push_back(symbol);
                }
            }
        }
    }

    ScoreLoadResult r;
    r.ok = true;
    r.song = std::move(song);
    r.stats = raw.stats;
    r.title = raw.title;
    r.composer = raw.composer;
    r.warnings = std::move(raw.warnings);
    return r;
}

} // namespace etude::score_detail
