// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_timing_test.hpp"

#include "builtin_piano_sink.hpp"
#include "dev_jitter.hpp"
#include "midi_input.hpp"
#include "midi_output.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"
#include "timed_sender.hpp"

#include <libremidi/libremidi.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace etude::app {
namespace {

constexpr std::int64_t kFrameNs = 1'000'000'000 / 60;
constexpr const char* kVirtualPort = "timing-receiver"; // not "etude...": those are never a piano

double ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }

void print_row(const char* what, std::vector<std::int64_t> values) {
    if (values.empty()) {
        std::printf("  %-34s no samples\n", what);
        return;
    }
    std::sort(values.begin(), values.end());
    const auto at = [&](double q) {
        return values[static_cast<std::size_t>(q * static_cast<double>(values.size() - 1))];
    };
    std::printf("  %-34s n=%-5zu min %8.3f  median %8.3f  p95 %8.3f  max %8.3f  (ms)\n", what,
                values.size(), ms(values.front()), ms(at(0.5)), ms(at(0.95)), ms(values.back()));
}

std::filesystem::path default_song() {
#ifdef ETUDE_DEV_SONGS_DIR
    return std::filesystem::path(ETUDE_DEV_SONGS_DIR) / "bach-bwv846-prelude.mid";
#else
    return {};
#endif
}

// Everything the two modes share: the song, the 60 FPS loop with its random extra sleep, and the
// bookkeeping of what was handed over when.
class Run {
  public:
    explicit Run(const SongTimingOptions& options) : options_(options) {}

    bool load() {
        const std::filesystem::path path =
            options_.song.empty() ? default_song() : std::filesystem::path(options_.song);
        SongFileLoad load = load_song_file(path);
        if (!load.ok) {
            std::fprintf(stderr, "song timing: %s\n", load.error.c_str());
            return false;
        }
        std::printf("song timing: %s, %.0f s of it, lead %.1f ms, frame jitter 0..%d ms\n",
                    load.song.title.c_str(), options_.seconds, ms(options_.lead_ns),
                    options_.frame_jitter_ms);
        SongOptions o;
        o.sound_lead_ns = options_.lead_ns;
        songs_.set_options(o, midi_clock_ns());
        songs_.load(std::move(load.song), midi_clock_ns());
        songs_.set_song_sound(SongSound::BothHands, midi_clock_ns());
        // 95%: the notes are then not a whole number of frames apart, so over a song they land on
        // every phase of the frame, as a player's song does.
        songs_.set_speed(95, midi_clock_ns());
        // Start between two notes: one due the instant play is pressed is a command's, not a
        // schedule's, and starts when the audio thread next looks.
        songs_.seek(1.1, midi_clock_ns());
        return true;
    }

    // Plays for options_.seconds. `route` gets each frame's events and the frame time.
    template <typename Route, typename Frame> void play(Route&& route, Frame&& each_frame) {
        songs_.play(midi_clock_ns());
        std::int64_t next = midi_clock_ns() + kFrameNs;
        const std::int64_t end = next + static_cast<std::int64_t>(options_.seconds * 1e9);
        std::vector<SongSoundEvent> events;
        while (true) {
            const std::int64_t wait = next - midi_clock_ns();
            if (wait > 0) {
                SDL_DelayPrecise(static_cast<Uint64>(wait));
                next += kFrameNs;
            } else {
                next = midi_clock_ns() + kFrameNs;
            }
            const std::int64_t now = midi_clock_ns();
            if (now >= end) {
                break;
            }
            songs_.update(now, {});
            frames_.push_back({now, songs_.position_s()});
            events.clear();
            songs_.take_sound_events(events);
            for (const SongSoundEvent& e : events) {
                if (e.kind == SongSoundEvent::Kind::Message && starts_note(e.message)) {
                    // How far ahead of its due time the frame handed the note over.
                    handoff_margin_.push_back(e.message.at_ns - now);
                    dues_.push_back(e.message.at_ns);
                    song_s_.push_back(e.message.song_s);
                }
            }
            route(events, now);
            each_frame(now);
            if (options_.frame_jitter_ms > 0) {
                SDL_Delay(static_cast<Uint32>(dev_jitter_ms(options_.frame_jitter_ms)));
            }
        }
        std::vector<SongSoundEvent> rest;
        songs_.shutdown(midi_clock_ns(), rest);
        route(rest, midi_clock_ns());
    }

    SongController& songs() { return songs_; }

    // When the screen shows note `i` reach the hit line: the song position follows the frames'
    // times linearly between two frames, so the note's song time gives the moment. -1 when the run
    // ended before a frame came after it.
    std::int64_t crossing_ns(std::size_t i) const {
        const double s = song_s_[i];
        for (std::size_t k = 1; k < frames_.size(); ++k) {
            if (frames_[k - 1].position_s <= s && s < frames_[k].position_s) {
                const double f = (s - frames_[k - 1].position_s) /
                                 (frames_[k].position_s - frames_[k - 1].position_s);
                return frames_[k - 1].ns +
                       std::llround(f * static_cast<double>(frames_[k].ns - frames_[k - 1].ns));
            }
        }
        return -1;
    }
    // One value per note: `at(i)` minus the hit-line crossing, for the notes that crossed.
    template <typename At> std::vector<std::int64_t> against_screen(std::size_t n, At at) const {
        std::vector<std::int64_t> out;
        for (std::size_t i = 0; i < n && i < song_s_.size(); ++i) {
            const std::int64_t cross = crossing_ns(i);
            if (cross >= 0) {
                out.push_back(at(i) - cross);
            }
        }
        return out;
    }
    const std::vector<std::int64_t>& handoff_margin() const { return handoff_margin_; }
    const std::vector<std::int64_t>& dues() const { return dues_; }

  private:
    struct Frame {
        std::int64_t ns = 0;
        double position_s = 0.0;
    };
    SongTimingOptions options_;
    SongController songs_;
    std::vector<Frame> frames_;
    std::vector<double> song_s_;
    std::vector<std::int64_t> handoff_margin_;
    std::vector<std::int64_t> dues_;
};

int run_synth(const SongTimingOptions& options) {
    AudioOut audio;
    if (!audio.start(options.audio)) {
        return 1;
    }
    Run run(options);
    if (!run.load()) {
        return 1;
    }
    BuiltinSongSink sink(audio.sink());
    audio.record_clock_residuals(true);
    audio.synth()->set_volume(0.0F); // the timing does not depend on the level; spare the speakers
    std::vector<std::int64_t> late;  // started - due
    std::vector<std::int64_t> started;  // the sample each note started on
    std::vector<std::int64_t> residual; // callback wake-up against the sample clock
    PianoSynth* synth = audio.synth();
    const auto drain = [&] {
        SongStart start;
        while (synth->take_song_start(start)) {
            late.push_back(start.started_ns - start.due_ns);
            started.push_back(start.started_ns);
        }
        std::int64_t r = 0;
        while (audio.take_clock_residual(r)) {
            residual.push_back(r);
        }
    };
    run.play(
        [&](std::vector<SongSoundEvent>& events, std::int64_t) {
            for (const SongSoundEvent& e : events) {
                sink.submit(e);
            }
        },
        [&](std::int64_t now) {
            audio.update(now);
            drain();
        });
    SDL_Delay(200);
    drain();
    sink.all_off();
    std::printf("built-in piano, started minus due (positive: late):\n");
    print_row("note start minus due", late);
    print_row("note start minus hit line",
              run.against_screen(started.size(), [&](std::size_t i) { return started[i]; }));
    std::printf("  handed to the audio thread before it was due by:\n");
    print_row("handoff margin", run.handoff_margin());
    std::printf("  audio callback wake-up against the sample clock:\n");
    print_row("callback jitter", residual);
    std::printf("  notes handed over %zu, started %zu, dropped %llu\n", run.dues().size(),
                late.size(), static_cast<unsigned long long>(synth->dropped_song_notes()));
    audio.stop();
    return 0;
}

int run_midi(const SongTimingOptions& options) {
    std::mutex arrivals_mutex;
    std::vector<std::int64_t> arrivals;
    libremidi::input_configuration conf;
    conf.on_raw_data = [&](std::span<const std::uint8_t> bytes, libremidi::timestamp) {
        if (bytes.size() >= 3 && (bytes[0] & 0xF0) == 0x90 && bytes[2] > 0) {
            const std::int64_t now = midi_clock_ns();
            const std::lock_guard<std::mutex> lock(arrivals_mutex);
            arrivals.push_back(now);
        }
    };
    libremidi::midi_in receiver(conf);
    if (receiver.open_virtual_port(kVirtualPort) != stdx::error{}) {
        std::fprintf(stderr, "song timing: cannot open a virtual MIDI port\n");
        return 1;
    }

    MidiOutput piano_out;
    const std::vector<std::string> names{kVirtualPort};
    // The app's watcher thread does this scan; a tool with no frame loop runs it itself.
    const auto scan = piano_out.port_task();
    for (int i = 0; i < 50 && !piano_out.connected(); ++i) {
        SDL_Delay(100);
        scan(names);
        piano_out.update();
    }
    if (!piano_out.connected()) {
        std::fprintf(stderr, "song timing: the MIDI output did not find the virtual port\n");
        return 1;
    }

    Run run(options);
    if (!run.load()) {
        return 1;
    }
    std::mutex sent_mutex;
    std::vector<std::int64_t> sent;
    TimedSender sender([&](const PlaybackMessage& m) {
        if (starts_note(m)) {
            const std::int64_t now = midi_clock_ns();
            const std::lock_guard<std::mutex> lock(sent_mutex);
            sent.push_back(now);
        }
        piano_out.play(m);
    });
    run.play(
        [&](std::vector<SongSoundEvent>& events, std::int64_t) {
            for (const SongSoundEvent& e : events) {
                if (options.direct) {
                    // As before notes were scheduled: the frame thread sends what the frame made.
                    if (e.kind == SongSoundEvent::Kind::Message) {
                        if (starts_note(e.message)) {
                            const std::lock_guard<std::mutex> lock(sent_mutex);
                            sent.push_back(midi_clock_ns());
                        }
                        piano_out.play(e.message);
                    }
                } else {
                    sender.submit(e);
                }
            }
        },
        [](std::int64_t) {});
    sender.stop();
    SDL_Delay(300);
    piano_out.all_off();

    const std::lock_guard<std::mutex> l1(arrivals_mutex);
    const std::lock_guard<std::mutex> l2(sent_mutex);
    const std::vector<std::int64_t>& dues = run.dues();
    const std::size_t n = std::min({dues.size(), sent.size(), arrivals.size()});
    std::vector<std::int64_t> send_late;
    std::vector<std::int64_t> arrive_late;
    for (std::size_t i = 0; i < n; ++i) {
        send_late.push_back(sent[i] - dues[i]);
        arrive_late.push_back(arrivals[i] - dues[i]);
    }
    // The receiver's own delay (CoreMIDI) is the same for every message; the spread around its
    // smallest value is the jitter.
    std::vector<std::int64_t> spread;
    if (!arrive_late.empty()) {
        const std::int64_t least = *std::min_element(arrive_late.begin(), arrive_late.end());
        for (const std::int64_t v : arrive_late) {
            spread.push_back(v - least);
        }
    }
    std::printf("MIDI out to a virtual port, %s, against the time each note-on was due:\n",
                options.direct ? "sent from the frame thread" : "sent by the timed sender");
    print_row("sent minus due", send_late);
    print_row("received minus due", arrive_late);
    print_row("received minus hit line",
              run.against_screen(n, [&](std::size_t i) { return arrivals[i]; }));
    print_row("received minus due, less the least", spread);
    std::printf("  handed over before it was due by:\n");
    print_row("handoff margin", run.handoff_margin());
    std::printf("  note-ons due %zu, sent %zu, received %zu, dropped %llu\n", dues.size(),
                sent.size(), arrivals.size(),
                static_cast<unsigned long long>(sender.dropped_note_ons()));
    return 0;
}

} // namespace

int run_song_timing(const SongTimingOptions& options) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    return options.midi ? run_midi(options) : run_synth(options);
}

} // namespace etude::app
