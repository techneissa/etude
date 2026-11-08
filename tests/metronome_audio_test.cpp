// SPDX-License-Identifier: GPL-3.0-or-later
// The metronome and the count-in from the song controller to the samples: a song with a tempo
// change plays at changing speeds with the count-in and the metronome on, the sound renders offline
// on the built-in synth with the device running a little ahead of the frame loop, and each click's
// first sample is compared with the beat grid. The largest error in samples is printed.
#include "piano_synth.hpp"
#include "song_controller.hpp"
#include "tiny_sf2.hpp"

#include "etude/midi_file.hpp"

#include "smf_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace etude::test;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 128;
constexpr std::int64_t kSec = 1'000'000'000;
constexpr std::int64_t kMs = 1'000'000;

// 120 BPM for the first bar (2 s), then 60 BPM (4 s a bar) for two bars.
LoadedSong tempo_change_song() {
    Track t;
    t.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60).bpm(1920, 60).off(1920 * 3, 0, 60).end();
    MidiLoadResult r = load_midi(smf({t}));
    REQUIRE(r.ok);
    LoadedSong out;
    out.song = std::move(r.song);
    out.title = "Tempo change";
    out.hash = 0x99;
    out.library_path = "t.mid";
    return out;
}

// The song's own beat times, in song seconds: bar 1 at 120 BPM, bars 2 and 3 at 60.
const double kBeats[] = {0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};

// The synth, run the way the audio callback runs it: blocks of kBlock frames, each rendered with the
// time its first sample reaches the device. The device is `ahead_ns` ahead of the frame loop.
struct Offline {
    PianoSynth synth;
    std::vector<float> mono; // channel 0 of everything rendered
    std::int64_t stream_ns = 0;
    std::int64_t blocks = 0;
    std::int64_t ahead_ns = 8 * kMs;

    explicit Offline(std::int64_t start_ns) : stream_ns(start_ns) {
        const std::vector<std::uint8_t> sf2 = tiny_sf2();
        REQUIRE(synth.load_memory(sf2.data(), sf2.size(), kRate));
    }
    std::int64_t block_start(std::int64_t block) const {
        return stream_ns + block * kBlock * kSec / kRate;
    }
    // Renders every block that starts before `now_ns` + ahead.
    void run_to(std::int64_t now_ns) {
        std::vector<float> block(2 * kBlock);
        while (block_start(blocks) < now_ns + ahead_ns) {
            synth.render(block.data(), kBlock, block_start(blocks));
            for (int i = 0; i < kBlock; ++i) {
                mono.push_back(block[static_cast<std::size_t>(2 * i)]);
            }
            ++blocks;
        }
    }
    // The first sample of each click: a sound after at least 120 ms of quiet.
    std::vector<std::int64_t> onsets() const {
        std::vector<std::int64_t> out;
        std::int64_t last_loud = -1'000'000;
        for (std::size_t i = 0; i < mono.size(); ++i) {
            if (std::fabs(mono[i]) > 1e-5F) {
                const auto at = static_cast<std::int64_t>(i);
                if (at - last_loud > kRate / 8) {
                    out.push_back(at);
                }
                last_loud = at;
            }
        }
        return out;
    }
    // The RMS of the samples [from, from + count), 0 outside what was rendered.
    double rms(std::int64_t from, std::int64_t count) const {
        double sum = 0.0;
        std::int64_t n = 0;
        for (std::int64_t i = std::max<std::int64_t>(0, from);
             i < from + count && i < static_cast<std::int64_t>(mono.size()); ++i) {
            sum += static_cast<double>(mono[static_cast<std::size_t>(i)]) *
                   static_cast<double>(mono[static_cast<std::size_t>(i)]);
            ++n;
        }
        return n == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(n));
    }
    double sample_of(std::int64_t ns) const {
        return static_cast<double>(ns - stream_ns) * kRate / static_cast<double>(kSec);
    }
};

} // namespace

TEST_CASE("clicks land on the beat grid to the sample through a count-in, a tempo change and speed changes") {
    SongController c;
    std::int64_t now = 10 * kSec;
    c.load(tempo_change_song(), now);
    SongOptions o;
    o.metronome = true;
    o.click_voice = ClickVoice::Click; // noise in the attack: the first sample is the first sound
    c.set_options(o, now);
    c.set_count_in_bars(1);
    c.set_speed(75, now);

    Offline audio(now);
    const std::int64_t t0 = now;
    auto frame = [&] {
        now += 16'667'000; // a 60 Hz frame loop
        c.update(now, {});
        std::vector<SongSoundEvent> events;
        c.take_sound_events(events);
        for (const SongSoundEvent& e : events) {
            audio.synth.song_event(e);
        }
        audio.run_to(now);
    };

    // The count-in: four beats at 75%, a beat every 0.5 / 0.75 s, then the song at 75%.
    c.play(now);
    {
        std::vector<SongSoundEvent> events;
        c.take_sound_events(events);
        for (const SongSoundEvent& e : events) {
            audio.synth.song_event(e);
        }
        audio.run_to(now);
    }
    std::vector<std::int64_t> expect; // wall ns of every click, from the beat grid and the speeds
    const double count_beat_s = 0.5 / 0.75;
    for (int k = 0; k < 4; ++k) {
        expect.push_back(t0 + std::llround(k * count_beat_s * 1e9));
    }
    const std::int64_t song_start = t0 + std::llround(4 * count_beat_s * 1e9);

    // 3 s of wall time into the song at 75% (2.25 s of song), then 150%.
    while (now < song_start + 3 * kSec) {
        frame();
    }
    c.set_speed(150, now);
    const double change_pos = c.position_s();
    const std::int64_t change_ns = now;
    {
        std::vector<SongSoundEvent> events;
        c.take_sound_events(events);
        for (const SongSoundEvent& e : events) {
            audio.synth.song_event(e);
        }
    }
    while (c.position_s() < 8.7 && now < t0 + 60 * kSec) {
        frame();
    }
    // Stop a beat short of the song's end; every click that was handed over has been rendered.
    c.pause(now);
    audio.run_to(now + 300 * kMs);

    for (const double beat : kBeats) {
        if (beat < change_pos - 1e-9) {
            expect.push_back(song_start + std::llround(beat / 0.75 * 1e9));
        } else {
            expect.push_back(change_ns + std::llround((beat - change_pos) / 1.5 * 1e9));
        }
    }
    const std::vector<std::int64_t> got = audio.onsets();
    // Clicks handed over before the pause are all there: the ones due after it were cancelled.
    std::size_t counted = 0;
    for (const std::int64_t ns : expect) {
        if (ns < now - 1 * kMs) {
            ++counted;
        }
    }
    REQUIRE(got.size() >= counted);
    REQUIRE(counted >= 14);
    double worst = 0.0;
    double quietest = 1e9;
    for (std::size_t i = 0; i < counted; ++i) {
        const double error = static_cast<double>(got[i]) - audio.sample_of(expect[i]);
        CAPTURE(i);
        CHECK(std::fabs(error) <= 1.5);
        worst = std::max(worst, std::fabs(error));
        // The click really sounded: energy in the 10 ms from its sample, silence in the 2 ms
        // before it.
        const auto at = static_cast<std::int64_t>(std::llround(audio.sample_of(expect[i])));
        CHECK(audio.rms(at, kRate / 100) > 0.01);
        CHECK(audio.rms(at - kRate / 500, kRate / 500) < 1e-4);
        quietest = std::min(quietest, audio.rms(at, kRate / 100));
    }
    std::printf("metronome click timing: %zu clicks, largest error %.2f samples (%.1f us at %d Hz), "
                "quietest click RMS %.4f\n",
                counted, worst, worst * 1e6 / kRate, kRate, quietest);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

namespace {

// A count-in and a few beats on the synth with `o`: the RMS of the 10 ms from each of the first
// `beats` beat times (0.5 s apart at 120 BPM) and of the whole stretch between them.
struct Heard {
    std::vector<double> at_beat;
    double between = 0.0;
};

Heard play_count_in(const SongOptions& o, int count_in_bars, int beats) {
    SongController c;
    std::int64_t now = 10 * kSec;
    c.load(tempo_change_song(), now);
    c.set_options(o, now);
    c.set_count_in_bars(count_in_bars);
    Offline audio(now);
    const std::int64_t t0 = now;
    const auto hand = [&] {
        std::vector<SongSoundEvent> events;
        c.take_sound_events(events);
        for (const SongSoundEvent& e : events) {
            audio.synth.song_event(e);
        }
        audio.run_to(now);
    };
    c.play(now);
    hand();
    while (now < t0 + (beats + 1) * 500 * kMs) {
        now += 16'667'000;
        c.update(now, {});
        hand();
    }
    Heard heard;
    for (int k = 0; k < beats; ++k) {
        heard.at_beat.push_back(
            audio.rms(static_cast<std::int64_t>(std::llround(audio.sample_of(t0 + k * 500 * kMs))),
                      kRate / 100));
    }
    // Between the clicks: from 60 ms after one to the next (the click rings about 40 ms).
    double sum = 0.0;
    int n = 0;
    for (int k = 0; k + 1 < beats; ++k) {
        const auto from = static_cast<std::int64_t>(
            std::llround(audio.sample_of(t0 + k * 500 * kMs + 60 * kMs)));
        sum += audio.rms(from, kRate / 4);
        ++n;
    }
    heard.between = n > 0 ? sum / n : 0.0;
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    return heard;
}

} // namespace

TEST_CASE("the count-in is heard with the metronome off, and a volume of 0 is silent") {
    SongOptions o;
    o.metronome = false;
    o.click_voice = ClickVoice::Click;
    const Heard on = play_count_in(o, 1, 4);
    REQUIRE(on.at_beat.size() == 4);
    for (const double e : on.at_beat) {
        CHECK(e > 0.01); // each of the four beats has its click
    }
    CHECK(on.at_beat[0] > on.at_beat[1]); // beat one is the accent
    o.click_volume = 0;
    const Heard silent = play_count_in(o, 1, 4);
    for (const double e : silent.at_beat) {
        CHECK(e == 0.0);
    }
    // Quieter at a lower volume.
    o.click_volume = 30;
    const Heard soft = play_count_in(o, 1, 4);
    CHECK(soft.at_beat[1] > 0.0);
    CHECK(soft.at_beat[1] < on.at_beat[1]);
    // No count-in, no metronome: nothing at all.
    o.click_volume = kDefaultClickVolume;
    const Heard none = play_count_in(o, 0, 4);
    for (const double e : none.at_beat) {
        CHECK(e == 0.0);
    }
}

TEST_CASE("the metronome is heard on every beat, and not when it is off") {
    SongOptions o;
    o.metronome = true;
    o.click_voice = ClickVoice::Click;
    const Heard on = play_count_in(o, 0, 5);
    for (const double e : on.at_beat) {
        CHECK(e > 0.01);
    }
    o.metronome = false;
    const Heard off = play_count_in(o, 0, 5);
    for (const double e : off.at_beat) {
        CHECK(e == 0.0);
    }
    CHECK(off.between == 0.0);
}
