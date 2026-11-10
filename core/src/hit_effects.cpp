// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hit_effects.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

// A spark's streak is its half width plus this much per pixel a second of speed, half long.
constexpr float kStreakStretchS = 0.012f;
constexpr float kStreakWidthShare = 0.6f; // of the spark's size, half the streak's width
constexpr float kHeadShare = 0.8f;        // the spark's bright head, of its size
constexpr float kSparkSideSpeedPx = 60.0f;
// The narrowest a glow or a flash gets, in 1080p pixels, so a thin lane still shows one.
constexpr float kMinSoftHalfWidthPx = 8.0f;

float ease_out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

} // namespace

std::string_view hit_effect_label(HitEffect effect) {
    switch (effect) {
    case HitEffect::None:
        return "None";
    case HitEffect::Glow:
        return "Glow";
    case HitEffect::Ripple:
        return "Ripple";
    case HitEffect::Sparks:
        return "Sparks";
    }
    return "";
}

HitEffect affordable_hit_effect(HitEffect wanted, bool particles) {
    return wanted == HitEffect::Sparks && !particles ? HitEffect::Glow : wanted;
}

float HitEffects::random01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>(rng_ >> 8) / 16777216.0f;
}

HitEffects::Slot& HitEffects::take() {
    Slot& s = pool_[next_];
    next_ = (next_ + 1) % kCapacity;
    return s;
}

void HitEffects::spawn(HitEffect effect, const HitSpot& spot, int note, int velocity) {
    if (effect == HitEffect::None || !(spot.lane_w > 0.0f) || !(spot.reach > 0.0f)) {
        return;
    }
    const float v = static_cast<float>(std::clamp(velocity, 1, 127)) / 127.0f;
    Slot base;
    base.note = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
    base.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    base.x = spot.x;
    base.hit_y = spot.hit_y;
    base.lane_w = spot.lane_w;
    base.reach = spot.reach;
    base.unit = spot.unit;
    base.strength = 0.6f + 0.4f * v;
    switch (effect) {
    case HitEffect::None:
        return;
    case HitEffect::Glow: {
        Slot& s = take();
        s = base;
        s.part = Part::Glow;
        s.life = kGlowLifeS;
        return;
    }
    case HitEffect::Ripple: {
        Slot& s = take();
        s = base;
        s.part = Part::Ripple;
        s.life = kRippleLifeS;
        return;
    }
    case HitEffect::Sparks: {
        Slot& flash = take();
        flash = base;
        flash.part = Part::Flash;
        flash.life = kSparkFlashLifeS;
        const int count =
            kSparkMin +
            static_cast<int>(std::lround(static_cast<float>(kSparkMax - kSparkMin) * v));
        const float g = kSparkGravityPx * spot.unit;
        for (int i = 0; i < count; ++i) {
            Slot& s = take();
            s = base;
            s.part = Part::Spark;
            s.size = kSparkSizePx * spot.unit * (1.0f + random01());
            // The top of its path, less room for its streak and head, stays under the reach.
            const float top = std::max(0.0f, kSparkReachShare * spot.reach - 2.5f * s.size);
            const float rise = top * (0.35f + 0.65f * random01());
            s.vy = std::sqrt(2.0f * g * rise);
            s.vx = (random01() - 0.5f) * 2.0f * kSparkSideSpeedPx * spot.unit;
            s.x = spot.x + (random01() - 0.5f) * 0.6f * spot.lane_w;
            s.life = kSparkMinLifeS + (kSparkMaxLifeS - kSparkMinLifeS) * random01();
        }
        return;
    }
    }
}

void HitEffects::update(std::int64_t now_ns) {
    if (!started_) {
        started_ = true;
        last_ns_ = now_ns;
        return;
    }
    const float dt = std::clamp(static_cast<float>(now_ns - last_ns_) * 1e-9f, 0.0f, 0.05f);
    last_ns_ = now_ns;
    for (Slot& s : pool_) {
        if (s.part == Part::Free) {
            continue;
        }
        s.age += dt;
        if (s.age >= s.life) {
            s.part = Part::Free;
        }
    }
}

std::size_t HitEffects::shapes(std::span<HitShape> out) const {
    std::size_t n = 0;
    const auto add = [&](const HitShape& shape) {
        if (n < out.size() && shape.alpha > 0.0f) {
            out[n++] = shape;
        }
    };
    for (const Slot& s : pool_) {
        if (s.part == Part::Free) {
            continue;
        }
        const float t = std::clamp(s.age / s.life, 0.0f, 1.0f);
        HitShape shape;
        shape.note = s.note;
        shape.velocity = s.velocity;
        shape.x = s.x;
        shape.y = s.hit_y;
        switch (s.part) {
        case Part::Free:
            break;
        case Part::Glow: {
            // Up quickly, then a long soft fall; the dome swells a little as it fades, the core
            // fades sooner.
            const float rise = std::min(1.0f, s.age / kGlowAttackS);
            const float fall = s.age <= kGlowAttackS
                                   ? 1.0f
                                   : 1.0f - (s.age - kGlowAttackS) / (s.life - kGlowAttackS);
            const float grow = ease_out(t);
            shape.kind = HitShapeKind::Soft;
            shape.rx = std::max(0.5f * kGlowLaneWidths * s.lane_w, kMinSoftHalfWidthPx * s.unit) *
                       (0.9f + 0.15f * grow);
            shape.ry = kGlowReachShare * s.reach * (0.75f + 0.25f * grow);
            shape.alpha = kGlowAlpha * s.strength * rise * fall;
            shape.whiten = kGlowWhiten;
            add(shape);
            HitShape core = shape;
            core.rx = std::max(0.5f * kGlowCoreLaneWidths * s.lane_w, kMinSoftHalfWidthPx * s.unit);
            core.ry = kGlowCoreReachShare * s.reach;
            core.alpha = kGlowCoreAlpha * s.strength * rise * fall * fall;
            core.whiten = kGlowCoreWhiten;
            add(core);
            break;
        }
        case Part::Ripple: {
            const float grow = ease_out(t);
            const float lanes = kRippleStartLanes + (kRippleEndLanes - kRippleStartLanes) * grow;
            shape.kind = HitShapeKind::Ring;
            shape.thickness = std::max(1.0f, kRippleWidthPx * s.unit);
            shape.rx = 0.5f * lanes * s.lane_w;
            shape.ry = std::min(kRippleFlatten * shape.rx,
                                kRippleReachShare * s.reach - 0.5f * shape.thickness);
            shape.ry = std::max(0.0f, shape.ry);
            shape.alpha = kRippleAlpha * s.strength * (1.0f - t) * (1.0f - t);
            shape.whiten = kRippleWhiten;
            add(shape);
            break;
        }
        case Part::Flash: {
            shape.kind = HitShapeKind::Soft;
            shape.rx = std::max(0.5f * kSparkFlashLanes * s.lane_w, kMinSoftHalfWidthPx * s.unit);
            shape.ry = std::min(0.3f * shape.rx, 0.25f * s.reach);
            shape.alpha = kSparkFlashAlpha * s.strength * (1.0f - t);
            shape.whiten = kSparkWhiten;
            add(shape);
            break;
        }
        case Part::Spark: {
            // A ballistic path: the highest point is vy² / 2g above the hit line.
            const float g = kSparkGravityPx * s.unit;
            const float vy = s.vy - g * s.age; // upward
            const float up = s.vy * s.age - 0.5f * g * s.age * s.age;
            const float x = s.x + s.vx * s.age;
            const float y = s.hit_y - up;
            const float speed = std::sqrt(s.vx * s.vx + vy * vy);
            const float alpha = kSparkAlpha * s.strength * (1.0f - t * t);
            shape.kind = HitShapeKind::Streak;
            shape.x = x;
            shape.y = y;
            shape.dx = speed > 1e-3f ? s.vx / speed : 0.0f;
            shape.dy = speed > 1e-3f ? -vy / speed : -1.0f;
            shape.rx = s.size + kStreakStretchS * speed;
            shape.ry = kStreakWidthShare * s.size;
            shape.alpha = alpha;
            shape.whiten = kSparkWhiten;
            add(shape);
            HitShape head = shape;
            head.kind = HitShapeKind::Soft;
            head.rx = kHeadShare * s.size;
            head.ry = head.rx;
            head.dx = 0.0f;
            head.dy = -1.0f;
            head.whiten = 0.5f + 0.5f * kSparkWhiten;
            add(head);
            break;
        }
        }
    }
    return n;
}

void HitEffects::clear() {
    for (Slot& s : pool_) {
        s.part = Part::Free;
    }
}

int HitEffects::live() const {
    return static_cast<int>(std::count_if(pool_.begin(), pool_.end(),
                                          [](const Slot& s) { return s.part != Part::Free; }));
}

} // namespace etude
