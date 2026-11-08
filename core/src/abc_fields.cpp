// SPDX-License-Identifier: GPL-3.0-or-later
#include "abc_fields.hpp"

#include <algorithm>
#include <cstring>

namespace etude::abc_detail {

int letter_index(char c) {
    switch (c) {
    case 'C':
    case 'c':
        return 0;
    case 'D':
    case 'd':
        return 1;
    case 'E':
    case 'e':
        return 2;
    case 'F':
    case 'f':
        return 3;
    case 'G':
    case 'g':
        return 4;
    case 'A':
    case 'a':
        return 5;
    case 'B':
    case 'b':
        return 6;
    default:
        return -1;
    }
}

KeySig key_from_fifths(int fifths) {
    // Order of sharps F C G D A E B, in letter indexes; flats are the reverse.
    constexpr int kSharpOrder[7] = {3, 0, 4, 1, 5, 2, 6};
    constexpr int kFlatOrder[7] = {6, 2, 5, 1, 4, 0, 3};
    KeySig sig{};
    fifths = std::clamp(fifths, -14, 14);
    for (int k = 0; k < std::abs(fifths); ++k) {
        const int letter = fifths > 0 ? kSharpOrder[k % 7] : kFlatOrder[k % 7];
        sig[static_cast<std::size_t>(letter)] += fifths > 0 ? 1 : -1;
    }
    return sig;
}

std::vector<Word> split_words(std::string_view s) {
    std::vector<Word> words;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && is_space(s[i])) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        Word w;
        std::size_t start = i;
        if (s[i] == '=') {
            // A natural in a key, "=f", is a word of its own.
            ++i;
        }
        while (i < s.size() && !is_space(s[i]) && s[i] != '=' && s[i] != '"') {
            ++i;
        }
        w.key = s.substr(start, i - start);
        if (i < s.size() && s[i] == '=') {
            ++i;
            if (i < s.size() && s[i] == '"') {
                ++i;
                const std::size_t close = s.find('"', i);
                const std::size_t stop = close == std::string_view::npos ? s.size() : close;
                w.value = std::string(s.substr(i, stop - i));
                i = close == std::string_view::npos ? s.size() : close + 1;
            } else {
                start = i;
                while (i < s.size() && !is_space(s[i])) {
                    ++i;
                }
                w.value = std::string(s.substr(start, i - start));
            }
        } else if (i < s.size() && s[i] == '"') {
            // A quoted word with no key (a voice name): skip it.
            const std::size_t close = s.find('"', i + 1);
            i = close == std::string_view::npos ? s.size() : close + 1;
        }
        if (!w.key.empty() || !w.value.empty()) {
            words.push_back(std::move(w));
        }
    }
    return words;
}

bool parse_signed(const std::string& s, int& out) {
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        negative = s[i] == '-';
        ++i;
    }
    if (i >= s.size() || !is_digit(s[i])) {
        return false;
    }
    long long v = read_number(s, i, 1000);
    out = static_cast<int>(negative ? -v : v);
    return true;
}

void read_voice_words(const std::vector<Word>& words, KeyInfo& info) {
    int semitones = 0;
    int octaves = 0;
    for (const Word& w : words) {
        if (equals_nocase(w.key, "clef") || equals_nocase(w.key, "bass") ||
            equals_nocase(w.key, "treble")) {
            const std::string_view name = w.value.empty() ? w.key : std::string_view(w.value);
            info.has_clef = true;
            info.clef_bass = starts_with_nocase(name, "bass");
            info.clef_treble = starts_with_nocase(name, "treble");
        } else if (equals_nocase(w.key, "transpose")) {
            int v = 0;
            if (parse_signed(w.value, v)) {
                semitones = std::clamp(v, -36, 36);
                info.has_shift = true;
            }
        } else if (equals_nocase(w.key, "octave")) {
            int v = 0;
            if (parse_signed(w.value, v)) {
                octaves = std::clamp(v, -4, 4);
                info.has_shift = true;
            }
        }
    }
    info.shift = semitones + 12 * octaves;
}

bool parse_key(std::string_view value, KeyInfo& out) {
    out = KeyInfo{};
    const std::vector<Word> words = split_words(value);
    read_voice_words(words, out);
    if (words.empty()) {
        return true;
    }
    std::size_t next = 0;
    int fifths = 0;
    int mode_shift = 0;
    bool ok = true;
    bool explicit_only = false;
    bool tonic_named = false;
    const std::string_view first = words[0].key;
    if (words[0].value.empty() && equals_nocase(first, "none")) {
        next = 1;
    } else if (words[0].value.empty() && (first == "HP" || first == "Hp")) {
        fifths = 2;
        next = 1;
    } else if (words[0].value.empty() && !first.empty() && letter_index(first[0]) >= 0 &&
               !equals_nocase(first, "exp") && !equals_nocase(first, "bass") &&
               !equals_nocase(first, "treble")) {
        // The tonic is a letter a-g in either case ("gm" is G minor); the words that start with a
        // letter and mean something else (exp, bass, treble) are not tonics.
        constexpr int kMajorFifths[7] = {0, 2, 4, -1, 1, 3, 5};
        fifths = kMajorFifths[letter_index(first[0])];
        tonic_named = true;
        std::size_t p = 1;
        if (p < first.size() && first[p] == '#') {
            fifths += 7;
            ++p;
        } else if (p < first.size() && first[p] == 'b') {
            fifths -= 7;
            ++p;
        }
        next = 1;
        std::string_view mode = first.substr(p);
        if (mode.empty() && words.size() > 1 && words[1].value.empty()) {
            const std::string_view w = words[1].key;
            const bool is_mode = starts_with_nocase(w, "maj") || starts_with_nocase(w, "min") ||
                                 starts_with_nocase(w, "ion") || starts_with_nocase(w, "mix") ||
                                 starts_with_nocase(w, "dor") || starts_with_nocase(w, "phr") ||
                                 starts_with_nocase(w, "lyd") || starts_with_nocase(w, "loc") ||
                                 starts_with_nocase(w, "aeo") || w == "m" || w == "M";
            if (is_mode) {
                mode = w;
                next = 2;
            }
        }
        if (!mode.empty()) {
            if (mode == "m") {
                mode_shift = -3; // only a lower-case m alone is minor
            } else if (mode == "M") {
                out.lone_capital_m = true; // major, with its own warning
            } else if (mode.size() >= 3) {
                const std::string m = {lower_of(mode[0]), lower_of(mode[1]), lower_of(mode[2])};
                if (m == "maj" || m == "ion") {
                    mode_shift = 0;
                } else if (m == "mix") {
                    mode_shift = -1;
                } else if (m == "dor") {
                    mode_shift = -2;
                } else if (m == "min" || m == "aeo") {
                    mode_shift = -3;
                } else if (m == "phr") {
                    mode_shift = -4;
                } else if (m == "lyd") {
                    mode_shift = 1;
                } else if (m == "loc") {
                    mode_shift = -5;
                } else {
                    ok = false;
                }
            } else {
                ok = false;
            }
        }
    } else if (!words[0].value.empty() || equals_nocase(first, "clef") ||
               equals_nocase(first, "bass") || equals_nocase(first, "treble") ||
               equals_nocase(first, "exp")) {
        // Only parameters, or a bare `exp`: C major.
        next = 0;
    } else {
        ok = false;
        next = 1;
    }
    out.sig = key_from_fifths(fifths + mode_shift);
    if (tonic_named) {
        out.has_tonic = true;
        out.tonic_line = fifths;
        out.mode = mode_from_line_shift(-mode_shift);
    }
    // `exp` and the accidentals that follow.
    KeySig extra{};
    std::array<bool, 7> extra_set{};
    for (std::size_t w = next; w < words.size(); ++w) {
        if (!words[w].value.empty()) {
            continue;
        }
        const std::string_view k = words[w].key;
        if (equals_nocase(k, "exp")) {
            explicit_only = true;
            continue;
        }
        std::size_t p = 0;
        while (p < k.size()) {
            int acc = 0;
            if (k[p] == '^' || k[p] == '_') {
                const char c = k[p];
                while (p < k.size() && k[p] == c) {
                    acc += c == '^' ? 1 : -1;
                    ++p;
                }
            } else if (k[p] == '=') {
                acc = 0;
                ++p;
            } else {
                break;
            }
            if (p < k.size() && letter_index(k[p]) >= 0) {
                const auto letter = static_cast<std::size_t>(letter_index(k[p]));
                extra[letter] = acc;
                extra_set[letter] = true;
                ++p;
            } else {
                break;
            }
        }
    }
    if (explicit_only) {
        out.sig = KeySig{};
    }
    for (std::size_t l = 0; l < 7; ++l) {
        if (extra_set[l]) {
            out.sig[l] = extra[l];
        }
    }
    return ok;
}

bool parse_meter(std::string_view value, Meter& out) {
    const std::string_view v = trim(value);
    out = Meter{};
    if (v.empty() || equals_nocase(v, "none")) {
        out.free = true;
        return true;
    }
    if (v == "C") {
        return true;
    }
    if (v == "C|") {
        out.numerator = 2;
        out.denominator = 2;
        return true;
    }
    long long num = 0;
    long long den = 0;
    bool in_den = false;
    bool any_num = false;
    std::size_t i = 0;
    while (i < v.size()) {
        if (is_digit(v[i])) {
            const long long n = read_number(v, i, 100000);
            (in_den ? den : num) += n;
            any_num = any_num || !in_den;
        } else if (v[i] == '/' && !in_den) {
            in_den = true;
            ++i;
        } else if (v[i] == '+' || v[i] == '(' || v[i] == ')' || is_space(v[i])) {
            ++i;
        } else {
            return false;
        }
    }
    if (!any_num || num < 1 || num > 255 || den < 1 || den > 64 || (den & (den - 1)) != 0) {
        return false;
    }
    out.numerator = static_cast<int>(num);
    out.denominator = static_cast<int>(den);
    return true;
}

bool parse_fraction(std::string_view v, long long& num, long long& den) {
    std::size_t i = 0;
    if (i >= v.size() || !is_digit(v[i])) {
        return false;
    }
    num = read_number(v, i, 100000);
    if (i >= v.size() || v[i] != '/') {
        return false;
    }
    ++i;
    if (i >= v.size() || !is_digit(v[i])) {
        return false;
    }
    den = read_number(v, i, 100000);
    return num >= 1 && den >= 1;
}

bool parse_tempo(std::string_view value, double& quarters_per_minute) {
    std::string plain;
    bool quoted = false;
    for (char c : value) {
        if (c == '"') {
            quoted = !quoted;
        } else if (!quoted) {
            plain += c;
        }
    }
    const std::string_view v = trim(plain);
    const std::size_t eq = v.find('=');
    double beat_quarters = 1.0;
    std::string_view number = v;
    if (eq != std::string_view::npos) {
        number = trim(v.substr(eq + 1));
        std::string_view left = trim(v.substr(0, eq));
        // The first beat length counts: "1/4 3/8=40" is taken as "1/4=40".
        const std::size_t cut = left.find(' ');
        if (cut != std::string_view::npos) {
            left = left.substr(0, cut);
        }
        long long n = 0;
        long long d = 0;
        if (left == "C") {
            beat_quarters = 1.0;
        } else if (parse_fraction(left, n, d)) {
            beat_quarters = 4.0 * static_cast<double>(n) / static_cast<double>(d);
        } else {
            return false;
        }
    }
    const std::string text(number);
    double bpm = 0.0;
    if (!score_detail::parse_double(text.c_str(), bpm) || !(bpm > 0.0) || bpm > 1000.0) {
        return false;
    }
    quarters_per_minute = bpm * beat_quarters;
    return quarters_per_minute > 0.0 && quarters_per_minute <= 4000.0;
}

Frac read_length(std::string_view s, std::size_t& i) {
    Frac f;
    if (i < s.size() && is_digit(s[i])) {
        f.num = std::max<long long>(read_number(s, i, kMaxLengthPart), 0);
    }
    if (i < s.size() && s[i] == '/') {
        int slashes = 0;
        while (i < s.size() && s[i] == '/') {
            ++slashes;
            ++i;
        }
        if (slashes == 1 && i < s.size() && is_digit(s[i])) {
            f.den = std::max<long long>(read_number(s, i, kMaxLengthPart), 1);
        } else {
            f.den = std::min<long long>(1LL << std::min(slashes, 10), kMaxLengthPart);
        }
    }
    return f;
}

bool parse_chord_symbol(std::string_view text, ChordSymbol& out) {
    std::string_view s = trim(text);
    while (!s.empty() && s.front() == '(') {
        s.remove_prefix(1);
    }
    if (s.empty()) {
        return false;
    }
    if (starts_with_nocase(s, "N.C") || equals_nocase(s, "NC")) {
        out.none = true;
        return true;
    }
    if (s[0] < 'A' || s[0] > 'G') {
        return false;
    }
    int root = kLetterPitchClass[letter_index(s[0])];
    std::size_t p = 1;
    if (p < s.size() && (s[p] == '#' || s[p] == 'b')) {
        // "Bb" and "Ab" are flats; a lone "b" after a root is a flat as well.
        root += s[p] == '#' ? 1 : -1;
        ++p;
    }
    std::string_view rest = s.substr(p);
    std::string_view quality = rest;
    std::string_view slash;
    const std::size_t cut = rest.find('/');
    if (cut != std::string_view::npos) {
        quality = rest.substr(0, cut);
        slash = rest.substr(cut + 1);
    }
    // Only qualities a chord name has; a word like "Chorus" or "Dorian" is an annotation.
    if (quality.size() > 12) {
        return false;
    }
    if (!quality.empty()) {
        const char c = quality[0];
        const bool known =
            c == 'm' || c == 'M' || c == '+' || c == '-' || is_digit(c) || c == '(' || c == '#' ||
            c == 'b' || c == ')' || starts_with_nocase(quality, "dim") ||
            starts_with_nocase(quality, "aug") || starts_with_nocase(quality, "add") ||
            starts_with_nocase(quality, "sus") || quality.substr(0, 2) == "\xC2\xB0" ||
            quality.substr(0, 2) == "\xC3\xB8";
        if (!known) {
            return false;
        }
    }
    std::string lowered;
    for (char c : quality) {
        lowered += lower_of(c);
    }
    int fifth = 7;
    if (lowered.find("dim") != std::string::npos || lowered.find("b5") != std::string::npos ||
        lowered.find("\xC2\xB0") != std::string::npos ||
        lowered.find("\xC3\xB8") != std::string::npos) {
        fifth = 6;
    } else if (lowered.find("aug") != std::string::npos || lowered.find('+') != std::string::npos ||
               lowered.find("#5") != std::string::npos) {
        fifth = 8;
    }
    out.root = ((root % 12) + 12) % 12;
    out.fifth = fifth;
    if (!slash.empty() && slash[0] >= 'A' && slash[0] <= 'G') {
        int bass = kLetterPitchClass[letter_index(slash[0])];
        if (slash.size() > 1 && slash[1] == '#') {
            ++bass;
        } else if (slash.size() > 1 && slash[1] == 'b') {
            --bass;
        }
        out.bass = ((bass % 12) + 12) % 12;
    }
    return true;
}

} // namespace etude::abc_detail
