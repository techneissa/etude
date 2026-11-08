// SPDX-License-Identifier: GPL-3.0-or-later
#include "piano_synth.hpp"
#include "tiny_sf2.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

using etude::app::PianoSynth;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 128;

// Renders `seconds` of audio in device-sized blocks and returns the interleaved samples.
std::vector<float> render(PianoSynth& synth, double seconds) {
    const int frames = static_cast<int>(seconds * kRate);
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    for (int done = 0; done < frames; done += kBlock) {
        synth.render(out.data() + static_cast<std::size_t>(done) * 2,
                     std::min(kBlock, frames - done));
    }
    return out;
}

float peak(const std::vector<float>& s, std::size_t from = 0) {
    float p = 0.0F;
    for (std::size_t i = from; i < s.size(); ++i) {
        p = std::max(p, std::fabs(s[i]));
    }
    return p;
}

bool all_finite(const std::vector<float>& s) {
    return std::all_of(s.begin(), s.end(), [](float x) { return std::isfinite(x); });
}

// Runs `body` once with the tiny sine SoundFont and once with the real piano when the build has it
// (ETUDE_PIANO_SF2_FILE or the download; see cmake/PianoAsset.cmake).
void for_each_font(const std::function<void(PianoSynth&)>& body) {
    {
        INFO("font: tiny sine");
        PianoSynth synth;
        const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
        REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
        body(synth);
    }
#ifdef ETUDE_TEST_PIANO_SF2
    if (std::filesystem::exists(ETUDE_TEST_PIANO_SF2)) {
        INFO("font: " << ETUDE_TEST_PIANO_SF2);
        PianoSynth synth;
        REQUIRE_MESSAGE(synth.load_file(ETUDE_TEST_PIANO_SF2, kRate), synth.error());
        body(synth);
    }
#endif
}

} // namespace

TEST_CASE("a C major chord sounds, stays finite and peaks below 0 dBFS") {
    for_each_font([](PianoSynth& synth) {
        synth.note_on(60, 100);
        synth.note_on(64, 100);
        synth.note_on(67, 100);
        const auto out = render(synth, 1.0);
        CHECK(all_finite(out));
        CHECK(peak(out) > 0.05F);
        CHECK(peak(out) <= PianoSynth::kCeiling);
    });
}

TEST_CASE("a loud cluster of twenty notes is held under the ceiling by the limiter") {
    for_each_font([](PianoSynth& synth) {
        for (int note = 48; note < 68; ++note) {
            synth.note_on(note, 127);
        }
        const auto out = render(synth, 0.5);
        CHECK(all_finite(out));
        CHECK(peak(out) <= PianoSynth::kCeiling);
        CHECK(peak(out) > 0.5F); // loud, so the limiter is what holds it
    });
}

TEST_CASE("a released note decays to silence") {
    for_each_font([](PianoSynth& synth) {
        synth.note_on(60, 100);
        render(synth, 0.5);
        synth.note_off(60);
        const auto tail = render(synth, 3.0);
        // The last 100 ms are below -80 dBFS and no voice is left.
        CHECK(peak(tail, tail.size() - 2 * 4800) < 1e-4F);
        CHECK(synth.active_voices() == 0);
    });
}

TEST_CASE("the sustain pedal holds released notes until it comes up") {
    for_each_font([](PianoSynth& synth) {
        synth.sustain(true);
        for (const int note : {60, 64, 67, 72}) {
            synth.note_on(note, 90);
            render(synth, 0.15);
            synth.note_off(note);
        }
        const auto held = render(synth, 1.0);
        // A second after the last key went up, the pedal keeps all four notes sounding.
        CHECK(peak(held, held.size() - 2 * 4800) > 1e-3F);
        CHECK(synth.active_voices() >= 4);
        synth.sustain(false);
        const auto tail = render(synth, 3.0);
        CHECK(peak(tail, tail.size() - 2 * 4800) < 1e-4F);
        CHECK(synth.active_voices() == 0);
    });
}

TEST_CASE("all notes off silences everything within 15 ms, pedal included") {
    for_each_font([](PianoSynth& synth) {
        synth.sustain(true);
        for (const int note : {48, 60, 64, 67, 72}) {
            synth.note_on(note, 110);
        }
        synth.note_off(60);
        render(synth, 0.3);
        synth.all_notes_off();
        const auto after = render(synth, 0.05);
        // From 15 ms on, below -60 dBFS.
        CHECK(peak(after, 2 * 720) < 1e-3F);
        // And the pedal is up: a new note released now does not hang on.
        synth.note_on(62, 100);
        render(synth, 0.1);
        synth.note_off(62);
        const auto tail = render(synth, 3.0);
        CHECK(peak(tail, tail.size() - 2 * 4800) < 1e-4F);
    });
}

TEST_CASE("more notes than voices: the cap holds and the oldest released voice goes first") {
    PianoSynth synth;
    const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
    REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
    // The sine uses one voice per note, so 64 held notes fill the pool.
    for (int i = 0; i < PianoSynth::kMaxVoices; ++i) {
        synth.note_on(20 + i, 100);
    }
    render(synth, 0.01);
    REQUIRE(synth.active_voices() == PianoSynth::kMaxVoices);
    synth.note_off(50); // released second-to-last...
    synth.note_off(30); // ...and this one later, but it was pressed earlier
    render(synth, 0.01);

    synth.note_on(100, 100);
    render(synth, 0.01);
    CHECK(synth.active_voices() == PianoSynth::kMaxVoices);
    CHECK(synth.stolen_voices() == 1);
    CHECK(synth.voices_for_key(30) == 0); // the oldest of the released notes went
    CHECK(synth.voices_for_key(50) == 1);
    CHECK(synth.voices_for_key(100) == 1);

    // With nothing released left the oldest held note goes, and the new note always sounds.
    synth.note_on(101, 100);
    synth.note_on(102, 100);
    render(synth, 0.01);
    CHECK(synth.voices_for_key(50) == 0); // released, so it goes before any held note
    CHECK(synth.voices_for_key(20) == 0); // then the oldest held
    CHECK(synth.voices_for_key(21) == 1);
    CHECK(synth.voices_for_key(101) == 1);
    CHECK(synth.voices_for_key(102) == 1);
}

TEST_CASE("a pedal-held voice is stolen before a held key's voice") {
    PianoSynth synth;
    const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
    REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
    synth.sustain(true);
    for (int i = 0; i < PianoSynth::kMaxVoices; ++i) {
        synth.note_on(20 + i, 100);
    }
    synth.note_off(70); // key up, pedal holds it
    render(synth, 0.01);
    synth.note_on(110, 100);
    render(synth, 0.01);
    CHECK(synth.voices_for_key(70) == 0);
    CHECK(synth.voices_for_key(20) == 1);
    CHECK(synth.voices_for_key(110) == 1);
}

TEST_CASE("the real piano plays from the cap down: 100 pedalled notes stay within the voice pool") {
    for_each_font([](PianoSynth& synth) {
        synth.sustain(true);
        for (int i = 0; i < 100; ++i) {
            synth.note_on(24 + (i * 7) % 80, 100);
            synth.note_off(24 + (i * 7) % 80);
            render(synth, 0.005);
        }
        const auto out = render(synth, 0.2);
        CHECK(synth.active_voices() <= PianoSynth::kMaxVoices);
        CHECK(all_finite(out));
        CHECK(peak(out) <= PianoSynth::kCeiling);
    });
}

TEST_CASE("master volume scales the output") {
    PianoSynth synth;
    const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
    REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
    synth.note_on(60, 64);
    const float full = peak(render(synth, 0.5), 2 * 12000);
    synth.set_volume(0.25F);
    const float quarter = peak(render(synth, 0.5), 2 * 12000);
    CHECK(quarter == doctest::Approx(full * 0.25F).epsilon(0.02));
}

TEST_CASE("without a SoundFont the synth accepts every call and renders silence") {
    PianoSynth synth;
    CHECK_FALSE(synth.loaded());
    synth.note_on(60, 100);
    synth.sustain(true);
    synth.set_volume(0.5F);
    synth.all_notes_off();
    std::vector<float> out(2 * kBlock, 1.0F);
    synth.render(out.data(), kBlock);
    CHECK(peak(out) == 0.0F);
    std::int64_t ns = 0;
    CHECK_FALSE(synth.take_latency(ns));
}

TEST_CASE("data that is not a SoundFont is refused and the synth stays silent") {
    PianoSynth synth;
    const std::vector<std::uint8_t> junk(1000, 0x42);
    CHECK_FALSE(synth.load_memory(junk.data(), junk.size(), kRate));
    CHECK_FALSE(synth.error().empty());
    CHECK_FALSE(synth.load_file("/nonexistent/piano.sf2", kRate));
    CHECK_FALSE(synth.loaded());
}

TEST_CASE("a timed note-on reports its press-to-first-sample latency once") {
    PianoSynth synth;
    const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
    REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
    const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    synth.note_on_at(60, 100, now - 2'000'000);
    render(synth, 0.01);
    std::int64_t ns = 0;
    REQUIRE(synth.take_latency(ns));
    CHECK(ns >= 2'000'000);
    CHECK(ns < 1'000'000'000);
    CHECK_FALSE(synth.take_latency(ns));
}

// Meant for ThreadSanitizer too: the main thread's calls and the audio thread's render share only
// the command queue, the latency queue and atomics. The two run in lock-step, about 8 commands per
// block with up to 64 in flight, so they overlap for the whole run at any speed (TSan's included)
// and the 512-slot queue never fills.
TEST_CASE("two threads: commands from one, rendering on the other") {
    PianoSynth synth;
    const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
    REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
    std::atomic<bool> done{false};
    std::atomic<int> rendered{0};
    std::atomic<int> produced{0};
    std::thread main_thread([&] {
        for (int i = 0; i < 20000; ++i) {
            while (i > 64 + 8 * rendered.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }
            const int note = 36 + (i * 5) % 48;
            switch (i % 9) {
            case 0:
                synth.sustain((i / 9) % 2 == 0);
                break;
            case 1:
                synth.set_volume(0.5F + 0.5F * static_cast<float>(i % 3) / 2.0F);
                break;
            case 2:
                if (i % 900 == 2) {
                    synth.all_notes_off();
                }
                break;
            default:
                if (i % 2 == 0) {
                    synth.note_on(note, 40 + i % 80);
                } else {
                    synth.note_off(note);
                }
            }
            produced.store(i + 1, std::memory_order_relaxed);
            std::int64_t ns = 0;
            while (synth.take_latency(ns)) {
            }
        }
        done.store(true, std::memory_order_release);
    });
    std::vector<float> block(2 * kBlock);
    bool finite = true;
    float max_peak = 0.0F;
    int blocks = 0;
    while (!done.load(std::memory_order_acquire) || blocks < 100) {
        while (!done.load(std::memory_order_relaxed) &&
               8 * blocks > produced.load(std::memory_order_relaxed) + 8) {
            std::this_thread::yield();
        }
        synth.render(block.data(), kBlock);
        finite = finite && all_finite(block);
        max_peak = std::max(max_peak, peak(block));
        ++blocks;
        rendered.store(blocks, std::memory_order_relaxed);
    }
    main_thread.join();
    CHECK(finite);
    CHECK(max_peak <= PianoSynth::kCeiling);
    CHECK(synth.active_voices() <= PianoSynth::kMaxVoices);
    CHECK(synth.dropped_commands() == 0);
    MESSAGE("rendered " << blocks << " blocks, " << synth.dropped_commands()
                        << " commands dropped, " << synth.stolen_voices() << " voices stolen");
}

// ---- The song's notes (SongSoundEvent): scheduled, cancelled, stopped

namespace {

using etude::PlaybackMessage;
using etude::SongSoundEvent;
using etude::app::SongStart;

constexpr std::int64_t kSecNs = 1'000'000'000;

SongSoundEvent song_message(PlaybackMessage::Kind kind, int key, std::int64_t due, int value = 90) {
    SongSoundEvent e;
    e.kind = SongSoundEvent::Kind::Message;
    e.message.kind = kind;
    e.message.data = static_cast<std::uint8_t>(key);
    e.message.value = static_cast<std::uint16_t>(value);
    e.message.at_ns = due;
    return e;
}

SongSoundEvent song_cancel(std::int64_t at) {
    SongSoundEvent e;
    e.kind = SongSoundEvent::Kind::CancelUnstarted;
    e.message.at_ns = at;
    return e;
}

SongSoundEvent song_stop() {
    SongSoundEvent e;
    e.kind = SongSoundEvent::Kind::StopAll;
    return e;
}

// ns of `frames` samples at kRate, rounded down.
std::int64_t frames_ns(int frames) { return static_cast<std::int64_t>(frames) * kSecNs / kRate; }

// The index of the first sample (of either channel's frame) that is not zero, or -1.
int first_sound(const std::vector<float>& s) {
    for (std::size_t i = 0; i < s.size(); i += 2) {
        if (s[i] != 0.0F || s[i + 1] != 0.0F) {
            return static_cast<int>(i / 2);
        }
    }
    return -1;
}

void load_tiny(PianoSynth& synth) {
    const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
    REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
}

} // namespace

TEST_CASE("a song note starts on the sample its due time falls on") {
    PianoSynth synth;
    const std::vector<std::uint8_t> sf2 = etude::test::tiny_sf2();
    REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
    const std::int64_t block_start = 10 * kSecNs;
    // A note's first sample is silent: the first sound comes this many samples after its start.
    const int lag = [&] {
        PianoSynth s;
        REQUIRE(s.load_memory(sf2.data(), sf2.size(), kRate));
        s.song_event(song_message(PlaybackMessage::Kind::NoteOn, 69, block_start));
        std::vector<float> block(2 * kBlock);
        s.render(block.data(), kBlock, block_start);
        return first_sound(block);
    }();
    REQUIRE(lag >= 0);
    for (const int offset : {0, 1, 37, 64, 120}) {
        INFO("offset " << offset);
        PianoSynth s;
        REQUIRE(s.load_memory(sf2.data(), sf2.size(), kRate));
        const std::int64_t due = block_start + frames_ns(offset) + 100; // 100 ns into that sample
        s.song_event(song_message(PlaybackMessage::Kind::NoteOn, 69, due));
        std::vector<float> block(2 * kBlock);
        s.render(block.data(), kBlock, block_start);
        // Silence before the sample, sound from it.
        const int first = first_sound(block);
        REQUIRE(first >= 0);
        CHECK(first == offset + lag);
        SongStart start;
        REQUIRE(s.take_song_start(start));
        CHECK(start.due_ns == due);
        CHECK(std::llabs(start.started_ns - due) <= kSecNs / kRate); // within one sample
    }
}

TEST_CASE(
    "a song note due in a later block waits for it; one that is late starts at the first sample") {
    PianoSynth synth;
    load_tiny(synth);
    const std::int64_t block_start = 10 * kSecNs;
    synth.song_event(
        song_message(PlaybackMessage::Kind::NoteOn, 69, block_start + frames_ns(kBlock + 20)));
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, 72, block_start - 5'000'000));
    std::vector<float> first(2 * kBlock);
    synth.render(first.data(), kBlock, block_start);
    CHECK(first_sound(first) == 1); // the late one, at once (a note's first sample is silent)
    CHECK(synth.voices_for_key(72) == 1);
    CHECK(synth.voices_for_key(69) == 0);
    std::vector<float> second(2 * kBlock);
    synth.render(second.data(), kBlock, block_start + frames_ns(kBlock));
    CHECK(synth.voices_for_key(69) == 1);
    SongStart a;
    SongStart b;
    REQUIRE(synth.take_song_start(a));
    REQUIRE(synth.take_song_start(b));
    CHECK(a.started_ns > a.due_ns); // late by 5 ms
    CHECK(std::llabs(b.started_ns - b.due_ns) <= kSecNs / kRate);
}

TEST_CASE("without a start time every queued song note starts at the first sample") {
    PianoSynth synth;
    load_tiny(synth);
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, 69, std::int64_t{1} << 60));
    std::vector<float> block(2 * kBlock);
    synth.render(block.data(), kBlock);
    CHECK(first_sound(block) == 1); // from the first sample (which is silent)
}

TEST_CASE("a cancel keeps a song note that has not started from ever starting") {
    for_each_font([](PianoSynth& synth) {
        const std::int64_t block_start = 10 * kSecNs;
        synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, 69, block_start + 30'000'000));
        synth.song_event(
            song_message(PlaybackMessage::Kind::NoteOff, 69, block_start + 60'000'000, 0));
        synth.song_event(song_cancel(block_start + 5'000'000));
        std::vector<float> out(2 * kBlock);
        std::int64_t t = block_start;
        for (int i = 0; i < 200; ++i) { // 0.53 s
            synth.render(out.data(), kBlock, t);
            t += frames_ns(kBlock);
            CHECK(peak(out) == 0.0F);
        }
        SongStart start;
        CHECK_FALSE(synth.take_song_start(start));
    });
}

TEST_CASE("song notes and the player's: stopping the song leaves the player's notes sounding") {
    PianoSynth synth;
    load_tiny(synth);
    const std::int64_t t0 = 10 * kSecNs;
    synth.note_on(60, 100); // the player's
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, 69, t0));
    std::vector<float> out(2 * kBlock);
    synth.render(out.data(), kBlock, t0);
    REQUIRE(synth.voices_for_key(60) == 1);
    REQUIRE(synth.voices_for_key(69) == 1);
    synth.song_event(song_stop());
    synth.render(out.data(), kBlock, t0 + frames_ns(kBlock));
    CHECK(synth.voices_for_key(60) == 1);
    // The song's key is released (a release tail may linger, but it is no longer held).
    for (int i = 0; i < 200; ++i) {
        synth.render(out.data(), kBlock, t0 + frames_ns(kBlock * (i + 2)));
    }
    CHECK(synth.voices_for_key(69) == 0);
    CHECK(synth.voices_for_key(60) == 1);
    // A song note-off for a key the song did not start does not touch the player's note.
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOff, 60, t0, 0));
    synth.render(out.data(), kBlock, t0 + frames_ns(kBlock * 300));
    CHECK(synth.voices_for_key(60) == 1);
}

TEST_CASE("song notes sustain with their pedal and All Notes Off stops them") {
    PianoSynth synth;
    load_tiny(synth);
    const std::int64_t t0 = 10 * kSecNs;
    std::vector<float> out(2 * kBlock);
    synth.song_event(song_message(PlaybackMessage::Kind::ControlChange, 64, t0, 127));
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, 69, t0));
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOff, 69, t0 + frames_ns(10), 0));
    float tail = 0.0F;
    for (int i = 0; i < 200; ++i) {
        synth.render(out.data(), kBlock, t0 + frames_ns(kBlock * i));
        tail = peak(out);
    }
    CHECK(tail > 0.05F); // held by the pedal for 0.5 s
    synth.song_event(
        song_message(PlaybackMessage::Kind::ControlChange, 64, t0 + frames_ns(kBlock * 200), 0));
    for (int i = 200; i < 600; ++i) {
        synth.render(out.data(), kBlock, t0 + frames_ns(kBlock * i));
    }
    CHECK(peak(out) < 1e-4F);
    // CC123 stops the song's notes.
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, 71, t0 + frames_ns(kBlock * 600)));
    synth.render(out.data(), kBlock, t0 + frames_ns(kBlock * 600));
    REQUIRE(synth.voices_for_key(71) == 1);
    synth.song_event(
        song_message(PlaybackMessage::Kind::ControlChange, 123, t0 + frames_ns(kBlock * 601), 0));
    for (int i = 601; i < 900; ++i) {
        synth.render(out.data(), kBlock, t0 + frames_ns(kBlock * i));
    }
    CHECK(synth.voices_for_key(71) == 0);
}

TEST_CASE("losing a song event other than a note-on stops the song, so no note stays on") {
    PianoSynth synth;
    load_tiny(synth);
    const std::int64_t t0 = 10 * kSecNs;
    std::vector<float> out(2 * kBlock);
    synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, 69, t0));
    synth.render(out.data(), kBlock, t0);
    REQUIRE(synth.voices_for_key(69) == 1);
    // Flood the queue (nothing renders): the note-off is lost with the rest.
    for (int i = 0; i < 1500; ++i) {
        synth.song_event(
            song_message(PlaybackMessage::Kind::NoteOff, 69, t0 + frames_ns(kBlock) + i, 0));
    }
    synth.song_event(
        song_message(PlaybackMessage::Kind::NoteOn, 70, t0 + frames_ns(kBlock) + 2000));
    CHECK(synth.dropped_song_notes() >= 1);
    for (int i = 1; i < 300; ++i) {
        synth.render(out.data(), kBlock, t0 + frames_ns(kBlock * i));
    }
    CHECK(synth.voices_for_key(69) == 0);
}

// Meant for ThreadSanitizer: the main thread sends the song's events while the audio thread
// renders, sharing only the lock-free queues and atomics.
TEST_CASE("two threads: the song's events from one, rendering on the other") {
    PianoSynth synth;
    load_tiny(synth);
    constexpr int kBlocks = 3000;
    std::atomic<int> rendered{0};
    std::atomic<int> produced{0};
    std::atomic<bool> done{false};
    std::thread main_thread([&] {
        std::int64_t due = 10 * kSecNs;
        for (int i = 0; i < kBlocks; ++i) {
            // Stay at most 40 blocks ahead of the audio thread.
            while (i > rendered.load(std::memory_order_relaxed) + 40) {
                std::this_thread::yield();
            }
            const int key = 48 + i % 40;
            synth.song_event(song_message(PlaybackMessage::Kind::NoteOn, key, due));
            synth.song_event(
                song_message(PlaybackMessage::Kind::NoteOff, key, due + 3 * frames_ns(kBlock), 0));
            if (i % 50 == 49) {
                synth.song_event(song_cancel(due - frames_ns(kBlock)));
            }
            if (i % 400 == 399) {
                synth.song_event(song_stop());
            }
            SongStart start;
            while (synth.take_song_start(start)) {
            }
            due += frames_ns(kBlock);
            produced.store(i + 1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });
    std::vector<float> block(2 * kBlock);
    std::int64_t t = 10 * kSecNs - 10 * frames_ns(kBlock);
    bool finite = true;
    int n = 0;
    while (n < kBlocks + 10) {
        // Block n starts at the due time of event n - 10: wait until that one is in.
        while (!done.load(std::memory_order_relaxed) &&
               produced.load(std::memory_order_relaxed) < n - 9) {
            std::this_thread::yield();
        }
        synth.render(block.data(), kBlock, t);
        t += frames_ns(kBlock);
        finite = finite && all_finite(block);
        ++n;
        rendered.store(n, std::memory_order_relaxed);
    }
    main_thread.join();
    CHECK(finite);
    CHECK(synth.active_voices() <= PianoSynth::kMaxVoices);
    CHECK(synth.dropped_song_notes() == 0);
}

// ---- Clicks (the metronome's voices) ------------------------------------------------------------

namespace {

SongSoundEvent click_message(int note, std::int64_t due, int velocity = 100) {
    SongSoundEvent e = song_message(PlaybackMessage::Kind::NoteOn, note, due, velocity);
    e.message.click = true;
    e.message.channel = 9;
    return e;
}

float loudest(const std::vector<float>& s, std::size_t from, std::size_t to) {
    float p = 0.0F;
    for (std::size_t i = from; i < to && i < s.size(); ++i) {
        p = std::max(p, std::fabs(s[i]));
    }
    return p;
}

} // namespace

TEST_CASE("a click starts on the sample its due time falls on, in all three voices") {
    for (const int note : {76, 77, 34, 33, 81, 80, 60}) {
        for (const int offset : {0, 1, 37, 100}) {
            INFO("note " << note << " offset " << offset);
            PianoSynth synth;
            load_tiny(synth);
            const std::int64_t block_start = 10 * kSecNs;
            synth.song_event(click_message(note, block_start + frames_ns(offset) + 100));
            std::vector<float> block(2 * kBlock);
            synth.render(block.data(), kBlock, block_start);
            // Silence before the due sample; sound within a sample of it (a tone's first sample is
            // zero, the noise of the others is not).
            const int first = first_sound(block);
            REQUIRE(first >= 0);
            CHECK(first >= offset);
            CHECK(first <= offset + 1);
            CHECK(loudest(block, 0, static_cast<std::size_t>(2 * offset)) == 0.0F);
            CHECK(all_finite(block));
            CHECK(loudest(block, 0, block.size()) <= PianoSynth::kCeiling);
        }
    }
}

TEST_CASE("a click is not a piano note: no voice of the piano's, and its note-off means nothing") {
    PianoSynth synth;
    load_tiny(synth);
    const std::int64_t block_start = 10 * kSecNs;
    synth.song_event(click_message(76, block_start));
    SongSoundEvent off = click_message(76, block_start + 1'000'000);
    off.message.kind = PlaybackMessage::Kind::NoteOff;
    synth.song_event(off);
    std::vector<float> block(2 * kBlock);
    synth.render(block.data(), kBlock, block_start);
    CHECK(synth.voices_for_key(76) == 0);
    CHECK(synth.active_voices() == 0);
    // The click rings out on its own, longer than the note-off 1 ms after it.
    CHECK(loudest(block, 2 * 120, 2 * kBlock) > 0.001F);
    // And dies away to silence.
    const auto tail = render(synth, 0.3);
    CHECK(loudest(tail, tail.size() - 2000, tail.size()) < 1e-4F);
}

TEST_CASE("clicks are louder with the velocity, and the accent voice differs from the beat's") {
    const auto peak_of = [](int note, int velocity) {
        PianoSynth synth;
        load_tiny(synth);
        synth.song_event(click_message(note, 10 * kSecNs, velocity));
        std::vector<float> block(2 * kBlock * 8);
        for (int b = 0; b < 8; ++b) {
            synth.render(block.data() + static_cast<std::size_t>(2 * kBlock * b), kBlock,
                         10 * kSecNs + frames_ns(kBlock * b));
        }
        return loudest(block, 0, block.size());
    };
    for (const int note : {76, 77, 34, 33, 81, 80}) {
        INFO("note " << note);
        CHECK(peak_of(note, 110) > peak_of(note, 50));
        CHECK(peak_of(note, 50) > peak_of(note, 20));
        CHECK(peak_of(note, 127) <= PianoSynth::kCeiling);
    }
}

TEST_CASE("a cancel keeps a click that has not started from ever starting, a stop silences one that has") {
    PianoSynth synth;
    load_tiny(synth);
    const std::int64_t block_start = 10 * kSecNs;
    synth.song_event(click_message(77, block_start + frames_ns(kBlock + 20)));
    synth.song_event(song_cancel(block_start));
    std::vector<float> first(2 * kBlock);
    std::vector<float> second(2 * kBlock);
    synth.render(first.data(), kBlock, block_start);
    synth.render(second.data(), kBlock, block_start + frames_ns(kBlock));
    CHECK(first_sound(first) == -1);
    CHECK(first_sound(second) == -1);

    synth.song_event(click_message(77, block_start + 2 * frames_ns(kBlock)));
    std::vector<float> third(2 * kBlock);
    synth.render(third.data(), kBlock, block_start + 2 * frames_ns(kBlock));
    REQUIRE(first_sound(third) == 0);
    synth.song_event(song_stop());
    std::vector<float> fourth(2 * kBlock);
    synth.render(fourth.data(), kBlock, block_start + 3 * frames_ns(kBlock));
    CHECK(first_sound(fourth) == -1); // the click that was ringing is gone
}

TEST_CASE("more clicks than voices at once: the oldest is taken over, the output stays finite") {
    PianoSynth synth;
    load_tiny(synth);
    for (int i = 0; i < 40; ++i) {
        synth.song_event(click_message(76 + i % 2, 10 * kSecNs + i * 100, 127));
    }
    std::vector<float> block(2 * kBlock);
    synth.render(block.data(), kBlock, 10 * kSecNs);
    CHECK(all_finite(block));
    CHECK(loudest(block, 0, block.size()) <= PianoSynth::kCeiling);
}

TEST_CASE("two threads: clicks from one, rendering on the other") {
    PianoSynth synth;
    load_tiny(synth);
    constexpr int kBlocks = 2000;
    std::atomic<int> rendered{0};
    std::atomic<int> produced{0};
    std::atomic<bool> done{false};
    std::thread main_thread([&] {
        std::int64_t due = 10 * kSecNs;
        for (int i = 0; i < kBlocks; ++i) {
            while (i > rendered.load(std::memory_order_relaxed) + 40) {
                std::this_thread::yield();
            }
            const int notes[] = {76, 77, 34, 33, 81, 80};
            synth.song_event(click_message(notes[i % 6], due, 60 + i % 60));
            if (i % 37 == 36) {
                synth.song_event(song_cancel(due - frames_ns(kBlock)));
            }
            if (i % 300 == 299) {
                synth.song_event(song_stop());
            }
            due += 2 * frames_ns(kBlock);
            produced.store(i + 1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });
    std::vector<float> block(2 * kBlock);
    std::int64_t t = 10 * kSecNs - 10 * frames_ns(kBlock);
    bool finite = true;
    int n = 0;
    while (n < 2 * kBlocks + 10) {
        while (!done.load(std::memory_order_relaxed) &&
               produced.load(std::memory_order_relaxed) < n / 2 - 9) {
            std::this_thread::yield();
        }
        synth.render(block.data(), kBlock, t);
        t += frames_ns(kBlock);
        finite = finite && all_finite(block);
        ++n;
        rendered.store(n / 2, std::memory_order_relaxed);
    }
    main_thread.join();
    CHECK(finite);
    CHECK(synth.dropped_song_notes() == 0);
}
