// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library.hpp"

#include "library_internal.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace etude {

namespace {

// White space and control characters collapse to one space; the ends are trimmed.
std::string collapse_spaces(std::string_view s) {
    std::string out;
    bool pending_space = false;
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (is_space_or_control(c)) {
            pending_space = !out.empty();
        } else {
            if (pending_space) {
                out.push_back(' ');
                pending_space = false;
            }
            out.push_back(ch);
        }
    }
    return out;
}

std::string cap_utf8(std::string s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) {
        return s;
    }
    std::size_t cut = max_bytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    s.resize(cut);
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    return s;
}

} // namespace

std::string clean_text(std::string_view valid_utf8) {
    std::string underscores(valid_utf8);
    std::replace(underscores.begin(), underscores.end(), '_', ' ');
    return cap_utf8(collapse_spaces(underscores), kMaxLibraryTitleBytes);
}

namespace {

bool has_known_extension(std::string_view name, std::size_t& dot) {
    static constexpr std::array<std::string_view, 11> kExtensions = {
        "mid", "midi", "kar", "smf", "rmi", "musicxml", "xml", "mxl", "mscz", "mscx", "abc"};
    dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        return false;
    }
    std::string ext;
    for (char c : name.substr(dot + 1)) {
        ext.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c);
    }
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

} // namespace

// A title that has a letter or digit of some script (not only punctuation or U+FFFD).
bool usable_title(std::string_view title) {
    for (char32_t cp : code_points(title)) {
        if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
            (cp >= 0xC0 && cp != kReplacement && !(cp >= 0x2000 && cp <= 0x206F) &&
             !(cp >= 0x3000 && cp <= 0x303F) && !(cp >= 0xFF00 && cp <= 0xFF0F))) {
            return true;
        }
    }
    return false;
}

namespace {

bool generic_title(std::string_view title) {
    std::string key = fold_for_search(title);
    static constexpr std::array<std::string_view, 15> kGeneric = {
        "untitled",      "unnamed",  "noname",  "no name", "no title",
        "new song",      "newsong",  "song",    "midi",    "track",
        "control track", "sequence", "default", "music",   "piano"};
    if (std::find(kGeneric.begin(), kGeneric.end(), key) != kGeneric.end()) {
        return true;
    }
    auto all_digits = [](std::string_view s) {
        return !s.empty() &&
               std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (all_digits(key)) {
        return true;
    }
    for (std::string_view prefix : {"track ", "midi ", "song "}) {
        if (key.starts_with(prefix) && all_digits(std::string_view(key).substr(prefix.size()))) {
            return true;
        }
    }
    return false;
}

// ---- MIDI sequence name

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}
    std::size_t pos() const { return pos_; }
    bool at_end() const { return pos_ >= data_.size(); }
    bool skip(std::size_t n) {
        if (n > data_.size() - pos_) {
            return false;
        }
        pos_ += n;
        return true;
    }
    bool u8(std::uint8_t& v) {
        if (at_end()) {
            return false;
        }
        v = data_[pos_++];
        return true;
    }
    bool be32(std::uint32_t& v) {
        if (data_.size() - pos_ < 4) {
            return false;
        }
        v = (std::uint32_t{data_[pos_]} << 24) | (std::uint32_t{data_[pos_ + 1]} << 16) |
            (std::uint32_t{data_[pos_ + 2]} << 8) | data_[pos_ + 3];
        pos_ += 4;
        return true;
    }
    bool vlq(std::uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            std::uint8_t b = 0;
            if (!u8(b)) {
                return false;
            }
            v = (v << 7) | (b & 0x7Fu);
            if ((b & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }
    std::span<const std::uint8_t> take(std::size_t n) {
        if (n > data_.size() - pos_) {
            return {};
        }
        auto out = data_.subspan(pos_, n);
        pos_ += n;
        return out;
    }
    std::span<const std::uint8_t> rest() const { return data_.subspan(pos_); }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

} // namespace

namespace {

bool ascii_digit(char c) { return c >= '0' && c <= '9'; }
bool ascii_lower(char c) { return c >= 'a' && c <= 'z'; }
bool ascii_upper(char c) { return c >= 'A' && c <= 'Z'; }
char to_lower(char c) { return ascii_upper(c) ? static_cast<char>(c + 32) : c; }
char to_upper(char c) { return ascii_lower(c) ? static_cast<char>(c - 32) : c; }

bool has_letter(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (ascii_lower(static_cast<char>(c)) || ascii_upper(static_cast<char>(c)) || c >= 0xC0) {
            return true;
        }
    }
    return false;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space_or_control(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space_or_control(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

bool ends_with_folded(std::string_view s, std::string_view lower_suffix) {
    if (s.size() < lower_suffix.size()) {
        return false;
    }
    const std::string_view tail = s.substr(s.size() - lower_suffix.size());
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (to_lower(tail[i]) != lower_suffix[i]) {
            return false;
        }
    }
    return true;
}

// "(1)", " copy", " copy 2", " - Copy" at the end.
std::string_view drop_copy_mark(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.back() == ')') {
        std::size_t i = s.size() - 1;
        std::size_t digits = 0;
        while (i > 0 && ascii_digit(s[i - 1])) {
            --i;
            ++digits;
        }
        if (digits > 0 && i > 0 && s[i - 1] == '(' && has_letter(s.substr(0, i - 1))) {
            return trim(s.substr(0, i - 1));
        }
    }
    std::string_view t = s;
    while (!t.empty() && ascii_digit(t.back())) {
        t.remove_suffix(1);
    }
    if (t.size() != s.size()) {
        t = trim(t);
    } else {
        t = s;
    }
    for (std::string_view mark : {" - copy", " copy"}) {
        if (ends_with_folded(t, mark) && has_letter(t.substr(0, t.size() - mark.size()))) {
            return trim(t.substr(0, t.size() - mark.size()));
        }
    }
    return s;
}

// A track number in front: digits, then a separator, or a leading zero and a space.
std::string_view drop_track_number(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && ascii_digit(s[i]) && i < 4) {
        ++i;
    }
    if (i == 0 || i == 4 || i >= s.size()) {
        return s;
    }
    std::size_t j = i;
    bool separated = false;
    if (s[j] == '.' || s[j] == '_' || s[j] == '-' || s[j] == ')') {
        separated = true;
        ++j;
    } else if (s[j] == ' ') {
        std::size_t k = j;
        while (k < s.size() && s[k] == ' ') {
            ++k;
        }
        if (k < s.size() && (s[k] == '-' || s[k] == '.' || s[k] == '_')) {
            separated = true;
            j = k + 1;
        } else {
            separated = s[0] == '0';
            j = k;
        }
    }
    if (!separated) {
        return s;
    }
    const std::string_view rest = trim(s.substr(j));
    return has_letter(rest) ? rest : s;
}

// Dashes as word separators become spaces: all of them with no space in the name, only those beside
// a space or at an end otherwise.
std::string dashes_to_spaces(std::string_view s) {
    std::string out(s);
    std::replace(out.begin(), out.end(), '_', ' ');
    const bool spaced = out.find(' ') != std::string::npos;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] != '-') {
            continue;
        }
        const bool beside = i == 0 || i + 1 == out.size() || out[i - 1] == ' ' || out[i + 1] == ' ';
        if (!spaced || beside) {
            out[i] = ' ';
        }
    }
    return out;
}

bool small_word(std::string_view w) {
    static constexpr std::array<std::string_view, 27> kSmall = {
        "a",  "an",  "and", "as",  "at",  "by",  "de",  "del", "der", "des", "di", "du", "en", "et",
        "for", "from", "in", "la", "le", "les", "of", "on", "or", "the", "to", "van", "von"};
    return std::find(kSmall.begin(), kSmall.end(), w) != kSmall.end();
}

bool roman_numeral(std::string_view w) {
    return !w.empty() && w.size() <= 4 && std::all_of(w.begin(), w.end(), [](char c) {
        return c == 'i' || c == 'v' || c == 'x';
    });
}

// Capitals for a name written all small or all capital (ASCII letters only change).
std::string tidy_capitals(std::string s) {
    bool upper = false;
    bool lower = false;
    for (const char c : s) {
        upper = upper || ascii_upper(c);
        lower = lower || ascii_lower(c);
    }
    if (upper && lower) {
        return s;
    }
    std::size_t i = 0;
    bool first = true;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') {
            ++i;
        }
        std::size_t end = i;
        while (end < s.size() && s[end] != ' ') {
            ++end;
        }
        if (end == i) {
            break;
        }
        std::string word = s.substr(i, end - i);
        for (char& c : word) {
            c = to_lower(c);
        }
        if (roman_numeral(word)) {
            for (char& c : word) {
                c = to_upper(c);
            }
        } else if (first || !small_word(word)) {
            word[0] = to_upper(word[0]);
        }
        s.replace(i, end - i, word);
        first = false;
        i = end;
    }
    return s;
}

} // namespace

std::string title_from_file_name(std::string_view file_name) {
    std::string text = utf8_from_midi_text(file_name);
    std::string_view name = text;
    std::size_t slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) {
        name.remove_prefix(slash + 1);
    }
    std::size_t dot = 0;
    if (has_known_extension(name, dot)) {
        name = name.substr(0, dot);
    }
    name = drop_copy_mark(name);
    name = drop_track_number(name);
    return tidy_capitals(clean_text(dashes_to_spaces(name)));
}

std::string midi_sequence_name(std::span<const std::uint8_t> bytes) {
    Reader r(bytes);
    // Header chunk.
    if (r.rest().size() < 4 ||
        std::string_view(reinterpret_cast<const char*>(r.rest().data()), 4) != "MThd") {
        return {};
    }
    r.skip(4);
    std::uint32_t header_length = 0;
    if (!r.be32(header_length) || !r.skip(header_length)) {
        return {};
    }
    // First track chunk (unknown chunks are skipped).
    for (;;) {
        if (r.rest().size() < 8) {
            return {};
        }
        std::string_view id(reinterpret_cast<const char*>(r.rest().data()), 4);
        r.skip(4);
        std::uint32_t length = 0;
        if (!r.be32(length)) {
            return {};
        }
        std::span<const std::uint8_t> body = r.take(std::min<std::size_t>(length, r.rest().size()));
        if (id != "MTrk") {
            if (body.size() < length) {
                return {};
            }
            continue;
        }
        Reader t(body);
        std::uint8_t running = 0;
        while (!t.at_end()) {
            std::uint32_t delta = 0;
            std::uint8_t status = 0;
            if (!t.vlq(delta) || !t.u8(status)) {
                return {};
            }
            if (status < 0x80) { // running status: the byte just read is data
                if (running == 0) {
                    return {};
                }
                bool one_data_byte = (running & 0xF0) == 0xC0 || (running & 0xF0) == 0xD0;
                if (!one_data_byte && !t.skip(1)) {
                    return {};
                }
            } else if (status == 0xFF) {
                running = 0;
                std::uint8_t type = 0;
                std::uint32_t meta_length = 0;
                if (!t.u8(type) || !t.vlq(meta_length)) {
                    return {};
                }
                std::span<const std::uint8_t> payload = t.take(meta_length);
                if (payload.size() < meta_length) {
                    return {};
                }
                if (type == 0x03) {
                    return std::string(reinterpret_cast<const char*>(payload.data()),
                                       payload.size());
                }
                if (type == 0x2F) {
                    return {};
                }
            } else if (status == 0xF0 || status == 0xF7) {
                running = 0;
                std::uint32_t sysex_length = 0;
                if (!t.vlq(sysex_length) || !t.skip(sysex_length)) {
                    return {};
                }
            } else if (status < 0xF0) {
                running = status;
                bool one_data_byte = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0;
                if (!t.skip(one_data_byte ? 1 : 2)) {
                    return {};
                }
            } else {
                return {};
            }
        }
        return {};
    }
}

namespace {

// What a sequencer or an engraver writes where a title goes when it has none.
bool placeholder_title(std::string_view title) {
    if (generic_title(title)) {
        return true;
    }
    const std::string key = fold_for_search(title);
    static constexpr std::array<std::string_view, 24> kNames = {
        "acoustic grand piano", "bright acoustic piano", "electric grand piano", "grand piano",
        "electric piano",       "keyboard",              "right hand",           "left hand",
        "rh",                   "lh",                    "melody",               "bass",
        "treble",               "staff",                 "voice",                "instrument",
        "tempo track",          "conductor",             "accompaniment",        "harmony",
        "soprano",              "alto",                  "tenor",                "title"};
    if (std::find(kNames.begin(), kNames.end(), key) != kNames.end()) {
        return true;
    }
    for (std::string_view start : {"copyright", "created by", "generated by", "made with",
                                   "sequenced by", "arranged by", "transcribed by", "www.",
                                   "http"}) {
        if (key.starts_with(start)) {
            return true;
        }
    }
    // A numbered part: "Staff 2", "Piano 1", "Voice 3".
    for (std::string_view start : {"staff ", "part ", "voice ", "piano ", "instrument "}) {
        if (key.starts_with(start)) {
            const std::string_view rest = std::string_view(key).substr(start.size());
            if (!rest.empty() && std::all_of(rest.begin(), rest.end(), ascii_digit)) {
                return true;
            }
        }
    }
    if (key.find("\xC2\xA9") != std::string::npos) { // ©
        return true;
    }
    std::size_t dot = 0;
    return has_known_extension(key, dot);
}

} // namespace

std::string choose_title(std::string_view file_name, std::string_view sequence_name) {
    std::string from_file = title_from_file_name(file_name);
    std::string from_sequence = clean_text(utf8_from_midi_text(sequence_name));
    bool file_ok = usable_title(from_file);
    bool sequence_ok = usable_title(from_sequence);
    if (sequence_ok && !placeholder_title(from_sequence)) {
        return from_sequence;
    }
    if (file_ok && !generic_title(from_file)) {
        return from_file;
    }
    if (file_ok) {
        return from_file;
    }
    if (sequence_ok) {
        return from_sequence;
    }
    return "Untitled";
}

} // namespace etude
