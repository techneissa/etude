// SPDX-License-Identifier: GPL-3.0-or-later
// The journal: what the history thread could not write to history.db when the app quit. A full or
// read-only disk keeps the finished runs, the songs' practice choices and the library's own
// changes in memory and tries again every few seconds; if the app quits first, those jobs go here,
// in a small file beside the database, and the next start writes them into the database and deletes
// the file.
//
// It is read without the database, so it works exactly when the database does not. Plain bytes, no
// SQLite, no file-system call outside append_journal().
//
// Layout of history.journal (all integers little-endian; a "varint" is LEB128, a "zigzag" the
// varint of (n << 1) ^ (n >> 63)):
//   "etude journal 1\n"                   the header; the number is the layout's version
//   per record:
//     length    4 bytes                     the payload's size, 1 .. kMaxJournalRecordBytes
//     check     4 bytes                     the low 32 bits of content_hash() of the payload
//     payload   `length` bytes
//   payload, first byte the kind:
//     'R' a finished run:  varint n, then n bytes of serialize_run_record(); varint n, then the
//         n bytes of the song's path; varint event count; zigzag dropped; zigzag expected_notes;
//         1 byte complete; per event: zigzag song_ms, zigzag error_ms, zigzag note_index, then one
//         byte each for kind, key, velocity, hand and grade; then (#79, left off by an older
//         writer: the default profile, no score) zigzag profile id, zigzag score, zigzag
//         covered_percent
//     'P' a song's practice choices:  8 bytes content hash; zigzag speed_percent; zigzag
//         count_in_bars; 1 byte hands; zigzag transpose_semitones; 1 byte parts_set; varint count,
//         then that many varint part keys; zigzag loop start and end in milliseconds; 1 byte
//         loop_enabled; zigzag section_id; zigzag plan_item; 1 byte no_shift_offer; zigzag
//         split_point (kAutoSplit when the record ends before it); zigzag at_utc_s; zigzag profile
//         id (#79; left off by an older writer: the default profile)
//     'S' song settings (a name, composer, genre, star): varint count, then per song 8 bytes content
//         hash; three texts (varint n, n bytes: title, composer, genre); 1 byte favourite; zigzag
//         added_utc_s; zigzag chosen_utc_s
//     'G' group edits: varint count, then per edit 1 byte kind (GroupEdit::Kind), 8 bytes group id,
//         text name, 1 byte group kind (GroupKind), text source, zigzag position, zigzag
//         created_utc_s, 8 bytes song hash, zigzag at_utc_s
//     'O' song sources: varint count, then per song 8 bytes content hash; nine texts (catalogue,
//         id, title, composer, licence, licence_url, credit, url, page); zigzag downloaded_utc_s
//     'V' version overrides: varint count, then per row 8 bytes hash a, 8 bytes hash b, 1 byte add
//     'N' a pin: zigzag run id (the database's own row id, so it fits the database it was made
//         for), 1 byte pinned; zigzag profile id (#79; left off by an older writer: the default
//         profile)
//     'F' profile rows (#79): varint count, then per profile zigzag id, text name, zigzag colour,
//         zigzag created_utc_s
//     'T' a profile's settings (#79): zigzag profile id, text (the settings text, cut to
//         kMaxJournalSettingsBytes), zigzag updated_utc_s
//   Kinds 'S' to 'N' came after 'R' and 'P' (#269) with no new version: a journal of only 'R' and
//   'P' is read as before, and a build that does not know a kind skips that record as damaged
//   (its neighbours are kept), then deletes the file once the others are in, so it loses the
//   library changes it could not read and corrupts nothing.
//   The at_utc_s of 'P' is when the journal was written (0 or left off: not known). Bytes after the fields a build knows are ignored, and a field a build knows that an
//   older writer left off takes its default, so a field can be added at the end without a new
//   version.
//
// Reading never fails: a record whose check or contents are wrong is skipped and counted; input
// that ends inside a record (a torn last write) keeps everything before it; a header that is not
// ours means it is not a journal; a header of a newer version leaves the file for that build.
// Every record is applied in order, and applying one twice is harmless (the run is skipped when
// the database has it, a song's choices, settings and sources are replaced whole, an edit of the
// groups or the overrides is an insert that keeps what is there or a delete or update of a row).
#pragma once

#include "etude/library_groups.hpp"
#include "etude/profile.hpp"
#include "etude/run_events.hpp"
#include "etude/run_record.hpp"
#include "etude/song_catalogue.hpp"
#include "etude/song_practice.hpp"
#include "etude/song_prefs.hpp"
#include "etude/song_versions.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace etude::app {

inline constexpr std::string_view kJournalFileName = "history.journal";
inline constexpr std::string_view kJournalHeaderPrefix = "etude journal ";
inline constexpr int kJournalVersion = 1;
inline constexpr std::size_t kMaxJournalRecordBytes = 8u << 20;
// The longest text of a song source or a group's source written to the journal, in bytes (cut at a
// character boundary): with kMaxJournalItems of them a record stays under kMaxJournalRecordBytes.
inline constexpr std::size_t kMaxJournalText = 4096;
// The longest settings text of a profile a record holds (a settings.conf is at most 1 MiB).
inline constexpr std::size_t kMaxJournalSettingsBytes = 1u << 20;

struct JournalRun {
    RunRecord record;
    std::string song_path;
    RunEvents events;
    std::int64_t profile_id = kDefaultProfileId; // whose run it is
};

struct JournalPractice {
    std::uint64_t hash = 0;
    SongPractice practice;
    // When the journal was written (UTC seconds; 0: not known). A replay leaves a song's row alone
    // when the database changed it after this moment, so a journal that is replayed a second time
    // never puts old choices over newer ones.
    std::int64_t at_utc_s = 0;
    std::int64_t profile_id = kDefaultProfileId; // whose choices they are
};

// One job of the library's own changes, as the history thread queued it (one transaction).
struct JournalPrefs {
    std::vector<std::pair<std::uint64_t, SongPrefs>> items;
};
struct JournalGroups {
    std::vector<GroupEdit> edits;
};
struct JournalOrigins {
    std::vector<std::pair<std::uint64_t, SongOrigin>> items;
};
struct JournalVersions {
    std::vector<VersionRow> rows;
};
// Profiles made or changed (id, name, colour, created).
struct JournalProfiles {
    std::vector<Profile> rows;
};
struct JournalProfileSettings {
    std::int64_t profile_id = kDefaultProfileId;
    std::string text;
    std::int64_t updated_utc_s = 0;
};
// A profile deleted with all of its own rows.
struct JournalProfileDelete {
    std::int64_t profile_id = 0;
};
struct JournalPin {
    std::int64_t run_id = 0;
    bool pinned = false;
    std::int64_t profile_id = kDefaultProfileId; // whose run it is
};

// The kinds only ever grow at the end: the order here is the order of the letters in the file.
using JournalEntry = std::variant<JournalRun, JournalPractice, JournalPrefs, JournalGroups,
                                  JournalOrigins, JournalVersions, JournalPin, JournalProfiles,
                                  JournalProfileSettings, JournalProfileDelete>;

// What a record of the journal holds, to count what was not kept.
struct JournalHeld {
    std::size_t runs = 0;
    std::size_t practice = 0;
    std::size_t library = 0;
};

// The most items one library record holds; a longer job is written as several records, so a record
// stays far below kMaxJournalRecordBytes.
inline constexpr std::size_t kMaxJournalItems = 200;

struct JournalDecode {
    std::vector<JournalEntry> entries; // in file order
    int damaged = 0;                   // records skipped: bad check, bad contents
    bool truncated = false;            // the file ends inside a record (a torn last write)
    bool framing_damaged = false;      // a record's length is impossible: the bytes from
                                       // `valid_end` on cannot be told apart, and are not read
    bool bad_header = false;           // not a journal at all
    int newer_version = 0;             // the version a newer build wrote (entries stay empty)
    std::size_t valid_end = 0;         // the offset after the last whole record
};

// The file's first bytes, and one framed record.
std::string encode_journal_header();
// The record's bytes, or nothing when its payload is over kMaxJournalRecordBytes (a reader would
// take it for damage in the framing, and set aside every record after it).
std::string encode_journal_entry(const JournalEntry& entry);
JournalDecode decode_journal(std::string_view bytes);

struct JournalAppend {
    std::size_t saved = 0;  // the entries that are on disk and synced
    std::vector<bool> kept; // per entry: whether it is one of them
    std::string why;        // empty when all were kept, else the first reason one was not
};

// Adds `entries` to the journal at `path` (made when it is not there), then syncs the file and its
// folder. When a write fails, the entries written before it are synced and counted as kept; the
// one it broke is cut off by the next append. An entry that is over the size cap is left out and
// the others are written. A torn tail left by an earlier write is cut off first. A file that is not a journal, or
// is a newer one, is left as it is and nothing is written. A file with a damaged length in the
// middle is moved to `path`.rejected (with the bytes after the damage) and the records read before
// it start the new file.
JournalAppend append_journal(const std::filesystem::path& path,
                             std::span<const JournalEntry> entries);

} // namespace etude::app
