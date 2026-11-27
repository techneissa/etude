// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_journal.hpp"

#include "history_journal_codec.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace etude::app {

using namespace journal_codec;

namespace {

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

} // namespace

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

} // namespace etude::app
