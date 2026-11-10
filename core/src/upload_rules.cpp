// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/upload_rules.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace etude {

namespace {

// Strict UTF-8: no overlong forms, no surrogates, nothing past U+10FFFF. On success `cp` is the
// code point and `i` is past it; on failure `i` is unchanged.
bool decode_utf8(std::string_view s, std::size_t& i, char32_t& cp) {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char b0 = byte(i);
    if (b0 < 0x80) {
        cp = b0;
        ++i;
        return true;
    }
    std::size_t len = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF; // the allowed range of the second byte
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        len = 2;
        cp = b0 & 0x1Fu;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        len = 3;
        cp = b0 & 0x0Fu;
        lo = b0 == 0xE0 ? 0xA0 : 0x80;
        hi = b0 == 0xED ? 0x9F : 0xBF;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        len = 4;
        cp = b0 & 0x07u;
        lo = b0 == 0xF0 ? 0x90 : 0x80;
        hi = b0 == 0xF4 ? 0x8F : 0xBF;
    } else {
        return false;
    }
    if (i + len > s.size()) {
        return false;
    }
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char b = byte(i + k);
        if (b < (k == 1 ? lo : 0x80) || b > (k == 1 ? hi : 0xBF)) {
            return false;
        }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    i += len;
    return true;
}

enum class Class : std::uint8_t {
    Plain,
    Control,   // controls and invisible format characters
    Separator, // / \ and lookalikes
    Dot,       // a lookalike of "."
};

bool in(char32_t cp, char32_t first, char32_t last) { return cp >= first && cp <= last; }

Class classify(char32_t cp) {
    if (cp < 0x20 || in(cp, 0x7F, 0x9F)) {
        return Class::Control;
    }
    switch (cp) {
    case U'/':
    case U'\\':
    case 0x2044: // FRACTION SLASH
    case 0x2215: // DIVISION SLASH
    case 0x2216: // SET MINUS
    case 0x29F5: // REVERSE SOLIDUS OPERATOR
    case 0x29F8: // BIG SOLIDUS
    case 0x29F9: // BIG REVERSE SOLIDUS
    case 0x2571: // BOX DRAWINGS LIGHT DIAGONAL UPPER RIGHT TO LOWER LEFT
    case 0x2572: // BOX DRAWINGS LIGHT DIAGONAL UPPER LEFT TO LOWER RIGHT
    case 0xFE68: // SMALL REVERSE SOLIDUS
    case 0xFF0F: // FULLWIDTH SOLIDUS
    case 0xFF3C: // FULLWIDTH REVERSE SOLIDUS
        return Class::Separator;
    case 0x2024: // ONE DOT LEADER
    case 0x2025: // TWO DOT LEADER
    case 0xFE52: // SMALL FULL STOP
    case 0xFF0E: // FULLWIDTH FULL STOP
        return Class::Dot;
    default:
        break;
    }
    // Invisible characters that reorder or hide text: a name that reads "a.mid" and is "a.dim".
    if (cp == 0x00AD || cp == 0x061C || cp == 0x180E || in(cp, 0x200B, 0x200F) ||
        in(cp, 0x2028, 0x202E) || in(cp, 0x2060, 0x206F) || cp == 0xFEFF ||
        in(cp, 0xFFF9, 0xFFFB) || cp == 0xFFFE || cp == 0xFFFF || in(cp, 0xE0000, 0xE007F)) {
        return Class::Control;
    }
    return Class::Plain;
}

// Checks one name (a file's or a folder's) and builds the text to use.
NameProblem check_part(std::string_view part, std::string& out) {
    out.clear();
    if (part.empty()) {
        return NameProblem::Empty;
    }
    if (part.size() > kMaxNameBytes) {
        return NameProblem::TooLong;
    }
    std::size_t i = 0;
    while (i < part.size()) {
        const std::size_t start = i;
        char32_t cp = 0;
        if (!decode_utf8(part, i, cp)) {
            return NameProblem::BadUtf8;
        }
        switch (classify(cp)) {
        case Class::Control:
            return NameProblem::Control;
        case Class::Separator:
            return NameProblem::Separator;
        case Class::Dot:
            return NameProblem::Dots;
        case Class::Plain:
            out.append(part.substr(start, i - start));
            break;
        }
    }
    if (out.front() == '.' || out.back() == '.') {
        return NameProblem::Dots; // ".", "..", hidden, and "a." (which Windows drops the dot of)
    }
    return NameProblem::None;
}

constexpr std::array<std::string_view, 11> kSongExtensions{
    ".mid", ".midi", ".kar", ".smf", ".rmi", ".musicxml", ".xml", ".mxl", ".mscz", ".mscx", ".abc"};

bool starts_with_bytes(std::span<const std::uint8_t> head, std::size_t offset,
                       std::string_view text) {
    if (head.size() < offset + text.size()) {
        return false;
    }
    for (std::size_t k = 0; k < text.size(); ++k) {
        if (head[offset + k] != static_cast<std::uint8_t>(text[k])) {
            return false;
        }
    }
    return true;
}

bool has_nul(std::span<const std::uint8_t> head) {
    return std::find(head.begin(), head.end(), std::uint8_t{0}) != head.end();
}

// The start of an XML file, after an optional UTF-8 byte order mark and spaces, with no NUL.
std::string_view xml_start(std::span<const std::uint8_t> head) {
    if (has_nul(head)) {
        return {};
    }
    std::size_t i = 0;
    if (head.size() >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
        i = 3;
    }
    while (i < head.size() &&
           (head[i] == ' ' || head[i] == '\t' || head[i] == '\n' || head[i] == '\r')) {
        ++i;
    }
    return {reinterpret_cast<const char*>(head.data()) + i, head.size() - i};
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// ABC text: no control byte but tab, line feed and carriage return, and a line that starts with
// "X:" (the tune's number) or "K:" (its key), which every tune has.
bool looks_like_abc(std::span<const std::uint8_t> head) {
    if (head.empty() || !std::all_of(head.begin(), head.end(), [](std::uint8_t b) {
            return b >= 0x20 || b == '\t' || b == '\n' || b == '\r';
        })) {
        return false;
    }
    const std::string_view text(reinterpret_cast<const char*>(head.data()), head.size());
    for (std::size_t line = 0; line < text.size();) {
        if (starts_with(text.substr(line), "X:") || starts_with(text.substr(line), "K:")) {
            return true;
        }
        const std::size_t eol = text.find('\n', line);
        if (eol == std::string_view::npos) {
            break;
        }
        line = eol + 1;
    }
    return false;
}

} // namespace

CleanName clean_file_name(std::string_view raw) {
    while (!raw.empty() && raw.front() == ' ') {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && raw.back() == ' ') {
        raw.remove_suffix(1);
    }
    CleanName result;
    result.problem = check_part(raw, result.name);
    if (result.problem != NameProblem::None) {
        result.name.clear();
    }
    return result;
}

std::string lower_extension(std::string_view name) {
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) {
        return {};
    }
    std::string ext(name.substr(dot));
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext;
}

bool is_song_extension(std::string_view extension) {
    return std::find(kSongExtensions.begin(), kSongExtensions.end(), extension) !=
           kSongExtensions.end();
}

std::optional<FileKind> kind_of_name(std::string_view name) {
    const std::string ext = lower_extension(name);
    if (is_song_extension(ext)) {
        return FileKind::Song;
    }
    if (ext == ".pdf") {
        return FileKind::Document;
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".heic" || ext == ".heif") {
        return FileKind::Image;
    }
    return std::nullopt;
}

CleanName check_upload_type(std::string_view raw) {
    CleanName cleaned = clean_file_name(raw);
    if (cleaned.ok() && !kind_of_name(cleaned.name)) {
        cleaned.problem = NameProblem::NotAllowed;
        cleaned.name.clear();
    }
    return cleaned;
}

std::size_t sniff_window(std::string_view extension) {
    // The text formats' first line may follow a comment or two; every binary type is known from
    // its first twelve bytes.
    return extension == ".abc" || extension == ".xml" || extension == ".musicxml" ||
                   extension == ".mscx"
               ? kSniffBytes
               : 12;
}

bool header_matches(std::string_view extension, std::span<const std::uint8_t> head) {
    if (extension == ".mid" || extension == ".midi" || extension == ".kar" || extension == ".smf") {
        return starts_with_bytes(head, 0, "MThd");
    }
    if (extension == ".rmi") {
        return (starts_with_bytes(head, 0, "RIFF") && starts_with_bytes(head, 8, "RMID")) ||
               starts_with_bytes(head, 0, "MThd");
    }
    if (extension == ".pdf") {
        return starts_with_bytes(head, 0, "%PDF-");
    }
    if (extension == ".png") {
        return starts_with_bytes(head, 0, "\x89PNG\r\n\x1a\n");
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return starts_with_bytes(head, 0, "\xFF\xD8\xFF");
    }
    if (extension == ".heic" || extension == ".heif") {
        return starts_with_bytes(head, 4, "ftyp");
    }
    if (extension == ".mxl" || extension == ".mscz") {
        return starts_with_bytes(head, 0, "PK\x03\x04");
    }
    if (extension == ".musicxml" || extension == ".xml") {
        const std::string_view start = xml_start(head);
        return starts_with(start, "<?xml") || starts_with(start, "<score-partwise") ||
               starts_with(start, "<score-timewise");
    }
    if (extension == ".mscx") {
        const std::string_view start = xml_start(head);
        return starts_with(start, "<?xml") || starts_with(start, "<museScore");
    }
    if (extension == ".abc") {
        return looks_like_abc(head);
    }
    return false;
}

RelPath clean_relative_path(std::string_view raw, bool allow_root) {
    RelPath result;
    const auto fail = [&](NameProblem problem) {
        result.problem = problem;
        result.path.clear();
        result.parts.clear();
        return result;
    };
    if (raw.size() > kMaxPathBytes) {
        return fail(NameProblem::TooLong);
    }
    if (raw.empty()) {
        return allow_root ? result : fail(NameProblem::Empty);
    }
    if (raw.front() == '/') {
        return fail(NameProblem::Separator);
    }
    std::string part;
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = raw.find('/', start);
        const std::string_view piece =
            raw.substr(start, slash == std::string_view::npos ? slash : slash - start);
        const NameProblem problem = check_part(piece, part);
        if (problem != NameProblem::None) {
            return fail(problem);
        }
        result.parts.push_back(part);
        if (result.parts.size() > kMaxDepth) {
            return fail(NameProblem::TooLong);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    for (std::size_t k = 0; k < result.parts.size(); ++k) {
        result.path += (k == 0 ? "" : "/") + result.parts[k];
    }
    return result;
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    char buffer[8];
    const auto unicode = [&](char32_t cp) {
        std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(cp));
        out += buffer;
    };
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        char32_t cp = 0;
        if (!decode_utf8(text, i, cp)) {
            unicode(0xFFFD); // a byte that is not UTF-8
            ++i;
            continue;
        }
        switch (cp) {
        case U'"':
            out += "\\\"";
            break;
        case U'\\':
            out += "\\\\";
            break;
        case U'\n':
            out += "\\n";
            break;
        case U'\r':
            out += "\\r";
            break;
        case U'\t':
            out += "\\t";
            break;
        case U'<':
        case U'>':
        case U'&':
        case 0x2028:
        case 0x2029:
            unicode(cp);
            break;
        default:
            if (cp < 0x20 || cp == 0x7F) {
                unicode(cp);
            } else {
                out.append(text.substr(start, i - start));
            }
        }
    }
    return out;
}

} // namespace etude
