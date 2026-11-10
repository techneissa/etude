// SPDX-License-Identifier: GPL-3.0-or-later
// The history journal: records round-tripped through the file's bytes, torn and damaged records, and what is kept at quit.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <cstring>
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST_CASE("journal: runs and practice choices survive a round trip through the file's bytes") {
    JournalRun run;
    run.record = sample_run(7);
    run.song_path = "/songs/caf\xC3\xA9.mid";
    run.events = sample_events(5, false);
    run.events.dropped = 3;
    JournalPractice practice;
    practice.hash = 0xFFFFFFFFFFFFFF01ULL; // the top bit set, as a hash can be
    practice.practice = busy_practice();
    JournalRun bare; // no events, no path
    bare.record = sample_run(8, 0);

    std::string bytes = encode_journal_header();
    for (const JournalEntry& e : {JournalEntry{run}, JournalEntry{practice}, JournalEntry{bare}}) {
        bytes += encode_journal_entry(e);
    }
    const JournalDecode back = decode_journal(bytes);
    REQUIRE(back.entries.size() == 3);
    CHECK(back.damaged == 0);
    CHECK_FALSE(back.truncated);
    CHECK(back.valid_end == bytes.size());
    const JournalRun& r0 = std::get<JournalRun>(back.entries[0]);
    CHECK(r0.record == run.record);
    CHECK(r0.song_path == run.song_path);
    CHECK(r0.events == run.events);
    const JournalPractice& p1 = std::get<JournalPractice>(back.entries[1]);
    CHECK(p1.hash == practice.hash);
    CHECK(p1.practice == practice.practice);
    const JournalRun& r2 = std::get<JournalRun>(back.entries[2]);
    CHECK(r2.record == bare.record);
    CHECK(r2.song_path.empty());
    CHECK(r2.events.events.empty());
}


TEST_CASE("journal: the library's records round-trip, and bytes after their fields are ignored") {
    JournalPrefs prefs;
    SongPrefs a;
    a.title = "Mine";
    a.composer = "Bach";
    a.genre = "Baroque";
    a.favourite = true;
    a.added_utc_s = 1000;
    a.chosen_utc_s = 2000;
    prefs.items.emplace_back(0xFFFFFFFFFFFFFF01ULL, a);
    prefs.items.emplace_back(2, SongPrefs{});
    JournalGroups groups;
    GroupEdit created;
    created.kind = GroupEdit::Kind::Create;
    created.group.id = 0xFFFFFFFFFFFFFFF0ULL;
    created.group.name = "Caf\xC3\xA9";
    created.group.kind = GroupKind::Catalogue;
    created.group.source = "mutopia";
    created.group.position = -3;
    created.group.created_utc_s = 77;
    GroupEdit added;
    added.kind = GroupEdit::Kind::Add;
    added.group.id = 9;
    added.hash = 0xABCDEF0123456789ULL;
    added.at_utc_s = 88;
    groups.edits = {created, added};
    JournalOrigins origins;
    origins.items.emplace_back(0x1234, sample_origin());
    JournalVersions versions;
    versions.rows = {{0xAA, 0, true}, {0xB1, 0xB2, false}};
    JournalPin pin{1234567, true};

    std::string bytes = encode_journal_header();
    for (const JournalEntry& e : {JournalEntry{prefs}, JournalEntry{groups}, JournalEntry{origins},
                                  JournalEntry{versions}, JournalEntry{pin}}) {
        bytes += encode_journal_entry(e);
    }
    const JournalDecode back = decode_journal(bytes);
    REQUIRE(back.entries.size() == 5);
    CHECK(back.damaged == 0);
    CHECK(back.valid_end == bytes.size());
    const JournalPrefs& p0 = std::get<JournalPrefs>(back.entries[0]);
    REQUIRE(p0.items.size() == 2);
    CHECK(p0.items[0] == prefs.items[0]);
    CHECK(p0.items[1] == prefs.items[1]);
    const JournalGroups& g1 = std::get<JournalGroups>(back.entries[1]);
    REQUIRE(g1.edits.size() == 2);
    CHECK(g1.edits[0].kind == GroupEdit::Kind::Create);
    CHECK(g1.edits[0].group == created.group);
    CHECK(g1.edits[1].kind == GroupEdit::Kind::Add);
    CHECK(g1.edits[1].group.id == 9);
    CHECK(g1.edits[1].hash == added.hash);
    CHECK(g1.edits[1].at_utc_s == 88);
    CHECK(std::get<JournalOrigins>(back.entries[2]).items == origins.items);
    const JournalVersions& v3 = std::get<JournalVersions>(back.entries[3]);
    REQUIRE(v3.rows.size() == 2);
    CHECK(v3.rows[0].a == 0xAA);
    CHECK(v3.rows[0].b == 0);
    CHECK(v3.rows[0].add);
    CHECK(v3.rows[1].b == 0xB2);
    CHECK_FALSE(v3.rows[1].add);
    CHECK(std::get<JournalPin>(back.entries[4]).run_id == 1234567);
    CHECK(std::get<JournalPin>(back.entries[4]).pinned);

    // A later writer's extra bytes after the fields are left unread.
    const std::string pin_payload = encode_journal_entry(pin).substr(8) + "more";
    const JournalDecode longer = decode_journal(encode_journal_header() + framed_record(pin_payload));
    REQUIRE(longer.entries.size() == 1);
    CHECK(longer.damaged == 0);
    CHECK(std::get<JournalPin>(longer.entries[0]).run_id == 1234567);

    // A record cut inside its fields, a count with nothing behind it, and a pin of no run are
    // skipped as damaged, and the records around them are kept.
    const std::string good = encode_journal_entry(pin);
    const std::string cut = encode_journal_entry(groups).substr(8);
    const std::string empty_list = std::string("V") + '\x00';
    const std::string no_run = encode_journal_entry(JournalPin{0, true}).substr(8);
    const JournalDecode odd = decode_journal(
        encode_journal_header() + good + framed_record(cut.substr(0, cut.size() - 3)) +
        framed_record(empty_list) + framed_record(no_run) + good);
    CHECK(odd.entries.size() == 2);
    CHECK(odd.damaged == 3);
    CHECK_FALSE(odd.truncated);
}

TEST_CASE("journal: a kind this build does not know is skipped like a damaged record, the rest is kept") {
    // What an etude from before #269 does with a new journal, and what this one does with a kind a
    // later one adds: the record is counted as damaged and skipped, the records around it are read.
    JournalRun run;
    run.record = sample_run(2);
    JournalPractice practice;
    practice.hash = 0x77;
    practice.practice = busy_practice();
    const std::string unknown = framed_record("Zsome records of a later build");
    const JournalDecode d = decode_journal(encode_journal_header() + encode_journal_entry(run) +
                                           unknown + encode_journal_entry(practice));
    REQUIRE(d.entries.size() == 2);
    CHECK(d.damaged == 1);
    CHECK_FALSE(d.truncated);
    CHECK_FALSE(d.framing_damaged);
    CHECK(std::holds_alternative<JournalRun>(d.entries[0]));
    CHECK(std::holds_alternative<JournalPractice>(d.entries[1]));
}

TEST_CASE("journal: a journal of runs and practice choices only, as the build before wrote it, still replays") {
    TempDir dir;
    {
        HistoryStore seed(dir.path, quiet());
        REQUIRE(open_now(seed).empty());
    }
    JournalRun run;
    run.record = sample_run(2);
    run.song_path = "/songs/a.mid";
    run.events = sample_events(4);
    const fs::path file = dir.path / kJournal;
    const std::string old_journal =
        "etude journal 1\n" + encode_journal_entry(run) + first_layout_practice_record();
    for (int pass = 0; pass < 2; ++pass) { // and again: a copy that came back adds nothing
        write_all(file, old_journal);
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 1);
        REQUIRE(store.song_practice().count(0x77) == 1);
        CHECK(store.song_practice().at(0x77).speed_percent == 40);
        CHECK(store.song_practice().at(0x77).hands == HandFilter::Left);
        CHECK(store.song_practice().at(0x77).muted_parts == std::vector<std::uint32_t>{5});
        CHECK_FALSE(fs::exists(file));
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    }
}

TEST_CASE("journal: a torn last record, a bad check and a wrong header are told apart") {
    JournalPractice practice;
    practice.hash = 5;
    JournalRun run;
    run.record = sample_run(1);
    const std::string head = encode_journal_header();
    const std::string a = encode_journal_entry(run);
    const std::string b = encode_journal_entry(practice);

    // Cut inside the last record: the first is read, the cut one is not, and the file's valid
    // part ends where the first does.
    const JournalDecode torn = decode_journal((head + a + b).substr(0, head.size() + a.size() + 5));
    CHECK(torn.entries.size() == 1);
    CHECK(torn.truncated);
    CHECK(torn.valid_end == head.size() + a.size());

    // A flipped byte in the first record's payload: that record is skipped, the next is read.
    std::string flipped = head + a + b;
    flipped[head.size() + 12] = static_cast<char>(flipped[head.size() + 12] ^ 0x40);
    const JournalDecode bad = decode_journal(flipped);
    CHECK(bad.damaged == 1);
    REQUIRE(bad.entries.size() == 1);
    CHECK(std::holds_alternative<JournalPractice>(bad.entries[0]));

    CHECK(decode_journal("").entries.empty());
    CHECK(decode_journal("not a journal\n").bad_header);
    CHECK(decode_journal("etude runs 1\n").bad_header);
    const JournalDecode cut_header = decode_journal("etude jour");
    CHECK(cut_header.truncated);
    CHECK_FALSE(cut_header.bad_header);
    CHECK(cut_header.valid_end == 0);
    const JournalDecode newer = decode_journal("etude journal 2\nanything");
    CHECK(newer.newer_version == 2);
    CHECK(newer.entries.empty());
}

#if !defined(_WIN32)
TEST_CASE("journal: appending cuts a torn tail first and refuses a file that is not ours") {
    TempDir dir;
    const fs::path file = dir.path / kJournal;
    JournalRun one;
    one.record = sample_run(1);
    JournalRun two;
    two.record = sample_run(2);
    JournalRun three;
    three.record = sample_run(3);

    CHECK(append_journal(file, std::vector<JournalEntry>{one}).saved == 1);
    // The next write was cut short.
    write_all(file, read_all(file) + encode_journal_entry(two).substr(0, 9));
    const JournalAppend added = append_journal(file, std::vector<JournalEntry>{three});
    CHECK(added.saved == 1);
    CHECK(added.why.empty());
    const JournalDecode all = decode_journal(read_all(file));
    CHECK(all.damaged == 0);
    CHECK_FALSE(all.truncated);
    REQUIRE(all.entries.size() == 2);
    CHECK(std::get<JournalRun>(all.entries[0]).record == one.record);
    CHECK(std::get<JournalRun>(all.entries[1]).record == three.record);

    // Not ours, or newer: left byte for byte, nothing written.
    for (const std::string& foreign : {std::string("a diary\n"), std::string("etude journal 9\nx")}) {
        write_all(file, foreign);
        const JournalAppend refused = append_journal(file, std::vector<JournalEntry>{one});
        CHECK(refused.saved == 0);
        CHECK_FALSE(refused.why.empty());
        CHECK(read_all(file) == foreign);
    }
    fs::remove(file);
    fs::create_directory(file); // a folder where the file should be
    CHECK(append_journal(file, std::vector<JournalEntry>{one}).saved == 0);
}

TEST_CASE("journal: jobs held when the app quits are kept, and the next start saves them") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    CHECK(quit_with_held_jobs(dir.path).empty()); // kept, so nothing to tell the player
    const fs::path file = dir.path / kJournal;
    REQUIRE(fs::exists(file));
    const JournalDecode kept = decode_journal(read_all(file));
    REQUIRE(kept.entries.size() == 3);
    CHECK(std::holds_alternative<JournalRun>(kept.entries[0]));
    CHECK(std::get<JournalRun>(kept.entries[1]).events == sample_events(4));
    CHECK(std::get<JournalPractice>(kept.entries[2]).practice == busy_practice());
    {
        Raw raw(dir.path / "history.db");
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1); // the database is as it was
    }

    make_writable(dir.path, true);
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.state() == HistoryState::Ready);
        REQUIRE(store.runs().size() == 3); // run 1 from before, then the two kept, in order
        CHECK(store.runs()[1] == sample_run(2));
        CHECK(store.runs()[2] == sample_run(3));
        REQUIRE(store.song_practice().count(0x77) == 1);
        CHECK(store.song_practice().at(0x77) == busy_practice());
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM run_events") ==
              static_cast<std::int64_t>(sample_events(4).events.size()));
        CHECK(raw.text("SELECT path FROM songs WHERE content_hash = " +
                       std::to_string(hash_to_column(0xabc0)) + " AND path <> ''") ==
              "/songs/a.mid");
    }
    CHECK_FALSE(fs::exists(file));
}

TEST_CASE("journal: replaying the same journal twice adds nothing") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    REQUIRE(quit_with_held_jobs(dir.path).empty());
    const fs::path file = dir.path / kJournal;
    const std::string journal = read_all(file);
    make_writable(dir.path, true);
    check_landed(dir.path, 3);
    CHECK_FALSE(fs::exists(file));

    // The file could not be deleted last time, or a copy came back: the same records again.
    for (int again = 0; again < 2; ++again) {
        write_all(file, journal);
        check_landed(dir.path, 3);
        CHECK_FALSE(fs::exists(file));
    }
    Raw raw(dir.path / "history.db");
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM song_practice") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM song_practice_parts") == 2);
}

TEST_CASE("journal: a torn last record is skipped and the others are saved") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    REQUIRE(quit_with_held_jobs(dir.path).empty());
    const fs::path file = dir.path / kJournal;
    const std::string journal = read_all(file);
    write_all(file, journal.substr(0, journal.size() - 4)); // the power went in the last record
    make_writable(dir.path, true);

    Log log;
    HistoryStore store(dir.path, log.options());
    CHECK(open_now(store).find("1 record") != std::string::npos); // and the player is told
    CHECK(store.runs().size() == 3);                 // the two runs landed
    CHECK(store.song_practice().count(0x77) == 0);   // the cut record did not
    CHECK_FALSE(fs::exists(file));
    bool said = false;
    for (const std::string& line : log.get()) {
        said = said || line.find("ended inside one") != std::string::npos;
    }
    CHECK(said);
}

TEST_CASE("journal: a record with a bad check is skipped and the ones after it are saved") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    REQUIRE(quit_with_held_jobs(dir.path).empty());
    const fs::path file = dir.path / kJournal;
    std::string journal = read_all(file);
    const JournalDecode kept = decode_journal(journal);
    const std::size_t second = encode_journal_header().size() + encode_journal_entry(kept.entries[0]).size();
    journal[second + 12] = static_cast<char>(journal[second + 12] ^ 0x01); // inside run 3's payload
    write_all(file, journal);
    make_writable(dir.path, true);

    HistoryStore store(dir.path, quiet());
    CHECK(open_now(store).find("1 record") != std::string::npos); // and the player is told
    REQUIRE(store.runs().size() == 2); // run 1 and run 2; run 3 is gone
    CHECK(store.runs()[1] == sample_run(2));
    CHECK(store.song_practice().count(0x77) == 1);
    CHECK_FALSE(fs::exists(file));
}

TEST_CASE("journal: when it cannot be written either, the loss is reported") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    const std::vector<std::string> errors = quit_with_held_jobs(dir.path, true, true);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0] == "2 practice runs, 1 practice setting and 1 library change could not be saved");
    make_writable(dir.path, true);
    check_landed(dir.path, 1, false); // only the first run: the rest is gone, and the player knows
}

TEST_CASE("journal: the library's changes and pins held at quit are kept, and lost only with the journal") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    for (const bool journal_is_a_folder : {false, true}) {
        CAPTURE(journal_is_a_folder);
        TempDir dir;
        {
            HistoryStore seed(dir.path, quiet());
            REQUIRE(open_now(seed).empty());
        }
        make_writable(dir.path, false);
        if (journal_is_a_folder) {
            fs::create_directory(dir.path / kJournal);
        }
        HistoryOptions options = quiet();
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        open_now(store);
        store.add_run(sample_run(1));
        SongPrefs star;
        star.favourite = true;
        store.set_song_prefs(0x9, star);
        store.set_run_pinned(1, true);
        CHECK_FALSE(flush_fails(store));
        store.close();
        store.close(); // twice is harmless
        const std::vector<std::string> errors = store.take_errors();
        if (journal_is_a_folder) {
            REQUIRE(errors.size() == 1);
            CHECK(errors[0] == "1 practice run and 2 library changes could not be saved");
        } else {
            CHECK(errors.empty()); // the run, the star and the pin are all in the journal
            const JournalDecode kept = decode_journal(read_all(dir.path / kJournal));
            REQUIRE(kept.entries.size() == 3);
            CHECK(std::holds_alternative<JournalRun>(kept.entries[0]));
            CHECK(std::holds_alternative<JournalPrefs>(kept.entries[1]));
            CHECK(std::holds_alternative<JournalPin>(kept.entries[2]));
        }
    }
}

#endif
