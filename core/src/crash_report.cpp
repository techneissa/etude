// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/crash_report.hpp"

#include "etude/update_feed.hpp"

#include <algorithm>
#include <cctype>

namespace etude {

namespace {

void put_digits(char* out, std::int64_t value, int digits) {
    for (int i = digits - 1; i >= 0; --i) {
        out[i] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
}

bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '-' ||
           c == '~';
}

// A path begins at a slash that is not inside a word or an address ("and/or", "1/2", "https://").
bool starts_path(std::string_view s, std::size_t i) {
    if (s[i] != '/' || i + 1 >= s.size()) {
        return false;
    }
    const char next = s[i + 1];
    if (std::isspace(static_cast<unsigned char>(next)) != 0 || next == '/') {
        return false;
    }
    if (i == 0) {
        return true;
    }
    const char prev = s[i - 1];
    return !is_word_char(prev) && prev != ':' && prev != '/';
}

bool ends_path(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0 || c == '"' || c == '\'' || c == '<' ||
           c == '>' || c == '|' || c == '`';
}

bool has_prefix_at(std::string_view s, std::size_t i, const std::string& dir) {
    if (dir.empty() || s.compare(i, dir.size(), dir) != 0) {
        return false;
    }
    const std::size_t end = i + dir.size();
    return end == s.size() || s[end] == '/' || dir.back() == '/';
}

// The app's folder at `i`, if one starts there: the longest, so a folder inside another is kept.
const std::string* app_dir_at(std::string_view s, std::size_t i, const ScrubRules& rules) {
    const std::string* best = nullptr;
    for (const std::string& dir : rules.app_dirs) {
        if (has_prefix_at(s, i, dir) && (best == nullptr || dir.size() > best->size())) {
            best = &dir;
        }
    }
    return best;
}

// A folder with the home folder at its front written `~`.
void append_with_tilde(std::string& out, std::string_view dir, const ScrubRules& rules) {
    const std::string& home = rules.home;
    if (!home.empty() && home != "/" && dir.compare(0, home.size(), home) == 0 &&
        (dir.size() == home.size() || dir[home.size()] == '/')) {
        out += '~';
        out += dir.substr(home.size());
    } else {
        out += dir;
    }
}

// Where an unquoted path ends: at its first blank, unless the words after the blank go on like a
// path (they hold a slash or a dot: "Doe/x.mid", "Song.mid"), which means the path has a space in it
// and its end cannot be known: then everything to the end of the line goes. `from` is where the
// scan starts (past a literal folder that may itself hold blanks).
std::size_t path_end(std::string_view line, std::size_t from) {
    std::size_t end = from;
    while (end < line.size() && !ends_path(line[end])) {
        ++end;
    }
    std::size_t look = end;
    while (look < line.size() && line[look] == ' ') {
        std::size_t word_end = look;
        while (word_end < line.size() && line[word_end] == ' ') {
            ++word_end;
        }
        const std::size_t word_start = word_end;
        while (word_end < line.size() && !ends_path(line[word_end])) {
            ++word_end;
        }
        std::string_view word = line.substr(word_start, word_end - word_start);
        while (!word.empty() && std::string_view(".,:;)]}!?").find(word.back()) != std::string_view::npos) {
            word.remove_suffix(1); // the end of a sentence is no part of a path
        }
        if (word.find_first_of("/.") != std::string_view::npos) {
            return line.size(); // a path with a space in it: how far it goes is not known
        }
        break;
    }
    // The last marks of a sentence are not part of the path.
    while (end > from && std::string_view(".,:;)]}!?").find(line[end - 1]) != std::string_view::npos) {
        --end;
    }
    return end;
}

// The home folder at `i`, written out whole (it may hold blanks), when no app folder starts there.
std::size_t home_len_at(std::string_view s, std::size_t i, const ScrubRules& rules) {
    return !rules.home.empty() && rules.home != "/" && has_prefix_at(s, i, rules.home) ? rules.home.size() : 0;
}

std::string scrub_paths(std::string_view line, const ScrubRules& rules) {
    std::string out;
    out.reserve(line.size());
    std::size_t i = 0;
    while (i < line.size()) {
        if (!starts_path(line, i)) {
            out += line[i++];
            continue;
        }
        if (const std::string* dir = app_dir_at(line, i, rules)) {
            // Inside the app's folders: the rest of the word stays with it.
            std::size_t end = i + dir->size();
            while (end < line.size() && !ends_path(line[end])) {
                ++end;
            }
            append_with_tilde(out, line.substr(i, end - i), rules);
            i = end;
            continue;
        }
        const char quote = i > 0 && (line[i - 1] == '"' || line[i - 1] == '\'') ? line[i - 1] : '\0';
        std::size_t end;
        if (quote != '\0') {
            // After a quote the path ends at the closing one, blanks or not.
            end = line.find(quote, i);
            end = end == std::string_view::npos ? line.size() : end;
        } else {
            end = path_end(line, i + home_len_at(line, i, rules));
        }
        out += "<path>";
        i = std::max(end, i + 1);
    }
    return out;
}

std::string scrub_names(std::string text, const std::vector<std::string>& names) {
    for (const std::string& name : names) {
        if (name.size() < 2) {
            continue;
        }
        std::size_t at = 0;
        while ((at = text.find(name, at)) != std::string::npos) {
            const bool left = at == 0 || !std::isalnum(static_cast<unsigned char>(text[at - 1]));
            const std::size_t end = at + name.size();
            const bool right = end >= text.size() || !std::isalnum(static_cast<unsigned char>(text[end]));
            if (left && right) {
                text.replace(at, name.size(), "<profile>");
                at += 9;
            } else {
                at = end;
            }
        }
    }
    return text;
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool has_word(std::string_view line, std::string_view word) {
    for (std::size_t at = line.find(word); at != std::string_view::npos; at = line.find(word, at + 1)) {
        const bool left = at == 0 || (std::isalnum(static_cast<unsigned char>(line[at - 1])) == 0 && line[at - 1] != '_');
        const std::size_t end = at + word.size();
        const bool right = end >= line.size() || (std::isalnum(static_cast<unsigned char>(line[end])) == 0 && line[end] != '_');
        if (left && right) {
            return true;
        }
    }
    return false;
}

// A frame of the handler itself, of the trampoline that calls it or of what raised the signal or
// ended in terminate: they say nothing about where the program went wrong.
bool is_handler_frame(std::string_view line) {
    for (const std::string_view mark :
         {"etude_crash_", "write_crash_report", "_sigtramp", "__restore_rt", "sigreturn", "backtrace",
          "on_terminate", "std::terminate", "_ZSt9terminatev", "__terminate", "__cxa_throw",
          "__cxa_rethrow", "__cxxabiv1", "pthread_kill", "gsignal"}) {
        if (line.find(mark) != std::string_view::npos) {
            return true;
        }
    }
    return has_word(line, "raise") || has_word(line, "abort");
}

// How many of the trace's first frames are the handler's: up to the last of them within the first
// twelve; and the frame after `etude_crash_signal` is the system's trampoline, which may have no
// name to tell it by.
std::size_t handler_frames(const std::vector<std::string>& frames) {
    std::size_t skip = 0;
    for (std::size_t i = 0; i < frames.size() && i < 12; ++i) {
        if (is_handler_frame(frames[i])) {
            skip = i + (frames[i].find("etude_crash_signal") != std::string::npos ? 2 : 1);
        }
    }
    return std::min(skip, frames.size());
}

bool is_hex_address(std::string_view word) {
    if (word.size() < 10 || !starts_with(word, "0x")) {
        return false;
    }
    return std::all_of(word.begin() + 2, word.end(),
                       [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

// One frame in a few words: no index, no module column, no long addresses, no namespace the app
// is all in, one space between words.
std::string frame_label(std::string_view line, std::size_t max_chars) {
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) {
        ++i;
    }
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])) != 0) {
        ++i; // the frame's number
    }
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) == 0) {
            ++i;
        }
        std::string_view word = line.substr(start, i - start);
        if (word.size() > 2 && word.front() == '[' && word.back() == ']') {
            word = word.substr(1, word.size() - 2);
        }
        if (!word.empty()) {
            words.emplace_back(word);
        }
    }
    std::string out;
    for (std::size_t w = 0; w < words.size(); ++w) {
        std::string& word = words[w];
        const std::size_t open = word.find('(');
        if (open != std::string::npos) {
            const std::size_t slash = word.rfind('/', open);
            if (slash != std::string::npos) {
                word.erase(0, slash + 1); // `/opt/x/bin/etude(f+0x1)` is `etude(f+0x1)`
            }
        }
        const bool module_column = w == 0 && w + 1 < words.size() && is_hex_address(words[1]) &&
                                   open == std::string::npos;
        if (is_hex_address(word) || module_column) {
            continue; // the address, and the module's name before it
        }
        if (!out.empty()) {
            out += ' ';
        }
        out += word;
    }
    for (const std::string_view boilerplate : {"etude::app::", "etude::"}) {
        for (std::size_t at = 0; (at = out.find(boilerplate, at)) != std::string::npos;) {
            out.erase(at, boilerplate.size());
        }
    }
    if (out.size() > max_chars) {
        out.resize(max_chars);
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0U) == 0x80U) {
            out.pop_back(); // never half a character
        }
        out += "...";
    }
    return out;
}

// `text` cut to at most `max_chars` bytes, never inside a UTF-8 character, with "..." in the last
// three when it was cut.
std::string clip(std::string_view text, std::size_t max_chars) {
    if (text.size() <= max_chars) {
        return std::string(text);
    }
    std::size_t n = max_chars > 3 ? max_chars - 3 : 0;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0U) == 0x80U) {
        --n;
    }
    return std::string(text.substr(0, n)) + (max_chars > 3 ? "..." : "");
}

// What each part of the address may take, so the longest inputs still fit: a QR code's address is
// 250 characters in all, and every byte outside the plain letters costs three of them.
struct UrlParts {
    std::size_t version;
    std::size_t os;
    std::size_t signal;
    std::size_t screen;
};

UrlParts parts_for(bool roomy) { return roomy ? UrlParts{48, 100, 200, 100} : UrlParts{12, 32, 40, 0}; }

// The roomy body (a browser) is a page of facts and the top frames; the short one (a QR code) keeps
// to a line of what and where and as many frames as the bound leaves room for.
std::string issue_body(const CrashSummary& s, std::size_t frames, std::size_t width, bool roomy) {
    const UrlParts p = parts_for(roomy);
    std::string body;
    if (roomy) {
        body += "Etude closed unexpectedly.\n\nWhat I was doing:\n\n";
        body += "Version: " + clip(s.version, p.version) + "\nOS: " + clip(s.os, p.os) +
                "\nSignal: " + clip(s.signal, p.signal) + "\n";
        if (!s.screen.empty()) {
            body += "Screen: " + clip(s.screen, p.screen) + "\n";
        }
    } else {
        body += "Etude " + clip(s.version, p.version) + ", " + clip(s.os, p.os) + "\n" +
                clip(s.signal, p.signal) + "\n";
    }
    if (roomy && frames > 0) {
        body += "Top frames:\n";
    }
    for (std::size_t i = 0; i < frames; ++i) {
        body += "- " + frame_label(s.frames[i], width) + "\n";
    }
    return body;
}

std::string issue_title(const CrashSummary& s, bool roomy) {
    const UrlParts p = parts_for(roomy);
    std::string title = "Crash: " + (s.signal.empty() ? std::string("closed unexpectedly")
                                                      : clip(s.signal, roomy ? p.signal : 24));
    if (roomy) {
        title += " in " + clip(s.version, p.version);
    }
    return title;
}

std::string issue_url(const std::string& title, const std::string& body) {
    std::string url(kRepositoryUrl);
    url += "/issues/new?title=";
    url += percent_encode(title);
    url += "&body=";
    url += percent_encode(body);
    return url;
}

} // namespace

void crash_report_name(std::int64_t epoch_seconds, char* out) {
    std::int64_t days = epoch_seconds / 86400;
    std::int64_t rest = epoch_seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    // The civil date of a day count (days since 1970-01-01), by the era's arithmetic.
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    put_digits(out, year, 4);
    put_digits(out + 4, month, 2);
    put_digits(out + 6, day, 2);
    out[8] = '-';
    put_digits(out + 9, rest / 3600, 2);
    put_digits(out + 11, rest % 3600 / 60, 2);
    put_digits(out + 13, rest % 60, 2);
    out[15] = '.';
    out[16] = 't';
    out[17] = 'x';
    out[18] = 't';
    out[19] = '\0';
}

bool is_crash_report_name(std::string_view name) {
    // Exactly YYYYMMDD-HHMMSS.txt: nothing else, so no slash or dot can steer a path.
    if (name.size() != 19 || name.substr(15) != ".txt") {
        return false;
    }
    for (std::size_t i = 0; i < 15; ++i) {
        const bool dash = i == 8;
        if (dash ? name[i] != '-' : std::isdigit(static_cast<unsigned char>(name[i])) == 0) {
            return false;
        }
    }
    return true;
}

std::string scrub_line(std::string_view line, const ScrubRules& rules) {
    return scrub_names(scrub_paths(line, rules), rules.names);
}

std::string scrub_text(std::string_view text, const ScrubRules& rules) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        const bool last = end == std::string_view::npos;
        if (last) {
            end = text.size();
        }
        out += scrub_line(text.substr(at, end - at), rules);
        if (!last) {
            out += '\n';
        }
        at = end + 1;
    }
    return out;
}

CrashSummary summarize_crash(std::string_view report) {
    CrashSummary s;
    bool in_trace = false;
    std::size_t at = 0;
    while (at < report.size()) {
        std::size_t end = report.find('\n', at);
        if (end == std::string_view::npos) {
            end = report.size();
        }
        const std::string_view line = report.substr(at, end - at);
        at = end + 1;
        if (in_trace) {
            if (trimmed(line).empty()) {
                in_trace = false;
            } else {
                s.frames.push_back(trimmed(line));
            }
        } else if (starts_with(line, "Version: ")) {
            s.version = trimmed(line.substr(9));
        } else if (starts_with(line, "OS: ")) {
            s.os = trimmed(line.substr(4));
        } else if (starts_with(line, "Signal: ")) {
            s.signal = trimmed(line.substr(8));
        } else if (starts_with(line, "Screen: ")) {
            s.screen = trimmed(line.substr(8));
        } else if (line == "Stack trace:") {
            in_trace = true;
        }
    }
    s.frames.erase(s.frames.begin(), s.frames.begin() + static_cast<std::ptrdiff_t>(handler_frames(s.frames)));
    return s;
}

std::string percent_encode(std::string_view text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 2);
    for (const char c : text) {
        const auto b = static_cast<unsigned char>(c);
        if (std::isalnum(b) != 0 || std::string_view("-._~:()/,;@!*").find(c) != std::string_view::npos) {
            out += c;
        } else {
            out += '%';
            out += hex[b >> 4];
            out += hex[b & 15U];
        }
    }
    return out;
}

std::string crash_issue_url(const CrashSummary& summary, std::size_t max_chars) {
    const bool roomy = max_chars >= 1000;
    const std::string title = issue_title(summary, roomy);
    const std::size_t most = std::min<std::size_t>(summary.frames.size(), roomy ? 8 : 4);
    const std::vector<std::size_t> widths = roomy ? std::vector<std::size_t>{110}
                                                  : std::vector<std::size_t>{40, 24};
    // The most frames, at the widest, that fit.
    for (std::size_t frames = most; frames > 0; --frames) {
        for (const std::size_t width : widths) {
            const std::string url = issue_url(title, issue_body(summary, frames, width, roomy));
            if (url.size() <= max_chars) {
                return url;
            }
        }
    }
    const std::string facts = issue_body(summary, 0, 0, roomy);
    std::string url = issue_url(title, facts);
    if (url.size() <= max_chars) {
        return url;
    }
    // Not even the facts fit: cut the body, then the title, until the whole does.
    std::string body = facts;
    while (!body.empty() && url.size() > max_chars) {
        body.resize(body.size() - std::min<std::size_t>(body.size(), 8));
        url = issue_url(title, body);
    }
    std::string short_title = title;
    while (!short_title.empty() && url.size() > max_chars) {
        short_title.resize(short_title.size() - 1);
        url = issue_url(short_title, body);
    }
    return url.size() <= max_chars ? url : url.substr(0, max_chars);
}

std::vector<std::string> reports_to_prune(std::vector<std::string> names, std::size_t keep) {
    names.erase(std::remove_if(names.begin(), names.end(),
                               [](const std::string& n) { return !is_crash_report_name(n); }),
                names.end());
    std::sort(names.begin(), names.end());
    if (names.size() <= keep) {
        return {};
    }
    names.resize(names.size() - keep);
    return names;
}

std::string newest_crash_report(const std::vector<std::string>& names) {
    std::string newest;
    for (const std::string& name : names) {
        if (is_crash_report_name(name) && name > newest) {
            newest = name;
        }
    }
    return newest;
}

CrashNoticePlan plan_crash_notice(const std::vector<std::string>& names, const std::string& seen) {
    CrashNoticePlan plan;
    plan.seen = seen;
    const std::string newest = newest_crash_report(names);
    if (!newest.empty() && newest > seen) {
        plan.show = newest;
        plan.seen = newest;
    }
    return plan;
}

} // namespace etude
