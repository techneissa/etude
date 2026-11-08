// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: the count-in's sound, a new song's defaults, 6/8 beats, tempo, speed, and allocation-free playback.
#include "alloc_count.hpp"
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

namespace {

// `bars` bars of 6/8 at 120 quarter notes a minute: a bar is 1.5 s, a dotted-quarter beat 0.75 s.
LoadedSong compound_song(int bars = 4) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    s.time_signatures = {TimeSignature{0, 6, 8}};
    for (int i = 0; i < bars * 2; ++i) {
        Note n;
        n.start_tick = i * 720;
        n.end_tick = n.start_tick + 600;
        n.pitch = 72;
        n.velocity = 80;
        n.hand = Hand::Right;
        s.notes.push_back(n);
    }
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Six-eight";
    out.hash = 0x6868;
    out.library_path = "six-eight.mid";
    return out;
}

} // namespace

TEST_CASE("count-in: it sounds with the metronome off, at the metronome's volume and voice") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(1);
    SongOptions o;
    o.metronome = false;
    o.click_volume = 35;
    c.set_options(o, now);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, t0 + 2 * kSec + 100 * kMs);
    REQUIRE(r.clicks.size() == 4);
    const ClickSound sound = click_sound_for(ClickVoice::WoodBlock, 35);
    REQUIRE(sound.enabled);
    CHECK(r.clicks[0].velocity == sound.accent_velocity);
    for (std::size_t i = 1; i < 4; ++i) {
        CHECK(r.clicks[i].velocity == sound.velocity);
    }
    // Louder at a higher volume: the same count-in at 70 is not the same velocity.
    CHECK(click_sound_for(ClickVoice::WoodBlock, 70).accent_velocity > sound.accent_velocity);
    // No metronome clicks follow: only the four of the count-in.
    r.until(c, now, t0 + 5 * kSec);
    CHECK(r.clicks.size() == 4);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: a click volume of 0 is a count-in you only see") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(1);
    SongOptions o;
    o.click_volume = 0;
    c.set_options(o, now);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    int labelled = 0;
    int last = 0;
    while (now < t0 + 2 * kSec - 20 * kMs) {
        r.frame(c, now);
        if (c.count_in_label() != last) {
            last = c.count_in_label();
            ++labelled;
        }
    }
    CHECK(labelled == 4); // the numbers 1 2 3 4 still show
    r.until(c, now, t0 + 3 * kSec);
    CHECK(r.clicks.empty());
    CHECK(c.state() == TransportState::Playing);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE(
    "resume countdown: it clicks with the metronome off, at the metronome volume; 0 is silent") {
    for (const bool counted_in : {true, false}) {
        for (const int volume : {70, 30, 0}) {
            CAPTURE(counted_in);
            CAPTURE(volume);
            SongController c;
            std::int64_t now = kSec;
            c.load(two_hand_song(8), now);
            c.set_count_in_bars(counted_in ? 1 : 0);
            SongOptions o;
            o.metronome = false;
            o.click_volume = volume;
            o.resume_countdown_s = 3;
            c.set_options(o, now);
            ClickRun r;
            c.seek(2.0, now);
            c.play(now);
            r.collect(c, now);
            r.until(c, now, now + 300 * kMs);
            c.menu_opened(now);
            r.collect(c, now);
            const std::size_t before = r.clicks.size();
            c.menu_closed(now);
            r.collect(c, now);
            CHECK(c.counting_down() == !counted_in);
            CHECK((c.state() == TransportState::CountIn) == counted_in);
            r.until(c, now, now + 3 * kSec + 100 * kMs);
            const std::size_t made = r.clicks.size() - before;
            if (volume == 0) {
                CHECK(made == 0);
            } else {
                CHECK(made == (counted_in ? 4u : 3u)); // a bar of beats, or a tick a second
                const ClickSound sound = click_sound_for(ClickVoice::WoodBlock, volume);
                for (std::size_t i = before; i < r.clicks.size(); ++i) {
                    CHECK(r.clicks[i].velocity > 0);
                    CHECK(r.clicks[i].velocity <= sound.accent_velocity);
                }
            }
            std::vector<SongSoundEvent> out;
            c.shutdown(now, out);
        }
    }
}

TEST_CASE("a different song starts at 100% and the default count-in; the same session restores") {
    SongController c;
    std::int64_t now = kSec;
    SongOptions o;
    o.default_count_in_bars = 1;
    c.set_options(o, now);
    c.load(two_hand_song(4), now); // song A
    CHECK(c.speed_percent() == 100);
    CHECK(c.count_in_bars() == 1);
    c.set_speed(70, now);
    c.set_count_in_bars(2);
    CHECK(c.speed_percent() == 70);
    CHECK(c.count_in_bars() == 2);
    LoadedSong b = two_hand_song(6);
    b.library_path = "b.mid";
    b.hash = 0xb;
    c.load(std::move(b), now); // song B
    CHECK(c.speed_percent() == 100);
    CHECK(c.count_in_bars() == 1);
    c.set_speed(130, now);
    c.set_count_in_bars(0);
    LoadedSong a = two_hand_song(4);
    c.load(std::move(a), now); // back to A: nothing is kept per song yet
    CHECK(c.speed_percent() == 100);
    CHECK(c.count_in_bars() == 1);
    // Restoring the session at startup, after the load, brings the speed and count-in back.
    SessionState saved;
    saved.speed_percent = 83;
    saved.count_in_bars = 2;
    c.apply_session(saved, now);
    CHECK(c.speed_percent() == 83);
    CHECK(c.count_in_bars() == 2);
    // The session writes what is set.
    SessionState written;
    c.write_session_state(written);
    CHECK(written.speed_percent == 83);
    CHECK(written.count_in_bars == 2);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE(
    "--song: the session's own song gets its speed, count-in, hands and transposition back, another song does not") {
    SessionState saved;
    saved.song_hash = 0x1234; // what two_hand_song() hashes to
    saved.speed_percent = 83;
    saved.count_in_bars = 2;
    saved.practice_hands = HandFilter::Left;
    saved.transpose_semitones = 2;
    SongOptions o;
    o.default_count_in_bars = 1;
    {
        // The same file (same content hash): as a normal startup restores it.
        SongController c;
        std::int64_t now = kSec;
        c.set_options(o, now);
        c.load(two_hand_song(4), now);
        CHECK(c.speed_percent() == 100);
        c.restore_song_settings(saved, now);
        CHECK(c.speed_percent() == 83);
        CHECK(c.count_in_bars() == 2);
        CHECK(c.practice().hands == HandFilter::Left);
        CHECK(c.transpose() == 2);
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
    {
        // Another file: the defaults a load gives.
        SongController c;
        std::int64_t now = kSec;
        c.set_options(o, now);
        LoadedSong other = two_hand_song(4);
        other.hash = 0x5678;
        c.load(std::move(other), now);
        c.restore_song_settings(saved, now);
        CHECK(c.speed_percent() == 100);
        CHECK(c.count_in_bars() == 1);
        CHECK(c.practice().hands == HandFilter::Both);
        CHECK(c.transpose() == 0);
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
    {
        // A session with no song (hash 0) restores nothing, whatever the loaded song hashes to.
        SongController c;
        std::int64_t now = kSec;
        c.set_options(o, now);
        LoadedSong zero = two_hand_song(4);
        zero.hash = 0;
        c.load(std::move(zero), now);
        SessionState none = saved;
        none.song_hash = 0;
        c.restore_song_settings(none, now);
        CHECK(c.speed_percent() == 100);
        CHECK(c.count_in_bars() == 1);
        // And with no song loaded at all.
        SongController empty;
        empty.restore_song_settings(saved, now);
        CHECK(empty.speed_percent() == 100);
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
    }
}

TEST_CASE("count-in: Home while the song plays in Wait Mode, mid-song, restarts from the start "
          "with one") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(8), now);
    c.set_mode(PracticeMode::Wait, now);
    c.set_count_in_bars(1);
    ClickRun r;
    c.seek(8.0, now); // mid-song
    c.play(now);      // not the start: no count-in
    r.collect(c, now);
    CHECK(c.state() != TransportState::CountIn);
    CHECK(c.playing());
    r.until(c, now, now + 500 * kMs);
    CHECK(c.position_s() >= 8.0);
    const std::size_t clicks_before = r.clicks.size();
    c.replay(now); // Home, while it plays
    r.collect(c, now);
    CHECK(c.position_s() == 0.0);
    CHECK(c.playing());
    CHECK(c.state() == TransportState::CountIn); // the start of the song: it counts in
    const std::int64_t t0 = now;
    r.until(c, now, t0 + 2 * kSec + 100 * kMs);
    CHECK(r.clicks.size() == clicks_before + 4);
    CHECK(c.state() != TransportState::CountIn);
    CHECK(c.playing());
    // With a loop in the middle, Home goes to the loop start: not the song's start, no count-in.
    REQUIRE(c.set_loop(4.0, 8.0, now));
    c.replay(now);
    CHECK(c.position_s() == doctest::Approx(4.0));
    CHECK(c.playing());
    CHECK(c.state() != TransportState::CountIn);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a controller with no default count-in starts a new song with none") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(2);
    c.load(two_hand_song(4), now);
    CHECK(c.count_in_bars() == 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("6/8: the count-in counts the felt beats, one and two bars") {
    SongController c;
    std::int64_t now = kSec;
    c.load(compound_song(), now);
    c.set_count_in_bars(1);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    CHECK(c.count_in_beats() == 2);
    CHECK(c.count_in_bar_beats() == 2); // two dots, not the four of 4/4
    std::vector<int> labels;
    while (now < t0 + 1500 * kMs - 20 * kMs) {
        r.frame(c, now);
        if (labels.empty() || labels.back() != c.count_in_label()) {
            labels.push_back(c.count_in_label());
        }
    }
    CHECK(labels == std::vector<int>{1, 2});
    r.until(c, now, t0 + 1600 * kMs);
    REQUIRE(r.clicks.size() == 2);
    CHECK(std::llabs(r.clicks[0].due_ns - t0) <= 2);
    CHECK(std::llabs(r.clicks[1].due_ns - (t0 + 750 * kMs)) <= 2); // a dotted quarter apart
    CHECK(r.clicks[0].velocity > r.clicks[1].velocity);
    c.pause(now);
    c.seek(0.0, now);
    c.set_count_in_bars(2);
    c.play(now);
    CHECK(c.count_in_beats() == 4);
    CHECK(c.count_in_bar_beats() == 2); // two bars of two beats: the dots are one bar's
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("6/8: the metronome clicks the dotted quarter, Divide its eighths, Triplets the same") {
    const auto clicks_in_first_bar = [](ClickSubdivision sub) {
        SongController c;
        std::int64_t now = kSec;
        c.load(compound_song(), now);
        SongOptions o;
        o.metronome = true;
        o.metronome_grid.subdivision = sub;
        c.set_options(o, now);
        ClickRun r;
        const std::int64_t t0 = now;
        c.play(now);
        r.collect(c, now);
        r.until(c, now, t0 + 1500 * kMs - 40 * kMs);
        std::vector<std::int64_t> due;
        for (const HandedClick& h : r.clicks) {
            if (h.due_ns - t0 <
                1490 * kMs) { // the first bar's; the next bar's beat is handed early
                due.push_back(h.due_ns - t0);
            }
        }
        std::vector<SongSoundEvent> out;
        c.shutdown(now, out);
        return due;
    };
    const auto beats = clicks_in_first_bar(ClickSubdivision::Off);
    REQUIRE(beats.size() == 2);
    CHECK(std::llabs(beats[1] - 750 * kMs) <= 2);
    const auto divided = clicks_in_first_bar(ClickSubdivision::Divide);
    REQUIRE(divided.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(std::llabs(divided[i] - static_cast<std::int64_t>(i) * 250 * kMs) <= 2);
    }
    CHECK(clicks_in_first_bar(ClickSubdivision::Triplets) == divided);
}

TEST_CASE("6/8: the chip's tempo is in dotted quarters and a typed tempo means them") {
    SongController c;
    std::int64_t now = kSec;
    CHECK_FALSE(c.tempo_beat_dotted());
    c.load(compound_song(), now); // 120 quarter notes a minute is 80 dotted quarters
    CHECK(c.tempo_beat_dotted());
    CHECK(c.tempo_bpm() == doctest::Approx(80.0));
    CHECK(c.main_tempo_bpm() == doctest::Approx(80.0));
    c.set_speed(70, now);
    CHECK(c.tempo_bpm() == doctest::Approx(56.0)); // "dotted quarter 56"
    c.set_target_bpm(40.0, now);
    CHECK(c.speed_percent() == 50);
    c.set_target_bpm(80.0, now);
    CHECK(c.speed_percent() == 100);
    c.set_target_bpm(56.0, now);
    CHECK(c.speed_percent() == 70);
    // A 4/4 song: quarters again.
    c.load(two_hand_song(4), now);
    CHECK_FALSE(c.tempo_beat_dotted());
    CHECK(c.tempo_bpm() == doctest::Approx(120.0));
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("speed is a whole percent: a typed tempo lands within a percent, the buttons step by 5") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now); // 120 BPM
    c.set_target_bpm(100.0, now);
    CHECK(c.speed_percent() == 83);
    CHECK(display_bpm(c.tempo_bpm()) == 100);
    c.change_speed(5, now);
    CHECK(c.speed_percent() == 85); // the next multiple of 5
    c.set_target_bpm(100.0, now);
    c.change_speed(-5, now);
    CHECK(c.speed_percent() == 80);
    c.set_speed(87, now);
    CHECK(c.speed_percent() == 87);
    c.set_speed(250, now);
    CHECK(c.speed_percent() == 200);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("count-in: a click volume of 0 counts in without a sound") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(1);
    SongOptions o;
    o.click_volume = 0;
    c.set_options(o, now);
    ClickRun r;
    c.play(now);
    r.collect(c, now);
    CHECK(c.state() == TransportState::CountIn);
    r.until(c, now, now + 2 * kSec + 100 * kMs);
    CHECK(r.clicks.empty());
    CHECK(c.state() == TransportState::Playing);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("beat pulse: the age of the last beat, for the pulse on the hit line") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_options(click_options(true), now);
    CHECK_FALSE(c.beat_pulse().active);
    ClickRun r;
    const std::int64_t t0 = now;
    c.play(now);
    r.collect(c, now);
    r.until(c, now, t0 + 750 * kMs);
    SongController::BeatPulse p = c.beat_pulse();
    CHECK(p.active);
    CHECK(p.age_s == doctest::Approx(0.25).epsilon(0.1)); // 0.75 s in: the beat at 0.5 s
    CHECK_FALSE(p.accent);
    r.until(c, now, t0 + 2 * kSec + 50 * kMs);
    p = c.beat_pulse();
    CHECK(p.accent); // beat one of bar 2
    CHECK(p.age_s < 0.1);
    c.pause(now);
    CHECK_FALSE(c.beat_pulse().active);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("tempo: the chip's number and a typed tempo, with the speed up to 200%") {
    SongController c;
    std::int64_t now = kSec;
    CHECK(c.tempo_bpm() == 0.0);
    c.load(two_hand_song(4), now); // 120 BPM
    CHECK(c.tempo_bpm() == doctest::Approx(120.0));
    CHECK(c.main_tempo_bpm() == doctest::Approx(120.0));
    c.set_speed(75, now);
    CHECK(c.tempo_bpm() == doctest::Approx(90.0));
    c.set_target_bpm(60.0, now);
    CHECK(c.speed_percent() == 50);
    c.set_target_bpm(240.0, now);
    CHECK(c.speed_percent() == 200);
    c.set_target_bpm(1000.0, now);
    CHECK(c.speed_percent() == 200);
    c.change_speed(5, now);
    CHECK(c.speed_percent() == 200);
    CHECK(c.tempo_bpm() == doctest::Approx(240.0));
    c.set_speed(10, now);
    CHECK(c.speed_percent() == 25);
}

TEST_CASE("session: the count-in and the speed up to 200% are saved and restored") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_count_in_bars(2);
    c.set_speed(200, now);
    SessionState s;
    c.write_session_state(s);
    CHECK(s.count_in_bars == 2);
    CHECK(s.speed_percent == 200);
    SongController d;
    d.load(two_hand_song(4), now);
    d.apply_session(s, now);
    CHECK(d.count_in_bars() == 2);
    CHECK(d.speed_percent() == 200);
}

namespace {

// The allocations of `frames` frames of a playing controller, counted once it is warm. The
// evaluator allocates as it resolves each note group (not part of this feature), so the metronome
// is judged by what it adds to that.
int allocations_while_playing(bool metronome, ClickSubdivision subdivision) {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(32), now);
    SongOptions o;
    o.metronome = metronome;
    o.metronome_grid.subdivision = subdivision;
    c.set_options(o, now);
    c.set_song_sound(SongSound::BothHands, now);
    std::vector<SongSoundEvent> events;
    events.reserve(256);
    const auto frame = [&] {
        now += 16 * kMs;
        c.update(now, {});
        events.clear();
        c.take_sound_events(events);
    };
    c.play(now);
    for (int i = 0; i < 400; ++i) {
        frame();
    }
    etude::test::start_counting_allocations();
    for (int i = 0; i < 1500; ++i) {
        frame();
    }
    const int counted = etude::test::stop_counting_allocations();
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    return counted;
}

} // namespace

TEST_CASE("metronome: playing it allocates nothing the controller did not already") {
    const int without = allocations_while_playing(false, ClickSubdivision::Off);
    CHECK(allocations_while_playing(true, ClickSubdivision::Off) == without);
    CHECK(allocations_while_playing(true, ClickSubdivision::Divide) == without);
    CHECK(allocations_while_playing(true, ClickSubdivision::Triplets) == without);
}

TEST_CASE("count-in: counting in allocates nothing") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(32), now);
    SongOptions o;
    o.metronome = true;
    c.set_options(o, now);
    c.set_count_in_bars(2);
    std::vector<SongSoundEvent> events;
    events.reserve(256);
    const auto frame = [&] {
        now += 16 * kMs;
        c.update(now, {});
        events.clear();
        c.take_sound_events(events);
    };
    c.play(now);
    for (int i = 0; i < 20; ++i) { // the first frames grow every vector
        frame();
    }
    etude::test::start_counting_allocations();
    for (int i = 0; i < 180; ++i) { // the rest of the 4 s the two bars take
        frame();
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    CHECK(c.state() == TransportState::CountIn);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

// ---------------------------------------------------------------------------------------------
// A song's practice tables made ahead (SongController::prepare), as the opener's thread does
