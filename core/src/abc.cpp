// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/abc.hpp"

#include "abc_parser.hpp"
#include "score_raw.hpp"
#include "etude/content_hash.hpp"
#include "etude/library.hpp"
#include "etude/song_builder.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string_view>
#include <tuple>
#include <utility>

namespace etude {
namespace {

using namespace abc_detail;

// ---- Tunes in a file

struct FoundTune {
    std::size_t begin = 0;
    std::size_t end = 0;
    long long number = 0;
    bool number_replaced = false; // the X: value was above kMaxAbcTuneNumber; `number` is its place
    std::string title;
};

// The tunes of the first `size` bytes, and where the file header ends.
std::vector<FoundTune> find_tunes(const char* text, std::size_t size, std::size_t& header_end,
                                  bool& dropped_some) {
    std::vector<FoundTune> tunes;
    dropped_some = false;
    header_end = size;
    std::size_t at = bom_length(text, size);
    while (at < size) {
        const LineSpan l = line_at(text, size, at);
        if (x_line(text, l)) {
            if (tunes.empty()) {
                header_end = l.begin;
            }
            if (!tunes.empty()) {
                tunes.back().end = l.begin; // the last kept tune ends where the next one starts
            }
            if (tunes.size() >= kMaxAbcTunes) {
                dropped_some = true;
                break;
            }
            FoundTune t;
            t.begin = l.begin;
            t.end = size;
            std::size_t i = 0;
            const std::string_view v = field_value({text + l.begin, l.end - l.begin});
            t.number = read_number(v, i, kMaxAbcTuneNumber + 1);
            if (t.number > kMaxAbcTuneNumber) {
                t.number = static_cast<long long>(tunes.size()) + 1; // its place in the file
                t.number_replaced = true;
            }
            tunes.push_back(std::move(t));
        }
        at = l.next;
    }
    // The first T: line of each tune, before its K: line.
    for (FoundTune& t : tunes) {
        std::size_t p = t.begin;
        while (p < t.end) {
            const LineSpan l = line_at(text, t.end, p);
            if (blank_line(text, l)) {
                break;
            }
            const std::string_view s(text + l.begin, l.end - l.begin);
            if (is_field_line(s)) {
                if (s[0] == 'T') {
                    t.title = clean_text(field_value(s));
                    break;
                }
                if (s[0] == 'K') {
                    break;
                }
            }
            p = l.next;
        }
    }
    return tunes;
}

} // namespace

std::vector<AbcTune> list_abc_tunes(const std::uint8_t* data, std::size_t size) {
    std::vector<AbcTune> out;
    if (data == nullptr || size == 0) {
        return out;
    }
    size = std::min(size, kMaxAbcBytes);
    const char* text = reinterpret_cast<const char*>(data);
    std::size_t header_end = 0;
    bool dropped_some = false;
    const std::vector<FoundTune> found = find_tunes(text, size, header_end, dropped_some);
    const std::uint64_t header_hash = content_hash(text, header_end);
    std::map<long long, int> seen;
    std::size_t line = 1;
    std::size_t at = 0;
    for (const FoundTune& f : found) {
        while (at < f.begin) {
            at = line_at(text, size, at).next;
            ++line;
        }
        AbcTune t;
        t.number = f.number;
        t.number_replaced = f.number_replaced;
        const int nth = ++seen[f.number];
        t.address = "X=" + std::to_string(f.number) + (nth > 1 ? "." + std::to_string(nth) : "");
        t.title = f.title;
        t.begin = f.begin;
        t.end = f.end;
        t.header_end = header_end;
        t.line = line;
        t.hash = content_hash_extend(header_hash, text + f.begin, f.end - f.begin);
        out.push_back(std::move(t));
    }
    if (dropped_some && !out.empty()) {
        out.back().more_tunes_follow = true;
    }
    return out;
}

ScoreLoadResult load_abc(const std::uint8_t* data, std::size_t size, const AbcOptions& options) {
    if (data == nullptr || size == 0) {
        return score_detail::make_error(ScoreFileError::NotScore, "the file is empty");
    }
    size = std::min(size, kMaxAbcBytes);
    const char* text = reinterpret_cast<const char*>(data);
    AbcTune chosen;
    if (options.listed != nullptr) {
        chosen = *options.listed;
        if (chosen.begin > chosen.end || chosen.end > size || chosen.header_end > chosen.begin) {
            return score_detail::make_error(ScoreFileError::NotScore,
                                            "the tune does not fit the file");
        }
    } else {
        const std::vector<AbcTune> listed = list_abc_tunes(data, size);
        if (listed.empty()) {
            return score_detail::make_error(ScoreFileError::NotScore,
                                            "the file holds no ABC tune (no X: line)");
        }
        std::size_t at = 0;
        if (!options.tune.empty()) {
            at = listed.size();
            for (std::size_t i = 0; i < listed.size(); ++i) {
                if (listed[i].address == options.tune) {
                    at = i;
                    break;
                }
            }
            if (at == listed.size()) {
                return score_detail::make_error(ScoreFileError::NotScore,
                                                "the file has no tune " + options.tune);
            }
        }
        chosen = listed[at];
    }
    TuneParser parser(options);
    return parser.run(text, chosen);
}

AbcLocation split_abc_location(std::string_view text) {
    const std::size_t hash = text.rfind('#');
    if (hash == std::string_view::npos) {
        return {text, {}};
    }
    const std::string_view tune = text.substr(hash + 1);
    // "X=" digits, then optionally "." digits.
    std::size_t i = 0;
    if (tune.size() < 3 || tune[0] != 'X' || tune[1] != '=') {
        return {text, {}};
    }
    i = 2;
    std::size_t digits = 0;
    while (i < tune.size() && is_digit(tune[i])) {
        ++i;
        ++digits;
    }
    if (digits == 0) {
        return {text, {}};
    }
    if (i < tune.size() && tune[i] == '.') {
        ++i;
        digits = 0;
        while (i < tune.size() && is_digit(tune[i])) {
            ++i;
            ++digits;
        }
        if (digits == 0) {
            return {text, {}};
        }
    }
    if (i != tune.size()) {
        return {text, {}};
    }
    return {text.substr(0, hash), tune};
}

std::string join_abc_location(std::string_view file, std::string_view tune) {
    std::string out(file);
    if (!tune.empty()) {
        out += '#';
        out += tune;
    }
    return out;
}

bool looks_like_abc(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 4) {
        return false;
    }
    const std::size_t limit = std::min<std::size_t>(size, 8192);
    for (std::size_t i = 0; i < limit; ++i) {
        if (data[i] == 0) {
            return false;
        }
    }
    const char* text = reinterpret_cast<const char*>(data);
    std::size_t at = bom_length(text, limit);
    bool first = true;
    bool seen_x = false;
    while (at < limit) {
        const LineSpan l = line_at(text, limit, at);
        const std::string_view s(text + l.begin, l.end - l.begin);
        if (first && s.substr(0, 4) == "%abc") {
            return true;
        }
        first = false;
        if (x_line(text, l)) {
            seen_x = true;
        } else if (seen_x && s.size() >= 2 && s[0] == 'K' && s[1] == ':') {
            return true;
        }
        at = l.next;
    }
    return false;
}

} // namespace etude

