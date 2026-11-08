// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_schema.hpp"

#include <array>

namespace etude::app {

namespace {

// Version 1.
//
// How the tables relate:
//
//   profiles 1 --- n runs n --- 1 songs
//                    |
//                    +--- n run_measures   (the per-bar stats of a run)
//                    +--- 0..1 run_event_sets (migration 9: the played notes of a run, kept
//                                 for the newest runs of a song and the pinned ones)
//                                   +--- n run_events
//
// Rows are never updated after they are written, except songs (its path, title and last_seen move
// forward). `runs.id` counts up in the order runs were saved: that order, not the wall times, is
// the history's order (a Pi without a battery clock can start at a wrong date).
//
// The run columns after song_title are exactly core's kRunColumnNames, in that order (a test
// compares them), so RunRecord <-> row is one table-driven mapping.
constexpr std::string_view kVersion1 = R"sql(
-- Who practises. The default profile exists from the start so every run has an owner; managing
-- more profiles (names, colours, switching) is a later step that only adds rows and columns.
CREATE TABLE profiles (
    id            INTEGER PRIMARY KEY,
    name          TEXT    NOT NULL,
    is_default    INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1)),
    created_utc_s INTEGER NOT NULL
);
CREATE UNIQUE INDEX profiles_one_default ON profiles (is_default) WHERE is_default = 1;
INSERT INTO profiles (id, name, is_default, created_utc_s)
    VALUES (1, 'Player', 1, CAST(strftime('%s', 'now') AS INTEGER));

-- Which pieces have been practised. song_key is the identity (core song_key.hpp) and it is the
-- file's content hash alone: identical files share one history, a renamed or moved file keeps its
-- history, an edited file is a new song. path is metadata, the latest place the file was seen.
-- content_hash holds the bit pattern of the unsigned 64-bit hash.
CREATE TABLE songs (
    id               INTEGER PRIMARY KEY,
    song_key         TEXT    NOT NULL UNIQUE,
    content_hash     INTEGER NOT NULL,
    path             TEXT    NOT NULL DEFAULT '',
    title            TEXT    NOT NULL DEFAULT '',
    first_seen_utc_s INTEGER NOT NULL,
    last_seen_utc_s  INTEGER NOT NULL
);
CREATE INDEX songs_by_content_hash ON songs (content_hash);  -- unused; dropped by migration 12

-- One finished practice run: everything in core's RunRecord, flat. song_id is null for a run whose
-- song has neither a hash nor a path. song_hash is kept on the run too, so the record round-trips
-- whole. mode: 0 Flow, 1 Wait. hands: 0 Both, 1 Left, 2 Right.
CREATE TABLE runs (
    id                  INTEGER PRIMARY KEY,
    profile_id          INTEGER NOT NULL REFERENCES profiles (id),
    song_id             INTEGER REFERENCES songs (id),
    song_title          TEXT    NOT NULL,
    song_hash           INTEGER NOT NULL,
    mode                INTEGER NOT NULL CHECK (mode IN (0, 1)),
    hands               INTEGER NOT NULL CHECK (hands IN (0, 1, 2)),
    speed_percent       INTEGER NOT NULL,
    transpose_semitones INTEGER NOT NULL,
    start_utc_s         INTEGER NOT NULL,
    end_utc_s           INTEGER NOT NULL,
    local_day           INTEGER NOT NULL,
    duration_ms         INTEGER NOT NULL,
    perfect             INTEGER NOT NULL,
    good                INTEGER NOT NULL,
    early               INTEGER NOT NULL,
    late                INTEGER NOT NULL,
    missed              INTEGER NOT NULL,
    played              INTEGER NOT NULL,
    wrong               INTEGER NOT NULL,
    unplayable          INTEGER NOT NULL,
    longest_combo       INTEGER NOT NULL,
    clean_passes        INTEGER NOT NULL,
    left_graded         INTEGER NOT NULL,
    left_hit            INTEGER NOT NULL,
    right_graded        INTEGER NOT NULL,
    right_hit           INTEGER NOT NULL,
    unknown_graded      INTEGER NOT NULL,
    unknown_hit         INTEGER NOT NULL,
    all_graded          INTEGER NOT NULL,
    all_hit             INTEGER NOT NULL
);
-- The same run saved twice (an import repeated, a write retried) is stored once: the fields are
-- core's same_run(), so the database and the progress functions agree on what a repeat is.
CREATE UNIQUE INDEX runs_same_run ON runs
    (profile_id, song_hash, start_utc_s, mode, hands, speed_percent, transpose_semitones);
-- A song's history, in saved order, for the trouble-spot map and per-song bests.
CREATE INDEX runs_by_song ON runs (profile_id, song_id, id);  -- unused; dropped by migration 12

-- The trouble-spot map of a run: one row per bar that had a graded note or a wrong press.
CREATE TABLE run_measures (
    run_id  INTEGER NOT NULL REFERENCES runs (id) ON DELETE CASCADE,
    measure INTEGER NOT NULL,
    graded  INTEGER NOT NULL,
    missed  INTEGER NOT NULL,
    wrong   INTEGER NOT NULL,
    PRIMARY KEY (run_id, measure)
) WITHOUT ROWID;

-- Reserved for recording and replaying a run: the notes the player played, in order, with the
-- hand and the note they matched. Nothing writes it yet; a later version may change its columns
-- in a migration, as long as it is still empty.
CREATE TABLE run_events (
    run_id     INTEGER NOT NULL REFERENCES runs (id) ON DELETE CASCADE,
    seq        INTEGER NOT NULL,
    t_ms       INTEGER NOT NULL,  -- from the run's start, on its monotonic clock
    kind       INTEGER NOT NULL,  -- 0 note on, 1 note off, 2 pedal
    key        INTEGER NOT NULL,  -- MIDI note number
    velocity   INTEGER NOT NULL,
    hand       INTEGER,           -- the hand of the song note it matched, null when none
    note_index INTEGER,           -- the song note it matched, null when none
    grade      INTEGER,           -- the grade it earned, null when none
    PRIMARY KEY (run_id, seq)
) WITHOUT ROWID;
)sql";

// Version 2: what the player set for a song in the library, by the song's content hash (as
// songs.content_hash, the bit pattern of the unsigned 64-bit hash), never written into the file:
// their name for it, its composer and genre (empty: the file's own), the favourite star, and when
// the library first listed it (0: not yet). One row per profile and song. Later per-song settings
// (speed, count-in, hands) add columns here in migrations of their own.
constexpr std::string_view kVersion2 = R"sql(
CREATE TABLE song_settings (
    profile_id   INTEGER NOT NULL REFERENCES profiles (id),
    content_hash INTEGER NOT NULL,
    title        TEXT    NOT NULL DEFAULT '',
    composer     TEXT    NOT NULL DEFAULT '',
    genre        TEXT    NOT NULL DEFAULT '',
    favourite    INTEGER NOT NULL DEFAULT 0 CHECK (favourite IN (0, 1)),
    added_utc_s  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (profile_id, content_hash)
) WITHOUT ROWID;
)sql";

// Version 3: the player's own groups of songs and what is in them. A group belongs to a profile;
// `kind` 0 is the player's, 1 a catalogue's (downloads land in one, `source` naming the
// catalogue). Songs are held by content hash (as in song_settings), one row per group and song,
// so a song can be in several groups and keeps them when its file moves.
constexpr std::string_view kVersion3 = R"sql(
CREATE TABLE song_groups (
    profile_id    INTEGER NOT NULL REFERENCES profiles (id),
    group_id      INTEGER NOT NULL,
    name          TEXT    NOT NULL DEFAULT '',
    kind          INTEGER NOT NULL DEFAULT 0 CHECK (kind IN (0, 1)),
    source        TEXT    NOT NULL DEFAULT '',
    position      INTEGER NOT NULL DEFAULT 0,
    created_utc_s INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (profile_id, group_id)
) WITHOUT ROWID;
CREATE TABLE song_group_members (
    profile_id   INTEGER NOT NULL REFERENCES profiles (id),
    group_id     INTEGER NOT NULL,
    content_hash INTEGER NOT NULL,
    added_utc_s  INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (profile_id, group_id, content_hash)
) WITHOUT ROWID;
CREATE INDEX song_group_members_by_song ON song_group_members (profile_id, content_hash);  -- unused; dropped by migration 12
)sql";

// Version 4: where a song downloaded from a free catalogue came from, by its content hash (as in
// song_settings), whoever plays it: the catalogue, the song's id there, its licence and the credit
// it asks for, and the file's address, so the credit shows wherever the file goes.
constexpr std::string_view kVersion4 = R"sql(
CREATE TABLE song_sources (
    content_hash     INTEGER NOT NULL PRIMARY KEY,
    catalogue        TEXT    NOT NULL DEFAULT '',
    catalogue_id     TEXT    NOT NULL DEFAULT '',
    title            TEXT    NOT NULL DEFAULT '',
    composer         TEXT    NOT NULL DEFAULT '',
    licence          TEXT    NOT NULL DEFAULT '',
    licence_url      TEXT    NOT NULL DEFAULT '',
    credit           TEXT    NOT NULL DEFAULT '',
    source_url       TEXT    NOT NULL DEFAULT '',
    page_url         TEXT    NOT NULL DEFAULT '',
    downloaded_utc_s INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;
)sql";

// Version 5: when the player last chose a song as the version of its piece to open (the library
// shows several versions of one piece as one card; the one chosen last is the one the card opens).
// By content hash like the rest of song_settings; 0 is never chosen.
constexpr std::string_view kVersion5 = R"sql(
ALTER TABLE song_settings ADD COLUMN chosen_utc_s INTEGER NOT NULL DEFAULT 0;
)sql";

// Version 6: what the player settled by hand about which songs are versions of one piece, by content
// hash (as in song_settings). A row with hash_b 0 takes hash_a out of its piece; a row with a
// hash_b joins the two (hash_a below hash_b).
constexpr std::string_view kVersion6 = R"sql(
CREATE TABLE version_overrides (
    profile_id INTEGER NOT NULL REFERENCES profiles (id),
    hash_a     INTEGER NOT NULL,
    hash_b     INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (profile_id, hash_a, hash_b)
) WITHOUT ROWID;
)sql";

// Version 7: the practice choices of a song, by content hash (as in song_settings): the speed, the
// bars of count-in, the hand(s) and the transposition, so opening a song brings back what the player
// last used on it and the last-song session is only where they left off. hands: 0 Both, 1 Left,
// 2 Right. One row per profile and song, written whole when any of them changes. Later per-song
// choices (the loop and section, the accompaniment per part) add columns or tables in migrations of
// their own.
constexpr std::string_view kVersion7 = R"sql(
CREATE TABLE song_practice (
    profile_id          INTEGER NOT NULL REFERENCES profiles (id),
    content_hash        INTEGER NOT NULL,
    speed_percent       INTEGER NOT NULL DEFAULT 100,
    count_in_bars       INTEGER NOT NULL DEFAULT 1,
    hands               INTEGER NOT NULL DEFAULT 0 CHECK (hands IN (0, 1, 2)),
    transpose_semitones INTEGER NOT NULL DEFAULT 0,
    updated_utc_s       INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (profile_id, content_hash)
) WITHOUT ROWID;
)sql";

// Version 8: the rest of a song's practice choices. The loop and where it came from (the section
// looped and the practice list item running, as the session keeps them) are columns of
// song_practice; which accompaniment parts the player muted is a row per muted part in
// song_practice_parts, by part key (track << 8 | channel). parts_set 0: the player never chose, so
// the global default decides.
constexpr std::string_view kVersion8 = R"sql(
ALTER TABLE song_practice ADD COLUMN loop_start_ms INTEGER NOT NULL DEFAULT 0;
ALTER TABLE song_practice ADD COLUMN loop_end_ms   INTEGER NOT NULL DEFAULT 0;
ALTER TABLE song_practice ADD COLUMN loop_enabled  INTEGER NOT NULL DEFAULT 0;
ALTER TABLE song_practice ADD COLUMN section_id    INTEGER NOT NULL DEFAULT 0;
ALTER TABLE song_practice ADD COLUMN plan_item     INTEGER NOT NULL DEFAULT 0;
ALTER TABLE song_practice ADD COLUMN parts_set     INTEGER NOT NULL DEFAULT 0;
CREATE TABLE song_practice_parts (
    profile_id   INTEGER NOT NULL REFERENCES profiles (id),
    content_hash INTEGER NOT NULL,
    part_key     INTEGER NOT NULL,
    PRIMARY KEY (profile_id, content_hash, part_key)
) WITHOUT ROWID;
)sql";

// Version 9: what the player played in a run, note by note (core run_events.hpp). Version 1 reserved
// a run_events table for this and said a later version may change it while it is empty; nothing ever
// wrote it, so it is dropped and made again with the columns the recorder produces.
//
// run_event_sets is one row per run that has events: how many, how many notes the run was expected
// to grade, how many the recorder had no room for (`dropped`), whether it is a complete pass over
// the song (the ghost's rule needs it) and whether the player pinned it. A run's events are deleted
// by deleting its row here (ON DELETE CASCADE), which keeps the run itself and its summary: the
// store keeps the newest few runs' events per song and the pinned ones. Deleting a run deletes its
// events the same way.
//
// run_events: one row per event in song-time order (`seq`). kind: 0 a press that graded a note, 1 a
// press that matched no note, 2 a note nobody played. Times are milliseconds; song_ms is the song
// clock's, error_ms wall time and signed (negative early). note_index, hand (0 unknown, 1 left, 2
// right) and grade (core NoteGrade) are null for a press that matched no note.
constexpr std::string_view kVersion9 = R"sql(
DROP TABLE IF EXISTS run_events;
DROP TABLE IF EXISTS run_event_sets;
CREATE TABLE run_event_sets (
    run_id         INTEGER NOT NULL PRIMARY KEY REFERENCES runs (id) ON DELETE CASCADE,
    event_count    INTEGER NOT NULL,
    expected_notes INTEGER NOT NULL,
    dropped        INTEGER NOT NULL DEFAULT 0,
    complete       INTEGER NOT NULL CHECK (complete IN (0, 1)),
    pinned         INTEGER NOT NULL DEFAULT 0 CHECK (pinned IN (0, 1))
);
CREATE TABLE run_events (
    run_id     INTEGER NOT NULL REFERENCES run_event_sets (run_id) ON DELETE CASCADE,
    seq        INTEGER NOT NULL,
    kind       INTEGER NOT NULL CHECK (kind IN (0, 1, 2)),
    song_ms    INTEGER NOT NULL,
    error_ms   INTEGER NOT NULL,
    key        INTEGER NOT NULL,
    velocity   INTEGER NOT NULL,
    note_index INTEGER,
    hand       INTEGER,
    grade      INTEGER,
    PRIMARY KEY (run_id, seq)
) WITHOUT ROWID;
)sql";

// Version 10: the octave-shift offer a player turned off for a song ("Don't ask again for this
// song"), a column of song_practice like the rest of its choices.
constexpr std::string_view kVersion10 = R"sql(
ALTER TABLE song_practice ADD COLUMN no_shift_offer INTEGER NOT NULL DEFAULT 0
    CHECK (no_shift_offer IN (0, 1));
)sql";

// Version 11: how the player split a song's notes between the hands. 0: Auto (the smart split);
// 1 to 127: the pitch, in the song's own pitches, from which notes are the right hand.
constexpr std::string_view kVersion11 = R"sql(
ALTER TABLE song_practice ADD COLUMN split_point INTEGER NOT NULL DEFAULT 0;
)sql";

// Version 12: three indexes nothing reads, each costing a write per run or per edit. Every query
// filters runs by (profile_id, song_hash, ...) (the runs_same_run index), songs by the unique
// song_key, and group members by the primary key; none filters songs by content_hash, runs by
// song_id or group members by hash alone, and no row of songs, profiles or groups is ever deleted
// under a foreign key that would need them. IF EXISTS: a file restored from a dump may lack them.
constexpr std::string_view kVersion12 = R"sql(
DROP INDEX IF EXISTS songs_by_content_hash;
DROP INDEX IF EXISTS runs_by_song;
DROP INDEX IF EXISTS song_group_members_by_song;
)sql";

// Version 13: player profiles (#79). The `profiles` table and the profile_id of runs, a song's
// practice choices (song_practice, song_practice_parts) and the library's rows have been there since
// version 1, every row holding the default profile (id 1), so the migration moves nothing: every
// existing run and choice stays with the default profile, which every start has. What it adds is
// what a profile needs besides its name:
//   - profiles.colour: 0xRRGGBB as an integer. The default profile gets the first colour of core's
//     palette (0x4C9BE8, 5020648).
//   - profile_settings: one row per profile that saved settings, the text of the person's part of
//     settings.conf (core profile_settings.hpp); no row is a profile that never set anything.
//   - runs.score and runs.covered_percent (core run_score.hpp), the last two columns of core's
//     kRunColumnNames. A run from before them holds 0 in both: its points are worked out from the
//     rest of its record when a board needs them.
constexpr std::string_view kVersion13 = R"sql(
ALTER TABLE profiles ADD COLUMN colour INTEGER NOT NULL DEFAULT 5020648;
CREATE TABLE profile_settings (
    profile_id    INTEGER NOT NULL PRIMARY KEY REFERENCES profiles (id),
    settings      TEXT    NOT NULL DEFAULT '',
    updated_utc_s INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;
ALTER TABLE runs ADD COLUMN score INTEGER NOT NULL DEFAULT 0;
ALTER TABLE runs ADD COLUMN covered_percent INTEGER NOT NULL DEFAULT 0;
)sql";

constexpr std::array<Migration, 13> kMigrations{{
    {1, kVersion1},
    {2, kVersion2},
    {3, kVersion3},
    {4, kVersion4},
    {5, kVersion5},
    {6, kVersion6},
    {7, kVersion7},
    {8, kVersion8},
    {9, kVersion9},
    {10, kVersion10},
    {11, kVersion11},
    {12, kVersion12},
    {13, kVersion13},
}};

} // namespace

std::span<const Migration> history_migrations() { return kMigrations; }

} // namespace etude::app
