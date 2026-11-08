// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/song.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace etude {

// Versions of one piece: the library holds an easy and an original arrangement, or the same file
// in two keys, and shows them as one piece with a choice of version. Which songs are the same piece
// is decided here, from what the files say (composer, title, opus or catalogue number) and from
// what they sound like (the melody's contour). Pure functions over a few values per song, so the
// app can run them on a thread of its own when the library loads or changes.

// ---- The melody's fingerprint

// The bars of a song the fingerprint reads, from the first note's bar, and the most notes it keeps.
inline constexpr int kMelodyBars = 8;
inline constexpr std::size_t kMelodyMaxNotes = 96;
// A fingerprint of fewer notes, or of fewer than this many different pitches, says too little to
// tell one melody from another (a drone, a trill, a scale).
inline constexpr std::size_t kMelodyMinNotes = 8;
inline constexpr std::size_t kMelodyMinPitches = 4;

// The pitch contour of a song's top line over its first bars: the interval in semitones from each
// note to the next, so it does not depend on the key. The top line is the highest note sounding at
// each onset (a lower note under a longer one still held is an inner voice and left out); a note
// that repeats the one before it is one note, so splitting or joining repeated notes (a simpler
// rhythm) changes nothing.
struct MelodyContour {
    std::vector<std::int8_t> steps; // between consecutive notes; one fewer than the notes

    std::size_t notes() const { return steps.empty() ? 0 : steps.size() + 1; }
    // Enough notes and enough different pitches to compare.
    bool usable() const;
    friend bool operator==(const MelodyContour&, const MelodyContour&) = default;
};

// The song's contour: the notes that are not drums or accompaniment, from the bar of the first one
// for kMelodyBars bars (a pickup bar comes first and does not count). Empty for a song with fewer
// than two such notes.
MelodyContour melody_contour(const Song& song);

// How alike two melodies are, 0 to 1: the two contours lined up end to end, in the key that fits
// them best, as a share of the shorter one. A note on one side only (an ornament, a passing note)
// costs a little, a wrong note more, and notes left over before the first match or after the last
// more still, so a melody that matches only a stretch of a longer one scores low. 0 when either is
// not usable. The same notes in another key score 1; one melody the other with a fifth of its
// notes left out or ornaments added, over 0.8; two other melodies, under 0.5.
double melody_similarity(const MelodyContour& a, const MelodyContour& b);

// ---- What the file says

// A level a file names for itself, in its title or its file name: "Fur Elise (Easy)", "Sonata
// intermediate", "Prelude - level 3". The levels are the library's own words (Easy, Simple, Medium,
// Hard, Expert, level_name) and the words that mean them.
enum class VersionLabel : std::uint8_t {
    None,
    Beginner, // "beginner", "novice", "level 1"
    Easy,     // "easy", "simplified", "level 2"
    Medium,   // "intermediate", "medium", "level 3"
    Hard,     // "advanced", "hard", "level 4"
    Expert,   // "expert", "virtuoso", "level 5"
    Original, // "original", "urtext"
};

// The label the title names, else the file name's; None when neither does.
VersionLabel version_label(std::string_view title, std::string_view file_name = {});
// The label as a chip says it: "Beginner", "Easy", "Medium", "Hard", "Expert", "Original".
std::string_view version_label_name(VersionLabel label);

// The library's word for a level of 1 to 5 stars (level_of_half_stars): Easy, Simple, Medium,
// Hard, Expert (outside that, the nearest end).
std::string_view level_name(int level);

// What a title says of the piece: folded, with the level words (and a "version" or "for" beside
// them), the opus and catalogue numbers and "the" and "a" left out. "Fur Elise (Easy Version)" and
// "Für Elise" give the same text.
std::string piece_title_key(std::string_view title);
// A title as the piece's name, for a card that stands for several versions: the title as written,
// with the level words ("Easy", "level 3") and the "version" or "for" beside them taken out, and
// the brackets and dashes that held them. "Lantern Waltz (Easy)" gives "Lantern Waltz". The title
// itself when nothing else would be left.
std::string piece_name(std::string_view title);
// The opus or catalogue number in a title as one text ("op27/2", "bwv846", "kv331"); empty when it
// names none. "Op. 27 No. 2", "op27 no2" and "Opus 27, No 2" are the same.
std::string catalogue_number(std::string_view title);
// The composer's surname, folded ("Ludwig van Beethoven" and "Beethoven, L. van" give
// "beethoven"); empty for none or for a name that names nobody ("Traditional", "Anonymous").
std::string composer_surname_key(std::string_view composer);

// ---- Grouping

// One song, as grouping sees it.
struct VersionSong {
    std::string title;     // as shown (a name the player gave counts)
    std::string composer;  // may be empty
    std::string file_name; // for a level word in it, and the kind of file
    std::string folder;    // the folders it is in, '/' separated; may be empty
    int half_stars = 0;    // our difficulty rating, 2 to 10; 0 not known
    MelodyContour melody;
};

// A group's most songs: past it, a song is not added (a group that large is a false chain).
inline constexpr std::size_t kMaxVersions = 8;

// Thresholds of melody_similarity. A title and composer that match say the songs are one piece; the
// melody confirms it, or vetoes it when it is too unlike. With a title that differs, or no composer
// to tell, the melody has to carry it alone.
inline constexpr double kVersionVeto = 0.3; // a catalogue number matched: below this, not one piece
inline constexpr double kVersionConfirm = 0.5; // a title matched: at least this, one piece
inline constexpr double kVersionMelody = 0.65; // the melody alone: at least this, one piece

// ---- The player's corrections

// What the player settled by hand, by content hash (so a rename or a move keeps it): songs taken out
// of their piece, and pairs joined into one. Laid over the automatic grouping: a split song is in
// no group whatever the files say; a joined pair is one group whatever the melody says.
struct VersionOverrides {
    std::vector<std::uint64_t> splits;                           // sorted
    std::vector<std::pair<std::uint64_t, std::uint64_t>> joins; // each first < second, sorted

    bool empty() const { return splits.empty() && joins.empty(); }
    bool split(std::uint64_t hash) const;
    // Joined to another song.
    bool joined(std::uint64_t hash) const;
    friend bool operator==(const VersionOverrides&, const VersionOverrides&) = default;
};
struct VersionEdit {
    enum class Kind : std::uint8_t {
        Split, // `a` leaves its piece and stays out; joins of it go
        Join,  // `a` and `b` are one piece; a split of either goes (and of no other song)
        Clear, // `a` is grouped by the files again: its split and its joins go
    };
    Kind kind = Kind::Clear;
    std::uint64_t a = 0;
    std::uint64_t b = 0;
};
// One row of the stored overrides, for the database: a split has b == 0.
struct VersionRow {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    bool add = true; // false: the row goes
};
// Applies `edit`; the rows that changed are appended to `rows` (null: not wanted). A hash of 0, or
// a join of a song with itself, changes nothing.
void apply_version_edit(VersionOverrides& overrides, const VersionEdit& edit,
                        std::vector<VersionRow>* rows = nullptr);

// The same corrections as places in the `songs` given to group_versions.
struct VersionLinks {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> joins;
    std::vector<std::uint32_t> splits;
};

struct VersionMember {
    std::uint32_t song = 0; // index into the songs given
    std::string name;       // the chip's word: the file's own label, else from its stars
    bool labelled = false;  // the name is the file's own label
};
struct VersionGroup {
    std::vector<VersionMember> members; // easiest first (kMaxVersions at most, at least two)
};

// Whether two songs are one piece (by title, composer and number, then the melody; see the
// thresholds above). Symmetric.
bool same_piece(const VersionSong& a, const VersionSong& b);

// The groups of two or more songs that are the same piece, each ordered by our rating (a file's own
// label breaks a tie), named by the file's own label or else by its stars. A name two members share
// is told apart by the word that sets one's title apart from the others' ("Medium", "Medium D"),
// else by the kind of file ("Medium MIDI", "Medium MusicXML"), else by the folder ("Medium
// Lessons"), else numbered ("Medium", "Medium 2"; a number is never put after another number). Every two songs of a group are the
// same piece (not only a chain of pairs). Songs in no group are not returned. The groups are in the
// order of their first song in `songs`.
// `links` are the player's corrections (see VersionOverrides): a split song is in no group, joined
// songs are one group (up to kMaxVersions of them) whatever their files say, and the every-pair rule
// takes a joined pair as the same piece. The player's latest explicit action wins: joining a split
// song (as `a` or `b` of the join) lifts its split, but a join never lifts the split of a song it
// does not name, so one song's join cannot pull a split song back into a piece.
std::vector<VersionGroup> group_versions(std::span<const VersionSong> songs,
                                         const VersionLinks& links = {});

} // namespace etude
