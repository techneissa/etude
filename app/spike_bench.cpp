// SPDX-License-Identifier: GPL-3.0-or-later
#include "spike_bench.hpp"

#include "alloc_probe.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"

#include "etude/progress.hpp"
#include "etude/midi_writer.hpp"
#include "etude/run_events.hpp"
#include "etude/section_suggestions.hpp"
#include "etude/song.hpp"
#include "etude/song_builder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace etude::app {

namespace {

constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t kSec = 1'000'000'000;

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
}

// A song of about `n` notes in 4/4 at 240 BPM: a right-hand line, a left-hand note every other
// step and a right-hand triad on the first step of each beat group, so chords exist as in real
// music. Hands are set, as a song with named hands has them, unless `named_hands` is false: then
// one track and channel holds every note with no hand, as a plain MIDI file does.
LoadedSong made_up_song(std::size_t n, bool named_hands = true) {
    Song s;
    s.tempo = make_tempo_map(480, 240.0);
    std::uint32_t x = 2463534242u;
    auto next = [&] {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    };
    int melody = 72;
    int bass = 48;
    for (std::int64_t step = 0; s.notes.size() < n; ++step) {
        const std::int64_t tick = step * 240;
        auto add = [&](int pitch, Hand hand, int channel) {
            Note note;
            note.start_tick = tick;
            note.end_tick = tick + 230;
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.velocity = 80;
            note.hand = named_hands ? hand : Hand::Unknown;
            note.channel = static_cast<std::uint8_t>(named_hands ? channel : 0);
            note.track = static_cast<std::uint8_t>(named_hands ? channel : 0);
            s.notes.push_back(note);
        };
        melody = std::clamp(melody + static_cast<int>(next() % 5) - 2, 60, 84);
        add(melody, Hand::Right, 0);
        if (step % 4 == 0) {
            add(melody - 4, Hand::Right, 0);
            add(melody - 7, Hand::Right, 0);
        }
        if (step % 2 == 0) {
            bass = std::clamp(bass + static_cast<int>(next() % 5) - 2, 36, 55);
            add(bass, Hand::Left, 1);
        }
    }
    finish_song(s);
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Spike bench";
    out.hash = 0xBE5C;
    out.library_path = "spike.mid";
    return out;
}

struct Spike {
    double call_ms = 0.0;    // the action's call
    double frames_ms = 0.0;  // every update after it until the change is in force, summed
    double worst_ms = 0.0;   // the longest single piece of frame-thread work
    double latency_ms = 0.0; // from the call to the change in force
    double frame_total() const { return call_ms + frames_ms; }
};

template <class Action, class Done>
Spike frame_spike(SongController& c, std::int64_t& now, Action&& action, Done&& in_force) {
    Spike r;
    const auto begin = Clock::now();
    now += 16 * kMs;
    const auto t0 = Clock::now();
    action(now);
    r.call_ms = ms_since(t0);
    r.worst_ms = r.call_ms;
    // The frames that follow, 16 ms apart on the controller's clock; the real wait between them
    // is the worker's time, not the frame thread's.
    for (int i = 0; i < 4000; ++i) {
        const auto u = Clock::now();
        now += 16 * kMs;
        c.update(now, {});
        const double took = ms_since(u);
        r.frames_ms += took;
        r.worst_ms = std::max(r.worst_ms, took);
        if (in_force()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    r.latency_ms = ms_since(begin);
    return r;
}

// `rounds` runs of `action` (each followed by `restore`, not counted), reported as the least and
// the median frame-thread time, the longest single piece and the time until it was in force.
// The least is the cost with nothing else running; the machine is shared, so the median is noisy.
template <class Action, class Restore, class Done>
void measure(const char* name, SongController& c, std::int64_t& now, int rounds, Action&& action,
             Restore&& restore, Done&& in_force) {
    std::vector<Spike> all;
    for (int i = 0; i < rounds; ++i) {
        all.push_back(frame_spike(c, now, action, in_force));
        frame_spike(c, now, restore, in_force);
    }
    auto by = [&](auto key) {
        std::vector<double> v;
        for (const Spike& s : all) {
            v.push_back(key(s));
        }
        std::sort(v.begin(), v.end());
        return std::pair{v.front(), v[v.size() / 2]};
    };
    const auto frame = by([](const Spike& s) { return s.frame_total(); });
    const auto worst = by([](const Spike& s) { return s.worst_ms; });
    const auto latency = by([](const Spike& s) { return s.latency_ms; });
    std::printf("  %-38s frame thread min %7.3f med %7.3f ms | worst piece min %7.3f med %7.3f | "
                "in force after min %7.1f med %7.1f\n",
                name, frame.first, frame.second, worst.first, worst.second, latency.first,
                latency.second);
}

constexpr double kMiB = 1024.0 * 1024.0;

// One step of the hand memory bench, all in MiB of heap: where it stood after the step and the
// highest it reached, both above where it stood before the song was opened, and how far above the
// step's own start the peak was (what the step needs on top of what is already held).
struct HeapStep {
    const char* name;
    double after_mib = 0.0;
    double peak_mib = 0.0;
    double over_start_mib = 0.0;
    double ms = 0.0;
};

template <class Body> HeapStep heap_step(const char* name, std::int64_t origin, Body&& body) {
    const std::int64_t start = heap_live_bytes();
    heap_reset_peak();
    const auto begin = Clock::now();
    body();
    HeapStep r{name};
    r.ms = ms_since(begin);
    r.after_mib = static_cast<double>(heap_live_bytes() - origin) / kMiB;
    r.peak_mib = static_cast<double>(heap_peak_bytes() - origin) / kMiB;
    r.over_start_mib = static_cast<double>(heap_peak_bytes() - start) / kMiB;
    return r;
}

// The file as bytes: a song of `notes` notes written as a one-track MIDI file.
std::vector<std::uint8_t> one_track_midi(std::size_t notes) {
    const LoadedSong made = made_up_song(notes, false);
    MidiWriteOptions options;
    options.format = 0;
    return write_midi(made.song, options).bytes;
}

} // namespace

int run_hand_memory_bench(std::size_t notes) {
    const std::vector<std::uint8_t> bytes = one_track_midi(notes);
    track_heap(true);
    std::int64_t origin = heap_live_bytes(); // the file's bytes are held from here on
    std::printf("hand split heap over a one-track MIDI file of about %zu notes (%zu bytes)\n",
                notes, bytes.size());
    std::vector<HeapStep> steps;
    std::int64_t now = kSec;
    // A change of split, until the notes have their new hands; then a moment for the tables the
    // evaluators dropped, which a worker frees, to be gone, so the heap is read at rest.
    double split_ms = 0.0;
    const auto wait_split = [&](SongController& c, int point) {
        const auto begin = Clock::now();
        c.set_hand_split(point, now);
        c.wait_hand_split(now += 16 * kMs);
        c.update(now += 16 * kMs, {});
        split_ms = ms_since(begin);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    };
    const auto split_step = [&](SongController& c, const char* name, int point) {
        steps.push_back(heap_step(name, origin, [&] { wait_split(c, point); }));
        steps.back().ms = split_ms;
    };
    // The opener's three parts, in the order it does them: the loader (parse, split, rate), the
    // tables a worker makes ahead, and the controller taking the song over.
    const auto open = [&](SongController& c, const char* loader, const char* tables_ahead,
                          const char* take_over) {
        SongFileLoad loaded;
        PreparedSong tables;
        steps.push_back(
            heap_step(loader, origin, [&] { loaded = load_song_bytes(bytes, "bench.mid"); }));
        if (!loaded.ok) {
            std::printf("  (the file did not load: %s)\n", loaded.error.c_str());
            return;
        }
        steps.push_back(heap_step(tables_ahead, origin, [&] {
            tables = SongController::prepare(loaded.song.song, c.load_plan());
        }));
        steps.push_back(heap_step(take_over, origin, [&] {
            c.load(std::move(loaded.song), now, std::move(tables));
            loaded = SongFileLoad{};
            tables = PreparedSong{};
        }));
    };
    {
        SongController first; // nothing is cached yet: the loader splits the hands itself
        open(first, "open: loader, split made", "open: tables ahead", "open: controller load");
    }
    // The library scan has split the file by the time a song is opened: its answer is cached. The
    // tables the first controller dropped are freed by a worker: wait for the heap to be at rest.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    origin = heap_live_bytes();
    SongController c;
    open(c, "open: loader, split cached", "open: tables ahead", "open: controller load");
    if (!c.hand_split_available()) {
        std::printf("  (the song's hands were not decided by the split)\n");
        return 1;
    }
    split_step(c, "Split at 60", 60);
    split_step(c, "Split at 64", 64);
    split_step(c, "Split at 60 again", 60);
    split_step(c, "From the sheet", kAutoSplit);
    std::printf("  one Song's notes: %.1f MiB (%zu notes of %zu bytes)\n",
                static_cast<double>(c.song().song.notes.size() * sizeof(Note)) / kMiB,
                c.song().song.notes.size(), sizeof(Note));
    std::printf("  MiB of heap above the point before the song was opened (the file's bytes "
                "aside):\n  %-28s %10s %10s %14s %8s\n",
                "step", "after", "peak", "peak over start", "ms");
    for (const HeapStep& s : steps) {
        std::printf("  %-28s %10.2f %10.2f %14.2f %8.1f\n", s.name, s.after_mib, s.peak_mib,
                    s.over_start_mib, s.ms);
    }
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    track_heap(false);
    return 0;
}

int run_spike_bench(std::size_t notes) {
    std::printf("frame-thread spikes over a made-up song of about %zu notes (this build)\n", notes);
    LoadedSong loaded = made_up_song(notes);
    const std::size_t count = loaded.song.notes.size();
    std::printf("  song: %zu notes, %zu bars\n", count, loaded.song.measures.size());

    auto t = Clock::now();
    const std::uint64_t fingerprint = song_fingerprint(loaded.song);
    std::printf("  song_fingerprint                           %8.3f ms  (%llx)\n", ms_since(t),
                static_cast<unsigned long long>(fingerprint));

    SongController c;
    t = Clock::now();
    PreparedSong prepared = SongController::prepare(loaded.song, c.load_plan());
    std::printf("  SongController::prepare (a worker's job)   %8.3f ms\n", ms_since(t));
    std::int64_t now = kSec;
    {
        // The fingerprint not carried (what a load did before): load() takes it on this thread.
        SongController other;
        LoadedSong copy = loaded;
        PreparedSong ahead = prepared;
        t = Clock::now();
        other.load(std::move(copy), now, std::move(ahead));
        std::printf("  SongController::load, fingerprint not carried %6.3f ms\n", ms_since(t));
    }
    loaded.fingerprint = fingerprint; // as the loader thread sets it
    t = Clock::now();
    c.load(std::move(loaded), now, std::move(prepared));
    std::printf("  SongController::load, fingerprint carried  %8.3f ms\n", ms_since(t));

    std::printf("changes on the frame thread (7 rounds each):\n");
    for (const PracticeMode mode : {PracticeMode::Flow, PracticeMode::Wait}) {
        const std::string label = mode == PracticeMode::Flow ? "Flow" : "Wait";
        c.set_mode(mode, now);
        c.update(now += 16 * kMs, {});
        for (const HandFilter hands : {HandFilter::Left, HandFilter::Right}) {
            const std::string name = std::string("set_practice_hands ") +
                                     (hands == HandFilter::Left ? "left" : "right") + " (" + label +
                                     ")";
            measure(
                name.c_str(), c, now, 7, [&](std::int64_t at) { c.set_practice_hands(hands, at); },
                [&](std::int64_t at) { c.set_practice_hands(HandFilter::Both, at); },
                [&] { return c.tables_settled(); });
        }
        const std::string name = "player_set_transpose +1 + update (" + label + ")";
        measure(
            name.c_str(), c, now, 7, [&](std::int64_t at) { c.player_set_transpose(1, at); },
            [&](std::int64_t at) { c.player_set_transpose(0, at); },
            [&] { return c.tables_settled(); });
    }

    // The end of a run over the whole song (what finish_run costs besides the sort): a second of
    // play, then a change of mode ends it.
    for (const PracticeMode mode : {PracticeMode::Flow}) {
        c.set_mode(mode, now);
        c.update(now += 16 * kMs, {});
        std::vector<double> times;
        for (int round = 0; round < 5; ++round) {
            c.replay(now);
            for (int i = 0; i < 400; ++i) {
                now += 16 * kMs;
                if (i == 100) { // a key no note wants: a wrong press makes the run an attempt
                    MidiEvent press;
                    press.time_ns = now;
                    press.type = MidiEventType::NoteOn;
                    press.data1 = 21;
                    press.data2 = 90;
                    c.update(now, std::span<const MidiEvent>(&press, 1));
                } else {
                    c.update(now, {});
                }
            }
            const auto begin = Clock::now();
            c.set_mode(mode == PracticeMode::Flow ? PracticeMode::Wait : PracticeMode::Flow, now);
            times.push_back(ms_since(begin));
            c.set_mode(mode, now);
            c.pause(now);
            c.update(now += 16 * kMs, {});
            std::vector<FinishedRun> finished = c.take_finished_runs();
            if (finished.empty()) {
                std::printf("  (no run was finished)\n");
            }
        }
        std::sort(times.begin(), times.end());
        std::printf("  finish_run after 6 s of play (%s)   min %7.3f med %7.3f ms\n",
                    mode == PracticeMode::Flow ? "Flow" : "Wait", times.front(),
                    times[times.size() / 2]);
    }

    // The end of a run that filled the recorder: results in the order the evaluator settles them
    // (notes in order, a wrong press now and then), 16,384 events; and the same shuffled, the
    // worst case for the sort.
    for (const bool shuffled : {false, true}) {
        std::vector<double> hand_over_ms;
        std::vector<double> ordering_ms;
        std::vector<double> finish_ms;
        for (int round = 0; round < 9; ++round) {
            RunEventRecorder recorder;
            std::uint32_t x = 12345u + static_cast<std::uint32_t>(round);
            std::vector<std::size_t> order(kRunEventCapacity);
            for (std::size_t i = 0; i < order.size(); ++i) {
                order[i] = i;
            }
            if (shuffled) {
                for (std::size_t i = order.size() - 1; i > 0; --i) {
                    x = x * 1664525u + 1013904223u;
                    std::swap(order[i], order[x % (i + 1)]);
                }
            }
            for (const std::size_t i : order) {
                x = x * 1664525u + 1013904223u;
                if (!shuffled && i % 10 == 9) {
                    WrongPress w;
                    w.key = 40 + static_cast<int>((x >> 20) % 50);
                    w.velocity = 80;
                    w.song_s = static_cast<double>(i) * 0.04;
                    recorder.add_wrong(w);
                    continue;
                }
                NoteResult r;
                r.note_index = static_cast<std::uint32_t>(i);
                r.grade = NoteGrade::Perfect;
                r.expected_pitch = 40 + static_cast<int>((x >> 20) % 50);
                r.hand = Hand::Right;
                r.song_s = static_cast<double>(i) * 0.04;
                r.velocity = 80;
                r.press_song_s = r.song_s + 0.01;
                r.timing_error_ns = static_cast<std::int64_t>((x >> 8) % 90 - 45) * kMs;
                recorder.add(r);
            }
            RunEventRecorder twin(recorder); // the same events for the second way
            const auto begin = Clock::now();
            RunEvents out = recorder.hand_over(kRunEventCapacity);
            const double handed = ms_since(begin);
            const auto sort_begin = Clock::now();
            finish_ordering(out);
            const double sorted = ms_since(sort_begin);
            const auto whole_begin = Clock::now();
            RunEvents whole = twin.finish(kRunEventCapacity);
            const double both = ms_since(whole_begin);
            hand_over_ms.push_back(handed);
            ordering_ms.push_back(sorted);
            finish_ms.push_back(both);
            if (out.events.empty() || !(out == whole)) {
                std::printf("  (hand_over + finish_ordering differs from finish)\n");
                return 1;
            }
        }
        auto least = [](std::vector<double>& v) { return *std::min_element(v.begin(), v.end()); };
        std::printf(
            "  run end, %zu events%-10s frame thread now (hand_over) %7.3f ms | history thread "
            "(finish_ordering) %7.3f ms | before: finish() on the frame thread %7.3f ms\n",
            kRunEventCapacity, shuffled ? ", shuffled" : "", least(hand_over_ms),
            least(ordering_ms), least(finish_ms));
    }

    // A section suggestion over 200 runs of one song, as the editor asks for it after each run:
    // a song of a few hundred bars, and the made-up song's own bars.
    for (const int bars : {300, 3000, 0}) {
        Song song;
        if (bars == 0) {
            song = c.song().song;
        } else {
            song.tempo = make_tempo_map(480, 120.0);
            for (int bar = 0; bar < bars; ++bar) {
                for (int beat = 0; beat < 4; ++beat) {
                    Note n;
                    n.start_tick = (bar * 4 + beat) * 480;
                    n.end_tick = n.start_tick + 400;
                    n.pitch = static_cast<std::uint8_t>(60 + beat);
                    song.notes.push_back(n);
                }
            }
            finish_song(song);
        }
        const std::uint64_t hash = 0xBE5C;
        std::vector<RunRecord> runs;
        std::uint32_t x = 99;
        for (int r = 0; r < 200; ++r) {
            RunRecord rec;
            rec.song_hash = hash;
            rec.song_title = "Spike bench";
            rec.start_utc_s = 86'400LL * 100 + r * 1000;
            rec.end_utc_s = rec.start_utc_s + 300;
            rec.duration_ms = 300'000;
            rec.local_day = 100 + r / 4;
            int graded = 0;
            int missed = 0;
            for (const Measure& m : song.measures) {
                x = x * 1664525u + 1013904223u;
                RunBar bar;
                bar.measure = m.number;
                bar.graded = 4;
                bar.missed = (x >> 24) % 11 == 0 ? 2 : 0;
                graded += bar.graded;
                missed += bar.missed;
                rec.measures.push_back(bar);
            }
            rec.all = {graded, graded - missed};
            rec.right = rec.all;
            rec.counts.perfect = graded - missed;
            rec.counts.missed = missed;
            runs.push_back(sanitized(rec));
        }
        double best = 1e9;
        std::size_t sections = 0;
        for (int round = 0; round < 5; ++round) {
            const auto begin = Clock::now();
            const SectionSuggestions s = suggest_sections(song, runs, hash);
            best = std::min(best, ms_since(begin));
            sections = s.sections.size();
        }
        std::printf("  suggest_sections, 200 runs, %5zu bars %-6s best %8.3f ms  (%zu sections)\n",
                    song.measures.size(), "", best, sections);
    }

    // save_runs compares a finished run with the history's runs of the song (progress.hpp): one
    // pass over every run, newest first. The worst case is a history all of one song and setup.
    for (const std::size_t n : {std::size_t{1'000}, std::size_t{10'000}}) {
        std::vector<RunRecord> runs;
        runs.reserve(n);
        for (std::size_t r = 0; r < n; ++r) {
            RunRecord rec;
            rec.song_hash = 0xBE5C;
            rec.song_title = "Spike bench";
            rec.start_utc_s = 86'400LL * 100 + static_cast<std::int64_t>(r) * 1000;
            rec.end_utc_s = rec.start_utc_s + 300;
            rec.duration_ms = 300'000;
            rec.local_day = 100 + static_cast<int>(r) / 4;
            rec.all = {100, 60 + static_cast<int>(r % 30)};
            rec.right = rec.all;
            for (int bar = 0; bar < 32; ++bar) {
                RunBar b;
                b.measure = bar + 1;
                b.graded = 4;
                rec.measures.push_back(b);
            }
            runs.push_back(sanitized(rec));
        }
        RunRecord now_run = runs.back();
        now_run.start_utc_s += 1000;
        now_run.end_utc_s += 1000;
        now_run.all = {100, 99};
        double best = 1e9;
        bool previous = false;
        for (int round = 0; round < 9; ++round) {
            const auto begin = Clock::now();
            const AttemptChange change = compare_with_last_attempt(runs, now_run);
            best = std::min(best, ms_since(begin));
            previous = change.has_previous;
        }
        std::printf("  compare_with_last_attempt, %6zu runs   best %8.4f ms  (previous %d)\n", n,
                    best, previous ? 1 : 0);
    }

    return 0;
}

} // namespace etude::app
