// SPDX-License-Identifier: GPL-3.0-or-later
#include "abc_parser.hpp"

#include <algorithm>

namespace etude::abc_detail {

ScoreLoadResult TuneParser::finish() {
    for (Voice& v : voices_) {
        commit_bar(v);
    }
    int bars_used = 0;
    for (const Voice& v : voices_) {
        if (v.notes > 0 || v.content) {
            bars_used = std::max(bars_used, v.bar + (v.content ? 1 : 0));
        }
    }
    if (raw_.notes.empty() || bars_used == 0) {
        return score_detail::make_error(ScoreFileError::NoNotes, "the tune has no notes");
    }
    raw_.measures.resize(static_cast<std::size_t>(bars_used));
    if (raw_.marks.size() > static_cast<std::size_t>(bars_used)) {
        raw_.marks.resize(static_cast<std::size_t>(bars_used));
    }
    if (!raw_.measures.empty() && !raw_.measures[0].has_signature) {
        raw_.measures[0].has_signature = true;
        raw_.measures[0].numerator = tune_.meter.numerator;
        raw_.measures[0].denominator = tune_.meter.denominator;
    }
    // A last bar that completes a pickup is not padded to a full bar.
    if (bars_used >= 2) {
        const RawMeasure& first = raw_.measures[0];
        RawMeasure& last = raw_.measures[static_cast<std::size_t>(bars_used) - 1];
        const std::int64_t nominal = kWholeTicks * first.numerator / std::max(first.denominator, 1);
        if (first.extent > 0 && first.extent < nominal && last.extent > 0 &&
            first.extent + last.extent == nominal) {
            last.implicit = true;
        }
    }

    // Parts: a voice with notes is a track; its hand is what its name or its clef says.
    int with_notes = 0;
    int bass_clefs = 0;
    int other_clefs = 0;
    for (const Voice& v : voices_) {
        if (v.track >= 0) {
            ++with_notes;
            (v.key.clef_bass ? bass_clefs : other_clefs) += 1;
        }
    }
    for (const Voice& v : voices_) {
        if (v.track < 0) {
            continue;
        }
        RawTrack& t = raw_.tracks[static_cast<std::size_t>(v.track)];
        t.hand = hand_from_name(v.name);
        if (t.hand == Hand::Unknown && with_notes >= 2 && bass_clefs > 0 && other_clefs > 0) {
            t.hand = v.key.clef_bass ? Hand::Left : Hand::Right;
        }
    }

    // Chord symbols as a left-hand part.
    if (options_.chords_as_left_hand && !chords_.empty()) {
        std::stable_sort(chords_.begin(), chords_.end(),
                         [](const ChordSymbol& a, const ChordSymbol& b) {
                             return std::tie(a.bar, a.pos) < std::tie(b.bar, b.pos);
                         });
        chords_.erase(std::unique(chords_.begin(), chords_.end(),
                                  [](const ChordSymbol& a, const ChordSymbol& b) {
                                      return a.bar == b.bar && a.pos == b.pos;
                                  }),
                      chords_.end());
        const int part = static_cast<int>(raw_.parts.size());
        raw_.parts.push_back(RawPart{"Chords", {}, {}, -1});
        RawTrack track;
        track.part = part;
        track.name = "Chords";
        track.hand = Hand::Left;
        raw_.tracks.push_back(track);
        const int chord_track = static_cast<int>(raw_.tracks.size()) - 1;
        // The melody's notes by bar: a chord note never shares a key with one of them while both
        // sound (it is left out, never moved to another key).
        const std::size_t melody_count = raw_.notes.size();
        std::vector<std::vector<std::size_t>> melody_in_bar(static_cast<std::size_t>(bars_used));
        for (std::size_t k = 0; k < melody_count; ++k) {
            const int bar = raw_.notes[k].measure;
            if (bar >= 0 && bar < bars_used) {
                melody_in_bar[static_cast<std::size_t>(bar)].push_back(k);
            }
        }
        bool added = false;
        for (std::size_t k = 0; k < chords_.size(); ++k) {
            const ChordSymbol& c = chords_[k];
            if (c.none || c.bar >= bars_used) {
                continue;
            }
            const std::int64_t bar_end = raw_.measures[static_cast<std::size_t>(c.bar)].extent;
            std::int64_t end = bar_end;
            if (k + 1 < chords_.size() && chords_[k + 1].bar == c.bar) {
                end = std::min(end, chords_[k + 1].pos);
            }
            const std::int64_t length = end - c.pos;
            if (length <= 0 || raw_.notes.size() + 2 > kMaxScoreNotes) {
                continue;
            }
            // The chord note sounds for the whole chord except where a melody note of the same
            // pitch sounds: there it is left out (never moved to another key), and it goes on after
            // that note ends.
            auto add = [&](int pitch) {
                pitch = std::clamp(pitch, 0, 127);
                std::vector<std::pair<std::int64_t, std::int64_t>> clashes;
                for (const std::size_t at : melody_in_bar[static_cast<std::size_t>(c.bar)]) {
                    const RawNote& m = raw_.notes[at];
                    if (m.pitch == pitch && m.offset < c.pos + length &&
                        c.pos < m.offset + m.length) {
                        clashes.emplace_back(m.offset, m.offset + m.length);
                    }
                }
                std::sort(clashes.begin(), clashes.end());
                std::int64_t from = c.pos;
                const std::int64_t until = c.pos + length;
                for (std::size_t i = 0; i <= clashes.size(); ++i) {
                    const std::int64_t to = i < clashes.size() ? std::min(clashes[i].first, until)
                                                               : until;
                    if (to > from && raw_.notes.size() < kMaxScoreNotes) {
                        RawNote n;
                        n.measure = c.bar;
                        n.offset = from;
                        n.length = to - from;
                        n.pitch = static_cast<std::uint8_t>(pitch);
                        n.velocity = static_cast<std::uint8_t>(kLeftHandVelocity);
                        n.track = chord_track;
                        raw_.notes.push_back(n);
                        added = true;
                    }
                    if (i < clashes.size()) {
                        from = std::max(from, clashes[i].second);
                    }
                }
            };
            if (c.bass >= 0) {
                add(kChordSymbolOctave + c.bass);
            } else {
                add(kChordSymbolOctave + c.root);
                add(kChordSymbolOctave + c.root + c.fifth);
            }
        }
        if (added) {
            // The tune's own line goes to the right hand when nothing says otherwise.
            if (with_notes == 1) {
                for (RawTrack& t : raw_.tracks) {
                    if (t.hand == Hand::Unknown) {
                        t.hand = Hand::Right;
                    }
                }
            }
        } else {
            raw_.tracks.pop_back();
            raw_.parts.pop_back();
        }
    }

    // What had to be left out.
    if (skipped_ > 0) {
        warn(std::to_string(skipped_) + (skipped_ == 1 ? " character" : " characters") +
             " it could not read " + (skipped_ == 1 ? "was" : "were") +
             " skipped, the first on line " + std::to_string(first_skipped_line_));
    }
    if (out_of_range_ > 0) {
        warn(std::to_string(out_of_range_) + (out_of_range_ == 1 ? " note" : " notes") +
             " outside the piano's range left out");
    }
    if (notes_capped_) {
        warn("the tune has more notes than a song can hold; the rest was left out");
    }
    if (bars_capped_) {
        warn("the tune has more bars than a song can hold; the rest was left out");
    }

    raw_.ppq = kPpq;
    raw_.title = tune_.title;
    ScoreLoadResult result = score_detail::assemble_score(raw_);
    if (result.ok) {
        result.composer = tune_.composer;
    }
    return result;
}


} // namespace etude::abc_detail
