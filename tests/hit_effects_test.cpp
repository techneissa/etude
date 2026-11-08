// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hit_effects.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace etude;

namespace {

constexpr std::int64_t kFrameNs = 1'000'000'000 / 120;

// A press every `gap_ns` across `notes` lanes, then frames until all have faded. Returns the
// highest any shape reached above the hit line, in pixels, and checks every frame's shapes on the
// way.
struct Run {
    float highest = 0.0f;
    std::size_t most_shapes = 0;
    bool any = false;
};

Run run_presses(HitEffect effect, const HitSpot& spot, int notes, std::int64_t gap_ns,
                int velocity) {
    HitEffects fx;
    std::vector<HitShape> out(HitEffects::kMaxShapes);
    Run run;
    std::int64_t now = 0;
    fx.update(now);
    int pressed = 0;
    std::int64_t next_press = 0;
    for (int frame = 0; frame < 240; ++frame) {
        while (pressed < notes && next_press <= now) {
            HitSpot at = spot;
            at.x += static_cast<float>(pressed) * spot.lane_w;
            fx.spawn(effect, at, 60 + pressed, velocity);
            ++pressed;
            next_press += gap_ns;
        }
        const std::size_t n = fx.shapes(out);
        run.most_shapes = std::max(run.most_shapes, n);
        for (std::size_t i = 0; i < n; ++i) {
            run.any = true;
            run.highest = std::max(run.highest, spot.hit_y - hit_shape_top(out[i]));
            CHECK(out[i].alpha > 0.0f);
            CHECK(out[i].alpha <= 1.0f);
        }
        now += kFrameNs;
        fx.update(now);
    }
    CHECK(fx.live() == 0);
    return run;
}

} // namespace

TEST_CASE("every hit effect stays within one white key's length above the hit line") {
    // 1920x1080 and 1366x768 with the whole keyboard, and a zoomed-in view with wide lanes.
    const HitSpot spots[] = {
        {300.0f, 870.0f, 37.0f, 170.0f, 1.0f},
        {200.0f, 620.0f, 26.0f, 120.0f, 768.0f / 1080.0f},
        {400.0f, 870.0f, 90.0f, 170.0f, 1.0f},
        {100.0f, 400.0f, 12.0f, 40.0f, 0.45f}, // a small window
    };
    for (const HitSpot& spot : spots) {
        for (const HitEffect effect : kAllHitEffects) {
            CAPTURE(hit_effect_label(effect));
            CAPTURE(spot.reach);
            for (const int velocity : {1, 64, 127}) {
                // A fast run of notes, and a chord.
                const Run fast = run_presses(effect, spot, 12, 60'000'000, velocity);
                const Run chord = run_presses(effect, spot, 5, 0, velocity);
                CHECK(fast.highest <= spot.reach);
                CHECK(chord.highest <= spot.reach);
                CHECK(fast.any == (effect != HitEffect::None));
            }
        }
    }
}

TEST_CASE("sparks rise no higher than their share of the reach, and only a few per press") {
    const HitSpot spot{500.0f, 870.0f, 37.0f, 170.0f, 1.0f};
    const Run soft = run_presses(HitEffect::Sparks, spot, 1, 0, 1);
    const Run hard = run_presses(HitEffect::Sparks, spot, 1, 0, 127);
    CHECK(hard.highest <= kSparkReachShare * spot.reach);
    CHECK(hard.highest > 0.3f * spot.reach); // they do rise
    // A spark is two shapes (streak and head) plus the one flash.
    CHECK(soft.most_shapes == 1 + 2 * kSparkMin);
    CHECK(hard.most_shapes == 1 + 2 * kSparkMax);
}

TEST_CASE("each effect looks different: a dome, a widening ring, rising sparks") {
    const HitSpot spot{500.0f, 870.0f, 37.0f, 170.0f, 1.0f};
    std::vector<HitShape> out(HitEffects::kMaxShapes);
    const auto at = [&](HitEffect effect, std::int64_t ns) {
        HitEffects fx;
        fx.update(0);
        fx.spawn(effect, spot, 60, 100);
        for (std::int64_t t = kFrameNs; t <= ns; t += kFrameNs) {
            fx.update(t);
        }
        const std::size_t n = fx.shapes(out);
        return std::vector<HitShape>(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n));
    };
    const auto glow = at(HitEffect::Glow, 100'000'000);
    REQUIRE(!glow.empty());
    for (const HitShape& s : glow) {
        CHECK(s.kind == HitShapeKind::Soft);
        CHECK(s.y == spot.hit_y);
    }

    const auto ring_early = at(HitEffect::Ripple, 50'000'000);
    const auto ring_late = at(HitEffect::Ripple, 300'000'000);
    REQUIRE(ring_early.size() == 1);
    REQUIRE(ring_late.size() == 1);
    CHECK(ring_early[0].kind == HitShapeKind::Ring);
    CHECK(ring_late[0].rx > ring_early[0].rx);
    CHECK(ring_late[0].alpha < ring_early[0].alpha);
    CHECK(ring_late[0].thickness <= 2.0f * spot.unit);
    CHECK(2.0f * ring_late[0].rx <= kRippleEndLanes * spot.lane_w);

    const auto sparks = at(HitEffect::Sparks, 100'000'000);
    const auto streaks = std::count_if(sparks.begin(), sparks.end(), [](const HitShape& s) {
        return s.kind == HitShapeKind::Streak;
    });
    CHECK(streaks >= kSparkMin);
    for (const HitShape& s : sparks) {
        if (s.kind == HitShapeKind::Streak) {
            CHECK(s.y < spot.hit_y); // in the air
        }
    }
    // The flash sits on the hit line and is no wider than its share of the lane.
    const auto flash = std::find_if(sparks.begin(), sparks.end(), [&](const HitShape& s) {
        return s.kind == HitShapeKind::Soft && s.y == spot.hit_y;
    });
    REQUIRE(flash != sparks.end());
    CHECK(2.0f * flash->rx <= kSparkFlashLanes * spot.lane_w + 0.01f);
}

TEST_CASE("a glow is clearly visible on the hit line, and still shows as it fades") {
    // The glow sprite is 1 at its centre and the shapes add up (additive blending), so the sum of
    // the alphas of the soft shapes centred on the hit line is how bright the lane's foot gets.
    const HitSpot spot{500.0f, 870.0f, 37.0f, 170.0f, 1.0f};
    std::vector<HitShape> out(HitEffects::kMaxShapes);
    const auto brightness = [&](std::int64_t ns, int velocity) {
        HitEffects fx;
        fx.update(0);
        fx.spawn(HitEffect::Glow, spot, 60, velocity);
        for (std::int64_t t = kFrameNs; t <= ns; t += kFrameNs) {
            fx.update(t);
        }
        const std::size_t n = fx.shapes(out);
        float sum = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(out[i].kind == HitShapeKind::Soft);
            if (out[i].y == spot.hit_y) {
                sum += out[i].alpha;
            }
        }
        return sum;
    };
    // A medium press, 100 ms on (the screenshot's moment): about as bright as a ripple's line.
    CHECK(brightness(100'000'000, 64) >= 1.0f);
    // A soft press still shows plainly, and the glow is still there at 300 ms.
    CHECK(brightness(100'000'000, 1) >= 0.7f);
    CHECK(brightness(300'000'000, 64) >= 0.3f);
}

TEST_CASE("hit effects are gone within their life, and a full pool reuses the oldest") {
    const HitSpot spot{500.0f, 870.0f, 37.0f, 170.0f, 1.0f};
    HitEffects fx;
    fx.update(0);
    for (std::size_t i = 0; i < HitEffects::kCapacity + 10; ++i) {
        fx.spawn(HitEffect::Glow, spot, 60, 100);
    }
    CHECK(fx.live() == static_cast<int>(HitEffects::kCapacity));
    fx.spawn(HitEffect::None, spot, 60, 100);
    CHECK(fx.live() == static_cast<int>(HitEffects::kCapacity));
    std::int64_t t = 0;
    while (t < 600'000'000) {
        t += kFrameNs;
        fx.update(t);
    }
    CHECK(fx.live() == 0);
    fx.spawn(HitEffect::Ripple, spot, 60, 100);
    fx.clear();
    CHECK(fx.live() == 0);
    // No lane or no reach (a keyboard with no room above it): nothing to draw.
    fx.spawn(HitEffect::Glow, {500.0f, 870.0f, 0.0f, 170.0f, 1.0f}, 60, 100);
    fx.spawn(HitEffect::Glow, {500.0f, 870.0f, 37.0f, 0.0f, 1.0f}, 60, 100);
    CHECK(fx.live() == 0);
}

TEST_CASE("sparks fall back to the glow when the frame cannot afford particles") {
    CHECK(affordable_hit_effect(HitEffect::Sparks, false) == HitEffect::Glow);
    CHECK(affordable_hit_effect(HitEffect::Sparks, true) == HitEffect::Sparks);
    for (const HitEffect e : {HitEffect::None, HitEffect::Glow, HitEffect::Ripple}) {
        CHECK(affordable_hit_effect(e, false) == e);
        CHECK(affordable_hit_effect(e, true) == e);
    }
}

TEST_CASE("hit effects are drawn before the blocks, and names after them") {
    static_assert(scene_layer_index(SceneLayer::HitEffects) <
                  scene_layer_index(SceneLayer::Blocks));
    CHECK(scene_layer_index(SceneLayer::Background) < scene_layer_index(SceneLayer::HitEffects));
    CHECK(scene_layer_index(SceneLayer::HitEffects) < scene_layer_index(SceneLayer::Blocks));
    CHECK(scene_layer_index(SceneLayer::Blocks) < scene_layer_index(SceneLayer::NoteText));
    CHECK(scene_layer_index(SceneLayer::Keyboard) < scene_layer_index(SceneLayer::NoteText));
    // Every layer once.
    for (const SceneLayer layer : kSceneLayers) {
        CHECK(std::count(kSceneLayers.begin(), kSceneLayers.end(), layer) == 1);
    }
}

TEST_CASE("hit effects have their labels") {
    CHECK(hit_effect_label(HitEffect::None) == "None");
    CHECK(hit_effect_label(HitEffect::Glow) == "Glow");
    CHECK(hit_effect_label(HitEffect::Ripple) == "Ripple");
    CHECK(hit_effect_label(HitEffect::Sparks) == "Sparks");
}
