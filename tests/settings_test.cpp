// SPDX-License-Identifier: GPL-3.0-or-later
// The settings file and the profile's part of it: saved with a backup, a damaged or newer file never
// lost, unknown keys kept, values the file gets wrong repaired, a person's keys swapped in and out, and
// the song sections kept in the same file.
#include "../app/ui/configs_logic.hpp"
#include "persistence.hpp"
#include "settings_store.hpp"
#include "upload_test_support.hpp"

#include "etude/profile.hpp"
#include "etude/profile_settings.hpp"
#include "etude/section_editing.hpp"
#include "etude/sections.hpp"
#include "etude/sections_store.hpp"
#include "etude/session_state.hpp"
#include "etude/settings_doc.hpp"
#include "etude/song_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace etude;
using namespace etude::app;
using namespace etude::app::ui;
using namespace upload_test;
using Kind = SettingsIssueKind;

namespace {

const std::string kV1 = "format = 1.0\n\n[app]\nhit_window_ms = 80\n";
const std::string kV2 = "format = 1.0\n\n[app]\nhit_window_ms = 90\n";
const std::string kV3 = "format = 1.0\n\n[app]\nhit_window_ms = 100\n";
constexpr std::uint64_t kHash = 0x9f3a0c1b22d4e5f6ULL;
constexpr const char* kName = "sections.9f3a0c1b22d4e5f6";

bool has_issue(const std::vector<SettingsIssue>& issues, Kind kind, const std::string& key = "") {
    return std::any_of(issues.begin(), issues.end(), [&](const SettingsIssue& i) { return i.kind == kind && (key.empty() || i.key == key); });
}

std::size_t count_issues(const std::vector<SettingsIssue>& issues, Kind kind) {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [&](const SettingsIssue& i) { return i.kind == kind; }));
}

SessionState read_state(const std::string& text, std::vector<SettingsIssue>* issues = nullptr) {
    return read_session(SettingsDocument::parse(text), issues);
}

constexpr std::array<std::string_view, 2> kModelSections{"look", "user_presets"};

UiModel model_of(const std::string& text) { return model_from(load_settings(text, kModelSections)); }

// Two looks that differ from the default in many options at once.
LookSettings neon_look() {
    LookSettings s;
    s.note_style = NoteStyle::Neon;
    s.style_intensity = 0.85f;
    s.block_glow = 55;
    s.color_scheme = ColorScheme::ByPitchClass;
    s.name_mode = NameMode::Off;
    s.background = BackgroundStyle::Motion;
    s.hit_effect = HitEffect::Sparks;
    s.visible_seconds = 4.5f;
    return s;
}

LookSettings glass_look() {
    LookSettings s;
    s.note_style = NoteStyle::Glass;
    s.style_intensity = 0.8f;
    s.left_color = {94, 234, 212, 255};
    s.right_color = {130, 170, 255, 255};
    s.hit_effect = HitEffect::Ripple;
    return s;
}

UiModel changed_model() {
    UiModel m;
    m.look = neon_look();
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

std::string value_of(const SettingsDocument& doc, std::string_view section, std::string_view key) {
    std::string out;
    for (const SettingsEntry& e : doc.section(section).entries) {
        if (e.key == key) {
            out = e.value;
        }
    }
    return out;
}

bool has_key(const SettingsDocument& doc, std::string_view section, std::string_view key) {
    const SettingsSection s = doc.section(section);
    return std::any_of(s.entries.begin(), s.entries.end(), [&](const SettingsEntry& e) { return e.key == key; });
}

// A settings file as one device might have it.
SettingsDocument device_file() {
    return SettingsDocument::parse("format = 1.0\n\n[app]\nhit_window_ms = 120\nmetronome = true\nmidi_in_port = Yamaha PSR-E383\n"
                                   "renderer = gpu\nlatency_correction_ms = 35\nget_songs_online = true\nfuture_key = 9\n\n"
                                   "[session]\nsong = Songs/Elise.mid\nspeed_percent = 60\nkeyboard = manual\nkeyboard_lowest = 48\n\n"
                                   "[look]\nnote_style = glow\n\n[user_presets]\nslot_1_name = Mine\n\n"
                                   "[sections.0123456789abcdef]\nversion = 1\nsection.1 = 0 16 Intro\n\n[something_new]\nx = 1\n");
}

// 16 bars of 4/4 at 120 BPM, one note across all of them: a bar lasts exactly 2 s.
Song song16() {
    Song song;
    song.tempo = make_tempo_map(480, 120);
    song.time_signatures = {TimeSignature{0, 4, 4}};
    Note n;
    n.start_tick = 0;
    n.end_tick = 1920LL * 16;
    n.pitch = 60;
    song.notes.push_back(n);
    REQUIRE(finish_song(song));
    return song;
}

// The entry of the golden examples: Intro, an unnamed second section, one playlist item.
SongSections example(const Song& song) {
    SongSections s;
    s.song_hash = kHash;
    REQUIRE(s.add_marker(song, 16.0).ok);
    REQUIRE(s.rename(s.sections[0].id, "Intro").ok);
    PlanItem p;
    p.section = s.sections[1].id;
    p.repeats = 4;
    p.clean_passes_only = true;
    p.speed_up = AutoSpeedUp{true, 60, 5, 100};
    REQUIRE(s.plan_append(p).ok);
    s.last_used = 5;
    return s;
}

SettingsDocument written(const SectionLibrary& lib) {
    SettingsDocument doc;
    write_sections(doc, lib);
    return doc;
}

} // namespace

TEST_CASE("store: each save keeps the previous file as the backup, and what was saved loads back") {
    TempDir dir;
    SettingsStore store(dir.path / "a" / "b"); // the first save makes the folder
    REQUIRE(store.save(kV1).ok);
    CHECK_FALSE(fs::exists(store.backup_path())); // nothing to keep yet
    REQUIRE(store.save(kV2).ok);
    REQUIRE(store.save(kV2).ok); // the same text again leaves the backup alone
    CHECK(read_file(store.backup_path()) == kV1);
    write_file(store.temp_path(), "format = 1.0\n[app]\nhit_"); // a save that was cut short
    const auto r = SettingsStore(dir.path / "a" / "b").load();
    CHECK(r.source == LoadSource::Main);
    CHECK(r.text == kV2);
    REQUIRE(store.save(kV3).ok);
    CHECK(read_file(store.main_path()) == kV3);
    CHECK(read_file(store.backup_path()) == kV2);
    CHECK_FALSE(fs::exists(store.temp_path()));
}

TEST_CASE("store: a missing, empty, binary, cut or oversize main file falls back to the backup") {
    TempDir dir;
    SettingsStore store(dir.path);
    REQUIRE(store.save(kV1).ok);
    REQUIRE(store.save(kV2).ok);
    fs::remove(store.main_path());
    const auto missing = SettingsStore(dir.path).load();
    CHECK(missing.source == LoadSource::Backup);
    CHECK(missing.text == kV1);
    CHECK_FALSE(missing.main_damaged);
    const std::string damaged[] = {"", std::string("\0\0\0\0\0\0", 6), "this is not a settings file\nneither is this\nor this one\n",
                                   "\x89PNG\r\n\x1a\n\x01\x02 binary stuff", "[app\nhit_window_ms", kV3 + std::string(kMaxSettingsBytes, '#')};
    for (const std::string& g : damaged) {
        write_file(store.main_path(), g);
        const auto r = SettingsStore(dir.path).load();
        INFO("damaged file of " << g.size() << " bytes");
        CHECK(r.source == LoadSource::Backup);
        CHECK(r.text == kV1);
        CHECK(r.main_damaged);
        CHECK_FALSE(r.message.empty());
    }
    // Both damaged: defaults, with a message. A damaged backup is not used while main is fine.
    write_file(store.backup_path(), "");
    const auto none = SettingsStore(dir.path).load();
    CHECK(none.source == LoadSource::None);
    CHECK(none.main_damaged);
    CHECK_FALSE(none.message.empty());
    write_file(store.main_path(), kV2);
    write_file(store.backup_path(), "garbage\n");
    CHECK(SettingsStore(dir.path).load().source == LoadSource::Main);
}

TEST_CASE("store: saving over a damaged main file keeps the good backup and sets the damaged one aside") {
    TempDir dir;
    SettingsStore store(dir.path);
    REQUIRE(store.save(kV1).ok);
    REQUIRE(store.save(kV2).ok);
    write_file(store.main_path(), "garbage\ngarbage\n");
    SettingsStore again(dir.path);
    REQUIRE(again.load().source == LoadSource::Backup);
    REQUIRE(again.save(kV1).ok); // the app rewrites what it restored, even when the text equals it
    CHECK(read_file(again.main_path()) == kV1);
    CHECK(read_file(again.backup_path()) == kV1); // not replaced by the garbage
    CHECK(read_file(again.damaged_path()) == "garbage\ngarbage\n");
}

TEST_CASE("store: a save that cannot be written reports why and leaves the old file") {
    TempDir dir;
    SettingsStore store(dir.path);
    REQUIRE(store.save(kV1).ok);
    fs::create_directory(store.temp_path()); // a folder where the temp file should go
    const auto r = store.save(kV2);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK(read_file(store.main_path()) == kV1);
    fs::remove(store.temp_path());
    CHECK_FALSE(store.save(std::string(kMaxSettingsBytes + 1, 'x')).ok); // too large
    CHECK(read_file(store.main_path()) == kV1);
}

TEST_CASE("file: odd but legal text, unknown keys, sections and malformed lines survive a save byte for byte") {
    // CRLF, no final newline, tabs, spaces around =, comments, blank runs, a BOM, junk lines.
    for (const std::string& text : {std::string("\xEF\xBB\xBF" "format = 1.0\r\n[app]\r\nhit_window_ms=80\r\n"),
                                    std::string("[app]\n\thit_window_ms\t=\t80  \n# c\n\n\n\nnot a pair\n[ session ]\nx = y"),
                                    std::string("no section at all\n= no key\nkey only\n"), std::string("[a]\n[b]\n[a]\n"),
                                    std::string("[unterminated\n[]\n[ ]\n]\n"), std::string("[a]\nk = v\0w\n", 12)}) {
        CAPTURE(text);
        CHECK(SettingsDocument::parse(text).serialize() == text);
    }
    const std::string text = "# my settings\r\nformat=1.0\r\n\r\n[app]\r\nhit_window_ms   =   80\r\nnew_key = x   y\r\n\r\n"
                             "[from_the_future]\r\nweird   =  1\r\nno equals\r\n\r\n[session]\r\nspeed_percent=100\r\n"
                             "song = Songs/a.mid\r\nfuture = 1\r\n";
    LoadedSettings loaded = load_settings(text);
    CHECK(has_issue(loaded.issues, Kind::UnknownKey, "future"));
    loaded.session.speed_percent = 70; // a change to one key
    const std::string out = loaded.serialize();
    // Every line is still there as typed, in order, except the one value that changed.
    std::size_t at = 0;
    for (std::size_t end = 0; (end = text.find("\r\n", at)) != std::string::npos; at = end + 2) {
        const std::string line = text.substr(at, end + 2 - at);
        CAPTURE(line);
        CHECK((line.rfind("speed_percent", 0) == 0 || out.find(line) != std::string::npos));
    }
    CHECK(load_settings(out).session.speed_percent == 70);
}

TEST_CASE("file: a newer major version loads what it can and recommends read-only") {
    LoadedSettings loaded = load_settings("format = 2.0\n[app]\nhit_window_ms = 100\nnew_thing = 1\n[session]\nspeed_percent = 50\n");
    CHECK(loaded.read_only_recommended());
    CHECK(loaded.app.hit_window_ms == 100);
    CHECK(loaded.session.speed_percent == 50);
    CHECK(has_issue(loaded.issues, Kind::NewerVersion));
}

TEST_CASE("file: a value that cannot be read keeps the default, one out of range is clamped, both are reported") {
    std::vector<SettingsIssue> issues;
    SessionState s = read_state("[session]\nspeed_percent = fast\nposition_s = nan\nloop_enabled = maybe\nsong_hash = xyz\n"
                                "count_in_bars = 99999999999999999999\nsong = \"unterminated\ntranspose_semitones = 3\n", &issues);
    CHECK(s.speed_percent == 100);
    CHECK(s.position_s == 0.0);
    CHECK_FALSE(s.loop_enabled);
    CHECK(s.song_hash == 0);
    CHECK(s.song_path.empty());
    CHECK(s.transpose_semitones == 3); // good lines still count
    for (const char* key : {"speed_percent", "position_s", "loop_enabled", "song_hash", "count_in_bars", "song"}) {
        CAPTURE(key);
        CHECK(has_issue(issues, Kind::BadValue, key));
    }
    issues.clear();
    s = read_state("[session]\ntranspose_semitones = 30\nspeed_percent = 260\nposition_s = -4\n", &issues);
    CHECK(s.transpose_semitones == 12);
    CHECK(s.speed_percent == 200);
    CHECK(s.position_s == 0.0);
    CHECK(has_issue(issues, Kind::OutOfRange, "speed_percent"));
    issues.clear();
    CHECK(read_state("[session]\nspeed_percent = 62\n", &issues).speed_percent == 62); // between the steps
    CHECK(issues.empty());
}

TEST_CASE("file: a song path that is absolute, climbs out or is not text is refused; real names round trip") {
    std::vector<SettingsIssue> issues;
    SessionState s = read_state("[session]\nsong = ../../etc/hosts\nsong_hash = 00000000000000ff\n", &issues);
    CHECK(s.song_path.empty());
    CHECK(s.song_hash == 0);
    CHECK(has_issue(issues, Kind::OutOfRange, "song"));
    for (const char* bad : {"rel/a.mid", "a.mid", "/x/../etc/passwd", "C:x.mid", "/a\nb.mid"}) {
        CAPTURE(bad);
        SessionState state;
        state.song_path = "a.mid";
        state.song_abs_path = bad; // the fallback path must be absolute and stay put
        CHECK(state.clamped().song_abs_path.empty());
        CHECK(state.clamped().song_path == "a.mid");
    }
    for (const char* name : {"  lead.mid", "\"quoted\".mid", "a # b = c.mid", "dir/\xE6\x9B\xB2.mid"}) {
        CAPTURE(name);
        SessionState state;
        state.song_path = name;
        state.song_abs_path = "/home/me/My Music/a # b.mid";
        state.song_hash = 9;
        SettingsDocument doc = SettingsDocument::parse("");
        write_session(doc, state);
        CHECK(read_session(SettingsDocument::parse(doc.serialize())) == state);
    }
}

TEST_CASE("a model survives the trip through the file text, every look field and the port choice included") {
    const UiModel m = changed_model();
    const std::string text = serialize_model(load_settings(""), m);
    for (const char* section : {"[look]", "[user_presets]", "[app]", "[session]"}) {
        CHECK(text.find(section) != std::string::npos);
    }
    CHECK(model_of(text) == m);

    UiModel all;
    LookSettings& s = all.look;
    const auto fill = [](auto& colours, int seed) {
        for (int i = 0; i < static_cast<int>(colours.size()); ++i) {
            colours[static_cast<std::size_t>(i)] =
                RGBA{static_cast<std::uint8_t>(seed + i * 7), static_cast<std::uint8_t>(200 - i), static_cast<std::uint8_t>(i * 3 + 1), 255};
        }
    };
    s.note_style = NoteStyle::Neon;
    s.style_intensity = 0.37f;
    s.block_glow = 77;
    s.beat_lines = BeatLines::Bars;
    s.color_scheme = ColorScheme::ByVelocity;
    s.left_color = {1, 2, 3, 255};
    s.right_color = {250, 251, 252, 128};
    s.unknown_color = {9, 99, 199, 255};
    fill(s.pitch_colors, 10);
    fill(s.octave_colors, 30);
    fill(s.velocity_colors, 50);
    s.single_color = {17, 34, 51, 255};
    s.palettes.fill(Palette::Sunset);
    s.saved_palettes[1].filled = true;
    fill(s.saved_palettes[1].colors, 70);
    s.note_slot = 1;
    s.high_contrast = true;
    s.learn_picks = {LearnShade::Lime, LearnShade::Jade, LearnShade::Magenta, LearnShade::Coral};
    s.name_mode = NameMode::Solfege;
    s.names_show_octave = true;
    s.names_spelling = NameSpelling::Flats;
    s.names_on_blocks = false;
    s.names_on_keys = true;
    s.names_on_sheet = true;
    s.sheet_naming = SheetNaming::Solfege;
    s.sheet_chords = SheetChords::Symbols;
    s.sheet_note_colours = true;
    s.black_key_names = BlackKeyNames::Always;
    s.block_layout = BlockLayout::EvenLanes;
    s.c_markers = false;
    s.octave_numbering = OctaveNumbering::Yamaha;
    s.key_flash = false;
    s.held_glow = false;
    s.hit_effect = HitEffect::Ripple;
    s.trail_fade = false;
    s.combo_number = false;
    s.rising_in_song = true;
    s.background = BackgroundStyle::Motion;
    s.reduced_effects = true;
    s.key_size = KeySize::Large;
    s.show_typing_keys = true;
    s.chord_label = false;
    s.chord_label_size = 1.7f;
    s.show_key = false;
    s.visible_seconds = 7.3f;
    s.keyboard_height = 0.2345f;
    s.grade_words = false;
    s.grade_position = GradePosition::Top;
    s.grade_opacity = 37;
    REQUIRE(s.is_valid());
    all.app.midi_in_port = "Yamaha PSR-E383";
    all.app.midi_out_port = "Synth";
    CHECK(model_of(serialize_model(load_settings(""), all)) == all);
}

TEST_CASE("a bad preset slot leaves the other slots loading") {
    UiModel m;
    m.user_presets.save_slot(0, "good", glass_look());
    m.user_presets.save_slot(1, "bad", neon_look());
    std::string text = serialize_model(load_settings(""), m);
    const std::string field = "slot_2_note_style = neon";
    const std::size_t at = text.find(field);
    REQUIRE(at != std::string::npos);
    text.replace(at, field.size(), "slot_2_note_style = ???");
    const UiModel back = model_of(text);
    CHECK(back.user_presets.slot_name(0) == "good");
    LookSettings first;
    back.user_presets.load_slot(0, &first);
    CHECK(first == glass_look());
    CHECK(back.user_presets.is_used(1));
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
    write_file(dir.path / "settings.conf", "garbage\nmore garbage\n");
    {
        Persistence p(dir.path);
        UiModel m;
        CHECK_FALSE(p.load(m).empty());
        CHECK(m == version_a);
    }
    write_file(dir.path / "settings.conf", "garbage\nmore garbage\n");
    write_file(dir.path / "settings.conf.bak", "");
    Persistence p(dir.path);
    UiModel m;
    CHECK_FALSE(p.load(m).empty());
    CHECK(m == UiModel{});
}

TEST_CASE("a file from a newer major version is read but never written over") {
    TempDir dir;
    const std::string newer = "format = 2.0\n\n[app]\nhit_window_ms = 100\n";
    write_file(dir.path / "settings.conf", newer);
    Persistence p(dir.path);
    UiModel m;
    CHECK_FALSE(p.load(m).empty());
    CHECK(m.app.hit_window_ms == 100);
    m.look.c_markers = false;
    CHECK(p.flush(m).empty());
    CHECK(read_file(dir.path / "settings.conf") == newer);
}

TEST_CASE("a save that fails is reported once and retried later") {
    TempDir dir;
    write_file(dir.path / "blocked", "x"); // the folder path is a file, so nothing can be written there
    Persistence p(dir.path / "blocked");
    UiModel m;
    p.load(m);
    m.look.c_markers = false;
    std::int64_t t = 0;
    p.update(m, t);
    t += kSaveDebounceNs;
    CHECK(p.update(m, t).empty());  // handed to the saver
    CHECK_FALSE(p.wait(t).empty()); // it failed: reported
    t += kSaveDebounceNs;
    CHECK(p.update(m, t).empty()); // not again right away
    CHECK(p.wait(t).empty());
    t += kRetryDelayNs;
    CHECK(p.update(m, t).empty()); // the retry is made...
    CHECK(p.wait(t).empty());      // ...and fails quietly
}

TEST_CASE("the sections of a song are saved with the settings and come back") {
    TempDir dir;
    UiModel m;
    {
        Persistence p(dir.path);
        p.load(m);
        SongSections s;
        s.song_hash = kHash;
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
        CHECK(read_file(p.store().main_path()).find("[" + std::string(kName) + "]") != std::string::npos);
    }
    Persistence again(dir.path);
    UiModel back;
    CHECK(again.load(back).empty());
    const SongSections* got = back.sections.find(kHash);
    REQUIRE(got != nullptr);
    REQUIRE(got->sections.size() == 2);
    CHECK(got->sections[0].name == "Intro");
    CHECK(got->sections[1].end_s == doctest::Approx(20.5));
    REQUIRE(got->plan.size() == 1);
    CHECK(got->plan[0].repeats == 4);
    CHECK(got->plan[0].clean_passes_only);
    CHECK(back == m);
}

TEST_CASE("profile settings: every [app] key the build writes has a place") {
    SettingsDocument doc;
    write_app_settings(doc, AppSettings{});
    const SettingsSection app = doc.section(kAppSection);
    REQUIRE_FALSE(app.entries.empty());
    for (const SettingsEntry& e : app.entries) {
        const auto in = [&](std::span<const std::string_view> keys) { return std::count(keys.begin(), keys.end(), e.key); };
        INFO("key: " << e.key);
        CHECK(in(profile_app_keys()) + in(device_app_keys()) == 1);
    }
    for (const auto keys : {profile_app_keys(), device_app_keys()}) {
        for (const std::string_view key : keys) {
            INFO("listed but not written: " << key);
            CHECK(has_key(doc, kAppSection, key));
        }
    }
}

TEST_CASE("profile settings: switching swaps the person's keys and leaves the device's") {
    SettingsDocument ben = SettingsDocument::parse("[app]\nhit_window_ms = 200\nbar_style = dock\nrenderer = basic\n\n"
                                                   "[session]\nsong = Songs/Ode.mid\nspeed_percent = 100\n\n[look]\nnote_style = flat\n");
    const std::string ben_text = profile_settings_text(ben);
    SettingsDocument doc = device_file();
    const std::string anna_text = profile_settings_text(doc);
    apply_profile_settings(doc, ben_text);
    // Ben's keys are in force.
    CHECK(value_of(doc, "app", "hit_window_ms") == "200");
    CHECK(value_of(doc, "app", "bar_style") == "dock");
    CHECK(value_of(doc, "session", "song") == "Songs/Ode.mid");
    CHECK(doc.section_text("look") == "note_style = flat\n");
    // What Anna had and Ben never set is gone, so it reads as its default.
    CHECK_FALSE(has_key(doc, "app", "metronome"));
    CHECK_FALSE(has_key(doc, "session", "keyboard"));
    CHECK_FALSE(has_key(doc, "session", "keyboard_lowest"));
    CHECK_FALSE(doc.has_section("user_presets"));
    CHECK_FALSE(doc.has_section("sections.0123456789abcdef"));
    // The device's stay, including a key and a section this build does not know and the renderer
    // Ben's file carried (a device key in a profile's text is not applied).
    CHECK(value_of(doc, "app", "renderer") == "gpu");
    CHECK(value_of(doc, "app", "midi_in_port") == "Yamaha PSR-E383");
    CHECK(value_of(doc, "app", "latency_correction_ms") == "35");
    CHECK(value_of(doc, "app", "get_songs_online") == "true");
    CHECK(value_of(doc, "app", "future_key") == "9");
    CHECK(value_of(doc, "something_new", "x") == "1");
    apply_profile_settings(doc, anna_text); // and back to Anna: everything she had returns
    CHECK(profile_settings_text(doc) == anna_text);
    CHECK(value_of(doc, "app", "renderer") == "gpu");
    CHECK(value_of(doc, "app", "hit_window_ms") == "120");
    CHECK(doc.has_section("sections.0123456789abcdef"));
}

TEST_CASE("copying another profile's config leaves their settings alone, and Undo gives yours back") {
    LookSettings dani;
    dani.note_style = NoteStyle::Neon;
    SettingsDocument doc;
    doc.set_section_text("look", serialize(dani));
    ProfileSet set;
    Profile made;
    REQUIRE(set.add("Dani", 1000, &made) == ProfileResult::Ok);
    const std::vector<ProfileLook> looks{{made.id, profile_look(profile_settings_text(doc))}};
    ProfileInfo info;
    info.set = &set;
    info.looks = looks;
    info.active = kDefaultProfileId;
    UiModel m;
    m.look.note_style = NoteStyle::Candy;
    const LookSettings mine = m.look;
    const LookSettings replaced = config_copy(m, *config_of_profile(info, made.id));
    CHECK(m.look == dani.clamped());
    CHECK(replaced == mine);
    config_undo_copy(m, replaced);
    CHECK(m.look == mine);
}

TEST_CASE("sections store: an entry reads back as it was written, several songs each in their own document section") {
    Song song = song16();
    SongSections original = example(song);
    SectionLibrary lib;
    lib.put(original);
    SectionLibrary back = read_sections(SettingsDocument::parse(written(lib).serialize()));
    REQUIRE(back.songs().size() == 1);
    SongSections s = back.songs()[0];
    CHECK(s.well_formed());
    s.fit_to_song(song); // derived fields come with the song
    CHECK(s.valid(song));
    CHECK(s == original);

    SectionLibrary several;
    for (std::uint64_t h : {1ULL, 2ULL, 0xFFFFFFFFFFFFFFFFULL}) {
        REQUIRE(several.edit(h)->add_marker(song, 16.0).ok);
    }
    SettingsDocument doc = written(several);
    CHECK(doc.has_section("sections.0000000000000001"));
    CHECK(doc.has_section("sections.0000000000000002"));
    CHECK(doc.has_section("sections.ffffffffffffffff"));
    CHECK(read_sections(doc).songs().size() == 3);
    CHECK(read_sections(doc).find(0xFFFFFFFFFFFFFFFFULL) != nullptr);
}

TEST_CASE("sections store: keys and comments this build does not know are kept") {
    Song song = song16();
    const std::string text = std::string("[") + kName + "]\n# written by a newer build\nversion = 1\nnext_id = 3\nfuture_key = 42\n"
                             "section.1 = 0 16 Intro\nsection.2 = 16 32\nnot a key value line\nplan.1 = 2 4 1 1 60 5 100\n";
    std::vector<SettingsIssue> issues;
    SettingsDocument doc = SettingsDocument::parse(text);
    SectionLibrary lib = read_sections(doc, &issues);
    REQUIRE(lib.songs().size() == 1);
    CHECK(count_issues(issues, Kind::UnknownKey) == 1);
    SongSections* s = lib.edit(kHash);
    REQUIRE(s != nullptr);
    s->fit_to_song(song);
    REQUIRE(s->rename(s->sections[1].id, "Verse").ok);
    write_sections(doc, lib);
    const std::string out = doc.section_text(kName);
    CHECK(out.find("# written by a newer build\n") != std::string::npos);
    CHECK(out.find("future_key = 42\n") != std::string::npos);
    CHECK(out.find("not a key value line\n") != std::string::npos);
    CHECK(out.find("section.2 = 16 32 Verse\n") != std::string::npos);
    CHECK(out.find("section.2 = 16 32\n") == std::string::npos);
    write_sections(doc, lib); // and again: nothing is added or lost
    CHECK(doc.section_text(kName) == out);
}

TEST_CASE("sections store: a damaged entry does not cost the others") {
    const std::string text = "[sections.0000000000000001]\nsection.1 = 0 16 A\nsection.2 = 16 32 B\n\n"
                             "[sections.garbage]\nsection.1 = 0 16\n\n"
                             "[sections.0000000000000002]\nsection.1 = 5 3\n\n"
                             "[sections.0000000000000003]\nsection.1 = 0 16 C\nsection.2 = 16 32 D\n\n"
                             "[sections.ABCDEF0000000004]\nsection.1 = 0 16 E\nsection.2 = 16 32 F\n\n"
                             "[sections.5]\nsection.1 = 0 16 G\nsection.2 = 16 32 H\n\n[app]\nhit_window_ms = 90\n";
    std::vector<SettingsIssue> issues;
    SettingsDocument doc = SettingsDocument::parse(text);
    SectionLibrary lib = read_sections(doc, &issues);
    CHECK(lib.songs().size() == 2);
    CHECK(lib.find(1) != nullptr);
    CHECK(lib.find(3) != nullptr);
    CHECK(lib.find(2) == nullptr); // no usable section
    CHECK(lib.stale_names().size() == 4); // the ones that could not be read are on the list to delete
    write_sections(doc, lib);
    for (const char* gone : {"sections.garbage", "sections.0000000000000002", "sections.ABCDEF0000000004", "sections.5"}) {
        CHECK_FALSE(doc.has_section(gone));
    }
    CHECK(doc.has_section("sections.0000000000000001"));
    CHECK(doc.has_section("sections.0000000000000003"));
    CHECK(doc.section_text("app") == "hit_window_ms = 90\n");
    CHECK(count_issues(issues, Kind::UnknownSection) == 3);
    CHECK(count_issues(issues, Kind::OutOfRange) == 1); // sections.0...02
}

TEST_CASE("sections store: an entry of a newer version is left alone; erasing a song deletes its entry on the next write") {
    const std::string text = "[sections.0000000000000001]\nversion = 2\nsection.1 = 0 16 A\nnew = 1\n";
    std::vector<SettingsIssue> issues;
    SettingsDocument doc = SettingsDocument::parse(text);
    SectionLibrary lib = read_sections(doc, &issues);
    CHECK(lib.songs().empty());
    CHECK(lib.stale_names().empty());
    CHECK(count_issues(issues, Kind::NewerVersion) == 1);
    Song song = song16();
    REQUIRE(lib.edit(9)->add_marker(song, 16.0).ok); // another song is written next to it; the newer entry stays byte for byte
    write_sections(doc, lib);
    CHECK(doc.section_text("sections.0000000000000001") == "version = 2\nsection.1 = 0 16 A\nnew = 1\n");
    CHECK(doc.has_section("sections.0000000000000009"));

    SectionLibrary two;
    REQUIRE(two.edit(1)->add_marker(song, 16.0).ok);
    REQUIRE(two.edit(2)->add_marker(song, 16.0).ok);
    SettingsDocument both = written(two);
    two.erase(1);
    write_sections(both, two);
    CHECK_FALSE(both.has_section("sections.0000000000000001"));
    CHECK(both.has_section("sections.0000000000000002"));
}

TEST_CASE("sections property: clear forgets every undo step, so an Undo from an old song is refused") {
    const Song song = song16();
    SectionHistory history;
    SongSections s;
    s.song_hash = 9;
    REQUIRE(s.add_marker(song, 8.0).ok);
    history.push(s);
    REQUIRE(history.can_undo());
    history.clear();
    CHECK_FALSE(history.can_undo());
    CHECK_FALSE(history.undo(s).has_value());
}
