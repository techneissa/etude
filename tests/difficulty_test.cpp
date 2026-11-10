// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/difficulty.hpp"

#include "smf_builder.hpp"

#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;
using etude::test::Bytes;

namespace {

Bytes read_data(const char* name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Song load(const char* name) {
    const Bytes bytes = read_data(name);
    Song song;
    if (bytes.size() >= 4 && bytes[0] == 'M' && bytes[1] == 'T') {
        MidiLoadResult r = load_midi(bytes);
        REQUIRE(r.ok);
        song = std::move(r.song);
    } else {
        ScoreLoadResult r = load_score(bytes);
        REQUIRE(r.ok);
        song = std::move(r.song);
    }
    return needs_hand_split(song) ? with_split_hands(song) : song;
}

SongDifficulty rated(const char* name) { return rate_song(load(name)); }

// A song of `bars` bars of 4/4 at `bpm`, notes added by bar and beat (quarter notes into the bar).
class Piece {
  public:
    explicit Piece(int bars, double bpm = 120.0) : bpm_(bpm) {
        song_.tempo.ticks_per_quarter = 480;
        song_.tempo.changes[0].us_per_quarter = static_cast<std::uint32_t>(60'000'000.0 / bpm);
        song_.time_signatures.push_back({0, 4, 4});
        for (int b = 0; b < bars; ++b) {
            Measure m;
            m.number = b + 1;
            m.start_tick = static_cast<std::int64_t>(b) * 4 * 480;
            m.start_s = b * 4 * 60.0 / bpm;
            song_.measures.push_back(m);
        }
        song_.duration_s = bars * 4 * 60.0 / bpm;
        song_.end_tick = static_cast<std::int64_t>(bars) * 4 * 480;
    }
    Piece& note(int bar, double beat, int pitch, Hand hand = Hand::Right, double length = 0.25) {
        Note n;
        const double quarters = bar * 4.0 + beat;
        n.start_tick = static_cast<std::int64_t>(quarters * 480);
        n.end_tick = n.start_tick + static_cast<std::int64_t>(length * 480);
        n.start_s = quarters * 60.0 / bpm_;
        n.duration_s = length * 60.0 / bpm_;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.hand = hand;
        n.measure = bar + 1;
        song_.notes.push_back(n);
        return *this;
    }
    // `count` equal notes across the bar, the pitches taken from `pitches` in turn.
    Piece& run(int bar, int count, std::initializer_list<int> pitches, Hand hand = Hand::Right) {
        const std::vector<int> p(pitches);
        for (int i = 0; i < count; ++i) {
            note(bar, 4.0 * i / count, p[static_cast<std::size_t>(i) % p.size()], hand);
        }
        return *this;
    }
    Song& song() {
        std::stable_sort(song_.notes.begin(), song_.notes.end(), [](const Note& a, const Note& b) {
            return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
        });
        return song_;
    }
    SongDifficulty rate() { return rate_song(song()); }

  private:
    Song song_;
    double bpm_;
};

} // namespace

// ---- Calibration: real and made-up pieces -------------------------------------------------------

TEST_CASE("difficulty calibration: score files are rated like MIDI ones") {
    // A voice and piano song: a middling piece, one bar per played bar.
    const SongDifficulty lied = rated("beethoven-op48-4-ehre-gottes.mxl");
    CHECK(lied.bars.size() == 43);
    CHECK(lied.half_stars >= 3);
    CHECK(lied.half_stars <= 7);
    // The seven-bar exercise with repeats rates a bar for every played bar and is easy.
    const SongDifficulty repeats = rated("repeats-voltas.musicxml");
    CHECK(repeats.bars.size() == 12);
    CHECK(repeats.half_stars <= 4);
}

TEST_CASE("difficulty: a bar takes the tempo in force at its start, whatever the changes before") {
    // 4/4 at 60 BPM for two bars, then 180 BPM; the same sixteenths in a bar of each.
    Piece p(4, 60.0);
    for (int bar = 0; bar < 4; ++bar) {
        p.run(bar, 16, {60, 62, 64, 65});
    }
    Song& s = p.song();
    s.tempo.changes.clear();
    s.tempo.changes.push_back({0, 0.0, 1'000'000});          // 60 BPM
    s.tempo.changes.push_back({2 * 4 * 480, 8.0, 500'000});  // 120 BPM from bar 3
    s.tempo.changes.push_back({3 * 4 * 480, 10.0, 333'333}); // 180 BPM from bar 4
    const SongDifficulty d = rate_song(s);
    CHECK(d.features[0].bpm == doctest::Approx(60.0));
    CHECK(d.features[1].bpm == doctest::Approx(60.0));
    CHECK(d.features[2].bpm == doctest::Approx(120.0));
    CHECK(d.features[3].bpm == doctest::Approx(180.0).epsilon(0.001)); // exactly at a change
    // No changes at all reads as 120 BPM.
    s.tempo.changes.clear();
    CHECK(rate_song(s).features[0].bpm == doctest::Approx(120.0));
}
