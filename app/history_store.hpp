// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history on disk: one SQLite database, history.db, beside settings.conf. The schema
// is in history_schema.cpp; core's RunRecord is the value that crosses the boundary (core never
// sees SQLite).
//
// Threads. One history thread does all the database work: it opens the file, migrates it, moves
// runs.dat in, reads the runs, and then saves each finished run in one short transaction. The
// frame thread never waits for it: open() only starts the thread, add_run() copies the run into
// the in-memory list (so the progress functions see it at once) and queues it, and poll() hands the
// loaded runs over once they are ready. runs(), add_run() and poll() belong to the frame thread.
// Nothing here is called from the audio or MIDI threads.
//
// Profiles (#79). Every profile's runs, practice choices and settings are read at start; runs() and
// song_practice() are the active profile's, and switch_profile() swaps them in memory. Each job
// carries the profile it was made for, so a switch never moves a run to the wrong person.
//
// One etude per history. A lock file, history.lock, is held (flock, released when the process
// exits) for as long as the store lives. A second etude on the same folder cannot get it: it
// reads the history without changing anything and keeps its new runs in memory only.
//
// Damage and everything else. Only damage (corrupt, not a SQLite file, a failed integrity check)
// moves a database aside: whole, with its WAL files, as history.db.damaged (then .damaged.1 .. .7;
// an existing copy is never overwritten, and if any of the renames fails the ones done are undone
// and nothing is deleted). That also happens when a write finds the open database corrupt: the
// held run and the queued ones go to the new file. Anything else that goes wrong (busy or locked,
// a full or read-only disk, an I/O error, a file that is not ours) keeps the runs in memory; the
// history thread logs one line, tries again every few seconds, and when the database opens it
// saves what it held. When the store is closing it makes one last attempt; what is still held then
// (finished runs, songs' practice choices, the library's own changes and pins) goes to
// history.journal beside the database (history_journal.hpp) and is written into it at the next
// start; if even that fails the loss is reported by take_errors(). A database from a newer etude
// is read only (to see its version) and otherwise left byte for byte as it is. On the first start
// with the database the old store, runs.dat, is read once into it and renamed runs.dat.bak.
#pragma once

#include "history_journal.hpp"
#include "history_schema.hpp"
#include "history_store_progress.hpp"
#include "loop_wake.hpp"
#include "sqlite_db.hpp"
#include "etude/ghost_run.hpp"
#include "etude/run_events.hpp"
#include "etude/run_record.hpp"
#include "etude/run_summary.hpp"
#include "etude/leaderboard.hpp"
#include "etude/library_groups.hpp"
#include "etude/profile.hpp"
#include "etude/song_catalogue.hpp"
#include "etude/song_practice.hpp"
#include "etude/song_prefs.hpp"
#include "etude/song_versions.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace etude::app {

inline constexpr std::string_view kHistoryFileName = "history.db";
inline constexpr std::string_view kHistoryLockName = "history.lock";

struct HistoryOptions {
    // The migrations to apply; empty means history_migrations(). Tests give their own to try a
    // schema version bump.
    std::span<const Migration> migrations;
    // How long one attempt waits for another connection's lock while opening, before the
    // database counts as busy.
    int open_busy_timeout_ms = 250;
    // How long one write waits for a lock before the run is held back.
    int write_busy_timeout_ms = 1000;
    // How long the history thread waits before it tries a busy database again.
    int retry_interval_ms = 3000;
    // One line per notable event (a database set aside, runs imported, a database busy). Called
    // on the history thread. Null prints to stderr.
    std::function<void(std::string_view)> log;
    // How the journal is removed once it is replayed, and renamed aside (history.journal.done,
    // .rejected). Null uses the file system; tests give ones that fail. Called on the history
    // thread.
    std::function<bool(const std::filesystem::path&)> remove_file;
    std::function<bool(const std::filesystem::path&, const std::filesystem::path&)> rename_file;
    // Empties a journal that can be neither removed nor renamed, so it is not replayed again.
    std::function<bool(const std::filesystem::path&)> truncate_file;
    // The bytes free for this process on the disk holding a path (null: the file system's own
    // answer; nullopt: not known). The one-time VACUUM that gives an old file incremental mode
    // needs about the file's size free beside it and in the temp folder; tests give small answers.
    std::function<std::optional<std::uintmax_t>(const std::filesystem::path&)> free_space;
    // An old file (auto_vacuum NONE) is rewritten once, only when its free pages are over both
    // of these (the larger share of the file and the larger amount).
    std::uint64_t compact_min_free_bytes = 8ull * 1024 * 1024;
    int compact_min_free_percent = 25;
    // Called on the history thread from inside the one-time VACUUM, about every thousand steps of
    // it. Null in the app; tests give one that waits, to quit or add a run while the VACUUM runs.
    std::function<void()> compacting;
    // The profile in use from the start (the app passes the one it saved). A profile the database
    // does not hold falls back to the default one when the history is loaded.
    std::int64_t profile_id = kDefaultProfileId;
};

enum class HistoryState : std::uint8_t {
    Loading,  // the history thread is opening the database and reading the runs
    Ready,    // loaded; each run is saved as it ends
    Waiting,  // the database is busy or locked: runs are held in memory, retried every few seconds
    ReadOnly, // another etude has the history: this one reads it and saves nothing
    Unavailable, // no usable database this session (from a newer version, cannot be opened)
};

struct HistoryStats {
    int writes = 0;       // runs saved, repeats included
    int failures = 0;     // runs that could not be saved
    double last_ms = 0.0; // the last write's transaction
    double max_ms = 0.0;
    double total_ms = 0.0;
    double load_ms =
        0.0; // opening, migrating, moving runs.dat in and reading, on the history thread
    int loaded_runs = 0;
};

// One run's events as stored, with what the ghost needs to know about the run.
struct StoredRunEvents {
    std::int64_t run_id = 0;
    std::uint64_t song_hash = 0; // of the song the run was played on
    GhostSetup setup;
    RunHand all; // the run's graded and hit notes
    bool pinned = false;
    RunEvents events;
};

enum class EventsReadKind : std::uint8_t { Run, Ghost, Pin, Replay };

// The answer to request_run_events() or request_ghost(), by the serial the request returned. Every
// request is answered: `found` is empty when there is no such run, no comparable run, or the
// database could not be read (or the store closed first).
//
// `song_hash` is the song the answer is about: the one a ghost request asked for, or the one the
// run found belongs to (0 when none was found). An answer comes back some frames after the request,
// by which time the player may have opened another song: a caller must check it with
// ghost_answer_applies() before it uses the events, and drop it otherwise.
struct EventsRead {
    std::uint64_t serial = 0;
    EventsReadKind kind = EventsReadKind::Run;
    std::uint64_t song_hash = 0;
    std::optional<StoredRunEvents> found;
    // For a Pin: the run had played notes, so the pin (or its release) took. False when the run's
    // notes are gone (pruned, or the run never had any), the run is not in the database, or the
    // pin could not be written; `found` is always empty for a Pin.
    bool pin_took = false;
    // For a Replay: the run's timeline and summary, finished on the history thread. A run whose
    // played notes are not kept (pruned, never kept, not in the database) has an empty timeline and
    // the summary of its record (`SummaryDetail::RecordOnly`). Null only when the store closed
    // before it could answer. `found` is empty for it.
    std::shared_ptr<const RunReplay> replay{};
    // For a Replay: the database's id of the run the answer is about, which the frame needs to pin
    // it (`set_run_pinned`); 0 when the database has no such run.
    std::int64_t run_id = 0;
    // For a Replay: the run is pinned in the database, so the chip opens as it was left.
    bool pinned = false;
};

// What a replay needs besides the stored notes: the run's own record and the song it was played
// on, as the evaluators' shared read-only tables (no copy of the song). The tables are the song's
// notes and bars at the time of the request; they stay alive while the request does.
struct ReplayRequest {
    RunRecord record;
    std::shared_ptr<const std::vector<Note>> notes;
    std::shared_ptr<const std::vector<Measure>> measures;
    // Whose run it is; 0 is the active profile when the request is made. A run the database has to
    // find by its record (run id 0) is looked up under this profile.
    std::int64_t profile_id = 0;
};

// Whether `answer` is the answer to the ghost request `serial` made for the song `song_hash`, the
// song open now. False for an answer to an older request, to a run request, or about another song
// (a ghost for song A is never drawn over song B). A song without a hash has no ghost.
inline bool ghost_answer_applies(const EventsRead& answer, std::uint64_t serial,
                                 std::uint64_t song_hash) {
    return song_hash != 0 && answer.kind == EventsReadKind::Ghost && answer.serial == serial &&
           answer.song_hash == song_hash;
}

// A deletion the history thread finished (or could not make), reported by poll().
struct ProfileDeleted {
    std::int64_t id = 0;
    std::string name;       // as it was
    bool done = false;      // false: nothing changed, the profile is still there
    bool switched = false;  // it was in use: `active` took over, and the app applies it
    std::int64_t active = 0; // the profile in use afterwards
};

struct HistoryPoll {
    bool loaded = false; // the runs from the database were just added to runs()
    // The load found the profile asked for gone and fell back to the default profile: the app
    // applies that profile's settings (profile_settings()) as it does after a switch.
    bool profile_changed = false;
    std::string message; // a sentence for the status row; empty when there is none
    std::vector<ProfileDeleted> deleted; // deletions that came to an end since the last poll
};

// One stored run with the profile it belongs to (the history thread loads every profile's runs).
struct ProfiledRun {
    std::int64_t profile_id = kDefaultProfileId;
    RunRecord run;
};

class HistoryStore {
  public:
    explicit HistoryStore(std::filesystem::path directory, HistoryOptions options = {});
    // Saves what is queued (a busy database gets one last try), then closes.
    ~HistoryStore();

    // The same as the destructor, now: stops the history thread, which saves what is queued, keeps
    // what it cannot save in the journal, and reports what even that could not keep; then the
    // database is closed. After it, take_errors() has the losses, and the store takes no more
    // work. Safe to call twice. Takes as long as the last attempt: one open, no retries, and one
    // file append.
    void close();
    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    // Starts the history thread and returns at once; no file is touched on the calling thread.
    // Call once, first.
    void open();

    // Once per frame: adds the runs read from the database to runs() when they are ready, and
    // returns what the player should be told. Takes a mutex the history thread only holds for a few
    // instructions.
    HistoryPoll poll();

    // True from open() until the runs from the database are in runs(): progress built on them is
    // not complete yet. (Also true while a busy database has not been opened at all.)
    bool loading() const { return !loaded_; }
    HistoryState state() const { return state_.load(); }
    // Waiting because the file or its folder cannot be written by this program (not because the
    // database is busy): saving waits for the player or the system to fix that.
    bool waiting_unwritable() const { return unwritable_.load(); }

    // The active profile's runs, oldest first in the order they were saved: the list the progress
    // functions take. Until poll() reports `loaded`, only this session's runs are in it. Frame
    // thread only.
    const std::vector<RunRecord>& runs() const { return runs_; }

    // ---- Profiles (#79; core profile.hpp, profile_settings.hpp). Frame thread only.
    //
    // Every profile's runs, practice choices and settings are read at start, so switching is a swap
    // in memory: nothing waits for the history thread, and what the old profile did is already
    // queued under its own id. The library (favourites, names, groups, versions, sources) belongs
    // to the device and is the same for every profile.
    const ProfileSet& profiles() const { return profiles_; }
    std::int64_t active_profile() const { return active_; }
    // Makes `id` the active profile: runs(), song_practice() and the settings text change to its
    // own at once, and runs finished from now on are saved under it. False (nothing changes) when
    // the history is not loaded yet, the id is not a profile, or it already is the active one. The
    // app then applies profile_settings(id) to its settings and opens the song the profile left.
    bool switch_profile(std::int64_t id);
    // Adds a profile with the next id and the next free colour (a name already used, an empty one
    // and a household of kMaxProfiles are refused); `made` gets it. Needs the history loaded.
    ProfileResult add_profile(std::string_view name, Profile* made = nullptr);
    ProfileResult rename_profile(std::int64_t id, std::string_view name);
    ProfileResult recolour_profile(std::int64_t id, std::uint32_t colour);
    // Deletes a profile and what is its own: its runs (with their notes and measures), its practice
    // choices and its settings text, in one transaction on the history thread. Ok means the deletion
    // is queued; the profile goes from profiles(), runs and every list when poll() reports it done
    // (`HistoryPoll::deleted`), and stays exactly as it was when the transaction fails. The profile in
    // use is left first for the most recently used of the others (the later of its newest run and the
    // time it was last switched away from), so there is always one in use. Refused, with nothing
    // queued, for the only profile and the first one (ProfileSet::check_delete) and when the history
    // cannot be changed now (NotSaved: another etude holds it). The library's own lists (stars,
    // names, groups, versions) are stored under the first profile today, which stays, so they are
    // never touched. Frame thread.
    ProfileResult delete_profile(std::int64_t id);
    // The text of a profile's part of the settings (core profile_settings_text); empty for a
    // profile that never saved any.
    const std::string& profile_settings(std::int64_t id) const;
    // Saves the text under the profile (queued; the same text twice is written once).
    void set_profile_settings(std::int64_t id, std::string text);
    // Every profile's runs, the active one's included, for the household boards
    // (leaderboard.hpp). The spans stay valid until the next switch, add_run or poll.
    std::vector<ProfileRuns> household_runs() const;
    // How many runs there are in all, every profile's: cheap, for what is rebuilt when it changes.
    std::size_t run_count_all() const;

    // Adds a finished run (sanitized) to runs() and queues it for the database. `song_path` is
    // where the song file was opened from, kept as the song's latest path. Does no file I/O.
    void add_run(const RunRecord& record, std::string_view song_path = {});
    // As above with what the player played in the run (core run_events.hpp). The events are kept
    // for the song's newest events_kept_per_song() runs, and not at all for a song without a content
    // hash. `profile_id` is whose run it is (the controller stamps it when the run ends); 0 is the
    // active profile. A run of another profile than the active one is kept with that profile's. No
    // file I/O.
    void add_run(const RunRecord& record, std::string_view song_path, RunEvents&& events,
                 std::int64_t profile_id = 0);

    // How many of a song's newest runs keep their events, apart from pinned runs (0: none).
    // Applies from the next run written; the older runs' events go the next time the song's run is
    // saved. Any thread.
    void set_events_kept_per_song(int count);
    int events_kept_per_song() const { return events_kept_.load(); }

    // Reads one run's events into a plain array, on the history thread, in one query. The answer
    // is waiting in take_event_reads() once the history thread has done it (flush() waits for it).
    // Returns the serial that comes back with the answer, 0 when the store takes no more work.
    // No file I/O on the calling thread.
    std::uint64_t request_run_events(std::int64_t run_id);
    // The best run of the song (by content hash) that can stand as the ghost of a run played in
    // `wanted` (ghost_run.hpp): complete, with its events kept, the same mode, hands and
    // transposition, speed within the tolerance, the highest accuracy. Same delivery as above.
    std::uint64_t request_ghost(std::uint64_t song_hash, const GhostSetup& wanted);
    // The replay of one run (`run_id` 0: the run `request.record` describes, as the database has it:
    // a run just finished, whose id only the history thread knows): its stored notes laid over the song's expected ones, and its summary,
    // both built on the history thread (core run_replay.hpp, run_summary.hpp) so the frame takes a
    // finished answer and does no work but the move: kind Replay in take_event_reads(). A run whose
    // notes are gone, or that the database does not have, is answered with an empty timeline and the
    // summary of its record (also built here). Returns the serial, 0 when the store takes no more
    // work or the request carries no song tables. No file I/O on the calling thread.
    std::uint64_t request_replay(std::int64_t run_id, ReplayRequest request);
    // The answers that are in, oldest first. Frame thread.
    std::vector<EventsRead> take_event_reads();
    // The Progress page's numbers for one profile and the household's lines, built on the history
    // thread from every profile's runs in the database (core build_progress_view). Jobs run in the
    // order they were queued, so a run queued before the request is in the answer. Returns the
    // serial the answer comes back with in take_progress(), 0 when the store takes no more work.
    // Never journaled; no file I/O on the calling thread.
    std::uint64_t request_progress(ProgressRequest request);
    // The answers that are in, oldest first. Frame thread.
    std::vector<ProgressAnswer> take_progress();
    // Pins a run's events so they are never deleted to make room (or lets go of the pin). Does
    // nothing for a run without events (one pruned already, one that never kept any): the answer
    // says whether it took. Returns the serial the answer comes back with in take_event_reads()
    // (kind Pin, `pin_took`), 0 when the store takes no more work. A pin held when the app quits
    // is kept in the journal and is answered "did not take" now. A UI offers a pin only for a run
    // that still has events (request_run_events finds some) and shows one as set only once the
    // answer says it took. No file I/O on the calling thread.
    std::uint64_t set_run_pinned(std::int64_t run_id, bool pinned);

    // What the player set for each song in the library (song_prefs.hpp), by content hash. Until
    // poll() reports `loaded`, only what this session set; a song set this session keeps that
    // over what the database had. Frame thread only.
    using SongPrefsMap = std::unordered_map<std::uint64_t, SongPrefs>;
    const SongPrefsMap& song_prefs() const { return prefs_; }
    // Sets a song's (sanitized; hash 0 is ignored) in song_prefs() and queues it for the database:
    // the whole record, except that the date added keeps the earliest one stored. No file I/O.
    void set_song_prefs(std::uint64_t hash, const SongPrefs& prefs);
    // Several songs, saved in one transaction (the library listing many songs for the first time).
    void set_song_prefs(std::span<const std::pair<std::uint64_t, SongPrefs>> many);

    // The practice choices of each song (song_practice.hpp), by content hash. Until poll() reports
    // `loaded`, only what this session set; a song set this session keeps that over what the
    // database had. Frame thread only.
    using SongPracticeMap = std::unordered_map<std::uint64_t, SongPractice>;
    const SongPracticeMap& song_practice() const { return practice_; }  // the active profile's
    // Sets a song's (sanitized; hash 0 is ignored) in song_practice() and queues it for the
    // database. No file I/O.
    void set_song_practice(std::uint64_t hash, const SongPractice& practice);

    // The player's groups of songs (library_groups.hpp). Until poll() reports `loaded`, only what
    // this session made; then what the database had with this session's edits laid over it.
    // Frame thread only.
    const LibraryGroups& groups() const { return groups_; }
    // Applies one edit to groups() and queues it for the database. No file I/O.
    void edit_groups(const GroupEdit& edit);
    // Several at once, written in one transaction (a song picked by the hundred).
    void edit_groups(std::span<const GroupEdit> edits);

    // Where each downloaded song came from (song_catalogue.hpp), by content hash. Until poll()
    // reports `loaded`, only what this session recorded. Frame thread only.
    using SongOriginsMap = std::unordered_map<std::uint64_t, SongOrigin>;
    const SongOriginsMap& song_origins() const { return sources_; }
    // Records it (hash 0 is ignored) and queues it for the database. No file I/O.
    void set_song_origin(std::uint64_t hash, const SongOrigin& source);

    // What the player settled by hand about which songs are one piece (song_versions.hpp), by
    // content hash. Until poll() reports `loaded`, only what this session set; then what the
    // database had with this session's edits laid over it. Frame thread only.
    const VersionOverrides& version_overrides() const { return overrides_; }
    // Changes whenever version_overrides() did.
    std::uint64_t version_overrides_serial() const { return overrides_serial_; }
    // Applies one edit and queues the rows it changed for the database. No file I/O.
    void edit_version_overrides(const VersionEdit& edit);

    // Waits until the history thread has dealt with the first load (done, or found the database
    // busy): until `state()` is no longer Loading. For tests and tools; the app never waits.
    bool wait_until_settled(std::chrono::milliseconds timeout);
    // Waits until every queued run is saved or given up. False on timeout (a database that stays
    // busy).
    bool flush(std::chrono::milliseconds timeout = std::chrono::seconds(30));

    // The reasons runs could not be saved since the last call, oldest first. After close(), the
    // runs and settings neither the database nor the journal could keep, as "N practice runs could
    // not be saved" (the player should be told before the app goes).
    std::vector<std::string> take_errors();

    HistoryStats stats() const;

    std::filesystem::path path() const { return directory_ / kHistoryFileName; }

  private:
    struct Job {
        RunRecord record;
        std::string song_path;
        // A job of song settings instead of a run, when not empty.
        std::vector<std::pair<std::uint64_t, SongPrefs>> prefs;
        // A job of group edits instead, when not empty.
        std::vector<GroupEdit> groups;
        // A job of songs' sources instead, when not empty.
        std::vector<std::pair<std::uint64_t, SongOrigin>> sources;
        // A job of version overrides instead, when not empty.
        std::vector<VersionRow> versions;
        // Whose run, practice choices, or read this is.
        std::int64_t profile_id = kDefaultProfileId;
        // A job of profile rows instead (made, renamed, recoloured), when not empty.
        std::vector<Profile> profiles;
        // A job that deletes this profile's rows instead, when not 0.
        std::int64_t delete_profile = 0;
        // A job of one profile's settings text instead, when not 0.
        std::int64_t settings_profile = 0;
        std::string settings_text;
        // A job of one song's practice choices instead, when its hash is not 0.
        std::uint64_t practice_hash = 0;
        SongPractice practice;
        std::int64_t at_utc_s = 0; // when the choices were made (UTC seconds; 0: when written)
        // The run's events, saved with it.
        RunEvents events;
        // A job of events instead (a read, or a pin), when not None.
        enum class Extra : std::uint8_t { None, ReadRun, ReadGhost, ReadReplay, ReadProgress, Pin };
        Extra extra = Extra::None;
        std::uint64_t serial = 0;
        std::int64_t run_id = 0;
        std::uint64_t song_hash = 0;
        GhostSetup setup;
        bool pinned = false;
        std::shared_ptr<const ReplayRequest> replay;
        ProgressRequest progress;
    };
    // Unwritable: the file or its folder is read-only for this program (permissions, a read-only disk).
    enum class Attempt { Ready, Damaged, Locked, Newer, Foreign, Unusable, Unwritable };

    static bool is_profile_job(const Job& job) {
        return !job.profiles.empty() || job.settings_profile != 0 || job.delete_profile != 0;
    }
    static EventsRead read_answer(const Job& job, std::uint64_t song_hash,
                                  std::optional<StoredRunEvents> found, bool pin_took = false);
    void log(std::string_view line) const;
    void thread_main();
    bool take_instance_lock();
    void release_instance_lock();
    void read_only_phase();
    static Attempt unusable_for(int rc);
    bool not_writable_for_app() const;
    void tell_waiting(Attempt attempt, const std::string& problem, const std::string& why);
    bool open_loop(bool initial);
    void give_up(bool permanent);
    bool recover_from_damage(const std::string& reason);
    void write_phase();
    void note_write_result(const Job& job, bool other, int rc, const std::string& message,
                           double ms);
    void apply_loaded_profiles(HistoryPoll& result); // mutex_ held, poll() helpers
    void apply_loaded_library();
    void publish_loaded(HistoryState next);
    void set_state(HistoryState next); // mutex_ held
    void say(std::string text);        // a status sentence for poll(); mutex_ not held
    bool sleep_for_retry();            // false when the store is being destroyed

    Attempt attempt_open(std::string& why);
    Attempt check_version(std::string& why);
    Attempt open_version(std::string& why);
    Attempt open_steps(std::string& why);
    Attempt fail(int rc, std::string& why);
    Attempt adopt_unversioned(std::int64_t& version, std::string& why);
    Attempt check_tables(std::string& why);
    Attempt migrate(std::string& why);
    bool prepare_statements();
    Attempt import_legacy_runs(std::string& why);
    Attempt load_runs(std::string& why);
    Attempt load_prefs(std::string& why);
    Attempt load_groups(std::string& why);
    Attempt load_everything(std::string& why);
    bool prepare_profile_statements();
    Attempt load_profiles(std::string& why);
    int write_profile_job(const Job& job); // profile rows, one profile's settings, or a deletion
    int delete_profile_rows(std::int64_t id); // one transaction; the caller has begun none
    bool profile_known(std::int64_t id) const; // the database holds the profile (history thread)
    // keep_held_jobs: the queued jobs as journal records (one JournalHeld each), the queue cleared of
    // all but the reads.
    void collect_held_jobs(std::vector<JournalEntry>& entries, std::vector<JournalHeld>& held);
    void report_deletion(std::int64_t id, bool done); // history thread; mutex_ not held
    void apply_deletions(HistoryPoll& result);        // poll(): mutex_ held
    std::int64_t stored_profile(std::int64_t id) const; // `id` when the database holds it, else 1 (reads)
    void queue_profile_job(Job&& job);
    int write_prefs(const Job& job);
    int write_groups(const Job& job);
    Attempt load_sources(std::string& why);
    int write_sources(const Job& job);
    bool has_sources() const; // the schema has song_sources (migration 4)
    bool has_groups() const; // the schema has the groups' tables (migration 3)
    bool has_versions() const; // and the version overrides' (migration 6)
    bool has_practice() const; // and the songs' practice choices' (migration 7)
    bool has_practice_extras() const; // with the loop and the muted parts (migration 8)
    bool has_practice_split() const;  // and the hand split (migration 10)
    bool has_practice_offer() const;  // and the octave-shift offer turned off (migration 10)
    Attempt load_practice(std::string& why);
    int write_practice(const Job& job);
    bool has_events() const; // the schema has run_event_sets (migration 9)
    int store_events(std::int64_t run_id, const Job& job);
    int run_events_job(const Job& job); // a read or a pin
    int run_progress_job(const Job& job);
    // Every profile's runs, as saved (ORDER BY id), appended to `out`; sqlite's result code.
    int read_all_runs(std::vector<ProfiledRun>& out);
    void answer_nothing(const Job& job); // mutex_ held: the answer of a read that found nothing
    void answer_reads_unread();         // mutex_ held: the queued reads get an empty answer
    int read_events(std::int64_t run_id, std::int64_t profile_id,
                    std::optional<StoredRunEvents>& out);
    int find_run_id(const RunRecord& record, std::int64_t profile_id, std::int64_t& id);
    int read_ghost(const Job& job, std::optional<StoredRunEvents>& out);
    Attempt load_versions(std::string& why);
    int write_versions(const Job& job);
    bool set_aside(std::string& moved);
    // The journal beside the database. Replayed while opening, before the runs are read.
    std::filesystem::path journal_path() const { return directory_ / kJournalFileName; }
    Attempt replay_journal(std::string& why);
    int replay_other(const JournalEntry& entry, bool& pin_dropped);
    // History thread, when it gives up on the jobs still queued for good: writes them (every kind
    // but a read) to the journal, reports (take_errors) what it could not keep, and ends the
    // acceptance of new jobs. `why` goes in the log.
    void keep_held_jobs(std::string_view why);
    void close_database();
    int store_run(const Job& job);
    int write_run(const Job& job);
    void compact_if_worthwhile(); // history thread, after the journal replay (#257)
    static int compact_progress(void* self); // sqlite3_progress_handler: nonzero stops the VACUUM
    static bool is_run_job(const Job& job);
    void reclaim_space();         // history thread, after a run that pruned event sets (#257)

    std::filesystem::path directory_;
    HistoryOptions options_;
    std::span<const Migration> migrations_;
    std::vector<RunRecord> runs_; // frame thread: the active profile's
    ProfileSet profiles_;         // frame thread
    std::int64_t active_ = kDefaultProfileId; // frame thread
    // Frame thread: the other profiles' runs, practice choices and the settings of all of them.
    std::map<std::int64_t, std::vector<RunRecord>> other_runs_;
    std::map<std::int64_t, SongPracticeMap> other_practice_;
    std::map<std::int64_t, std::string> profile_settings_;
    std::map<std::int64_t, std::int64_t> left_utc_s_; // when each profile was last switched away from
    std::set<std::int64_t> deleting_;                 // deletions queued and not yet reported
    std::vector<std::pair<std::int64_t, bool>> deletion_reports_; // mutex_: (id, done) from the thread
    SongPrefsMap prefs_;          // frame thread
    SongOriginsMap sources_;      // frame thread
    SongPracticeMap practice_;    // frame thread
    VersionOverrides overrides_;  // frame thread
    std::uint64_t overrides_serial_ = 0;
    std::vector<VersionEdit> override_edits_; // frame thread: this session's, until the load is in
    LibraryGroups groups_;        // frame thread
    std::vector<GroupEdit> group_edits_; // frame thread: this session's, until the load is in
    bool loaded_ = false;         // frame thread: poll() has added the database's runs

    // History thread only, until publish_loaded() moves the runs over. The connection and its
    // statements are declared in this order so the statements are finalized before the connection
    // closes.
    std::vector<ProfiledRun> thread_runs_;
    std::vector<Profile> thread_profile_rows_;
    std::vector<std::pair<std::int64_t, std::string>> thread_profile_settings_;
    std::vector<std::int64_t> thread_profile_ids_; // the profiles the database holds
    int lock_fd_ = -1;
    sql::Database db_;
    sql::Statement begin_, commit_, rollback_, upsert_song_, insert_run_, insert_measure_,
        upsert_profile_, upsert_profile_settings_, upsert_prefs_, insert_group_, rename_group_,
        delete_group_, delete_group_songs_, add_group_song_, drop_group_song_, move_group_,
        upsert_source_, insert_override_, delete_override_, upsert_practice_, delete_parts_,
        insert_part_, insert_event_set_, insert_event_, prune_events_, pin_events_,
        select_event_set_, select_events_, select_candidates_, select_run_id_;
    std::vector<std::pair<std::uint64_t, SongPrefs>> thread_prefs_;
    std::vector<SongGroup> thread_groups_;
    std::vector<std::pair<std::uint64_t, SongOrigin>> thread_sources_;
    std::vector<VersionRow> thread_versions_;
    struct ProfiledPractice {
        std::int64_t profile_id = kDefaultProfileId;
        std::uint64_t hash = 0;
        SongPractice practice;
    };
    std::vector<ProfiledPractice> thread_practice_;
    std::vector<LibraryGroups::Member> thread_members_;

    mutable std::mutex mutex_;
    std::condition_variable work_;    // the history thread waits on it
    std::condition_variable changed_; // flush() and wait_until_settled() wait on it
    void announce() {
        changed_.notify_all();
        wake_loop(WakeSource::History); // a result the frame thread polls
    }
    std::atomic<HistoryState> state_{HistoryState::Loading};
    std::atomic<bool> unwritable_{false};
    std::deque<Job> queue_;
    bool busy_ = false;
    bool stop_ = false;
    bool accepting_ = true; // false once the thread is done with the queue for good
    std::vector<ProfiledRun> loaded_runs_; // handed over by publish_loaded()
    std::vector<Profile> loaded_profile_rows_;
    std::vector<std::pair<std::int64_t, std::string>> loaded_profile_settings_;
    std::vector<std::pair<std::uint64_t, SongPrefs>> loaded_prefs_;
    std::vector<SongGroup> loaded_groups_;
    std::vector<std::pair<std::uint64_t, SongOrigin>> loaded_sources_;
    std::vector<VersionRow> loaded_versions_;
    std::vector<ProfiledPractice> loaded_practice_;
    std::vector<LibraryGroups::Member> loaded_members_;
    bool load_ready_ = false;
    bool journal_reported_ = false; // history thread: a journal left alone was said once
    bool instance_locked_ = false;  // history thread: this process holds history.lock
    bool journal_replayed_ = false; // history thread: the journal was read this session
    bool pin_matched_ = true;       // history thread: the last pin found its run's notes
    bool skip_replay_ = false;      // history thread: the attempt made at quit does not replay
    bool incremental_ = false;      // history thread: the open file is in auto_vacuum INCREMENTAL
    bool reclaim_pending_ = false;  // history thread: free pages may still be handed back
    bool compacting_ = false;       // mutex_: the one-time VACUUM is running
    std::atomic<bool> compact_abort_{false}; // close() or a finished run asks it to stop
    std::vector<std::string> messages_;
    std::vector<std::string> errors_;
    std::vector<EventsRead> event_reads_; // answers waiting for take_event_reads()
    std::vector<ProgressAnswer> progress_answers_; // waiting for take_progress()
    std::uint64_t read_serial_ = 0;
    std::atomic<int> events_kept_{kDefaultRunEventsKept};
    HistoryStats stats_;
    std::thread thread_;
};

} // namespace etude::app
