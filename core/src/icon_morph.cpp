// SPDX-License-Identifier: GPL-3.0-or-later
// Morphing stroke icons, after the technique of morphicons (https://github.com/guillermolg00/

#include "icon_morph_internal.hpp"
// morphicons, MIT License, Copyright (c) 2026 Guillermo). A port of the technique, not the code.
#include "etude/icon_morph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace etude {

using namespace morph_detail;

namespace {

// --- Parsing -----------------------------------------------------------------------------------

} // namespace

void evaluate_morph(const MorphPlan& plan, float t, MorphShape& out) {
    out.count = plan.count;
    for (int j = 0; j < plan.count; ++j) {
        const MorphPair& p = plan.pairs[static_cast<std::size_t>(j)];
        MorphSubpath& o = out.subpaths[static_cast<std::size_t>(j)];
        const float sc = std::exp(t * p.log_scale);
        const float c = std::cos(t * p.angle) * sc;
        const float s = std::sin(t * p.angle) * sc;
        MorphPoint centre;
        if (plan.rigid) {
            const MorphPoint off{p.ca.x - plan.ga.x, p.ca.y - plan.ga.y};
            const MorphPoint turned = rotate(off, c, s);
            centre = {p.ca.x + t * p.drift.x + turned.x - off.x,
                      p.ca.y + t * p.drift.y + turned.y - off.y};
        } else {
            centre = lerp(p.ca, p.cb, t);
        }
        const float u = 1.0f - t;
        for (int i = 0; i < kMorphSamples; ++i) {
            const MorphPoint& pa = p.a[static_cast<std::size_t>(i)];
            const MorphPoint& pb = p.b[static_cast<std::size_t>(i)];
            const MorphPoint v{u * pa.x + t * pb.x, u * pa.y + t * pb.y};
            o.pts[static_cast<std::size_t>(i)] = {centre.x + c * v.x - s * v.y,
                                                  centre.y + s * v.x + c * v.y};
        }
        o.closed = p.closed;
    }
}

bool step_spring(SpringState& s, Spring spring, float dt) {
    dt = std::clamp(dt, 0.0f, 0.25f); // a long stall does not run thousands of substeps
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / kSpringStepS - 1e-4f)));
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        s.v += (spring.stiffness * (1.0f - s.x) - spring.damping * s.v) * h;
        s.x += s.v * h;
    }
    if (std::fabs(1.0f - s.x) < 0.001f && std::fabs(s.v) < 0.02f) {
        s.x = 1.0f;
        s.v = 0.0f;
        return true;
    }
    return false;
}

MorphIconData make_icon(std::string_view d) {
    MorphIconData icon;
    icon.error = parse_svg_path(d, icon.path);
    resample(icon.path, icon.shape);
    return icon;
}

void MorphIcon::set(const MorphIconData& icon) {
    target_ = &icon;
    frame_ = icon.shape;
    spring_ = {};
    moving_ = false;
}

void MorphIcon::morph_to(const MorphIconData& icon, Spring spring) {
    if (target_ == &icon) {
        return;
    }
    if (target_ == nullptr) {
        set(icon);
        return;
    }
    float v = 0.0f;
    if (moving_) {
        // Plan from the frame on screen; its samples serve as the source directly.
        for (int i = 0; i < frame_.count; ++i) {
            refresh_metrics(frame_.subpaths[static_cast<std::size_t>(i)]);
        }
        v = std::clamp(spring_.v, -kSpringMaxSpeed, kSpringMaxSpeed);
    }
    plan_morph(moving_ ? frame_ : target_->shape, icon.shape, plan_);
    target_ = &icon;
    params_ = spring;
    spring_ = {0.0f, v};
    moving_ = true;
    evaluate_morph(plan_, 0.0f, frame_);
}

void MorphIcon::go(const MorphIconData& icon, bool animate, Spring spring) {
    if (animate) {
        morph_to(icon, spring);
    } else if (target_ != &icon || moving_) {
        set(icon);
    }
}

bool MorphIcon::step(float dt) {
    if (!moving_) {
        return false;
    }
    if (step_spring(spring_, params_, dt)) {
        frame_ = target_->shape; // the exact target, and its own subpath count
        moving_ = false;
        return false;
    }
    evaluate_morph(plan_, spring_.x, frame_);
    return true;
}

} // namespace etude
