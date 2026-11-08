// SPDX-License-Identifier: GPL-3.0-or-later
// one_hand: controller (split from one_hand_test.cpp).
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

#include "one_hand_test_support.hpp"

using namespace etude;
using namespace etude::app;
using etude::test::Track;
using namespace one_hand_test_support;

namespace {

MidiEvent key_off(int key, std::int64_t at) {
    MidiEvent ev = key_on(key, at);
    ev.type = MidiEventType::NoteOff;
    ev.data2 = 0;
    return ev;
}

// Plays from `now` for `frames` frames of 16 ms and counts the note-ons of the song's sound per
// channel (channels 0..3).
std::array<int, 4> sound_ons(SongController& c, std::int64_t& now, int frames) {
    std::array<int, 4> ons{};
    std::vector<SongSoundEvent> out;
    for (int i = 0; i < frames; ++i) {
        now += 16 * kMs;
        c.update(now, {});
        out.clear();
        c.take_sound_events(out);
        for (const SongSoundEvent& e : out) {
            if (e.kind == SongSoundEvent::Kind::Message &&
                e.message.kind == PlaybackMessage::Kind::NoteOn && e.message.value > 0 &&
                e.message.channel < 4) {
                ++ons[e.message.channel];
            }
        }
    }
    return ons;
}

} // namespace

// ---- The controller with a right-only song

TEST_CASE("a right-only song: the view locks the left hand and the present hand stays on") {
    SongController c;
    SongTransport port(c);
    std::int64_t now = kSec;
    c.load(make_loaded(right_only_notes()), now);
    port.update(now);
    CHECK_FALSE(port.view().left_available);
    CHECK(port.view().right_available);
    CHECK_FALSE(port.view().hands_in_section);
    CHECK_FALSE(port.view().left_hand);
    CHECK(port.view().right_hand);
    // Asking for the left hand, for both, or for neither changes nothing.
    port.set_hands(true, false, now);
    CHECK(c.practice_hands() == HandFilter::Right);
    CHECK_FALSE(port.view().left_hand);
    port.set_hands(true, true, now);
    CHECK(c.practice_hands() == HandFilter::Right);
    port.set_hands(false, false, now);
    CHECK(c.practice_hands() == HandFilter::Right);
    CHECK(port.view().right_hand);
    // The status line names the hand in use.
    CHECK(c.status_line().find("Right") != std::string::npos);
}

TEST_CASE("a right-only song: Wait Mode waits for the right hand even if the left was asked") {
    SongController c;
    std::int64_t now = kSec;
    c.load(make_loaded(right_only_notes()), now);
    c.set_practice_hands(HandFilter::Left, now);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    for (int i = 0; i < 40; ++i) {
        now += 16 * kMs;
        c.update(now, {});
    }
    // An empty left hand would let the song play straight through; here it holds at the first
    // note (C4) and waits for it.
    REQUIRE(c.waiting());
    CHECK(c.position_s() == doctest::Approx(0.0).epsilon(0.001));
    REQUIRE(c.waiting_keys().size() == 1);
    CHECK(c.waiting_keys()[0].key == 60);
    CHECK(c.waiting_keys()[0].hand == Hand::Right);
    // Playing it moves on to the next one, D4 two semitones up, half a second in.
    const std::array<MidiEvent, 1> press{key_on(60, now)};
    c.update(now + 16 * kMs, press);
    now += 16 * kMs;
    for (int i = 0; i < 100; ++i) {
        now += 16 * kMs;
        c.update(now, {});
    }
    REQUIRE(c.waiting());
    CHECK(c.position_s() == doctest::Approx(0.5).epsilon(0.001));
    REQUIRE(c.waiting_keys().size() == 1);
    CHECK(c.waiting_keys()[0].key == 62);
}

TEST_CASE("a right-only song: the sound has mute and play-along, no other hand") {
    SongController c;
    SongTransport port(c);
    std::int64_t now = kSec;
    c.load(make_loaded(right_only_notes()), now);
    CHECK(c.song_sound() == SongSound::Mute);
    c.cycle_song_sound(now);
    CHECK(c.song_sound() == SongSound::BothHands); // not the other hand
    c.cycle_song_sound(now);
    CHECK(c.song_sound() == SongSound::Mute);
    // Asking for the other hand directly (a key, a saved session) is mute, as it has no other.
    c.set_song_sound(SongSound::OtherHand, now);
    CHECK(c.song_sound() == SongSound::Mute);
    port.update(now);
    CHECK(port.view().sound == ui::SongSound::Mute);
    c.play(now);
    CHECK(sound_ons(c, now, 100) == std::array<int, 4>{0, 0, 0, 0});
    c.set_song_sound(SongSound::BothHands, now);
    CHECK(sound_ons(c, now, 100)[0] > 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a right-only song: playing the melody grades it with the left hand asked") {
    SongController c;
    std::int64_t now = kSec;
    c.load(make_loaded(right_only_notes()), now);
    c.set_practice_hands(HandFilter::Left, now);
    c.play(now);
    const std::int64_t start = now;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < 8; ++i) {
        const std::int64_t t = start + i * 500 * kMs;
        script.push_back({t, key_on(60 + i * 2, t)});
        script.push_back({t + 200 * kMs, key_off(60 + i * 2, t + 200 * kMs)});
    }
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    std::vector<SongSoundEvent> out;
    while (c.state() != TransportState::Ended && now < start + 10 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        out.clear();
        c.take_sound_events(out);
    }
    REQUIRE(c.state() == TransportState::Ended);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    const RunRecord& r = runs[0].record;
    CHECK(r.hands == HandFilter::Right);
    CHECK(r.all.graded == 8);
    CHECK(r.all.hit == 8);
    CHECK(r.right.graded == 8);
    CHECK(r.wrong == 0);
    CHECK_FALSE(c.take_ungraded_run());
    const std::string line = results_line(r, compare_with_last_attempt({}, r));
    CHECK(line.find("right 100%") != std::string::npos);
}

TEST_CASE("a backing part is not graded or waited for, and sounds whatever the song sound says "
          "until it is muted") {
    std::vector<NoteSpec> notes = right_only_notes();
    for (int i = 0; i < 8; ++i) {
        notes.push_back({static_cast<double>(i), 40 + i, 1.0, Hand::Unknown, 2, true});
    }
    SongController c;
    std::int64_t now = kSec;
    c.load(make_loaded(notes), now);
    REQUIRE(c.accompaniment_parts().size() == 1);
    c.set_mode(PracticeMode::Wait, now);
    c.play(now);
    for (int i = 0; i < 40; ++i) {
        now += 16 * kMs;
        c.update(now, {});
    }
    REQUIRE(c.waiting());
    CHECK(c.waiting_keys().size() == 1); // the melody's C4 only, not the backing part's E2
    CHECK(c.waiting_keys()[0].key == 60);
    // Sound muted: the piano's hands are silent, the backing part sounds (it is its own choice).
    c.set_mode(PracticeMode::Flow, now);
    c.seek(0.0, now);
    c.play(now);
    const auto muted_song = sound_ons(c, now, 60);
    CHECK(muted_song[0] == 0);
    CHECK(muted_song[2] > 0);
    // Muting the part silences it; the song sound still does not matter.
    c.set_accompaniment_muted(all_keys(c.accompaniment_parts()), now);
    CHECK(c.accompaniment_state() == AccompanimentState::Off);
    c.seek(0.0, now);
    c.play(now);
    CHECK(sound_ons(c, now, 60)[2] == 0);
    c.set_song_sound(SongSound::BothHands, now);
    c.seek(0.0, now);
    c.play(now);
    const auto along = sound_ons(c, now, 60);
    CHECK(along[0] > 0);
    CHECK(along[2] == 0);
    c.set_accompaniment_muted({}, now);
    c.seek(0.0, now);
    c.play(now);
    CHECK(sound_ons(c, now, 60)[2] > 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

namespace {

// The velocities of the song's note-ons on `channel` over `frames` frames of 16 ms.
std::vector<int> velocities(SongController& c, std::int64_t& now, int frames, int channel) {
    std::vector<int> out;
    std::vector<SongSoundEvent> events;
    for (int i = 0; i < frames; ++i) {
        now += 16 * kMs;
        c.update(now, {});
        events.clear();
        c.take_sound_events(events);
        for (const SongSoundEvent& e : events) {
            if (e.kind == SongSoundEvent::Kind::Message &&
                e.message.kind == PlaybackMessage::Kind::NoteOn && e.message.value > 0 &&
                e.message.channel == channel) {
                out.push_back(e.message.value);
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("the accompaniment volume option scales what the backing part sounds with") {
    std::vector<NoteSpec> notes = right_only_notes();
    for (int i = 0; i < 8; ++i) {
        notes.push_back({static_cast<double>(i), 40 + i, 1.0, Hand::Unknown, 2, true});
    }
    SongController c;
    std::int64_t now = kSec;
    c.load(make_loaded(notes), now);
    c.play(now);
    const std::vector<int> full = velocities(c, now, 40, 2);
    REQUIRE_FALSE(full.empty());
    CHECK(full.front() == 80);
    SongOptions o = c.options();
    o.accompaniment_volume = 25;
    c.set_options(o, now);
    c.seek(0.0, now);
    c.play(now);
    const std::vector<int> quiet = velocities(c, now, 40, 2);
    REQUIRE_FALSE(quiet.empty());
    CHECK(quiet.front() == 20);
    // The piano's own parts are not touched by it.
    c.seek(0.0, now);
    c.set_song_sound(SongSound::BothHands, now);
    c.play(now);
    const std::vector<int> melody = velocities(c, now, 40, 0);
    REQUIRE_FALSE(melody.empty());
    CHECK(melody.front() == 80);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

namespace {

// One note in each of `parts` accompaniment parts (their own tracks on channel 2), over a melody.
LoadedSong with_many_parts(int parts) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (int i = 0; i < 8; ++i) {
        Note n;
        n.start_tick = i * 480;
        n.end_tick = n.start_tick + 400;
        n.pitch = static_cast<std::uint8_t>(60 + i);
        n.velocity = 80;
        n.hand = Hand::Right;
        s.notes.push_back(n);
    }
    for (int i = 0; i < parts; ++i) {
        Note n;
        n.start_tick = 0;
        n.end_tick = 1440;
        n.pitch = static_cast<std::uint8_t>(30 + i);
        n.velocity = 70;
        n.hand = Hand::Unknown;
        n.channel = 2;
        n.track = static_cast<std::uint16_t>(2 + i);
        n.accompaniment = true;
        s.notes.push_back(n);
    }
    REQUIRE(finish_song(s));
    for (Part& part : s.parts) {
        part.accompaniment = part.track >= 2;
    }
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Many";
    out.hash = 0x81;
    return out;
}

} // namespace

TEST_CASE("parts past the list's cap follow the button: Off silences every one, On lets them all "
          "sound") {
    constexpr int kParts = static_cast<int>(kMaxAccompanimentParts) + 1; // 33: one has no row
    SongController c;
    std::int64_t now = kSec;
    c.load(with_many_parts(kParts), now);
    REQUIRE(c.accompaniment_parts().size() == kMaxAccompanimentParts);
    const auto note_ons_ch2 = [&](int frames) {
        std::int64_t t = now;
        const std::array<int, 4> ons = sound_ons(c, t, frames);
        now = t;
        return ons[2];
    };
    c.play(now);
    CHECK(note_ons_ch2(30) == kParts); // every part's note, listed or not
    // The button's tap: all listed parts sound, so it mutes them, and the unlisted one with them.
    c.set_accompaniment_muted(after_button_tap(c.accompaniment_parts(), c.accompaniment_muted()),
                              now);
    CHECK(c.accompaniment_state() == AccompanimentState::Off);
    c.seek(0.0, now);
    c.play(now);
    CHECK(note_ons_ch2(30) == 0);
    c.set_accompaniment_muted({}, now);
    CHECK(c.accompaniment_state() == AccompanimentState::On);
    c.seek(0.0, now);
    c.play(now);
    CHECK(note_ons_ch2(30) == kParts);
    // A new song starts sounding even when the last one was left Off.
    c.set_accompaniment_muted(all_keys(c.accompaniment_parts()), now);
    c.load(with_many_parts(2), now);
    c.play(now);
    CHECK(note_ons_ch2(30) == 2);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("muting a part in the middle of its note cancels what was handed over, then stops it") {
    std::vector<NoteSpec> notes = right_only_notes();
    notes.push_back({0, 40, 4.0, Hand::Unknown, 2, true}); // sounds from the start for 2 s
    SongController c;
    std::int64_t now = kSec;
    c.load(make_loaded(notes), now);
    c.play(now);
    std::vector<SongSoundEvent> events;
    for (int i = 0; i < 20; ++i) {
        now += 16 * kMs;
        c.update(now, {});
    }
    c.take_sound_events(events); // what the first frames handed over
    const bool started = std::any_of(events.begin(), events.end(), [](const SongSoundEvent& e) {
        return e.kind == SongSoundEvent::Kind::Message &&
               e.message.kind == PlaybackMessage::Kind::NoteOn && e.message.channel == 2 &&
               e.message.data == 40;
    });
    REQUIRE(started);
    events.clear();
    c.set_accompaniment_muted(all_keys(c.accompaniment_parts()), now);
    c.take_sound_events(events);
    REQUIRE_FALSE(events.empty());
    // First what was handed over and not started is cancelled, then the sounding note is stopped.
    CHECK(events.front().kind == SongSoundEvent::Kind::CancelUnstarted);
    std::size_t off = events.size();
    for (std::size_t i = 0; i < events.size(); ++i) {
        const SongSoundEvent& e = events[i];
        CHECK_FALSE((e.kind == SongSoundEvent::Kind::Message &&
                     e.message.kind == PlaybackMessage::Kind::NoteOn && e.message.channel == 2));
        if (e.kind == SongSoundEvent::Kind::Message &&
            e.message.kind == PlaybackMessage::Kind::NoteOff && e.message.channel == 2 &&
            e.message.data == 40) {
            off = i;
        }
    }
    REQUIRE(off < events.size());
    CHECK(off > 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("the song port shows the accompaniment: its parts, its state and what a tap does") {
    std::vector<NoteSpec> notes = right_only_notes();
    notes.push_back({0, 40, 4.0, Hand::Unknown, 2, true});
    notes.push_back({0, 43, 4.0, Hand::Unknown, 3, true});
    SongController c;
    SongTransport port(c);
    std::int64_t now = kSec;
    port.update(now);
    CHECK_FALSE(port.view().has_accompaniment);
    c.load(make_loaded(notes), now);
    port.update(now);
    REQUIRE(port.view().has_accompaniment);
    CHECK(port.view().accompaniment == ui::AccompState::On);
    REQUIRE(port.view().accompaniment_parts.size() == 2);
    CHECK_FALSE(port.view().accompaniment_parts[0].muted);
    CHECK_FALSE(port.view().accompaniment_voice);
    port.toggle_accompaniment_part(1, now);
    CHECK(port.view().accompaniment == ui::AccompState::Partial);
    CHECK(port.view().accompaniment_parts[1].muted);
    CHECK_FALSE(port.view().accompaniment_parts[0].muted);
    port.tap_accompaniment(now); // some sound: they all sound
    CHECK(port.view().accompaniment == ui::AccompState::On);
    port.tap_accompaniment(now); // all sound: none does
    CHECK(port.view().accompaniment == ui::AccompState::Off);
    port.toggle_accompaniment_part(7, now); // no such part: nothing
    CHECK(port.view().accompaniment == ui::AccompState::Off);
    // The song sound is untouched by any of it.
    CHECK(port.view().sound == ui::SongSound::Mute);
    c.unload(now);
    port.update(now);
    CHECK_FALSE(port.view().has_accompaniment);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a section where one hand rests offers only the playing hand") {
    // The right hand plays beats 0..7; the left hand only the first two seconds (beats 0..3).
    std::vector<NoteSpec> notes = right_only_notes();
    notes.push_back({0, 48, 2, Hand::Left, 1});
    notes.push_back({2, 50, 2, Hand::Left, 1});
    SongController c;
    SongTransport port(c);
    std::int64_t now = kSec;
    c.load(make_loaded(notes), now);
    port.update(now);
    CHECK(port.view().left_available);
    CHECK(port.view().right_available);
    c.set_practice_hands(HandFilter::Both, now);
    REQUIRE(c.set_loop(2.0, 4.0, now)); // the second half: no left-hand notes starting there
    port.update(now);
    CHECK_FALSE(port.view().left_available);
    CHECK(port.view().right_available);
    CHECK(port.view().hands_in_section);
    CHECK(c.practice_hands() == HandFilter::Right);
    CHECK_FALSE(port.view().left_hand);
    // The section with both hands brings the asked-for hands back.
    REQUIRE(c.set_loop(0.0, 2.0, now));
    port.update(now);
    CHECK(port.view().left_available);
    CHECK(c.practice_hands() == HandFilter::Both);
    c.clear_loop(now);
    port.update(now);
    CHECK(port.view().left_available);
    CHECK_FALSE(port.view().hands_in_section);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}
