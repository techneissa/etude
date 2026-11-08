// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/tar_archive.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace etude {

namespace {

constexpr std::size_t kBlock = 512;

// The fields of a tar header block (offsets and widths in bytes).
constexpr std::size_t kNameAt = 0, kNameLen = 100;
constexpr std::size_t kModeAt = 100;
constexpr std::size_t kSizeAt = 124, kSizeLen = 12;
constexpr std::size_t kSumAt = 148, kSumLen = 8;
constexpr std::size_t kTypeAt = 156;
constexpr std::size_t kLinkAt = 157, kLinkLen = 100;
constexpr std::size_t kMagicAt = 257;
constexpr std::size_t kPrefixAt = 345, kPrefixLen = 155;

// A zero-terminated text field of at most `len` bytes.
std::string text_field(const std::uint8_t* at, std::size_t len) {
    const void* end = std::memchr(at, 0, len);
    const std::size_t n = end == nullptr ? len : static_cast<std::size_t>(
                                                     static_cast<const std::uint8_t*>(end) - at);
    return std::string(reinterpret_cast<const char*>(at), n);
}

// An octal number in a header field (digits, then spaces or zeros); false for anything else.
bool octal_field(const std::uint8_t* at, std::size_t len, std::uint64_t& out) {
    std::size_t i = 0;
    while (i < len && at[i] == ' ') {
        ++i;
    }
    if (i == len || at[i] < '0' || at[i] > '7') {
        return false;
    }
    std::uint64_t value = 0;
    for (; i < len && at[i] >= '0' && at[i] <= '7'; ++i) {
        if (value > (UINT64_MAX >> 3)) {
            return false;
        }
        value = (value << 3) | static_cast<std::uint64_t>(at[i] - '0');
    }
    for (; i < len; ++i) {
        if (at[i] != ' ' && at[i] != 0) {
            return false;
        }
    }
    out = value;
    return true;
}

bool block_is_zero(const std::uint8_t* at) {
    return std::all_of(at, at + kBlock, [](std::uint8_t b) { return b == 0; });
}

bool checksum_ok(const std::uint8_t* header) {
    std::uint64_t want = 0;
    if (!octal_field(header + kSumAt, kSumLen, want)) {
        return false;
    }
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i) {
        sum += (i >= kSumAt && i < kSumAt + kSumLen) ? 32 : header[i];
    }
    return sum == want;
}

// The parts of a '/'-separated path (one trailing '/' allowed, on a folder), or false for an empty
// part, `.`, `..`, a backslash, a control character or a leading '/'.
bool split_path(std::string path, std::vector<std::string>& parts) {
    parts.clear();
    if (path.empty() || path.front() == '/') {
        return false;
    }
    if (path.back() == '/') {
        path.pop_back();
    }
    if (path.empty()) {
        return false;
    }
    std::size_t at = 0;
    for (;;) {
        std::size_t end = path.find('/', at);
        if (end == std::string::npos) {
            end = path.size();
        }
        const std::string part = path.substr(at, end - at);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (const char c : part) {
            if (c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                return false;
            }
        }
        parts.push_back(part);
        if (end == path.size()) {
            return true;
        }
        at = end + 1;
    }
}

// Whether a symbolic link at `folder` (the parts of the folder it is in, below the top) with this
// target stays inside the top folder, walking the target's parts from there. A `..` may only come
// before the first name: after a name it would climb out of whatever that name is, and a name that is
// itself a link (`a/b` -> `..`, then `c` -> `a/b/..`) makes it leave the top folder though the target
// reads as inside. With the climbing first, every name after it only goes down.
bool link_stays_inside(std::vector<std::string> folder, const std::string& target) {
    if (target.empty() || target.front() == '/') {
        return false;
    }
    bool named = false;
    std::size_t at = 0;
    while (at <= target.size()) {
        std::size_t end = target.find('/', at);
        if (end == std::string::npos) {
            end = target.size();
        }
        const std::string part = target.substr(at, end - at);
        for (const char c : part) {
            if (c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                return false;
            }
        }
        if (part == "..") {
            if (named || folder.empty()) {
                return false;
            }
            folder.pop_back();
        } else if (!part.empty() && part != ".") {
            named = true;
            folder.push_back(part);
        }
        if (end == target.size()) {
            break;
        }
        at = end + 1;
    }
    return true;
}

TarResult fail(TarError error, std::string path = {}) {
    TarResult r;
    r.error = error;
    r.path = std::move(path);
    return r;
}

// Values the next entry takes from a GNU long-name entry or a pax header.
struct Pending {
    bool has_path = false;
    std::string path;
    bool has_link = false;
    std::string link;
    bool has_size = false;
    std::uint64_t size = 0;
};

// The records of a pax header, "<length> <key>=<value>\n", for the keys path, linkpath and size.
bool read_pax(std::span<const std::uint8_t> data, Pending& out) {
    std::size_t at = 0;
    while (at < data.size()) {
        std::size_t length = 0;
        std::size_t i = at;
        while (i < data.size() && data[i] >= '0' && data[i] <= '9' && length < 100000) {
            length = length * 10 + static_cast<std::size_t>(data[i] - '0');
            ++i;
        }
        if (i >= data.size() || data[i] != ' ' || length < i - at + 3 || at + length > data.size()) {
            return false;
        }
        const std::string record(reinterpret_cast<const char*>(data.data()) + i + 1,
                                 at + length - i - 1);
        if (record.back() != '\n') {
            return false;
        }
        const std::size_t eq = record.find('=');
        if (eq == std::string::npos) {
            return false;
        }
        const std::string key = record.substr(0, eq);
        const std::string value = record.substr(eq + 1, record.size() - eq - 2);
        if (key == "path") {
            out.has_path = true;
            out.path = value;
        } else if (key == "linkpath") {
            out.has_link = true;
            out.link = value;
        } else if (key == "size") {
            out.has_size = true;
            out.size = 0;
            for (const char c : value) {
                if (c < '0' || c > '9' || out.size > (UINT64_MAX / 10) - 9) {
                    return false;
                }
                out.size = out.size * 10 + static_cast<std::uint64_t>(c - '0');
            }
        }
        at += length;
    }
    return true;
}

// Looks up whether any folder on the way to `parts` (not the last part) is a file or link.
bool below_file(const std::map<std::string, TarKind>& seen, const std::vector<std::string>& parts) {
    std::string prefix;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        prefix += (i == 0 ? "" : "/") + parts[i];
        const auto it = seen.find(prefix);
        if (it != seen.end() && it->second != TarKind::Directory) {
            return true;
        }
    }
    return false;
}

// What the reading has seen so far.
struct State {
    std::string_view top;
    TarLimits limits;
    std::map<std::string, TarKind> seen; // path below the top folder -> kind
    std::set<std::string> implied;       // folders only named by the path of something in them
    Pending pending;
    std::size_t total = 0;
    bool any = false;
    TarArchive archive;
    std::string bad; // the path at fault
};

// What one header says before its path is checked.
struct Head {
    char type = '0';
    std::uint64_t size = 0;
    std::size_t data_at = 0;
    std::string name;
    std::string link;
    std::uint64_t mode = 0;
};

// The name of the entry at `header`: a long-name or pax path, else the header's own (with the
// ustar prefix in front of it when the archive is POSIX ustar, not GNU).
std::string entry_name(const Pending& pending, const std::uint8_t* header) {
    if (pending.has_path) {
        return pending.path;
    }
    std::string name = text_field(header + kNameAt, kNameLen);
    const bool posix = std::memcmp(header + kMagicAt, "ustar\0", 6) == 0;
    const std::string prefix = posix ? text_field(header + kPrefixAt, kPrefixLen) : "";
    return prefix.empty() ? name : prefix + "/" + name;
}

// The entry for `head` below the top folder, with its kind, mode and link checked; `parts` are the
// path's parts below the top folder.
TarError make_entry(State& s, const Head& head, TarEntry& entry, std::vector<std::string>& parts) {
    s.bad = head.name;
    if (!split_path(head.name, parts)) {
        return TarError::BadPath;
    }
    if (parts.front() != s.top) {
        return TarError::WrongTop;
    }
    parts.erase(parts.begin());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        entry.path += (i == 0 ? "" : "/") + parts[i];
    }
    const bool is_dir = head.type == '5' || (!head.name.empty() && head.name.back() == '/');
    entry.kind = head.type == '2' ? TarKind::Symlink : (is_dir ? TarKind::Directory : TarKind::File);
    if (entry.kind == TarKind::File) {
        entry.size = static_cast<std::size_t>(head.size);
        entry.offset = head.data_at;
        entry.mode = (head.mode & 0111) != 0 ? 0755 : 0644;
        if (entry.size > s.limits.max_file_bytes || s.total + entry.size > s.limits.max_total_bytes) {
            return TarError::TooBig;
        }
        s.total += entry.size;
    } else if (entry.kind == TarKind::Directory) {
        entry.mode = 0755;
    } else {
        entry.mode = 0777;
        entry.link = head.link;
        const auto folder_end = parts.empty() ? parts.end() : parts.end() - 1;
        if (!link_stays_inside(std::vector<std::string>(parts.begin(), folder_end), head.link)) {
            return TarError::BadLink;
        }
    }
    return TarError::None;
}

// Notes `entry` among the ones seen: no repeat, nothing below a file or link, no file where a
// folder has things in it.
TarError register_entry(State& s, const TarEntry& entry, const std::vector<std::string>& parts) {
    if (entry.path.empty()) {
        // The top folder's own entry: a folder, once, first.
        if (entry.kind != TarKind::Directory) {
            return TarError::UnderFile;
        }
        return s.any ? TarError::Duplicate : TarError::None;
    }
    if (below_file(s.seen, parts)) {
        return TarError::UnderFile;
    }
    if (!s.seen.emplace(entry.path, entry.kind).second ||
        (entry.kind != TarKind::Directory && s.implied.count(entry.path) != 0)) {
        return TarError::Duplicate;
    }
    std::string prefix;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        prefix += (i == 0 ? "" : "/") + parts[i];
        s.implied.insert(prefix);
    }
    return TarError::None;
}

// Takes a GNU long-name or long-link entry, or a pax header, for the entry that follows.
TarError take_meta(State& s, char type, std::span<const std::uint8_t> data) {
    if (type == 'x') {
        return read_pax(data, s.pending) ? TarError::None : TarError::BadHeader;
    }
    const std::string value = text_field(data.data(), data.size());
    (type == 'L' ? s.pending.path : s.pending.link) = value;
    (type == 'L' ? s.pending.has_path : s.pending.has_link) = true;
    return TarError::None;
}

// Checks and adds the entry whose header is at `header`.
TarError take_entry(State& s, const std::uint8_t* header, Head head) {
    if (head.type != '0' && head.type != 0 && head.type != '5' && head.type != '2') {
        s.bad = text_field(header + kNameAt, kNameLen);
        return TarError::Unsupported;
    }
    head.name = entry_name(s.pending, header);
    head.link = s.pending.has_link ? s.pending.link : text_field(header + kLinkAt, kLinkLen);
    s.pending = Pending{};
    octal_field(header + kModeAt, 8, head.mode);
    TarEntry entry;
    std::vector<std::string> parts;
    if (const TarError error = make_entry(s, head, entry, parts); error != TarError::None) {
        return error;
    }
    if (const TarError error = register_entry(s, entry, parts); error != TarError::None) {
        return error;
    }
    if (s.archive.entries.size() >= s.limits.max_entries) {
        return TarError::TooBig;
    }
    s.any = true;
    s.archive.entries.push_back(std::move(entry));
    return TarError::None;
}

} // namespace

TarResult read_tar(std::span<const std::uint8_t> tar, std::string_view top,
                   const TarLimits& limits) {
    State s;
    s.top = top;
    s.limits = limits;
    std::size_t at = 0;
    while (at + kBlock <= tar.size()) {
        const std::uint8_t* header = tar.data() + at;
        if (block_is_zero(header)) {
            break;
        }
        Head head;
        if (!checksum_ok(header) || !octal_field(header + kSizeAt, kSizeLen, head.size)) {
            return fail(TarError::BadHeader);
        }
        head.type = static_cast<char>(header[kTypeAt]);
        const bool is_meta = head.type == 'L' || head.type == 'K' || head.type == 'x' || head.type == 'g';
        if (!is_meta && s.pending.has_size) {
            head.size = s.pending.size;
        }
        head.data_at = at + kBlock;
        if (head.size > tar.size() || head.data_at > tar.size() - static_cast<std::size_t>(head.size)) {
            return fail(TarError::Truncated);
        }
        const std::size_t size = static_cast<std::size_t>(head.size);
        const std::size_t padded = (size + kBlock - 1) / kBlock * kBlock;
        TarError error = TarError::None;
        if (head.type == 'L' || head.type == 'K' || head.type == 'x') {
            error = take_meta(s, head.type, tar.subspan(head.data_at, size));
        } else if (head.type != 'g') { // global pax records name nothing this reader uses
            error = take_entry(s, header, head);
        }
        if (error != TarError::None) {
            return fail(error, s.bad);
        }
        at = head.data_at + std::min(padded, tar.size() - head.data_at);
    }
    if (!s.any) {
        return fail(TarError::MissingTop);
    }
    TarResult result;
    result.archive = std::move(s.archive);
    return result;
}

std::string tar_error_text(const TarResult& r) {
    const auto with_path = [&](const char* text) {
        return r.path.empty() ? std::string(text) : std::string(text) + ": " + r.path;
    };
    switch (r.error) {
    case TarError::None:
        return {};
    case TarError::Truncated:
        return "the archive is cut off";
    case TarError::BadHeader:
        return "the archive is damaged";
    case TarError::Unsupported:
        return with_path("the archive holds a kind of file that is not allowed");
    case TarError::BadPath:
        return with_path("the archive has a path that is not allowed");
    case TarError::WrongTop:
        return with_path("the archive has a file outside its folder");
    case TarError::BadLink:
        return with_path("the archive has a link that leaves its folder");
    case TarError::Duplicate:
        return with_path("the archive names a path twice");
    case TarError::UnderFile:
        return with_path("the archive puts a file below a file or link");
    case TarError::TooBig:
        return "the archive is too large";
    case TarError::MissingTop:
        return "the archive is empty";
    }
    return {};
}

} // namespace etude
