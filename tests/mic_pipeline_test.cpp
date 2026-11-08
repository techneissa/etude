// SPDX-License-Identifier: GPL-3.0-or-later
// The microphone path without a device: a fake source thread plays the part of the capture
// callback and pushes blocks into the pipeline; the analysis thread turns them into events. Run
// under ThreadSanitizer these cover the hand-off between the three threads.
#include "mic_pipeline.hpp"
#include "wav_file.hpp"

#include "etude/pitch_synth.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 480; // 10 ms, like a typical capture buffer

// The steady-clock time of sample `index` on a stream whose sample 0 is at `base_ns`.
std::int64_t time_of(std::int64_t base_ns, std::int64_t index) {
    return base_ns + index * 1'000'000'000 / kRate;
}

SynthClip clean_clip(int note) {
    SynthClipOptions o;
    o.interferer = false;
    o.noise_rms = 3e-4f;
    o.lead_in_s = 0.4f;
    o.note_s = 0.8f;
    return make_synth_clip(note, 0.7f, 1, o, kRate);
}

// Pushes `audio` the way a capture callback would, block by block, with the time each block's last
// sample was captured on a virtual clock that starts at `base_ns`. `first_index` is the stream
// index of audio[0]. Waits while the analysis thread is more than `max_backlog` samples behind,
// so nothing is dropped.
void feed(MicPipeline& p, const std::vector<float>& audio, std::int64_t first_index,
          std::int64_t base_ns, std::size_t max_backlog) {
    for (std::size_t pos = 0; pos < audio.size(); pos += kBlock) {
        const int n = static_cast<int>(std::min<std::size_t>(kBlock, audio.size() - pos));
        while (p.backlog_samples() > max_backlog) {
            std::this_thread::yield();
        }
        p.push(audio.data() + pos, n,
               time_of(base_ns, first_index + static_cast<std::int64_t>(pos) + n));
    }
}

// Waits up to `timeout_ms` for a NoteOn.
bool wait_for_note_on(MicPipeline& p, MicEventIn& out, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        MicEventIn e;
        while (p.pop(e)) {
            if (e.event.type == PitchEventType::NoteOn) {
                out = e;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

TEST_CASE("mic pipeline: a note pushed from another thread comes out as a NoteOn at its onset") {
    const SynthClip clip = clean_clip(60);
    MicPipeline p;
    p.start({});
    const std::int64_t base_ns = 5'000'000'000;
    std::thread source([&] { feed(p, clip.samples, 0, base_ns, 4096); });
    MicEventIn e;
    const bool got = wait_for_note_on(p, e, 20000);
    source.join();
    REQUIRE(got);
    CHECK(e.event.note == 60);
    // The onset maps back through the stamps to within a few ms of where the clip put it.
    const std::int64_t expected_ns = time_of(base_ns, clip.onset);
    CHECK(std::llabs(e.onset_ns - expected_ns) < 8'000'000);
    CHECK(e.sample_ns > e.onset_ns);
    const MicStats st = p.take_stats();
    CHECK(st.blocks > 0);
    CHECK(st.worst_block_ns > 0);
    CHECK(st.dropped_samples == 0);
    CHECK(st.dropped_events == 0);
    p.stop();
}

TEST_CASE("mic pipeline: samples dropped by a full ring do not shift later onsets") {
    // Fill the ring before the thread runs: the blocks that do not fit are dropped, and the
    // pipeline must still place the note after them at its position on the capture clock.
    MicPipeline p;
    p.prepare({});
    const std::int64_t base_ns = 2'000'000'000;
    const std::vector<float> silence(p.ring_capacity() + 4 * kBlock, 0.0f);
    std::int64_t index = 0;
    for (std::size_t pos = 0; pos < silence.size(); pos += kBlock) {
        p.push(silence.data() + pos, kBlock, time_of(base_ns, index + kBlock));
        index += kBlock;
    }
    p.start_thread();
    const MicStats before = p.take_stats();
    CHECK(before.dropped_samples >= 4 * kBlock - kBlock);
    const std::uint64_t dropped = before.dropped_samples;
    CHECK(dropped % kBlock == 0); // whole blocks only
    while (p.backlog_samples() > 0) {
        std::this_thread::yield();
    }
    // The stream continues: the note's clip starts at stream index `index`.
    const SynthClip clip = clean_clip(64);
    std::thread source([&] { feed(p, clip.samples, index, base_ns, 4096); });
    MicEventIn e;
    const bool got = wait_for_note_on(p, e, 20000);
    source.join();
    REQUIRE(got);
    CHECK(e.event.note == 64);
    const std::int64_t expected_ns = time_of(base_ns, index + clip.onset);
    CHECK(std::llabs(e.onset_ns - expected_ns) < 8'000'000);
    p.stop();
}

TEST_CASE("mic pipeline: a producer that outruns the analysis thread drops whole blocks and counts "
          "them") {
    MicPipeline p;
    p.start({});
    const std::vector<float> noise(kBlock, 0.01f);
    std::uint64_t pushed = 0;
    std::thread source([&] {
        for (int i = 0; i < 4000; ++i) {
            p.push(noise.data(), kBlock, 1'000'000'000 + static_cast<std::int64_t>(i) * 10'000'000);
            pushed += kBlock;
        }
    });
    source.join();
    const MicStats st = p.take_stats();
    // However the threads interleaved, what was not dropped was accepted: nothing vanished.
    CHECK(st.dropped_samples % kBlock == 0);
    CHECK(st.dropped_samples <= pushed);
    while (p.backlog_samples() > 0) {
        std::this_thread::yield();
    }
    p.stop();
}

TEST_CASE(
    "mic pipeline: a stereo 44.1 kHz device is mixed down and resampled, onsets keep their time") {
    constexpr int kDeviceRate = 44100;
    SynthClipOptions o;
    o.interferer = false;
    o.noise_rms = 3e-4f;
    o.lead_in_s = 0.4f;
    o.note_s = 0.8f;
    const SynthClip clip = make_synth_clip(60, 0.7f, 1, o, kDeviceRate);
    std::vector<float> stereo(clip.samples.size() * 2);
    for (std::size_t i = 0; i < clip.samples.size(); ++i) {
        stereo[2 * i] = clip.samples[i];
        stereo[2 * i + 1] = clip.samples[i];
    }
    MicPipelineConfig c;
    c.input_rate = kDeviceRate;
    c.input_channels = 2;
    MicPipeline p;
    p.start(c);
    CHECK(p.sample_rate() == kDeviceRate);
    CHECK(p.input_channels() == 2);
    const std::int64_t base_ns = 3'000'000'000;
    std::thread source([&] {
        constexpr int kFrames = 441; // 10 ms
        for (std::size_t pos = 0; pos < clip.samples.size(); pos += kFrames) {
            const int n =
                static_cast<int>(std::min<std::size_t>(kFrames, clip.samples.size() - pos));
            while (p.backlog_samples() > 8192) {
                std::this_thread::yield();
            }
            const std::int64_t end_frame = static_cast<std::int64_t>(pos) + n;
            p.push(stereo.data() + 2 * pos, n, base_ns + end_frame * 1'000'000'000 / kDeviceRate);
        }
    });
    MicEventIn e;
    const bool got = wait_for_note_on(p, e, 20000);
    source.join();
    REQUIRE(got);
    CHECK(e.event.note == 60);
    const std::int64_t expected_ns = base_ns + clip.onset * 1'000'000'000 / kDeviceRate;
    CHECK(std::llabs(e.onset_ns - expected_ns) < 8'000'000);
    p.stop();
}

TEST_CASE("mic pipeline: stamps that do not fit their queue are counted") {
    MicPipeline p;
    p.prepare({}); // no thread: nothing drains the stamps
    const float one = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        p.push(&one, 1, 1'000'000'000 + i);
    }
    const MicStats st = p.take_stats();
    CHECK(st.dropped_stamps >= 1400);
    CHECK(st.dropped_samples == 0);
}

TEST_CASE("mic pipeline: commands reach the analysis thread (noise floor, A4)") {
    MicPipeline p;
    MicPipelineConfig c;
    p.start(c);
    p.set_noise_floor_rms(0.002f);
    p.set_a4_hz(442.0f);
    for (int i = 0; i < 500 && (p.noise_floor_rms() != 0.002f || p.a4_hz() != 442.0f); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(p.noise_floor_rms() == doctest::Approx(0.002f));
    CHECK(p.a4_hz() == doctest::Approx(442.0f));
    p.stop();
}

TEST_CASE("mic pipeline: calibration measures the room's noise as the floor") {
    MicPipeline p;
    p.start({});
    p.begin_noise_calibration();
    // 1 s of steady noise at an RMS of about 0.005.
    std::vector<float> noise(kRate);
    std::vector<float> tmp(kRate, 0.0f);
    add_room_noise(tmp.data(), kRate, 0.005f, 3);
    std::thread source([&] { feed(p, tmp, 0, 1'000'000'000, 4096); });
    source.join();
    while (p.backlog_samples() > 0) {
        std::this_thread::yield();
    }
    p.end_noise_calibration();
    for (int i = 0; i < 500 && p.noise_floor_rms() == 0.0f; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(p.noise_floor_rms() > 0.002f);
    CHECK(p.noise_floor_rms() < 0.012f);
    p.stop();
}

TEST_CASE("mic pipeline: stop is safe twice and the pipeline can start again") {
    MicPipeline p;
    p.start({});
    p.stop();
    p.stop();
    p.start({});
    CHECK(p.running());
    p.stop();
    CHECK_FALSE(p.running());
}

TEST_CASE("wav file: a 16-bit stereo file reads back as mono and resamples") {
    const auto path = std::filesystem::temp_directory_path() / "etude-mic-test.wav";
    {
        const int frames = 4800;
        const int channels = 2;
        const int rate = 24000;
        std::vector<std::int16_t> pcm(static_cast<std::size_t>(frames * channels));
        for (int i = 0; i < frames; ++i) {
            pcm[static_cast<std::size_t>(2 * i)] = 16384;      // left 0.5
            pcm[static_cast<std::size_t>(2 * i + 1)] = -16384; // right -0.5
        }
        std::ofstream out(path, std::ios::binary);
        const auto w32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
        const auto w16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
        const std::uint32_t data_bytes = static_cast<std::uint32_t>(pcm.size() * 2);
        out.write("RIFF", 4);
        w32(36 + data_bytes);
        out.write("WAVEfmt ", 8);
        w32(16);
        w16(1);
        w16(static_cast<std::uint16_t>(channels));
        w32(static_cast<std::uint32_t>(rate));
        w32(static_cast<std::uint32_t>(rate * channels * 2));
        w16(static_cast<std::uint16_t>(channels * 2));
        w16(16);
        out.write("data", 4);
        w32(data_bytes);
        out.write(reinterpret_cast<const char*>(pcm.data()),
                  static_cast<std::streamsize>(data_bytes));
    }
    WavData wav;
    std::string error;
    REQUIRE(read_wav_file(path, wav, error));
    CHECK(wav.sample_rate == 24000);
    REQUIRE(wav.mono.size() == 4800);
    CHECK(wav.mono[100] == doctest::Approx(0.0f));
    const std::vector<float> up = resample_linear(wav.mono, 24000, 48000);
    CHECK(up.size() == 9600);
    std::filesystem::remove(path);

    WavData bad;
    CHECK_FALSE(read_wav_file(path, bad, error)); // gone
}
