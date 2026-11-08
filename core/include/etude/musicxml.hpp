// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "etude/song.hpp"

namespace etude {

// Score files: MusicXML. Uncompressed `.musicxml`/`.xml`,
// compressed `.mxl`, and MuseScore's `.mscz`. Like load_midi(), every loader works on bytes the
// caller read (core has no file system), never throws, never reads outside the bytes, and gives
// ok == false with an error and an English message when the file is no use.

enum class ScoreFileError : std::uint8_t {
    None,
    NotXml,      // not XML at all, or XML with no readable root element
    NotZip,      // an .mxl or .mscz that is not a zip archive, or a damaged one
    NotScore,    // XML or zip that holds no score (another XML dialect, no score file in the zip)
    Unsupported, // a score format or version this loader does not read; the message says what to
                 // do (for .mscz: export to MusicXML from MuseScore)
    Corrupt,     // a score that is damaged or contradicts itself
    NoNotes,     // a readable score without a single playable note
    TooLarge,    // more bytes, entries, notes or bars than any real score needs (zip bomb guard)
};

// What the loader skipped or changed. Counts are over the whole file.
struct ScoreLoadStats {
    int grace_notes_dropped = 0; // grace notes have no length; dropped
    int cue_notes_dropped = 0;   // small cue notes show other instruments; they take time, no note
    int unpitched_dropped = 0;   // percussion on a staff line
    int unreadable_notes = 0;    // pitch missing, out of MIDI range, or no duration
    int ties_merged = 0;         // note heads joined into the note before them
    int unmatched_ties = 0;      // a tie stop with nothing to join (kept as a note of its own); a
                                 // stop after a jump counts too
    int overlaps_split = 0;      // same pitch sounding again on one channel: the first one cut
    int duplicates_merged = 0;   // two notes of one pitch starting together on one channel
    int repeats_followed = 0;    // repeat barlines and jumps (D.C., D.S., To Coda) taken
    int repeats_ignored = 0;     // repeat signs left out: unmatched, no jump target, endings with
                                 // no repeat; their bars play straight through
    bool repeats_capped = false; // the expansion passed its cap, so the score plays straight
                                 // through
    int bars_written = 0;        // bars in the file
    int bars_played = 0;         // bars after repeats are expanded (equal when none)
    int repeats_implied = 0;     // endings given a backward repeat the file left out
    int ties_not_continued = 0;  // tie starts whose stop does not play right after them (a jump)
};

struct ScoreLoadResult {
    bool ok = false;
    ScoreFileError error = ScoreFileError::None;
    std::string message; // English detail for logs
    Song song;           // empty unless ok
    ScoreLoadStats stats;
    // The title the file gives, may be empty (UTF-8): MusicXML's work title (with the movement title
    // after a colon), else its movement title, else the words of its title credit (or of its first
    // credit with no type); MuseScore's workTitle, else its movementTitle; ABC's first T: line.
    std::string title;
    // The composer the file names, may be empty (UTF-8): MusicXML's creator of type "composer",
    // else its composer credit; MuseScore's composer meta tag; ABC's C: line.
    std::string composer;
    // Things the loader had to guess, in English, for the log and the song list.
    std::vector<std::string> warnings;
};

// Parses MusicXML (partwise, or timewise converted to partwise) from memory.
//
// Mapping, each with a test in tests/musicxml_test.cpp:
// - `divisions` set the tick length; ticks per quarter is a common multiple of 480 and every
//   `divisions` value of the file (capped at 30240; above that positions are rounded to a tick).
//   A part that writes durations before any `<divisions>` gets them from its first full bar (a
//   4/4 bar of eight units is two units to the quarter note), or 1 when the bars do not say, and
//   a line in ScoreLoadResult::warnings.
// - Tempo: `<sound tempo>` wins, else `<metronome>` (beat unit and dots turned into quarter notes
//   per minute). Read from the first part that has any; default 120. Time signatures likewise from
//   the first part that has any; `3+2` beats are summed, only the first of several signatures in
//   one `<time>` is used.
// - Bars: one per played `<measure>`, numbered 1, 2, 3 in playing order (the `number` attribute is
//   not used: endings and split bars repeat numbers; see Repeats below for what repeats do). A
//   first bar marked implicit, numbered 0 in the file, or shorter than its time signature while a
//   second bar follows is a pickup, numbered 0.
// - Ties (`<tie>`, else `<tied>`) join into one note of the summed length when the pitch matches
//   in the same part, whatever the staff or voice, and the next head starts where the last ended,
//   in playing order (a stop joins the latest open start of its pitch). A tie stop with nothing to
//   join is kept as a note of its own.
// - Chords (`<chord/>`), `<backup>` and `<forward>` (several voices) place notes at their
//   position in the bar.
// - Grace notes are dropped, since they have no length and would need time taken from a neighbour.
//   Cue notes and unpitched (percussion) notes take their time but give no note. Rests move time.
// - Hands: in a part with two or more staves, staff 1 is the Right hand and the others the Left.
//   A part with one staff gets the hand its name says (hand_from_name), else Unknown.
// - Fingering: the first digit 1..5 of `<technical><fingering>` goes to Note::finger.
// - Transposing instruments: `<transpose>` is applied (chromatic and octave-change), so notes are
//   at sounding (concert) pitch. This is the pitch MIDI would carry. The song's own
//   transposition stays 0.
// - Dynamics: pp..ff marks, sf/sfz/fp, `<sound dynamics>` and a note's `dynamics` attribute give
//   Note::velocity (ppp 16, pp 33, p 49, mp 64, mf 80, f 96, ff 112, fff 126). A mark holds
//   in its part until the next. The default is mf.
// - Repeats are expanded once at load into a linear timeline (core/include/etude/repeat_order.hpp
//   has the rules): forward and backward repeat barlines with `times`, first, second and further
//   endings (`<ending number="1, 2">`), nested and back-to-back repeats, D.C., D.S., segno, coda,
//   To Coda and Fine (from `<sound dacapo|dalsegno|segno|coda|tocoda|fine>`, the segno and coda
//   signs, and the words "D.C.", "D.S.", "To Coda", "Fine"). Jumps are per bar: the bar holding the
//   sign plays whole. Each played bar is a Measure with a new `number`; `source_number` is the
//   written bar's number, and a note's `source_measure` follows it. Ties join along the playing
//   order: a stop joins only the note that plays right before it, so a tie into a second ending
//   is struck again on the pass that reaches it. An ending that is not the last and has no
//   backward repeat gets one implied (stats.repeats_implied). Wedges, pedal and articulations are
//   ignored.
// - Parts: one Song::Part per part and staff (track in file order, channel per MusicXML part
//   skipping 10), named after the part, with `midi-program` as the program when given.
//   The same pitch overlapping on one channel is cut or merged as load_midi() does.
ScoreLoadResult load_musicxml(const std::uint8_t* data, std::size_t size);
inline ScoreLoadResult load_musicxml(std::span<const std::uint8_t> data) {
    return load_musicxml(data.data(), data.size());
}

// Parses a compressed MusicXML file: a zip whose META-INF/container.xml names the score
// (the first rootfile); without a usable container, the first `.musicxml` or `.xml` entry outside
// META-INF. Zip limits: kMaxZipEntries entries, kMaxZipEntryBytes for the score and
// kMaxZipTotalBytes declared for all entries; beyond them ScoreFileError::TooLarge.
ScoreLoadResult load_mxl(const std::uint8_t* data, std::size_t size);
inline ScoreLoadResult load_mxl(std::span<const std::uint8_t> data) {
    return load_mxl(data.data(), data.size());
}

// Reads a MuseScore 3 or 4 file: a zip with an embedded `.mscx`. Best effort for the notes: pitch,
// length, dots, tuplets, ties, chords, voices, staves (two staves in a part make Right and Left),
// tempo, time signatures, fingering and dynamics. Grace notes are dropped as above. MuseScore's
// format changes between versions, so a file this cannot read gives ScoreFileError::Unsupported
// and a message that says to export to MusicXML from MuseScore, which is the reliable path.
ScoreLoadResult load_mscz(const std::uint8_t* data, std::size_t size);
inline ScoreLoadResult load_mscz(std::span<const std::uint8_t> data) {
    return load_mscz(data.data(), data.size());
}

// Reads a bare `.mscx` (MuseScore's XML, uncompressed).
ScoreLoadResult load_mscx(const std::uint8_t* data, std::size_t size);

// What a file's first bytes say it is.
enum class SongFileFormat : std::uint8_t { Unknown, Midi, Xml, Zip, Abc };
SongFileFormat sniff_song_format(const std::uint8_t* data, std::size_t size);
inline SongFileFormat sniff_song_format(std::span<const std::uint8_t> data) {
    return sniff_song_format(data.data(), data.size());
}

// Loads a score file whatever its extension: XML is MusicXML (or a bare .mscx, told by its root),
// a zip is .mxl or .mscz (told by what is inside). Anything else is NotScore.
ScoreLoadResult load_score(const std::uint8_t* data, std::size_t size);
inline ScoreLoadResult load_score(std::span<const std::uint8_t> data) {
    return load_score(data.data(), data.size());
}

inline constexpr std::size_t kMaxZipEntries = 1000;
inline constexpr std::size_t kMaxZipEntryBytes = 64u * 1024 * 1024;
inline constexpr std::size_t kMaxZipTotalBytes = 256u * 1024 * 1024;
inline constexpr std::size_t kMaxScoreXmlBytes = kMaxZipEntryBytes;
inline constexpr std::size_t kMaxScoreNotes = kMaxSongNotes;

const char* to_string(ScoreFileError error);

} // namespace etude
