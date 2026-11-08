// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the sections_controller_*test.cpp files (split from sections_controller_test.cpp): the helpers more than one of them uses.
#pragma once

#include "sections_controller.hpp"
#include "song_transport.hpp"
#include "etude/song_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <vector>

namespace sections_controller_test_support {

using namespace etude;
using namespace etude::app;

constexpr std::int64_t kMs = 1'000'000;

// `bars` bars of 4/4 at 120 BPM (a bar is 2 s): the right hand plays four quarter notes in each,
// the left hand holds a low note.
inline LoadedSong song_of(int bars, std::uint64_t hash = 0x77, bool left_hand = true) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            Note n;
            n.start_tick = (bar * 4 + beat) * 480;
            n.end_tick = n.start_tick + 400;
            n.pitch = static_cast<std::uint8_t>(72 + beat * 2);
            n.velocity = 80;
            n.hand = Hand::Right;
            s.notes.push_back(n);
        }
        if (!left_hand) {
            continue;
        }
        Note l;
        l.start_tick = bar * 4 * 480;
        l.end_tick = l.start_tick + 4 * 480 - 10;
        l.pitch = 48;
        l.velocity = 70;
        l.hand = Hand::Left;
        l.channel = 1;
        l.track = 1;
        s.notes.push_back(l);
    }
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Test";
    out.hash = hash;
    out.library_path = "test.mid";
    return out;
}

struct Rig {
    SongController songs;
    SectionLibrary library;
    SectionsController sections{songs, library};
    std::int64_t now = 1'000 * kMs;
    std::vector<SongSoundEvent> sent;
    std::bitset<128> sounding;
    int note_ons = 0;

    explicit Rig(int bars = 8, bool left_hand = true) {
        songs.load(song_of(bars, 0x77, left_hand), now);
        step(0);
    }
    void step(std::int64_t ms) {
        now += ms * kMs;
        songs.update(now, {});
        songs.take_sound_events(sent);
        sections.update(now);
        for (const SongSoundEvent& e : sent) {
            if (e.kind == SongSoundEvent::Kind::StopAll) {
                sounding.reset();
                continue;
            }
            if (e.kind != SongSoundEvent::Kind::Message) {
                continue; // the queue's own bookkeeping: the notes here are taken as they come
            }
            const PlaybackMessage& m = e.message;
            if (m.kind == PlaybackMessage::Kind::NoteOn && m.value > 0) {
                sounding.set(m.data);
                ++note_ons;
            } else if (m.kind == PlaybackMessage::Kind::NoteOff ||
                       m.kind == PlaybackMessage::Kind::NoteOn) {
                sounding.reset(m.data);
            }
        }
        sent.clear();
    }
    void run(std::int64_t ms) {
        for (std::int64_t t = 0; t < ms; t += 10) {
            step(10);
        }
    }
    const ui::SectionsView& view() { return sections.view(); }
};

} // namespace sections_controller_test_support
