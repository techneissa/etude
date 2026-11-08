// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/press_effects.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace etude;

TEST_CASE("the flash is 1 at the press, falls to 0 over its length, and is 0 with Key flash off") {
    const PressEffects fx;
    CHECK(flash_level(true, 0, fx.flash_ns) == doctest::Approx(1.0f));
    CHECK(flash_level(true, fx.flash_ns / 2, fx.flash_ns) == doctest::Approx(0.5f));
    CHECK(flash_level(true, fx.flash_ns, fx.flash_ns) == 0.0f);
    CHECK(flash_level(true, -1, fx.flash_ns) == 0.0f); // before the press
    CHECK(flash_level(true, 0, 0) == 0.0f);
    CHECK(flash_level(false, 0, fx.flash_ns) == 0.0f);
    CHECK(flash_level(false, fx.flash_ns / 4, fx.flash_ns) == 0.0f);
}

TEST_CASE("Key flash changes the glow and the whitening of a fresh press, and only then") {
    const PressEffects fx;
    const float style = 0.2f;
    const float on = flash_level(true, fx.flash_ns / 10, fx.flash_ns);
    const float off = flash_level(false, fx.flash_ns / 10, fx.flash_ns);
    const PressGlow with = press_glow(style, false, true, on, fx);
    const PressGlow without = press_glow(style, false, true, off, fx);
    CHECK(with.alpha > without.alpha);
    CHECK(with.reach > without.reach);
    CHECK(flash_whiten_share(on, fx) > flash_whiten_share(off, fx));
    CHECK(without.alpha == doctest::Approx(style));
    CHECK(without.reach == doctest::Approx(1.0f));
    CHECK(flash_whiten_share(off, fx) == 0.0f);
    // The flash fades: later in it the glow is weaker.
    const float late = flash_level(true, fx.flash_ns * 8 / 10, fx.flash_ns);
    CHECK(press_glow(style, false, true, late, fx).alpha < with.alpha);
}

TEST_CASE("Held glow changes the glow of a held key, and a key that is up has none of it") {
    const PressEffects fx;
    const float style = 0.2f;
    const PressGlow held_on = press_glow(style, true, true, 0.0f, fx);
    const PressGlow held_off = press_glow(style, true, false, 0.0f, fx);
    const PressGlow up_on = press_glow(style, false, true, 0.0f, fx);
    CHECK(held_on.alpha > held_off.alpha);
    CHECK(held_on.reach > held_off.reach);
    CHECK(held_off.alpha == doctest::Approx(style));
    CHECK(up_on.alpha == doctest::Approx(style));
    CHECK(up_on.reach == doctest::Approx(1.0f));
    CHECK(held_on.alpha <= 1.0f);
    CHECK(press_glow(0.9f, true, true, 1.0f, fx).alpha == doctest::Approx(1.0f)); // never past full
}

TEST_CASE("the switches are independent: each changes the glow without the other") {
    const PressEffects fx;
    const float flash = 0.8f;
    const PressGlow neither = press_glow(0.2f, true, false, 0.0f, fx);
    const PressGlow only_flash = press_glow(0.2f, true, false, flash, fx);
    const PressGlow only_held = press_glow(0.2f, true, true, 0.0f, fx);
    const PressGlow both = press_glow(0.2f, true, true, flash, fx);
    CHECK(only_flash.alpha > neither.alpha);
    CHECK(only_held.alpha > neither.alpha);
    CHECK(both.reach > only_flash.reach);
    CHECK(both.reach > only_held.reach);
}

// What the scene applies to a note: `Scene::flash_at` is `press_flash` and `Scene::paint` is
// `press_paint` (the test below reads the scene's source to hold that), so these are the scene's
// own arithmetic for a pressed note and a held one with each switch on and off.
TEST_CASE("scene path: Key flash on whitens a fresh press and swells its glow, off does neither") {
    const PressEffects fx;
    const std::int64_t age = fx.flash_ns / 10; // just after the press
    PressState on;
    PressState off;
    off.key_flash = false;
    const float f_on = press_flash(on, age);
    const float f_off = press_flash(off, age);
    const PressPaint with = press_paint(on, false, f_on, 0.2f);
    const PressPaint without = press_paint(off, false, f_off, 0.2f);
    CHECK(f_on > 0.8f);
    CHECK(f_off == 0.0f);
    CHECK(with.whiten > 0.8f); // close to white at the press
    CHECK(with.wash > 0.8f);   // and so is the key
    CHECK(without.whiten == 0.0f);
    CHECK(with.glow > without.glow + 0.5f);
    CHECK(with.reach > without.reach + 1.0f);
    // And it is over after its length.
    CHECK(press_flash(on, fx.flash_ns) == 0.0f);
}

TEST_CASE("scene path: Held glow on gives a held note a clear halo, off leaves the style's own") {
    PressState on;
    PressState off;
    off.held_glow = false;
    const PressPaint held_on = press_paint(on, true, 0.0f, 0.2f);
    const PressPaint held_off = press_paint(off, true, 0.0f, 0.2f);
    const PressPaint up = press_paint(on, false, 0.0f, 0.2f);
    CHECK(held_on.glow >= held_off.glow + 0.7f);
    CHECK(held_on.reach >= held_off.reach + 1.5f);
    CHECK(held_on.wash > held_off.wash + 0.2f); // its key is washed toward white too
    CHECK(held_off.wash == 0.0f);
    CHECK(held_off.glow == doctest::Approx(0.2f));
    CHECK(held_off.reach == doctest::Approx(1.0f));
    CHECK(up.glow == doctest::Approx(0.2f)); // a key that is up has no halo
}

TEST_CASE("scene path: Reduced effects keeps a short whitening without the bloom, and the held glow") {
    const PressEffects fx;
    PressState reduced;
    reduced.reduced = true;
    PressState full;
    const std::int64_t age = fx.flash_ns / 10;
    const float f_full = press_flash(full, age);
    const float f_red = press_flash(reduced, age);
    const PressPaint a = press_paint(full, false, f_full, 0.2f);
    const PressPaint b = press_paint(reduced, false, f_red, 0.2f);
    CHECK(b.whiten > 0.5f);           // still whitens
    CHECK(b.glow == doctest::Approx(0.2f)); // no swelling glow
    CHECK(b.reach == doctest::Approx(1.0f));
    CHECK(a.glow > b.glow);
    // Shorter: gone at half the time the full flash lasts.
    CHECK(press_flash(full, fx.flash_ns * 6 / 10) > 0.0f);
    CHECK(press_flash(reduced, fx.flash_ns * 6 / 10) == 0.0f);
    // Held glow is what it is.
    const PressPaint held_full = press_paint(full, true, 0.0f, 0.2f);
    const PressPaint held_red = press_paint(reduced, true, 0.0f, 0.2f);
    CHECK(held_red.glow == doctest::Approx(held_full.glow));
    CHECK(held_red.reach == doctest::Approx(held_full.reach));
    // Reduced effects does not bring a flash back that Key flash turned off.
    reduced.key_flash = false;
    CHECK(press_flash(reduced, age) == 0.0f);
}

std::string read_scene_source(const char* name) {
    const std::filesystem::path path = std::filesystem::path(ETUDE_SOURCE_DIR) / "app" / name;
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

TEST_CASE("the scene applies the press effects through these functions, so the tests above guard it") {
    const std::string scene = read_scene_source("scene.cpp");
    REQUIRE_FALSE(scene.empty());
    // flash_at is press_flash, and paint takes the whitening, the glow and its reach from
    // press_paint: a scene that stops calling them stops showing the effects.
    CHECK(scene.find("return press_flash(press_state(), now_ns - pressed_ns, look_.press);") !=
          std::string::npos);
    CHECK(scene.find("press_paint(press_state(), held, flash, recipe_.glow_alpha, look_.press)") !=
          std::string::npos);
    CHECK(scene.find("p.glow = pp.glow;") != std::string::npos);
    CHECK(scene.find("p.glow_reach = pp.reach;") != std::string::npos);
    CHECK(scene.find("pp.whiten") != std::string::npos);
    // The reach is drawn: a block's glow and a key's glow both scale by it.
    CHECK(read_scene_source("scene_notes.cpp").find("* p.glow_reach") != std::string::npos);
    CHECK(read_scene_source("scene_keyboard.cpp").find("glow_size *= p.glow_reach;") !=
          std::string::npos);
}
