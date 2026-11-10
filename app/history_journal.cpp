// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_journal.hpp"

#include "etude/content_hash.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>
#include <type_traits>

#include <fcntl.h>
#include <unistd.h>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr char kKindRun = 'R';
constexpr char kKindPractice = 'P';
constexpr char kKindPrefs = 'S';
constexpr char kKindGroups = 'G';
constexpr char kKindOrigins = 'O';
constexpr char kKindVersions = 'V';
constexpr char kKindPin = 'N';
constexpr char kKindProfiles = 'F';
constexpr char kKindProfileSettings = 'T';
constexpr char kKindProfileDelete = 'D';

void put_u32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}

void put_u64(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
    }
}

void put_varint(std::string& out, std::uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

void put_zigzag(std::string& out, std::int64_t v) {
    put_varint(out, (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63));
}

void put_bytes(std::string& out, std::string_view bytes) {
    put_varint(out, bytes.size());
    out += bytes;
}

// A text cut to kMaxJournalText at a character boundary.
void put_text(std::string& out, std::string_view text) {
    if (text.size() > kMaxJournalText) {
        std::size_t end = kMaxJournalText;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end; // not inside a UTF-8 sequence
        }
        text = text.substr(0, end);
    }
    put_bytes(out, text);
}

std::uint32_t check_of(std::string_view payload) {
    return static_cast<std::uint32_t>(content_hash(payload) & 0xFFFFFFFFu);
}

// Reads a payload; every read says whether the bytes were there.
class Reader {
  public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}
    bool done() const { return at_ >= bytes_.size(); }

    bool byte(std::uint8_t& out) {
        if (done()) {
            return false;
        }
        out = static_cast<std::uint8_t>(bytes_[at_++]);
        return true;
    }
    bool u64(std::uint64_t& out) {
        if (bytes_.size() - at_ < 8) {
            return false;
        }
        out = 0;
        for (int i = 0; i < 8; ++i) {
            out |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[at_++])) << (8 * i);
        }
        return true;
    }
    bool varint(std::uint64_t& out) {
        out = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            std::uint8_t b = 0;
            if (!byte(b)) {
                return false;
            }
            if (shift == 63 && (b & 0x7E) != 0) {
                return false; // more than 64 bits
            }
            out |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }
    bool zigzag(std::int64_t& out) {
        std::uint64_t v = 0;
        if (!varint(v)) {
            return false;
        }
        out = static_cast<std::int64_t>(v >> 1) ^ -static_cast<std::int64_t>(v & 1);
        return true;
    }
    // A zigzag that has to fit an int.
    bool integer(int& out) {
        std::int64_t v = 0;
        if (!zigzag(v) || v < -2'000'000'000 || v > 2'000'000'000) {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    }
    bool flag(bool& out) {
        std::uint8_t b = 0;
        if (!byte(b) || b > 1) {
            return false;
        }
        out = b == 1;
        return true;
    }
    bool bytes(std::string_view& out) {
        std::uint64_t n = 0;
        if (!varint(n) || n > bytes_.size() - at_) {
            return false;
        }
        out = bytes_.substr(at_, static_cast<std::size_t>(n));
        at_ += static_cast<std::size_t>(n);
        return true;
    }

  private:
    std::string_view bytes_;
    std::size_t at_ = 0;
};

std::string run_payload(const JournalRun& run) {
    std::string out(1, kKindRun);
    put_bytes(out, serialize_run_record(run.record));
    put_bytes(out, run.song_path);
    const RunEvents& set = run.events;
    put_varint(out, set.events.size());
    put_zigzag(out, set.dropped);
    put_zigzag(out, set.expected_notes);
    out.push_back(set.complete ? 1 : 0);
    for (const RunEvent& e : set.events) {
        put_zigzag(out, e.song_ms);
        put_zigzag(out, e.error_ms);
        put_zigzag(out, e.note_index);
        out.push_back(static_cast<char>(e.kind));
        out.push_back(static_cast<char>(e.key));
        out.push_back(static_cast<char>(e.velocity));
        out.push_back(static_cast<char>(e.hand));
        out.push_back(static_cast<char>(e.grade));
    }
    put_zigzag(out, run.profile_id);
    put_zigzag(out, run.record.score);
    put_zigzag(out, run.record.covered_percent);
    return out;
}

std::string practice_payload(const JournalPractice& entry) {
    const SongPractice& p = entry.practice;
    std::string out(1, kKindPractice);
    put_u64(out, entry.hash);
    put_zigzag(out, p.speed_percent);
    put_zigzag(out, p.count_in_bars);
    out.push_back(static_cast<char>(p.hands));
    put_zigzag(out, p.transpose_semitones);
    out.push_back(p.parts_set ? 1 : 0);
    put_varint(out, p.muted_parts.size());
    for (const std::uint32_t key : p.muted_parts) {
        put_varint(out, key);
    }
    // Milliseconds, as the database keeps them.
    put_zigzag(out, std::llround(p.loop_start_s * 1000.0));
    put_zigzag(out, std::llround(p.loop_end_s * 1000.0));
    out.push_back(p.loop_enabled ? 1 : 0);
    put_zigzag(out, p.section_id);
    put_zigzag(out, p.plan_item);
    out.push_back(p.no_shift_offer ? 1 : 0);
    put_zigzag(out, p.split_point);
    put_zigzag(out, entry.at_utc_s);
    put_zigzag(out, entry.profile_id);
    return out;
}

std::string prefs_payload(const JournalPrefs& entry) {
    std::string out(1, kKindPrefs);
    put_varint(out, entry.items.size());
    for (const auto& [hash, p] : entry.items) {
        put_u64(out, hash);
        put_bytes(out, p.title);
        put_bytes(out, p.composer);
        put_bytes(out, p.genre);
        out.push_back(p.favourite ? 1 : 0);
        put_zigzag(out, p.added_utc_s);
        put_zigzag(out, p.chosen_utc_s);
    }
    return out;
}

std::string groups_payload(const JournalGroups& entry) {
    std::string out(1, kKindGroups);
    put_varint(out, entry.edits.size());
    for (const GroupEdit& e : entry.edits) {
        out.push_back(static_cast<char>(e.kind));
        put_u64(out, e.group.id);
        put_text(out, e.group.name);
        out.push_back(static_cast<char>(e.group.kind));
        put_text(out, e.group.source);
        put_zigzag(out, e.group.position);
        put_zigzag(out, e.group.created_utc_s);
        put_u64(out, e.hash);
        put_zigzag(out, e.at_utc_s);
    }
    return out;
}

std::string origins_payload(const JournalOrigins& entry) {
    std::string out(1, kKindOrigins);
    put_varint(out, entry.items.size());
    for (const auto& [hash, s] : entry.items) {
        put_u64(out, hash);
        for (const std::string* text : {&s.catalogue, &s.id, &s.title, &s.composer, &s.licence,
                                        &s.licence_url, &s.credit, &s.url, &s.page}) {
            put_text(out, *text);
        }
        put_zigzag(out, s.downloaded_utc_s);
    }
    return out;
}

std::string versions_payload(const JournalVersions& entry) {
    std::string out(1, kKindVersions);
    put_varint(out, entry.rows.size());
    for (const VersionRow& row : entry.rows) {
        put_u64(out, row.a);
        put_u64(out, row.b);
        out.push_back(row.add ? 1 : 0);
    }
    return out;
}

std::string profiles_payload(const JournalProfiles& entry) {
    std::string out(1, kKindProfiles);
    put_varint(out, entry.rows.size());
    for (const Profile& p : entry.rows) {
        put_zigzag(out, p.id);
        put_bytes(out, p.name);
        put_zigzag(out, p.colour);
        put_zigzag(out, p.created_utc_s);
    }
    return out;
}

std::string profile_settings_payload(const JournalProfileSettings& entry) {
    std::string out(1, kKindProfileSettings);
    put_zigzag(out, entry.profile_id);
    put_bytes(out, std::string_view(entry.text).substr(0, kMaxJournalSettingsBytes));
    put_zigzag(out, entry.updated_utc_s);
    return out;
}

std::string profile_delete_payload(const JournalProfileDelete& entry) {
    std::string out(1, kKindProfileDelete);
    put_zigzag(out, entry.profile_id);
    return out;
}

std::string pin_payload(const JournalPin& entry) {
    std::string out(1, kKindPin);
    put_zigzag(out, entry.run_id);
    out.push_back(entry.pinned ? 1 : 0);
    put_zigzag(out, entry.profile_id);
    return out;
}

bool parse_run(Reader& in, JournalRun& out) {
    std::string_view blob;
    std::string_view path;
    if (!in.bytes(blob) || !in.bytes(path)) {
        return false;
    }
    if (parse_run_record(blob, out.record) != RunParseStatus::Ok) {
        return false;
    }
    out.song_path = std::string(path);
    std::uint64_t count = 0;
    std::int64_t dropped = 0;
    std::int64_t expected = 0;
    bool complete = false;
    if (!in.varint(count) || count > kRunEventCapacity || !in.zigzag(dropped) ||
        !in.zigzag(expected) || !in.flag(complete) || dropped < 0 || dropped > 2'000'000'000 ||
        expected < 0 || expected > 2'000'000'000) {
        return false;
    }
    RunEvents& set = out.events;
    set.dropped = static_cast<std::int32_t>(dropped);
    set.expected_notes = static_cast<std::int32_t>(expected);
    set.complete = complete;
    set.events.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        RunEvent e;
        int song_ms = 0;
        int error_ms = 0;
        int note_index = 0;
        std::uint8_t kind = 0;
        std::uint8_t key = 0;
        std::uint8_t velocity = 0;
        std::uint8_t hand = 0;
        std::uint8_t grade = 0;
        if (!in.integer(song_ms) || !in.integer(error_ms) || !in.integer(note_index) ||
            !in.byte(kind) || !in.byte(key) || !in.byte(velocity) || !in.byte(hand) ||
            !in.byte(grade) || kind > static_cast<std::uint8_t>(RunEventKind::Missed) ||
            key > 127 || velocity > 127 || hand > static_cast<std::uint8_t>(Hand::Right) ||
            grade > static_cast<std::uint8_t>(NoteGrade::Played)) {
            return false;
        }
        e.song_ms = song_ms;
        e.error_ms = error_ms;
        e.note_index = note_index;
        e.kind = static_cast<RunEventKind>(kind);
        e.key = key;
        e.velocity = velocity;
        e.hand = static_cast<Hand>(hand);
        e.grade = static_cast<NoteGrade>(grade);
        set.events.push_back(e);
    }
    // Fields added later sit at the end; a record from before them is the default profile's, and
    // has no score.
    std::int64_t profile = kDefaultProfileId;
    int score = 0;
    int covered = 0;
    if (!in.done() && (!in.zigzag(profile) || profile <= 0 || !in.integer(score) ||
                       !in.integer(covered))) {
        return false;
    }
    out.profile_id = profile;
    out.record.score = score;
    out.record.covered_percent = covered;
    out.record = sanitized(out.record);
    return true; // bytes after the known fields are ignored
}

bool parse_practice(Reader& in, JournalPractice& out) {
    SongPractice p;
    std::uint8_t hands = 0;
    std::uint64_t parts = 0;
    std::int64_t loop_start_ms = 0;
    std::int64_t loop_end_ms = 0;
    if (!in.u64(out.hash) || !in.integer(p.speed_percent) || !in.integer(p.count_in_bars) ||
        !in.byte(hands) || hands > static_cast<std::uint8_t>(HandFilter::Right) ||
        !in.integer(p.transpose_semitones) || !in.flag(p.parts_set) || !in.varint(parts) ||
        parts > kMaxSongPracticeParts) {
        return false;
    }
    p.hands = static_cast<HandFilter>(hands);
    for (std::uint64_t i = 0; i < parts; ++i) {
        std::uint64_t key = 0;
        if (!in.varint(key) || key > 0xFFFFFFFFu) {
            return false;
        }
        p.muted_parts.push_back(static_cast<std::uint32_t>(key));
    }
    if (!in.zigzag(loop_start_ms) || !in.zigzag(loop_end_ms) || !in.flag(p.loop_enabled) ||
        !in.integer(p.section_id) || !in.integer(p.plan_item) || !in.flag(p.no_shift_offer)) {
        return false;
    }
    // Fields added later sit at the end; a record from before them leaves them at the default.
    if (!in.done() && !in.integer(p.split_point)) {
        return false;
    }
    if (!in.done() && !in.zigzag(out.at_utc_s)) {
        return false;
    }
    if (!in.done() && (!in.zigzag(out.profile_id) || out.profile_id <= 0)) {
        return false;
    }
    p.loop_start_s = static_cast<double>(loop_start_ms) / 1000.0;
    p.loop_end_s = static_cast<double>(loop_end_ms) / 1000.0;
    out.practice = sanitized(p);
    return out.hash != 0;
}

bool read_text(Reader& in, std::string& out) {
    std::string_view text;
    if (!in.bytes(text)) {
        return false;
    }
    out = std::string(text);
    return true;
}

// The count in front of a list of library items: what a writer can have made (kMaxJournalItems a
// record), with room for a later writer's longer ones.
bool read_count(Reader& in, std::uint64_t& count) {
    return in.varint(count) && count <= 100'000;
}

bool parse_prefs(Reader& in, JournalPrefs& out) {
    std::uint64_t count = 0;
    if (!read_count(in, count)) {
        return false;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t hash = 0;
        SongPrefs p;
        if (!in.u64(hash) || hash == 0 || !read_text(in, p.title) || !read_text(in, p.composer) ||
            !read_text(in, p.genre) || !in.flag(p.favourite) || !in.zigzag(p.added_utc_s) ||
            !in.zigzag(p.chosen_utc_s)) {
            return false;
        }
        out.items.emplace_back(hash, sanitized(p));
    }
    return count > 0;
}

bool parse_groups(Reader& in, JournalGroups& out) {
    std::uint64_t count = 0;
    if (!read_count(in, count)) {
        return false;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        GroupEdit e;
        std::uint8_t kind = 0;
        std::uint8_t group_kind = 0;
        int position = 0;
        if (!in.byte(kind) || kind > static_cast<std::uint8_t>(GroupEdit::Kind::Move) ||
            !in.u64(e.group.id) || e.group.id == 0 || !read_text(in, e.group.name) ||
            !in.byte(group_kind) || group_kind > static_cast<std::uint8_t>(GroupKind::Catalogue) ||
            !read_text(in, e.group.source) || !in.integer(position) ||
            !in.zigzag(e.group.created_utc_s) || !in.u64(e.hash) || !in.zigzag(e.at_utc_s)) {
            return false;
        }
        e.kind = static_cast<GroupEdit::Kind>(kind);
        e.group.kind = static_cast<GroupKind>(group_kind);
        e.group.position = position;
        out.edits.push_back(std::move(e));
    }
    return count > 0;
}

bool parse_origins(Reader& in, JournalOrigins& out) {
    std::uint64_t count = 0;
    if (!read_count(in, count)) {
        return false;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t hash = 0;
        SongOrigin s;
        if (!in.u64(hash) || hash == 0) {
            return false;
        }
        for (std::string* text : {&s.catalogue, &s.id, &s.title, &s.composer, &s.licence,
                                  &s.licence_url, &s.credit, &s.url, &s.page}) {
            if (!read_text(in, *text)) {
                return false;
            }
        }
        if (!in.zigzag(s.downloaded_utc_s)) {
            return false;
        }
        out.items.emplace_back(hash, std::move(s));
    }
    return count > 0;
}

bool parse_versions(Reader& in, JournalVersions& out) {
    std::uint64_t count = 0;
    if (!read_count(in, count)) {
        return false;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        VersionRow row;
        if (!in.u64(row.a) || row.a == 0 || !in.u64(row.b) || !in.flag(row.add)) {
            return false;
        }
        out.rows.push_back(row);
    }
    return count > 0;
}

bool parse_profiles(Reader& in, JournalProfiles& out) {
    std::uint64_t count = 0;
    if (!read_count(in, count)) {
        return false;
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        Profile p;
        std::int64_t colour = 0;
        if (!in.zigzag(p.id) || p.id <= 0 || !read_text(in, p.name) || !in.zigzag(colour) ||
            !in.zigzag(p.created_utc_s)) {
            return false;
        }
        p.colour = static_cast<std::uint32_t>(colour & 0xFFFFFF);
        out.rows.push_back(sanitized(p));
    }
    return count > 0;
}

bool parse_profile_settings(Reader& in, JournalProfileSettings& out) {
    return in.zigzag(out.profile_id) && out.profile_id > 0 && read_text(in, out.text) &&
           in.zigzag(out.updated_utc_s);
}

bool parse_profile_delete(Reader& in, JournalProfileDelete& out) {
    // The first profile is never deleted (core ProfileSet::check_delete): a record naming it is damage.
    return in.zigzag(out.profile_id) && out.profile_id > kDefaultProfileId;
}

bool parse_pin(Reader& in, JournalPin& out) {
    if (!in.zigzag(out.run_id) || out.run_id <= 0 || !in.flag(out.pinned)) {
        return false;
    }
    // Added later, at the end: a record from before it is the default profile's.
    return in.done() || (in.zigzag(out.profile_id) && out.profile_id > 0);
}

template <typename Entry, typename Parse>
bool parse_into(Reader& in, JournalEntry& out, Parse parse) {
    Entry entry;
    if (!parse(in, entry)) {
        return false;
    }
    out = std::move(entry);
    return true;
}

bool parse_payload(std::string_view payload, JournalEntry& out) {
    if (payload.empty()) {
        return false;
    }
    Reader in(payload.substr(1));
    if (payload[0] == kKindRun) {
        JournalRun run;
        if (!parse_run(in, run)) {
            return false;
        }
        out = std::move(run);
        return true;
    }
    if (payload[0] == kKindPractice) {
        JournalPractice practice;
        if (!parse_practice(in, practice)) {
            return false;
        }
        out = std::move(practice);
        return true;
    }
    switch (payload[0]) {
    case kKindPrefs:
        return parse_into<JournalPrefs>(in, out, parse_prefs);
    case kKindGroups:
        return parse_into<JournalGroups>(in, out, parse_groups);
    case kKindOrigins:
        return parse_into<JournalOrigins>(in, out, parse_origins);
    case kKindVersions:
        return parse_into<JournalVersions>(in, out, parse_versions);
    case kKindPin:
        return parse_into<JournalPin>(in, out, parse_pin);
    case kKindProfiles:
        return parse_into<JournalProfiles>(in, out, parse_profiles);
    case kKindProfileSettings:
        return parse_into<JournalProfileSettings>(in, out, parse_profile_settings);
    case kKindProfileDelete:
        return parse_into<JournalProfileDelete>(in, out, parse_profile_delete);
    default:
        return false; // a kind a later build added: skipped, counted as damaged
    }
}

std::uint32_t get_u32(std::string_view bytes, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(
                 static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(i)]))
             << (8 * i);
    }
    return v;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

std::string encode_journal_header() {
    return std::string(kJournalHeaderPrefix) + std::to_string(kJournalVersion) + "\n";
}

std::string encode_journal_entry(const JournalEntry& entry) {
    const std::string payload = std::visit(
        [](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, JournalRun>) {
                return run_payload(e);
            } else if constexpr (std::is_same_v<T, JournalPractice>) {
                return practice_payload(e);
            } else if constexpr (std::is_same_v<T, JournalPrefs>) {
                return prefs_payload(e);
            } else if constexpr (std::is_same_v<T, JournalGroups>) {
                return groups_payload(e);
            } else if constexpr (std::is_same_v<T, JournalOrigins>) {
                return origins_payload(e);
            } else if constexpr (std::is_same_v<T, JournalVersions>) {
                return versions_payload(e);
            } else if constexpr (std::is_same_v<T, JournalProfiles>) {
                return profiles_payload(e);
            } else if constexpr (std::is_same_v<T, JournalProfileSettings>) {
                return profile_settings_payload(e);
            } else if constexpr (std::is_same_v<T, JournalProfileDelete>) {
                return profile_delete_payload(e);
            } else {
                return pin_payload(e);
            }
        },
        entry);
    std::string out;
    if (payload.size() > kMaxJournalRecordBytes) {
        return out;
    }
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    put_u32(out, check_of(payload));
    out += payload;
    return out;
}

JournalDecode decode_journal(std::string_view bytes) {
    JournalDecode out;
    if (bytes.empty()) {
        return out;
    }
    const std::string_view prefix = kJournalHeaderPrefix;
    if (bytes.substr(0, prefix.size()) != prefix) {
        // A header cut short by a torn first write is an empty journal; anything else is not ours.
        if (prefix.substr(0, bytes.size()) == bytes) {
            out.truncated = true;
        } else {
            out.bad_header = true;
        }
        return out;
    }
    const std::size_t newline = bytes.find('\n');
    const std::string_view number =
        bytes.substr(prefix.size(), newline == std::string_view::npos ? std::string_view::npos
                                                                      : newline - prefix.size());
    int version = 0;
    bool digits = number.size() <= 6;
    for (const char c : number) {
        if (c < '0' || c > '9') {
            digits = false;
            break;
        }
        version = version * 10 + (c - '0');
    }
    if (newline == std::string_view::npos) {
        // "etude journal " or "etude journal 1" and no line end: a header cut short.
        (digits ? out.truncated : out.bad_header) = true;
        return out;
    }
    if (!digits || number.empty() || version <= 0) {
        out.bad_header = true;
        return out;
    }
    if (version > kJournalVersion) {
        out.newer_version = version;
        return out;
    }
    std::size_t at = newline + 1;
    out.valid_end = at;
    while (at < bytes.size()) {
        if (bytes.size() - at < 8) {
            out.truncated = true;
            break;
        }
        const std::uint32_t length = get_u32(bytes, at);
        const std::uint32_t check = get_u32(bytes, at + 4);
        if (length == 0 || length > kMaxJournalRecordBytes) {
            // The framing itself is damaged: nothing after it can be trusted to line up.
            ++out.damaged;
            out.framing_damaged = true;
            break;
        }
        if (bytes.size() - at - 8 < length) {
            out.truncated = true;
            break;
        }
        const std::string_view payload = bytes.substr(at + 8, length);
        at += 8 + length;
        out.valid_end = at;
        JournalEntry entry;
        if (check_of(payload) != check || !parse_payload(payload, entry)) {
            ++out.damaged;
            continue;
        }
        out.entries.push_back(std::move(entry));
    }
    return out;
}

JournalAppend append_journal(const fs::path& path, std::span<const JournalEntry> entries) {
    JournalAppend result;
    const std::size_t asked = entries.size();
    std::error_code ec;
    std::size_t keep = 0;               // the bytes of an existing journal to keep
    std::vector<JournalEntry> combined; // the records read before a damaged length, and `entries`
    if (fs::exists(path, ec) || fs::is_symlink(path, ec)) {
        if (!fs::is_regular_file(path, ec)) {
            result.why = "it is not a file";
            return result;
        }
        const std::string existing = read_file(path);
        JournalDecode found = decode_journal(existing);
        if (found.bad_header) {
            result.why = "the file there is not a journal";
            return result;
        }
        if (found.newer_version != 0) {
            result.why = "the file there is from a newer Etude (journal version " +
                         std::to_string(found.newer_version) + ")";
            return result;
        }
        keep = found.valid_end; // 0 for an empty file; a torn tail is cut off
        if (found.framing_damaged) {
            // Appending would put the new records behind bytes no reader gets past, and cutting
            // there would lose the ones after: the file is kept whole, aside.
            fs::path aside;
            for (int n = 0; n < 8 && aside.empty(); ++n) {
                fs::path candidate = path;
                candidate += n == 0 ? ".rejected" : ".rejected." + std::to_string(n);
                if (!fs::exists(candidate, ec) && !fs::is_symlink(candidate, ec)) {
                    aside = candidate;
                }
            }
            if (aside.empty()) {
                result.why = "the file there is damaged and every name for a copy is taken";
                return result;
            }
            fs::rename(path, aside, ec);
            if (ec) {
                result.why =
                    "the file there is damaged and cannot be moved aside (" + ec.message() + ")";
                return result;
            }
            combined = std::move(found.entries);
            combined.insert(combined.end(), entries.begin(), entries.end());
            entries = combined;
            keep = 0;
        }
    }
    // Each entry as it goes to disk; nothing for one that is over the cap.
    std::vector<std::string> framed;
    framed.reserve(entries.size());
    for (const JournalEntry& entry : entries) {
        framed.push_back(encode_journal_entry(entry));
    }
    const std::size_t old = entries.size() - asked; // records read before a damaged length
    std::vector<bool> wrote(entries.size(), false);
    const auto note = [&result](const std::string& why) {
        if (result.why.empty()) {
            result.why = why;
        }
    };
    const auto finish = [&] {
        result.kept.assign(wrote.begin() + static_cast<std::ptrdiff_t>(old), wrote.end());
        result.saved = static_cast<std::size_t>(
            std::count(result.kept.begin(), result.kept.end(), true));
    };
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        note("cannot open the file for writing (" + std::string(std::strerror(errno)) + ")");
        finish();
        return result;
    }
    int write_errno = 0;
    const auto write_all = [fd, &write_errno](const std::string& bytes) {
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                write_errno = errno;
                return false;
            }
            done += static_cast<std::size_t>(n);
        }
        return true;
    };
    bool ok = ::ftruncate(fd, static_cast<off_t>(keep)) == 0 &&
              ::lseek(fd, static_cast<off_t>(keep), SEEK_SET) == static_cast<off_t>(keep);
    if (!ok) {
        write_errno = errno;
    }
    if (ok && keep == 0) {
        ok = write_all(encode_journal_header());
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (framed[i].empty()) {
            note("a record is over the size cap");
            continue;
        }
        if (!ok) {
            break;
        }
        ok = write_all(framed[i]);
        wrote[i] = ok;
    }
    if (!ok) {
        note("cannot write the file (" + std::string(std::strerror(write_errno)) + ")");
    }
    // What was written before a failure counts once it is on disk.
    const bool synced = ::fsync(fd) == 0;
    const int sync_errno = errno;
    ::close(fd);
    if (!synced) {
        wrote.assign(wrote.size(), false);
        note("cannot sync the file (" + std::string(std::strerror(sync_errno)) + ")");
        finish();
        return result;
    }
    // The new file's name must reach the disk too.
    const int dir = ::open(path.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
    if (dir >= 0) {
        (void)::fsync(dir);
        ::close(dir);
    }
    finish();
    return result;
}

} // namespace etude::app
