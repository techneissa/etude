// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_journal.hpp"

#include "history_journal_codec.hpp"

#include <cmath>
#include <type_traits>
#include <variant>

namespace etude::app {

using namespace journal_codec;

namespace {

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

} // namespace etude::app
