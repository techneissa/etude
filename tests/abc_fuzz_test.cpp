// SPDX-License-Identifier: GPL-3.0-or-later
// The ABC loader gets damaged, truncated and hostile text: it must return (ok or an error), never
// crash or hang, and a Song it returns must be well formed. Run under ASan and UBSan for the memory
// checks; the assertions here check the Song's shape and that no input takes long.
//
// Each group runs twice. A short run (a few hundred inputs, about a second) is part of the normal
// suite. The long run (thousands of inputs and 20000-character shapes, half a minute) is skipped
// unless ETUDE_FUZZ_LONG is set; ctest sets it for the test "etude_abc_fuzz_long", so
// `just ci-linux` runs it and `just test` does not.
#include "etude/abc.hpp"
#include "etude/library.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;

namespace {

using Bytes = std::vector<std::uint8_t>;

// xorshift64*: a fixed sequence, so a failure replays.
class Rng {
  public:
    explicit Rng(std::uint64_t seed) : state_(seed * 2685821657736338717ull + 1) {}
    std::uint64_t next() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 2685821657736338717ull;
    }
    std::size_t below(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }

  private:
    std::uint64_t state_;
};

// Characters that mean something in ABC: mutations draw from them so the parser's branches are hit.
constexpr char kInteresting[] =
    "ABCDEFGabcdefg^_=,'/0123456789()[]{}|:<>-.&\"!+%\\~xzZyV:KMLQXTw \n\r";

Bytes mutate(const Bytes& in, Rng& rng) {
    Bytes out = in;
    const std::size_t edits = 1 + rng.below(10);
    for (std::size_t i = 0; i < edits && !out.empty(); ++i) {
        const std::size_t at = rng.below(out.size());
        switch (rng.below(6)) {
        case 0:
            out[at] = static_cast<std::uint8_t>(rng.next());
            break;
        case 1:
            out[at] = static_cast<std::uint8_t>(kInteresting[rng.below(sizeof(kInteresting) - 1)]);
            break;
        case 2:
            out.erase(out.begin() + static_cast<std::ptrdiff_t>(at));
            break;
        case 3:
            out.insert(
                out.begin() + static_cast<std::ptrdiff_t>(at),
                static_cast<std::uint8_t>(kInteresting[rng.below(sizeof(kInteresting) - 1)]));
            break;
        case 4: {
            // Repeat a piece of the text: nested repeats and long runs.
            const std::size_t len = std::min<std::size_t>(1 + rng.below(40), out.size() - at);
            const Bytes piece(out.begin() + static_cast<std::ptrdiff_t>(at),
                              out.begin() + static_cast<std::ptrdiff_t>(at + len));
            for (std::size_t k = 0, n = 1 + rng.below(6); k < n; ++k) {
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(at), piece.begin(),
                           piece.end());
            }
            break;
        }
        default:
            out.resize(at);
            break; // cut the tail off
        }
    }
    return out;
}

// What every Song returned by the loader must satisfy (the same shape as the score loaders').
void expect_well_formed(const ScoreLoadResult& r) {
    if (!r.ok) {
        CHECK(r.error != ScoreFileError::None);
        CHECK_FALSE(r.message.empty());
        CHECK(r.song.notes.empty());
        return;
    }
    const Song& s = r.song;
    REQUIRE_FALSE(s.notes.empty());
    REQUIRE_FALSE(s.measures.empty());
    REQUIRE_FALSE(s.time_signatures.empty());
    CHECK(s.tempo.ticks_per_quarter > 0);
    CHECK(s.notes.size() <= kMaxScoreNotes);
    std::int64_t last_start = 0;
    for (const Note& n : s.notes) {
        REQUIRE(n.end_tick > n.start_tick);
        REQUIRE(n.duration_s > 0.0);
        REQUIRE(n.start_tick >= last_start);
        last_start = n.start_tick;
        REQUIRE(n.pitch <= 127);
        REQUIRE(n.velocity >= 1);
        REQUIRE(n.velocity <= 127);
        REQUIRE(n.channel <= 15);
        REQUIRE(n.track < s.parts.size() + 1);
    }
    for (std::size_t i = 1; i < s.measures.size(); ++i) {
        REQUIRE(s.measures[i].start_tick > s.measures[i - 1].start_tick);
        REQUIRE(s.measures[i].number == s.measures[i - 1].number + 1);
    }
    for (const Note& n : s.notes) {
        REQUIRE(n.measure >= s.measures.front().number);
        REQUIRE(n.measure <= s.measures.back().number);
        REQUIRE(n.written_measure() >= 0);
    }
    for (const TempoChange& t : s.tempo.changes) {
        REQUIRE(t.us_per_quarter >= 1);
    }
    for (const Part& p : s.parts) {
        CHECK(p.note_count > 0);
    }
}

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Runs the loader on `bytes` every way the app does: the first tune, each listed tune, with and
// without chord symbols, and the library entry. Returns the seconds it took.
double load_all_ways(const Bytes& bytes) {
    const auto start = std::chrono::steady_clock::now();
    for (const bool chords : {false, true}) {
        AbcOptions options;
        options.chords_as_left_hand = chords;
        expect_well_formed(load_abc(bytes, options));
    }
    const std::vector<AbcTune> tunes = list_abc_tunes(bytes);
    std::size_t shown = 0;
    for (const AbcTune& t : tunes) {
        if (shown++ >= 3) {
            break;
        }
        AbcOptions options;
        options.chords_as_left_hand = true;
        options.listed = &t;
        expect_well_formed(load_abc(bytes, options));
    }
    LibraryLoadOptions library;
    library.abc_chords = true;
    library.abc_tunes = &tunes;
    const LibraryLoad entry = load_library_entry(bytes, "fuzz.abc", library);
    if (entry.ok) {
        CHECK(entry.entry.note_count > 0);
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

const char* const kTunes[] = {"abc-reel-repeats.abc", "abc-dorian.abc", "abc-triplets.abc",
                              "abc-two-voices.abc", "abc-collection.abc"};

bool long_run() { return std::getenv("ETUDE_FUZZ_LONG") != nullptr; }

// Every `stride`-th prefix of the test tunes.
void prefixes(std::size_t stride) {
    for (const char* name : kTunes) {
        const Bytes data = read_data(name);
        CAPTURE(name);
        for (std::size_t cut = 0; cut <= data.size(); cut += stride) {
            const Bytes prefix(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(cut));
            expect_well_formed(load_abc(prefix));
        }
    }
}

void mutated(int per_tune) {
    Rng rng(0xABC0);
    double slowest = 0.0;
    for (const char* name : kTunes) {
        const Bytes data = read_data(name);
        CAPTURE(name);
        for (int i = 0; i < per_tune; ++i) {
            const Bytes damaged = mutate(data, rng);
            slowest = std::max(slowest, load_all_ways(damaged));
        }
    }
    // No input is slow: a few kilobytes of text load in far under a second even in a debug build.
    CHECK(slowest < 1.0);
}

void random_inputs(int count) {
    Rng rng(0xFACE);
    for (int i = 0; i < count; ++i) {
        Bytes noise(rng.below(600));
        for (std::uint8_t& b : noise) {
            b = static_cast<std::uint8_t>(rng.next());
        }
        expect_well_formed(load_abc(noise));
        // Something that starts like a tune and goes on in ABC's alphabet.
        std::string text = "X:1\nT:fuzz\nK:";
        text += kInteresting[rng.below(sizeof(kInteresting) - 1)];
        text += "\n";
        for (std::size_t k = 0, n = rng.below(500); k < n; ++k) {
            text += kInteresting[rng.below(sizeof(kInteresting) - 1)];
        }
        const Bytes bytes(text.begin(), text.end());
        load_all_ways(bytes);
    }
}

// Inputs shaped to find a quadratic loop or a runaway: long runs of one sign, huge numbers, many
// repeat signs, jumps, tunes and voices. `n` is the length of a run.
void pathological(std::size_t n) {
    auto bytes_of = [](const std::string& s) { return Bytes(s.begin(), s.end()); };
    const std::string head = "X:1\nT:t\nM:4/4\nL:1/8\nK:C\n";
    std::vector<std::string> cases;
    for (const char sign : {'[', '(', '{', '"', '!', '|', ':'}) {
        cases.push_back(head + std::string(n, sign));
    }
    cases.push_back(head + std::string(n / 4, '&') + "C");
    cases.push_back(head + std::string(n, '>') + "CD");
    cases.push_back(head + std::string(n, '-') + "CD");
    cases.push_back(head + "|:" + std::string(n / 4, ':') + "|" + std::string(n / 4, '1'));
    cases.push_back(head + std::string(n / 10, '^') + "C" + std::string(n / 10, ',') + "C");
    cases.push_back(head + "C" + std::string(n, '/') + "D");
    cases.push_back(head + "C" + std::string(n, '9') + "D");
    cases.push_back(head + "(3:" + std::string(n, '9') + " CDE");
    cases.push_back(head + "[V:" + std::string(n, 'a') + "] C");
    cases.push_back(head + "K:" + std::string(n, 'D'));
    cases.push_back(head + "M:" + std::string(n, '1'));
    cases.push_back(head + "Q:" + std::string(n, '"') + "\nC");
    cases.push_back(head + "\"" + std::string(n, 'C') + "\" C");
    {
        std::string many;
        for (std::size_t i = 0; i < n; ++i) {
            many += "|: C :| ";
        }
        cases.push_back(head + many);
    }
    {
        std::string endings;
        for (std::size_t i = 0; i < n; ++i) {
            endings += "|1 C :|2 D ";
        }
        cases.push_back(head + endings);
    }
    {
        std::string jumps;
        for (std::size_t i = 0; i < n / 4; ++i) {
            jumps += "!D.S.! C !segno! D !D.C.! E !fine! F !coda! G !dacoda! ";
        }
        cases.push_back(head + jumps);
    }
    {
        std::string tunes;
        for (std::size_t i = 0; i < n; ++i) {
            tunes += "X:" + std::to_string(i % 7) + "\nK:C\nC\n";
        }
        cases.push_back(tunes);
    }
    {
        std::string voices = head;
        for (std::size_t i = 0; i < n; ++i) {
            voices += "V:" + std::to_string(i % 100) +
                      " name=\"x\" clef=bass transpose=" + std::to_string(i) + "\n\"C\"C D\n";
        }
        cases.push_back(voices);
    }
    for (std::size_t i = 0; i < cases.size(); ++i) {
        CAPTURE(i);
        const double seconds = load_all_ways(bytes_of(cases[i]));
        CHECK(seconds < 5.0);
    }
}

} // namespace

// ---- The short run, in the normal suite

TEST_CASE("abc fuzz: prefixes of the test tunes load or fail cleanly") { prefixes(13); }

TEST_CASE("abc fuzz: mutated test tunes load or fail cleanly, and quickly") { mutated(25); }

TEST_CASE("abc fuzz: random bytes and random ABC-looking text") { random_inputs(60); }

TEST_CASE("abc fuzz: pathological shapes") { pathological(1500); }

// ---- The long run: ctest's "etude_abc_fuzz_long" sets ETUDE_FUZZ_LONG

TEST_CASE("abc fuzz long: every prefix of the test tunes" * doctest::skip(!long_run())) {
    prefixes(1);
}

TEST_CASE("abc fuzz long: mutated test tunes" * doctest::skip(!long_run())) { mutated(1500); }

TEST_CASE("abc fuzz long: random bytes and random ABC-looking text" * doctest::skip(!long_run())) {
    random_inputs(1500);
}

TEST_CASE("abc fuzz long: pathological shapes, 20000 long" * doctest::skip(!long_run())) {
    pathological(20000);
}
