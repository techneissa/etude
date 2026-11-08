// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/instrument.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace etude {

namespace {

// Whole words of part and instrument names, lower case. A word that is in no table (a role like
// "melody", "chords", "bass", "lead", "right", "left", "rh", "lh", "treble", "accomp", "track" or
// "hand") says nothing about the instrument, and neither does a number.
constexpr std::string_view kKeyboardWords[] = {
    "piano", "pianos", "pianoforte",  "pno",     "klavier",    "keyboard", "keyboards", "organ",
    "orgel", "organo", "harpsichord", "cembalo", "clavichord", "clavinet", "celesta",   "celeste"};

constexpr std::string_view kOtherWords[] = {
    "voice",     "voices",    "vocal",      "vocals",       "vox",        "singstimme",
    "gesang",    "singer",    "singers",    "soprano",      "alto",       "tenor",
    "baritone",  "choir",     "choirs",     "chorus",       "chor",       "canto",
    "violin",    "violins",   "violine",    "geige",        "viola",      "violas",
    "bratsche",  "cello",     "cellos",     "violoncello",  "contrabass", "flute",
    "flutes",    "floete",    "piccolo",    "oboe",         "clarinet",   "bassoon",
    "fagott",    "saxophone", "sax",        "horn",         "trumpet",    "trompete",
    "trombone",  "tuba",      "guitar",     "guitars",      "gitarre",    "ukulele",
    "banjo",     "mandolin",  "harp",       "strings",      "streicher",  "recorder",
    "marimba",   "xylophone", "vibraphone", "glockenspiel", "timpani",    "percussion",
    "accordion", "akkordeon", "harmonica",  "mundharmonika"};

constexpr std::string_view kVoiceWords[] = {
    "voice",   "voices",  "vocal",  "vocals",  "vox",     "singstimme", "gesang",
    "singer",  "singers", "soprano", "alto",   "tenor",   "baritone",   "choir",
    "choirs",  "chorus",  "chor",   "canto"};

// Lower-case words of `text`: runs of letters, so "Voice1" is "voice" and "Chords" is one word.
std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> words(1);
    for (char c : text) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (letter) {
            words.back() += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        } else if (!words.back().empty()) {
            words.emplace_back();
        }
    }
    if (words.back().empty()) {
        words.pop_back();
    }
    return words;
}

bool names_any(const std::vector<std::string>& words, std::span<const std::string_view> table) {
    return std::any_of(words.begin(), words.end(), [&](const std::string& w) {
        return std::find(table.begin(), table.end(), w) != table.end();
    });
}

bool keyboard_program(int program) {
    return std::any_of(std::begin(kKeyboardProgramRanges), std::end(kKeyboardProgramRanges),
                       [&](const int* r) { return program >= r[0] && program <= r[1]; });
}

} // namespace

InstrumentKind instrument_of_text(std::string_view text) {
    const std::vector<std::string> words = words_of(text);
    if (names_any(words, kOtherWords)) {
        return InstrumentKind::Other;
    }
    if (names_any(words, kKeyboardWords)) {
        return InstrumentKind::Keyboard;
    }
    return InstrumentKind::Unknown;
}

InstrumentKind classify_instrument(const Part& part) {
    const std::string_view texts[] = {part.instrument_id, part.instrument_name, part.name};
    for (const std::string_view text : texts) {
        if (instrument_of_text(text) == InstrumentKind::Other) {
            return InstrumentKind::Other;
        }
    }
    if (part.has_program) {
        return keyboard_program(part.program) ? InstrumentKind::Keyboard : InstrumentKind::Other;
    }
    for (const std::string_view text : texts) {
        if (instrument_of_text(text) == InstrumentKind::Keyboard) {
            return InstrumentKind::Keyboard;
        }
    }
    return InstrumentKind::Unknown;
}

bool is_voice_part(const Part& part) {
    const std::string_view texts[] = {part.instrument_id, part.instrument_name, part.name};
    bool other = false;
    for (const std::string_view text : texts) {
        const std::vector<std::string> words = words_of(text);
        if (names_any(words, kVoiceWords)) {
            return true;
        }
        other = other || names_any(words, kOtherWords);
    }
    return !other && part.has_program && part.program >= 52 && part.program <= 54;
}

bool has_keyboard_part(const Song& song) {
    return std::any_of(song.parts.begin(), song.parts.end(), [](const Part& p) {
        return !p.drum && classify_instrument(p) == InstrumentKind::Keyboard;
    });
}

bool is_accompaniment_part(const Part& part, bool song_has_keyboard) {
    return !part.drum && classify_instrument(part) == InstrumentKind::Other && song_has_keyboard;
}

bool is_accompaniment_part(const Song& song, const Part& part) {
    return is_accompaniment_part(part, has_keyboard_part(song));
}

} // namespace etude
