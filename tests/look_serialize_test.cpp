// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/look.hpp"

#include <random>
#include <string>
#include <utility>

#include <doctest/doctest.h>

#include "sample_looks.hpp"

#include "etude/color.hpp"
#include "etude/note_style.hpp"
#include "etude/palette.hpp"

using namespace etude;
using namespace etude::test;

namespace {

// A settings value where every field differs from the default.
LookSettings everything_changed() {
    LookSettings s;
    s.note_style = NoteStyle::Neon;
    s.style_intensity = 0.37f;
    s.color_scheme = ColorScheme::ByVelocity;
    s.left_color = {1, 2, 3, 255};
    s.right_color = {250, 251, 252, 128};
    s.unknown_color = {9, 99, 199, 255};
    for (std::size_t i = 0; i < s.pitch_colors.size(); ++i) {
        s.pitch_colors[i] =
            RGBA{static_cast<std::uint8_t>(i * 20), static_cast<std::uint8_t>(255 - i),
                 static_cast<std::uint8_t>(i), 255};
    }
    s.single_color = {17, 34, 51, 255};
    s.saved_palettes[1].filled = true;
    for (std::size_t i = 0; i < s.saved_palettes[1].colors.size(); ++i) {
        s.saved_palettes[1].colors[i] = RGBA{static_cast<std::uint8_t>(5 + i), 60,
                                             static_cast<std::uint8_t>(250 - i), 255};
    }
    s.note_slot = 1;
    s.high_contrast = true;
    s.name_mode = NameMode::Solfege;
    s.names_show_octave = true;
    s.names_spelling = NameSpelling::Flats;
    s.names_on_blocks = false;
    s.names_on_keys = true;
    s.c_markers = false;
    s.octave_numbering = OctaveNumbering::Yamaha;
    s.key_size = KeySize::Large;
    s.show_typing_keys = true;
    s.key_flash = false;
    s.held_glow = false;
    s.hit_effect = HitEffect::Ripple;
    s.trail_fade = false;
    s.combo_number = false;
    s.rising_in_song = true;
    s.background = BackgroundStyle::Motion;
    s.reduced_effects = true;
    s.chord_label = false;
    s.show_key = false;
    s.chord_label_size = 1.7f;
    s.visible_seconds = 7.3f;
    s.keyboard_height = 0.2345f;
    s.grade_words = false;
    s.grade_position = GradePosition::Top;
    s.grade_opacity = 37;
    s.black_key_names = BlackKeyNames::Always;
    s.learn_picks = {LearnShade::Lime, LearnShade::Jade, LearnShade::Magenta, LearnShade::Coral};
    return s;
}

bool has_issue(const ParseReport& r, ParseIssue::Kind kind, const std::string& key) {
    for (const ParseIssue& i : r.issues) {
        if (i.kind == kind && i.key == key) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("every sample look survives a text round trip exactly") {
    for (Sample id : kAllSamples) {
        const LookSettings p = sample_look(id);
        ParseReport report;
        const LookSettings back = parse_look(serialize(p), &report);
        CHECK(back == p);
        CHECK(report.clean());
        CHECK(report.version == kLookFormatVersion);
    }
}

TEST_CASE("a settings value with every field changed round-trips exactly") {
    const LookSettings s = everything_changed();
    REQUIRE(s.is_valid());
    for (Sample id : kAllSamples) {
        REQUIRE_FALSE(s == sample_look(id));
    }
    ParseReport report;
    CHECK(parse_look(serialize(s), &report) == s);
    CHECK(report.clean());
}

TEST_CASE("floats round-trip bit for bit") {
    LookSettings s;
    s.style_intensity = 0.1f;
    s.visible_seconds = 3.14159274f;
    s.keyboard_height = 0.123456789f;
    s.chord_label_size = 1.0000001f;
    CHECK(parse_look(serialize(s)) == s);
}

TEST_CASE("the text starts with a version line and uses key = value") {
    const std::string text = serialize(LookSettings{});
    CHECK(text.rfind("version = 1\n", 0) == 0);
    CHECK(text.find("note_style = glass\n") != std::string::npos);
    CHECK(text.find("right_color = #5AAAFF\n") != std::string::npos);
    CHECK(text.find("name_mode = letters\n") != std::string::npos);
    CHECK(text.find("background = gradient\n") != std::string::npos);
    CHECK(text.find("high_contrast = false\n") != std::string::npos);
    CHECK(text.back() == '\n');
}

TEST_CASE("empty text and missing keys keep the defaults") {
    ParseReport report;
    CHECK(parse_look("", &report) == LookSettings{});
    CHECK(report.clean());
    CHECK(report.applied == 0);
    CHECK(report.version == 0);

    const LookSettings s = parse_look("version = 1\nnote_style = ghost\n", &report);
    LookSettings expected;
    expected.note_style = NoteStyle::Outline; // ghost is read as Outline
    CHECK(s == expected);
    CHECK(report.clean());
    CHECK(report.applied == 2);
}

TEST_CASE("the key label setting: on by default, saved with the look, kept by a reset") {
    CHECK(LookSettings{}.show_key);
    LookSettings off;
    off.show_key = false;
    const std::string text = serialize(off);
    CHECK(text.find("show_key = false\n") != std::string::npos);
    CHECK(parse_look(text) == off);
    CHECK(serialize(LookSettings{}).find("show_key = true\n") != std::string::npos);
    // A look saved before the setting existed shows the key, and that is no error.
    ParseReport report;
    CHECK(parse_look("version = 1\nname_mode = solfege\n", &report).show_key);
    CHECK(report.clean());
    // A bad value is reported and the default stays.
    CHECK(parse_look("show_key = maybe\n", &report).show_key);
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "show_key"));
    // A user slot carries its own switch.
    LookDocument doc;
    doc.user_presets.save_slot(0, "No key", off);
    CHECK_FALSE(parse_look_document(serialize_document(doc)).user_presets.slots[0].look.show_key);
    for (const Sample id : kAllSamples) {
        CHECK(sample_look(id).show_key);
    }
}

TEST_CASE("black key spelling: By key by default, saved, and the old switch keeps its choice") {
    CHECK(LookSettings{}.names_spelling == NameSpelling::ByKey);
    for (const NameSpelling sp : {NameSpelling::ByKey, NameSpelling::Sharps, NameSpelling::Flats}) {
        LookSettings s;
        s.names_spelling = sp;
        CHECK(parse_look(serialize(s)) == s);
    }
    LookSettings flats;
    flats.names_spelling = NameSpelling::Flats;
    CHECK(serialize(flats).find("names_spelling = flats\n") != std::string::npos);
    CHECK(serialize(flats).find("names_prefer_flats") == std::string::npos); // no longer written
    // A look saved when this was a switch: on is Flats, off is Sharps (not By key), and it is no error.
    ParseReport report;
    CHECK(parse_look("version = 1\nnames_prefer_flats = true\n", &report).names_spelling ==
          NameSpelling::Flats);
    CHECK(report.clean());
    CHECK(parse_look("version = 1\nnames_prefer_flats = false\n", &report).names_spelling ==
          NameSpelling::Sharps);
    CHECK(report.clean());
    // A look with neither is By key.
    CHECK(parse_look("version = 1\n").names_spelling == NameSpelling::ByKey);
    // The new key wins over a stale old one that comes before it; a bad value is reported.
    CHECK(parse_look("names_prefer_flats = true\nnames_spelling = by_key\n").names_spelling ==
          NameSpelling::ByKey);
    CHECK(parse_look("names_spelling = maybe\n", &report).names_spelling == NameSpelling::ByKey);
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "names_spelling"));
    // A user slot saved with the old switch keeps it too.
    const LookDocument doc = parse_look_document(
        "version = 1\nslot_1_used = true\nslot_1_name = Old\nslot_1_names_prefer_flats = true\n");
    CHECK(doc.user_presets.slots[0].look.names_spelling == NameSpelling::Flats);
}

TEST_CASE("octave numbering and C markers: defaults, text form and a file without them") {
    CHECK(LookSettings{}.octave_numbering == OctaveNumbering::Scientific);
    CHECK(LookSettings{}.c_markers);
    LookSettings s;
    s.octave_numbering = OctaveNumbering::Yamaha;
    s.c_markers = false;
    const std::string text = serialize(s);
    CHECK(text.find("octave_numbering = yamaha\n") != std::string::npos);
    CHECK(text.find("c_markers = false\n") != std::string::npos);
    CHECK(parse_look(text) == s);
    // A file saved before these settings existed keeps the defaults and is not an error.
    ParseReport report;
    const LookSettings old = parse_look("version = 1\nname_mode = solfege\n", &report);
    CHECK(old.octave_numbering == OctaveNumbering::Scientific);
    CHECK(old.c_markers);
    CHECK(report.clean());
    // A bad value is reported and the default stays.
    const LookSettings bad = parse_look("octave_numbering = roland\n", &report);
    CHECK(bad.octave_numbering == OctaveNumbering::Scientific);
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "octave_numbering"));
    // Each user slot carries its own numbering.
    LookDocument doc;
    doc.user_presets.save_slot(1, "Yamaha", s);
    CHECK(parse_look_document(serialize_document(doc)) == doc);
}

TEST_CASE("unknown keys are ignored and reported") {
    ParseReport report;
    const LookSettings s =
        parse_look("version = 1\nsparkle_factor = 11\nnote_style = neon\n", &report);
    CHECK(s.note_style == NoteStyle::Neon);
    CHECK(has_issue(report, ParseIssue::Kind::UnknownKey, "sparkle_factor"));
    CHECK(report.issues.size() == 1);
    CHECK(report.issues[0].line == 2);
}

TEST_CASE("bad values are ignored and reported, the setting keeps its default") {
    ParseReport report;
    const LookSettings s = parse_look("note_style = sparkly\n"
                                      "style_intensity = lots\n"
                                      "right_color = blue\n"
                                      "sparks = maybe\n"
                                      "visible_seconds = \n"
                                      "pitch_color_3 = #12\n"
                                      "color_scheme = by-hand\n",
                                      &report);
    CHECK(s == LookSettings{});
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "note_style"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "style_intensity"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "right_color"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "sparks"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "visible_seconds"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "pitch_color_3"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "color_scheme"));
    CHECK(report.applied == 0);
}

TEST_CASE("numbers outside their range are clamped and reported") {
    ParseReport report;
    const LookSettings s =
        parse_look("visible_seconds = 500\nkeyboard_height = -2\nstyle_intensity = 0.5\n", &report);
    CHECK(s.visible_seconds == kMaxVisibleSeconds);
    CHECK(s.keyboard_height == kMinKeyboardHeight);
    CHECK(s.style_intensity == doctest::Approx(0.5f));
    CHECK(s.is_valid());
    CHECK(has_issue(report, ParseIssue::Kind::OutOfRange, "visible_seconds"));
    CHECK(has_issue(report, ParseIssue::Kind::OutOfRange, "keyboard_height"));
    CHECK(report.applied == 3);
}

TEST_CASE("nan and inf are bad values, not numbers") {
    ParseReport report;
    const LookSettings s = parse_look("style_intensity = nan\nvisible_seconds = inf\n", &report);
    CHECK(s == LookSettings{});
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "style_intensity"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "visible_seconds"));
}

TEST_CASE("lines without key = value are reported, comments and blanks are not") {
    ParseReport report;
    parse_look("# my look\n\n   \nversion = 1\nthis is nonsense\n  = 5\n", &report);
    REQUIRE(report.issues.size() == 2);
    CHECK(report.issues[0].kind == ParseIssue::Kind::BadLine);
    CHECK(report.issues[0].line == 5);
    CHECK(report.issues[1].kind == ParseIssue::Kind::BadLine);
    CHECK(report.issues[1].line == 6);
}

TEST_CASE("whitespace, CRLF line ends and a missing final newline are fine") {
    ParseReport report;
    const LookSettings s =
        parse_look("  note_style=filled  \r\n\tsparks =\tfalse\r\nright_color = #010203", &report);
    CHECK(s.note_style == NoteStyle::Solid);
    CHECK(s.hit_effect == HitEffect::None);
    CHECK(s.right_color == RGBA{1, 2, 3, 255});
    CHECK(report.clean());
}

TEST_CASE("a later line for the same key wins") {
    CHECK(parse_look("note_style = ghost\nnote_style = shiny\n").note_style == NoteStyle::Candy);
    CHECK(parse_look("note_style = pixel\nnote_style = ink\n").note_style == NoteStyle::Ink);
}

TEST_CASE("a newer format version is reported but still read") {
    ParseReport report;
    const LookSettings s =
        parse_look("version = 99\nnote_style = ghost\nfuture_key = 1\n", &report);
    CHECK(s.note_style == NoteStyle::Outline);
    CHECK(report.version == 99);
    CHECK(has_issue(report, ParseIssue::Kind::NewerVersion, "version"));
    CHECK(has_issue(report, ParseIssue::Kind::UnknownKey, "future_key"));
}

TEST_CASE("a bad version line is a bad value") {
    ParseReport report;
    parse_look("version = two\n", &report);
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "version"));
    CHECK(report.version == 0);
}

TEST_CASE("parsing never throws and always returns valid settings, whatever the input") {
    std::mt19937 rng(12345);
    const std::string base = serialize(everything_changed());
    for (int round = 0; round < 300; ++round) {
        std::string text = base;
        const int edits = 1 + static_cast<int>(rng() % 20);
        for (int e = 0; e < edits; ++e) {
            const std::size_t pos = rng() % (text.size() + 1);
            switch (rng() % 3) {
            case 0:
                text.insert(pos, 1, static_cast<char>(rng() % 256));
                break;
            case 1:
                if (pos < text.size()) {
                    text.erase(pos, 1 + rng() % 8);
                }
                break;
            default:
                if (pos < text.size()) {
                    text[pos] = static_cast<char>(rng() % 256);
                }
                break;
            }
        }
        ParseReport report;
        LookSettings parsed;
        CHECK_NOTHROW(parsed = parse_look(text, &report));
        CHECK(parsed.is_valid());
    }
    std::string noise(4096, '\0');
    for (char& c : noise) {
        c = static_cast<char>(rng() % 256);
    }
    CHECK(parse_look(noise).is_valid());
    CHECK(parse_look(std::string(100000, '=')).is_valid());
}

TEST_CASE("key size defaults to fit keyboard and round trips as text") {
    CHECK(LookSettings{}.key_size == KeySize::FitKeyboard);
    CHECK_FALSE(LookSettings{}.show_typing_keys);
    for (KeySize size : {KeySize::FitKeyboard, KeySize::FitSong, KeySize::Small, KeySize::Medium,
                         KeySize::Large, KeySize::ExtraLarge}) {
        LookSettings s;
        s.key_size = size;
        ParseReport report;
        CHECK(parse_look(serialize(s), &report).key_size == size);
        CHECK(report.clean());
    }
    LookSettings s;
    s.key_size = KeySize::ExtraLarge;
    CHECK(serialize(s).find("key_size = extra_large\n") != std::string::npos);
}

TEST_CASE("a file without key_size keeps fit keyboard, a bad value is reported") {
    CHECK(parse_look("style_intensity = 0.5\n").key_size == KeySize::FitKeyboard);
    ParseReport report;
    const LookSettings bad = parse_look("key_size = huge\n", &report);
    CHECK(bad.key_size == KeySize::FitKeyboard);
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "key_size"));
}

TEST_CASE("a stray key size value is brought back to the default") {
    LookSettings s;
    s.key_size = static_cast<KeySize>(9);
    CHECK_FALSE(s.is_valid());
    CHECK(s.clamped().key_size == KeySize::FitKeyboard);
}

TEST_CASE("grade words default to on, at the bottom, fully opaque") {
    const LookSettings d;
    CHECK(d.grade_words);
    CHECK(d.grade_position == GradePosition::Bottom);
    CHECK(d.grade_opacity == 100);
    CHECK(d.grade_words_shown());
    CHECK(d.grade_alpha() == doctest::Approx(1.0f));
    // A file from before the grade options keeps the defaults.
    const LookSettings old = parse_look("style_intensity = 0.5\n");
    CHECK(old.grade_words);
    CHECK(old.grade_position == GradePosition::Bottom);
    CHECK(old.grade_opacity == 100);
}

TEST_CASE("grade words are written under their names and round trip") {
    LookSettings s;
    s.grade_words = false;
    s.grade_position = GradePosition::Top;
    s.grade_opacity = 40;
    const std::string text = serialize(s);
    CHECK(text.find("grade_words = false\n") != std::string::npos);
    CHECK(text.find("grade_position = top\n") != std::string::npos);
    CHECK(text.find("grade_opacity = 40\n") != std::string::npos);
    ParseReport report;
    CHECK(parse_look(text, &report) == s);
    CHECK(report.clean());

    UserPresets presets;
    presets.save_slot(1, "Words", s);
    LookDocument doc{LookSettings{}, presets};
    CHECK(parse_look_document(serialize_document(doc)).user_presets.slots[1].look == s);
}

TEST_CASE("grade opacity outside 0..100 is clamped and reported") {
    ParseReport report;
    CHECK(parse_look("grade_opacity = 250\n", &report).grade_opacity == 100);
    CHECK(has_issue(report, ParseIssue::Kind::OutOfRange, "grade_opacity"));
    CHECK(parse_look("grade_opacity = -5\n").grade_opacity == 0);
    CHECK(parse_look("grade_opacity = 99999999999999999999\n").grade_opacity == 100);
    CHECK(parse_look("grade_opacity = -99999999999999999999\n").grade_opacity == 0);
    LookSettings s;
    s.grade_opacity = 400;
    CHECK_FALSE(s.is_valid());
    CHECK(s.clamped().grade_opacity == 100);
    s.grade_opacity = -1;
    CHECK(s.clamped().grade_opacity == 0);
}

TEST_CASE("unknown grade values are reported and keep the defaults") {
    ParseReport report;
    const LookSettings s =
        parse_look("grade_words = maybe\ngrade_position = middle\ngrade_opacity = lots\n", &report);
    CHECK(s.grade_words);
    CHECK(s.grade_position == GradePosition::Bottom);
    CHECK(s.grade_opacity == 100);
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "grade_words"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "grade_position"));
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "grade_opacity"));
    CHECK(parse_look("grade_opacity = 12.5\n").grade_opacity == 100); // whole percent only
    LookSettings stray;
    stray.grade_position = static_cast<GradePosition>(7);
    CHECK_FALSE(stray.is_valid());
    CHECK(stray.clamped().grade_position == GradePosition::Bottom);
}

TEST_CASE("the grade switch and the opacity slider stay consistent") {
    LookSettings s;
    s.grade_opacity = 40;
    // Switch off: hidden, the strength is kept, so on brings it back.
    set_grade_words(s, false);
    CHECK_FALSE(s.grade_words_shown());
    CHECK(s.grade_alpha() == 0.0f);
    CHECK(s.grade_opacity == 40);
    set_grade_words(s, true);
    CHECK(s.grade_words_shown());
    CHECK(s.grade_opacity == 40);
    // Slider to 0: off. The remembered strength goes back as the stored one.
    set_grade_opacity(s, 0, 40);
    CHECK_FALSE(s.grade_words);
    CHECK_FALSE(s.grade_words_shown());
    CHECK(s.grade_opacity == 40);
    // Slider up from 0 switches on at that value.
    set_grade_opacity(s, 65, 40);
    CHECK(s.grade_words);
    CHECK(s.grade_opacity == 65);
    CHECK(s.grade_alpha() == doctest::Approx(0.65f));
    // Opacity 0 with the switch on shows nothing, and the switch restores the last strength.
    s.grade_words = true;
    s.grade_opacity = 0;
    CHECK_FALSE(s.grade_words_shown());
    set_grade_words(s, true, 30);
    CHECK(s.grade_words_shown());
    CHECK(s.grade_opacity == 30);
    // Nothing known to restore: full strength; out-of-range input is clamped.
    s.grade_opacity = 0;
    set_grade_words(s, true);
    CHECK(s.grade_opacity == 100);
    set_grade_opacity(s, 500);
    CHECK(s.grade_opacity == 100);
    set_grade_opacity(s, -3, 0);
    CHECK_FALSE(s.grade_words);
    CHECK(s.grade_opacity == 1);
}

TEST_CASE("sample looks have grade words that are valid and shown; reset keeps the player's") {
    for (Sample id : kAllSamples) {
        const LookSettings p = sample_look(id);
        CHECK(p.is_valid());
        CHECK(p.grade_words_shown());
        CHECK(p.grade_opacity >= 40);
    }
    CHECK(LookSettings{}.grade_opacity == 100);
    // Resetting keeps the player's grade-word choices.
    LookSettings mine;
    mine.grade_words = false;
    mine.grade_position = GradePosition::Top;
    mine.grade_opacity = 35;
    const LookSettings applied = reset_look(mine);
    CHECK_FALSE(applied.grade_words);
    CHECK(applied.grade_position == GradePosition::Top);
    CHECK(applied.grade_opacity == 35);
}

TEST_CASE("note names on black keys default to Auto and round trip") {
    CHECK(LookSettings{}.black_key_names == BlackKeyNames::Auto);
    // A file from before the option keeps the default.
    CHECK(parse_look("style_intensity = 0.5\n").black_key_names == BlackKeyNames::Auto);
    for (BlackKeyNames mode : {BlackKeyNames::Auto, BlackKeyNames::Always, BlackKeyNames::Off}) {
        LookSettings s;
        s.black_key_names = mode;
        const std::string text = serialize(s);
        CHECK(text.find("black_key_names = ") != std::string::npos);
        CHECK(parse_look(text).black_key_names == mode);
    }
    CHECK(parse_look("black_key_names = off\n").black_key_names == BlackKeyNames::Off);
    // A word we do not know leaves the default and is reported.
    ParseReport report;
    CHECK(parse_look("black_key_names = sometimes\n", &report).black_key_names ==
          BlackKeyNames::Auto);
    CHECK_FALSE(report.clean());
}

TEST_CASE("a stray black key names value is brought back to Auto") {
    LookSettings s;
    s.black_key_names = static_cast<BlackKeyNames>(7);
    CHECK_FALSE(s.is_valid());
    CHECK(s.clamped().black_key_names == BlackKeyNames::Auto);
}

TEST_CASE("a reset keeps the player's black key names choice") {
    LookSettings current = sample_look(Sample::Glass);
    current.black_key_names = BlackKeyNames::Off;
    CHECK(reset_look(current).black_key_names == BlackKeyNames::Off);
}

TEST_CASE("the block layout defaults to Key width, round trips and survives a reset") {
    CHECK(LookSettings{}.block_layout == BlockLayout::KeyWidth);
    // A look saved before the option gets Key width.
    CHECK(parse_look("style_intensity = 0.5\n").block_layout == BlockLayout::KeyWidth);
    for (BlockLayout mode : {BlockLayout::KeyWidth, BlockLayout::EvenLanes}) {
        LookSettings s;
        s.block_layout = mode;
        const std::string text = serialize(s);
        CHECK(text.find("block_layout = ") != std::string::npos);
        CHECK(parse_look(text).block_layout == mode);
    }
    CHECK(parse_look("block_layout = even_lanes\n").block_layout == BlockLayout::EvenLanes);
    ParseReport report;
    CHECK(parse_look("block_layout = wide\n", &report).block_layout == BlockLayout::KeyWidth);
    CHECK_FALSE(report.clean());
    LookSettings stray;
    stray.block_layout = static_cast<BlockLayout>(5);
    CHECK(stray.clamped().block_layout == BlockLayout::KeyWidth);
    LookSettings current = sample_look(Sample::Glass);
    current.block_layout = BlockLayout::EvenLanes;
    CHECK(reset_look(current).block_layout == BlockLayout::EvenLanes);
}

TEST_CASE("the hit effect is saved by name, and a look from before it keeps its sparks") {
    for (const HitEffect e : kAllHitEffects) {
        LookSettings s;
        s.hit_effect = e;
        ParseReport report;
        CHECK(parse_look(serialize(s), &report).hit_effect == e);
        CHECK(report.clean());
    }
    LookSettings glow;
    CHECK(serialize(glow).find("hit_effect = glow\n") != std::string::npos);
    // The old switch is read, never written.
    CHECK(serialize(glow).find("sparks =") == std::string::npos);
    ParseReport report;
    CHECK(parse_look("sparks = true\n", &report).hit_effect == HitEffect::Sparks);
    CHECK(report.clean());
    CHECK(parse_look("sparks = false\n", &report).hit_effect == HitEffect::None);
    CHECK(report.clean());
    // No sparks key at all: the new default.
    CHECK(parse_look("note_style = filled\n").hit_effect == HitEffect::Glow);
    CHECK(parse_look("hit_effect = fireworks\n", &report).hit_effect == HitEffect::Glow);
    CHECK(has_issue(report, ParseIssue::Kind::BadValue, "hit_effect"));
    // A user slot saved before keeps its sparks too.
    const LookDocument doc = parse_look_document("slot_1_used = true\nslot_1_sparks = true\n");
    CHECK(doc.user_presets.slots[0].look.hit_effect == HitEffect::Sparks);
}

TEST_CASE("the six older note styles load as the styles that replaced them, and save as those") {
    const struct {
        const char* old_name;
        NoteStyle now;
        const char* saved;
    } kMoves[] = {{"filled", NoteStyle::Solid, "solid"},    {"glassy", NoteStyle::Glass, "glass"},
                  {"frosted", NoteStyle::Glass, "glass"},   {"shiny", NoteStyle::Candy, "candy"},
                  {"ghost", NoteStyle::Outline, "outline"}, {"neon", NoteStyle::Neon, "neon"}};
    for (const auto& m : kMoves) {
        CAPTURE(m.old_name);
        ParseReport report;
        const LookSettings s =
            parse_look(std::string("version = 1\nnote_style = ") + m.old_name + "\n", &report);
        CHECK(s.note_style == m.now);
        CHECK(report.clean()); // an old name is not a bad value
        CHECK(serialize(s).find(std::string("note_style = ") + m.saved + "\n") !=
              std::string::npos);
    }
    ParseReport report;
    CHECK(parse_look("note_style = sparkly\n", &report).note_style == LookSettings{}.note_style);
    CHECK_FALSE(report.clean());
}

TEST_CASE("block glow and beat lines: defaults, saved, clamped and read back") {
    const LookSettings d;
    CHECK(d.block_glow == kDefaultBlockGlow);
    CHECK(d.beat_lines == BeatLines::Off);
    LookSettings s;
    s.block_glow = 85;
    s.beat_lines = BeatLines::HalfBeats;
    const std::string text = serialize(s);
    CHECK(text.find("block_glow = 85\n") != std::string::npos);
    CHECK(text.find("beat_lines = half_beats\n") != std::string::npos);
    CHECK(parse_look(text) == s);
    for (const auto& [name, mode] :
         {std::pair{"off", BeatLines::Off}, std::pair{"beats", BeatLines::Beats},
          std::pair{"half_beats", BeatLines::HalfBeats}, std::pair{"bars", BeatLines::Bars}}) {
        CHECK(parse_look(std::string("beat_lines = ") + name + "\n").beat_lines == mode);
    }
    ParseReport report;
    CHECK(parse_look("block_glow = 400\n", &report).block_glow == kMaxBlockGlow);
    CHECK(parse_look("block_glow = -3\n").block_glow == kMinBlockGlow);
    LookSettings bad;
    bad.block_glow = 1000;
    bad.beat_lines = static_cast<BeatLines>(9);
    CHECK_FALSE(bad.is_valid());
    CHECK(bad.clamped().block_glow == kMaxBlockGlow);
    CHECK(bad.clamped().beat_lines == BeatLines::Off);
}

TEST_CASE("sheet music's helps: off by default, letters, their own text form") {
    const LookSettings d;
    CHECK_FALSE(d.names_on_sheet);
    CHECK_FALSE(d.sheet_note_colours);
    CHECK(d.sheet_naming == SheetNaming::Letters);
    CHECK(d.sheet_chords == SheetChords::NoteNames);
    LookSettings s;
    s.names_on_sheet = true;
    s.sheet_naming = SheetNaming::Solfege;
    s.sheet_note_colours = true;
    s.sheet_chords = SheetChords::Symbols;
    const std::string text = serialize(s);
    CHECK(text.find("names_on_sheet = true\n") != std::string::npos);
    CHECK(text.find("sheet_naming = solfege\n") != std::string::npos);
    CHECK(text.find("sheet_chords = symbols\n") != std::string::npos);
    CHECK(text.find("sheet_note_colours = true\n") != std::string::npos);
    CHECK(parse_look(text) == s);
    // The blocks' Do Re Mi does not make the sheet's names Do Re Mi.
    ParseReport report;
    const LookSettings blocks = parse_look("version = 1\nname_mode = solfege\n", &report);
    CHECK(blocks.sheet_naming == SheetNaming::Letters);
    CHECK(report.clean());
}

TEST_CASE("a look saved before notes could rise during a song reads as off") {
    LookSettings s;
    s.rising_in_song = true;
    std::string text = serialize(s);
    const std::size_t at = text.find("rising_in_song");
    REQUIRE(at != std::string::npos);
    text.erase(at, text.find('\n', at) - at + 1);
    ParseReport report;
    CHECK_FALSE(parse_look(text, &report).rising_in_song);
    CHECK(report.clean());
}

TEST_CASE("a settings file that still has the dropped combo rail switch loads clean") {
    ParseReport report;
    const LookSettings look = parse_look("combo_rail = false\ncombo_number = false\n", &report);
    CHECK(report.clean());
    CHECK_FALSE(look.combo_number);
    // And it is not written again.
    CHECK(serialize(look).find("combo_rail") == std::string::npos);
}

TEST_CASE("the Learn sections' shades: teal, pink, coral and violet by default, saved by name") {
    const LookSettings d;
    CHECK(d.learn_picks == kLearnDefaultPicks);
    CHECK(learn_shade_info(d.learn_picks[static_cast<std::size_t>(LearnSection::Scales)]).key == "teal");
    CHECK(learn_shade_info(d.learn_picks[static_cast<std::size_t>(LearnSection::Chords)]).key == "pink");
    CHECK(learn_shade_info(d.learn_picks[static_cast<std::size_t>(LearnSection::Ear)]).key == "coral");
    CHECK(learn_shade_info(d.learn_picks[static_cast<std::size_t>(LearnSection::Melodies)]).key == "violet");

    LookSettings mine;
    mine.learn_picks = {LearnShade::Lime, LearnShade::Lime, LearnShade::Jade, LearnShade::Magenta};
    const std::string text = serialize(mine);
    CHECK(text.find("learn_scales_colour = lime\n") != std::string::npos);
    CHECK(text.find("learn_chords_colour = lime\n") != std::string::npos);
    CHECK(text.find("learn_ear_colour = jade\n") != std::string::npos);
    CHECK(text.find("learn_melodies_colour = magenta\n") != std::string::npos);
    ParseReport report;
    const LookSettings back = parse_look(text, &report);
    CHECK(report.clean());
    CHECK(back == mine); // two sections may share a shade
    CHECK(back.learn_picks == mine.learn_picks);
}

TEST_CASE("a look saved before the Learn colours loads with the default shades, a bad one keeps the "
          "default and says so") {
    ParseReport report;
    const LookSettings old = parse_look("version = 1\nnote_style = neon\nhigh_contrast = true\n", &report);
    CHECK(report.clean());
    CHECK(old.learn_picks == kLearnDefaultPicks);

    const LookSettings bad = parse_look("learn_ear_colour = chartreuse\nlearn_scales_colour = jade\n", &report);
    CHECK_FALSE(report.clean());
    CHECK(bad.learn_picks[static_cast<std::size_t>(LearnSection::Ear)] == LearnShade::Coral);
    CHECK(bad.learn_picks[static_cast<std::size_t>(LearnSection::Scales)] == LearnShade::Jade);
}

TEST_CASE("a saved look slot keeps its Learn shades") {
    UserPresets presets;
    LookSettings mine;
    mine.learn_picks[static_cast<std::size_t>(LearnSection::Chords)] = LearnShade::Teal;
    REQUIRE(presets.save_slot(1, "Night", mine));
    const LookDocument doc{mine, presets};
    const LookDocument back = parse_look_document(serialize_document(doc));
    LookSettings loaded;
    REQUIRE(back.user_presets.load_slot(1, &loaded));
    CHECK(loaded.learn_picks == mine.learn_picks);
}

TEST_CASE("palettes and the octave and velocity colours are saved and read back") {
    LookSettings s;
    s.color_scheme = ColorScheme::ByOctave;
    apply_palette(s, Palette::Vivid);
    s.color_scheme = ColorScheme::ByVelocity;
    apply_palette(s, Palette::Mono);
    set_swatch_color(s, ColorScheme::ByVelocity, 1, {9, 99, 199, 255});
    const std::string text = serialize(s);
    CHECK(text.find("palette_by_octave = vivid\n") != std::string::npos);
    CHECK(text.find("palette_by_velocity = mono\n") != std::string::npos);
    CHECK(text.find("palette_by_hand = classic\n") != std::string::npos);
    CHECK(text.find("velocity_color_1 = #0963C7\n") != std::string::npos);
    CHECK(text.find("octave_color_8 = ") != std::string::npos);
    ParseReport report;
    const LookSettings back = parse_look(text, &report);
    CHECK(report.clean());
    CHECK(back == s);
    CHECK(matching_palette(back, ColorScheme::ByOctave) == Palette::Vivid);
    CHECK_FALSE(matching_palette(back, ColorScheme::ByVelocity).has_value());
    // The reset still knows which palette the changed colours came from.
    LookSettings reset = back;
    reset_to_palette(reset);
    CHECK(matching_palette(reset, ColorScheme::ByVelocity) == Palette::Mono);
}

TEST_CASE("a look saved before palettes reads as the colours it had") {
    // Default colours: Classic everywhere, and the octave and velocity ramps as they were.
    const LookSettings plain = parse_look("version = 1\ncolor_scheme = by_octave\n");
    CHECK(plain.octave_colors == default_octave_colors());
    CHECK(plain.velocity_colors == default_velocity_colors());
    for (const ColorScheme scheme : kAllColorSchemes) {
        CHECK(plain.palettes[static_cast<std::size_t>(scheme)] == Palette::Classic);
    }

    // Own hand colours: Custom, with Classic to reset to.
    ParseReport report;
    const LookSettings own =
        parse_look("version = 1\nleft_color = #112233\nright_color = #445566\n", &report);
    CHECK(report.clean());
    CHECK(own.left_color == RGBA{0x11, 0x22, 0x33, 255});
    CHECK_FALSE(matching_palette(own, ColorScheme::ByHand).has_value());
    CHECK(own.palettes[0] == Palette::Classic);

    // The Pastel notes, saved before palettes: shown as Pastel.
    LookSettings frosted = sample_look(Sample::FrostedPastel);
    std::string old_text;
    for (std::string_view line : {std::string_view("version = 1\n")}) {
        old_text += line;
    }
    const std::string full = serialize(frosted);
    std::size_t pos = 0;
    while (pos < full.size()) {
        const std::size_t eol = full.find('\n', pos);
        const std::string line = full.substr(pos, eol - pos + 1);
        pos = eol + 1;
        if (line.rfind("palette_", 0) != 0 && line.rfind("octave_color_", 0) != 0 &&
            line.rfind("velocity_color_", 0) != 0 && line.rfind("version", 0) != 0) {
            old_text += line;
        }
    }
    const LookSettings read = parse_look(old_text);
    CHECK(read.palettes[static_cast<std::size_t>(ColorScheme::ByPitchClass)] == Palette::Pastel);
    CHECK(read == frosted);
}

TEST_CASE("a bad palette name is reported and the palette comes from the colours") {
    ParseReport report;
    const LookSettings s = parse_look("version = 1\npalette_by_hand = rainbow\n", &report);
    REQUIRE(report.issues.size() == 1);
    CHECK(report.issues[0].kind == ParseIssue::Kind::BadValue);
    CHECK(report.issues[0].key == "palette_by_hand");
    CHECK(s.palettes[0] == Palette::Classic);
    // A stray value in memory is brought back to Classic.
    LookSettings stray;
    stray.palettes[2] = static_cast<Palette>(99);
    CHECK_FALSE(stray.is_valid());
    CHECK(stray.clamped().palettes[2] == Palette::Classic);
}

TEST_CASE("a malformed octave or velocity colour keeps the default and is named in the report") {
    const LookSettings defaults;
    ParseReport report;
    const LookSettings got = parse_look("version = 1\noctave_color_3 = banana\n"
                                        "octave_color_9 = #123456\nvelocity_color_1 = #12\n",
                                        &report);
    CHECK(got.octave_colors == defaults.octave_colors);
    CHECK(got.velocity_colors == defaults.velocity_colors);
    CHECK_FALSE(report.clean());
    const auto named = [&](const char* key) {
        std::string all;
        for (const auto& issue : report.issues) {
            all += issue.key + "\n";
        }
        return all.find(key) != std::string::npos;
    };
    CHECK(named("octave_color_3"));
    CHECK(named("octave_color_9"));
    CHECK(named("velocity_color_1"));
}

TEST_CASE("a settings file that names the removed colour-blind palette loads as Classic") {
    ParseReport report;
    const LookSettings got = parse_look("version = 1\ncolor_scheme = by_pitch_class\n"
                                        "palette_by_hand = colour_blind\n"
                                        "palette_by_pitch_class = colour_blind\n"
                                        "palette_single = vivid\n",
                                        &report);
    for (const ColorScheme scheme : {ColorScheme::ByHand, ColorScheme::ByPitchClass}) {
        CHECK(got.palettes[static_cast<std::size_t>(scheme)] == Palette::Classic);
    }
    CHECK(got.palettes[static_cast<std::size_t>(ColorScheme::Single)] == Palette::Vivid);
    CHECK(report.clean());
    // The same in a saved look slot.
    LookDocument doc;
    doc.user_presets.save_slot(1, "Old", LookSettings{});
    std::string text = serialize_document(doc);
    const std::string key = "slot_2_palette_by_octave = classic\n";
    const std::size_t at = text.find(key);
    REQUIRE(at != std::string::npos);
    text.replace(at, key.size(), "slot_2_palette_by_octave = colour_blind\n");
    ParseReport slot_report;
    const LookDocument back = parse_look_document(text, &slot_report);
    CHECK(back.user_presets.slots[1].look.palettes[static_cast<std::size_t>(
              ColorScheme::ByOctave)] == Palette::Classic);
    CHECK(slot_report.clean());
}

TEST_CASE("names round-trip, the six older names read as their new style, unknown are refused") {
    for (NoteStyle s : kAllNoteStyles) {
        NoteStyle back = NoteStyle::Solid;
        CHECK(parse_note_style(note_style_name(s), &back));
        CHECK(back == s);
    }
    const struct {
        const char* old_name;
        NoteStyle now;
    } kMoves[] = {{"filled", NoteStyle::Solid},  {"glassy", NoteStyle::Glass},
                  {"frosted", NoteStyle::Glass}, {"shiny", NoteStyle::Candy},
                  {"ghost", NoteStyle::Outline}, {"neon", NoteStyle::Neon}};
    for (const auto& m : kMoves) {
        CAPTURE(m.old_name);
        NoteStyle got = NoteStyle::Ink;
        CHECK(parse_note_style(m.old_name, &got));
        CHECK(got == m.now);
    }
    NoteStyle untouched = NoteStyle::Soft;
    CHECK_FALSE(parse_note_style("sparkly", &untouched));
    CHECK_FALSE(legacy_note_style("solid", &untouched)); // a current name is not an old one
    CHECK(untouched == NoteStyle::Soft);
}

TEST_CASE("hex text round-trips and rejects bad input") {
    CHECK(to_hex(RGBA{90, 170, 255, 255}) == "#5AAAFF");
    CHECK(to_hex(RGBA{1, 2, 3, 4}) == "#01020304");
    RGBA out;
    CHECK(parse_hex("#5AAAFF", &out));
    CHECK(out == RGBA{90, 170, 255, 255});
    CHECK(parse_hex("#5aaaff80", &out));
    CHECK(out == RGBA{90, 170, 255, 128});
    CHECK_FALSE(parse_hex("5AAAFF", &out));
    CHECK_FALSE(parse_hex("#5AAAF", &out));
    CHECK_FALSE(parse_hex("#5AAAFG", &out));
    CHECK_FALSE(parse_hex("", &out));
    CHECK_FALSE(parse_hex("#5AAAFF00FF", &out));
}
