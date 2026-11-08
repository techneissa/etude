// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace etude {

// What the two press effects add to a note's block and key, as plain numbers, so the renderer and
// the tests read the same arithmetic.
//
// Key flash (`LookSettings::key_flash`): for a moment after a press the note whitens and its glow
// swells. Held glow (`LookSettings::held_glow`): while the key stays down the note's glow is
// stronger and reaches further. Both were first a faint blink on top of the glow every style
// already has (a fresh press blended 45% to white for 0.16 s; a held note's glow gained 0.26 of
// alpha at the same size), which on the Effects page's small preview and over the hit effect's own
// glow looked the same switched on or off. They are now strong enough to see from a seat away.
struct PressEffects {
    std::int64_t flash_ns = 300'000'000; // the flash falls from 1 to 0 over this long after a note-on
    float flash_whiten = 0.95f;          // how far a fresh press blends toward white
    float flash_glow = 1.0f;             // glow alpha added at the press, falling with the flash
    float flash_grow = 1.6f;             // the glow's reach grows by this share at the press
    float held_glow = 1.0f;              // glow alpha added while the key is down
    float held_grow = 2.0f;              // and its reach grows by this share
    float held_wash = 0.45f;             // a held key's face is washed this far toward white
};

// 1 at the note-on, falling to 0 over `flash_ns`; 0 with the switch off, before the press and
// after the flash.
inline float flash_level(bool enabled, std::int64_t age_ns, std::int64_t flash_ns) {
    if (!enabled || flash_ns <= 0 || age_ns < 0 || age_ns >= flash_ns) {
        return 0.0f;
    }
    return 1.0f - static_cast<float>(age_ns) / static_cast<float>(flash_ns);
}

// A note's glow: its alpha (0..1) and how much wider than the style's own it reaches (1 for the
// style's own), from the style's alpha, whether the key is held with Held glow on, and the flash
// level (`flash_level`, 0 with Key flash off).
struct PressGlow {
    float alpha = 0.0f;
    float reach = 1.0f;
};
// `bloom` false drops the flash's swelling glow and keeps the held glow (Reduced effects).
inline PressGlow press_glow(float style_alpha, bool held, bool held_glow_on, float flash,
                            const PressEffects& fx = {}, bool bloom = true) {
    const bool lit = held && held_glow_on;
    const float swell = bloom ? flash : 0.0f;
    PressGlow g;
    g.alpha = std::min(1.0f, style_alpha + (lit ? fx.held_glow : 0.0f) + fx.flash_glow * swell);
    g.reach = 1.0f + (lit ? fx.held_grow : 0.0f) + fx.flash_grow * swell;
    return g;
}

// How far a fresh press blends toward white at `flash` (0..1).
inline float flash_whiten_share(float flash, const PressEffects& fx = {}) {
    return fx.flash_whiten * std::clamp(flash, 0.0f, 1.0f);
}

// The switches the scene reads (LookSettings::key_flash, held_glow, reduced_effects).
struct PressState {
    bool key_flash = true;
    bool held_glow = true;
    bool reduced = false; // Reduced effects: a short whitening, no bloom
};

// Reduced effects halves the flash.
inline std::int64_t flash_length_ns(const PressEffects& fx, bool reduced) {
    return reduced ? fx.flash_ns / 2 : fx.flash_ns;
}

// The flash level of a note pressed `age_ns` ago: what the scene's `flash_at` returns.
inline float press_flash(const PressState& st, std::int64_t age_ns, const PressEffects& fx = {}) {
    return flash_level(st.key_flash, age_ns, flash_length_ns(fx, st.reduced));
}

// Everything a note's paint takes from the two effects: the share it whitens toward white (and a
// lit key's white wash), and its glow's alpha and reach. This is what `Scene::paint` applies.
struct PressPaint {
    float whiten = 0.0f;
    float wash = 0.0f; // the white wash over a lit key's face: the flash's, or the held key's
    float glow = 0.0f;
    float reach = 1.0f;
};
inline PressPaint press_paint(const PressState& st, bool held, float flash, float style_glow,
                              const PressEffects& fx = {}) {
    const PressGlow g = press_glow(style_glow, held, st.held_glow, flash, fx, !st.reduced);
    const float whiten = flash_whiten_share(flash, fx);
    const float held_wash = held && st.held_glow ? fx.held_wash : 0.0f;
    return {whiten, std::max(whiten, held_wash), g.alpha, g.reach};
}

} // namespace etude
