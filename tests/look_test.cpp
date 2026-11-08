// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/look.hpp"

#include <limits>
#include <string>

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("defaults are valid and match the documented values") {
    const LookSettings d;
    CHECK(d.is_valid());
    CHECK(d.note_style == NoteStyle::Glass);
    CHECK(d.style_intensity == doctest::Approx(0.6f));
    CHECK(d.color_scheme == ColorScheme::ByHand);
    CHECK(d.right_color == RGBA{90, 170, 255, 255}); // the app's accent
    CHECK_FALSE(d.high_contrast);
    CHECK(d.name_mode == NameMode::Letters);
    CHECK(d.names_on_blocks);
    CHECK_FALSE(d.names_on_keys);
    CHECK_FALSE(d.names_show_octave);
    CHECK(d.c_markers);
    CHECK(d.octave_numbering == OctaveNumbering::Scientific);
    CHECK(d.names_spelling == NameSpelling::ByKey); // black keys spelled by the key
    CHECK(d.background == BackgroundStyle::Gradient);
    CHECK_FALSE(d.reduced_effects);
    CHECK(d.chord_label);
    CHECK(d.show_key); // the key label is on by default
    CHECK(d.chord_label_size == doctest::Approx(1.0f));
    CHECK(d.visible_seconds == doctest::Approx(4.0f));
    CHECK(d.keyboard_height == doctest::Approx(0.18f));
    CHECK(d.pitch_colors == default_pitch_colors());
    CHECK(kBackgroundColor == RGBA{18, 20, 26, 255}); // what the app clears the screen to
}

TEST_CASE("clamped brings every number back into its range") {
    LookSettings s;
    s.style_intensity = 7.0f;
    s.chord_label_size = 0.0f;
    s.visible_seconds = 1000.0f;
    s.keyboard_height = -1.0f;
    CHECK_FALSE(s.is_valid());
    const LookSettings c = s.clamped();
    CHECK(c.is_valid());
    CHECK(c.style_intensity == kMaxStyleIntensity);
    CHECK(c.chord_label_size == kMinChordLabelSize);
    CHECK(c.visible_seconds == kMaxVisibleSeconds);
    CHECK(c.keyboard_height == kMinKeyboardHeight);
    // Untouched fields stay.
    CHECK(c.note_style == s.note_style);
    CHECK(c.right_color == s.right_color);
}

TEST_CASE("clamped leaves in-range values alone and validate() clamps in place") {
    LookSettings s;
    s.visible_seconds = kMinVisibleSeconds;
    s.keyboard_height = kMaxKeyboardHeight;
    CHECK(s.is_valid());
    CHECK(s.clamped() == s);
    s.visible_seconds = 99.0f;
    s.validate();
    CHECK(s.visible_seconds == kMaxVisibleSeconds);
    CHECK(s.is_valid());
}

TEST_CASE("NaN and infinity fall back to the default or the nearest end") {
    LookSettings s;
    s.style_intensity = std::numeric_limits<float>::quiet_NaN();
    s.visible_seconds = std::numeric_limits<float>::infinity();
    s.keyboard_height = -std::numeric_limits<float>::infinity();
    CHECK_FALSE(s.is_valid());
    const LookSettings c = s.clamped();
    CHECK(c.is_valid());
    CHECK(c.style_intensity == LookSettings{}.style_intensity);
    CHECK(c.visible_seconds == kMaxVisibleSeconds);
    CHECK(c.keyboard_height == kMinKeyboardHeight);
}

TEST_CASE("a stray enum value is reset to its default") {
    LookSettings s;
    s.note_style = static_cast<NoteStyle>(200);
    s.color_scheme = static_cast<ColorScheme>(99);
    s.name_mode = static_cast<NameMode>(9);
    s.background = static_cast<BackgroundStyle>(77);
    s.octave_numbering = static_cast<OctaveNumbering>(5);
    CHECK_FALSE(s.is_valid());
    const LookSettings c = s.clamped();
    CHECK(c.is_valid());
    CHECK(c.note_style == LookSettings{}.note_style);
    CHECK(c.color_scheme == LookSettings{}.color_scheme);
    CHECK(c.name_mode == LookSettings{}.name_mode);
    CHECK(c.background == LookSettings{}.background);
    CHECK(c.octave_numbering == LookSettings{}.octave_numbering);
}

TEST_CASE("reduced effects forces motion and the hit effect off, and only those") {
    LookSettings s;
    s.background = BackgroundStyle::Motion;
    s.hit_effect = HitEffect::Sparks;
    CHECK(s.effective_background() == BackgroundStyle::Motion);
    CHECK(s.effective_hit_effect() == HitEffect::Sparks);

    s.reduced_effects = true;
    CHECK(s.effective_background() == BackgroundStyle::Gradient);
    for (const HitEffect e : kAllHitEffects) {
        s.hit_effect = e;
        CHECK(s.effective_hit_effect() == HitEffect::None);
    }
    s.hit_effect = HitEffect::Sparks;

    s.background = BackgroundStyle::Plain;
    CHECK(s.effective_background() == BackgroundStyle::Plain);
    s.background = BackgroundStyle::Gradient;
    CHECK(s.effective_background() == BackgroundStyle::Gradient);

    // The stored choice is not touched, so switching the flag off brings it back.
    s.background = BackgroundStyle::Motion;
    s.reduced_effects = false;
    CHECK(s.effective_background() == BackgroundStyle::Motion);
}

TEST_CASE("the hit effect is Glow by default and is drawn as chosen") {
    LookSettings s;
    CHECK(s.hit_effect == HitEffect::Glow);
    for (const HitEffect e : kAllHitEffects) {
        s.hit_effect = e;
        CHECK(s.effective_hit_effect() == e);
    }
    s.hit_effect = static_cast<HitEffect>(9);
    CHECK_FALSE(s.is_valid());
    CHECK(s.clamped().hit_effect == HitEffect::Glow);
}

TEST_CASE("a reset gives the default look and keeps the player's own preferences") {
    LookSettings mine;
    mine.octave_numbering = OctaveNumbering::Yamaha;
    mine.key_size = KeySize::Large;
    mine.show_typing_keys = true;
    mine.grade_words = false;
    mine.grade_position = GradePosition::Top;
    mine.grade_opacity = 35;
    mine.black_key_names = BlackKeyNames::Off;
    mine.block_layout = BlockLayout::EvenLanes;
    mine.beat_lines = BeatLines::Bars;
    mine.rising_in_song = true;
    mine.combo_number = false;
    // Everything else is the player's look and goes back to the default.
    mine.note_style = NoteStyle::Neon;
    mine.hit_effect = HitEffect::Ripple;
    mine.high_contrast = true;
    const LookSettings out = reset_look(mine);
    CHECK(out.octave_numbering == OctaveNumbering::Yamaha);
    CHECK(out.key_size == KeySize::Large);
    CHECK(out.show_typing_keys);
    CHECK_FALSE(out.grade_words);
    CHECK(out.grade_position == GradePosition::Top);
    CHECK(out.grade_opacity == 35);
    CHECK(out.black_key_names == BlackKeyNames::Off);
    CHECK(out.block_layout == BlockLayout::EvenLanes);
    CHECK(out.beat_lines == BeatLines::Bars);
    CHECK(out.rising_in_song);
    CHECK_FALSE(out.combo_number);
    LookSettings expected;
    expected.octave_numbering = out.octave_numbering;
    expected.key_size = out.key_size;
    expected.show_typing_keys = out.show_typing_keys;
    expected.grade_words = out.grade_words;
    expected.grade_position = out.grade_position;
    expected.grade_opacity = out.grade_opacity;
    expected.black_key_names = out.black_key_names;
    expected.block_layout = out.block_layout;
    expected.beat_lines = out.beat_lines;
    expected.rising_in_song = out.rising_in_song;
    expected.combo_number = out.combo_number;
    CHECK(out == expected);
    // Resetting a default look changes nothing.
    CHECK(reset_look(LookSettings{}) == LookSettings{});
}

TEST_CASE("notes rising during a song and the combo number have their defaults") {
    CHECK_FALSE(LookSettings{}.rising_in_song);
    CHECK(LookSettings{}.combo_number);
}

TEST_CASE("a reset sends the Learn sections back to their own shades, and a stray shade is mended") {
    LookSettings mine;
    mine.learn_picks = {LearnShade::Lime, LearnShade::Lime, LearnShade::Lime, LearnShade::Lime};
    CHECK(reset_look(mine).learn_picks == kLearnDefaultPicks);

    mine.learn_picks[2] = static_cast<LearnShade>(40);
    CHECK_FALSE(mine.is_valid());
    const LookSettings mended = mine.clamped();
    CHECK(mended.learn_picks[0] == LearnShade::Lime);
    CHECK(mended.learn_picks[2] == kLearnDefaultPicks[2]);
}
