// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace etude {

// What the player reads: a flat is "♭" (U+266D) and a sharp "♯" (U+266F), never the letter b or a
// number sign, so a flat can't be mistaken for the note B. Everything the code builds, stores,
// parses and searches by keeps the plain spelling ("Bb", "F#", "Ebm7b5/Bb"); only the text that
// reaches the screen goes through the two functions below.
inline constexpr std::string_view kFlatSign = "\xE2\x99\xAD";
inline constexpr std::string_view kSharpSign = "\xE2\x99\xAF";

// Display text of a name the code spelled itself: a note ("Bb3", "F#", "Reb"), a chord
// ("Ebm7b5/Bb", "C7#11", "Bbmaj7"), a key or a scale ("Bb major", "F# dorian"). A name starts the
// text and follows a space, "/" or "(". There a letter A to G or a solfège syllable (Do Re Mi Fa
// Sol La Si) followed by b or # takes them as accidentals, so the note B ("B", "B major", "Blues")
// stays B. Anywhere else a # is a sharp and a b before a digit is a flat degree ("b5", "b9", "b13"
// become ♭5, ♭9, ♭13). Text that holds none of these comes back unchanged. For generated note and
// chord names only: it converts inside words ("Fabric", "Abba"), so free text must use
// accidental_display_text_into.
std::string accidental_display(std::string_view spelled);

// Display text of free text that names notes among other words, such as a song's title ("Prelude in
// Bb major", "Sonata in C# minor", "Cm7b5 arpeggio"): only a whole word that starts with a capital
// A to G and is a note or chord name changes: accidentals, then a quality and a number, alterations
// ("b5", "#9", "#11", "b13") and a slash bass ("Bb7/D", "C#m/G#"). "Abbey", "Abba", "Bbq", "Bebop"
// and "BB" are left alone. A word ends at a space, ASCII or UTF-8 punctuation (curly quotes,
// dashes, guillemets); accented letters belong to the word ("Été", "Café").
std::string accidental_display_text(std::string_view text);

// `accidental_display_text` into `out` (cleared first), which keeps its capacity: a draw path that
// reuses one string allocates nothing once it has grown.
void accidental_display_text_into(std::string_view text, std::string& out);

// Whether `accidental_display_text` would change `text`; no allocation, so a draw path can skip the
// copy for the many labels that name no note.
bool has_accidental_spelling(std::string_view text);

// The reverse, for matching: "B♭" folds to "Bb" and "F♯" to "F#" (a search for "B♭" finds "Bb").
std::string plain_accidentals(std::string_view display);

} // namespace etude
