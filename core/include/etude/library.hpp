// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "etude/abc.hpp"
#include "etude/difficulty.hpp"
#include "etude/keyboard.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"
#include "etude/progress.hpp"
#include "etude/song.hpp"
#include "etude/song_versions.hpp"

namespace etude {

// The song library index behind the song browser. The app scans
// folders and hands core the bytes of each file and its name; core has no file system. Everything
// here is a pure function over those values and the run history.
//
// One SongEntry per file: what the browser shows (title, length, difficulty, best score) and what
// its filters and sorts need (pitch range, notes per hand, last played).

// ---- Text

// True when `text` is well-formed UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
bool is_valid_utf8(std::string_view text);

// Text from a MIDI file or a file name as valid UTF-8. Many files are not UTF-8, and the format
// does not say what they are, so this is a best effort with this fallback order:
// 1. Valid UTF-8 is returned unchanged.
// 2. Shift-JIS, when every high byte fits Shift-JIS and at least one is kana (half-width
//    katakana, hiragana, katakana) or a full-width Latin letter or digit. Those are decoded by
//    arithmetic; kanji need a table of thousands of entries that core does not carry, so each
//    kanji becomes U+FFFD. A title made only of kanji is therefore unusable (choose_title skips
//    it).
// 3. Otherwise Windows-1252, a superset of Latin-1: each byte is one character, and the five
//    bytes 1252 leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D) become U+FFFD.
// The guess can be wrong: a Latin-1 word like "Café" followed by an ASCII letter looks like a
// Shift-JIS pair, but with no kana to confirm it step 3 wins; Shift-JIS text of only kanji decodes
// as Latin-1 mojibake. The result is always valid UTF-8.
std::string utf8_from_midi_text(std::string_view bytes);

// The longest title kept, as for run records.
inline constexpr std::size_t kMaxLibraryTitleBytes = kMaxRunTitleBytes;

// A display title from a file name, cleaned. The name is decoded with utf8_from_midi_text, then:
// - the folder is dropped ('/' or '\' separated) and a known song extension (.mid .midi .kar .smf
//   .rmi .musicxml .xml .mxl .mscz .mscx .abc, any case);
// - a copy mark the system added at the end is dropped: "(1)", " copy", " copy 2", " - Copy";
// - a leading track number is dropped when a separator follows it ("01 - ", "1. ", "03_", "2)",
//   "7-") or it starts with a zero ("01 Prelude"), and a letter is left after it ("12 Variations"
//   keeps its number);
// - underscores become spaces, and so do dashes: all of them in a name with no space, only the
//   ones beside a space otherwise ("Jean-Philippe Rameau - Tambourin" keeps its hyphen);
// - runs of white space and control characters collapse to one space and the ends are trimmed;
// - capitals are tidied when the name has no capital or no small letter at all: every word gets a
//   capital first letter, except a short joining word inside ("of", "the", "in", "de", "von" ...),
//   and a Roman numeral of up to four letters is all capitals ("moonlight sonata ii" is
//   "Moonlight Sonata II"). A name in mixed case is left as it is;
// - the result is cut to kMaxLibraryTitleBytes at a character boundary.
std::string title_from_file_name(std::string_view file_name);

// The name in the first track's Track Name event (meta 0x03), the "sequence name" of a format 0 or
// format 1 file, as the raw bytes; empty when there is none or the data is not a readable MIDI
// file. Reads only inside `bytes`.
std::string midi_sequence_name(std::span<const std::uint8_t> bytes);

// The title to show, the file's own name first: `sequence_name` is the title the file gives (a
// score's title, a MIDI file's first track name), used when it has a letter or digit and is not a
// placeholder: generic ("untitled", "song", "midi", "track 1", "control track", only digits) or
// what a sequencer writes there instead of a title (an instrument or hand: "Piano", "Acoustic Grand
// Piano", "Right Hand", "Melody"; a credit: "Copyright ...", "Created by ...", a web address; a
// file name with its extension). Otherwise the cleaned file name (title_from_file_name), when it is
// usable and not generic; otherwise whichever of the two has anything, else "Untitled".
std::string choose_title(std::string_view file_name, std::string_view sequence_name);

// Lower case with accents removed, for search and for sorting by title. Handles ASCII, Latin-1
// Supplement and Latin Extended-A letters (Á->a, ß->ss, Æ->ae, Ł->l), combining marks (dropped),
// full-width Latin letters and digits, and plain Greek and Cyrillic lower casing. Other characters
// are kept as they are. Invalid UTF-8 is read as Latin-1 first (utf8_from_midi_text).
std::string fold_for_search(std::string_view text);

// fold_for_search into `out` (cleared first), for text typed as it is typed: valid UTF-8 is folded
// as above and a byte that is not part of a character is read as Latin-1. It allocates only when
// `out` has too little capacity, so a buffer reserved once serves every keystroke.
void fold_for_search_into(std::string_view text, std::string& out);

// ---- Keyboard fit

enum class KeyFit : std::uint8_t {
    Fits,          // every note is on a key as written
    FitsWithShift, // a transposition of at most an octave puts every note on a key
    TooWide,       // no shift of up to an octave does: the song is wider than the keyboard, or
                   // sits further away. Playing it folds notes by octaves (see transpose.hpp).
};

struct KeyboardFit {
    KeyFit fit = KeyFit::Fits;
    // The shift in semitones (the smallest in magnitude that fits; -12..12) for FitsWithShift,
    // otherwise 0. Pass it as the transposition of Transposition.
    int semitones = 0;
    bool fits() const { return fit != KeyFit::TooWide; }
};
// Whether notes from `lowest` to `highest` fit `range`; a negative `lowest` (no notes) fits.
KeyboardFit fit_to_keyboard(int lowest, int highest, KeyRange range);

// ---- Entries

struct SongEntry {
    std::uint64_t hash =
        0;                 // content_hash() of the file's bytes: the song's identity in the history
    std::string title;     // for display
    std::string title_key; // fold_for_search(title): for sorting and searching
    std::string composer;  // the composer the file names (score files and ABC); may be empty
    double duration_s = 0.0;
    int bar_count = 0;
    int note_count = 0; // playable (non-drum) notes
    int left_notes = 0;
    int right_notes = 0;
    int unknown_notes = 0; // no hand: the song needs the hand split (hand_split.hpp)
    int lowest_pitch = -1; // -1 when there are no playable notes
    int highest_pitch = -1;
    // The keys each hand covers, its lowest note to its highest (melody_shape.hpp), 0 for a hand with
    // no notes; the "From your library" row of Melodies sorts by them.
    int left_keys = 0;
    int right_keys = 0;
    // The song's rating (difficulty.hpp, rate_song) in half stars 2..10, and the whole-star level
    // 1..5 the library sorts and filters by (level_of_half_stars).
    int half_stars = kMinHalfStars;
    int difficulty = 1;
    // The top line's pitch contour over the first bars (song_versions.hpp), read once with the
    // rest and kept with it: what tells that two files are versions of one piece.
    MelodyContour melody;

    // From the run history.
    bool has_best = false;
    BestEntry best;          // the best accuracy over every mode, hands and speed
    int run_count = 0;       // runs of this song, repeats left out
    int last_run_index = -1; // position in the history of the latest run, -1 if never played
    std::int32_t last_run_day = 0;

    bool hands_known() const { return unknown_notes == 0; }
    bool played() const { return run_count > 0; }
};

// The fit of a song's notes on a keyboard.
inline KeyboardFit fit_to_keyboard(const SongEntry& entry, KeyRange range) {
    return fit_to_keyboard(entry.lowest_pitch, entry.highest_pitch, range);
}

// The entry for a loaded song: `bytes` are the file's contents (for the hash and the sequence name)
// and `file_name` its name as the folder scan saw it (may be empty). `rated` is the song's rating
// when the caller made it already (a loader that keeps the bars' ratings too); null rates the song
// here. Hands should be split first, as the rating reads them.
SongEntry make_song_entry(const Song& song, std::span<const std::uint8_t> bytes,
                          std::string_view file_name, const SongDifficulty* rated = nullptr);
// The same for a score file: `score_title` is the title the file names (ScoreLoadResult::title,
// which stands in for the MIDI sequence name in choose_title) and `composer` its composer.
SongEntry make_song_entry(const Song& song, std::span<const std::uint8_t> bytes,
                          std::string_view file_name, std::string_view score_title,
                          std::string_view composer = {}, const SongDifficulty* rated = nullptr);

// The entry for one tune of an ABC file. `hash` is AbcTune::hash. The tune's T: title names it, so
// the file name does not win here as it does for the other formats; with no usable title the file
// name stands in, followed by `tune_address` when `in_collection` (the file holds several tunes).
SongEntry make_abc_entry(const Song& song, std::uint64_t hash, std::string_view tune_title,
                         std::string_view file_name, std::string_view tune_address,
                         bool in_collection, std::string_view composer = {},
                         const SongDifficulty* rated = nullptr);

// What load_library_entry() needs besides the bytes.
struct LibraryLoadOptions {
    std::string tune; // ABC: the tune's address in the file (AbcTune::address); empty: the first
    bool abc_chords = false; // ABC: chord symbols become a left-hand part (AbcOptions)
    // ABC: the file's tunes when the caller listed them already (list_abc_tunes of these bytes), so
    // that loading every tune of a big file lists it once.
    const std::vector<AbcTune>* abc_tunes = nullptr;
    // Splits the hands of a song that needs it (hand_split.hpp), given the hash of what the file
    // holds (the file's, or the ABC tune's) and `abc_chords`. The entry's counts and difficulty
    // are read from the split song; the app passes a splitter that remembers its answer, so
    // opening the song later does not split it again. Null: with_split_hands, forgotten.
    std::function<void(Song& song, std::uint64_t hash, bool abc_chords)> split_hands;
};

struct LibraryLoad {
    bool ok = false;
    SongFileFormat format = SongFileFormat::Unknown;   // what the bytes are, by content
    MidiFileError error = MidiFileError::None;         // NotMidi when the bytes are a score file
    ScoreFileError score_error = ScoreFileError::None; // set when a score file failed to load
    std::string message;
    SongEntry entry; // empty unless ok
};
// Loads a song file and makes its entry, for the scan. The format comes from the bytes, not the
// file name: "MThd" is a Standard MIDI File (load_midi), XML is MusicXML or MuseScore XML and a zip
// is .mxl or .mscz (load_score), text with an X: line and a K: line is ABC (load_abc, one tune of
// it). Bytes that are none of these report the MIDI error. Never throws.
LibraryLoad load_library_entry(std::span<const std::uint8_t> bytes, std::string_view file_name,
                               const LibraryLoadOptions& options = {});

// What the history says of one song, for the whole library at once.
struct SongPlays {
    int runs = 0;                // runs of the song, repeats left out
    int last_run_index = -1;     // position in the history of the latest run
    std::int32_t last_day = 0;   // the local day of the latest run
    int best_permille = -1;      // the best accuracy in thousandths, -1 for none
};
// One pass over `runs` (oldest first): per song hash, the runs, latest run and best (the best is
// the highest accuracy of a run with at least kMinGradedForBest graded notes, rounded to
// thousandths). Runs with hash 0 are left out.
std::unordered_map<std::uint64_t, SongPlays> summarise_plays(std::span<const RunRecord> runs);

// ---- Browsing

enum class SortKey : std::uint8_t {
    Title,          // ascending A to Z
    Difficulty,     // ascending: easy first
    RecentlyPlayed, // ascending: the oldest run first, descending: the latest first; songs never
                    // played always come last
    Length,         // ascending: short first
};

struct LibrarySort {
    SortKey key = SortKey::Title;
    bool descending = false; // reverses the key; ties always go by title A to Z, then by hash
};

enum class KeyboardFilter : std::uint8_t {
    Any,
    FitsAsIs,      // every note on a key as written
    FitsWithShift, // fits as written or with a shift of up to an octave
};

enum class PlayedFilter : std::uint8_t { Any, PlayedOnly, UnplayedOnly };

struct LibraryFilter {
    int min_difficulty = 1;
    int max_difficulty = 5; // an empty range (min > max) matches nothing
    KeyboardFilter keyboard = KeyboardFilter::Any;
    KeyRange range = kRange61; // the player's keyboard, used by `keyboard`
    PlayedFilter played = PlayedFilter::Any;
};

} // namespace etude
