// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/musicxml.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

// ABC notation (`.abc`): a small text format for tunes, mostly one melody line with chord symbols.
// Like the score loaders, this works on bytes the caller read (core has no file system), never
// throws, never reads outside the bytes, never loops without consuming input, and loads what it can
// from a damaged tune with a line in ScoreLoadResult::warnings for what it had to skip or guess.
//
// A file holds one or more tunes. Each starts at an `X:` line and runs to the next `X:` line;
// whatever comes before the first `X:` is the file header, whose fields (L:, M:, Q:, K:, C:, ...)
// are the defaults of every tune.

// One tune of a file, as the library lists it.
struct AbcTune {
    // How the tune is addressed inside its file: "X=3", or "X=3.2" for the second tune of the file
    // that is numbered 3 (a damaged collection may repeat a number). The library writes it after
    // the file's path, `songs.abc#X=3`.
    std::string address;
    long long number = 0; // the X: number, 0 when there is none
    // The X: value was larger than kMaxTuneNumber (a million): `number` is the tune's place in the
    // file (1 for the first) instead, with the usual ".2" for a number that is already taken.
    bool number_replaced = false;
    // Set on the last listed tune of a file that has more than kMaxAbcTunes: the tunes after it are
    // left out of the list.
    bool more_tunes_follow = false;
    std::string title;     // the first T: line, UTF-8, may be empty
    std::size_t begin = 0; // the tune's text is bytes [begin, end) of the file
    std::size_t end = 0;
    std::size_t header_end = 0; // the file header is bytes [0, header_end)
    std::size_t line = 1;       // the line `begin` is on, counting from 1
    // The song's identity in the practice history: content_hash() of the file header followed by
    // the tune's text. A file holding one tune and no header text of its own therefore has the hash
    // of the whole file, as every other format does, and a tune keeps its history when other tunes
    // of its file are added, edited or reordered.
    std::uint64_t hash = 0;
};

// The most tunes listed from one file; the tunes after them are left out.
inline constexpr std::size_t kMaxAbcTunes = 5000;
// The largest X: number kept as it is.
inline constexpr long long kMaxAbcTuneNumber = 1'000'000;
// The most bytes of a file that are read.
inline constexpr std::size_t kMaxAbcBytes = 16u * 1024 * 1024;

// Lists the tunes of a file without parsing their music. An empty list when the bytes hold no tune
// (not ABC at all, or no `X:` line).
std::vector<AbcTune> list_abc_tunes(const std::uint8_t* data, std::size_t size);
inline std::vector<AbcTune> list_abc_tunes(std::span<const std::uint8_t> data) {
    return list_abc_tunes(data.data(), data.size());
}

struct AbcOptions {
    // The tune to load, as AbcTune::address; empty loads the first tune of the file. An address
    // that names no tune of the file is a load error (NotScore).
    std::string tune;
    // The tune as list_abc_tunes() gave it for these same bytes. Loading many tunes of one big file
    // passes it, so the file is listed once instead of once per tune. Takes the place of `tune`; a
    // reference that does not fit the bytes is a load error.
    const AbcTune* listed = nullptr;
    // Chord symbols ("Am", "G7") become a left-hand part: on each chord change the root and the
    // fifth above it (the root alone for a slash chord, whose bass note is the one after the
    // slash), in the octave from C2, held until the next chord symbol or the end of the bar. The
    // tune's own notes then go to the right hand. A chord note is left out only while one of the
    // tune's own notes sounds on the same key (it is never moved to another key): it goes on after
    // that note has ended, to the end of the chord. A tune with no chord symbols gets no such part.
    // Off by default, because a single melodic line stays in one hand.
    bool chords_as_left_hand = false;
};

// Parses one tune of an ABC file (ABC 2.1, the part that matters for playing):
//
// - Header fields: X (number), T (title; the first one names the tune), C (composer), M (meter:
//   `4/4`, `C`, `C|`, `none`, `(2+3+2)/8`), L (unit note length; without one it is 1/8, or 1/16 for
//   a meter under 3/4), Q (tempo: `1/4=120`, `3/8=60`, `"Allegro" 1/4=120`, a bare `120` counts
//   quarter notes; default 120), K (tonic with `#` or `b`, mode: major, minor, ionian, dorian,
//   phrygian, lydian, mixolydian, aeolian, locrian, any abbreviation of three letters or `m`;
//   a lower-case tonic is read too (`gm`); only a lone lower-case `m` is minor, a lone `M` is major
//   with a warning;
//   `exp` and explicit accidentals `^f _b =c`; `none`, `HP`, `Hp`). V (voices) and `clef=` /
//   `transpose=` / `octave=` / `name=` on K: and V:. Fields in the body, on their own line or
//   inline in `[K:G]`, change the key, meter, unit length and tempo from there. Other fields are
//   skipped.
// - Notes `C D E F G A B c d e f g a b` (C is middle C, lower case is the octave above), `,` and
// `'`
//   for other octaves, `^ ^^ _ __ =` accidentals that last to the end of the bar for that pitch in
//   that octave (and into the next bar for the note a tie leads to), on top of the key. Lengths
//   `2`, `/2`, `/`, `3/2`, `//`; broken rhythm `>` `<` `>>` `<<`; rests `z` `x` `Z` (a bar's rest,
//   `Z4` four bars); `y` spacers are skipped. Ties `-`; slurs `(` `)`, decorations (`!trill!`,
//   `+fermata+`, `.`, `~`, `H`, `T`, ...), annotations `"^text"`, and lyrics lines are ignored.
//   Dynamics `!p!` `!mf!` `!ff!` and so on set the velocity. Chords `[CEG]2`: the first note's
//   length, times the outer length, is how long the chord lasts. Triplets `(3abc` and general
//   tuplets `(p:q:r` (p notes in the time of q, over the next r notes; q defaults as in the
//   standard). Grace notes `{gab}` are dropped and counted in stats.grace_notes_dropped.
// - Bar lines `| || |] [| :| |: :|: ::`; a repeat expands once at load, with first and second
//   endings (`|1`, `[1`, `:|1` and `:|[1` (which are `|1`: the repeat closes at the first ending's
//   own `:|`), `:|2`, `[1,2`, `[1-3`) through plan_repeats() (repeat_order.hpp), so a
//   tune's played bars are numbered 1, 2, 3 in order and `source_number` keeps the written bar.
//   `!D.C.!`, `!D.S.!`, `!segno!`, `!coda!` and `!fine!` give the jumps of that file. A first bar
//   shorter than the meter is a pickup, numbered 0 (first_bar_is_pickup), and a last bar that
//   completes it is not padded.
// - Voices: each voice that has notes is a part with its own track, named after `name=` or the
//   voice id. A voice's hand is Left or Right when its name says so (hand_from_name) or, in a tune
//   whose voices differ in clef, when its clef is bass or treble; otherwise Unknown, so a single
//   line goes whole to one hand (hand_split.hpp). A `&` starts an overlay: a second line of notes
//   in the same bar, back at the bar's start.
// - Ties join along the playing order like the score loaders (score_raw.cpp).
// - Text that is not valid UTF-8 is read as Latin-1.
//
// Limits: kMaxAbcBytes, kMaxScoreNotes notes and kMaxSongMeasures bars; past them the rest of the
// tune is left out with a warning. A tune without a note is NoNotes.
ScoreLoadResult load_abc(const std::uint8_t* data, std::size_t size,
                         const AbcOptions& options = {});
inline ScoreLoadResult load_abc(std::span<const std::uint8_t> data,
                                const AbcOptions& options = {}) {
    return load_abc(data.data(), data.size(), options);
}

// Where a tune is inside a file, written after the file's path: "folk.abc#X=3". The address is the
// text after the last '#' when it has the form of AbcTune::address ("X=" and digits, then "." and
// digits for a repeated number); any other text, '#' included, is all path.
struct AbcLocation {
    std::string_view file;
    std::string_view tune; // empty when the text has no address
};
AbcLocation split_abc_location(std::string_view text);
// The path and address as one text; just the path for an empty address.
std::string join_abc_location(std::string_view file, std::string_view tune);

// True when the bytes read as ABC: the first line is `%abc`, or an `X:` line comes before the
// first K: line within the first few kilobytes of text with no NUL byte.
bool looks_like_abc(const std::uint8_t* data, std::size_t size);

} // namespace etude
