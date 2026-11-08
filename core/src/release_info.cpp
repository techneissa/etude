// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/release_info.hpp"

#include "etude/semver.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace etude {
namespace {

struct Value {
    bool is_number = false;
    std::string text;
    std::uint64_t number = 0;
};

using Members = std::vector<std::pair<std::string, Value>>;

// A reader over a flat JSON object: members whose values are strings or whole numbers.
class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}

    bool object(Members& out) {
        skip_space();
        if (!take('{')) {
            return false;
        }
        skip_space();
        if (take('}')) {
            return at_end();
        }
        for (;;) {
            std::string key;
            Value value;
            skip_space();
            if (!string(key)) {
                return false;
            }
            skip_space();
            if (!take(':')) {
                return false;
            }
            skip_space();
            if (!read_value(value)) {
                return false;
            }
            const bool duplicate = std::any_of(out.begin(), out.end(), [&](const auto& m) {
                return m.first == key;
            });
            if (duplicate) {
                return false;
            }
            out.emplace_back(std::move(key), std::move(value));
            skip_space();
            if (take('}')) {
                return at_end();
            }
            if (!take(',')) {
                return false;
            }
        }
    }

private:
    std::string_view text_;
    std::size_t at_ = 0;

    bool take(char c) {
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }
    void skip_space() {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }
    bool at_end() {
        skip_space();
        return at_ == text_.size();
    }

    bool read_value(Value& out) {
        if (at_ < text_.size() && text_[at_] == '"') {
            return string(out.text);
        }
        const std::size_t start = at_;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            ++at_;
        }
        const std::string_view digits = text_.substr(start, at_ - start);
        if (digits.empty() || (digits.size() > 1 && digits.front() == '0')) {
            return false;
        }
        const auto [end, error] =
            std::from_chars(digits.data(), digits.data() + digits.size(), out.number);
        out.is_number = true;
        return error == std::errc() && end == digits.data() + digits.size();
    }

    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool unicode_escape(std::string& out) {
        if (at_ + 4 > text_.size()) {
            return false;
        }
        unsigned code = 0;
        const auto [end, error] =
            std::from_chars(text_.data() + at_, text_.data() + at_ + 4, code, 16);
        if (error != std::errc() || end != text_.data() + at_ + 4 || (code >= 0xD800 && code < 0xE000)) {
            return false; // not four hex digits, or half of a surrogate pair
        }
        at_ += 4;
        append_utf8(out, code);
        return true;
    }

    bool escape(std::string& out) {
        if (at_ >= text_.size()) {
            return false;
        }
        const char c = text_[at_++];
        switch (c) {
        case '"': case '\\': case '/': out.push_back(c); return true;
        case 'b': out.push_back('\b'); return true;
        case 'f': out.push_back('\f'); return true;
        case 'n': out.push_back('\n'); return true;
        case 'r': out.push_back('\r'); return true;
        case 't': out.push_back('\t'); return true;
        case 'u': return unicode_escape(out);
        default: return false;
        }
    }

    bool string(std::string& out) {
        if (!take('"')) {
            return false;
        }
        out.clear();
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c == '\\') {
                if (!escape(out)) {
                    return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }
};

bool all_of_chars(std::string_view s, bool (*ok)(char)) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ok);
}

bool is_lower_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}
bool is_word(char c) { // a Debian codename or an architecture
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}
bool is_dotted_digit(char c) {
    return (c >= '0' && c <= '9') || c == '.';
}

// A dotted version of whole numbers ("12", "12.1"): its parts, or nothing.
std::optional<std::vector<std::uint32_t>> dotted_numbers(std::string_view s) {
    std::vector<std::uint32_t> parts;
    if (s.empty() || s.size() > 16 || !all_of_chars(s, is_dotted_digit)) {
        return std::nullopt;
    }
    std::size_t from = 0;
    while (from <= s.size()) {
        std::size_t at = s.find('.', from);
        if (at == std::string_view::npos) {
            at = s.size();
        }
        const std::string_view part = s.substr(from, at - from);
        std::uint32_t n = 0;
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), n);
        if (part.empty() || error != std::errc() || end != part.data() + part.size()) {
            return std::nullopt;
        }
        parts.push_back(n);
        from = at + 1;
    }
    return parts;
}

// Whether `word` is one of the space-separated words of `list`.
bool has_token(std::string_view list, std::string_view word) {
    std::size_t from = 0;
    while (from < list.size()) {
        std::size_t at = list.find(' ', from);
        if (at == std::string_view::npos) {
            at = list.size();
        }
        if (list.substr(from, at - from) == word) {
            return true;
        }
        from = at + 1;
    }
    return false;
}

const Value* find(const Members& members, std::string_view key) {
    for (const auto& [name, value] : members) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

ReleaseInfoResult fail(ReleaseInfoError error, std::string_view field) {
    ReleaseInfoResult result;
    result.error = error;
    result.field = std::string(field);
    return result;
}

// The string member `key`, or the failure to report.
const std::string* string_member(const Members& members, std::string_view key,
                                 ReleaseInfoResult& problem) {
    const Value* value = find(members, key);
    if (value == nullptr) {
        problem = fail(ReleaseInfoError::MissingField, key);
        return nullptr;
    }
    if (value->is_number) {
        problem = fail(ReleaseInfoError::BadField, key);
        return nullptr;
    }
    return &value->text;
}

} // namespace

ReleaseInfoResult parse_release_info(std::string_view json) {
    if (json.size() > kMaxReleaseInfoBytes) {
        return fail(ReleaseInfoError::TooLarge, {});
    }
    Members members;
    if (!Reader(json).object(members)) {
        return fail(ReleaseInfoError::NotJson, {});
    }
    ReleaseInfoResult result;
    ReleaseInfo& info = result.info;
    const std::pair<std::string ReleaseInfo::*, const char*> strings[] = {
        {&ReleaseInfo::version, "version"}, {&ReleaseInfo::os, "os"},
        {&ReleaseInfo::arch, "arch"},       {&ReleaseInfo::min_os, "min_os"},
        {&ReleaseInfo::file, "file"},       {&ReleaseInfo::sha256, "sha256"}};
    for (const auto& [member, key] : strings) {
        ReleaseInfoResult problem;
        const std::string* text = string_member(members, key, problem);
        if (text == nullptr) {
            return problem;
        }
        info.*member = *text;
    }
    const Value* size = find(members, "size");
    if (size == nullptr) {
        return fail(ReleaseInfoError::MissingField, "size");
    }
    info.size = size->number;
    if (!size->is_number || info.size == 0 || info.size > kMaxReleaseBytes) {
        return fail(ReleaseInfoError::BadField, "size");
    }
    const std::optional<SemVer> version = parse_semver(info.version);
    if (!version || !version->prerelease.empty() || info.version.size() > 64) {
        return fail(ReleaseInfoError::BadField, "version");
    }
    if (info.os.size() > 32 || !all_of_chars(info.os, is_word)) {
        return fail(ReleaseInfoError::BadField, "os");
    }
    if (info.arch.size() > 32 || !all_of_chars(info.arch, is_word)) {
        return fail(ReleaseInfoError::BadField, "arch");
    }
    if (!dotted_numbers(info.min_os)) {
        return fail(ReleaseInfoError::BadField, "min_os");
    }
    if (info.sha256.size() != 64 || !all_of_chars(info.sha256, is_lower_hex)) {
        return fail(ReleaseInfoError::BadField, "sha256");
    }
    if (info.file != release_file_name(info.version, info.os, info.arch)) {
        return fail(ReleaseInfoError::BadField, "file");
    }
    return result;
}

std::string release_file_name(std::string_view version, std::string_view os, std::string_view arch) {
    return "etude-" + std::string(version) + "-" + std::string(os) + "-" + std::string(arch) +
           (os == kMacOsName ? ".zip" : ".tar.gz");
}

std::string release_trusted_comment(const ReleaseInfo& info) {
    return "etude " + info.version + " " + info.os + " " + info.arch;
}

bool parse_release_trusted_comment(std::string_view comment, ReleaseComment& out) {
    constexpr std::string_view kTool = "etude ";
    if (comment.substr(0, kTool.size()) != kTool) {
        return false;
    }
    std::string_view rest = comment.substr(kTool.size());
    std::string_view fields[3];
    for (int i = 0; i < 3; ++i) {
        const std::size_t space = rest.find(' ');
        if ((i < 2) != (space != std::string_view::npos)) {
            return false;
        }
        fields[i] = rest.substr(0, space);
        rest = space == std::string_view::npos ? std::string_view() : rest.substr(space + 1);
    }
    const std::optional<SemVer> parsed = parse_semver(fields[0]);
    if (!parsed || !parsed->prerelease.empty() || fields[0].find('+') != std::string_view::npos ||
        fields[0].size() > 32) {
        return false;
    }
    for (const std::string_view word : {fields[1], fields[2]}) {
        if (word.empty() || word.size() > 32 || !all_of_chars(word, is_word)) {
            return false;
        }
    }
    out.version.assign(fields[0]);
    out.os.assign(fields[1]);
    out.arch.assign(fields[2]);
    return true;
}

std::string signature_trusted_comment(std::string_view signature_file) {
    constexpr std::string_view kKey = "trusted comment:";
    std::size_t at = 0;
    while (at < signature_file.size()) {
        std::size_t end = signature_file.find('\n', at);
        if (end == std::string_view::npos) {
            end = signature_file.size();
        }
        std::string_view line = signature_file.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.substr(0, kKey.size()) == kKey) {
            line.remove_prefix(kKey.size());
            if (!line.empty() && line.front() == ' ') {
                line.remove_prefix(1);
            }
            return std::string(line);
        }
    }
    return {};
}

bool release_fits_machine(const ReleaseInfo& info, const MachineOs& machine) {
    const auto minimum = dotted_numbers(info.min_os);
    if (info.arch != machine.arch || !minimum) {
        return false;
    }
    // A Mac release runs on a Mac of that macOS version or newer, and on nothing else; a Debian
    // release never runs on a Mac.
    const bool mac_release = info.os == kMacOsName;
    if (mac_release != (machine.id == kMacOsName)) {
        return false;
    }
    std::string_view debian_version;
    if (mac_release) {
        debian_version = machine.version_id; // the macOS version, compared the same way
    } else if (machine.id == "debian" || machine.id == "raspbian") {
        debian_version = machine.version_id;
    } else if (has_token(machine.id_like, "debian")) {
        debian_version = machine.debian_version;
    }
    const auto have = dotted_numbers(debian_version);
    if (!have) {
        return false;
    }
    std::vector<std::uint32_t> a = *have;
    std::vector<std::uint32_t> b = *minimum;
    a.resize(std::max(a.size(), b.size()), 0);
    b.resize(a.size(), 0);
    return !(a < b);
}

} // namespace etude
