// SPDX-License-Identifier: GPL-3.0-or-later
#include "bench.hpp"

#include "alloc_probe.hpp"
#include "combo_demo.hpp"
#include "frame_log.hpp"
#include "gfx.hpp"
#include "library_bench.hpp"
#include "scene.hpp"
#include "sections_controller.hpp"
#include "sheet/sheet_view.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"
#include "song_transport.hpp"
#include "etude/combo_display.hpp"
#include "etude/effects_governor.hpp"
#include "etude/game_feel.hpp"
#include "etude/key_state.hpp"
#include "etude/keyboard.hpp"
#include "etude/keyboard_view.hpp"
#include "etude/note_style.hpp"
#include "etude/note_trail.hpp"
#include "etude/screen_layout.hpp"
#include "etude/sheet_view.hpp"
#include "ui.hpp"
#include "ui/tokens.hpp"
#include "window_icon.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace etude::app {

namespace {

constexpr std::int64_t kSecond = 1'000'000'000;
constexpr double kWarmupSeconds = 0.5; // after the screen has filled (the visible seconds)
constexpr double kFirstBeat = 0.2;     // seconds
constexpr double kAvgNoteSeconds = 0.55;
constexpr std::size_t kBenchLibrarySongs = 5000; // songs on the library screen (--panel library)
constexpr std::size_t kBenchSectionSongs = 12;   // of those, the first ones given sections
// A chord of 3 or 4, a melody note, a bass note every 4th beat: about 5 notes a beat, measured
// down to 4.1 so the count on screen lands near the asked-for number.
constexpr double kNotesPerBeat = 4.1;

// A fixed, made-up piece: chords on every beat, a melody note between them, a held bass note every
// fourth beat and a long held chord every third. Sized so about `notes` blocks are on screen.
struct Schedule {
    std::vector<MidiEvent> events; // sorted by time, relative to the start
    double beat = 0.5;
};

class Rng {
  public:
    double next() {
        state_ = state_ * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state_ >> 11) / 9007199254740992.0;
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() * (hi - lo + 1)); }

  private:
    std::uint64_t state_ = 0x243f6a8885a308d3ull;
};

Schedule make_schedule(int notes, double visible_seconds, double total_seconds, KeyRange range) {
    Schedule s;
    const double rate = std::max(1.0, notes / (visible_seconds + kAvgNoteSeconds));
    s.beat = kNotesPerBeat / rate;
    Rng rng;
    const auto add = [&](int note, double start, double length, int velocity) {
        note = std::clamp(note, range.lowest, range.highest);
        const auto on = static_cast<std::int64_t>(start * 1e9);
        const auto off = static_cast<std::int64_t>((start + std::max(0.06, length)) * 1e9);
        s.events.push_back({on, MidiEventType::NoteOn, 0, static_cast<std::uint8_t>(note),
                            static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)), false});
        s.events.push_back(
            {off, MidiEventType::NoteOff, 0, static_cast<std::uint8_t>(note), 0, false});
    };
    const int mid = (range.lowest + range.highest) / 2;
    int beat_index = 0;
    for (double t = kFirstBeat; t < total_seconds; t += s.beat, ++beat_index) {
        // Chord: a triad with the octave on top, major or minor, sometimes held long.
        const int root = std::clamp(mid - 12 + rng.range(-5, 7), range.lowest, range.highest - 12);
        const int third = rng.next() < 0.5 ? 4 : 3;
        const bool long_hold = beat_index % 3 == 0;
        const double length = long_hold ? s.beat * (2.5 + rng.next()) : s.beat * (0.5 + rng.next());
        const int velocity = rng.range(60, 120);
        add(root, t, length, velocity);
        add(root + third, t, length, velocity);
        add(root + 7, t, length, velocity);
        if (rng.next() < 0.75) {
            add(root + 12, t, length, velocity);
        }
        // Melody between the beats, above the chord.
        add(mid + rng.range(4, 26), t + 0.5 * s.beat, s.beat * (0.3 + 0.5 * rng.next()),
            rng.range(50, 127));
        if (beat_index % 4 == 0) {
            add(range.lowest + rng.range(4, 20), t, s.beat * 3.5, rng.range(70, 110));
        }
    }
    std::stable_sort(s.events.begin(), s.events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.time_ns != b.time_ns) {
            return a.time_ns < b.time_ns;
        }
        return a.type == MidiEventType::NoteOff && b.type != MidiEventType::NoteOff;
    });
    return s;
}

// A shot depends on its arguments only: its clock starts at a fixed time (the UI's animations read
// the absolute time) and the UI reads no wall clock (`ui::exact_shot`, set in `run_bench_mode`).
constexpr std::int64_t kShotEpochNs = 10'000'000'000;

std::int64_t clock_origin(bool shot) {
    return shot ? kShotEpochNs : static_cast<std::int64_t>(SDL_GetTicksNS());
}

// The screenshot patterns: the last note-on lands on this 60 Hz frame time.
constexpr double kPatternEndSeconds = 2.0;

// A fast run up the C major scale from middle C (ten notes, 70 ms apart, short), or a C major
// chord with the octave on top, held; the last note-on at kPatternEndSeconds.
Schedule make_pattern(const std::string& pattern) {
    Schedule s;
    const auto add = [&](int note, double start, double length, int velocity) {
        const auto on = static_cast<std::int64_t>(start * 1e9);
        const auto off = static_cast<std::int64_t>((start + length) * 1e9);
        s.events.push_back({on, MidiEventType::NoteOn, 0, static_cast<std::uint8_t>(note),
                            static_cast<std::uint8_t>(velocity), false});
        s.events.push_back(
            {off, MidiEventType::NoteOff, 0, static_cast<std::uint8_t>(note), 0, false});
    };
    if (pattern == "run") {
        static constexpr int kScale[] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76};
        const double step = 0.07;
        double t = kPatternEndSeconds - step * static_cast<double>(std::size(kScale) - 1);
        for (const int note : kScale) {
            add(note, t, 0.12, 96);
            t += step;
        }
    } else if (pattern.rfind("song-", 0) == 0) {
        // Falling song notes only (song_pattern): nothing is pressed.
    } else {
        for (const int note : {60, 64, 67, 72}) {
            add(note, kPatternEndSeconds, 0.5, 100);
        }
    }
    std::stable_sort(s.events.begin(), s.events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.time_ns != b.time_ns) {
            return a.time_ns < b.time_ns;
        }
        return a.type == MidiEventType::NoteOff && b.type != MidiEventType::NoteOff;
    });
    return s;
}

// The song patterns: notes falling toward the keys, none pressed, each hand in its own colour (left
// below middle C). song-chromatic: every key from G3 to C5, one after another; song-chord: a chord
// in each hand on black and white keys, then another; song-run: a fast run in both hands. The
// note's start and length are in seconds before it reaches the hit line.
std::vector<SongBar> song_pattern(const std::string& pattern, float visible_seconds) {
    std::vector<SongBar> bars;
    const auto add = [&](int key, float start, float length) {
        SongBar b;
        b.key = static_cast<std::uint8_t>(key);
        b.velocity = 96;
        b.hand = key < 60 ? Hand::Left : Hand::Right;
        b.bottom = start / visible_seconds;
        b.top = (start + length) / visible_seconds;
        bars.push_back(b);
    };
    if (pattern == "song-chromatic") {
        for (int key = 55; key <= 72; ++key) {
            add(key, 0.12f + 0.13f * static_cast<float>(key - 55), 0.12f);
        }
    } else if (pattern == "song-chord") {
        for (const int key : {37, 44, 61, 65, 68, 73}) {
            add(key, 0.2f, 1.0f);
        }
        for (const int key : {41, 48, 60, 65, 69, 72}) {
            add(key, 1.45f, 0.8f);
        }
    } else if (pattern == "song-run") {
        static constexpr int kRight[] = {66, 68, 70, 71, 73, 75, 76, 78, 80, 82, 83, 85};
        static constexpr int kLeft[] = {42, 44, 46, 47, 49, 51, 52, 54, 56, 58, 59, 61};
        for (std::size_t i = 0; i < std::size(kRight); ++i) {
            const float t = 0.25f + 0.1f * static_cast<float>(i);
            add(kRight[i], t, 0.09f);
            add(kLeft[i] - 12, t, 0.09f);
        }
    }
    return bars;
}

double ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }

struct Stats {
    std::size_t frames = 0;
    double min = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double max = 0.0;
};

Stats summarise(std::vector<std::int64_t>& samples) {
    Stats st;
    if (samples.empty()) {
        return st;
    }
    std::sort(samples.begin(), samples.end());
    st.frames = samples.size();
    st.min = ms(samples.front());
    st.max = ms(samples.back());
    st.median = ms(samples[samples.size() / 2]);
    st.p95 = ms(samples[std::min(samples.size() - 1, samples.size() * 95 / 100)]);
    return st;
}

// One style's run: draws frames until `seconds` of measurement (after warm-up) have passed, or,
// with a screenshot path, steps a 60 Hz clock to `at` and saves that frame.
struct RunResult {
    Stats stats;
    Stats ui; // the UI's draw call alone, CPU
    bool has_ui = false;
    // Heap allocations per frame after the warm-up: the UI's draw call, and the whole frame.
    double ui_allocs_mean = 0.0;
    std::uint64_t ui_allocs_max = 0;
    double frame_allocs_mean = 0.0;
    std::uint64_t frame_allocs_max = 0;
    double system_allocs_mean = 0.0; // the same frames, made by other libraries (the driver)
    bool counted_allocs = false;
    std::int64_t stalls = 0;
    std::int64_t frames_logged = 0;
    double worst_ms = 0.0;
    EffectsLevel lowest = EffectsLevel::Full;
    EffectsLevel last = EffectsLevel::Full;
    int notes_on_screen = 0;
    double sheet_first_ms = 0.0; // --view: from the song's first job to its first picture
    bool ok = true;
};

// The made-up library on the screen, its first songs given sections so the details sheet shows
// them.
void show_bench_library(std::optional<BenchLibrary>& songs, Ui& ui, UiModel& model) {
    songs.emplace(kBenchLibrarySongs);
    ui.set_library(&*songs);
    for (std::size_t i = 0; i < kBenchSectionSongs; ++i) {
        SongSections marked;
        marked.song_hash = BenchLibrary::hash_of(i);
        double from_s = 0.0;
        for (const char* name : {"Intro", "Theme", "Variation", "Coda"}) {
            Section sec;
            sec.id = marked.next_id++;
            sec.name = name;
            sec.custom_name = true;
            sec.start_s = from_s;
            from_s += 12.0;
            sec.end_s = from_s;
            sec.snapped_to_bars = true;
            sec.first_measure = static_cast<int>(sec.start_s / 3.0) + 1;
            sec.last_measure = static_cast<int>(sec.end_s / 3.0);
            marked.sections.push_back(std::move(sec));
        }
        model.sections.put(std::move(marked));
    }
}

RunResult run_one(SDL_Renderer* renderer, SDL_Texture* target, const BenchOptions& o,
                  const LookSettings& look) {
    RunResult result;
    const KeyboardLayout keyboard(kRange88);
    Scene scene(renderer, look);
    scene.set_pinned_bar({o.bar_pinned, o.bar_style, !o.bar_song.empty()});
    const std::int64_t span_ns = scene.trail_span_ns();
    const bool demo = !o.combo_demo.empty();
    const ComboTimeline timeline(demo ? o.combo_demo : "perf");
    if (demo && !timeline.valid()) {
        std::fprintf(stderr, "bench: unknown --combo-demo %s (appear, pulse, break)\n",
                     o.combo_demo.c_str());
        result.ok = false;
        return result;
    }
    const bool shot = !o.screenshot.empty() || demo;
    const double warmup = look.visible_seconds + kWarmupSeconds;
    // A demo steps the screen full of notes first, then the timeline's lead, then records.
    const double demo_from = warmup + timeline.lead_s();
    const double demo_end = demo_from + timeline.record_s();
    constexpr std::int64_t kDemoHz = 50;
    int demo_frames_saved = 0;
    int combo_before = 0;
    const double total = (shot ? (demo ? demo_end : o.at_seconds) : warmup + o.seconds) + 1.0;
    const bool pattern = shot && !o.shot_pattern.empty();
    const Schedule schedule =
        pattern ? make_pattern(o.shot_pattern)
                : make_schedule(o.notes, look.visible_seconds, total, keyboard.range());
    // A screenshot lands just after a chord's note-on, while its effects are in the air; a
    // pattern's `shot_after_ms` after its last note-on. The last frame before `at` is saved.
    const double at =
        demo ? demo_end
        : pattern ? kPatternEndSeconds + o.shot_after_ms / 1000.0 + 0.5 / 60.0
                : std::floor((o.at_seconds - kFirstBeat) / schedule.beat) * schedule.beat +
                      kFirstBeat + 0.12;

    std::vector<SongBar> song_bars =
        pattern ? song_pattern(o.shot_pattern, look.visible_seconds) : std::vector<SongBar>{};
    // --song-bars: every scheduled note as a falling song block, over a 4/4 grid at its beat.
    struct Falling {
        int key;
        double start_s;
        double end_s;
    };
    std::vector<Falling> falling_notes;
    Song grid;
    if (o.song_bars && !pattern) {
        std::array<double, 128> on_at{};
        on_at.fill(-1.0);
        for (const MidiEvent& e : schedule.events) {
            const double t = static_cast<double>(e.time_ns) / 1e9;
            const int key = e.data1 & 127;
            if (e.type == MidiEventType::NoteOn && e.data2 > 0) {
                on_at[static_cast<std::size_t>(key)] = t;
            } else if (on_at[static_cast<std::size_t>(key)] >= 0.0) {
                falling_notes.push_back({key, on_at[static_cast<std::size_t>(key)], t});
                on_at[static_cast<std::size_t>(key)] = -1.0;
            }
        }
        grid.tempo.changes.front().us_per_quarter =
            static_cast<std::uint32_t>(std::lround(schedule.beat * 1e6));
        const std::int64_t bar = 4LL * grid.tempo.ticks_per_quarter;
        for (std::int64_t tick = 0; grid.tempo.seconds_at(tick) < total + look.visible_seconds;
             tick += bar) {
            Measure m;
            m.number = static_cast<int>(grid.measures.size()) + 1;
            m.start_tick = tick;
            m.start_s = grid.tempo.seconds_at(tick);
            grid.measures.push_back(m);
        }
        song_bars.reserve(falling_notes.size());
    }
    KeyState keys;
    NoteTrail trail;
    // The key size of the look moves the view to follow the played notes, as in the app.
    KeyViewFollower follower;
    std::int64_t view_ns = 0;
    int view_w = 0;
    std::optional<KeyboardLayout> layout(keyboard.range());
    follower.snap(keyboard.range(), keyboard.range());
    KeyViewFrame view = follower.frame();
    std::vector<TrailBar> bars;
    bars.reserve(4096);
    // Flow Mode's grade words: every press gets one, in turn, and it fades as in a song.
    struct Graded {
        std::uint8_t key;
        NoteGrade grade;
        std::int64_t at_ns;
    };
    std::vector<Graded> graded;
    std::vector<GradePopup> popups;
    std::size_t grade_turn = 0;
    std::array<std::int64_t, 128> last_press{};
    last_press.fill(kNeverPressed);
    std::array<std::uint8_t, 128> last_velocity{};
    std::vector<std::int64_t> samples;
    samples.reserve(65536);
    EffectsGovernor governor({.shader_level = scene.has_shader_effect()});
    const std::string status = std::string("Bench: ") +
                               std::string(note_style_name(look.note_style)) + ", " +
                               std::to_string(o.notes) + " notes";
    char level_line[64] = "";
    // The UI as the app draws it while playing: the top row (status text and gear), the bar
    // hidden. No song, so no transport.
    UiModel model;
    model.look = look;
    model.app.bar_style = o.bar_style;
    model.app.bar_pinned = o.bar_pinned;
    model.app.library_view = o.library_view;
    if (o.library_my_groups) {
        model.app.library.group = BrowseGroup::MyGroups;
    }
    SongController songs;
    SongTransport song(songs);
    ui::NullInputPort mic_port;
    SectionLibrary no_sections;
    SectionsController sections(songs, no_sections);
    Ui ui(gfx::window_of(renderer), renderer);
    const bool bar = !o.bar_song.empty();
    if (bar) {
        SongFileLoad loaded = load_song_file(o.bar_song);
        if (!loaded.ok) {
            std::fprintf(stderr, "bench: %s: %s\n", o.bar_song.c_str(), loaded.error.c_str());
            result.ok = false;
            return result;
        }
        // Sheet music reads the score again from where it was opened.
        loaded.song.absolute_path = absolute_path_text(o.bar_song);
        songs.load(std::move(loaded.song), clock_origin(shot));
        if (o.panel) {
            // The section band's pages (the lane, the practice list) draw the song's sections: cut
            // it in four.
            const auto cut_at = clock_origin(shot);
            for (const double t : {20.0, 45.0, 70.0}) {
                song.seek(t, cut_at);
                sections.update(cut_at);
                sections.add_marker_at_playhead(cut_at);
            }
            song.seek(0.0, cut_at);
        }
    }
    // Sheet music over the note area (--view), with the song loaded by --bar.
    std::unique_ptr<SheetView> sheet;
    if (bar && o.view != ViewMode::Falling) {
        sheet = std::make_unique<SheetView>(renderer);
    }
    SheetPlan sheet_plan;
    double measure_from = warmup; // later while the sheet music has no picture yet
    bool sheet_ready = sheet == nullptr;
    std::vector<std::int64_t> ui_samples;
    ui_samples.reserve(65536);
    std::vector<std::uint64_t> ui_allocs;
    ui_allocs.reserve(65536);
    std::vector<std::uint64_t> frame_allocs;
    frame_allocs.reserve(65536);
    std::vector<std::uint64_t> system_allocs;
    system_allocs.reserve(65536);
    FrameLog frame_log;
    set_active_frame_log(&frame_log);
    struct LogOff {
        ~LogOff() { set_active_frame_log(nullptr); }
    } log_off;
    // The library screen draws a made-up library of a few thousand songs.
    std::optional<BenchLibrary> songs_shown;
    if (o.panel == PanelTab::Library) {
        show_bench_library(songs_shown, ui, model);
    }
    if (o.panel) {
        ui.open_panel(*o.panel);
    }
    const auto draw_ui = [&](std::int64_t now_ns, std::string_view note) {
        int out_w = 0;
        int out_h = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h);
        UiFrameInfo info;
        info.width_px = static_cast<float>(out_w);
        info.height_px = static_cast<float>(out_h);
        info.px_per_mm = kDefaultPxPerMm;
        info.layout =
            compute_layout(info.width_px, info.height_px, info.px_per_mm, model.look,
                           {model.app.bar_pinned, model.app.bar_style, bar, ui.lane_wanted()});
        info.combo = scene.combo_rect();
        scene.set_combo_clear_y(ui.hanging_band_bottom() * info.pixel_density);
        if (o.bar_pinned && bar && !demo) {
            // A toast on screen all the time: a new one just before the last one runs out.
            static std::int64_t toast_frame = 0;
            if (toast_frame++ % 60 == 0) {
                ui.show_toast(toast_frame % 120 == 1 ? "Song sound off" : "Look: Night");
            }
        }
        info.now_ns = now_ns;
        info.playing = true;
        info.status_label = status;
        info.status_parts.add(note, etude::app::StatusRank::Plan, 0);
        song.update(now_ns);
        sections.update(now_ns);
        if (bar) {
            // A mouse resting on the top row keeps the bar shown.
            SDL_Event move{};
            move.type = SDL_EVENT_MOUSE_MOTION;
            move.motion.windowID = SDL_GetWindowID(gfx::window_of(renderer));
            move.motion.x = static_cast<float>(out_w) * 0.4f;
            move.motion.y = 12.0f;
            ui.process_event(move, now_ns);
        }
        const auto ui_start = static_cast<std::int64_t>(SDL_GetTicksNS());
        const std::uint64_t allocs_before = allocations_counted();
        ui.draw(model, song, sections, mic_port, info);
        ui_allocs.push_back(allocations_counted() - allocs_before);
        ui_samples.push_back(static_cast<std::int64_t>(SDL_GetTicksNS()) - ui_start);
    };

    const std::int64_t start = clock_origin(shot);
    std::size_t next_event = 0;
    std::int64_t frame = 0;
    const SDL_Rect one{0, 0, 1, 1};
    for (;;) {
        // Let the GPU finish everything queued so far (the last frame and its present, which waits
        // for the display) before the clock starts.
        if (target != nullptr) {
            gfx::set_target(renderer, target);
        }
        SDL_DestroySurface(gfx::read_pixels(renderer, &one));
        const std::int64_t frame_start = static_cast<std::int64_t>(SDL_GetTicksNS());
        frame_log.start(frame_clock_ns());
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                result.ok = false;
                return result;
            }
        }
        frame_log.mark(FramePhase::Events, frame_clock_ns());
        const std::int64_t real_now = static_cast<std::int64_t>(SDL_GetTicksNS());
        const std::int64_t now =
            demo ? start + frame * kSecond / kDemoHz : shot ? start + frame * kSecond / 60 : real_now;
        const double elapsed = static_cast<double>(now - start) / 1e9;
        if (shot ? elapsed >= at : sheet_ready && elapsed >= measure_from + o.seconds) {
            break;
        }
        if (!sheet_ready && sheet != nullptr && sheet->stats().textures >= 2) {
            sheet_ready = true;
            measure_from = std::max(warmup, elapsed);
            result.sheet_first_ms = sheet->stats().first_picture_ms;
        } else if (!sheet_ready && elapsed > 30.0) {
            std::fprintf(stderr, "bench: the sheet music had no picture after 30 s (%s)\n",
                         std::string(sheet->offer().reason).c_str());
            result.ok = false;
            return result;
        }

        while (next_event < schedule.events.size() &&
               start + schedule.events[next_event].time_ns <= now) {
            MidiEvent ev = schedule.events[next_event++];
            ev.time_ns += start;
            const KeyTransition t = keys.apply(ev);
            trail.apply(t, ev.time_ns);
            if (t.type == KeyTransitionType::Pressed) {
                last_press[t.note] = ev.time_ns;
                last_velocity[t.note] = t.velocity;
                static constexpr NoteGrade kTurns[] = {NoteGrade::Perfect, NoteGrade::Good,
                                                       NoteGrade::Early, NoteGrade::Late,
                                                       NoteGrade::Missed};
                graded.push_back({t.note, kTurns[grade_turn++ % std::size(kTurns)], ev.time_ns});
            }
        }
        popups.clear();
        std::erase_if(graded, [&](const Graded& g) { return now - g.at_ns >= kGradeShowNs; });
        for (const Graded& g : graded) {
            popups.push_back(
                {g.key, g.grade,
                 static_cast<float>(now - g.at_ns) / static_cast<float>(kGradeShowNs)});
        }
        trail.prune(now, span_ns);
        trail.visible(now, span_ns, bars);
        {
            int out_w = 0;
            int out_h = 0;
            SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h);
            KeyViewParams vp;
            vp.keyboard = keyboard.range();
            vp.width_px = static_cast<float>(out_w);
            vp.px_per_mm = kDefaultPxPerMm;
            vp.size = look.key_size;
            const KeyViewTarget wanted = compute_key_view(vp);
            if (!follower.has_position() || out_w != view_w) {
                follower.snap(keyboard.range(), wanted.range);
            } else {
                follower.advance(keyboard.range(), wanted.range,
                                 static_cast<double>(now - view_ns) / 1e9);
            }
            view_w = out_w;
            view_ns = now;
            view = follower.frame();
            if (!layout || layout->range().lowest != view.layout_range.lowest ||
                layout->range().highest != view.layout_range.highest ||
                layout->blocks() != look.block_layout) {
                layout.emplace(view.layout_range, look.block_layout);
            }
        }
        result.notes_on_screen = std::max(result.notes_on_screen, static_cast<int>(bars.size()));

        if (target != nullptr) {
            gfx::set_target(renderer, target);
        }
        FrameInput input{*layout, view, keys, bars, last_press, last_velocity, now, false, nullptr};
        input.grades = popups;
        if (!falling_notes.empty()) {
            // The notes due in the next visible seconds, falling to the hit line.
            song_bars.clear();
            const double ahead = look.visible_seconds;
            for (const Falling& f : falling_notes) {
                const double bottom = (f.start_s - elapsed) / ahead;
                const double top = (f.end_s - elapsed) / ahead;
                if (top > 0.0 && bottom < 1.0) {
                    SongBar b;
                    b.key = static_cast<std::uint8_t>(f.key);
                    b.velocity = 90;
                    b.hand = f.key < 60 ? Hand::Left : Hand::Right;
                    b.bottom = static_cast<float>(bottom);
                    b.top = static_cast<float>(top);
                    song_bars.push_back(b);
                }
            }
            input.song = &grid;
            input.song_now_s = elapsed;
        }
        input.song_bars = song_bars;
        if (demo || o.combo) {
            // The timeline starts when the screen is full in a demo; a bench has its own clock.
            const int combo = timeline.combo_at(demo ? elapsed - warmup : elapsed);
            input.combo = combo;
            input.combo_milestone =
                combo_event(combo_before, combo).kind == ComboEventKind::Milestone;
            combo_before = combo;
        }
        // The key label is part of the frame, so the numbers include it.
        static const KeyChange kBenchKey = key_from_signature(0, 1, KeyMode::Major);
        input.key_label.key = &kBenchKey;
        // The scene and the UI, not the bench's own toggling of the song between them (that is a
        // change of value, which rebuilds on purpose).
        const std::uint64_t scene_before = allocations_counted();
        const std::uint64_t system_before = system_allocations_counted();
        frame_log.mark(FramePhase::Update, frame_clock_ns());
        if (sheet != nullptr) {
            int out_w = 0;
            int out_h = 0;
            SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h);
            const ScreenLayout lay = compute_layout(
                static_cast<float>(out_w), static_cast<float>(out_h), kDefaultPxPerMm, look,
                {o.bar_pinned, o.bar_style, true, false});
            sheet->sync_song(songs, now);
            sheet->set_helps(look, kDefaultPxPerMm);
            const RectF room = score_area(lay);
            const float corner = ui::tok::kHit * ui_scale(static_cast<float>(out_w),
                                                           static_cast<float>(out_h), kDefaultPxPerMm);
            const SheetPlan engrave = plan_sheet(room, o.view, true, sheet->two_staves(),
                                                 kDefaultPxPerMm, corner, sheet->name_room(kDefaultPxPerMm));
            sheet_plan = plan_sheet(room, o.view, true, sheet->two_staves(), kDefaultPxPerMm, corner,
                                    sheet->name_room(kDefaultPxPerMm));
            sheet->update(songs, engrave, sheet_plan, now, look.reduced_effects);
            RectF falling = lay.note_area;
            const float top = std::max(falling.y, sheet_plan.notes.y);
            falling.h = std::max(0.0f, falling.bottom() - top);
            falling.y = top;
            scene.set_falling_area(falling, false);
        }
        Scene::begin_frame(!demo); // a demo's frames are stills: every name made
        scene.draw(input);
        if (sheet != nullptr) {
            sheet->draw(sheet_plan);
        }
        frame_log.mark(FramePhase::Scene, frame_clock_ns());
        const std::uint64_t scene_allocs = allocations_counted() - scene_before;
        const std::uint64_t scene_system = system_allocations_counted() - system_before;
        if (bar && !demo && frame % 12 == 0) {
            // Keep the transport's icons changing: play/pause, and the left hand on and off.
            song.toggle_play(now);
            if (frame % 24 == 0) {
                const bool left = song.view().left_hand;
                song.set_hands(!left, true, now);
            }
        }
        const std::uint64_t ui_before = allocations_counted();
        const std::uint64_t ui_system_before = system_allocations_counted();
        draw_ui(now, level_line);
        frame_allocs.push_back(scene_allocs + allocations_counted() - ui_before);
        system_allocs.push_back(scene_system + system_allocations_counted() - ui_system_before);
        frame_log.mark(FramePhase::Ui, frame_clock_ns());
        if (demo && elapsed >= demo_from) {
            SDL_Surface* pixels = gfx::read_pixels(renderer, nullptr);
            char name[32];
            std::snprintf(name, sizeof name, "/f%04d.png", demo_frames_saved);
            if (pixels == nullptr || !SDL_SavePNG(pixels, (o.combo_out + name).c_str())) {
                std::fprintf(stderr, "combo demo: no frame saved: %s\n", SDL_GetError());
                result.ok = false;
            }
            SDL_DestroySurface(pixels);
            ++demo_frames_saved;
        }
        // Reading one pixel back waits for the GPU to finish the frame, so the time below is the
        // frame's whole cost on CPU and GPU, without the wait for the display's refresh (macOS
        // keeps vsync on whatever the renderer asks).
        SDL_DestroySurface(gfx::read_pixels(renderer, &one));
        const std::int64_t frame_ns = static_cast<std::int64_t>(SDL_GetTicksNS()) - frame_start;
        if (!o.headless) {
            if (target != nullptr) {
                gfx::set_target(renderer, nullptr);
                gfx::clear(renderer, 0, 0, 0, 255);
                gfx::draw_texture(renderer, target, nullptr, nullptr);
            }
            gfx::present(renderer);
        }
        ++frame;
        frame_log.mark(FramePhase::Present, frame_clock_ns());
        frame_log.finish(frame_clock_ns(), frame);

        if (!shot && elapsed >= measure_from && sheet_ready) {
            samples.push_back(frame_ns);
        } else if (!ui_samples.empty()) {
            ui_samples.pop_back(); // warm-up
            ui_allocs.pop_back();
            frame_allocs.pop_back();
            system_allocs.pop_back();
        }
        if (o.degrade && !shot) {
            const EffectsLevel before = scene.effects_level();
            scene.set_effects_level(governor.add_frame(frame_ns));
            if (scene.effects_level() != before) {
                std::snprintf(level_line, sizeof level_line, "effects: %s",
                              std::string(effects_level_name(scene.effects_level())).c_str());
            }
        }
    }

    if (demo) {
        std::printf("combo demo %s: %d frames at %lld Hz in %s\n", o.combo_demo.c_str(),
                    demo_frames_saved, static_cast<long long>(kDemoHz), o.combo_out.c_str());
        return result;
    }
    if (shot) {
        // Draw the frame once more into the target and read it back.
        if (target != nullptr) {
            gfx::set_target(renderer, target);
        }
        Scene::begin_frame(false); // a still: every name made
        FrameInput still{
            *layout, view,   keys, bars, last_press, last_velocity, start + frame * kSecond / 60,
            false,   nullptr};
        still.song_bars = song_bars;
        scene.draw(still);
        draw_ui(start + frame * kSecond / 60, "");
        SDL_Surface* pixels = gfx::read_pixels(renderer, nullptr);
        if (target != nullptr) {
            gfx::set_target(renderer, nullptr);
        }
        if (pixels == nullptr || !SDL_SavePNG(pixels, o.screenshot.c_str())) {
            std::fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
            result.ok = false;
        } else {
            std::printf("saved %s (%dx%d, %s, t=%.2f s, %d notes on screen)\n",
                        o.screenshot.c_str(), pixels->w, pixels->h,
                        std::string(note_style_name(look.note_style)).c_str(), at,
                        static_cast<int>(bars.size() + song_bars.size()));
        }
        SDL_DestroySurface(pixels);
        return result;
    }

    result.stats = summarise(samples);
    result.ui = summarise(ui_samples);
    result.has_ui = bar || o.panel.has_value();
    result.counted_allocs = o.count_allocs;
    const auto mean_max = [](const std::vector<std::uint64_t>& counts, double& mean,
                             std::uint64_t& max) {
        std::uint64_t sum = 0;
        for (const std::uint64_t c : counts) {
            sum += c;
            max = std::max(max, c);
        }
        mean = counts.empty() ? 0.0 : static_cast<double>(sum) / static_cast<double>(counts.size());
    };
    mean_max(ui_allocs, result.ui_allocs_mean, result.ui_allocs_max);
    mean_max(frame_allocs, result.frame_allocs_mean, result.frame_allocs_max);
    std::uint64_t system_max = 0;
    mean_max(system_allocs, result.system_allocs_mean, system_max);
    result.stalls = frame_log.stalls();
    result.frames_logged = frame_log.frames();
    result.worst_ms = ms(frame_log.worst_ns());
    result.lowest = governor.lowest();
    result.last = governor.level();
    return result;
}

} // namespace

// The bench's window and renderer. Draw straight into an exact window when asked (the Pi at its
// panel size); otherwise into a target texture of the requested size, shown scaled in a small
// window. Headless: always into the target, in a hidden window, and nothing is presented.
static bool open_bench_window(const BenchOptions& o, SDL_Window** window, SDL_Renderer** renderer) {
    const int ww = o.exact_window ? o.width : 960;
    const int wh = o.exact_window ? o.height : std::max(1, 960 * o.height / std::max(1, o.width));
    return gfx::create_window_and_renderer(
        "Etude bench", ww, wh, o.headless ? SDL_WINDOW_HIDDEN : 0, o.renderer, window, renderer);
}

int run_bench(const BenchOptions& o, const LookSettings& base) {
    if (o.count_allocs) {
        count_imgui_allocations();
        count_allocations(true);
    }
    if (o.headless) {
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1"); // no Dock icon, no focus
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!open_bench_window(o, &window, &renderer)) {
        std::fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    set_window_icon(window);
    gfx::set_vsync(renderer, 0); // measure what a frame costs, not the refresh rate
    int out_w = 0;
    int out_h = 0;
    gfx::output_size(renderer, &out_w, &out_h);
    SDL_Texture* target = nullptr;
    if (o.headless || out_w != o.width || out_h != o.height) {
        target = gfx::create_texture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                     o.width, o.height);
        if (target == nullptr) {
            std::fprintf(stderr, "render target failed: %s\n", SDL_GetError());
            gfx::destroy_renderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        gfx::set_texture_linear(target);
    }

    std::vector<NoteStyle> styles = o.styles;
    if (styles.empty()) {
        styles.push_back(base.note_style);
    }
    if (o.screenshot.empty()) {
        std::printf("bench: %dx%d%s, ~%d notes on screen, %.1f s per style, renderer %s, "
                    "auto-degrade %s\n",
                    o.width, o.height, target != nullptr ? " (offscreen target)" : "", o.notes,
                    o.seconds, gfx::renderer_name(renderer), o.degrade ? "on" : "off");
        std::printf("%-8s %7s %8s %8s %8s %8s %7s %6s  %s\n", "style", "frames", "min ms", "median",
                    "p95", "max", "fps cap", "notes", "effects level reached (final)");
    }
    int code = 0;
    if (o.shot_pattern == "all") {
        // Every hit effect after a run and after a chord, in this one window: PREFIX-EFFECT-P.png.
        for (const HitEffect effect : kAllHitEffects) {
            for (const char* pattern : {"run", "chord"}) {
                BenchOptions one = o;
                one.shot_pattern = pattern;
                std::string name(hit_effect_label(effect));
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                one.screenshot = o.screenshot + "-" + name + "-" + pattern + ".png";
                LookSettings look = base;
                look.hit_effect = effect;
                if (!run_one(renderer, target, one, look).ok) {
                    code = 1;
                }
            }
        }
        styles.clear();
    }
    for (NoteStyle style : styles) {
        LookSettings look = base;
        look.note_style = style;
        const RunResult r = run_one(renderer, target, o, look);
        if (!r.ok) {
            code = 1;
            break;
        }
        if (o.screenshot.empty()) {
            std::printf("%-8s %7zu %8.2f %8.2f %8.2f %8.2f %7.0f %6d  %s (%s)\n",
                        std::string(note_style_name(look.note_style)).c_str(), r.stats.frames,
                        r.stats.min, r.stats.median, r.stats.p95, r.stats.max,
                        r.stats.median > 0.0 ? 1000.0 / r.stats.median : 0.0, r.notes_on_screen,
                        std::string(effects_level_name(r.lowest)).c_str(),
                        std::string(effects_level_name(r.last)).c_str());
            if (r.sheet_first_ms > 0.0) {
                std::printf("sheet music (%s): first picture %.1f ms after the song's job\n",
                            view_mode_label(o.view), r.sheet_first_ms);
            }
            if (r.has_ui) {
                std::printf("%-8s %7zu %8.3f %8.3f %8.3f %8.3f  (UI draw alone, CPU)\n", "ui",
                            r.ui.frames, r.ui.min, r.ui.median, r.ui.p95, r.ui.max);
                if (r.counted_allocs) {
                    std::printf("allocs per frame after warm-up, ours: UI draw mean %.2f max %llu, "
                                "scene + UI mean %.2f max %llu; made by system libraries: mean "
                                "%.2f\n",
                                r.ui_allocs_mean, static_cast<unsigned long long>(r.ui_allocs_max),
                                r.frame_allocs_mean,
                                static_cast<unsigned long long>(r.frame_allocs_max),
                                r.system_allocs_mean);
                }
            }
            if (r.stalls > 0) {
                std::printf("frame log: %lld frames, %lld over %.0f ms, worst %.1f ms\n",
                            static_cast<long long>(r.frames_logged),
                            static_cast<long long>(r.stalls), ms(kFrameStallNs), r.worst_ms);
            }
        }
    }

    if (target != nullptr) {
        gfx::destroy_texture(target);
    }
    gfx::destroy_renderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}

} // namespace etude::app
