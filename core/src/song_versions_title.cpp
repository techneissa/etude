// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_versions.hpp"

#include "song_versions_internal.hpp"

#include "etude/library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace etude {

// ---- What the file says

namespace {

struct LabelWord {
    std::string_view word;
    VersionLabel label;
};
constexpr LabelWord kLabelWords[] = {
    {"beginner", VersionLabel::Beginner},   {"beginners", VersionLabel::Beginner},
    {"novice", VersionLabel::Beginner},     {"easy", VersionLabel::Easy},
    {"easier", VersionLabel::Easy},         {"easiest", VersionLabel::Easy},
    {"simplified", VersionLabel::Easy},     {"simpler", VersionLabel::Easy},
    {"intermediate", VersionLabel::Medium}, {"medium", VersionLabel::Medium},
    {"moderate", VersionLabel::Medium},     {"advanced", VersionLabel::Hard},
    {"hard", VersionLabel::Hard},           {"harder", VersionLabel::Hard},
    {"difficult", VersionLabel::Hard},      {"expert", VersionLabel::Expert},
    {"virtuoso", VersionLabel::Expert},     {"professional", VersionLabel::Expert},
    {"original", VersionLabel::Original},   {"urtext", VersionLabel::Original},
};
// Words that sit beside a level word and say nothing of the piece: "easy version", "for beginners".
constexpr std::string_view kFillers[] = {"version",  "ver",     "arrangement", "arr",
                                         "arranged", "edition", "level",       "for"};
// A title that names no piece.
constexpr std::string_view kGenericTitles[] = {"untitled", "song",  "track",
                                               "midi",     "music", "unknown"};

bool is_alnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c >= 0x80;
}
bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

// A text as words: folded, split at anything that is not a letter or digit and where letters meet
// digits ("op27" is "op" and "27"), "#" and "b" signs as words.
std::vector<std::string> words_of(std::string_view text) {
    std::string folded = fold_for_search(text);
    std::vector<std::string> out;
    std::string cur;
    const auto flush = [&] {
        if (!cur.empty()) {
            out.push_back(std::move(cur));
            cur.clear();
        }
    };
    for (std::size_t i = 0; i < folded.size(); ++i) {
        const auto c = static_cast<unsigned char>(folded[i]);
        if (c == '#') {
            flush();
            out.emplace_back("sharp");
        } else if (is_alnum(c)) {
            if (!cur.empty() && is_digit(static_cast<unsigned char>(cur.back())) != is_digit(c) &&
                c < 0x80 && static_cast<unsigned char>(cur.back()) < 0x80) {
                flush();
            }
            cur += static_cast<char>(c);
        } else {
            flush();
        }
    }
    flush();
    return out;
}

VersionLabel label_of_word(const std::string& w) {
    for (const LabelWord& l : kLabelWords) {
        if (l.word == w) {
            return l.label;
        }
    }
    return VersionLabel::None;
}

VersionLabel label_of_level(const std::string& digits) {
    if (digits.size() != 1) {
        return VersionLabel::None;
    }
    switch (digits[0]) {
    case '1':
        return VersionLabel::Beginner;
    case '2':
        return VersionLabel::Easy;
    case '3':
        return VersionLabel::Medium;
    case '4':
        return VersionLabel::Hard;
    case '5':
        return VersionLabel::Expert;
    default:
        return VersionLabel::None;
    }
}

} // namespace

TitleFacts facts_of_title(std::string_view title) {
    TitleFacts out;
    const std::vector<std::string> w = words_of(title);
    std::vector<char> gone(w.size(), 0);
    for (std::size_t i = 0; i < w.size(); ++i) {
        if (w[i] == "level" && i + 1 < w.size() && all_digits(w[i + 1])) {
            const VersionLabel l = label_of_level(w[i + 1]);
            if (l != VersionLabel::None) {
                if (out.label == VersionLabel::None) {
                    out.label = l;
                }
                gone[i] = gone[i + 1] = 1;
                continue;
            }
        }
        const VersionLabel l = label_of_word(w[i]);
        if (l != VersionLabel::None) {
            if (out.label == VersionLabel::None) {
                out.label = l;
            }
            gone[i] = 1;
        }
    }
    // "easy version", "for beginners": the words beside a level word go with it.
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < w.size(); ++i) {
            if (gone[i] == 0 && in_list(w[i], kFillers) &&
                ((i > 0 && gone[i - 1] != 0) || (i + 1 < w.size() && gone[i + 1] != 0))) {
                gone[i] = 1;
            }
        }
    }
    // The first catalogue number: "op 27 no 2", "bwv 846", "k 331".
    for (std::size_t i = 0; i + 1 < w.size() && out.catalogue.empty(); ++i) {
        if (gone[i] != 0 || gone[i + 1] != 0 || !all_digits(w[i + 1])) {
            continue;
        }
        std::string prefix;
        if (w[i] == "op" || w[i] == "opus") {
            prefix = "op";
        } else if (w[i] == "k" || w[i] == "kv") {
            prefix = "kv";
        } else if (w[i] == "bwv" || w[i] == "hob" || w[i] == "hwv" || w[i] == "woo" ||
                   w[i] == "rv" || w[i] == "wq") {
            prefix = w[i];
        } else {
            continue;
        }
        out.catalogue = prefix + w[i + 1];
        gone[i] = gone[i + 1] = 1;
        if (prefix == "op" && i + 3 < w.size() && gone[i + 2] == 0 && w[i + 2] == "no" &&
            all_digits(w[i + 3])) {
            out.catalogue += "/" + w[i + 3];
            gone[i + 2] = gone[i + 3] = 1;
        }
    }
    for (std::size_t i = 0; i < w.size(); ++i) {
        if (gone[i] == 0 && !in_list(w[i], kStopWords)) {
            out.core.push_back(w[i]);
        }
    }
    if (out.core.size() == 1 && in_list(out.core[0], kGenericTitles)) {
        out.core.clear();
    }
    return out;
}

VersionLabel version_label(std::string_view title, std::string_view file_name) {
    const VersionLabel l = facts_of_title(title).label;
    if (l != VersionLabel::None || file_name.empty()) {
        return l;
    }
    // The file name without its folders and extension.
    const std::size_t slash = file_name.find_last_of('/');
    std::string_view base =
        slash == std::string_view::npos ? file_name : file_name.substr(slash + 1);
    if (const std::size_t dot = base.find_last_of('.'); dot != std::string_view::npos && dot > 0) {
        base = base.substr(0, dot);
    }
    return facts_of_title(base).label;
}

std::string_view version_label_name(VersionLabel label) {
    switch (label) {
    case VersionLabel::None:
        return {};
    case VersionLabel::Beginner:
        return "Beginner";
    case VersionLabel::Easy:
        return "Easy";
    case VersionLabel::Medium:
        return "Medium";
    case VersionLabel::Hard:
        return "Hard";
    case VersionLabel::Expert:
        return "Expert";
    case VersionLabel::Original:
        return "Original";
    }
    return {};
}

std::string_view level_name(int level) {
    static constexpr std::string_view names[] = {"Easy", "Simple", "Medium", "Hard", "Expert"};
    return names[std::clamp(level, 1, 5) - 1];
}

namespace {

// A letter of either case, a digit, or a byte of a non-ASCII letter.
bool is_word_byte(char c) {
    const auto u = static_cast<unsigned char>(c);
    return is_alnum(u) || (u >= 'A' && u <= 'Z');
}

} // namespace

std::vector<TitleToken> tokens_of(std::string_view text) {
    std::vector<TitleToken> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (!is_word_byte(text[i])) {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        while (i < text.size() && is_word_byte(text[i])) {
            ++i;
        }
        // "C#": a sharp after a lone note letter belongs to the word.
        if (i - begin == 1 && i < text.size() && text[i] == '#' && text[begin] >= 'A' &&
            text[begin] <= 'G') {
            ++i;
        }
        out.push_back({begin, i, fold_for_search(text.substr(begin, i - begin))});
    }
    return out;
}

// Which tokens are the title's level words and the words beside them (as facts_of_title takes
// them out).
std::vector<char> level_tokens(const std::vector<TitleToken>& t) {
    std::vector<char> gone(t.size(), 0);
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i].word == "level" && i + 1 < t.size() && all_digits(t[i + 1].word) &&
            label_of_level(t[i + 1].word) != VersionLabel::None) {
            gone[i] = gone[i + 1] = 1;
        } else if (label_of_word(t[i].word) != VersionLabel::None) {
            gone[i] = 1;
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (gone[i] == 0 && in_list(t[i].word, kFillers) &&
                ((i > 0 && gone[i - 1] != 0) || (i + 1 < t.size() && gone[i + 1] != 0))) {
                gone[i] = 1;
            }
        }
    }
    return gone;
}

namespace {

bool is_separator_byte(char c) { return c == '-' || c == ',' || c == ':' || c == ';' || c == '_'; }

// Takes out what a removed word leaves behind: empty brackets, dashes with nothing beside them,
// spaces in a row.
std::string tidy_title(std::string text) {
    const auto replace_all = [&](std::string_view from, std::string_view to) {
        for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at)) {
            text.replace(at, from.size(), to);
        }
    };
    for (int pass = 0; pass < 3; ++pass) {
        for (const std::string_view brackets : {"()", "[]", "{}"}) {
            for (std::size_t at = text.find(brackets[0]); at != std::string::npos;
                 at = text.find(brackets[0], at)) {
                std::size_t close = at + 1;
                while (close < text.size() && text[close] == ' ') {
                    ++close;
                }
                if (close < text.size() && text[close] == brackets[1]) {
                    text.erase(at, close + 1 - at);
                } else {
                    ++at;
                }
            }
        }
        replace_all("  ", " ");
        replace_all(" - -", " -");
        replace_all("- - ", "- ");
    }
    // Neither end is left with a dash or a space.
    while (!text.empty() && (text.back() == ' ' || is_separator_byte(text.back()))) {
        text.pop_back();
    }
    std::size_t front = 0;
    while (front < text.size() && (text[front] == ' ' || is_separator_byte(text[front]))) {
        ++front;
    }
    return text.substr(front);
}

} // namespace

std::string piece_name(std::string_view title) {
    const std::vector<TitleToken> t = tokens_of(title);
    const std::vector<char> gone = level_tokens(t);
    std::string out;
    std::size_t at = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (gone[i] != 0) {
            out.append(title.substr(at, t[i].begin - at));
            at = t[i].end;
        }
    }
    out.append(title.substr(at));
    out = tidy_title(std::move(out));
    return out.empty() ? tidy_title(std::string(title)) : out;
}

std::string piece_title_key(std::string_view title) { return join(facts_of_title(title).core); }

std::string catalogue_number(std::string_view title) { return facts_of_title(title).catalogue; }

std::string composer_surname_key(std::string_view composer) {
    std::string folded = fold_for_search(composer);
    if (const std::size_t open = folded.find('('); open != std::string::npos) {
        folded.resize(open);
    }
    // "Surname, First names" names the surname first.
    const std::size_t comma = folded.find(',');
    const std::string surname_part = comma != std::string::npos ? folded.substr(0, comma) : folded;
    const std::vector<std::string> w = words_of(surname_part);
    std::string key;
    for (std::size_t i = w.size(); i-- > 0;) {
        const std::string& t = w[i];
        const bool suffix =
            t == "jr" || t == "sr" || t == "i" || t == "ii" || t == "iii" || t == "iv";
        if (!suffix || i == 0) {
            key = t;
            break;
        }
    }
    static constexpr std::string_view kNobody[] = {"traditional", "anonymous", "unknown", "various",
                                                   "trad",        "folk",      "composer"};
    if (in_list(key, kNobody)) {
        return {};
    }
    return key;
}

} // namespace etude
