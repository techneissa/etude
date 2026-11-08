// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the one_hand_*test.cpp files (split from one_hand_test.cpp): the helpers more than one of them uses.
#pragma once

#include "song_controller.hpp"
#include "song_transport.hpp"
#include "etude/hand_split.hpp"
#include "etude/hands_present.hpp"
#include "etude/instrument.hpp"
#include "etude/library.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"
#include "etude/song_builder.hpp"
#include "smf_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace one_hand_test_support {

using namespace etude;
using namespace etude::app;
using etude::test::Track;

constexpr std::int64_t kMs = 1'000'000;

constexpr std::int64_t kSec = 1'000'000'000;

// One track of notes in a row, one every 240 ticks (a quarter at 120 BPM and 480 per quarter),
// each held `hold` ticks: 240 leaves no overlap, more makes legato playing.
inline Track line_track(int from, int to, int channel = 0, int hold = 240, int program = -1,
                 const char* name = nullptr) {
    Track t;
    if (name != nullptr) {
        t.name(0, name);
    }
    if (program >= 0) {
        t.program(0, channel, program);
    }
    std::vector<std::array<int, 3>> events; // tick, on (1) or off (0), pitch
    int i = 0;
    for (int p = from; p <= to; ++p, ++i) {
        events.push_back({i * 240, 1, p});
        events.push_back({i * 240 + hold, 0, p});
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const auto& a, const auto& b) { return a[0] < b[0]; });
    int at = 0;
    for (const auto& e : events) {
        const auto delta = static_cast<std::uint32_t>(e[0] - at);
        at = e[0];
        e[1] == 1 ? t.on(delta, channel, e[2]) : t.off(delta, channel, e[2]);
    }
    return t.end();
}

inline Song load_split(const etude::test::Bytes& bytes) {
    MidiLoadResult r = load_midi(bytes);
    REQUIRE(r.ok);
    return with_split_hands(r.song);
}

inline int count_hand(const Song& s, Hand h) {
    return static_cast<int>(std::count_if(s.notes.begin(), s.notes.end(), [&](const Note& n) {
        return !n.drum && !n.accompaniment && n.hand == h;
    }));
}

struct NoteSpec {
    double beat;
    int pitch;
    double beats;
    Hand hand;
    int channel;
    bool accompaniment = false;
};

inline LoadedSong make_loaded(const std::vector<NoteSpec>& specs) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (const NoteSpec& n : specs) {
        Note note;
        note.start_tick = static_cast<std::int64_t>(n.beat * 480);
        note.end_tick = note.start_tick + static_cast<std::int64_t>(n.beats * 480) - 20;
        note.pitch = static_cast<std::uint8_t>(n.pitch);
        note.velocity = 80;
        note.hand = n.hand;
        note.channel = static_cast<std::uint8_t>(n.channel);
        note.track = static_cast<std::uint16_t>(n.channel);
        note.accompaniment = n.accompaniment;
        s.notes.push_back(note);
    }
    REQUIRE(finish_song(s));
    for (Part& part : s.parts) {
        part.accompaniment = std::any_of(specs.begin(), specs.end(), [&](const NoteSpec& n) {
            return n.accompaniment && n.channel == part.channel;
        });
    }
    LoadedSong out;
    out.song = std::move(s);
    out.title = "One hand";
    out.hash = 0x80;
    return out;
}

// Eight quarter notes, the right hand only.
inline std::vector<NoteSpec> right_only_notes() {
    std::vector<NoteSpec> v;
    for (int i = 0; i < 8; ++i) {
        v.push_back({static_cast<double>(i), 60 + i * 2, 1.0, Hand::Right, 0});
    }
    return v;
}

inline MidiEvent key_on(int key, std::int64_t at) {
    MidiEvent ev;
    ev.time_ns = at;
    ev.type = MidiEventType::NoteOn;
    ev.data1 = static_cast<std::uint8_t>(key);
    ev.data2 = 90;
    ev.source = InputSource::ComputerKeyboard;
    return ev;
}

} // namespace one_hand_test_support
