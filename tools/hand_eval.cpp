// SPDX-License-Identifier: GPL-3.0-or-later
// hand-eval: scores the automatic hand split against reference hands, over a corpus of song files.
//
//   hand-eval [--detail SUBSTR [--bars FROM TO]] [--no-single-lines] [--keep-parts] [--time]
//   PATH...
//
// Every PATH is a song file (.mid, .mxl, .musicxml, .mscz, .mscx, .abc) or a folder searched for
// them. A file is part of the corpus when its notes come with both hands (named tracks, staves).
// Its hands are taken away, the split runs, and the result is compared with them: per song, the
// notes compared and the fixed and smart agreement, then the corpus totals (pooled over notes, and
// the mean of the songs). Two reads of the same piece count once each; the corpus lists are the
// caller's business.
//
// The hands of a song are taken away and its tracks merged into one (--keep-parts leaves them as
// they are, which lets the single-line rule alone find the hands of a file with one track a hand).
//
// --detail SUBSTR lists the misassigned notes of each song whose path contains SUBSTR, with the
// kind of mistake (see classify below), and counts them by kind.
//
// --time times the split of a synthetic song of 120,000 notes: a one-track piece with two hands
// woven in it (the case the pitch rules work hardest on) and a plain ascending line (which the
// single-line rule answers without the pitch rules).
#include "etude/abc.hpp"
#include "etude/hand_split.hpp"
#include "etude/instrument.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>
#include <string>
#include <vector>

using namespace etude;
namespace fs = std::filesystem;

namespace {

struct Reference {
    Song named;    // hands from the file (ground truth)
    Song stripped; // the same notes with every hand Unknown
};

std::vector<std::uint8_t> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

// The song with the hands taken away. With `merge`, the notes of the hand parts (not drums, not
// accompaniment) also share one new track, as in a format 0 file, so that the part structure
// gives the split no hint: the case the pitch rules exist for.
Song strip_hands(Song s, bool merge) {
    constexpr std::uint16_t kMergedTrack = 60000;
    const bool keyboard = has_keyboard_part(s);
    std::vector<Part> parts;
    Part merged;
    bool have_merged = false;
    for (Part& p : s.parts) {
        p.hand = Hand::Unknown;
        if (!merge || p.drum || p.accompaniment || is_accompaniment_part(p, keyboard)) {
            parts.push_back(p);
        } else if (!have_merged) {
            have_merged = true;
            merged = p;
            merged.track = kMergedTrack;
            merged.channel = 0;
            merged.name.clear();
        } else {
            merged.note_count += p.note_count;
        }
    }
    if (have_merged) {
        parts.push_back(merged);
    }
    for (Note& n : s.notes) {
        n.hand = Hand::Unknown;
        if (!merge || n.drum || n.accompaniment) {
            continue;
        }
        bool hand_part = true;
        for (const Part& p : s.parts) {
            if (p.track == n.track && p.channel == n.channel) {
                hand_part = !(p.accompaniment || is_accompaniment_part(p, keyboard));
            }
        }
        if (hand_part) {
            n.track = kMergedTrack;
            n.channel = 0;
        }
    }
    s.parts = std::move(parts);
    return s;
}

bool both_hands(const Song& s) {
    bool left = false;
    bool right = false;
    for (const Note& n : s.notes) {
        if (!n.drum && !n.accompaniment) {
            left = left || n.hand == Hand::Left;
            right = right || n.hand == Hand::Right;
        }
    }
    return left && right;
}

bool load_reference(const fs::path& p, bool merge, Reference& out) {
    const std::string ext = p.extension().string();
    const std::vector<std::uint8_t> bytes = read_bytes(p);
    if (ext == ".mid") {
        MidiLoadResult named = load_midi(bytes);
        MidiLoadOptions strip;
        strip.assign_hands = false;
        MidiLoadResult plain = load_midi(bytes, strip);
        if (!named.ok || !plain.ok || named.song.notes.size() != plain.song.notes.size()) {
            return false;
        }
        out.named = std::move(named.song);
        out.stripped = strip_hands(std::move(plain.song), merge);
    } else {
        ScoreLoadResult r;
        if (ext == ".abc") {
            r = load_abc(bytes);
        } else if (ext == ".mxl" || ext == ".musicxml" || ext == ".mscz" || ext == ".mscx") {
            r = load_score(bytes);
        } else {
            return false;
        }
        if (!r.ok) {
            return false;
        }
        out.stripped = strip_hands(r.song, merge);
        out.named = std::move(r.song);
    }
    return both_hands(out.named);
}

HandSplitOptions options(SplitMethod m, bool single_lines) {
    HandSplitOptions o;
    o.method = m;
    o.single_lines_whole = single_lines;
    return o;
}

// What kind of mistake a misassigned note is, first match wins:
//  ref-unreachable the reference gives one hand a chord wider than a major tenth
//  chord-cut     its chord (notes within the chord window) is one hand in the reference and was
//                cut between the hands
//  chord-merged  its chord is both hands in the reference and was given to one
//  crossing      it sits on the wrong side of a note of the other hand that sounds with it
//  repeated      the same pitch was played by its reference hand a moment before (within 2 beats)
//  inner-left    a reference left-hand note from middle C up (the left hand in the middle)
//  inner-right   a reference right-hand note under middle C
//  other         anything else
const char* classify(const Song& truth, const std::vector<Hand>& got, std::size_t i,
                     const std::vector<std::size_t>& chord_first,
                     const std::vector<std::size_t>& chord_last) {
    const Note& n = truth.notes[i];
    const std::size_t a = chord_first[i];
    const std::size_t b = chord_last[i];
    // The reference gives one hand more than it can reach (a major tenth) in this chord: no
    // hand model can follow it.
    for (const Hand h : {Hand::Left, Hand::Right}) {
        int lowest = 128;
        int highest = -1;
        for (std::size_t k = a; k <= b; ++k) {
            if (truth.notes[k].hand == h) {
                lowest = std::min<int>(lowest, truth.notes[k].pitch);
                highest = std::max<int>(highest, truth.notes[k].pitch);
            }
        }
        if (highest - lowest > kHandReachSemitones) {
            return "ref-unreachable";
        }
    }
    if (b > a) {
        bool ref_same = true;
        bool got_same = true;
        for (std::size_t k = a; k <= b; ++k) {
            if (truth.notes[k].hand == Hand::Unknown) {
                continue;
            }
            ref_same = ref_same && truth.notes[k].hand == truth.notes[a].hand;
            got_same = got_same && got[k] == got[a];
        }
        if (ref_same && !got_same) {
            return "chord-cut";
        }
        if (!ref_same && got_same) {
            return "chord-merged";
        }
    }
    // A note of the other hand that sounds at its start, on the wrong side of it.
    for (std::size_t k = 0; k < truth.notes.size(); ++k) {
        const Note& o = truth.notes[k];
        if (o.start_s > n.start_s + 1e-6) {
            break;
        }
        if (o.hand == Hand::Unknown || o.hand == n.hand || o.drum ||
            o.start_s + o.duration_s <= n.start_s + 1e-6) {
            continue;
        }
        if (n.hand == Hand::Left ? o.pitch < n.pitch : o.pitch > n.pitch) {
            return "crossing";
        }
    }
    const std::int64_t span = 2 * std::max(1, truth.tempo.ticks_per_quarter);
    for (std::size_t k = i; k-- > 0;) {
        const Note& o = truth.notes[k];
        if (n.start_tick - o.start_tick > span) {
            break;
        }
        if (o.hand == n.hand && o.pitch == n.pitch) {
            return "repeated";
        }
    }
    if (n.hand == Hand::Left && n.pitch >= kMiddleC) {
        return "inner-left";
    }
    if (n.hand == Hand::Right && n.pitch < kMiddleC) {
        return "inner-right";
    }
    return "other";
}

// All notes of bars [from, to], one per line, with the reference hand and the split's.
void dump(const Reference& ref, const std::vector<Hand>& got, int from, int to) {
    const Song& t = ref.named;
    for (std::size_t i = 0; i < t.notes.size(); ++i) {
        const Note& n = t.notes[i];
        if (n.measure < from || n.measure > to) {
            continue;
        }
        std::printf("    bar %3d beat %7.2f len %5.2f pitch %3d  ref %s  got %s%s\n", n.measure,
                    static_cast<double>(n.start_tick) / t.tempo.ticks_per_quarter,
                    static_cast<double>(n.end_tick - n.start_tick) / t.tempo.ticks_per_quarter,
                    n.pitch, n.hand == Hand::Left ? "L" : "R", got[i] == Hand::Left ? "L" : "R",
                    got[i] == n.hand ? "" : "  <--");
    }
}

void detail(const Reference& ref, const std::vector<Hand>& got) {
    const Song& t = ref.named;
    std::vector<std::size_t> first(t.notes.size());
    std::vector<std::size_t> last(t.notes.size());
    for (std::size_t i = 0; i < t.notes.size();) {
        std::size_t j = i;
        while (j + 1 < t.notes.size() &&
               t.notes[j + 1].start_s - t.notes[i].start_s <= kChordWindowSeconds) {
            ++j;
        }
        for (std::size_t k = i; k <= j; ++k) {
            first[k] = i;
            last[k] = j;
        }
        i = j + 1;
    }
    std::map<std::string, int> kinds;
    for (std::size_t i = 0; i < t.notes.size(); ++i) {
        const Note& n = t.notes[i];
        if (n.drum || n.hand == Hand::Unknown || got[i] == n.hand) {
            continue;
        }
        const char* kind = classify(t, got, i, first, last);
        ++kinds[kind];
        std::printf("    bar %3d beat %7.2f  pitch %3d  ref %s  got %s  %s\n", n.measure,
                    static_cast<double>(n.start_tick) / t.tempo.ticks_per_quarter, n.pitch,
                    n.hand == Hand::Left ? "L" : "R", got[i] == Hand::Left ? "L" : "R", kind);
    }
    for (const auto& [k, c] : kinds) {
        std::printf("  %-13s %d\n", k.c_str(), c);
    }
}

// The same notes with the same hands (a score in three file formats): one piece, counted once.
std::uint64_t notes_signature(const Song& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (const Note& n : s.notes) {
        for (std::uint64_t v :
             {static_cast<std::uint64_t>(n.start_tick), static_cast<std::uint64_t>(n.pitch),
              static_cast<std::uint64_t>(n.hand)}) {
            h = (h ^ v) * 1099511628211ull;
        }
    }
    return h;
}

// Adds the song files of `p` (the file itself, or the files under a folder). False, with a line on
// stderr, when there is no such path.
bool collect(const fs::path& p, std::vector<fs::path>& out, std::set<fs::path>& named) {
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        std::fprintf(stderr, "hand-eval: cannot read %s\n", p.string().c_str());
        return false;
    }
    if (fs::is_directory(p)) {
        std::vector<fs::path> found;
        for (const auto& e : fs::recursive_directory_iterator(p)) {
            if (e.is_regular_file()) {
                found.push_back(e.path());
            }
        }
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    } else {
        out.push_back(p);
        named.insert(p);
    }
    return true;
}

// A one-track song of `n` notes: a bass line of dotted figures under a melody with a long range,
// chords now and then, so every pitch rule runs.
Song synthetic_two_hands(int n) {
    Song s;
    s.tempo.ticks_per_quarter = 480;
    s.parts.push_back(Part{});
    s.parts.back().note_count = n;
    std::uint32_t x = 12345;
    auto next = [&] {
        x = x * 1664525u + 1013904223u;
        return x >> 8;
    };
    std::int64_t tick = 0;
    int melody = 72;
    int bass = 45;
    for (int i = 0; i < n; ++i) {
        Note note;
        const bool left = (i % 3) == 0;
        if (left) {
            bass = std::clamp(bass + static_cast<int>(next() % 7) - 3, 36, 58);
        } else {
            melody = std::clamp(melody + static_cast<int>(next() % 9) - 4, 55, 88);
        }
        note.pitch = static_cast<std::uint8_t>(left ? bass : melody);
        note.start_tick = tick;
        note.end_tick = tick + 480;
        note.start_s = static_cast<double>(tick) / 960.0;
        note.duration_s = 0.5;
        note.measure = 1 + static_cast<int>(tick / 1920);
        s.notes.push_back(note);
        if (i % 3 != 2) {
            tick += 240;
        }
    }
    std::stable_sort(s.notes.begin(), s.notes.end(), [](const Note& a, const Note& b) {
        return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
    });
    return s;
}

// `n` notes in block chords of six (two under the left hand, four over the right), one a beat,
// from notes that drift up and down: the case with the most notes in every step of the search.
Song synthetic_block_chords(int n) {
    Song s;
    s.tempo.ticks_per_quarter = 480;
    s.parts.push_back(Part{});
    s.parts.back().note_count = n;
    std::uint32_t x = 99;
    auto next = [&] {
        x = x * 1664525u + 1013904223u;
        return x >> 8;
    };
    int root = 48;
    for (int i = 0; i < n / 6; ++i) {
        root = std::clamp(root + static_cast<int>(next() % 5) - 2, 40, 58);
        for (const int p : {root - 12, root, root + 12, root + 16, root + 19, root + 24}) {
            Note note;
            note.pitch = static_cast<std::uint8_t>(p);
            note.start_tick = static_cast<std::int64_t>(i) * 480;
            note.end_tick = note.start_tick + 480;
            note.start_s = static_cast<double>(note.start_tick) / 960.0;
            note.duration_s = 0.5;
            note.measure = 1 + i / 4;
            s.notes.push_back(note);
        }
    }
    return s;
}

// All the notes at one instant: nothing a real file does, but it must not blow up.
Song synthetic_blob(int n) {
    Song s;
    s.tempo.ticks_per_quarter = 480;
    s.parts.push_back(Part{});
    s.parts.back().note_count = n;
    for (int i = 0; i < n; ++i) {
        Note note;
        note.pitch = static_cast<std::uint8_t>(24 + i % 96);
        note.start_tick = 0;
        note.end_tick = 480;
        note.duration_s = 0.5;
        s.notes.push_back(note);
    }
    return s;
}

// One note an onset, pitches drawn at random over six octaves: no structure for the registers or
// the lines to find, so the search does as much work as it ever does.
Song synthetic_random(int n) {
    Song s;
    s.tempo.ticks_per_quarter = 480;
    s.parts.push_back(Part{});
    s.parts.back().note_count = n;
    std::uint32_t x = 4242;
    for (int i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        Note note;
        note.pitch = static_cast<std::uint8_t>(30 + (x >> 8) % 72);
        note.start_tick = static_cast<std::int64_t>(i) * 240;
        note.end_tick = note.start_tick + 480;
        note.start_s = static_cast<double>(note.start_tick) / 960.0;
        note.duration_s = 0.5;
        note.measure = 1 + i / 8;
        s.notes.push_back(note);
    }
    return s;
}

Song synthetic_line(int n) {
    Song s;
    s.tempo.ticks_per_quarter = 480;
    s.parts.push_back(Part{});
    s.parts.back().note_count = n;
    static const int scale[] = {60, 62, 64, 65, 67, 69, 71, 72};
    for (int i = 0; i < n; ++i) {
        Note note;
        note.pitch = static_cast<std::uint8_t>(scale[i % 8]);
        note.start_tick = static_cast<std::int64_t>(i) * 240;
        note.end_tick = note.start_tick + 240;
        note.start_s = static_cast<double>(note.start_tick) / 960.0;
        note.duration_s = 0.25;
        note.measure = 1 + i / 8;
        s.notes.push_back(note);
    }
    return s;
}

double time_split(const Song& s, const HandSplitOptions& o) {
    double best = 1e30;
    for (int rep = 0; rep < 5; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<Hand> h = split_hands(s, o);
        const auto t1 = std::chrono::steady_clock::now();
        if (h.size() != s.notes.size()) {
            std::abort();
        }
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best;
}

} // namespace

int main(int argc, char** argv) {
    std::string detail_of;
    int bars_from = 0;
    int bars_to = 0;
    bool single_lines = true;
    bool timing = false;
    bool merge = true;
    std::vector<fs::path> paths;
    std::set<fs::path> named; // the files asked for one by one, not found under a folder
    bool bad_path = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--detail") == 0 && i + 1 < argc) {
            detail_of = argv[++i];
        } else if (std::strcmp(argv[i], "--bars") == 0 && i + 2 < argc) {
            bars_from = std::atoi(argv[++i]);
            bars_to = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--no-single-lines") == 0) {
            single_lines = false;
        } else if (std::strcmp(argv[i], "--keep-parts") == 0) {
            merge = false;
        } else if (std::strcmp(argv[i], "--time") == 0) {
            timing = true;
        } else {
            bad_path = !collect(argv[i], paths, named) || bad_path;
        }
    }
    if (paths.empty() && !timing) {
        if (!bad_path) {
            std::fputs("usage: hand-eval [--detail SUBSTR [--bars FROM TO]] [--no-single-lines] "
                       "[--keep-parts] [--time] PATH...\n"
                       "  PATH: a song file, or a folder searched for them (try tests/data)\n",
                       stderr);
        }
        return 2;
    }
    if (timing) {
        const Song woven = synthetic_two_hands(120000);
        const Song line = synthetic_line(120000);
        const Song blocks = synthetic_block_chords(120000);
        const Song blob = synthetic_blob(120000);
        const Song random = synthetic_random(120000);
        // The large file of tests/song_opener_test.cpp: 15,000 bars of eight notes, as ABC.
        std::string text = "X:1\nT:Large\nM:4/4\nL:1/8\nK:C\n";
        for (int bar = 0; bar < 15000; ++bar) {
            text += "CDEF GABc|\n";
        }
        const std::vector<std::uint8_t> bytes(text.begin(), text.end());
        const ScoreLoadResult abc = load_abc(bytes);
        std::printf("split time, best of 5 (ms), smart:\n");
        std::printf("  %zu notes, two hands woven in one part: %.1f\n", woven.notes.size(),
                    time_split(woven, options(SplitMethod::Smart, true)));
        std::printf("  %zu notes, block chords of six:          %.1f\n", blocks.notes.size(),
                    time_split(blocks, options(SplitMethod::Smart, true)));
        std::printf("  %zu notes, all at one instant:          %.1f\n", blob.notes.size(),
                    time_split(blob, options(SplitMethod::Smart, true)));
        std::printf("  %zu notes, random pitches, one an onset: %.1f\n", random.notes.size(),
                    time_split(random, options(SplitMethod::Smart, true)));
        std::printf("  %zu notes, one ascending line:          %.1f\n", line.notes.size(),
                    time_split(line, options(SplitMethod::Smart, true)));
        std::printf("  %zu notes, the large ABC file:          %.1f\n", abc.song.notes.size(),
                    time_split(abc.song, options(SplitMethod::Smart, true)));
        std::printf("  %zu notes, the large ABC file, no single-line rule: %.1f\n",
                    abc.song.notes.size(),
                    time_split(abc.song, options(SplitMethod::Smart, false)));
    }
    int total_compared = 0;
    int total_fixed = 0;
    int total_smart = 0;
    double sum_fixed = 0.0;
    double sum_smart = 0.0;
    int songs = 0;
    std::map<std::uint64_t, std::string> seen;
    for (const fs::path& p : paths) {
        Reference ref;
        if (!load_reference(p, merge, ref)) {
            if (named.count(p) != 0) {
                std::fprintf(stderr, "hand-eval: %s is not a song file with reference hands\n",
                             p.string().c_str());
                bad_path = true;
            }
            continue;
        }
        const auto [at, fresh] = seen.emplace(notes_signature(ref.named), p.filename().string());
        if (!fresh) {
            std::printf("%-62s same notes as %s: not counted again\n",
                        p.filename().string().c_str(), at->second.c_str());
            continue;
        }
        const std::vector<Hand> f =
            split_hands(ref.stripped, options(SplitMethod::Fixed, single_lines));
        const std::vector<Hand> s =
            split_hands(ref.stripped, options(SplitMethod::Smart, single_lines));
        const HandAgreement fa = compare_hands(ref.named, f);
        const HandAgreement sa = compare_hands(ref.named, s);
        std::printf("%-62s %6d notes  fixed %6.2f%%  smart %6.2f%%\n",
                    p.filename().string().c_str(), sa.compared, fa.fraction() * 100.0,
                    sa.fraction() * 100.0);
        total_compared += sa.compared;
        total_fixed += fa.matching;
        total_smart += sa.matching;
        sum_fixed += fa.fraction();
        sum_smart += sa.fraction();
        ++songs;
        if (!detail_of.empty() && p.string().find(detail_of) != std::string::npos) {
            if (bars_to > 0) {
                dump(ref, s, bars_from, bars_to);
            } else {
                detail(ref, s);
            }
        }
    }
    if (songs > 0) {
        std::printf("%d songs, %d notes: pooled fixed %.2f%% smart %.2f%%; mean of songs fixed "
                    "%.2f%% smart %.2f%%\n",
                    songs, total_compared, 100.0 * total_fixed / total_compared,
                    100.0 * total_smart / total_compared, 100.0 * sum_fixed / songs,
                    100.0 * sum_smart / songs);
    }
    return bad_path ? 1 : 0;
}
