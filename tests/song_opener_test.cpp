// SPDX-License-Identifier: GPL-3.0-or-later
// Opening a song off the frame thread: a request never waits, the answer is taken on a later
// frame, a newer request replaces an older one, and a large file is read and parsed on the loader's
// thread with the same result as reading it here.
#include "song_opener.hpp"

#include "hand_split_cache.hpp"

#include "smf_builder.hpp"

#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

namespace fs = std::filesystem;

// Holds a load until the test lets it go, so the order of events is the test's and not a race.
class Gate {
  public:
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return open_; });
    }
    void open() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
        }
        cv_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = false;
};

SongFileLoad fake_song(const std::filesystem::path& path) {
    SongFileLoad out;
    out.ok = true;
    out.song.title = path.filename().string();
    return out;
}

// Polls for the answer the way the frame loop does, one frame at a time, for at most five seconds
// (a failure, not a measurement: nothing is judged on how long it takes).
std::optional<SongOpener::Opened> take_within_five_seconds(SongOpener& opener) {
    for (int frame = 0; frame < 5000; ++frame) {
        if (auto opened = opener.take()) {
            return opened;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

fs::path scratch_dir(const char* name) {
    const fs::path dir = fs::temp_directory_path() /
                         (std::string("etude-opener-") + name + "-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("opening: a request does not wait for the load, and the answer comes later") {
    Gate gate;
    SongOpener opener([&](const fs::path& path, const SongLoadOptions&) {
        gate.wait();
        return fake_song(path);
    });
    CHECK_FALSE(opener.busy());
    opener.request("a.mid", {});
    CHECK(opener.busy());
    CHECK_FALSE(opener.take().has_value()); // still loading: the old song would keep drawing
    gate.open();
    const auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    CHECK(opened->path == "a.mid");
    CHECK(opened->load.ok);
    CHECK_FALSE(opener.busy());
    CHECK_FALSE(opener.take().has_value()); // an answer is returned once
}

TEST_CASE("opening: five quick requests open only the last") {
    Gate first_started_gate;
    Gate release_first;
    std::mutex mutex;
    std::vector<std::string> loaded;
    SongOpener opener([&](const fs::path& path, const SongLoadOptions&) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            loaded.push_back(path.filename().string());
        }
        if (path.filename() == "1.mid") {
            first_started_gate.open();
            release_first.wait();
        }
        return fake_song(path);
    });
    opener.request("1.mid", {});
    first_started_gate.wait(); // the loader is inside the first load
    for (const char* name : {"2.mid", "3.mid", "4.mid", "5.mid"}) {
        opener.request(name, {});
    }
    release_first.open();
    const auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    CHECK(opened->path == "5.mid");
    CHECK_FALSE(opener.take().has_value());
    opener.cancel();
    // The first load had begun and ran to its end; 2, 3 and 4 never began.
    const std::lock_guard<std::mutex> lock(mutex);
    CHECK(loaded == std::vector<std::string>{"1.mid", "5.mid"});
    CHECK(opener.started() == 2);
}

TEST_CASE("opening: the answer of a load that was replaced while it ran is dropped") {
    Gate started;
    Gate release;
    SongOpener opener([&](const fs::path& path, const SongLoadOptions&) {
        if (path.filename() == "old.mid") {
            started.open();
            release.wait();
        }
        return fake_song(path);
    });
    opener.request("old.mid", {});
    started.wait();
    opener.request("new.mid", {});
    release.open();
    const auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    CHECK(opened->path == "new.mid"); // never the old one, whichever load ended first
}

TEST_CASE("opening: a cancelled request never answers") {
    Gate gate;
    Gate started;
    SongOpener opener([&](const fs::path& path, const SongLoadOptions&) {
        started.open();
        gate.wait();
        return fake_song(path);
    });
    opener.request("a.mid", {});
    started.wait(); // the loader is inside the load
    opener.cancel();
    CHECK_FALSE(opener.busy());
    gate.open();
    // The load ran to its end (it could not be told to stop) and its answer was dropped.
    for (int i = 0; i < 5000 && opener.finished() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(opener.finished() == 1);
    CHECK_FALSE(opener.take().has_value());
    CHECK_FALSE(opener.busy());
}

TEST_CASE("opening: destroying the opener while a load runs waits for it and answers nothing") {
    Gate started;
    Gate release;
    std::atomic<bool> load_returned{false};
    std::thread releaser;
    {
        SongOpener opener([&](const fs::path& path, const SongLoadOptions&) {
            started.open();
            release.wait();
            load_returned = true;
            return fake_song(path);
        });
        opener.request("a.mid", {});
        started.wait(); // the loader is inside the load
        releaser = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            release.open();
        });
    } // the destructor joins the loader: it cannot leave it running on freed state
    CHECK(load_returned.load());
    releaser.join();
}

TEST_CASE("opening: a delayed load is never read when the opener is destroyed or replaced") {
    std::atomic<int> loads{0};
    const auto count = [&](const fs::path& path, const SongLoadOptions&) {
        ++loads;
        return fake_song(path);
    };
    {
        // Quitting ends the delay at once instead of waiting it out.
        const auto begin = std::chrono::steady_clock::now();
        {
            SongOpener opener(count, std::chrono::seconds(30));
            opener.request("a.mid", {});
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); // the loader is waiting
        }
        CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(10));
        CHECK(loads == 0);
    }
    {
        // A request that replaces the waiting one is the one that loads.
        SongOpener opener(count, std::chrono::milliseconds(200));
        opener.request("old.mid", {});
        opener.request("new.mid", {});
        const auto opened = take_within_five_seconds(opener);
        REQUIRE(opened.has_value());
        CHECK(opened->path == "new.mid");
        CHECK(loads == 1);
        CHECK(opener.started() == 1);
    }
}

TEST_CASE("opening: a load that throws answers with a failure instead of ending the program") {
    SongOpener opener([](const fs::path&, const SongLoadOptions&) -> SongFileLoad {
        throw std::bad_alloc();
    });
    opener.request("/songs/huge.mid", {});
    const auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    CHECK_FALSE(opened->load.ok);
    CHECK(opened->load.error.find("huge.mid") != std::string::npos);
    // And the loader still serves the next request.
    opener.request("b.mid", {});
    CHECK(take_within_five_seconds(opener).has_value());
}

TEST_CASE("opening: a file that fails to load answers with its error") {
    SongOpener opener;
    opener.request("/nonexistent/folder/missing.mid", {});
    const auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    CHECK_FALSE(opened->load.ok);
    CHECK_FALSE(opened->load.error.empty());
}

TEST_CASE("opening: the tune asked for comes back with the answer") {
    SongOpener opener([&](const fs::path& path, const SongLoadOptions& options) {
        SongFileLoad out = fake_song(path);
        out.song.title = options.tune;
        return out;
    });
    SongLoadOptions options;
    options.tune = "X=3";
    opener.request("tunes.abc", options);
    const auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    CHECK(opened->tune == "X=3");
    CHECK(opened->load.song.title == "X=3");
}

TEST_CASE("opening: a large file is parsed on the loader's thread, to the same song") {
    // About 120,000 notes: far more than any real tune, so the parse is the slow part of an open.
    std::string text = "X:1\nT:Large\nM:4/4\nL:1/8\nK:C\n";
    for (int bar = 0; bar < 15000; ++bar) {
        text += "CDEF GABc|\n";
    }
    const fs::path dir = scratch_dir("large");
    const fs::path file = dir / "large.abc";
    {
        std::ofstream out(file, std::ios::binary);
        out << text;
    }
    SongOpener opener;
    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id loaded_on;
    SongOpener spy([&](const fs::path& path, const SongLoadOptions& options) {
        loaded_on = std::this_thread::get_id();
        return load_song_file(path, options);
    });
    spy.request(file, {});
    const auto opened = take_within_five_seconds(spy);
    REQUIRE(opened.has_value());
    REQUIRE(opened->load.ok);
    CHECK(loaded_on != caller);
    const SongFileLoad here = load_song_file(file, {});
    REQUIRE(here.ok);
    CHECK(opened->load.song.song.notes.size() == here.song.song.notes.size());
    CHECK(opened->load.song.hash == here.song.hash);
    CHECK(opened->load.song.title == "Large");
    // The default loader gives the same song as well.
    opener.request(file, {});
    const auto plain = take_within_five_seconds(opener);
    REQUIRE(plain.has_value());
    CHECK(plain->load.song.hash == here.song.hash);
    fs::remove_all(dir);
}

TEST_CASE("opening: the 'Opening' state shows from 150 ms on, not before") {
    CHECK_FALSE(opening_shown(0));
    CHECK_FALSE(opening_shown(kOpeningShownNs - 1));
    CHECK(opening_shown(kOpeningShownNs)); // exactly at the limit
    CHECK(opening_shown(kOpeningShownNs + 1));
    CHECK(kOpeningShownNs == 150'000'000);
}

TEST_CASE("opening: the status text names the file, and the tune when there is one") {
    CHECK(opening_text("/songs/etude.mid", {}) == "Opening etude.mid\xE2\x80\xA6");
    CHECK(opening_text("/songs/folk.abc", "X=3") == "Opening folk.abc#X=3\xE2\x80\xA6");
}

TEST_CASE("opening: with a plan the answer carries the song's practice tables, made by the loader") {
    const std::string bach = std::string(ETUDE_TEST_DATA_DIR) + "/bach-bwv846-prelude.mid";
    SongController controller;
    SongOpener opener;
    opener.request(bach, {}, controller.load_plan());
    auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    REQUIRE(opened->load.ok);
    CHECK(opened->found);
    REQUIRE(opened->prepared.valid);
    CHECK(opened->prepared.flow.note_count() == opened->load.song.song.notes.size());
    CHECK(opened->prepared.wait.note_count() == opened->load.song.song.notes.size());
    CHECK(opened->prepared.densest_window > 0);
    // The frame thread takes them over without building them.
    controller.load(std::move(opened->load.song), 1'000'000'000, std::move(opened->prepared));
    CHECK(opened->prepared.flow.note_count() == 0);
    CHECK(controller.has_song());

    // No plan, no tables; a file that is not there is not found and has none.
    opener.request(bach, {});
    const auto plain = take_within_five_seconds(opener);
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->prepared.valid);
    opener.request("/nonexistent/none.mid", {}, controller.load_plan());
    const auto missing = take_within_five_seconds(opener);
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->found);
    CHECK_FALSE(missing->load.ok);
    CHECK_FALSE(missing->prepared.valid);
    std::vector<SongSoundEvent> out;
    controller.shutdown(1'000'000'000, out);
}

namespace {

// The saved song's two places: a library copy and an absolute copy, as files in a scratch folder.
struct RestoreFolder {
    fs::path dir = scratch_dir("restore");
    fs::path library = dir / "library" / "tune.mid";
    fs::path absolute = dir / "elsewhere" / "tune.mid";
    std::string bach = std::string(ETUDE_TEST_DATA_DIR) + "/bach-bwv846-prelude.mid";
    std::string satie = std::string(ETUDE_TEST_DATA_DIR) + "/satie-gymnopedie-1.mid";

    RestoreFolder() {
        fs::create_directories(library.parent_path());
        fs::create_directories(absolute.parent_path());
    }
    ~RestoreFolder() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    static void copy(const std::string& from, const fs::path& to) {
        fs::copy_file(from, to, fs::copy_options::overwrite_existing);
    }
    static std::uint64_t hash_of(const std::string& file) {
        const SongFileLoad load = load_song_file(file);
        REQUIRE(load.ok);
        return load.song.hash;
    }
    // A saved session naming both copies, with the hash of `wanted`.
    SessionState saved(const std::string& wanted) const {
        SessionState s;
        s.song_path = "tune.mid";
        s.song_abs_path = absolute.generic_string();
        s.song_hash = hash_of(wanted);
        s.position_s = 3.0;
        return s;
    }
    SavedSongRestore::Request request(const SessionState& state) const {
        SavedSongRestore::Request r;
        r.saved = state;
        r.library = SongPath{library, {}};
        r.absolute = split_song_path(state.song_abs_path);
        r.options = [](const SongPath& song) {
            SongLoadOptions o;
            o.tune = song.tune;
            return o;
        };
        return r;
    }
};

std::optional<SavedSongRestore::Result> poll_within_five_seconds(SavedSongRestore& restore,
                                                                 SongOpener& opener) {
    for (int frame = 0; frame < 5000; ++frame) {
        if (auto result = restore.poll(opener)) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("restore: the library copy is used when it has the saved content, and nothing else is read") {
    RestoreFolder f;
    RestoreFolder::copy(f.bach, f.library);
    RestoreFolder::copy(f.satie, f.absolute); // another song at the absolute path
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, f.request(f.saved(f.bach)));
    CHECK(restore.active());
    const auto result = poll_within_five_seconds(restore, opener);
    REQUIRE(result.has_value());
    CHECK_FALSE(restore.active());
    CHECK(result->source == SongSource::Library);
    CHECK(result->probe.found);
    CHECK(result->probe.loaded);
    CHECK(result->probe.hash_matches);
    REQUIRE(result->song.has_value());
    CHECK(result->song->path == f.library);
    CHECK(result->load.ok);
    CHECK(opener.started() == 1); // the absolute copy was never read
    CHECK_FALSE(restore.poll(opener).has_value()); // and it answers once
}

TEST_CASE("restore: a library file that changed is passed over for the absolute copy that matches") {
    RestoreFolder f;
    RestoreFolder::copy(f.satie, f.library); // changed since it was saved
    RestoreFolder::copy(f.bach, f.absolute);
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, f.request(f.saved(f.bach)));
    const auto result = poll_within_five_seconds(restore, opener);
    REQUIRE(result.has_value());
    CHECK(result->source == SongSource::Absolute);
    CHECK(result->probe.hash_matches);
    REQUIRE(result->song.has_value());
    CHECK(result->song->path == f.absolute);
    CHECK(result->load.song.hash == RestoreFolder::hash_of(f.bach));
    CHECK(opener.started() == 2); // the library's, then the absolute one
}

TEST_CASE("restore: a missing library file falls back to the absolute copy") {
    RestoreFolder f;
    RestoreFolder::copy(f.bach, f.absolute);
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, f.request(f.saved(f.bach)));
    const auto result = poll_within_five_seconds(restore, opener);
    REQUIRE(result.has_value());
    CHECK(result->source == SongSource::Absolute);
    CHECK(result->probe.found);
    CHECK(result->probe.hash_matches);
}

TEST_CASE("restore: when neither copy matches, the library's changed file answers (the song changed)") {
    RestoreFolder f;
    RestoreFolder::copy(f.satie, f.library);
    // The absolute copy is gone.
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, f.request(f.saved(f.bach)));
    const auto result = poll_within_five_seconds(restore, opener);
    REQUIRE(result.has_value());
    CHECK(result->source == SongSource::Library);
    CHECK(result->probe.found);
    CHECK(result->probe.loaded);
    CHECK_FALSE(result->probe.hash_matches);
    const RestorePlan plan =
        plan_restore(f.saved(f.bach), result->probe.found, result->probe.hash_matches,
                     result->load.ok ? &result->load.song.song : nullptr);
    CHECK(plan.outcome == RestoreOutcome::SongChanged);
    CHECK(plan.load_song); // the changed file is the player's, and opens from the top

    // Both changed: the library's still answers.
    RestoreFolder::copy(f.satie, f.absolute);
    SavedSongRestore again;
    again.begin(opener, f.request(f.saved(f.bach)));
    const auto both = poll_within_five_seconds(again, opener);
    REQUIRE(both.has_value());
    CHECK(both->source == SongSource::Library);
}

TEST_CASE("restore: a saved session with no hash takes the first copy that loads") {
    RestoreFolder f;
    RestoreFolder::copy(f.satie, f.library);
    SessionState state = f.saved(f.bach);
    state.song_hash = 0;
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, f.request(state));
    const auto result = poll_within_five_seconds(restore, opener);
    REQUIRE(result.has_value());
    CHECK(result->source == SongSource::Library);
    CHECK(result->probe.hash_matches);
    CHECK(opener.started() == 1);
}

TEST_CASE("restore: a song only saved by its absolute path is read from there alone") {
    RestoreFolder f;
    RestoreFolder::copy(f.bach, f.absolute);
    SessionState state = f.saved(f.bach);
    state.song_path.clear();
    SavedSongRestore::Request request = f.request(state);
    request.library.reset(); // the library has no such file
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, std::move(request));
    const auto result = poll_within_five_seconds(restore, opener);
    REQUIRE(result.has_value());
    CHECK(result->source == SongSource::Absolute);
    CHECK(result->probe.hash_matches);
    CHECK(opener.started() == 1);
}

TEST_CASE("restore: a failed restore says so and leaves the opener and the app usable") {
    RestoreFolder f;
    // Nothing at either path: not found.
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, f.request(f.saved(f.bach)));
    auto missing = poll_within_five_seconds(restore, opener);
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->probe.found);
    CHECK_FALSE(missing->load.ok);
    CHECK_FALSE(missing->prepared.valid);
    CHECK(plan_restore(f.saved(f.bach), missing->probe.found, missing->probe.hash_matches, nullptr)
              .outcome == RestoreOutcome::SongMissing);

    // A file that is there but is not a song: found, not loaded.
    {
        std::ofstream(f.library, std::ios::binary) << "this is not a song";
    }
    SavedSongRestore garbage;
    garbage.begin(opener, f.request(f.saved(f.bach)));
    const auto unreadable = poll_within_five_seconds(garbage, opener);
    REQUIRE(unreadable.has_value());
    CHECK(unreadable->probe.found);
    CHECK_FALSE(unreadable->probe.loaded);
    CHECK_FALSE(unreadable->load.error.empty());
    CHECK(plan_restore(f.saved(f.bach), unreadable->probe.found, unreadable->probe.hash_matches,
                       nullptr)
              .outcome == RestoreOutcome::SongUnreadable);

    // Nothing is left running: a song asked for next opens as usual.
    CHECK_FALSE(garbage.active());
    CHECK_FALSE(opener.busy());
    opener.request(f.bach, {});
    const auto next = take_within_five_seconds(opener);
    REQUIRE(next.has_value());
    CHECK(next->load.ok);
}

TEST_CASE("restore: no candidate to read is decided at the first poll, without the opener") {
    RestoreFolder f;
    SessionState state = f.saved(f.bach);
    SavedSongRestore::Request request = f.request(state);
    request.library.reset();
    request.absolute.reset(); // the library said the file is not there
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, std::move(request));
    const auto result = restore.poll(opener);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->song.has_value());
    CHECK_FALSE(result->probe.found);
    CHECK(opener.started() == 0);
}

TEST_CASE("restore: polling never waits for a read, and a restore that is let go answers nothing") {
    Gate gate;
    SongOpener opener([&](const fs::path& path, const SongLoadOptions&) {
        gate.wait();
        return fake_song(path);
    });
    RestoreFolder f;
    SavedSongRestore restore;
    restore.begin(opener, f.request(f.saved(f.bach)));
    for (int frame = 0; frame < 5; ++frame) {
        CHECK_FALSE(restore.poll(opener).has_value()); // still reading: the frame goes on
    }
    CHECK(restore.active());
    // The player asks for another song meanwhile: the opener serves that, and the restore is let go.
    restore.abandon();
    CHECK_FALSE(restore.active());
    opener.request("other.mid", {});
    gate.open();
    const auto other = take_within_five_seconds(opener);
    REQUIRE(other.has_value());
    CHECK(other->path == "other.mid");
    CHECK_FALSE(restore.poll(opener).has_value());
}

TEST_CASE("restore: the tables for the saved session's hands and key come with the chosen song") {
    RestoreFolder f;
    RestoreFolder::copy(f.bach, f.library);
    SongController controller;
    SessionState state = f.saved(f.bach);
    state.practice_hands = HandFilter::Right;
    SavedSongRestore::Request request = f.request(state);
    request.plan = controller.load_plan_for(state);
    SongOpener opener;
    SavedSongRestore restore;
    restore.begin(opener, std::move(request));
    auto result = poll_within_five_seconds(restore, opener);
    REQUIRE(result.has_value());
    REQUIRE(result->prepared.valid);
    CHECK(result->prepared.hands == HandFilter::Right);
    controller.apply_session(state, 1'000'000'000);
    controller.load(std::move(result->load.song), 1'000'000'000, std::move(result->prepared));
    CHECK(result->prepared.flow.note_count() == 0); // taken over, the hands being the same
    std::vector<SongSoundEvent> out;
    controller.shutdown(1'000'000'000, out);
}

namespace {

// A MIDI file whose one track has no hand names and notes in both registers, so it needs the hand
// split; `seed` makes its content its own, so no other test has split it.
etude::test::Bytes unsplit_midi(int seed) {
    etude::test::Track track;
    track.name(0, "Piano");
    for (int i = 0; i < 24; ++i) {
        track.on(i == 0 ? 0u : 240u, 0, i % 2 == 0 ? 40 + (i % 5) : 76 + (i % 7), 60 + seed % 50);
        track.off(200, 0, i % 2 == 0 ? 40 + (i % 5) : 76 + (i % 7));
    }
    track.end();
    return etude::test::smf({track});
}

void write_bytes(const fs::path& path, const etude::test::Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("hand split: a file the library scan split is not split again when it is opened") {
    const fs::path dir = scratch_dir("split-once");
    const fs::path file = dir / "plain.mid";
    write_bytes(file, unsplit_midi(static_cast<int>(::getpid())));
    {
        // Sanity: the file does need the split, and the split puts both hands on it.
        const etude::MidiLoadResult raw = etude::load_midi(unsplit_midi(1));
        REQUIRE(raw.ok);
        REQUIRE(etude::needs_hand_split(raw.song));
    }
    const std::uint64_t before = etude::hand_split_runs();

    SongLibrary library({dir});
    library.scan();
    library.start_loading();
    library.wait_until_settled();
    REQUIRE(library.state().loaded() == 1);
    CHECK(etude::hand_split_runs() == before + 1); // the scan split it
    const SongEntry& entry = library.state().items[0].entry;
    CHECK(entry.left_notes > 0);
    CHECK(entry.right_notes > 0);

    // Opening parses the file again and takes the scan's split instead of making its own.
    SongOpener opener;
    opener.request(file, {});
    const auto opened = take_within_five_seconds(opener);
    REQUIRE(opened.has_value());
    REQUIRE(opened->load.ok);
    CHECK(opened->load.song.hands_split);
    CHECK(etude::hand_split_runs() == before + 1);
    // And the hands are what a split of the file gives.
    const etude::MidiLoadResult raw = etude::load_midi(unsplit_midi(static_cast<int>(::getpid())));
    REQUIRE(raw.ok);
    const etude::Song expected = etude::with_split_hands(raw.song);
    const std::uint64_t splits = etude::hand_split_runs(); // after the test's own split
    REQUIRE(opened->load.song.song.notes.size() == expected.notes.size());
    for (std::size_t i = 0; i < expected.notes.size(); ++i) {
        CHECK(opened->load.song.song.notes[i].hand == expected.notes[i].hand);
        CHECK(opened->load.song.song.notes[i].accompaniment == expected.notes[i].accompaniment);
    }
    // Another open of the same file is no new split either.
    opener.request(file, {});
    REQUIRE(take_within_five_seconds(opener).has_value());
    CHECK(etude::hand_split_runs() == splits);

    // A file that changed is not the one that was split: it is split again.
    write_bytes(file, unsplit_midi(static_cast<int>(::getpid()) + 7));
    opener.request(file, {});
    const auto changed = take_within_five_seconds(opener);
    REQUIRE(changed.has_value());
    REQUIRE(changed->load.ok);
    CHECK(etude::hand_split_runs() == splits + 1);
    fs::remove_all(dir);
}

namespace {
std::shared_ptr<const etude::HandSplit> split_of(std::size_t notes, std::uint64_t fingerprint) {
    auto split = std::make_shared<etude::HandSplit>();
    split->fingerprint = fingerprint;
    split->note_hands.resize(notes);
    return split;
}
} // namespace

TEST_CASE("hand split cache: a split for a key that is held replaces it, and the notes add up") {
    etude::app::HandSplitCache cache;
    const etude::app::HandSplitCache::Key key{42, false};
    cache.put(key, split_of(1'200'000, 1));
    cache.put(key, split_of(1'200'000, 2)); // the first one no longer fitted the file
    const auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found->fingerprint == 2);
    // The replaced split's notes were taken off the count: 1.2 + 0.7 million fit, nothing is evicted.
    cache.put({43, false}, split_of(700'000, 3));
    CHECK(cache.find(key) != nullptr);
    CHECK(cache.find({43, false}) != nullptr);
    // A third that does not fit pushes the oldest out.
    cache.put({44, false}, split_of(700'000, 4));
    CHECK(cache.find(key) == nullptr);
    CHECK(cache.find({44, false}) != nullptr);
}
