// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: the Other hand sound, unload, transposition, the status line and its parts, and the name helpers.
#include "alloc_count.hpp"
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <cstring>
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

namespace {

// Right C5 (channel 0), left C3 (1), a note with no hand (2) and a drum hit (9), all at once and
// again each second for four seconds.
LoadedSong mixed_hand_song() {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    const auto add = [&](int tick, int pitch, Hand hand, std::uint8_t channel) {
        Note n;
        n.start_tick = tick;
        n.end_tick = tick + 200;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.velocity = 80;
        n.hand = hand;
        n.channel = channel;
        n.track = channel;
        s.notes.push_back(n);
    };
    for (int beat = 0; beat < 8; beat += 2) {
        add(beat * 480, 72, Hand::Right, 0);
        add(beat * 480, 48, Hand::Left, 1);
        add(beat * 480, 60, Hand::Unknown, 2);
        add(beat * 480, 38, Hand::Unknown, 9);
    }
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Mixed";
    out.hash = 0x77;
    out.library_path = "mixed.mid";
    return out;
}

// Note-ons per channel (0, 1, 2 and 9 as slots 0..3) after playing the whole song.
std::array<int, 4> ons_of_mixed(SongSound sound, HandFilter hands) {
    SongController c;
    std::int64_t now = kSec;
    c.load(mixed_hand_song(), now);
    c.set_practice_hands(hands, now);
    c.set_song_sound(sound, now);
    std::array<int, 4> ons{};
    c.play(now);
    std::vector<SongSoundEvent> out;
    for (int i = 0; i < 250; ++i) {
        now += 16 * kMs;
        c.update(now, {});
        out.clear();
        c.take_sound_events(out);
        for (const auto& e : out) {
            const PlaybackMessage& m = e.message;
            if (e.kind == SongSoundEvent::Kind::Message &&
                m.kind == PlaybackMessage::Kind::NoteOn && m.value != 0) {
                const int slot = m.channel == 9 ? 3 : (m.channel < 3 ? m.channel : -1);
                if (slot >= 0) {
                    ++ons[static_cast<std::size_t>(slot)];
                }
            }
        }
    }
    return ons;
}

} // namespace

TEST_CASE("Other hand: drums always sound; a note with no hand sounds unless it is graded") {
    // Both hands practised: the evaluator grades the hand-less notes, so they are the player's
    // and stay muted, along with both hands. Drums are not the player's: they sound.
    const auto both = ons_of_mixed(SongSound::OtherHand, HandFilter::Both);
    CHECK(both[0] == 0);
    CHECK(both[1] == 0);
    CHECK(both[2] == 0);
    CHECK(both[3] > 0);
    // One hand practised: the evaluator does not grade hand-less notes, so they sound.
    const auto right = ons_of_mixed(SongSound::OtherHand, HandFilter::Right);
    CHECK(right[0] == 0);
    CHECK(right[1] > 0);
    CHECK(right[2] > 0);
    CHECK(right[3] > 0);
    const auto left = ons_of_mixed(SongSound::OtherHand, HandFilter::Left);
    CHECK(left[0] > 0);
    CHECK(left[1] == 0);
    CHECK(left[2] > 0);
    CHECK(left[3] > 0);
    // Both hands sound: everything. Mute: nothing.
    const auto all = ons_of_mixed(SongSound::BothHands, HandFilter::Right);
    CHECK((all[0] > 0 && all[1] > 0 && all[2] > 0 && all[3] > 0));
    CHECK(ons_of_mixed(SongSound::Mute, HandFilter::Both) == std::array<int, 4>{0, 0, 0, 0});
}

TEST_CASE("unload and silence leave no note sounding on the piano") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_song_sound(SongSound::BothHands, now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 700 * kMs);
    REQUIRE(piano.count() > 0);

    c.silence(now);
    drain(c, piano, now);
    CHECK(piano.silent());
    CHECK_FALSE(c.needs_silence());

    run(c, piano, now, now + 700 * kMs); // still playing: sounds again
    c.unload(now);
    drain(c, piano, now);
    CHECK(piano.silent());
    CHECK_FALSE(c.needs_silence());
    CHECK_FALSE(c.has_song());
    CHECK(c.status_line().empty());
    CHECK(c.grade_of(0) == -1);
    run(c, piano, now, now + 500 * kMs);
    CHECK(piano.silent());
    c.unload(now); // twice is harmless
}

TEST_CASE("transpose is clamped, shows in transposition() and the status line, and notes stay "
          "silent-safe") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    CHECK(c.transpose() == 0);
    CHECK(c.transposition().semitones() == 0);
    c.set_transpose(3, now);
    CHECK(c.transpose() == 3);
    CHECK(c.transposition().semitones() == 3);
    CHECK(c.status_line().find("+3 st") != std::string::npos);
    c.set_transpose(-5, now);
    CHECK(c.status_line().find("-5 st") != std::string::npos);
    c.set_transpose(99, now);
    CHECK(c.transpose() == kMaxTransposeSemitones);
    c.set_transpose(-99, now);
    CHECK(c.transpose() == -kMaxTransposeSemitones);
    c.set_transpose(0, now);
    CHECK(c.status_line().find(" st") == std::string::npos);
}

TEST_CASE("menu_open follows menu_opened and menu_closed; closing twice does nothing") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    CHECK_FALSE(c.menu_open());
    c.menu_opened(now);
    CHECK(c.menu_open());
    c.menu_closed(now);
    CHECK_FALSE(c.menu_open());
    c.menu_closed(now);
    CHECK_FALSE(c.menu_open());
    CHECK_FALSE(c.counting_down()); // it was paused when the menu opened
}

TEST_CASE("grade_of is -1 until a note is final, then its grade; out of range is -1") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    CHECK(c.grade_of(0) == -1);
    CHECK(c.grade_of(100000) == -1);
    c.play(now);
    FakePiano piano;
    run(c, piano, now, now + 2500 * kMs); // nobody plays: notes that passed are missed
    int missed = 0;
    for (std::uint32_t i = 0; i < c.song().song.notes.size(); ++i) {
        if (c.grade_of(i) == static_cast<int>(NoteGrade::Missed)) {
            ++missed;
        }
    }
    CHECK(missed > 0);
    CHECK(c.grade_of(100000) == -1);
}

TEST_CASE("status_line names the song, mode, hands, speed, bar, sound and state") {
    SongController c;
    CHECK(c.status_line().empty());
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Left, now);
    c.set_song_sound(SongSound::BothHands, now);
    const std::string line = c.status_line();
    CHECK(line.find("Test") == 0);
    CHECK(line.find("Flow") != std::string::npos);
    CHECK(line.find("Left hand") != std::string::npos);
    CHECK(line.find("100%") != std::string::npos);
    CHECK(line.find("Bar 1/2") != std::string::npos);
    CHECK(line.find("Sound: Both hands") != std::string::npos);
    CHECK(line.find("Paused") != std::string::npos);
    CHECK(line.find("Combo 0") != std::string::npos);
    c.set_mode(PracticeMode::Wait, now);
    CHECK(c.status_line().find("Wait") != std::string::npos);
    CHECK(c.status_line().find("Combo") == std::string::npos);
}

TEST_CASE("status parts are the status line, cut at its dots, each with a drop rank") {
    SongController c;
    std::int64_t now = kSec;
    etude::app::StatusParts none;
    c.add_status_parts(none, 1);
    CHECK(none.count == 0); // free play: no song, no line
    c.load(two_hand_song(), now);
    c.set_mode(PracticeMode::Flow, now);
    etude::app::StatusParts parts;
    c.add_status_parts(parts, 1);
    REQUIRE(parts.count >= 3);
    // Joined with the dot the parts read as the line, and the title leads it.
    std::string joined;
    for (int i = 0; i < parts.count; ++i) {
        joined += i > 0 ? " \xC2\xB7 " : "";
        joined += parts.part[static_cast<std::size_t>(i)].text;
        CHECK(parts.part[static_cast<std::size_t>(i)].group == 1);
    }
    CHECK(joined == c.status_line());
    CHECK(parts.part[0].rank == etude::app::StatusRank::Title);
    const auto rank_of = [&](std::string_view text) {
        for (int i = 0; i < parts.count; ++i) {
            if (parts.part[static_cast<std::size_t>(i)].text == text) {
                return parts.part[static_cast<std::size_t>(i)].rank;
            }
        }
        return etude::app::StatusRank::OutOfRange; // not found
    };
    CHECK(rank_of("Flow") == etude::app::StatusRank::Mode);
    CHECK(rank_of("100%") == etude::app::StatusRank::Speed);
    CHECK(rank_of("Bar 1/2") == etude::app::StatusRank::Bar);
    CHECK(rank_of("Sound: Mute") == etude::app::StatusRank::Sound);
    CHECK(rank_of("Combo 0") == etude::app::StatusRank::Combo);
    CHECK(rank_of("Paused") == etude::app::StatusRank::State);
    // A change shows in the parts as in the line.
    c.set_speed(75, now);
    etude::app::StatusParts again;
    c.add_status_parts(again, 1);
    bool found = false;
    for (int i = 0; i < again.count; ++i) {
        found = found || again.part[static_cast<std::size_t>(i)].text == "75%";
    }
    CHECK(found);
    // Unloaded, there are none again.
    c.unload(now);
    etude::app::StatusParts gone;
    c.add_status_parts(gone, 1);
    CHECK(gone.count == 0);
}

TEST_CASE("status_line is built again only when something it shows changes") {
    SongController c;
    std::int64_t now = kSec;
    LoadedSong song = two_hand_song();
    song.title = "A rather long song title that does not fit in the small string buffer";
    c.load(std::move(song), now);
    const std::string first = c.status_line();
    CHECK(first.find("A rather long song title") == 0);
    // Asking again, frame after frame, allocates nothing and returns the same line.
    etude::test::start_counting_allocations();
    for (int i = 0; i < 100; ++i) {
        CHECK(&c.status_line() == &c.status_line());
        CHECK(c.status_line().size() == first.size());
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    // A change shows at once.
    c.set_speed(75, now);
    CHECK(c.status_line().find("75%") != std::string::npos);
    c.set_practice_hands(HandFilter::Right, now);
    CHECK(c.status_line().find("Right hand") != std::string::npos);
    // A different song with the same settings shows its own title.
    LoadedSong other = two_hand_song();
    other.title = "Another title that is just as long as the first one was";
    c.load(std::move(other), now);
    CHECK(c.status_line().find("Another title") == 0);
    c.unload(now);
    CHECK(c.status_line().empty());
}

TEST_CASE("status_line adds the sheet's bar number when a repeat makes it differ") {
    SongController c;
    std::int64_t now = kSec;
    LoadedSong repeated = two_hand_song(4); // bars of 2 s
    // Played bars 3 and 4 are written bars 1 and 2 again.
    repeated.song.measures[2].source_number = 1;
    repeated.song.measures[3].source_number = 2;
    c.load(std::move(repeated), now);
    CHECK(c.status_line().find("Bar 1/4") != std::string::npos);
    CHECK(c.status_line().find("sheet bar") == std::string::npos);
    c.seek(4.5, now);
    CHECK(c.bar() == 3);
    CHECK(c.written_bar() == 1);
    CHECK(c.status_line().find("Bar 3/4 \xC2\xB7 sheet bar 1") != std::string::npos);
    c.seek(7.0, now);
    CHECK(c.written_bar() == 2);
    CHECK(c.status_line().find("Bar 4/4 \xC2\xB7 sheet bar 2") != std::string::npos);
}

namespace {
// Bars of 2 s. Bar 1 (0-2 s): both hands. Bar 2 (2-4 s): the right hand alone.
LoadedSong both_then_right_song() {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (int beat = 0; beat < 4; ++beat) {
        Note n;
        n.start_tick = beat * 480;
        n.end_tick = n.start_tick + 400;
        n.pitch = 72;
        n.velocity = 80;
        n.hand = Hand::Right;
        n.channel = 0;
        s.notes.push_back(n);
    }
    Note left;
    left.start_tick = 0;
    left.end_tick = 4 * 480 - 10;
    left.pitch = 48;
    left.velocity = 70;
    left.hand = Hand::Left;
    left.channel = 1;
    left.track = 1;
    s.notes.push_back(left);
    for (int beat = 0; beat < 4; ++beat) {
        Note n;
        n.start_tick = (4 + beat) * 480;
        n.end_tick = n.start_tick + 400;
        n.pitch = 74;
        n.velocity = 80;
        n.hand = Hand::Right;
        n.channel = 0;
        s.notes.push_back(n);
    }
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Probe";
    out.hash = 1;
    out.library_path = "p.mid";
    return out;
}
} // namespace

TEST_CASE("status_line follows the hands and the sound a loop changes") {
    SongController c;
    std::int64_t now = kSec;
    c.load(both_then_right_song(), now);
    c.set_song_sound(SongSound::OtherHand, now);
    c.seek(2.5, now);
    c.set_loop(0.0, 4.0, now); // both hands have notes in the loop
    CHECK(c.status_line().find("Both hands") != std::string::npos);
    CHECK(c.status_line().find("Sound: Other hand") != std::string::npos);
    c.set_loop(2.0, 4.0, now); // the right hand alone: nothing asked, everything changes
    CHECK(c.practice_hands() == HandFilter::Right);
    CHECK(c.status_line().find("Right hand") != std::string::npos);
    CHECK(c.status_line().find("Both hands") == std::string::npos);
    CHECK(c.status_line().find("Sound: Mute") != std::string::npos); // no other hand to play
    c.set_loop(0.0, 4.0, now);
    CHECK(c.status_line().find("Both hands") != std::string::npos);
    CHECK(c.status_line().find("Sound: Other hand") != std::string::npos);
}

TEST_CASE("the name helpers give the shown names and round-trip the saved choices") {
    CHECK(std::string(song_sound_name(SongSound::Mute)) == "Mute");
    CHECK(std::string(song_sound_name(SongSound::OtherHand)) == "Other hand");
    CHECK(std::string(song_sound_name(SongSound::BothHands)) == "Both hands");
    CHECK(std::string(hands_name(HandFilter::Left)) == "Left hand");
    CHECK(std::string(hands_name(HandFilter::Right)) == "Right hand");
    CHECK(std::string(hands_name(HandFilter::Both)) == "Both hands");
    CHECK(std::string(mode_name(PracticeMode::Flow)) == "Flow");
    CHECK(std::string(mode_name(PracticeMode::Wait)) == "Wait");
    CHECK(std::string(mode_name(PracticeMode::FreePlay)) == "Free play");
    for (const SongSound s : {SongSound::Mute, SongSound::OtherHand, SongSound::BothHands}) {
        CHECK(song_sound_from(to_piano_plays(s)) == s);
    }
    CHECK(to_piano_plays(SongSound::Mute) == PianoPlays::Nothing);
    CHECK(to_piano_plays(SongSound::BothHands) == PianoPlays::Both);
    // Older saved choices read as Other hand.
    CHECK(song_sound_from(PianoPlays::Left) == SongSound::OtherHand);
    CHECK(song_sound_from(PianoPlays::Right) == SongSound::OtherHand);
}

// Issue #25: the song's notes are handed over ahead, each with the time it reaches the hit line.
