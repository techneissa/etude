// SPDX-License-Identifier: GPL-3.0-or-later
#include "alloc_count.hpp"
#include "persistence.hpp"

#include <doctest/doctest.h>

#include "sample_looks.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>

using namespace etude;
using namespace etude::test;
using namespace etude::app;
using etude::app::LoadSource;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("etude-persist-test-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

UiModel changed_model() {
    UiModel m;
    m.look = sample_look(Sample::NeonNight);
    m.look.octave_numbering = OctaveNumbering::Yamaha;
    m.look.pitch_colors[3] = RGBA{1, 2, 3, 255};
    m.user_presets.save_slot(1, "Night owl", m.look);
    m.app.hit_window_ms = 120;
    m.app.latency_correction_ms = -35;
    m.session.keyboard = KeyboardChoice::Manual;
    m.session.keyboard_lowest = 28;
    m.session.keyboard_highest = 103;
    return m;
}

} // namespace

namespace etude::app {
// Hands a model to the writer at an exact moment (a friend of Persistence).
struct PersistenceProbe {
    static bool submit(Persistence& p, const UiModel& m) { return p.submit(m); }
};
} // namespace etude::app

namespace {

// Waits (a failure after five seconds, nothing is judged on how long) until the write that was
// handed over has finished and its outcome has not been taken.
bool finished_untaken(const Persistence& p) {
    for (int i = 0; i < 5000; ++i) {
        if (p.uncollected()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

TEST_CASE("a model survives the trip through the file text") {
    const UiModel m = changed_model();
    const std::string text = serialize_model(load_settings(""), m);
    CHECK(text.find("[look]") != std::string::npos);
    CHECK(text.find("[user_presets]") != std::string::npos);
    CHECK(text.find("[app]") != std::string::npos);
    CHECK(text.find("[session]") != std::string::npos);
    const UiModel back =
        model_from(load_settings(text, std::array<std::string_view, 2>{"look", "user_presets"}));
    CHECK(back == m);
}

TEST_CASE("sections and keys this build does not know survive a save") {
    const std::string original =
        "format = 1.0\n\n[from_the_future]\nweird = 1\n\n[app]\nhit_window_ms = 90\nnew_key = x\n";
    const std::array<std::string_view, 2> owned{"look", "user_presets"};
    LoadedSettings loaded = load_settings(original, owned);
    UiModel m = model_from(loaded);
    m.look.note_style = NoteStyle::Neon;
    const std::string text = serialize_model(loaded, m);
    CHECK(text.find("[from_the_future]\nweird = 1\n") != std::string::npos);
    CHECK(text.find("new_key = x") != std::string::npos);
    CHECK(model_from(load_settings(text, owned)).look.note_style == NoteStyle::Neon);
}

TEST_CASE("a settings.conf that names the removed colour-blind palette loads as Classic") {
    TempDir dir;
    {
        std::ofstream(dir.path / "settings.conf", std::ios::trunc)
            << "format = 1.0\n\n[look]\ncolor_scheme = by_pitch_class\n"
               "palette_by_pitch_class = colour_blind\npalette_by_hand = colour_blind\n\n"
               "[user_presets]\nslot_1_used = true\nslot_1_name = Old\nslot_1_palette_single = colour_blind\n";
    }
    Persistence p(dir.path);
    UiModel m;
    CHECK(p.load(m).empty()); // no message: the file is not damaged
    CHECK(m.look.color_scheme == ColorScheme::ByPitchClass);
    CHECK(m.look.palettes[static_cast<std::size_t>(ColorScheme::ByPitchClass)] == Palette::Classic);
    CHECK(m.look.palettes[static_cast<std::size_t>(ColorScheme::ByHand)] == Palette::Classic);
    LookSettings saved;
    REQUIRE(m.user_presets.load_slot(0, &saved));
    CHECK(saved.palettes[static_cast<std::size_t>(ColorScheme::Single)] == Palette::Classic);
}

TEST_CASE("the first start gives the defaults and says nothing") {
    TempDir dir;
    Persistence p(dir.path);
    UiModel m;
    CHECK(p.load(m).empty());
    CHECK(m == UiModel{});
}

TEST_CASE("a change is written only after the debounce, and survives a restart") {
    TempDir dir;
    UiModel m;
    {
        Persistence p(dir.path);
        p.load(m);
        m = changed_model();
        std::int64_t t = 1'000'000'000;
        CHECK(p.update(m, t).empty());
        CHECK_FALSE(fs::exists(p.store().main_path())); // not yet
        m.look.visible_seconds = 6.0f;                  // another change restarts the wait
        t += kSaveDebounceNs - 1;
        p.update(m, t);
        CHECK_FALSE(fs::exists(p.store().main_path()));
        t += kSaveDebounceNs;
        CHECK(p.update(m, t).empty());
        CHECK(p.wait(t).empty()); // the write is made on the saver's thread
        CHECK(fs::exists(p.store().main_path()));
    }
    Persistence again(dir.path);
    UiModel back;
    CHECK(again.load(back).empty());
    CHECK(back == m);
}

TEST_CASE("flush writes an unsaved change at once, and a clean model writes nothing") {
    TempDir dir;
    Persistence p(dir.path);
    UiModel m;
    p.load(m);
    CHECK(p.flush(m).empty());
    CHECK_FALSE(fs::exists(p.store().main_path()));
    m.look.c_markers = false;
    CHECK(p.flush(m).empty());
    UiModel back;
    Persistence again(dir.path);
    again.load(back);
    CHECK_FALSE(back.look.c_markers);
}

TEST_CASE("a startup override that is adopted is not written until something else changes") {
    TempDir dir;
    Persistence p(dir.path);
    UiModel m;
    p.load(m);
    m.look = sample_look(Sample::Glass);
    p.adopt(m);
    CHECK(p.update(m, 0).empty());
    CHECK(p.update(m, 10 * kSaveDebounceNs).empty());
    CHECK_FALSE(fs::exists(p.store().main_path()));
}

TEST_CASE("a damaged file falls back to the backup, then the defaults, with a message") {
    TempDir dir;
    UiModel version_a = changed_model();
    UiModel version_b = version_a;
    version_b.look.visible_seconds = 7.0f;
    {
        Persistence p(dir.path);
        UiModel m;
        p.load(m);
        p.flush(version_a);
        p.flush(version_b); // main holds B, the backup holds A
    }
    const fs::path main_file = dir.path / "settings.conf";
    {
        std::ofstream(main_file, std::ios::trunc) << "garbage\nmore garbage\n";
    }
    {
        Persistence p(dir.path);
        UiModel m;
        CHECK_FALSE(p.load(m).empty());
        CHECK(m == version_a);
    }
    {
        std::ofstream(main_file, std::ios::trunc) << "garbage\nmore garbage\n";
        std::ofstream(dir.path / "settings.conf.bak", std::ios::trunc) << "";
    }
    {
        Persistence p(dir.path);
        UiModel m;
        CHECK_FALSE(p.load(m).empty());
        CHECK(m == UiModel{});
    }
}

TEST_CASE("a file from a newer major version is read but never written over") {
    TempDir dir;
    const std::string newer = "format = 2.0\n\n[app]\nhit_window_ms = 100\n";
    {
        std::ofstream(dir.path / "settings.conf") << newer;
    }
    Persistence p(dir.path);
    UiModel m;
    CHECK_FALSE(p.load(m).empty());
    CHECK(m.app.hit_window_ms == 100);
    m.look.c_markers = false;
    CHECK(p.flush(m).empty());
    CHECK(read(dir.path / "settings.conf") == newer);
}

TEST_CASE("a save that fails is reported once and retried later") {
    TempDir dir;
    // The folder path is a file, so nothing can be written there.
    const fs::path blocked = dir.path / "blocked";
    {
        std::ofstream(blocked) << "x";
    }
    Persistence p(blocked);
    UiModel m;
    p.load(m);
    m.look.c_markers = false;
    std::int64_t t = 0;
    p.update(m, t);
    t += kSaveDebounceNs;
    CHECK(p.update(m, t).empty());       // handed to the saver
    CHECK_FALSE(p.wait(t).empty());      // it failed: reported
    t += kSaveDebounceNs;
    CHECK(p.update(m, t).empty());       // not again right away
    CHECK(p.wait(t).empty());
    t += kRetryDelayNs;
    CHECK(p.update(m, t).empty());       // the retry is made...
    CHECK(p.wait(t).empty());            // ...and fails quietly
}

TEST_CASE("a save is built and written off the calling thread, which allocates nothing for it") {
    TempDir dir;
    Persistence p(dir.path);
    UiModel m = changed_model();
    p.load(m);
    std::int64_t t = 1'000'000'000;
    // A first save sizes the buffers the hand-over reuses.
    m.look.visible_seconds = 5.0f;
    p.update(m, t);
    t += kSaveDebounceNs;
    p.update(m, t);
    REQUIRE(p.wait(t).empty());
    p.update(m, t);

    m.look.visible_seconds = 6.0f; // an edit like a slider's: no new entry, nothing to grow
    t += 10 * kSaveDebounceNs;
    p.update(m, t); // sees the change and starts the wait
    t += kSaveDebounceNs;
    etude::test::start_counting_allocations();
    p.update(m, t); // hands the model over
    const int handing = etude::test::stop_counting_allocations();
    CHECK(handing == 0);
    REQUIRE(p.wait(t).empty());
    etude::test::start_counting_allocations();
    p.update(m, t); // takes the outcome
    const int taking = etude::test::stop_counting_allocations();
    CHECK(taking == 0);
    UiModel back;
    Persistence again(dir.path);
    again.load(back);
    CHECK(back == m);
}

TEST_CASE("an update while a write is under way leaves it alone and loses no change") {
    TempDir dir;
    Persistence p(dir.path);
    UiModel m;
    p.load(m);
    m.look.visible_seconds = 5.0f;
    std::int64_t t = 1'000'000'000;
    p.update(m, t);
    t += kSaveDebounceNs;
    p.update(m, t); // the first write starts
    m.look.visible_seconds = 6.0f;
    // Frames go on while it is written: the new change waits for the writer and is saved after.
    for (int frame = 0; frame < 4; ++frame) {
        t += kSaveDebounceNs;
        p.update(m, t);
        p.wait(t);
    }
    UiModel back;
    Persistence again(dir.path);
    again.load(back);
    CHECK(back.look.visible_seconds == 6.0f);
}

TEST_CASE("a model offered while a finished write's outcome is untaken is refused, so nothing is lost") {
    TempDir dir;
    Persistence p(dir.path);
    UiModel m;
    p.load(m);
    UiModel a = m;
    a.look.visible_seconds = 5.0f;
    UiModel b = m;
    b.look.visible_seconds = 6.0f;
    REQUIRE(PersistenceProbe::submit(p, a));
    REQUIRE(finished_untaken(p));
    // The writer is idle, but a's outcome has not been taken: b must not replace a's model, or the
    // outcome would be read as b's.
    CHECK_FALSE(PersistenceProbe::submit(p, b));
    CHECK(p.wait().empty()); // a's outcome: saved
    std::int64_t t = 1'000'000'000;
    p.update(b, t);
    t += kSaveDebounceNs;
    p.update(b, t); // b is written now
    CHECK(p.wait(t).empty());
    UiModel back;
    Persistence again(dir.path);
    again.load(back);
    CHECK(back.look.visible_seconds == 6.0f);
}

TEST_CASE("a failed write that is untaken is not overwritten by the next model, and is retried") {
    TempDir dir;
    const fs::path blocked = dir.path / "blocked";
    {
        std::ofstream(blocked) << "x";
    }
    Persistence p(blocked);
    UiModel m;
    p.load(m);
    UiModel a = m;
    a.look.visible_seconds = 5.0f;
    UiModel b = m;
    b.look.visible_seconds = 6.0f;
    REQUIRE(PersistenceProbe::submit(p, a));
    REQUIRE(finished_untaken(p));
    CHECK_FALSE(PersistenceProbe::submit(p, b));
    const std::string message = p.wait(0);
    CHECK_FALSE(message.empty()); // the failure of a's write is reported, not dropped
    // And a is what is retried: an update that sees a later model takes the retry's timing.
    std::int64_t t = kRetryDelayNs;
    CHECK(p.update(a, t).empty());
    CHECK(p.wait(t).empty());
}

TEST_CASE("after restoring from the backup the main file is written again at once") {
    TempDir dir;
    const UiModel a = changed_model();
    UiModel b = a;
    b.look.visible_seconds = 7.0f;
    {
        Persistence p(dir.path);
        UiModel m;
        p.load(m);
        p.flush(a);
        p.flush(b);
    }
    {
        std::ofstream(dir.path / "settings.conf", std::ios::trunc) << "garbage\ngarbage\n";
    }
    Persistence p(dir.path);
    UiModel m;
    REQUIRE_FALSE(p.load(m).empty());
    CHECK(m == a); // the backup
    CHECK(p.update(m, 0).empty());
    CHECK(p.wait().empty());
    // The main file is good again, so a plain load finds it.
    SettingsStore check(dir.path);
    CHECK(check.load().source == LoadSource::Main);
    Persistence again(dir.path);
    UiModel m2;
    CHECK(again.load(m2).empty());
    CHECK(m2 == a);
    // The damaged file was kept aside, and the good backup was not replaced by it.
    CHECK(fs::exists(dir.path / "settings.conf.damaged"));
    CHECK(settings_text_usable(read(dir.path / "settings.conf.bak")));
}

TEST_CASE("the sections of a song are saved with the settings and come back") {
    TempDir dir;
    UiModel m;
    {
        Persistence p(dir.path);
        p.load(m);
        SongSections s;
        s.song_hash = 0x9f3a0c1b22d4e5f6ULL;
        Section a;
        a.id = 1;
        a.start_s = 0.0;
        a.end_s = 8.0;
        a.name = "Intro";
        a.custom_name = true;
        Section b;
        b.id = 2;
        b.start_s = 8.0;
        b.end_s = 20.5;
        b.name = "Section 2";
        s.sections = {a, b};
        s.next_id = 3;
        PlanItem item;
        item.section = 2;
        item.repeats = 4;
        item.clean_passes_only = true;
        item.speed_up.enabled = true;
        s.plan.push_back(item);
        m.sections.put(s);
        std::int64_t t = 1'000'000'000;
        p.update(m, t);
        CHECK(p.update(m, t + kSaveDebounceNs + 1).empty());
        CHECK(p.wait().empty());
        CHECK(read(p.store().main_path()).find("[sections.9f3a0c1b22d4e5f6]") !=
              std::string::npos);
    }
    Persistence again(dir.path);
    UiModel back;
    CHECK(again.load(back).empty());
    const SongSections* got = back.sections.find(0x9f3a0c1b22d4e5f6ULL);
    REQUIRE(got != nullptr);
    REQUIRE(got->sections.size() == 2);
    CHECK(got->sections[0].name == "Intro");
    CHECK(got->sections[1].end_s == doctest::Approx(20.5));
    REQUIRE(got->plan.size() == 1);
    CHECK(got->plan[0].repeats == 4);
    CHECK(got->plan[0].clean_passes_only);
    CHECK(back == m);
}

TEST_CASE("deleting a song's last section removes its entry from the file") {
    TempDir dir;
    UiModel m;
    Persistence p(dir.path);
    p.load(m);
    SongSections s;
    s.song_hash = 0x1111222233334444ULL;
    Section a;
    a.id = 1;
    a.start_s = 0.0;
    a.end_s = 8.0;
    a.name = "Only";
    a.custom_name = true;
    s.sections = {a};
    s.next_id = 2;
    m.sections.put(s);
    CHECK(p.flush(m).empty());
    CHECK(read(p.store().main_path()).find("sections.1111222233334444") != std::string::npos);
    m.sections.erase(0x1111222233334444ULL);
    CHECK(p.flush(m).empty());
    CHECK(read(p.store().main_path()).find("sections.1111222233334444") == std::string::npos);
}

// A settings file written before the settings sheet's pages were merged (Quick, Look and Note
// names into Notes, Effects and Colours into Colours & effects, Keyboard, Metronome and Microphone
// into their parents): the same keys still load to the same values, and a save writes the same
// keys back. Only the screens moved.
TEST_CASE("a settings file from before the settings restructure loads and keeps its keys") {
    const std::string text = read(fs::path(ETUDE_TEST_DATA_DIR) / "settings-before-117.conf");
    REQUIRE_FALSE(text.empty());
    const std::array<std::string_view, 2> owned{"look", "user_presets"};
    const LoadedSettings loaded = load_settings(text, owned);
    const UiModel m = model_from(loaded);

    // Notes (was Look and Note names).
    CHECK(m.look.note_style == NoteStyle::Glass); // frosted, migrated
    CHECK(m.look.block_layout == BlockLayout::EvenLanes);
    CHECK(m.look.name_mode == NameMode::Solfege);
    CHECK(m.look.names_on_blocks);
    CHECK(m.look.names_on_keys);
    CHECK(m.look.names_spelling == NameSpelling::Flats); // the old switch, on
    CHECK(m.look.black_key_names == BlackKeyNames::Always);
    CHECK_FALSE(m.look.names_show_octave);
    CHECK_FALSE(m.look.c_markers);
    CHECK(m.look.octave_numbering == OctaveNumbering::Yamaha);
    // Colours & effects (was Look > Colours and Effects).
    CHECK(m.look.color_scheme == ColorScheme::ByPitchClass);
    CHECK(m.look.left_color == RGBA{10, 20, 30, 255});
    CHECK(m.look.right_color == RGBA{200, 100, 50, 255});
    CHECK(m.look.pitch_colors[3] == RGBA{1, 2, 3, 255});
    CHECK(m.look.single_color == RGBA{9, 99, 199, 255});
    CHECK(m.look.high_contrast);
    CHECK(m.look.hit_effect == HitEffect::Ripple);
    CHECK_FALSE(m.look.key_flash);
    CHECK_FALSE(m.look.held_glow);
    CHECK_FALSE(m.look.trail_fade);
    CHECK(m.look.background == BackgroundStyle::Gradient);
    CHECK(m.look.reduced_effects);
    // View (now with the grade words).
    CHECK(m.look.key_size == KeySize::Large);
    CHECK(m.look.grade_position == GradePosition::Top);
    CHECK(m.look.grade_opacity == 55);
    CHECK_FALSE(m.look.chord_label);
    CHECK(m.app.bar_style == BarStyle::Dock);
    CHECK(m.app.bar_pinned);
    // Practice (now with the metronome's settings).
    CHECK(m.app.hit_window_ms == 120);
    CHECK(m.app.resume_countdown_s == 5);
    CHECK(m.app.skip_back_seconds == 7);
    CHECK(m.app.skip_forward_seconds == 9);
    CHECK(m.app.metronome);
    CHECK(m.app.metronome_volume == 40);
    CHECK(m.app.metronome_sound == ClickVoice::Beep);
    CHECK_FALSE(m.app.metronome_accent);
    CHECK(m.app.metronome_subdivision == ClickSubdivision::Divide);
    CHECK(m.app.metronome_pulse);
    // Input (now with the keyboard profile, typing letters, latency and ABC chords).
    CHECK(m.app.input_mode == InputMode::Both);
    CHECK(m.app.mic_device == "USB Mic");
    CHECK(m.app.mic_learn_tuning);
    CHECK(m.app.mic_a4_tenths == 4425);
    CHECK(m.look.show_typing_keys);
    CHECK(m.app.latency_correction_ms == -35);
    CHECK(m.app.abc_chords);
    CHECK(m.session.keyboard == KeyboardChoice::Manual);
    CHECK(m.session.keyboard_lowest == 28);
    CHECK(m.session.keyboard_highest == 103);
    // Presets.
    CHECK(m.user_presets.is_used(1));
    CHECK(m.user_presets.slot_name(1) == "Night owl");

    // Saving writes every key of the old file back, under the same section: none renamed or lost.
    const std::string saved = serialize_model(loaded, m);
    std::string section;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::string line = text.substr(at, end == std::string::npos ? end : end - at);
        at = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line.rfind("format", 0) == 0) {
            continue;
        }
        if (line.front() == '[') {
            section = line;
            continue;
        }
        std::string key = line.substr(0, line.find(' '));
        // The one key that was renamed on purpose: the spelling of black keys was a switch
        // (`names_prefer_flats`) and is three choices now, `names_spelling`; the saved value moved.
        const std::size_t old_name = key.find("names_prefer_flats");
        if (old_name != std::string::npos) {
            key.replace(old_name, std::string("names_prefer_flats").size(), "names_spelling");
        }
        const std::size_t sec = saved.find(section + "\n");
        REQUIRE(sec != std::string::npos);
        const std::size_t next = saved.find("\n[", sec + 1);
        const std::string body = saved.substr(sec, next == std::string::npos ? next : next - sec);
        CAPTURE(section);
        CAPTURE(key);
        CHECK(body.find("\n" + key + " =") != std::string::npos);
    }
    // And the model after the trip is the one loaded.
    CHECK(model_from(load_settings(saved, owned)) == m);
}
