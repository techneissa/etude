// SPDX-License-Identifier: GPL-3.0-or-later
// The player's choice of where the notes divide between the hands: kept apart from the song the
// file gave, made again on a worker, taken over by the controller, and what the screen shows and
// the practice expects follows it. The cache keeps one split per file and per choice.
#include "hand_split_cache.hpp"
#include "hand_split_job.hpp"
#include "song_library.hpp"
#include "song_transport.hpp"
#include "ui/hands_row.hpp"

#include "smf_builder.hpp"

#include "timing_factor.hpp"
#include "etude/hand_split.hpp"
#include "etude/accidental_text.hpp"
#include "etude/midi_file.hpp"
#include "etude/pitch.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using etude::test::Track;

namespace {

constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t kSec = 1'000'000'000;

// Twelve notes, one every eighth (0.25 s), the left hand's register and the right's taking turns.
// Index i: 0 48, 1 72, 2 50, 3 74, 4 52, 5 76, 6 53, 7 77, 8 55, 9 79, 10 57, 11 81. No names, one
// track: the split decides every hand.
constexpr std::array<int, 12> kPitches{48, 72, 50, 74, 52, 76, 53, 77, 55, 79, 57, 81};

etude::test::Bytes unnamed_bytes() {
    Track t;
    for (std::size_t i = 0; i < kPitches.size(); ++i) {
        t.on(i == 0 ? 0 : 40, 0, kPitches[i]);
        t.off(200, 0, kPitches[i]);
    }
    return etude::test::smf({t});
}

// One track with the notes of the register `high` says, each at its place in the twelve.
Track named_track(const char* name, int channel, bool high) {
    Track t;
    t.name(0, name);
    std::uint32_t at = 0; // the tick of the last event written
    for (std::size_t i = 0; i < kPitches.size(); ++i) {
        if ((kPitches[i] >= 70) != high) {
            continue;
        }
        const std::uint32_t start = static_cast<std::uint32_t>(i) * 240;
        t.on(start - at, channel, kPitches[i]);
        t.off(200, channel, kPitches[i]);
        at = start + 200;
    }
    return t;
}

// The same notes with the hands named by two tracks.
etude::test::Bytes named_bytes() {
    return etude::test::smf(
        {named_track("Right Hand", 1, true), named_track("Left Hand", 0, false)});
}

LoadedSong open(const etude::test::Bytes& bytes, std::uint64_t* hash_out = nullptr) {
    SongFileLoad load = load_song_bytes(bytes, "split.mid");
    REQUIRE(load.ok);
    if (hash_out != nullptr) {
        *hash_out = load.song.hash;
    }
    return std::move(load.song);
}

std::vector<Hand> hands_of(const SongController& c) {
    std::vector<Hand> out;
    for (const Note& n : c.song().song.notes) {
        out.push_back(n.hand);
    }
    return out;
}

// Frames go by, the piano's events thrown away.
void run_for(SongController& c, std::int64_t& now, std::int64_t ms) {
    std::vector<SongSoundEvent> sink;
    for (std::int64_t t = 0; t < ms; t += 10) {
        now += 10 * kMs;
        c.update(now, {});
        c.take_sound_events(sink);
        sink.clear();
    }
}

void finish(SongController& c, std::int64_t now) {
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

// The song as its file gave it: the loaded song with the hands the file gave put back.
Song unsplit_of(const LoadedSong& loaded) {
    REQUIRE(loaded.file_hands != nullptr);
    Song song = loaded.song;
    REQUIRE(apply_hand_split(song, *loaded.file_hands, true));
    REQUIRE(song_fingerprint(song) == loaded.file_hands->fingerprint); // the file's song again
    return song;
}

} // namespace

TEST_CASE(
    "hand split choice: a song whose hands the split decided keeps the hands as the file gave") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    REQUIRE(c.hand_split_available());
    REQUIRE(c.song().file_hands != nullptr);
    for (const Hand hand : c.song().file_hands->note_hands) {
        CHECK(hand == Hand::Unknown); // as the file gave them
    }
    CHECK(c.hand_split() == kAutoSplit);
    // The split the library made is Auto's: two registers, so both hands.
    const std::vector<Hand> hands = hands_of(c);
    CHECK(hands[0] == Hand::Left);
    CHECK(hands[1] == Hand::Right);
    finish(c, now);
}

TEST_CASE("hand split choice: a file that names the hands has no split to change") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(named_bytes()), now);
    CHECK_FALSE(c.hand_split_available());
    CHECK(c.song().file_hands == nullptr);
    const std::vector<Hand> before = hands_of(c);
    c.set_hand_split(51, now);
    c.wait_hand_split(now);
    CHECK(c.hand_split() == 51); // the choice is kept, for the record,
    CHECK(hands_of(c) == before); // and changes nothing
    CHECK(c.hand_splits_applied() == 0);
    finish(c, now);
}

TEST_CASE("hand split choice: a fixed point makes the hands again, Auto brings the first back") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    const std::vector<Hand> autos = hands_of(c);
    const int auto_marker = c.hand_split_point();
    CHECK(auto_marker > 53);
    CHECK(auto_marker <= 72); // between the two registers

    c.set_hand_split(51, now);
    CHECK(c.hand_split_point() == 51);
    c.wait_hand_split(now);
    CHECK(c.hand_splits_applied() == 1);
    for (std::size_t i = 0; i < kPitches.size(); ++i) {
        CHECK(c.song().song.notes[i].hand == (kPitches[i] >= 51 ? Hand::Right : Hand::Left));
    }

    c.set_hand_split(kAutoSplit, now);
    c.wait_hand_split(now);
    CHECK(hands_of(c) == autos);
    CHECK(c.hand_split_point() == auto_marker);
    CHECK(c.hand_splits_applied() == 2);
    finish(c, now);
}

TEST_CASE("hand split choice: the marker stays where it was until Auto's hands are in") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    c.set_hand_split(51, now);
    c.wait_hand_split(now);
    c.set_hand_split(kAutoSplit, now);
    CHECK(c.hand_split_point() == 51);
    c.wait_hand_split(now);
    CHECK(c.hand_split_point() != 51);
    finish(c, now);
}

TEST_CASE("hand split choice: several moves in a row end on the last one") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    for (const int point : {50, 52, 54, 56, 58, 60, 62, 64, 51}) {
        c.set_hand_split(point, now);
    }
    c.wait_hand_split(now);
    c.update(now + 10 * kMs, {});
    CHECK(c.hand_split() == 51);
    for (std::size_t i = 0; i < kPitches.size(); ++i) {
        CHECK(c.song().song.notes[i].hand == (kPitches[i] >= 51 ? Hand::Right : Hand::Left));
    }
    finish(c, now);
}

TEST_CASE("hand split choice: the keys to press follow the new split, for the hand practised") {
    SongController c;
    std::int64_t now = kSec;
    c.set_mode(PracticeMode::Wait, now);
    c.load(open(unnamed_bytes()), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.seek(0.95, now); // after the 74 (0.75 s), before the 52 (1.0 s) and the 76 (1.25 s)
    c.play(now);
    run_for(c, now, 30);
    REQUIRE(c.expected_keys().size() == 1);
    CHECK(c.expected_keys()[0] == 76); // under Auto the 52 is the left hand's
    c.pause(now);

    c.set_hand_split(51, now);
    c.wait_hand_split(now);
    c.seek(0.95, now);
    c.play(now);
    run_for(c, now, 30);
    REQUIRE(c.expected_keys().size() == 1);
    CHECK(c.expected_keys()[0] == 52); // from 51 up it is the right hand's
    c.pause(now);

    // The other hand now has the notes under 51: the left hand waits for the 50, which is at
    // 0.5 s, so from the start it is the first note after the 48.
    c.set_practice_hands(HandFilter::Left, now);
    c.seek(0.3, now);
    c.play(now);
    run_for(c, now, 30);
    REQUIRE(c.expected_keys().size() == 1);
    CHECK(c.expected_keys()[0] == 50);
    finish(c, now);
}

TEST_CASE("hand split choice: a hand with no notes under the new split is not available") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    CHECK(c.hands_available().both());
    c.set_hand_split(40, now); // under every note: all right hand
    c.wait_hand_split(now);
    CHECK(c.hands_available().right);
    CHECK_FALSE(c.hands_available().left);
    // The left hand asked for is not practised: only the hand with notes is.
    c.set_practice_hands(HandFilter::Left, now);
    CHECK(c.practice_hands() == HandFilter::Right);
    c.set_hand_split(100, now); // above every note: all left hand
    c.wait_hand_split(now);
    CHECK_FALSE(c.hands_available().right);
    CHECK(c.practice_hands() == HandFilter::Left);
    finish(c, now);
}

TEST_CASE("hand split choice: the position, speed and loop stay where they were") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    c.set_speed(70, now);
    c.seek(1.0, now);
    CHECK(c.set_loop(0.5, 2.0, now));
    c.set_hand_split(51, now);
    c.wait_hand_split(now);
    CHECK(c.speed_percent() == 70);
    CHECK(c.position_s() == doctest::Approx(1.0).epsilon(0.01));
    CHECK(c.loop_enabled());
    CHECK(c.loop_start_s() == doctest::Approx(0.5));
    CHECK(c.loop_end_s() == doctest::Approx(2.0));
    finish(c, now);
}

TEST_CASE(
    "hand split choice: a song playing is held still by the choice, not left sounding old hands") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    c.set_song_sound(SongSound::BothHands, now);
    c.play(now);
    run_for(c, now, 100);
    c.set_hand_split(51, now);
    c.wait_hand_split(now);
    run_for(c, now, 100);
    CHECK(c.playing()); // the transport carries on
    // Nothing is left sounding from before the change.
    c.pause(now);
    run_for(c, now, 50);
    CHECK_FALSE(c.needs_silence());
    finish(c, now);
}

TEST_CASE("hand split choice: a new song forgets the choice and a split still on its way") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    c.set_hand_split(51, now);
    c.load(open(named_bytes()), now); // before the answer was taken
    CHECK(c.hand_split() == kAutoSplit);
    CHECK_FALSE(c.hand_split_available());
    const std::vector<Hand> hands = hands_of(c);
    c.update(now + 10 * kMs, {});
    CHECK(hands_of(c) == hands); // the old song's answer was not put on this one
    finish(c, now);
}

TEST_CASE("hand split choice: the practice record carries the choice") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    CHECK(c.practice().split_point == kAutoSplit);
    SongPractice p = c.practice();
    p.split_point = 55;
    c.apply_practice(p, now);
    CHECK(c.hand_split() == 55);
    CHECK(c.practice().split_point == 55);
    c.wait_hand_split(now);
    CHECK(c.song().song.notes[8].hand == Hand::Right); // the 55
    CHECK(c.song().song.notes[6].hand == Hand::Left);  // the 53
    finish(c, now);
}

TEST_CASE("hand split choice: the cache keeps a split for each file and each choice, made once") {
    std::uint64_t hash = 0;
    const LoadedSong loaded = open(unnamed_bytes(), &hash);
    const Song unsplit = unsplit_of(loaded);
    const std::uint64_t runs = hand_split_runs();
    const HandSplitOptions at46 = options_for_split_point(46);
    const HandSplitOptions at126 = options_for_split_point(126);

    const auto a = hand_split_for(unsplit, hash, false, at46);
    CHECK(hand_split_runs() == runs + 1);
    const auto again = hand_split_for(unsplit, hash, false, at46);
    CHECK(hand_split_runs() == runs + 1); // the same choice: found
    CHECK(again == a);

    const auto b = hand_split_for(unsplit, hash, false, at126);
    CHECK(hand_split_runs() == runs + 2); // another point: made
    CHECK(b != a);
    CHECK(b->note_hands != a->note_hands);

    const auto back = hand_split_for(unsplit, hash, false, at46);
    CHECK(hand_split_runs() == runs + 2); // back to the first: still there
    CHECK(back == a);

    // Another file with the same bytes held in the ABC chord setting is another entry.
    hand_split_for(unsplit, hash, true, at46);
    CHECK(hand_split_runs() == runs + 3);
}

TEST_CASE("hand split cache: every option is part of the key") {
    HandSplitOptions base;
    const auto key = [](const HandSplitOptions& o, bool abc = false, std::uint64_t hash = 7) {
        return HandSplitCache::key_of(hash, abc, o);
    };
    CHECK(key(base) == key(base));
    HandSplitOptions o = base;
    o.split_point = 61;
    CHECK(key(o) != key(base));
    o = base;
    o.method = SplitMethod::Fixed;
    CHECK(key(o) != key(base));
    o = base;
    o.single_lines_whole = false;
    CHECK(key(o) != key(base));
    o = base;
    o.by_pitch = true;
    CHECK(key(o) != key(base));
    CHECK(key(base, true) != key(base));
    CHECK(key(base, false, 8) != key(base));

    HandSplitCache cache;
    auto split = std::make_shared<HandSplit>();
    split->note_hands.resize(3);
    cache.put(key(base), split);
    CHECK(cache.find(key(base)) != nullptr);
    CHECK(cache.find(key(options_for_split_point(60))) == nullptr);
}

TEST_CASE("hand split choice: the worker's answer for a split point is the one the core makes") {
    std::uint64_t hash = 0;
    const LoadedSong song = open(unnamed_bytes(), &hash);
    const Song unsplit = unsplit_of(song);
    HandSplitJob job;
    HandSplitRequest request;
    request.unsplit = std::make_shared<const Song>(unsplit);
    request.hash = hash;
    request.split_point = 56;
    request.serial = 9;
    job.request(request);
    job.wait_idle();
    std::optional<HandSplitResult> done = job.take();
    REQUIRE(done.has_value());
    CHECK(done->serial == 9);
    CHECK(done->split_point == 56);
    CHECK(done->prepared.valid);
    const std::vector<Hand> expected = split_hands(unsplit, options_for_split_point(56));
    REQUIRE(done->song.notes.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(done->song.notes[i].hand == expected[i]);
    }
    CHECK_FALSE(job.take().has_value()); // taken once
    // A cancelled job's answer never arrives.
    job.request(request);
    job.cancel();
    job.wait_idle();
    CHECK_FALSE(job.take().has_value());
}

namespace {

// The song as a controller holds it: split, with the hands the file gave kept apart.
struct Lending {
    LoadedSong loaded;
    std::uint64_t hash = 0;
    std::shared_ptr<LentSong> lent = std::make_shared<LentSong>();
    Lending() {
        loaded = open(unnamed_bytes(), &hash);
        lent->song = &loaded.song;
        lent->generation = 1;
    }
    HandSplitRequest request(int split_point) const {
        HandSplitRequest r;
        r.file_hands = loaded.file_hands;
        r.lent = lent;
        r.generation = lent->generation;
        r.hash = hash;
        r.split_point = split_point;
        return r;
    }
};

} // namespace

TEST_CASE("hand split job: a lent song is split again from the hands the file gave") {
    Lending l;
    // The lent song has a split on it already (Auto's); the answer is nevertheless the core's
    // split of the song as the file gave it.
    const Song unsplit = unsplit_of(l.loaded);
    HandSplitJob job;
    for (const int point : {56, 70, kAutoSplit}) {
        job.request(l.request(point));
        job.wait_idle();
        std::optional<HandSplitResult> done = job.take();
        REQUIRE(done.has_value());
        const std::vector<Hand> expected = split_hands(unsplit, options_for_split_point(point));
        REQUIRE(done->song.notes.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK(done->song.notes[i].hand == expected[i]);
        }
    }
    // The lent song itself is not touched.
    CHECK(song_fingerprint(l.loaded.song) != song_fingerprint(unsplit));
}

TEST_CASE("hand split job: a song replaced since the request, or not the file's, makes no answer") {
    Lending l;
    HandSplitJob job;
    HandSplitRequest old = l.request(56);
    ++l.lent->generation; // another song was loaded
    job.request(old);
    job.wait_idle();
    CHECK_FALSE(job.take().has_value());

    job.request(l.request(56)); // the lent song is there and fits
    job.wait_idle();
    CHECK(job.take().has_value());

    l.lent->song = nullptr; // unloaded
    job.request(l.request(56));
    job.wait_idle();
    CHECK_FALSE(job.take().has_value());

    // Hands that are not this song's: the copy does not come back as the file's song.
    Song other = l.loaded.song;
    other.notes[3].pitch = static_cast<std::uint8_t>(other.notes[3].pitch + 1);
    l.lent->song = &other;
    job.request(l.request(56));
    job.wait_idle();
    CHECK_FALSE(job.take().has_value());
}

TEST_CASE("hand split job: a lent song is copied under its lock, and not before it is free") {
    Lending l;
    HandSplitJob job;
    std::unique_lock held(l.lent->mutex); // the frame thread is writing the song
    job.request(l.request(56));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(job.busy()); // the worker waits for the song
    CHECK_FALSE(job.take().has_value());
    held.unlock();
    job.wait_idle();
    CHECK(job.take().has_value());
}

TEST_CASE(
    "hand split choice: a split is not taken over while a worker copies the song, and then is") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    const std::vector<Hand> at_start = hands_of(c);
    c.set_hand_split(56, now);
    for (int i = 0; i < 500 && c.hand_split_working(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE_FALSE(c.hand_split_working()); // the answer is ready, not yet taken over
    std::atomic<bool> holding{false};
    std::atomic<bool> release{false};
    std::thread copier([&] { // a copy in flight on another thread
        const std::lock_guard lock(c.lent_song()->mutex);
        holding = true;
        for (int i = 0; i < 10000 && !release; ++i) { // 20 s at most: an update that waits for it is slow
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    while (!holding) {
        std::this_thread::yield();
    }
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        c.update(now += 16 * kMs, {}); // never waits for the copy, never writes the song
    }
    const auto took = std::chrono::steady_clock::now() - begin;
    CHECK(took < std::chrono::seconds(10)); // a wait for the copy would run to its 20 s
    CHECK_TIMING(took < std::chrono::milliseconds(500));
    CHECK(c.hand_splits_applied() == 0);
    CHECK(hands_of(c) == at_start);
    release = true;
    copier.join();
    c.update(now += 16 * kMs, {});
    CHECK(c.hand_splits_applied() == 1);
    CHECK(hands_of(c) != at_start);
    finish(c, now);
}

TEST_CASE("hand split choice: a load or an unload waits for a copy of the song in flight") {
    SongController c;
    std::int64_t now = kSec;
    c.load(open(unnamed_bytes()), now);
    for (const bool unload : {false, true}) {
        std::atomic<bool> holding{false};
        std::atomic<bool> done_copying{false};
        std::thread copier([&] { // a copy in flight on another thread
            const std::lock_guard lock(c.lent_song()->mutex);
            holding = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            done_copying = true;
        });
        while (!holding) {
            std::this_thread::yield();
        }
        if (unload) {
            c.unload(now);
        } else {
            c.load(open(unnamed_bytes()), now);
        }
        CHECK(done_copying); // the song was not written under the copy
        copier.join();
        CHECK(c.lent_song()->song == (unload ? nullptr : &c.song().song));
    }
    finish(c, now);
}

TEST_CASE(
    "hand split choice: splits asked in a burst, with frames and a load in between, end right") {
    // Real copies on the worker against the frame thread's polls and its loads of another song
    // (the test the thread sanitizer runs): a song long enough that a copy is in flight when the
    // frame thread acts.
    Track t;
    for (int i = 0; i < 60000; ++i) {
        const int pitch = i % 2 == 0 ? 40 + i % 9 : 70 + i % 11;
        t.on(i == 0 ? 0 : 40, 0, pitch);
        t.off(200, 0, pitch);
    }
    const etude::test::Bytes bytes = etude::test::smf({t});
    SongController c;
    std::int64_t now = kSec;
    LoadedSong first = open(bytes);
    const Song unsplit = unsplit_of(first);
    c.load(std::move(first), now);
    for (int round = 0; round < 12; ++round) {
        LoadedSong next = open(bytes);
        c.set_hand_split(40 + round % 45, now);
        if (round % 2 == 1) {
            // The next song comes while the worker is at its copy of this one: the lock is taken
            // then, so the frame thread sees it busy and the load waits for the copy to end.
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < limit) {
                if (!c.lent_song()->mutex.try_lock()) {
                    break;
                }
                c.lent_song()->mutex.unlock();
                std::this_thread::yield();
            }
            c.load(std::move(next), now); // a split on its way for the old song is dropped
            CHECK(c.hand_split() == kAutoSplit);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1 + round % 3));
            c.update(now += 16 * kMs, {});
        }
    }
    c.set_hand_split(57, now);
    c.wait_hand_split(now);
    const std::vector<Hand> expected = split_hands(unsplit, options_for_split_point(57));
    const std::vector<Hand> hands = hands_of(c);
    REQUIRE(hands.size() == expected.size());
    CHECK(hands == expected);
    finish(c, now);
}

// ---- What Settings > Practice's Hands row shows and does to the song (through the UI's port)

namespace {

// A song and its transport, the row's side of the song.
struct HandsRow {
    SongController songs;
    SongTransport transport{songs};
    std::int64_t now = kSec;
    explicit HandsRow(const etude::test::Bytes& bytes) {
        songs.load(open(bytes), now);
        transport.update(now);
    }
    ~HandsRow() {
        std::vector<SongSoundEvent> out;
        songs.shutdown(now, out);
    }
    void settle() {
        songs.wait_hand_split(now);
        transport.update(now);
    }
};

// Two unnamed tracks, one a hand each: the staves decide.
etude::test::Bytes two_tracks() {
    Track low;
    Track high;
    for (int i = 0; i < 8; ++i) {
        low.on(i == 0 ? 0 : 40, 0, 48 + i);
        low.off(200, 0, 48 + i);
        high.on(i == 0 ? 0 : 40, 1, 72 + i);
        high.off(200, 1, 72 + i);
    }
    return etude::test::smf({low, high});
}

} // namespace

TEST_CASE("hands row: a song that gives no hands shows the row, a song that names them does not") {
    HandsRow unnamed(unnamed_bytes());
    CHECK(unnamed.transport.view().split_available);
    HandsRow named(named_bytes());
    CHECK_FALSE(named.transport.view().split_available);
}

TEST_CASE("hands row: from the sheet says which rule applied") {
    // One track: the smart split divides the one staff, about where the registers part.
    HandsRow one(unnamed_bytes());
    const ui::SongView& v = one.transport.view();
    CHECK_FALSE(v.split_fixed);
    CHECK_FALSE(v.split_by_staves);
    CHECK(v.split_key > 53);
    CHECK(v.split_key <= 72);
    // Two tracks: their staves are the hands.
    HandsRow two(two_tracks());
    CHECK_FALSE(two.transport.view().split_fixed);
    CHECK(two.transport.view().split_by_staves);
}

TEST_CASE("hands row: Split at places the split, the stepper moves it, From the sheet clears it") {
    HandsRow r(unnamed_bytes());
    const int start = r.transport.view().split_key;
    r.transport.set_split_key(start, r.now); // the cell "Split at": the line stays where it was
    r.settle();
    CHECK(r.songs.hand_split() == start);
    CHECK(r.transport.view().split_fixed);
    r.transport.set_split_key(start + 1, r.now); // "+"
    r.settle();
    CHECK(r.songs.hand_split() == start + 1);
    CHECK(r.transport.view().split_key == start + 1);
    for (std::size_t i = 0; i < kPitches.size(); ++i) {
        CHECK(r.songs.song().song.notes[i].hand ==
              (kPitches[i] >= start + 1 ? Hand::Right : Hand::Left));
    }
    r.transport.reset_split(r.now); // the cell "From the sheet"
    r.settle();
    CHECK(r.songs.hand_split() == kAutoSplit);
    CHECK_FALSE(r.transport.view().split_fixed);
    CHECK(r.transport.view().split_key == start);
}

TEST_CASE("hands row: the key shown is the keyboard's, the split stays with the song's pitches") {
    HandsRow r(unnamed_bytes());
    r.songs.set_transpose(2, r.now);
    r.transport.update(r.now);
    r.transport.set_split_key(62, r.now); // key D4 plays the song's C4
    r.settle();
    CHECK(r.songs.hand_split() == 60);
    CHECK(r.transport.view().split_key == 62);
    r.songs.set_transpose(-12, r.now);
    r.transport.update(r.now);
    CHECK(r.songs.hand_split() == 60);
    CHECK(r.transport.view().split_key == 48);
}

TEST_CASE("hands row: a song with no row has no split to place") {
    HandsRow r(named_bytes());
    const std::vector<Hand> before = hands_of(r.songs);
    r.transport.set_split_key(60, r.now);
    r.transport.reset_split(r.now);
    r.settle();
    CHECK(r.songs.hand_split() == kAutoSplit);
    CHECK(hands_of(r.songs) == before);
}

TEST_CASE("hands row: the sheet is engraved again from the new hands") {
    HandsRow r(unnamed_bytes());
    const std::uint32_t before = r.songs.notes_serial();
    r.transport.set_split_key(60, r.now);
    r.settle();
    CHECK(r.songs.notes_serial() != before);
}

// Two unnamed tracks whose staves and whose best straight line disagree: the lower track climbs to
// 70 over the upper one's 62.
etude::test::Bytes crossing_tracks() {
    Track low;
    Track high;
    const int lows[] = {48, 52, 70, 55, 50, 70, 53, 57};
    const int highs[] = {74, 76, 62, 79, 77, 62, 81, 78};
    for (int i = 0; i < 8; ++i) {
        low.on(i == 0 ? 0 : 40, 0, lows[i]);
        low.off(200, 0, lows[i]);
        high.on(i == 0 ? 0 : 40, 1, highs[i]);
        high.off(200, 1, highs[i]);
    }
    return etude::test::smf({low, high});
}

// What the row does with a port: the taps of hands_row.hpp applied.
void apply_outcome(HandsRow& r, const ui::HandsOutcome& out) {
    if (out.action == ui::HandsAction::Place) {
        r.transport.set_split_key(out.key, r.now);
    } else if (out.action == ui::HandsAction::Reset) {
        r.transport.reset_split(r.now);
    }
    r.settle();
}

TEST_CASE("hands row: choosing Split at moves no note until the first step") {
    HandsRow r(crossing_tracks());
    REQUIRE(r.transport.view().split_by_staves);
    const std::vector<Hand> from_staves = hands_of(r.songs);
    // The straight line that fits the staves best cuts the climbing track: placing it would move
    // notes, so the choice waits.
    ui::HandsRowState state;
    const ui::SongView& view = r.transport.view();
    CHECK_FALSE(ui::hands_split_shown(view, state));
    apply_outcome(r, ui::hands_choose(view, state, true));
    CHECK(ui::hands_split_shown(view, state)); // the cell shows selected, with its stepper,
    CHECK_FALSE(view.split_fixed);
    CHECK(hands_of(r.songs) == from_staves); // and nothing moved
    CHECK(r.songs.hand_split() == kAutoSplit);
    // The first step places the split a key from where the row stood.
    const int start = view.split_key;
    apply_outcome(r, ui::hands_step(view, state, 1));
    CHECK(view.split_fixed);
    CHECK(view.split_key == start + 1);
    CHECK(hands_of(r.songs) != from_staves);
    CHECK_FALSE(state.pending(view));
    // From the sheet puts the staves' hands back.
    apply_outcome(r, ui::hands_choose(view, state, false));
    CHECK(hands_of(r.songs) == from_staves);
}

TEST_CASE("hands row: a Split at chosen on one song is not carried to the next") {
    HandsRow r(unnamed_bytes());
    ui::HandsRowState state;
    ui::hands_choose(r.transport.view(), state, true);
    REQUIRE(state.pending(r.transport.view()));
    r.songs.load(open(two_tracks()), r.now);
    r.transport.update(r.now);
    CHECK_FALSE(state.pending(r.transport.view()));
}

TEST_CASE("hands row: the stepper stops at the song's pitches 1 and 127, whatever the transposition") {
    HandsRow r(unnamed_bytes());
    r.songs.set_transpose(12, r.now);
    r.transport.update(r.now);
    r.transport.set_split_key(13, r.now); // key 13 is the song's pitch 1
    r.settle();
    REQUIRE(r.songs.hand_split() == 1);
    CHECK_FALSE(ui::hands_can_step(r.transport.view(), -1)); // "-" would snap back to pitch 1
    CHECK(ui::hands_can_step(r.transport.view(), 1));
    r.songs.set_transpose(-12, r.now);
    r.transport.update(r.now);
    r.transport.set_split_key(127 - 12, r.now);
    r.settle();
    REQUIRE(r.songs.hand_split() == 127);
    CHECK_FALSE(ui::hands_can_step(r.transport.view(), 1));
    CHECK(ui::hands_can_step(r.transport.view(), -1));
}

TEST_CASE("hands row: a key name is spelled from the song's key, flats in a flat key") {
    const auto bytes_in = [](int sharps) {
        Track t;
        t.key_sig(0, sharps, false);
        for (std::size_t i = 0; i < kPitches.size(); ++i) {
            t.on(i == 0 ? 0 : 40, 0, kPitches[i]);
            t.off(200, 0, kPitches[i]);
        }
        return etude::test::smf({t});
    };
    const auto name_of = [](const HandsRow& r) {
        return accidental_display(note_name(r.transport.view().split_key, r.transport.view().split_flats));
    };
    HandsRow flat(bytes_in(-3)); // E flat major
    flat.transport.set_split_key(58, flat.now);
    flat.settle();
    CHECK(flat.transport.view().split_flats);
    CHECK(name_of(flat) == "B\xE2\x99\xAD" "3");
    HandsRow sharp(bytes_in(2)); // D major
    sharp.transport.set_split_key(58, sharp.now);
    sharp.settle();
    CHECK_FALSE(sharp.transport.view().split_flats);
    CHECK(name_of(sharp) == "A\xE2\x99\xAF" "3");
}

TEST_CASE("hand split cache: past its room the oldest go first, the replaced do not count twice") {
    const auto split_of = [](std::size_t notes) {
        auto split = std::make_shared<HandSplit>();
        split->note_hands.resize(notes);
        return split;
    };
    const auto key = [](int point) {
        return HandSplitCache::key_of(9, false, options_for_split_point(point));
    };
    HandSplitCache cache;
    cache.put(key(50), split_of(900'000));
    cache.put(key(51), split_of(900'000));
    cache.put(key(52), split_of(900'000)); // 2.7 million: the oldest goes
    CHECK(cache.find(key(50)) == nullptr);
    CHECK(cache.find(key(51)) != nullptr);
    CHECK(cache.find(key(52)) != nullptr);
    // The same key again takes its own notes off the count first: no eviction for the swap.
    cache.put(key(52), split_of(900'000));
    CHECK(cache.find(key(51)) != nullptr);
    CHECK(cache.find(key(52)) != nullptr);
    // One split bigger than the whole room is still kept (the last entry is never dropped), and it
    // pushes every older one out.
    cache.put(key(53), split_of(2'500'000));
    CHECK(cache.find(key(53)) != nullptr);
    CHECK(cache.find(key(51)) == nullptr);
    CHECK(cache.find(key(52)) == nullptr);
    // Another file's Auto split is another entry and goes by the same order.
    cache.put(HandSplitCache::key_of(10, false, HandSplitOptions{}), split_of(100));
    CHECK(cache.find(key(53)) == nullptr);
    CHECK(cache.find(HandSplitCache::key_of(10, false, HandSplitOptions{})) != nullptr);
}
