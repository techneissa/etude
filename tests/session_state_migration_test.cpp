// SPDX-License-Identifier: GPL-3.0-or-later
// session_state: migration (split from session_state_test.cpp).
#include "etude/melody_added.hpp"
#include "etude/session_state.hpp"
#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "session_state_test_support.hpp"

using namespace etude;
using Kind = SettingsIssueKind;
using namespace session_state_test_support;

TEST_CASE("the metronome and the count-in have their defaults, ranges and keys") {
    const AppSettings d;
    CHECK_FALSE(d.metronome);
    CHECK(d.metronome_sound == ClickVoice::WoodBlock);
    CHECK(d.metronome_volume == kDefaultClickVolume);
    CHECK(d.metronome_accent);
    CHECK(d.metronome_subdivision == ClickSubdivision::Off);
    CHECK_FALSE(d.metronome_pulse);
    CHECK(SessionState{}.count_in_bars == kDefaultCountInBars);
    CHECK(kDefaultCountInBars == 1);

    AppSettings a;
    a.metronome = true;
    a.metronome_sound = ClickVoice::Beep;
    a.metronome_volume = 35;
    a.metronome_accent = false;
    a.metronome_subdivision = ClickSubdivision::Triplets;
    a.metronome_pulse = true;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("metronome = true\n") != std::string::npos);
    CHECK(text.find("metronome_sound = beep\n") != std::string::npos);
    CHECK(text.find("metronome_volume = 35\n") != std::string::npos);
    CHECK(text.find("metronome_accent = false\n") != std::string::npos);
    CHECK(text.find("metronome_subdivision = triplets\n") != std::string::npos);
    CHECK(text.find("metronome_pulse = true\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app_settings(SettingsDocument::parse(text), &issues) == a);
    CHECK(issues.empty());

    a.metronome_volume = 400;
    a.metronome_sound = static_cast<ClickVoice>(9);
    a.metronome_subdivision = static_cast<ClickSubdivision>(9);
    const AppSettings c = a.clamped();
    CHECK(c.metronome_volume == kMaxClickVolume);
    CHECK(c.metronome_sound == ClickVoice::WoodBlock);
    CHECK(c.metronome_subdivision == ClickSubdivision::Off);
    a.metronome_volume = -3;
    CHECK(a.clamped().metronome_volume == 0);
}

TEST_CASE("the speed is saved up to 200% and the count-in in bars") {
    SessionState s;
    s.song_path = "a.mid";
    s.speed_percent = 200;
    s.count_in_bars = 2;
    SettingsDocument doc = SettingsDocument::parse("");
    write_session(doc, s);
    const SessionState back = read_session(SettingsDocument::parse(doc.serialize()));
    CHECK(back.speed_percent == 200);
    CHECK(back.count_in_bars == 2);
    s.speed_percent = 205;
    CHECK(s.clamped().speed_percent == 200);
    s.count_in_bars = 3;
    CHECK(s.clamped().count_in_bars == 2);
}

// ---- A file written by the version before the metronome and count-in moved

namespace {

// What main wrote: [session] had `metronome` and `count_in_beats`, [app] neither.
const char* const kMainEraFile =
    "# etude settings\n"
    "format = 1.0\n"
    "\n"
    "[app]\n"
    "hit_window_ms = 80\n"
    "bar_style = strip\n"
    "\n"
    "[session]\n"
    "song = Songs/a.mid\n"
    "position_s = 4.5\n"
    "speed_percent = 80\n"
    "count_in_beats = 4\n"
    "metronome = true\n"
    "mode = flow\n";

} // namespace

TEST_CASE("migration: a main-era file keeps the metronome on and the count-in") {
    const LoadedSettings loaded = load_settings(kMainEraFile, {});
    CHECK(loaded.app.metronome);
    CHECK(loaded.session.count_in_bars == 1); // beats 4: any beats are one bar
    CHECK(loaded.session.speed_percent == 80);
    CHECK(loaded.session.position_s == doctest::Approx(4.5));
    // The old keys are not unknown keys: no issue at all.
    CHECK(loaded.issues.empty());
}

TEST_CASE("migration: count_in_beats 0 is no count-in, and the metronome off stays off") {
    const LoadedSettings loaded = load_settings(
        "[session]\ncount_in_beats = 0\nmetronome = false\n", {});
    CHECK(loaded.session.count_in_bars == 0);
    CHECK_FALSE(loaded.app.metronome);
    CHECK(loaded.issues.empty());
    // Every other count of beats, up to the old maximum, is one bar.
    for (const int beats : {1, 2, 3, 4, 8, 16}) {
        CAPTURE(beats);
        const std::string text = "[session]\ncount_in_beats = " + std::to_string(beats) + "\n";
        CHECK(load_settings(text, {}).session.count_in_bars == 1);
    }
}

TEST_CASE("migration: the new keys win over the old ones, and the old ones are never read twice") {
    // [app] metronome present: the [session] one is ignored.
    LoadedSettings a = load_settings(
        "[app]\nmetronome = false\n[session]\nmetronome = true\ncount_in_bars = 2\n"
        "count_in_beats = 0\n",
        {});
    CHECK_FALSE(a.app.metronome);
    CHECK(a.session.count_in_bars == 2); // count_in_bars present: count_in_beats is ignored
    CHECK(a.issues.empty());
    // Neither new key and neither old one: the defaults.
    LoadedSettings none = load_settings("[session]\nspeed_percent = 90\n", {});
    CHECK_FALSE(none.app.metronome);
    CHECK(none.session.count_in_bars == kDefaultCountInBars);
    // A bad old value keeps the default and is reported like any bad value.
    std::vector<SettingsIssue> issues;
    read("[session]\ncount_in_beats = lots\n", &issues);
    CHECK(has_issue(issues, Kind::BadValue, "count_in_beats"));
    CHECK_FALSE(has_issue(issues, Kind::UnknownKey));
}

TEST_CASE("migration: the next save writes the new keys and drops the old ones, nothing else") {
    LoadedSettings loaded = load_settings(kMainEraFile, {});
    const std::string saved = loaded.serialize();
    CHECK(saved.find("count_in_beats") == std::string::npos);
    CHECK(saved.find("metronome = true") != std::string::npos); // now in [app]
    const SettingsDocument doc = SettingsDocument::parse(saved);
    bool in_app = false;
    for (const SettingsEntry& e : doc.section("app").entries) {
        in_app = in_app || (e.key == "metronome" && e.value == "true");
    }
    CHECK(in_app);
    for (const SettingsEntry& e : doc.section("session").entries) {
        CHECK(e.key != "metronome");
        CHECK(e.key != "count_in_beats");
    }
    // What the file said that was not migrated stays: the song, and the bar style.
    CHECK(saved.find("song = Songs/a.mid") != std::string::npos);
    CHECK(saved.find("bar_style = strip") != std::string::npos);
    // Reading what was saved gives the same state, and no issue.
    const LoadedSettings again = load_settings(saved, {});
    CHECK(again.app == loaded.app);
    CHECK(again.session == loaded.session);
    CHECK(again.issues.empty());
    // And the save is stable: saving again changes nothing.
    CHECK(again.serialize() == saved);
}

TEST_CASE("a speed written with a fraction is read as a number and rounded") {
    std::vector<SettingsIssue> issues;
    CHECK(read("[session]\nspeed_percent = 87.5\n", &issues).speed_percent == 88);
    CHECK(read("[session]\nspeed_percent = 87.4\n").speed_percent == 87);
    CHECK(read("[session]\nspeed_percent = 99.6\n").speed_percent == 100);
    CHECK(read("[session]\nspeed_percent = 1e2\n").speed_percent == 100);
    CHECK(read("[session]\nspeed_percent = 250.7\n").speed_percent == 200);
    CHECK(read("[session]\nspeed_percent = 3.2\n").speed_percent == 25);
    // Not a number: the default (100), reported.
    issues.clear();
    CHECK(read("[session]\nspeed_percent = fast\n", &issues).speed_percent == 100);
    CHECK(has_issue(issues, Kind::BadValue, "speed_percent"));
    CHECK(read("[session]\nspeed_percent = nan\n").speed_percent == 100);
    CHECK(read("[session]\nspeed_percent = inf\n").speed_percent == 100);
    // A whole number inside the range is exact and reported as nothing.
    issues.clear();
    CHECK(read("[session]\nspeed_percent = 62\n", &issues).speed_percent == 62);
    CHECK(issues.empty());
}

TEST_CASE("the picker's recent colours are saved in [app] as recent_colors and read back") {
    CHECK(AppSettings{}.recent_colors.empty());
    AppSettings a;
    a.recent_colors.add({1, 2, 3, 255});
    a.recent_colors.add({250, 100, 200, 255});
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("recent_colors = #FA64C8 #010203\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    const AppSettings back = read_app(text, &issues);
    CHECK(issues.empty());
    CHECK(back.recent_colors == a.recent_colors);
    CHECK(back == a);
    // A file from before the key keeps the list empty and reports nothing.
    CHECK(read_app("[app]\nbar_style = dock\n", &issues).recent_colors.empty());
    CHECK(issues.empty());
}

TEST_CASE("bad words in recent_colors are dropped and reported, the good ones kept in order") {
    std::vector<SettingsIssue> issues;
    const AppSettings got =
        read_app("[app]\nrecent_colors = \"#112233 banana #12 #445566\"\n", &issues);
    REQUIRE(got.recent_colors.size() == 2);
    CHECK(got.recent_colors.items()[0] == RGBA{0x11, 0x22, 0x33, 255});
    CHECK(got.recent_colors.items()[1] == RGBA{0x44, 0x55, 0x66, 255});
    CHECK(has_issue(issues, Kind::BadValue, "recent_colors"));

    // More than fit: the first `kRecentColours` count.
    issues.clear();
    std::string words;
    for (std::size_t i = 1; i <= kRecentColours + 2; ++i) {
        char word[16];
        std::snprintf(word, sizeof word, "%s#0000%02zX", i > 1 ? " " : "", i);
        words += word;
    }
    const AppSettings many = read_app("[app]\nrecent_colors = \"" + words + "\"\n", &issues);
    CHECK(many.recent_colors.size() == kRecentColours);
    CHECK(many.recent_colors.items()[0] == RGBA{0, 0, 1, 255});
    CHECK(many.recent_colors.items()[kRecentColours - 1] ==
          RGBA{0, 0, static_cast<std::uint8_t>(kRecentColours), 255});
    CHECK(has_issue(issues, Kind::BadValue, "recent_colors"));

    // Bad quoting keeps the default; an empty text is an empty list.
    issues.clear();
    CHECK(read_app("[app]\nrecent_colors = \"#112233\n", &issues).recent_colors.empty());
    CHECK(has_issue(issues, Kind::BadValue, "recent_colors"));
    issues.clear();
    CHECK(read_app("[app]\nrecent_colors = \"\"\n", &issues).recent_colors.empty());
    CHECK(issues.empty());
}

TEST_CASE("the library's scroll position is saved in [app] and clamped") {
    AppSettings a;
    std::vector<SettingsIssue> issues;
    CHECK(AppSettings{}.library_scroll == 0);
    a.library_scroll = 4321;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    CHECK(doc.serialize().find("library_scroll = 4321\n") != std::string::npos);
    CHECK(read_app(doc.serialize(), &issues) == a);
    CHECK(read_app("[app]\nlibrary_scroll = -5\n", &issues).library_scroll == 0);
    CHECK(read_app("[app]\nlibrary_scroll = 999999999\n", &issues).library_scroll ==
          kMaxLibraryScroll);
}

TEST_CASE("the library's sort, direction, grouping, filter, view and versions are saved in [app]") {
    AppSettings a;
    CHECK(a.library == BrowseOrder{});
    a.library = {BrowseSort::Recent, true, BrowseGroup::Genre, BrowseFilter::InProgress};
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("library_sort = recent\n") != std::string::npos);
    CHECK(text.find("library_sort_reversed = true\n") != std::string::npos);
    CHECK(text.find("library_group = genre\n") != std::string::npos);
    CHECK(text.find("library_filter = in_progress\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app(text, &issues) == a);
    CHECK(issues.empty());
    // The level and hands filters and the view.
    a.library.levels = 1 | 4;
    a.library.hands = BrowseHands::RightOnly;
    a.library.group = BrowseGroup::MyGroups;
    a.library_view = BrowseView::Shelves;
    a.get_songs_online = true;
    doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string more = doc.serialize();
    CHECK(more.find("library_levels = 5\n") != std::string::npos);
    CHECK(more.find("library_hands = right\n") != std::string::npos);
    CHECK(more.find("library_group = my_groups\n") != std::string::npos);
    CHECK(more.find("library_view = shelves\n") != std::string::npos);
    CHECK(more.find("get_songs_online = true\n") != std::string::npos);
    CHECK(read_app(more, &issues) == a);
    CHECK(issues.empty());
    CHECK(read_app("[app]\nlibrary_levels = 99\n", &issues).library.levels == kLevelBits);
    // Versions of one piece are grouped unless the player turns it off, and the setting is kept.
    CHECK(AppSettings{}.group_versions);
    CHECK(read_app("[app]\nbar_pinned = true\n", &issues).group_versions);
    a.group_versions = false;
    doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string ungrouped = doc.serialize();
    CHECK(ungrouped.find("group_versions = false\n") != std::string::npos);
    CHECK_FALSE(read_app(ungrouped, &issues).group_versions);
    CHECK(read_app(ungrouped, &issues) == a);
    CHECK_FALSE(read_app("[app]\ngroup_versions = false\n", &issues).group_versions);
    issues.clear();
    CHECK(read_app("[app]\ngroup_versions = maybe\n", &issues).group_versions);
    CHECK(has_issue(issues, Kind::BadValue, "group_versions"));
    issues.clear();
    // An unknown word keeps the default and is reported.
    const AppSettings bad = read_app("[app]\nlibrary_sort = colour\n", &issues);
    CHECK(bad.library.sort == BrowseSort::Name);
    CHECK(has_issue(issues, Kind::BadValue, "library_sort"));
}

TEST_CASE("learn settings: the picker, the scale last looked at and the hands, saved and kept in "
          "range") {
    const AppSettings d;
    CHECK(d.learn_picker == LearnPicker::Ring);
    CHECK(d.learn_root == 0);
    CHECK(d.learn_scale == ScaleType::Major);
    CHECK(d.learn_hands == LearnHands::Both);

    AppSettings a;
    a.learn_picker = LearnPicker::Keys;
    a.learn_root = 6;
    a.learn_scale = ScaleType::HarmonicMinor;
    a.learn_hands = LearnHands::Left;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("learn_picker = keys\n") != std::string::npos);
    CHECK(text.find("learn_root = 6\n") != std::string::npos);
    CHECK(text.find("learn_scale = harmonic_minor\n") != std::string::npos);
    CHECK(text.find("learn_hands = left\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app(text, &issues) == a);
    CHECK(issues.empty());
    // Every scale type has a name and comes back as itself.
    for (const ScaleType type : kAllScaleTypes) {
        AppSettings each;
        each.learn_scale = type;
        SettingsDocument one = SettingsDocument::parse("");
        write_app_settings(one, each);
        CHECK(read_app(one.serialize(), &issues).learn_scale == type);
    }
    // The chord card's chord comes back as itself, for every quality and inversion.
    AppSettings chord;
    chord.learn_chord_root = 3;
    chord.learn_chord = ChordQuality::HalfDiminished7;
    chord.learn_inversion = 2;
    SettingsDocument chord_doc = SettingsDocument::parse("");
    write_app_settings(chord_doc, chord);
    const std::string chord_text = chord_doc.serialize();
    CHECK(chord_text.find("learn_chord_root = 3\n") != std::string::npos);
    CHECK(chord_text.find("learn_chord = half_diminished7\n") != std::string::npos);
    CHECK(chord_text.find("learn_inversion = 2\n") != std::string::npos);
    CHECK(read_app(chord_text, &issues) == chord);
    for (int q = 1; q <= static_cast<int>(ChordQuality::Dominant13); ++q) {
        AppSettings each;
        each.learn_chord = static_cast<ChordQuality>(q);
        SettingsDocument one = SettingsDocument::parse("");
        write_app_settings(one, each);
        CHECK(read_app(one.serialize(), &issues).learn_chord == each.learn_chord);
    }
    CHECK(AppSettings{}.learn_chord == ChordQuality::Major);
    CHECK(read_app("[app]\nlearn_chord = none\n", &issues).learn_chord == ChordQuality::Major);
    CHECK(read_app("[app]\nlearn_inversion = 9\n", &issues).learn_inversion == 4);
    CHECK(read_app("[app]\nlearn_chord_root = 12\n", &issues).learn_chord_root == 11);
    issues.clear();
    CHECK(read_app("[app]\nlearn_chord = polka\n", &issues).learn_chord == ChordQuality::Major);
    CHECK(has_issue(issues, Kind::BadValue, "learn_chord"));
    issues.clear();
    // A root outside 0..11 is clamped, a word this build does not know keeps the default.
    CHECK(read_app("[app]\nlearn_root = 40\n", &issues).learn_root == 11);
    CHECK(read_app("[app]\nlearn_root = -3\n", &issues).learn_root == 0);
    issues.clear();
    CHECK(read_app("[app]\nlearn_scale = polka\n", &issues).learn_scale == ScaleType::Major);
    CHECK(has_issue(issues, Kind::BadValue, "learn_scale"));
    issues.clear();
    CHECK(read_app("[app]\nlearn_picker = ring2\n", &issues).learn_picker == LearnPicker::Ring);
    CHECK(has_issue(issues, Kind::BadValue, "learn_picker"));
}

TEST_CASE("learn drill settings: tempo, direction, octaves and arpeggio, saved, read back and kept "
          "in range") {
    const AppSettings d;
    CHECK(d.learn_tempo == 80);
    CHECK(d.learn_direction == LearnDirection::Up);
    CHECK(d.learn_octaves == 1);
    CHECK_FALSE(d.learn_arpeggio);
    AppSettings a;
    a.learn_tempo = 125;
    a.learn_direction = LearnDirection::UpDown;
    a.learn_octaves = 2;
    a.learn_arpeggio = true;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("learn_tempo = 125\n") != std::string::npos);
    CHECK(text.find("learn_direction = up_down\n") != std::string::npos);
    CHECK(text.find("learn_octaves = 2\n") != std::string::npos);
    CHECK(text.find("learn_arpeggio = true\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app(text, &issues) == a);
    CHECK(issues.empty());
    CHECK(read_app("[app]\nlearn_tempo = 5\n", &issues).learn_tempo == 40);
    CHECK(read_app("[app]\nlearn_tempo = 900\n", &issues).learn_tempo == 200);
    CHECK(read_app("[app]\nlearn_octaves = 3\n", &issues).learn_octaves == 2);
    issues.clear();
    CHECK(read_app("[app]\nlearn_direction = sideways\n", &issues).learn_direction ==
          LearnDirection::Up);
    CHECK(has_issue(issues, Kind::BadValue, "learn_direction"));
}

TEST_CASE("melody_added: the songs added to Melodies are saved per profile, read back and kept valid") {
    const AppSettings d;
    CHECK(d.melody_added.empty());
    AppSettings a;
    a.melody_added = "0123456789abcdef:4 00000000000000ff:1";
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("melody_added = 0123456789abcdef:4 00000000000000ff:1\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app(text, &issues) == a);
    CHECK(issues.empty());
    // A text past the room the settings keep for it is not taken.
    const std::string long_text(kMaxMelodyAddedBytes + 1, 'a');
    CHECK(read_app("[app]\nmelody_added = " + long_text + "\n", &issues).melody_added.empty());
}

TEST_CASE("learn ear settings: mode, octave, any octave and the largest interval, saved, read back "
          "and kept in range") {
    const AppSettings d;
    CHECK(d.learn_ear_mode == LearnEarMode::Note);
    CHECK(d.learn_ear_octave == 4);
    CHECK_FALSE(d.learn_ear_any_octave);
    CHECK(d.learn_ear_max == 12);
    AppSettings a;
    a.learn_ear_mode = LearnEarMode::IntervalDown;
    a.learn_ear_octave = 3;
    a.learn_ear_any_octave = true;
    a.learn_ear_max = 7;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("learn_ear_mode = interval_down\n") != std::string::npos);
    CHECK(text.find("learn_ear_octave = 3\n") != std::string::npos);
    CHECK(text.find("learn_ear_any_octave = true\n") != std::string::npos);
    CHECK(text.find("learn_ear_max = 7\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app(text, &issues) == a);
    CHECK(issues.empty());
    CHECK(read_app("[app]\nlearn_ear_octave = 0\n", &issues).learn_ear_octave == 1);
    CHECK(read_app("[app]\nlearn_ear_octave = 9\n", &issues).learn_ear_octave == 6);
    CHECK(read_app("[app]\nlearn_ear_max = 1\n", &issues).learn_ear_max == 2);
    CHECK(read_app("[app]\nlearn_ear_max = 40\n", &issues).learn_ear_max == 12);
    issues.clear();
    CHECK(read_app("[app]\nlearn_ear_mode = chord\n", &issues).learn_ear_mode == LearnEarMode::Note);
    CHECK(has_issue(issues, Kind::BadValue, "learn_ear_mode"));
}

TEST_CASE("badge wall view: category by default, saved, read back, an unknown word keeps the default") {
    CHECK(AppSettings{}.badge_wall_view == BadgeWallView::Category);
    AppSettings a;
    a.badge_wall_view = BadgeWallView::Rows;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("badge_wall_view = rows\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app(text, &issues) == a);
    CHECK(issues.empty());
    CHECK(read_app("[app]\nbadge_wall_view = shelf\n", &issues).badge_wall_view == BadgeWallView::Category);
    CHECK(has_issue(issues, Kind::BadValue, "badge_wall_view"));
}

TEST_CASE("runs chart view: bars by default, saved, read back, an unknown word keeps the default") {
    CHECK(AppSettings{}.runs_chart_view == RunsChartView::Bars);
    for (const RunsChartView view : {RunsChartView::Line, RunsChartView::Ranked}) {
        AppSettings a;
        a.runs_chart_view = view;
        SettingsDocument doc = SettingsDocument::parse("");
        write_app_settings(doc, a);
        const std::string text = doc.serialize();
        CHECK(text.find(view == RunsChartView::Line ? "runs_chart_view = line\n"
                                                    : "runs_chart_view = ranked\n") != std::string::npos);
        std::vector<SettingsIssue> issues;
        CHECK(read_app(text, &issues) == a);
        CHECK(issues.empty());
    }
    std::vector<SettingsIssue> issues;
    CHECK(read_app("[app]\nruns_chart_view = pie\n", &issues).runs_chart_view == RunsChartView::Bars);
    CHECK(has_issue(issues, Kind::BadValue, "runs_chart_view"));
}

TEST_CASE("accompaniment settings: on and full by default, saved, read back and kept in range") {
    const AppSettings d;
    CHECK(d.accompaniment);
    CHECK(d.accompaniment_volume == kDefaultAccompanimentVolume);
    CHECK(kDefaultAccompanimentVolume == 100);

    AppSettings a;
    a.accompaniment = false;
    a.accompaniment_volume = 35;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const std::string text = doc.serialize();
    CHECK(text.find("accompaniment = false\n") != std::string::npos);
    CHECK(text.find("accompaniment_volume = 35\n") != std::string::npos);
    std::vector<SettingsIssue> issues;
    CHECK(read_app_settings(SettingsDocument::parse(text), &issues) == a);
    CHECK(issues.empty());

    a.accompaniment_volume = 400;
    CHECK(a.clamped().accompaniment_volume == kMaxAccompanimentVolume);
    a.accompaniment_volume = -3;
    CHECK(a.clamped().accompaniment_volume == 0);
    // A file from before the setting keeps the defaults and reports nothing.
    issues.clear();
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nhit_window_ms = 90\n"), &issues)
              .accompaniment);
    CHECK(issues.empty());
}
