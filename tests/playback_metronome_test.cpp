// SPDX-License-Identifier: GPL-3.0-or-later
// playback: metronome (split from playback_test.cpp).
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

#include "playback_test_support.hpp"

using namespace etude;
using namespace etude::test;
using MsgKind = PlaybackMessage::Kind;
using OpKind = TransportOp::Kind;
using namespace playback_test_support;

// ---- Metronome ---------------------------------------------------------------------------------

namespace {

std::vector<TransportOp> count_in_clicks(int beats) {
    CountInInput in;
    in.total_beats = beats;
    in.beats_left = beats;
    int next = 0;
    std::vector<TransportOp> out;
    plan_count_in(in, 0, sec(100), {}, next, out);
    return out;
}

} // namespace

TEST_CASE("playback: count-in and metronome clicks become percussion notes when asked") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 8.0, 0, 60)}));
    ClickSound sound;
    sound.enabled = true;
    r.playback.set_click_sound(sound);
    r.play(0.0);
    r.last.clear();
    for (const TransportOp& op : count_in_clicks(4)) {
        r.playback.apply(op, r.last);
    }
    r.piano.feed(r.last);
    std::vector<PlaybackMessage> ons = of_kind(r.piano.log, MsgKind::NoteOn);
    std::vector<PlaybackMessage> clicks;
    std::copy_if(ons.begin(), ons.end(), std::back_inserter(clicks),
                 [](const PlaybackMessage& m) { return m.channel == 9; });
    REQUIRE(clicks.size() == 4);
    CHECK(clicks[0].data == sound.accent_note);
    CHECK(clicks[0].value == sound.accent_velocity);
    CHECK(clicks[1].data == sound.note);
    CHECK(clicks[1].at_ns == sec(0.5));
    // Each click note ends by itself shortly after.
    std::vector<PlaybackMessage> offs = of_kind(r.piano.log, MsgKind::NoteOff);
    CHECK(std::count_if(offs.begin(), offs.end(),
                        [](const PlaybackMessage& m) { return m.channel == 9; }) == 4);
    CHECK_FALSE(r.piano.key_down(9, sound.accent_note));
    CHECK_FALSE(r.piano.key_down(9, sound.note));
}

TEST_CASE("playback: clicks are silent unless a click sound is enabled") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 8.0, 0, 60)}));
    r.play(0.0);
    r.last.clear();
    for (const TransportOp& op : count_in_clicks(4)) {
        r.playback.apply(op, r.last);
    }
    r.update(3.0);
    for (const PlaybackMessage& m : r.last) {
        CHECK(m.channel == 0);
    }
}

// ---- Reset by CC121
// ------------------------------------------------------------------------------

TEST_CASE("playback: a reset-all-controllers in the file is remembered when restoring state") {
    Rig r(make_song({AbsTrack("Right")
                         .cc(0.5, 0, 7, 50)
                         .cc(1.0, 0, 121, 0) // the piano puts the volume back to 100 itself
                         .note(0.0, 5.0, 0, 60)}));
    r.play(0.0);
    r.update(1.5);
    REQUIRE(r.piano.controller(0, 7) == 100);
    // The file sets 50 at 0.5 s. Playback must know the piano is at 100 now, not at 50.
    r.seek(1.5, 0.7);
    CHECK(r.piano.controller(0, 7) == 50);
}

// ---- Stuck-note safety across every transition, with random commands ----------------------------

namespace {

struct FileState {
    int program = 0;
    int bend = 8192;
    std::array<int, 128> cc{};
    FileState() {
        cc.fill(0);
        cc[7] = 100;
        cc[10] = 64;
        cc[11] = 127;
        cc[91] = 40;
    }
};

// What the file says each channel's state is at `position`: events at or before it.
std::array<FileState, 16> file_state_at(const Song& song, double position) {
    std::array<FileState, 16> st{};
    for (const ChannelEvent& e : song.events) {
        if (e.time_s > position) {
            break;
        }
        FileState& s = st[e.channel];
        switch (e.kind) {
        case ChannelEventKind::Program:
            s.program = e.value;
            break;
        case ChannelEventKind::PitchBend:
            s.bend = e.value;
            break;
        case ChannelEventKind::Control:
            s.cc[e.number] = e.value;
            break;
        }
    }
    return st;
}

Song random_song(std::mt19937& rng) {
    std::uniform_real_distribution<double> start(0.0, 20.0);
    std::uniform_real_distribution<double> len(0.05, 6.0);
    std::uniform_int_distribution<int> pitch(36, 84);
    std::uniform_int_distribution<int> chan(0, 3);
    std::uniform_int_distribution<int> value(0, 127);
    std::uniform_int_distribution<int> coin(0, 3);
    AbsTrack right("Right Hand");
    AbsTrack left("Left Hand");
    for (int i = 0; i < 60; ++i) {
        double s = start(rng);
        int ch = chan(rng);
        (coin(rng) < 2 ? right : left).note(s, s + len(rng), ch == 3 ? 9 : ch, pitch(rng));
    }
    for (int i = 0; i < 40; ++i) {
        double s = start(rng);
        int ch = chan(rng);
        switch (coin(rng)) {
        case 0:
            right.program(s, ch, value(rng));
            break;
        case 1:
            right.cc(s, ch, 64, value(rng));
            break;
        case 2:
            left.cc(s, ch,
                    std::array<int, 5>{0, 7, 11, 32, 91}[static_cast<std::size_t>(value(rng) % 5)],
                    value(rng));
            break;
        default:
            right.bend(s, ch, value(rng) * 128);
            break;
        }
    }
    return make_song({right, left});
}

} // namespace

TEST_CASE("playback: no stuck notes and the right state after any sequence of transport commands") {
    std::mt19937 rng(2024);
    for (int round = 0; round < 25; ++round) {
        Song song = random_song(rng);
        Rig r(song);
        std::uniform_real_distribution<double> dt(0.001, 1.5);
        std::uniform_real_distribution<double> where(0.0, song.duration_s);
        std::uniform_int_distribution<int> action(0, 11);
        std::uniform_int_distribution<int> speed(25, 150);
        double now = 0.0;
        for (int step = 0; step < 120; ++step) {
            now += dt(rng);
            std::vector<TransportOp> ops;
            switch (action(rng)) {
            case 0:
                r.transport.play(sec(now));
                break;
            case 1:
                r.transport.pause(sec(now));
                break;
            case 2:
                r.transport.seek(sec(now), where(rng));
                break;
            case 3:
                r.transport.set_speed_percent(sec(now), speed(rng));
                break;
            case 4: {
                double a = where(rng);
                r.transport.set_loop(sec(now), a, a + 0.2 + dt(rng) * 2);
                break;
            }
            case 5:
                r.transport.clear_loop(sec(now));
                break;
            case 6:
                r.transport.hold(sec(now));
                break;
            case 7:
                r.transport.release(sec(now));
                break;
            case 8: {
                std::vector<PlaybackMessage> out;
                Hand h = static_cast<Hand>(action(rng) % 3);
                r.playback.set_hand_muted(h, action(rng) % 2 == 0, sec(now), out);
                r.piano.feed(out);
                break;
            }
            default:
                r.transport.update(sec(now));
                break;
            }
            // Walk the ops one at a time to check the piano between them.
            r.transport.take_ops(ops);
            for (const TransportOp& op : ops) {
                std::vector<PlaybackMessage> out;
                r.playback.apply(op, out);
                r.piano.feed(out);
                for (const PlaybackMessage& m : out) {
                    CHECK(m.data < 128);
                    CHECK(m.value < 16384);
                }
                switch (op.kind) {
                case OpKind::Pause:
                case OpKind::Seek:
                case OpKind::Wrap:
                case OpKind::End: {
                    CAPTURE(round);
                    CAPTURE(step);
                    CHECK_FALSE(r.piano.ringing());
                    break;
                }
                default:
                    break;
                }
                if (op.kind == OpKind::Play || op.kind == OpKind::Wrap ||
                    (op.kind == OpKind::Seek && r.playback.running())) {
                    auto expected = file_state_at(song, op.to_s);
                    for (int ch = 0; ch < 16; ++ch) {
                        CAPTURE(ch);
                        CAPTURE(op.to_s);
                        CHECK(r.piano.program(ch) ==
                              expected[static_cast<std::size_t>(ch)].program);
                        CHECK(r.piano.bend(ch) == expected[static_cast<std::size_t>(ch)].bend);
                        for (int n : {0, 7, 10, 11, 32, 64, 91}) {
                            CAPTURE(n);
                            CHECK(r.piano.controller(ch, n) ==
                                  expected[static_cast<std::size_t>(ch)]
                                      .cc[static_cast<std::size_t>(n)]);
                        }
                    }
                }
            }
        }
        r.pause(now + 1.0);
        CHECK_FALSE(r.piano.ringing());
        std::vector<PlaybackMessage> out;
        r.playback.silence(sec(now + 2.0), out);
        r.piano.feed(out);
        CHECK_FALSE(r.piano.ringing());
    }
}
