// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the playback_*test.cpp files (split from playback_test.cpp): the helpers more than one of them uses.
#pragma once

#include "etude/metronome.hpp"
#include "etude/playback.hpp"
#include "etude/evaluator.hpp"
#include "etude/keyboard.hpp"
#include "etude/midi_file.hpp"
#include "etude/note_window.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"
#include "smf_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace playback_test_support {

using namespace etude;
using namespace etude::test;
using MsgKind = PlaybackMessage::Kind;
using OpKind = TransportOp::Kind;

constexpr std::int64_t sec(double s) { return static_cast<std::int64_t>(s * 1e9); }

// 120 BPM and 480 ticks per quarter: a second is 960 ticks.
constexpr std::uint32_t tk(double s) { return static_cast<std::uint32_t>(s * 960.0); }

// One track written with absolute ticks, so a test reads as a list of what happens when.
class AbsTrack {
  public:
    explicit AbsTrack(std::string name) : name_(std::move(name)) {}
    AbsTrack& on(std::uint32_t tick, int ch, int pitch, int vel = 100) {
        return add(tick, {u8(0x90 | ch), u8(pitch), u8(vel)});
    }
    AbsTrack& off(std::uint32_t tick, int ch, int pitch) {
        return add(tick, {u8(0x80 | ch), u8(pitch), 64});
    }
    AbsTrack& note(double start_s, double end_s, int ch, int pitch, int vel = 100) {
        return on(tk(start_s), ch, pitch, vel).off(tk(end_s), ch, pitch);
    }
    AbsTrack& cc(double at_s, int ch, int controller, int value) {
        return add(tk(at_s), {u8(0xB0 | ch), u8(controller), u8(value)});
    }
    AbsTrack& program(double at_s, int ch, int program) {
        return add(tk(at_s), {u8(0xC0 | ch), u8(program)});
    }
    AbsTrack& bend(double at_s, int ch, int value14) {
        return add(tk(at_s), {u8(0xE0 | ch), u8(value14 & 0x7F), u8(value14 >> 7)});
    }
    Bytes body() const {
        std::vector<std::pair<std::uint32_t, Bytes>> sorted = events_;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        Bytes out;
        put_vlq(out, 0);
        out.insert(out.end(), {0xFF, 0x03, static_cast<std::uint8_t>(name_.size())});
        out.insert(out.end(), name_.begin(), name_.end());
        std::uint32_t last = 0;
        for (const auto& [tick, bytes] : sorted) {
            put_vlq(out, tick - last);
            out.insert(out.end(), bytes.begin(), bytes.end());
            last = tick;
        }
        out.insert(out.end(), {0x00, 0xFF, 0x2F, 0x00});
        return out;
    }

  private:
    static std::uint8_t u8(int v) { return static_cast<std::uint8_t>(v); }
    AbsTrack& add(std::uint32_t tick, Bytes bytes) {
        events_.emplace_back(tick, std::move(bytes));
        return *this;
    }
    std::string name_;
    std::vector<std::pair<std::uint32_t, Bytes>> events_;
};

inline Song make_song(const std::vector<AbsTrack>& tracks) {
    Bytes file = header(1, static_cast<int>(tracks.size()), 480);
    for (const AbsTrack& t : tracks) {
        put_chunk(file, "MTrk", t.body());
    }
    MidiLoadResult r = load_midi(file.data(), file.size());
    REQUIRE(r.ok);
    return std::move(r.song);
}

// A stand-in for the piano: what a real General MIDI keyboard would end up doing with the stream.
class FakePiano {
  public:
    FakePiano() {
        for (std::size_t ch = 0; ch < 16; ++ch) {
            bend_[ch] = 8192;
            cc_[ch].fill(0);
            cc_[ch][7] = 100;
            cc_[ch][10] = 64;
            cc_[ch][11] = 127;
            cc_[ch][91] = 40;
            key_[ch].fill(false);
            held_[ch].fill(false);
        }
    }
    void feed(const std::vector<PlaybackMessage>& messages) {
        for (const PlaybackMessage& m : messages) {
            CHECK(m.channel < 16);
            apply(m);
            log.push_back(m);
        }
    }
    bool ringing() const {
        for (std::size_t ch = 0; ch < 16; ++ch) {
            for (std::size_t k = 0; k < 128; ++k) {
                if (key_[ch][k] || held_[ch][k]) {
                    return true;
                }
            }
        }
        return false;
    }
    bool key_down(int ch, int note) const { return key_[idx(ch)][idx(note)]; }
    int program(int ch) const { return program_[idx(ch)]; }
    int controller(int ch, int n) const { return cc_[idx(ch)][idx(n)]; }
    int bend(int ch) const { return bend_[idx(ch)]; }
    std::vector<PlaybackMessage> log;

  private:
    static std::size_t idx(int v) { return static_cast<std::size_t>(v); }
    void apply(const PlaybackMessage& m) {
        const std::size_t ch = m.channel;
        switch (m.kind) {
        case MsgKind::NoteOn:
            key_[ch][m.data] = true;
            break;
        case MsgKind::NoteOff:
            key_[ch][m.data] = false;
            if (cc_[ch][64] >= 64) {
                held_[ch][m.data] = true; // the pedal keeps it ringing
            }
            break;
        case MsgKind::ProgramChange:
            program_[ch] = m.data;
            break;
        case MsgKind::PitchBend:
            bend_[ch] = m.value;
            break;
        case MsgKind::ControlChange:
            if (m.data == 123) {
                key_[ch].fill(false);
                held_[ch].fill(false);
                break;
            }
            if (m.data == 121) {
                for (std::size_t n = 1; n < 120; ++n) {
                    if (n != 32) {
                        cc_[ch][n] = n == 7 ? 100 : n == 10 ? 64 : n == 11 ? 127 : n == 91 ? 40 : 0;
                    }
                }
                bend_[ch] = 8192;
                held_[ch].fill(false);
                break;
            }
            cc_[ch][m.data] = m.value;
            if (m.data == 64 && m.value < 64) {
                held_[ch].fill(false);
            }
            break;
        }
    }
    std::array<std::array<bool, 128>, 16> key_{};
    std::array<std::array<bool, 128>, 16> held_{};
    std::array<std::array<int, 128>, 16> cc_{};
    std::array<int, 16> program_{};
    std::array<int, 16> bend_{};
};

// A transport, a playback and a piano wired together the way the app will wire them.
struct Rig {
    Song song;
    Transport transport;
    Playback playback;
    FakePiano piano;
    std::vector<PlaybackMessage> last; // what the last pump() produced

    explicit Rig(Song s) : song(std::move(s)), playback(song) { transport.set_song(song); }

    void pump() {
        std::vector<TransportOp> ops;
        transport.take_ops(ops);
        last.clear();
        for (const TransportOp& op : ops) {
            playback.apply(op, last);
        }
        piano.feed(last);
    }
    void play(double now_s) { transport.play(sec(now_s)), pump(); }
    void pause(double now_s) { transport.pause(sec(now_s)), pump(); }
    void update(double now_s) { transport.update(sec(now_s)), pump(); }
    void seek(double now_s, double to_s) { transport.seek(sec(now_s), to_s), pump(); }
};

inline std::vector<PlaybackMessage> of_kind(const std::vector<PlaybackMessage>& v, MsgKind kind) {
    std::vector<PlaybackMessage> out;
    std::copy_if(v.begin(), v.end(), std::back_inserter(out),
                 [&](const PlaybackMessage& m) { return m.kind == kind; });
    return out;
}

// "on 60", "off 60", "pc 5", "cc 64=127", "bend 8192", in order, ignoring times.
inline std::vector<std::string> describe(const std::vector<PlaybackMessage>& v) {
    std::vector<std::string> out;
    for (const PlaybackMessage& m : v) {
        std::string d;
        switch (m.kind) {
        case MsgKind::NoteOn:
            d = "on " + std::to_string(m.data);
            break;
        case MsgKind::NoteOff:
            d = "off " + std::to_string(m.data);
            break;
        case MsgKind::ProgramChange:
            d = "pc " + std::to_string(m.data);
            break;
        case MsgKind::ControlChange:
            d = "cc " + std::to_string(m.data) + "=" + std::to_string(m.value);
            break;
        case MsgKind::PitchBend:
            d = "bend " + std::to_string(m.value);
            break;
        }
        d += "@" + std::to_string(m.channel);
        out.push_back(d);
    }
    return out;
}

using Strings = std::vector<std::string>;

// The notes only: the all-notes-off at the end of a song is not what these tests are about.
inline Strings note_messages(const std::vector<PlaybackMessage>& v) {
    std::vector<PlaybackMessage> notes;
    std::copy_if(v.begin(), v.end(), std::back_inserter(notes), [](const PlaybackMessage& m) {
        return m.kind == MsgKind::NoteOn || m.kind == MsgKind::NoteOff;
    });
    return describe(notes);
}

} // namespace playback_test_support
