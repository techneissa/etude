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

float stars_of_bar(Piece p) { return p.rate().bars.at(0); }

} // namespace

// ---- The pure measure ---------------------------------------------------------------------------

TEST_CASE("difficulty: the weights sum to one and the scale runs 1 to 5") {
    using namespace difficulty_tok;
    CHECK(kSpeed + kChords + kSpan + kReach + kLeaps + kIndependence + kColour + kTempo ==
          doctest::Approx(1.0));
    CHECK(stars_from_sum(0.0) == doctest::Approx(1.0f));
    CHECK(stars_from_sum(kFullSum) == doctest::Approx(5.0f));
    CHECK(stars_from_sum(10.0) == doctest::Approx(5.0f));
    CHECK(stars_from_sum(-1.0) == doctest::Approx(1.0f));
}

TEST_CASE("difficulty: a bar's stars never fall as any feature rises") {
    BarFeatures base;
    base.notes = 8;
    base.bpm = 120.0;
    base.mean_chord_notes = 1.0;
    base.max_chord_notes = 1;
    const auto check = [&](auto set, double from, double to, double step) {
        BarFeatures f = base;
        set(f, from);
        float last = bar_stars(f);
        for (double v = from; v <= to; v += step) {
            set(f, v);
            const float now = bar_stars(f);
            CHECK(now >= last);
            CHECK(now >= kMinStars);
            CHECK(now <= kMaxStars);
            last = now;
        }
        set(f, to);
        CHECK(bar_stars(f) > bar_stars(base)); // the feature does count
    };
    check([](BarFeatures& f, double v) { f.onsets_per_second = v; }, 0, 30, 0.25);
    check([](BarFeatures& f, double v) { f.mean_chord_notes = v; }, 1, 6, 0.25);
    check([](BarFeatures& f, double v) { f.max_chord_notes = static_cast<int>(v); }, 1, 8, 1);
    check([](BarFeatures& f, double v) { f.max_span = v; }, 0, 30, 0.5);
    check([](BarFeatures& f, double v) { f.max_reach = v; }, 0, 40, 0.5);
    check([](BarFeatures& f, double v) { f.max_leap = v; }, 0, 40, 0.5);
    check([](BarFeatures& f, double v) { f.big_leaps = static_cast<int>(v); }, 0, 8, 1);
    check([](BarFeatures& f, double v) { f.independence = v; }, 0, 1, 0.05);
    check([](BarFeatures& f, double v) { f.black_share = v; }, 0, 1, 0.05);
    check([](BarFeatures& f, double v) { f.accidental_share = v; }, 0, 1, 0.05);
    // The tempo counts only where the beat is divided: sixteenths at 160 against at 60.
    BarFeatures busy = base;
    busy.onsets_per_second = 8.0;
    busy.bpm = 60.0;
    BarFeatures quick = busy;
    quick.bpm = 160.0;
    quick.onsets_per_second = 8.0 * 160.0 / 60.0; // the same four onsets a beat
    CHECK(bar_stars(quick) > bar_stars(busy));
    BarFeatures slow_beats = base;
    slow_beats.onsets_per_second = 0.0;
    slow_beats.bpm = 60.0;
    BarFeatures fast_beats = slow_beats;
    fast_beats.bpm = 200.0;
    fast_beats.onsets_per_second = fast_beats.bpm / 60.0; // one onset a beat, no division
    CHECK(bar_parts(fast_beats).tempo == doctest::Approx(0.0));
}

TEST_CASE("difficulty: a bar with no notes is one star") {
    BarFeatures none;
    none.bpm = 200.0;
    none.onsets_per_second = 50.0; // nothing to play, whatever the numbers say
    CHECK(bar_stars(none) == doctest::Approx(1.0f));
    Piece p(3);
    p.run(1, 8, {60, 62, 64});
    const SongDifficulty d = p.rate();
    REQUIRE(d.bars.size() == 3);
    CHECK(d.bars[0] == doctest::Approx(1.0f));
    CHECK(d.bars[1] > 1.0f);
    CHECK(d.bars[2] == doctest::Approx(1.0f));
}

// ---- Reading a song -----------------------------------------------------------------------------

TEST_CASE("difficulty: speed, chord size and span raise a bar") {
    // Bars of 4/4 at 120 BPM last 2 s.
    const float slow = stars_of_bar(Piece(1).run(0, 4, {60, 62, 64, 65}));
    const float quick = stars_of_bar(Piece(1).run(0, 16, {60, 62, 64, 65}));
    CHECK(quick > slow);
    {
        const SongDifficulty d = Piece(1).run(0, 4, {60, 62, 64, 65}).rate();
        CHECK(d.features[0].onsets_per_second == doctest::Approx(2.0));
        CHECK(d.features[0].notes == 4);
    }
    // The same onsets as triads, then as wide chords.
    Piece triads(1);
    Piece wide(1);
    for (int i = 0; i < 4; ++i) {
        for (int iv : {0, 4, 7}) {
            triads.note(0, i, 60 + iv);
        }
        for (int iv : {0, 7, 12, 16}) {
            wide.note(0, i, 55 + iv);
        }
    }
    const SongDifficulty t = triads.rate();
    const SongDifficulty w = wide.rate();
    CHECK(t.features[0].max_chord_notes == 3);
    CHECK(t.features[0].max_span == doctest::Approx(7.0));
    CHECK(w.features[0].max_chord_notes == 4);
    CHECK(w.features[0].max_span == doctest::Approx(16.0));
    CHECK(t.bars[0] > slow);
    CHECK(w.bars[0] > t.bars[0]);
}

TEST_CASE("difficulty: leaps count between onsets of one hand, and carry over the bar line") {
    const SongDifficulty steps = Piece(1).run(0, 8, {60, 62, 64, 65, 67, 65, 64, 62}).rate();
    const SongDifficulty jumps = Piece(1).run(0, 8, {48, 72, 48, 72, 48, 72, 48, 72}).rate();
    CHECK(steps.features[0].max_leap <= 2.0);
    CHECK(jumps.features[0].max_leap == doctest::Approx(24.0));
    CHECK(jumps.features[0].big_leaps == 7);
    CHECK(jumps.bars[0] > steps.bars[0]);
    // A leap is counted in the bar its landing note is in.
    Piece p(2);
    p.note(0, 3, 40).note(1, 0, 70);
    const SongDifficulty d = p.rate();
    CHECK(d.features[0].max_leap == doctest::Approx(0.0));
    CHECK(d.features[1].max_leap == doctest::Approx(30.0));
    // A leap after a long rest is not one: the hand has time to move.
    Piece rest(2);
    rest.note(0, 0, 40).note(1, 3, 70);
    CHECK(rest.rate().features[1].max_leap == doctest::Approx(0.0));
}

TEST_CASE("difficulty: reach is a hand's five next onsets spread wider than a position") {
    const SongDifficulty close = Piece(1).run(0, 8, {60, 62, 64, 65, 67, 65, 64, 62}).rate();
    const SongDifficulty over = Piece(1).run(0, 8, {60, 64, 67, 72, 76, 79, 84, 79}).rate();
    CHECK(close.features[0].max_reach <= 7.0);
    CHECK(over.features[0].max_reach >= 16.0);
    CHECK(over.bars[0] > close.bars[0]);
}

TEST_CASE("difficulty: hand independence is the sparser hand playing against the other") {
    // Together: both hands on the same four beats.
    Piece together(1);
    // Against: the left hand plays the off-beats of the right hand's quarter notes.
    Piece against(1);
    // Holding: the left hand plays one long note under a running right hand.
    Piece holding(1);
    for (int i = 0; i < 4; ++i) {
        together.note(0, i, 72, Hand::Right).note(0, i, 48, Hand::Left);
        against.note(0, i, 72, Hand::Right).note(0, i + 0.5, 48, Hand::Left);
    }
    holding.note(0, 0, 48, Hand::Left, 4.0);
    for (int i = 0; i < 8; ++i) {
        holding.note(0, i * 0.5, 72 + i % 3, Hand::Right);
    }
    const SongDifficulty a = together.rate();
    const SongDifficulty b = against.rate();
    const SongDifficulty c = holding.rate();
    CHECK(a.features[0].independence == doctest::Approx(0.0));
    CHECK(b.features[0].independence == doctest::Approx(1.0));
    // One left note against eight: the sparser hand matches the first, and counts for little.
    CHECK(c.features[0].independence == doctest::Approx(0.0));
    CHECK(b.bars[0] > a.bars[0]);
    // One hand alone is never independent of the other.
    Piece alone(1);
    alone.run(0, 8, {60, 62}, Hand::Right);
    CHECK(alone.rate().features[0].independence == doctest::Approx(0.0));
}

TEST_CASE("difficulty: black keys and notes outside the key count") {
    const SongDifficulty white = Piece(1).run(0, 8, {60, 62, 64, 65}).rate();
    const SongDifficulty black = Piece(1).run(0, 8, {61, 63, 66, 68}).rate();
    CHECK(white.features[0].black_share == doctest::Approx(0.0));
    CHECK(black.features[0].black_share == doctest::Approx(1.0));
    CHECK(black.bars[0] > white.bars[0]);

    // D major (two sharps): F sharp and C sharp are its own, F and C are not.
    const auto in_d_major = [](std::initializer_list<int> pitches) {
        Piece p(1);
        p.run(0, 8, pitches);
        Song& s = p.song();
        KeyChange k;
        k.tonic = 2;
        k.fifths = 2;
        s.keys.push_back(k);
        return rate_song(s);
    };
    const SongDifficulty own = in_d_major({62, 64, 66, 67, 69, 71, 73, 74}); // D E F# G A B C# D
    const SongDifficulty foreign = in_d_major({62, 64, 65, 67, 69, 71, 72, 74}); // F and C natural
    CHECK(own.features[0].accidental_share == doctest::Approx(0.0));
    CHECK(foreign.features[0].accidental_share == doctest::Approx(0.25));
    CHECK(foreign.bars[0] > own.bars[0]);
    // The same notes with no key at all are read in C major: F sharp is outside it.
    const SongDifficulty none = Piece(1).run(0, 8, {62, 64, 66, 67, 69, 71, 73, 74}).rate();
    CHECK(none.features[0].accidental_share == doctest::Approx(0.25));
}

TEST_CASE("difficulty: the same notes at a faster tempo rate higher") {
    const auto at = [](double bpm) {
        Piece p(1, bpm);
        p.run(0, 16, {60, 62, 64, 65});
        return p.rate();
    };
    const SongDifficulty slow = at(60.0);
    const SongDifficulty fast = at(160.0);
    CHECK(slow.features[0].bpm == doctest::Approx(60.0));
    CHECK(fast.features[0].bpm == doctest::Approx(160.0));
    CHECK(fast.bars[0] > slow.bars[0]);
}

TEST_CASE("difficulty: drums and accompaniment parts are not rated") {
    Piece p(1);
    p.run(0, 4, {60, 62, 64, 65});
    Song& s = p.song();
    const SongDifficulty before = rate_song(s);
    for (int i = 0; i < 64; ++i) {
        Note drum = s.notes[0];
        drum.drum = true;
        drum.start_s += i * 0.01;
        s.notes.push_back(drum);
        Note along = s.notes[0];
        along.accompaniment = true;
        along.pitch = static_cast<std::uint8_t>(30 + i);
        s.notes.push_back(along);
    }
    std::stable_sort(s.notes.begin(), s.notes.end(),
                     [](const Note& a, const Note& b) { return a.start_tick < b.start_tick; });
    const SongDifficulty after = rate_song(s);
    CHECK(after.bars == before.bars);
}

TEST_CASE("difficulty: a song with no bars is rated as one") {
    Song s;
    s.duration_s = 4.0;
    for (int i = 0; i < 8; ++i) {
        Note n;
        n.start_s = i * 0.5;
        n.start_tick = i * 240;
        n.end_tick = n.start_tick + 200;
        n.pitch = static_cast<std::uint8_t>(60 + i);
        n.hand = Hand::Right;
        s.notes.push_back(n);
    }
    const SongDifficulty d = rate_song(s);
    REQUIRE(d.bars.size() == 1);
    CHECK(d.bars[0] > 1.0f);
    CHECK(rate_song(Song{}).half_stars == kMinHalfStars);
    CHECK(rate_song(Song{}).stars == doctest::Approx(1.0f));
}

TEST_CASE("difficulty: a bar is found by its number, a pickup being 0") {
    Piece p(4);
    p.run(1, 16, {60, 62, 64, 65});
    Song& s = p.song();
    for (Measure& m : s.measures) {
        m.number -= 1; // 0, 1, 2, 3
    }
    const SongDifficulty d = rate_song(s);
    CHECK(d.numbers == std::vector<int>{0, 1, 2, 3});
    CHECK(d.bar_at(0) == doctest::Approx(1.0f));
    CHECK(d.bar_at(1) == d.bars[1]);
    CHECK(d.bar_at(1) > 1.0f);
    CHECK(d.bar_at(4) == 0.0f); // no such bar
    CHECK(d.bar_at(-1) == 0.0f);
}

// ---- A song from its hardest stretches ----------------------------------------------------------

TEST_CASE("difficulty: a song rates by its hardest stretches, not its average") {
    std::vector<float> easy(40, 1.2f);
    CHECK(song_stars(easy) == doctest::Approx(1.2f));
    // Four hard bars in a song of forty: the average stays low, the rating does not.
    std::vector<float> mixed = easy;
    mixed[10] = mixed[11] = 4.6f;
    mixed[30] = mixed[31] = 4.6f;
    float mean = 0.0f;
    for (float b : mixed) {
        mean += b;
    }
    mean /= static_cast<float>(mixed.size());
    CHECK(mean < 1.6f);
    CHECK(song_stars(mixed) > mean + 1.0f);
    CHECK(song_stars(mixed) <= 4.6f);
    // One hard stretch is not the song: a single bar of ten leaves the rating under its height.
    std::vector<float> one(40, 1.2f);
    one[20] = 5.0f;
    CHECK(song_stars(one) < 5.0f);
    CHECK(song_stars(one) > 1.2f);
    // A song is never rated under its easiest bar or over its hardest.
    CHECK(song_stars(mixed) >= 1.2f);
    // Few bars: a one-bar song is that bar, two bars their mean.
    CHECK(song_stars(std::vector<float>{3.0f}) == doctest::Approx(3.0f));
    CHECK(song_stars(std::vector<float>{2.0f, 4.0f}) == doctest::Approx(3.0f));
    CHECK(song_stars({}) == doctest::Approx(1.0f));
}

TEST_CASE("difficulty: half stars and levels") {
    CHECK(half_stars_of(1.0f) == 2);
    CHECK(half_stars_of(1.24f) == 2);
    CHECK(half_stars_of(1.26f) == 3);
    CHECK(half_stars_of(2.74f) == 5);
    CHECK(half_stars_of(2.76f) == 6);
    CHECK(half_stars_of(5.0f) == 10);
    CHECK(half_stars_of(0.0f) == kMinHalfStars);
    CHECK(half_stars_of(9.0f) == kMaxHalfStars);
    // The level is the half stars rounded half up to whole stars.
    CHECK(level_of_half_stars(2) == 1);
    CHECK(level_of_half_stars(3) == 2);
    CHECK(level_of_half_stars(4) == 2);
    CHECK(level_of_half_stars(5) == 3);
    CHECK(level_of_half_stars(9) == 5);
    CHECK(level_of_half_stars(10) == 5);
    CHECK(level_of_half_stars(0) == 1);
    CHECK(level_of_half_stars(99) == 5);
}

TEST_CASE("difficulty: the hardest bars, hardest first, a tie to the earlier bar") {
    SongDifficulty d;
    d.bars = {1.0f, 3.0f, 2.5f, 4.0f, 3.0f, 1.5f, 4.0f};
    d.numbers = {1, 2, 3, 4, 5, 6, 7};
    // Within a star of the peak (4): the two 4.0 bars and the two 3.0 ones.
    CHECK(hardest_bars(d, 6) == std::vector<int>{4, 7, 2, 5});
    CHECK(hardest_bars(d, 3) == std::vector<int>{4, 7, 2});
    CHECK(hardest_bars(d, 0).empty());
    // A bar at exactly the floor counts, one under it does not.
    CHECK(hardest_bars(d, 6, 3.0f) == std::vector<int>{4, 7, 2, 5});
    CHECK(hardest_bars(d, 6, 3.01f) == std::vector<int>{4, 7});
    // An easy song suggests nothing: no bar reaches the floor.
    SongDifficulty easy;
    easy.bars = {1.0f, 1.4f, 1.9f};
    easy.numbers = {1, 2, 3};
    CHECK(hardest_bars(easy, 3).empty());
    CHECK(hardest_bars(SongDifficulty{}, 3).empty());
}

// ---- Calibration: real and made-up pieces -------------------------------------------------------

// The pieces and the ratings they must keep. Ranges and order are pinned, not the floats behind
// them. The scale, the arpeggio and the etude are written for this repo
// (make-difficulty-pieces.py); the Gymnopedie and the prelude are Mutopia's (tests/data/README.md).
TEST_CASE("difficulty calibration: the pieces rate in order, each in its range") {
    struct Piece1 {
        const char* file;
        int lowest; // half stars
        int highest;
    };
    const Piece1 pieces[] = {
        {"difficulty-scale.mid", 2, 3},     // C major scale, quarter notes, one hand
        {"difficulty-arpeggio.mid", 3, 5},  // broken chords over a held bass
        {"satie-gymnopedie-1.mid", 4, 6},   // slow waltz, chords, wide left-hand leaps
        {"bach-bwv846-prelude.mid", 5, 7},  // sixteenths in both hands, every bar
        {"difficulty-etude.mid", 8, 9},     // fast figuration in five flats, stride left hand
        {"difficulty-toccata.mid", 10, 10}, // triads and triplets on black keys, both hands
    };
    float last = 0.0f;
    for (const Piece1& p : pieces) {
        const SongDifficulty d = rated(p.file);
        MESSAGE(std::string(p.file)
                << ": " << d.stars << " stars, " << d.half_stars << " half stars");
        CHECK_MESSAGE(d.half_stars >= p.lowest, p.file);
        CHECK_MESSAGE(d.half_stars <= p.highest, p.file);
        // Each is harder than the one before by at least a quarter star.
        CHECK_MESSAGE(d.stars >= last + 0.25f, p.file);
        last = d.stars;
    }
    // The étude is hard but not off the scale: the toccata sits above it with room to spare.
    CHECK(rated("difficulty-etude.mid").stars < 4.75f);
    CHECK(rated("difficulty-etude.mid").stars >= 4.25f);
    CHECK(rated("difficulty-toccata.mid").stars >= 4.75f);
}

TEST_CASE("difficulty calibration: the hard bars of a real piece are the hard ones") {
    const SongDifficulty bach = rated("bach-bwv846-prelude.mid");
    const SongDifficulty satie = rated("satie-gymnopedie-1.mid");
    // The song takes its hardest stretches: above the mean of its bars, at most its hardest bar.
    for (const SongDifficulty* d : {&bach, &satie}) {
        float mean = 0.0f;
        float hardest = 0.0f;
        for (float b : d->bars) {
            mean += b;
            hardest = std::max(hardest, b);
        }
        mean /= static_cast<float>(d->bars.size());
        CHECK(d->stars > mean);
        CHECK(d->stars <= hardest);
    }
    // The prelude is even: no bar is far from the rest. The waltz has quiet bars and busy ones.
    const auto spread = [](const SongDifficulty& d) {
        const auto [lo, hi] = std::minmax_element(d.bars.begin(), d.bars.end());
        return *hi - *lo;
    };
    CHECK(spread(bach) > 0.0f);
    CHECK(spread(satie) > 0.0f);
    // The etude's every bar is hard.
    const SongDifficulty etude = rated("difficulty-etude.mid");
    CHECK(*std::min_element(etude.bars.begin(), etude.bars.end()) >= 3.5f);
    // The scale's every bar is easy.
    const SongDifficulty scale = rated("difficulty-scale.mid");
    CHECK(*std::max_element(scale.bars.begin(), scale.bars.end()) <= 2.0f);
}

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
