// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library.hpp"

#include "library_internal.hpp"

#include "etude/content_hash.hpp"
#include "etude/hand_split.hpp"
#include "etude/melody_shape.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude {

// ---- Keyboard fit

KeyboardFit fit_to_keyboard(int lowest, int highest, KeyRange range) {
    KeyboardFit out;
    if (lowest < 0 || highest < lowest) {
        return out;
    }
    // A shift s fits when lowest + s >= range.lowest and highest + s <= range.highest.
    int low = std::max(range.lowest - lowest, -kMaxTransposeSemitones);
    int high = std::min(range.highest - highest, kMaxTransposeSemitones);
    if (low > high) {
        out.fit = KeyFit::TooWide;
    } else if (low > 0) {
        out.fit = KeyFit::FitsWithShift;
        out.semitones = low;
    } else if (high < 0) {
        out.fit = KeyFit::FitsWithShift;
        out.semitones = high;
    }
    return out;
}

// ---- Entries

namespace {

SongEntry build_entry_titled(const Song& loaded, std::uint64_t hash, std::string title,
                             const SongDifficulty* rated = nullptr) {
    // Hand counts and difficulty are read after the hand split, so a song whose tracks have no
    // hand names does not show "0 left, 0 right". A song that is split already is read as it is.
    Song split;
    const bool needs_split = needs_hand_split(loaded);
    if (needs_split) {
        split = with_split_hands(loaded);
    }
    const Song& song = needs_split ? split : loaded;
    SongEntry e;
    e.hash = hash;
    e.title = std::move(title);
    e.title_key = fold_for_search(e.title);
    e.duration_s = song.duration_s;
    e.bar_count = static_cast<int>(song.measures.size());
    int lowest = 128;
    int highest = -1;
    for (const Note& n : song.notes) {
        if (n.drum || n.accompaniment) {
            continue;
        }
        ++e.note_count;
        (n.hand == Hand::Left    ? e.left_notes
         : n.hand == Hand::Right ? e.right_notes
                                 : e.unknown_notes) += 1;
        lowest = std::min<int>(lowest, n.pitch);
        highest = std::max<int>(highest, n.pitch);
    }
    if (highest >= 0) {
        e.lowest_pitch = lowest;
        e.highest_pitch = highest;
    }
    const MelodyShape shape = melody_shape(song);
    e.left_keys = shape.left.keys();
    e.right_keys = shape.right.keys();
    if (rated != nullptr) {
        e.half_stars = rated->half_stars;
    } else {
        e.half_stars = rate_song(song).half_stars;
    }
    e.difficulty = level_of_half_stars(e.half_stars);
    e.melody = melody_contour(song);
    return e;
}

SongEntry build_entry(const Song& loaded, std::span<const std::uint8_t> bytes,
                      std::string_view file_name, std::string_view sequence_name,
                      const SongDifficulty* rated) {
    return build_entry_titled(loaded, content_hash(bytes.data(), bytes.size()),
                              choose_title(file_name, sequence_name), rated);
}

} // namespace

SongEntry make_abc_entry(const Song& song, std::uint64_t hash, std::string_view tune_title,
                         std::string_view file_name, std::string_view tune_address,
                         bool in_collection, std::string_view composer,
                         const SongDifficulty* rated) {
    // The T: title names the tune; the file name stands in when there is none, with the tune's
    // address after it when the file holds several.
    std::string title = clean_text(utf8_from_midi_text(tune_title));
    if (!usable_title(title)) {
        title = title_from_file_name(file_name);
        if (!usable_title(title)) {
            title = "Untitled";
        }
        if (in_collection && !tune_address.empty()) {
            title += " " + std::string(tune_address);
        }
    }
    SongEntry e = build_entry_titled(song, hash, std::move(title), rated);
    e.composer = clean_text(utf8_from_midi_text(composer));
    return e;
}

SongEntry make_song_entry(const Song& song, std::span<const std::uint8_t> bytes,
                          std::string_view file_name, const SongDifficulty* rated) {
    return build_entry(song, bytes, file_name, midi_sequence_name(bytes), rated);
}

SongEntry make_song_entry(const Song& song, std::span<const std::uint8_t> bytes,
                          std::string_view file_name, std::string_view score_title,
                          std::string_view composer, const SongDifficulty* rated) {
    SongEntry e = build_entry(song, bytes, file_name, score_title, rated);
    e.composer = clean_text(utf8_from_midi_text(composer));
    return e;
}

LibraryLoad load_library_entry(std::span<const std::uint8_t> bytes, std::string_view file_name,
                               const LibraryLoadOptions& options) {
    LibraryLoad out;
    out.format = sniff_song_format(bytes);
    if (out.format == SongFileFormat::Abc) {
        AbcOptions abc;
        abc.tune = options.tune;
        abc.chords_as_left_hand = options.abc_chords;
        std::vector<AbcTune> listed_here;
        if (options.abc_tunes == nullptr) {
            listed_here = list_abc_tunes(bytes);
        }
        const std::vector<AbcTune>& tunes =
            options.abc_tunes != nullptr ? *options.abc_tunes : listed_here;
        const AbcTune* found = nullptr;
        for (const AbcTune& t : tunes) {
            if (options.tune.empty() || t.address == options.tune) {
                found = &t;
                break;
            }
        }
        out.error = MidiFileError::NotMidi;
        if (found == nullptr) {
            out.score_error = ScoreFileError::NotScore;
            out.message = "the file has no tune " + options.tune;
            return out;
        }
        abc.listed = found;
        ScoreLoadResult r = load_abc(bytes, abc);
        out.message = r.message;
        out.score_error = r.error;
        if (!r.ok) {
            return out;
        }
        out.error = MidiFileError::None;
        out.ok = true;
        if (options.split_hands && needs_hand_split(r.song)) {
            options.split_hands(r.song, found->hash, options.abc_chords);
        }
        out.entry = make_abc_entry(r.song, found->hash, r.title, file_name, found->address,
                                   tunes.size() > 1, r.composer);
        return out;
    }
    if (out.format == SongFileFormat::Xml || out.format == SongFileFormat::Zip) {
        ScoreLoadResult r = load_score(bytes);
        out.error = MidiFileError::NotMidi;
        out.message = r.message;
        out.score_error = r.error;
        if (!r.ok) {
            return out;
        }
        out.error = MidiFileError::None;
        out.ok = true;
        if (options.split_hands && needs_hand_split(r.song)) {
            options.split_hands(r.song, content_hash(bytes.data(), bytes.size()), false);
        }
        out.entry = make_song_entry(r.song, bytes, file_name, r.title, r.composer);
        return out;
    }
    MidiLoadResult r = load_midi(bytes);
    out.error = r.error;
    out.message = r.message;
    if (!r.ok) {
        return out;
    }
    out.ok = true;
    if (options.split_hands && needs_hand_split(r.song)) {
        options.split_hands(r.song, content_hash(bytes.data(), bytes.size()), false);
    }
    out.entry = make_song_entry(r.song, bytes, file_name);
    return out;
}

void attach_history(SongEntry& entry, std::span<const RunRecord> runs) {
    entry.has_best = false;
    entry.best = BestEntry{};
    entry.run_count = 0;
    entry.last_run_index = -1;
    entry.last_run_day = 0;
    if (entry.hash == 0) {
        return;
    }
    for (std::size_t i : unique_run_indices(runs)) {
        if (runs[i].song_hash == entry.hash) {
            ++entry.run_count;
            entry.last_run_index = static_cast<int>(i);
            entry.last_run_day = runs[i].local_day;
        }
    }
    for (const BestEntry& b : song_bests(runs, entry.hash)) {
        if (!entry.has_best || b.accuracy > entry.best.accuracy ||
            (b.accuracy == entry.best.accuracy && b.speed_percent > entry.best.speed_percent)) {
            entry.best = b;
            entry.has_best = true;
        }
    }
}

std::unordered_map<std::uint64_t, SongPlays> summarise_plays(std::span<const RunRecord> runs) {
    std::unordered_map<std::uint64_t, SongPlays> out;
    for (std::size_t i : unique_run_indices(runs)) {
        const RunRecord& r = runs[i];
        if (r.song_hash == 0) {
            continue;
        }
        SongPlays& p = out[r.song_hash];
        ++p.runs;
        p.last_run_index = static_cast<int>(i);
        p.last_day = r.local_day;
        if (r.all.graded >= kMinGradedForBest) {
            const int permille =
                static_cast<int>(std::lround(r.all.accuracy().value_or(0.0) * 1000.0));
            p.best_permille = std::max(p.best_permille, permille);
        }
    }
    return out;
}

// ---- Browsing

bool matches_filter(const SongEntry& entry, const LibraryFilter& filter) {
    if (entry.difficulty < filter.min_difficulty || entry.difficulty > filter.max_difficulty) {
        return false;
    }
    if (filter.played == PlayedFilter::PlayedOnly && !entry.played()) {
        return false;
    }
    if (filter.played == PlayedFilter::UnplayedOnly && entry.played()) {
        return false;
    }
    if (filter.keyboard != KeyboardFilter::Any) {
        KeyFit fit = fit_to_keyboard(entry, filter.range).fit;
        if (fit == KeyFit::TooWide ||
            (filter.keyboard == KeyboardFilter::FitsAsIs && fit != KeyFit::Fits)) {
            return false;
        }
    }
    return matches_search(entry.title_key, filter.text);
}

std::vector<std::size_t> browse_library(std::span<const SongEntry> entries,
                                        const LibraryFilter& filter, const LibrarySort& sort) {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (matches_filter(entries[i], filter)) {
            order.push_back(i);
        }
    }
    auto by_title = [&](std::size_t a, std::size_t b) {
        const SongEntry& x = entries[a];
        const SongEntry& y = entries[b];
        if (x.title_key != y.title_key) {
            return x.title_key < y.title_key;
        }
        if (x.hash != y.hash) {
            return x.hash < y.hash;
        }
        return a < b;
    };
    auto primary = [&](std::size_t a, std::size_t b) {
        const SongEntry& x = entries[a];
        const SongEntry& y = entries[b];
        switch (sort.key) {
        case SortKey::Title:
            return 0;
        case SortKey::Difficulty:
            return x.difficulty < y.difficulty ? -1 : x.difficulty > y.difficulty ? 1 : 0;
        case SortKey::Length:
            return x.duration_s < y.duration_s ? -1 : x.duration_s > y.duration_s ? 1 : 0;
        case SortKey::RecentlyPlayed:
            return x.last_run_index < y.last_run_index   ? -1
                   : x.last_run_index > y.last_run_index ? 1
                                                         : 0;
        }
        return 0;
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (sort.key == SortKey::Title) {
            return sort.descending ? by_title(b, a) : by_title(a, b);
        }
        if (sort.key == SortKey::RecentlyPlayed) {
            bool a_never = entries[a].last_run_index < 0;
            bool b_never = entries[b].last_run_index < 0;
            if (a_never != b_never) {
                return b_never; // played songs first
            }
            if (a_never) {
                return by_title(a, b);
            }
        }
        int c = primary(a, b);
        if (c != 0) {
            return sort.descending ? c > 0 : c < 0;
        }
        return by_title(a, b);
    });
    return order;
}

} // namespace etude
