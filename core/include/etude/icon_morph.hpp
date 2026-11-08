// SPDX-License-Identifier: GPL-3.0-or-later
// Morphing stroke icons: any icon morphs into any other, driven by a spring.
//
// The technique is the one morphicons describes (https://github.com/guillermolg00/morphicons,
// MIT License, Copyright (c) 2026 Guillermo); this is a C++ port of the technique, not of its
// code:
// - The icon's path data (an SVG `d` string, the subset stroke icons use) becomes cubic Béziers.
// - Each subpath is resampled at kMorphSamples points equidistant by arc length, with every corner
//   kept as an exact sample.
// - Subpaths pair by centroid distance and arc length. When the counts differ, the nearest subpath
//   of the smaller side is duplicated and the copies separate in flight.
// - Each pair is aligned with the closed-form 2D Procrustes fit (rotation, scale, translation),
//   trying both directions and, for a loop, every start point. A global fit over the whole icon
//   makes a congruent icon turn as one rigid piece.
// - In flight, translation and angle move linearly and scale log-linearly, around the residual
//   that blends in the aligned frame; on settle the shape snaps to the exact target.
//
// Nothing here allocates: every buffer has a fixed capacity, so a plan (once per state change)
// and each frame's evaluation cost no heap work.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace etude {

inline constexpr int kMorphSamples = 64; // points per subpath
inline constexpr int kMaxSubpaths = 8;   // per icon, and per morph after duplication
inline constexpr int kMaxCubics = 96;    // cubic segments per icon

struct MorphPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct Cubic {
    MorphPoint p0, c1, c2, p3;
};

struct CubicSubpath {
    int first = 0; // index of its first cubic in CubicPath::cubics
    int count = 0;
    bool closed = false;
};

// An icon's path data as cubic Béziers, in its own units (Lucide: a 24-unit square).
struct CubicPath {
    std::array<Cubic, kMaxCubics> cubics{};
    int cubic_count = 0;
    std::array<CubicSubpath, kMaxSubpaths> subpaths{};
    int subpath_count = 0;
};

enum class PathError : std::uint8_t {
    None,
    Syntax,          // an unknown command or a malformed number
    TooManyCubics,   // more than kMaxCubics segments
    TooManySubpaths, // more than kMaxSubpaths subpaths
};

// Appends the subpaths of an SVG path `d` string to `out`: M L H V C S Q T A Z in absolute and
// relative form, implicit repeats, packed arc flags and exponents. Lines, quadratics and arcs
// become cubics (arcs in slices of at most 90 degrees). Segments of no length are dropped, and a
// subpath left empty with them. On an error `out` holds what parsed before it, the subpath in
// progress included (left open).
PathError parse_svg_path(std::string_view d, CubicPath& out);
// A circle as a closed subpath of four cubics.
PathError add_circle(CubicPath& out, float cx, float cy, float r);
// A rectangle, its corners rounded by `rx` (0: square), as a closed subpath.
PathError add_rect(CubicPath& out, float x, float y, float w, float h, float rx);

struct MorphSubpath {
    std::array<MorphPoint, kMorphSamples> pts{};
    bool closed = false; // a loop: pts.back() equals pts.front()
    float length = 0.0f;
    MorphPoint centroid; // of the distinct points (a loop's last point is its first)
};

// An icon or an in-flight frame: every subpath at kMorphSamples points.
struct MorphShape {
    std::array<MorphSubpath, kMaxSubpaths> subpaths{};
    int count = 0;
};

// Corners: a turn between two cubics sharper than this is kept as an exact sample.
inline constexpr float kCornerRadians = 0.35f;

// Samples every subpath of `path` by arc length (8-point Gauss-Legendre per cubic), corners
// anchored and the points between them shared by length (largest remainder, at least one interval
// per run, kMorphSamples - 1 in all). A loop is anchored only at its corners, so its start point
// does not matter.
void resample(const CubicPath& path, MorphShape& out);
// The centroid and length of a subpath whose points are already set.
void refresh_metrics(MorphSubpath& s);

// The best similarity mapping a's points onto b's (both `n` points): b ≈ scale·R(angle)·(a − ca)
// + cb. `residual` is the RMS error relative to b's spread (0: the same shape).
struct Procrustes {
    float angle = 0.0f; // radians, (−π, π]
    float scale = 1.0f;
    MorphPoint ca, cb;
    float residual = 0.0f;
};
Procrustes procrustes(const MorphPoint* a, const MorphPoint* b, int n);

// Below this global residual the whole icon is congruent and moves as one piece.
inline constexpr float kRigidResidual = 5e-3f;

// One pair of subpaths: a's centred points and b brought into a's frame.
struct MorphPair {
    std::array<MorphPoint, kMorphSamples> a{}; // a − ca
    std::array<MorphPoint, kMorphSamples> b{}; // R(−angle)·(b − cb) / scale
    MorphPoint ca, cb;
    MorphPoint drift; // the block transport's correction, so the centroid lands on cb (rigid only)
    float angle = 0.0f;
    float log_scale = 0.0f;
    bool closed = false; // drawn as a loop: both ends were loops
    int from = 0;        // the source and target subpath this pair came from
    int to = 0;
};

struct MorphPlan {
    std::array<MorphPair, kMaxSubpaths> pairs{};
    int count = 0;
    bool rigid = false; // one similarity for the whole icon
    float angle = 0.0f;
    float log_scale = 0.0f;
    MorphPoint ga; // the source's centroid, which a rigid icon turns around
};

// Plans the morph from `a` to `b`. With no subpaths on one side the plan is empty.
void plan_morph(const MorphShape& a, const MorphShape& b, MorphPlan& out);
// The shape at progress `t`: `a` at 0, `b` at 1; beyond 1 (a spring's overshoot) it extrapolates.
void evaluate_morph(const MorphPlan& plan, float t, MorphShape& out);

// A damped spring over progress 0 → 1: x'' = k·(1 − x) − c·x'.
struct Spring {
    float stiffness = 170.0f;
    float damping = 26.0f;
};
inline constexpr Spring kSpringSmooth{170.0f, 26.0f}; // critically damped, no overshoot
inline constexpr Spring kSpringSnappy{420.0f, 30.0f}; // fast, a little overshoot

struct SpringState {
    float x = 1.0f;
    float v = 0.0f;
};
inline constexpr float kSpringStepS = 1.0f / 240.0f; // integration substep
inline constexpr float kSpringMaxSpeed = 14.0f;      // a carried-over velocity is clamped to this
// Advances by `dt` seconds in kSpringStepS substeps (semi-implicit Euler). Returns true once it
// has settled (|1 − x| < 0.001 and |v| < 0.02), with x set to exactly 1 and v to 0.
bool step_spring(SpringState& s, Spring spring, float dt);

// An icon ready to morph: its curves (drawn at rest) and its samples (the morph's source and
// target). Build it once; a MorphIcon points at it.
struct MorphIconData {
    CubicPath path;
    MorphShape shape;
    PathError error = PathError::None;
};
// Parses each `d` string into one icon and resamples it.
MorphIconData make_icon(std::string_view d);

// One icon on screen that morphs when its target changes.
class MorphIcon {
  public:
    // Shows `icon` at once: the first state, or reduced effects.
    void set(const MorphIconData& icon);
    // Morphs to `icon` from what shows now. The same target again changes nothing; a new target
    // in flight plans from the current frame and keeps the velocity.
    void morph_to(const MorphIconData& icon, Spring spring = kSpringSnappy);
    // `animate` false is set(), true is morph_to().
    void go(const MorphIconData& icon, bool animate, Spring spring = kSpringSnappy);
    // Advances the spring. Returns true while it moves.
    bool step(float dt);

    bool moving() const { return moving_; }
    // At rest: the target's exact curves. In flight: the frame's samples.
    const MorphIconData* target() const { return target_; }
    const MorphShape& frame() const { return frame_; }
    float progress() const { return spring_.x; }
    float velocity() const { return spring_.v; }

  private:
    const MorphIconData* target_ = nullptr;
    MorphPlan plan_;
    MorphShape frame_;
    SpringState spring_;
    Spring params_ = kSpringSnappy;
    bool moving_ = false;
};

} // namespace etude
