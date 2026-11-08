// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace etude {

// What a key press shows at the hit line. Every effect is drawn beneath the falling blocks and
// stays within one white key's length above the hit line, so it never covers a block or a name
// the player is reading.
//   Glow: a soft pulse of the note's colour on the hit line.
//   Ripple: a thin ring that widens and fades at the hit line.
//   Sparks: a few small sparks that rise a little and fade, over a short flash.
enum class HitEffect : std::uint8_t { None, Glow, Ripple, Sparks };
inline constexpr std::array<HitEffect, 4> kAllHitEffects{HitEffect::None, HitEffect::Glow,
                                                         HitEffect::Ripple, HitEffect::Sparks};

// "None", "Glow", "Ripple", "Sparks".
std::string_view hit_effect_label(HitEffect effect);

// The effect drawn when the frame can or cannot afford particles (EffectsBudget::particles):
// Sparks become the Glow (two shapes), so a press still shows; the others cost at most two shapes
// and stay.
HitEffect affordable_hit_effect(HitEffect wanted, bool particles);

// Every size below is a share of the lane, of the reach (one white key's length) or a number of
// 1080p pixels times the screen unit, so the effects follow the window size.

// Glow: a soft dome over the lane, at most this share of the reach tall, this many lanes wide,
// with a brighter, whiter core on the hit line (`kGlowCore*`) that fades sooner. The two add up,
// so the lane's foot lights plainly at any velocity.
inline constexpr float kGlowLifeS = 0.5f;
inline constexpr float kGlowAttackS = 0.05f;
inline constexpr float kGlowReachShare = 0.45f;
inline constexpr float kGlowLaneWidths = 2.4f;
inline constexpr float kGlowAlpha = 1.0f;
inline constexpr float kGlowWhiten = 0.15f;
inline constexpr float kGlowCoreReachShare = 0.2f;
inline constexpr float kGlowCoreLaneWidths = 1.2f;
inline constexpr float kGlowCoreAlpha = 0.9f;
inline constexpr float kGlowCoreWhiten = 0.45f;

// Ripple: a thin ring centred on the hit line, growing from just past the lane to this many lanes
// across, flattened to `kRippleFlatten` of its width and never taller than `kRippleReachShare` of
// the reach.
inline constexpr float kRippleLifeS = 0.5f;
inline constexpr float kRippleStartLanes = 1.1f;
inline constexpr float kRippleEndLanes = 3.0f;
inline constexpr float kRippleFlatten = 0.4f;
inline constexpr float kRippleReachShare = 0.6f;
inline constexpr float kRippleWidthPx = 2.0f; // the line, 1080p pixels, at least 1 px
inline constexpr float kRippleAlpha = 0.85f;
inline constexpr float kRippleWhiten = 0.25f;

// Sparks: `kSparkMin` to `kSparkMax` per press (more for a harder press), each `kSparkSizePx` to
// twice that, living up to `kSparkMaxLifeS`, rising at most `kSparkReachShare` of the reach on a
// ballistic path; over a flat flash `kSparkFlashLanes` lanes wide that sits on the hit line.
inline constexpr int kSparkMin = 3;
inline constexpr int kSparkMax = 6;
inline constexpr float kSparkSizePx = 2.0f;
inline constexpr float kSparkMinLifeS = 0.22f;
inline constexpr float kSparkMaxLifeS = 0.35f;
inline constexpr float kSparkReachShare = 0.8f;
inline constexpr float kSparkGravityPx = 2600.0f; // 1080p pixels per second squared
inline constexpr float kSparkAlpha = 0.75f;
inline constexpr float kSparkWhiten = 0.3f;
inline constexpr float kSparkFlashLanes = 1.2f;
inline constexpr float kSparkFlashLifeS = 0.2f;
inline constexpr float kSparkFlashAlpha = 0.45f;

// One press, in window pixels.
struct HitSpot {
    float x = 0.0f;      // the lane's centre
    float hit_y = 0.0f;  // the hit line
    float lane_w = 0.0f; // the lane's width
    float reach = 0.0f;  // how far above the hit line a shape may reach: one white key's length
    float unit = 1.0f;   // pixels per 1080p pixel
};

// One thing to draw. `Soft` is the soft glow sprite stretched over the ellipse of radii `rx`,
// `ry`; `Ring` an ellipse outline `thickness` wide; `Streak` a soft bar `2 rx` long along the
// direction (`dx`, `dy`) and `2 ry` wide. `whiten` blends the note's colour toward white.
enum class HitShapeKind : std::uint8_t { Soft, Ring, Streak };
struct HitShape {
    HitShapeKind kind = HitShapeKind::Soft;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    float x = 0.0f;
    float y = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
    float dx = 0.0f;
    float dy = -1.0f;
    float thickness = 0.0f;
    float alpha = 0.0f;
    float whiten = 0.0f;
};

// The highest point (smallest y) the shape covers.
float hit_shape_top(const HitShape& shape);

// A fixed pool of running hit effects. Once full, a new press takes the oldest slot, so it never
// allocates. Positions follow from each slot's age, so a frame's shapes depend only on the clock.
class HitEffects {
  public:
    static constexpr std::size_t kCapacity = 512;
    // Most shapes one frame can produce: two per slot (a spark's streak and its head, a glow's dome
    // and its core).
    static constexpr std::size_t kMaxShapes = 2 * kCapacity;

    // Starts `effect` for a press of `note` at `velocity` (1..127) at `spot`. None does nothing.
    void spawn(HitEffect effect, const HitSpot& spot, int note, int velocity);
    // Ages every effect to `now_ns`; the first call only starts the clock. A step is at most 50 ms,
    // so a stall does not skip effects.
    void update(std::int64_t now_ns);
    // Writes this frame's shapes into `out` (at most its size) and returns how many.
    std::size_t shapes(std::span<HitShape> out) const;
    void clear();
    int live() const;

  private:
    enum class Part : std::uint8_t { Free, Glow, Ripple, Spark, Flash };
    struct Slot {
        Part part = Part::Free;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        float x = 0.0f;
        float hit_y = 0.0f;
        float lane_w = 0.0f;
        float reach = 0.0f;
        float unit = 1.0f;
        float age = 0.0f;
        float life = 0.0f;
        float size = 0.0f; // a spark's half width
        float vx = 0.0f;   // a spark's sideways speed
        float vy = 0.0f;   // its upward speed at the start
        float strength = 1.0f;
    };
    Slot& take();
    float random01();

    std::array<Slot, kCapacity> pool_{};
    std::size_t next_ = 0;
    std::int64_t last_ns_ = 0;
    bool started_ = false;
    std::uint32_t rng_ = 0x9e3779b9u;
};

// The order the scene draws its layers in, first to last. Hit effects come before the blocks, so
// a block or its name is never covered by one; the key label comes over the blocks (faint) and
// under their names; the names come after the keyboard and the blocks; the combo number comes over
// the names, in the corner they keep clear.
enum class SceneLayer : std::uint8_t {
    Background,
    HitEffects,
    Blocks,
    Keyboard,
    KeyLabel,
    NoteText,
    Combo,
    ChordLabel,
    Prompt,
};
inline constexpr std::array<SceneLayer, 9> kSceneLayers{
    SceneLayer::Background, SceneLayer::HitEffects, SceneLayer::Blocks,    SceneLayer::Keyboard,
    SceneLayer::KeyLabel,   SceneLayer::NoteText,   SceneLayer::Combo,     SceneLayer::ChordLabel,
    SceneLayer::Prompt};

// Where `layer` falls in kSceneLayers.
constexpr std::size_t scene_layer_index(SceneLayer layer) {
    for (std::size_t i = 0; i < kSceneLayers.size(); ++i) {
        if (kSceneLayers[i] == layer) {
            return i;
        }
    }
    return kSceneLayers.size();
}

} // namespace etude
