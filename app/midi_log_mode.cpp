// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli.hpp"

#include "audio_out.hpp"
#include "keyboard_session.hpp"
#include "mic_source.hpp"
#include "midi_input.hpp"

#include "etude/pitch.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace etude::app {
namespace {

// --- Headless MIDI log --------------------------------------------------------------------------

volatile std::sig_atomic_t g_stop = 0;

// From --octaves: added to the octave number in every note name the log prints.
int g_octave_offset = 0;

void on_signal(int /*sig*/) { g_stop = 1; }

const char* event_name(etude::MidiEventType type) {
    switch (type) {
    case etude::MidiEventType::NoteOn:
        return "note-on";
    case etude::MidiEventType::NoteOff:
        return "note-off";
    case etude::MidiEventType::ControlChange:
        return "cc";
    case etude::MidiEventType::Other:
        break;
    }
    return "other";
}

void log_transition(const Session& s, const KeyTransition& t) {
    switch (t.type) {
    case KeyTransitionType::Pressed:
        std::printf("    key %s down%s, %d held\n",
                    etude::note_name(t.note, false, g_octave_offset).c_str(),
                    s.layout.range().contains(t.note) ? "" : " (outside the keyboard's range)",
                    s.keys.held_count());
        break;
    case KeyTransitionType::Released:
        std::printf("    key %s up, %d held\n",
                    etude::note_name(t.note, false, g_octave_offset).c_str(),
                    s.keys.held_count());
        break;
    case KeyTransitionType::AllReleased:
        std::printf("    all keys up (%d released)\n", t.released_count);
        break;
    case KeyTransitionType::None:
        break;
    }
}

// The microphone's notes and doubts, printed as they arrive. True once the recording was fed in
// and heard.
bool log_mic(etude::app::MicSource& mic, const etude::AppSettings& app_settings,
             Session& session, std::vector<MidiEvent>& mic_events,
             std::vector<etude::app::UnsureMark>& mic_marks, std::int64_t& wav_done_ns) {
    const std::int64_t now = midi_clock_ns();
    mic.apply(app_settings, now);
    mic_events.clear();
    mic_marks.clear();
    mic.poll(now, mic_events, mic_marks);
    for (const MidiEvent& ev : mic_events) {
        const double delay_ms = static_cast<double>(now - ev.time_ns) / 1e6;
        std::printf("t=%lld ns (onset %.1f ms ago) mic %s %s\n", static_cast<long long>(ev.time_ns),
                    delay_ms, event_name(ev.type),
                    etude::note_name(ev.data1, false, g_octave_offset).c_str());
        log_transition(session, apply_event(session, ev));
    }
    for (const etude::app::UnsureMark& m : mic_marks) {
        std::printf("mic not sure (near %s)\n",
                    m.guess >= 0 ? etude::note_name(m.guess, false, g_octave_offset).c_str()
                                 : "nowhere in particular");
    }
    mic.log_presented(midi_clock_ns(), false);
    if (mic.wav_finished()) {
        if (wav_done_ns == 0) {
            wav_done_ns = now;
        } else if (now - wav_done_ns > 1'000'000'000) {
            return true; // the recording was fed in and heard
        }
    }
    return false;
}

} // namespace

// Prints decoded events and key transitions to stdout without opening a window, for checking MIDI
// where no display is available. Runs until Ctrl-C.
int run_midi_log(const WindowOptions& opt, int octave_offset) {
    g_octave_offset = octave_offset;
    const etude::app::AudioOptions& audio_options = opt.audio;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    auto midi = std::make_unique<MidiInput>();
    Session session;
    std::printf("Listening for MIDI. Ctrl-C to stop.\n");

    // --audio-latency-log: the MIDI notes also play the built-in piano, to measure press-to-sound
    // without a window (e.g. with `just midi-demo`).
    etude::app::AudioOut audio;
    if (audio_options.latency_log) {
        audio.start(audio_options);
    }

    // The microphone, when asked for: its notes are printed as they arrive.
    etude::AppSettings app_settings;
    if (opt.input_mode) {
        app_settings.input_mode = *opt.input_mode;
    } else if (!opt.mic_wav.empty()) {
        app_settings.input_mode = etude::InputMode::Microphone;
    }
    etude::app::MicSource mic({opt.mic_wav, opt.mic_log});
    std::vector<MidiEvent> mic_events;
    std::vector<etude::app::UnsureMark> mic_marks;
    std::int64_t wav_done_ns = 0;

    bool sustain = false;
    while (g_stop == 0) {
        MidiInput::pump_system_events();
        const auto changes = midi->update();
        const int held_before = session.keys.held_count();
        bool label_changed = false;
        apply_port_changes(session, *midi, changes, label_changed);
        if (label_changed || changes.closed > 0) {
            std::printf("keyboard: %s (range %d..%d)", session.label.c_str(),
                        session.layout.range().lowest, session.layout.range().highest);
            for (const auto& name : midi->open_names()) {
                std::printf(" [%s]", name.c_str());
            }
            std::printf("\n");
            if (changes.closed > 0) {
                std::printf("    input removed, keys cleared (%d were held)\n", held_before);
            }
        }

        midi->drain([&](const MidiEvent& ev) {
            audio.play_midi(ev);
            const double delay_ms =
                static_cast<double>(etude::app::midi_clock_ns() - ev.time_ns) / 1e6;
            std::printf("t=%lld ns (+%.3f ms in queue) %s ch%d %d %d\n",
                        static_cast<long long>(ev.time_ns), delay_ms, event_name(ev.type),
                        ev.channel + 1, ev.data1, ev.data2);
            log_transition(session, apply_event(session, ev));
            if (session.keys.sustain_down() != sustain) {
                sustain = session.keys.sustain_down();
                std::printf("    sustain %s\n", sustain ? "down" : "up");
            }
        });
        if (app_settings.input_mode != etude::InputMode::Midi &&
            log_mic(mic, app_settings, session, mic_events, mic_marks, wav_done_ns)) {
            break;
        }
        audio.update(etude::app::midi_clock_ns());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    audio.report(etude::app::midi_clock_ns());

    std::printf("stopped. dropped %llu events, %llu presses outside the range\n",
                static_cast<unsigned long long>(midi->dropped()),
                static_cast<unsigned long long>(session.out_of_range));
    return 0;
}
} // namespace etude::app
