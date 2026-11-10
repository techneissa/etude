// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "synth_sink.hpp"

#include "etude/song_schedule.hpp"
#include "etude/spsc_queue.hpp"

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

struct tsf;

namespace etude::app {

// One command from the main thread to the audio thread. Plain data, copied through a queue.
struct SynthCommand {
    enum class Type : std::uint8_t { NoteOn, NoteOff, Sustain, AllNotesOff, Volume };
    Type type = Type::NoteOn;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0; // NoteOn; Sustain uses 1 for down
    float value = 0.0F;        // Volume
    std::int64_t event_ns = 0; // NoteOn: when the press happened (steady clock), 0 = not timed
};

// When a note of the song started, for measuring the schedule: the time it was due and the time of
// the sample it started on, both steady-clock ns.
struct SongStart {
    std::int64_t due_ns = 0;
    std::int64_t started_ns = 0;
};

// The built-in piano: TinySoundFont playing the acoustic grand from etude-piano.sf2, behind the
// SynthSink interface. No SDL in here, so tests render it offline without an audio device.
//
// Threads: load, the SynthSink calls, set_volume, take_latency and take_song_start run on the main
// thread; render runs on the audio thread (or the test thread). They share only the lock-free
// queues and a few atomics. render never allocates, locks, logs or waits.
//
// The metronome's clicks (PlaybackMessage::click) are not piano notes: the synth makes each one
// itself, a few milliseconds of a damped tone or noise (a wood block, a click or a beep, by the
// General MIDI percussion note it is sent as), starting on the sample its due time falls on like a
// note. They do not need the SoundFont and have no voices of the piano's to steal.
//
// The song's notes arrive with a due time (song_event) and start on the sample that time falls on:
// render is told when its block starts, splits the block at each due note and starts it there. A
// note whose time has passed starts at the first sample of the block.
//
// Without a loaded SoundFont every call is accepted and render writes silence.
class PianoSynth final : public SynthSink {
  public:
    static constexpr int kMaxVoices = 64;
    // The soft limiter holds peaks at this level (-1 dBFS), so chords never clip.
    static constexpr float kCeiling = 0.891F;

    PianoSynth();
    ~PianoSynth() override;
    PianoSynth(const PianoSynth&) = delete;
    PianoSynth& operator=(const PianoSynth&) = delete;

    // Main thread, before audio starts. Loads a SoundFont and sets the output rate (stereo
    // interleaved float). Returns false, and keeps the silent state, when the data is not a
    // usable SoundFont; error() then says why.
    bool load_file(const std::string& path, int sample_rate);
    bool load_memory(const void* data, std::size_t size, int sample_rate);
    bool loaded() const { return font_ != nullptr; }
    const std::string& error() const { return error_; }

    // SynthSink, main thread. note_on stamps the press with the steady clock.
    void note_on(int note, int velocity) override;
    void note_off(int note) override;
    void sustain(bool down) override;
    void all_notes_off() override;
    // Main thread. The song's sound; see SynthSink. A full queue drops a note-on and counts it;
    // losing anything else stops the song's notes, so none stays on.
    void song_event(const SongSoundEvent& event) override;

    // note_on with the time the press happened (steady clock ns, as midi_clock_ns), for latency.
    void note_on_at(int note, int velocity, std::int64_t event_ns);
    // Master volume, 0 to 1 (linear gain, ramped over a few ms).
    void set_volume(float volume);

    // Audio thread. Applies queued commands, then writes `frames` stereo frames (interleaved).
    // Every song note queued starts at the first sample.
    void render(float* out, int frames);
    // The same, for a block whose first sample goes to the device at `block_start_ns` (steady
    // clock): song notes start on the sample their due time falls on.
    void render(float* out, int frames, std::int64_t block_start_ns);

    // Main thread: one press-to-first-sample latency in ns, measured by render from a note-on's
    // event time to the end of the block that started its sound. False when none are waiting.
    bool take_latency(std::int64_t& ns) { return latencies_.pop(ns); }

    // Main thread: when a note of the song started against when it was due. False when none are
    // waiting. Only blocks rendered with a start time are reported.
    bool take_song_start(SongStart& start) { return song_starts_.pop(start); }
    // Song note-ons dropped because the audio thread was behind or its queue was full.
    std::uint64_t dropped_song_notes() const {
        return dropped_song_notes_.load(std::memory_order_relaxed);
    }

    // Commands dropped because the queue was full (main thread pushing faster than audio drains).
    std::uint64_t dropped_commands() const { return commands_.dropped(); }
    // Voices cut to make room for new notes, in total. Any thread.
    std::uint64_t stolen_voices() const { return stolen_.load(std::memory_order_relaxed); }

  private:
    bool finish_load(tsf* font, int sample_rate);
    void push(const SynthCommand& c);
    void apply(const SynthCommand& c);
    void start_note(int note, float velocity);
    void take_song_events();
    void song_message(const PlaybackMessage& m, std::int64_t block_start_ns, int frame);
    void start_click(int note, int velocity);
    void mix_clicks(float* out, int frames);
    void render_span(float* out, int frames);
    void stop_clicks();
    void end_song_note(int note);
    void stop_song_notes();
    void stop_song();
    void limit(float* out, int frames);

    tsf* font_ = nullptr;
    int preset_index_ = 0;
    float sample_rate_ = 48000.0F;
    std::int64_t rate_ = 48000; // Hz, the same as sample_rate_
    std::string error_;

    SpscQueue<SynthCommand, 512> commands_;       // main -> audio
    SpscQueue<std::int64_t, 256> latencies_;      // audio -> main
    SpscQueue<SongSoundEvent, 1024> song_events_; // main -> audio
    SpscQueue<SongStart, 256> song_starts_;       // audio -> main
    std::atomic<bool> song_lost_{false};          // main -> audio: an event was lost, stop the song
    std::atomic<std::uint64_t> dropped_song_notes_{0};
    std::atomic<int> active_voices_{0};
    std::atomic<std::uint64_t> stolen_{0};

    // One click sounding. Audio thread only, fixed storage.
    struct ClickVoice {
        bool active = false;
        float phase = 0.0F;      // radians of the tone
        float step = 0.0F;       // radians per sample
        float env = 0.0F;        // current level
        float decay = 0.0F;      // per-sample factor of the level
        float noise = 0.0F;      // level of the noise burst
        float noise_decay = 0.0F;
        std::uint32_t seed = 1;
        float lowpass = 0.0F;    // one-pole state of the noise
    };
    static constexpr int kMaxClickVoices = 8;
    std::array<ClickVoice, kMaxClickVoices> clicks_{};
    int next_click_ = 0;

    // Audio thread only.
    DueQueue<512> song_pending_;     // song messages waiting for their due time
    std::bitset<128> song_sounding_; // keys the song started and has not stopped
    bool song_pedal_ = false;
    std::uint64_t seen_lost_offs_ = 0;
    float volume_ = 1.0F;
    float volume_target_ = 1.0F;
    float volume_step_ = 0.0F; // per-sample ramp coefficient
    float limiter_env_ = 0.0F; // peak follower
    float limiter_release_ = 0.0F;
    std::int64_t timed_[16] = {}; // event times of the note-ons applied in this block
    int timed_count_ = 0;
};

} // namespace etude::app
