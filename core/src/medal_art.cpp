// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/medal_art.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mix(Vec3 a, Vec3 b, float t) { return a * (1.0f - t) + b * t; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalised(Vec3 v) {
    const float n = std::sqrt(dot(v, v));
    return n > 1e-6f ? v * (1.0f / n) : Vec3{0.0f, 0.0f, 1.0f};
}
Vec3 hex(std::uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f};
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float smoothstep(float a, float b, float v) {
    const float t = clamp01((v - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// ---- Noise: a hash of the lattice point, so the same pixels come out every time -----------------

float hash2(int x, int y, int seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393U + static_cast<std::uint32_t>(y) * 668265263U +
                      static_cast<std::uint32_t>(seed) * 2246822519U;
    h = (h ^ (h >> 13)) * 1274126177U;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFU) / static_cast<float>(0xFFFFFF);
}

float value_noise(float x, float y, int seed) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi);
    const float fy = y - static_cast<float>(yi);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash2(xi, yi, seed);
    const float b = hash2(xi + 1, yi, seed);
    const float c = hash2(xi, yi + 1, seed);
    const float d = hash2(xi + 1, yi + 1, seed);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

float fbm(float x, float y, int octaves, int seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * value_noise(x, y, seed + i * 17);
        norm += amp;
        x *= 2.03f;
        y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

// ---- The materials -------------------------------------------------------------------------------

struct Spec {
    std::uint32_t rim;       // the rim's base colour
    std::uint32_t face;      // the face's base colour
    float diffuse;           // how much of the light the colour takes
    float specular;          // the highlight's strength
    float shininess;         // ... and its tightness
    float env;               // how much of the surroundings it mirrors (a metal)
    std::uint32_t env_sky;   // what it mirrors above ...
    std::uint32_t env_ground;
    std::uint32_t ribbon;
    std::uint32_t stripe;
    std::uint32_t icon_fill;
    std::uint32_t emboss_light;
    std::uint32_t emboss_dark;
    bool edge_stripes = false; // the ribbon's stripes at its edges (the top tier's gold edging), not down its middle
};

Spec spec_of(Material m) {
    switch (m) {
    case Material::Wood:
        return {0x5A3519, 0x9A6B3A, 1.00f, 0.06f, 10.0f, 0.00f, 0, 0, 0x2E6B44, 0xE9DDB8, 0x6B4524, 0xC79B66, 0x3A210E};
    case Material::Iron:
        return {0x2A2D34, 0x3E424B, 0.85f, 0.35f, 26.0f, 0.28f, 0x8E97A8, 0x15171C, 0x46556E, 0xA9B4C6, 0x2A2D34, 0x7C8494, 0x0E0F12};
    case Material::Bronze:
        return {0x8A4F22, 0xB87238, 0.80f, 0.60f, 38.0f, 0.50f, 0xFFD2A0, 0x4A2410, 0x8C1F2B, 0xE8B05A, 0x7A421C, 0xE9A56A, 0x4A230B};
    case Material::Silver:
        return {0x8E949E, 0xCACDD3, 0.70f, 0.95f, 100.0f, 0.88f, 0xFFFFFF, 0x2C3038, 0x1F3A7A, 0xE5ECFA, 0x666C78, 0xFFFFFF, 0x4E535C};
    case Material::Gold:
        return {0xC98A12, 0xF2BC38, 0.75f, 1.00f, 80.0f, 0.85f, 0xFFF1B0, 0x6A3E05, 0xB3202B, 0xFFE9A0, 0xB07A10, 0xFFEFA8, 0x7A4D05};
    case Material::Platinum:
        return {0x7E93BC, 0xC4D2EC, 0.78f, 0.60f, 45.0f, 0.55f, 0xEAF1FF, 0x5B6F9A, 0x137C86, 0xCFF3F0, 0x667BA6, 0xFFFFFF, 0x4E6290};
    case Material::Diamond:
        return {0x62B8E6, 0xA8E4FF, 0.85f, 1.00f, 70.0f, 0.55f, 0xFFFFFF, 0x1B6E99, 0x6C3DBF, 0xD8C6FF, 0x3A93C6, 0xFFFFFF, 0x1D6C94};
    case Material::Obsidian:
        // The top tier: a deep glassy violet-black with a strong sweep of light, a thin gold ring where the rim
        // begins, flecks in the stone, a black ribbon edged in gold and a faint violet halo round it.
        return {0x4A3590, 0x1E1440, 0.95f, 1.00f, 120.0f, 0.95f, 0xE4D8FF, 0x0E0820, 0x120E1E, 0xE7B83C, 0xE9C24E, 0xFFF3B8, 0x120A26, true};
    }
    return {};
}

// ---- The canvas ---------------------------------------------------------------------------------

constexpr MedalFrame kLarge{192, 272, 96.0f, 174.0f, 84.0f, 58.0f};
constexpr MedalFrame kSmall{64, 64, 32.0f, 32.0f, 29.0f, 20.0f};

// The disc's profile by the share of its radius: the height of the rim and the face, and its slope.
struct Profile {
    float height = 0.0f;
    float slope = 0.0f; // d(height)/d(r)
};

constexpr float kFaceR = 0.76f;   // the face ends here, the rim's inner bevel starts
constexpr float kRimTop = 0.84f;  // the rim's flat top
constexpr float kRimEdge = 0.95f; // ... until the outer bevel

Profile disc_profile(float r, bool coarse) {
    Profile p;
    if (r < kFaceR) { // the face: a very gentle dome, and an engraved line near its edge
        p.height = 0.10f * (1.0f - (r / kFaceR) * (r / kFaceR));
        p.slope = -0.20f * r / (kFaceR * kFaceR);
        if (!coarse && std::fabs(r - 0.66f) < 0.012f) {
            const float k = (r - 0.66f) / 0.012f; // the groove
            p.height -= 0.05f * (1.0f - k * k);
            p.slope += 0.05f * 2.0f * k / 0.012f;
        }
    } else if (r < kRimTop) { // the inner bevel climbs
        const float t = (r - kFaceR) / (kRimTop - kFaceR);
        p.height = 0.0f + 0.34f * t;
        p.slope = 0.34f / (kRimTop - kFaceR);
    } else if (r < kRimEdge) { // the rim's top
        p.height = 0.34f;
        p.slope = 0.0f;
    } else { // the outer bevel falls away
        const float t = clamp01((r - kRimEdge) / (1.0f - kRimEdge));
        p.height = 0.34f * (1.0f - t);
        p.slope = -0.34f / (1.0f - kRimEdge);
    }
    return p;
}

const Vec3 kLight = normalised({-0.52f, -0.62f, 0.60f});

struct Shaded {
    Vec3 colour;
};

// The environment a metal shows: bright sky over a dark ground, by the normal's slope.
Vec3 environment(const Spec& s, Vec3 n) {
    const float up = clamp01(0.5f - 0.5f * n.y * 1.3f + 0.12f * -n.x);
    const float band = smoothstep(0.35f, 0.5f, up) * (1.0f - smoothstep(0.5f, 0.66f, up)); // a bright band
    return mix(hex(s.env_ground), hex(s.env_sky), clamp01(smoothstep(0.30f, 0.80f, up) + 0.35f * band));
}

// The light on a surface point: `n` its normal, `base` its colour.
Vec3 light_surface(const Spec& s, Vec3 n, Vec3 base) {
    const float diff = std::max(0.0f, dot(n, kLight));
    const Vec3 half = normalised(kLight + Vec3{0.0f, 0.0f, 1.0f});
    const float spec = std::pow(std::max(0.0f, dot(n, half)), s.shininess) * s.specular;
    Vec3 colour = base * (0.28f + 0.95f * diff * s.diffuse);
    if (s.env > 0.0f) {
        colour = mix(colour, base * environment(s, n) * 1.25f, s.env * 0.55f);
    }
    return colour + Vec3{spec, spec, spec} * (0.55f + 0.45f * (s.env > 0.0f ? 1.0f : 0.3f));
}

// ---- Finishes: the bump and the colour a material adds to the disc ---------------------------------

struct Surface {
    Vec3 base;
    Vec3 bump; // added to the normal's x and y (z unused)
};

// Diamond: cut, with wedges round the centre, two rings of them, each its own tilt.
void finish_diamond(Surface& out, float x, float y, float r, bool rim, bool coarse) {
    const float pi = 3.14159265f;
    const bool inner = r < (rim ? 2.0f : 0.40f);
    const int wedges = rim ? 24 : (inner ? 8 : 16);
    const float a = std::atan2(y, x) + pi;
    const float t = a / (2.0f * pi) * static_cast<float>(wedges);
    const int id = static_cast<int>(std::floor(t));
    const float within = t - std::floor(t);
    const float tilt = 0.25f + 0.55f * hash2(id, inner ? 1 : 2, rim ? 77 : 71);
    const float centre_a = (static_cast<float>(id) + 0.5f) / static_cast<float>(wedges) * 2.0f * pi - pi;
    out.bump = {std::cos(centre_a) * tilt * (inner ? -1.0f : 1.0f), std::sin(centre_a) * tilt * (inner ? -1.0f : 1.0f), 0.0f};
    const float edge = std::min(within, 1.0f - within) * static_cast<float>(wedges) * (r + 0.05f);
    out.base = out.base * (0.82f + 0.30f * hash2(id, 5, 73));
    if (edge < 0.05f * (coarse ? 2.0f : 1.0f)) { // the cut's edge, a thin bright line
        out.base = mix(out.base, Vec3{1.0f, 1.0f, 1.0f}, 0.5f);
    }
}

// Obsidian: glassy violet-black with swirls under the surface, flecks, and a thin gold ring.
void finish_obsidian(Surface& out, float x, float y, float r, bool rim, bool coarse) {
    const float detail = coarse ? 0.55f : 1.0f;
    const float sw = fbm(x * 4.0f + fbm(x * 3.0f, y * 3.0f, 2, 91) * 2.0f, y * 4.0f, 3, 93);
    out.base = out.base + Vec3{0.16f, 0.07f, 0.30f} * sw;
    out.bump = {(sw - 0.5f) * 0.02f * detail, (sw - 0.5f) * 0.02f * detail, 0.0f};
    if (!rim) { // faint sparkle flecks in the stone: lilac, and now and then gold
        const float lilac = std::pow(value_noise(x * 34.0f, y * 34.0f, 95), 28.0f);
        const float gold = std::pow(value_noise(x * 23.0f, y * 23.0f, 97), 40.0f);
        out.base = out.base + Vec3{0.85f, 0.75f, 1.00f} * lilac * 0.9f * detail + Vec3{1.0f, 0.78f, 0.30f} * gold * 0.9f * detail;
    }
    if (r > kFaceR - 0.015f && r < kFaceR + 0.045f) { // the thin ring where the rim starts: gold with a play of colour
        const float a = std::atan2(y, x);
        const float play = 0.5f + 0.5f * std::sin(a * 3.0f + 1.0f);
        out.base = mix(Vec3{0.95f, 0.72f, 0.22f}, Vec3{0.75f, 0.62f, 1.0f}, play * 0.45f);
    }
}

Surface finish(Material m, const Spec& s, float x, float y, float r, bool rim, bool coarse) {
    Surface out;
    out.base = hex(rim ? s.rim : s.face);
    const float detail = coarse ? 0.55f : 1.0f;
    switch (m) {
    case Material::Wood: { // rings of growth round a point off to one side, with a wobble
        const float cx = -0.9f;
        const float cy = 0.55f;
        const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
        const float wobble = fbm(x * 3.0f, y * 3.0f, 3, 5) * 0.9f;
        const float grain = 0.5f + 0.5f * std::sin(d * 38.0f + wobble * 9.0f);
        const float fine = value_noise(x * 60.0f, y * 5.0f, 9);
        const Vec3 dark = out.base * 0.62f;
        const Vec3 light = out.base * 1.18f;
        out.base = mix(dark, light, clamp01(grain * 0.8f + fine * 0.25f));
        out.bump = {(fine - 0.5f) * 0.04f * detail, (grain - 0.5f) * 0.05f * detail, 0.0f};
        break;
    }
    case Material::Iron: { // hammer marks: a mottled surface with small dents
        const float n1 = fbm(x * 14.0f, y * 14.0f, 3, 21);
        const float n2 = value_noise(x * 40.0f, y * 40.0f, 23);
        out.base = out.base * (0.80f + 0.45f * n1);
        out.bump = {(n1 - 0.5f) * 0.30f * detail, (fbm(x * 14.0f + 7.0f, y * 14.0f, 3, 21) - 0.5f) * 0.30f * detail, 0.0f};
        out.base = out.base * (0.94f + 0.12f * n2);
        break;
    }
    case Material::Bronze: { // warm, with a slight mottling
        const float n1 = fbm(x * 9.0f, y * 9.0f, 3, 31);
        out.base = mix(out.base * 0.86f, out.base * 1.12f, n1);
        out.bump = {(n1 - 0.5f) * 0.07f * detail, (fbm(x * 9.0f, y * 9.0f + 5.0f, 3, 31) - 0.5f) * 0.07f * detail, 0.0f};
        break;
    }
    case Material::Silver: { // brushed along x
        const float streak = value_noise(x * 3.0f, y * 90.0f, 41);
        out.base = out.base * (0.93f + 0.12f * streak);
        out.bump = {0.0f, (streak - 0.5f) * 0.03f * detail, 0.0f};
        break;
    }
    case Material::Gold: {
        const float streak = value_noise(x * 5.0f, y * 70.0f, 51);
        out.base = out.base * (0.95f + 0.10f * streak);
        out.bump = {0.0f, (streak - 0.5f) * 0.02f * detail, 0.0f};
        break;
    }
    case Material::Platinum: { // satin: fine radial brushing and a faint sparkle
        const float a = std::atan2(y, x);
        const float brush = value_noise(a * 38.0f, r * 3.0f, 61);
        const float spark = std::pow(value_noise(x * 70.0f, y * 70.0f, 63), 14.0f);
        out.base = out.base * (0.94f + 0.10f * brush) + Vec3{spark, spark, spark} * 0.35f * detail;
        out.bump = {0.0f, 0.0f, 0.0f};
        break;
    }
    case Material::Diamond:
        finish_diamond(out, x, y, r, rim, coarse);
        break;
    case Material::Obsidian:
        finish_obsidian(out, x, y, r, rim, coarse);
        break;
    }
    return out;
}

// ---- Pixels ---------------------------------------------------------------------------------------

struct Pixel {
    Vec3 colour;
    float alpha = 0.0f;
};

// Puts `top` over `under` ("over" in straight alpha).
Pixel over(Pixel top, Pixel under) {
    Pixel out;
    out.alpha = top.alpha + under.alpha * (1.0f - top.alpha);
    if (out.alpha < 1e-5f) {
        out.colour = under.colour;
        return out;
    }
    out.colour = (top.colour * top.alpha + under.colour * (under.alpha * (1.0f - top.alpha))) * (1.0f / out.alpha);
    return out;
}

// One of the ribbon's straps: from `a` to `b` along its axis, `wa` wide at `a` and `wb` at `b`.
Pixel ribbon_strap(const Spec& s, float px, float py, float ax, float ay, float bx, float by, float wa, float wb) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    const float ux = dx / len;
    const float uy = dy / len;
    const float rx = px - ax;
    const float ry = py - ay;
    const float t = (rx * ux + ry * uy) / len;
    const float side = rx * -uy + ry * ux;
    const float half = 0.5f * (wa + (wb - wa) * clamp01(t));
    Pixel p;
    const float cover = clamp01(half - std::fabs(side) + 0.5f) * clamp01(t * len + 0.5f) * clamp01((1.0f - t) * len + 0.5f);
    if (cover <= 0.0f) {
        return p;
    }
    const float u = side / half; // -1 .. 1 across
    Vec3 colour = hex(s.ribbon);
    const float centre = 1.0f - smoothstep(0.10f, 0.16f, std::fabs(u));
    if (s.edge_stripes) { // two thin gold lines near the edges, a faint violet thread down the middle
        const float lines = 1.0f - smoothstep(0.035f, 0.07f, std::fabs(std::fabs(u) - 0.80f));
        colour = mix(colour, hex(s.stripe), lines * 0.95f);
        colour = mix(colour, Vec3{0.42f, 0.30f, 0.80f}, centre * 0.35f);
    } else {
        colour = mix(colour, hex(s.stripe), centre * 0.9f);
    }
    const float weave = 0.93f + 0.07f * value_noise(px * 1.3f, py * 1.3f, 111);
    const float fold = 0.78f + 0.22f * std::cos(u * 1.4f);     // rounder across, dark at the edges
    const float shade = fold * (0.86f + 0.20f * (1.0f - clamp01(t))) * weave;
    colour = colour * shade;
    const float edge = smoothstep(0.80f, 1.0f, std::fabs(u)); // a darker hem
    colour = colour * (1.0f - 0.25f * edge);
    p.colour = colour;
    p.alpha = cover;
    return p;
}

Pixel disc_pixel(Material m, const Spec& s, const MedalFrame& f, float px, float py, bool coarse) {
    const float dx = (px - f.centre_x) / f.radius;
    const float dy = (py - f.centre_y) / f.radius;
    const float r = std::sqrt(dx * dx + dy * dy);
    Pixel p;
    // Anti-aliased by the distance to the edge in pixels.
    const float cover = clamp01((1.0f - r) * f.radius + 0.5f);
    if (cover <= 0.0f) {
        return p;
    }
    const bool rim = r >= kFaceR;
    const Profile prof = disc_profile(r, coarse);
    const float k = r > 1e-4f ? 1.0f / r : 0.0f;
    // The normal from the height's slope, with the finish's bump added.
    const float strength = 3.2f;
    Vec3 n{-prof.slope * dx * k * strength, -prof.slope * dy * k * strength, 1.0f};
    const Surface surf = finish(m, s, dx, dy, r, rim, coarse);
    n = normalised({n.x + surf.bump.x, n.y + surf.bump.y, 1.0f});
    Vec3 base = surf.base;
    // Ambient occlusion in the groove under the rim, and a darker edge.
    const float occlusion = 1.0f - 0.35f * (1.0f - smoothstep(0.0f, 0.07f, std::fabs(r - kFaceR) + (r < kFaceR ? 0.0f : 0.0f)));
    base = base * (r < kFaceR ? occlusion : 1.0f);
    Vec3 colour = light_surface(s, n, base);
    if (m == Material::Obsidian) { // the sweep: a broad band of light across the upper left, and a crisp streak by it
        const float along = dx * 0.8f + dy * 0.6f;
        const float band = std::exp(-std::pow((along + 0.30f) * 3.0f, 2.0f));
        const float streak = std::exp(-std::pow((along + 0.56f) * 11.0f, 2.0f));
        const float where = r < kFaceR ? 1.0f : 0.45f;
        colour = colour + Vec3{0.50f, 0.42f, 0.78f} * band * where * 0.9f + Vec3{0.95f, 0.92f, 1.0f} * streak * where * 0.8f;
    }
    if (m == Material::Diamond) {
        const float spark = std::pow(value_noise(dx * 9.0f, dy * 9.0f, 133), 22.0f);
        colour = colour + Vec3{1.0f, 1.0f, 1.0f} * spark * 1.4f;
    }
    // The outer edge catches a thin light on the upper left and goes dark on the lower right.
    const float edge = smoothstep(0.93f, 1.0f, r);
    const float facing = clamp01(0.5f + 0.5f * (dx * kLight.x + dy * kLight.y) / std::max(r, 1e-3f) * -1.0f);
    colour = colour * (1.0f - edge * 0.35f * (1.0f - facing));
    p.colour = {clamp01(colour.x), clamp01(colour.y), clamp01(colour.z)};
    p.alpha = cover;
    return p;
}

// The shadow under the medal: soft, down and to the right.
Pixel shadow_pixel(const MedalFrame& f, float px, float py, bool coarse) {
    const float off = f.radius * (coarse ? 0.05f : 0.06f);
    const float dx = px - (f.centre_x + off * 0.7f);
    const float dy = py - (f.centre_y + off * 1.3f);
    const float d = std::sqrt(dx * dx + dy * dy);
    Pixel p;
    p.colour = {0.0f, 0.0f, 0.02f};
    const float soft = f.radius * 0.16f;
    p.alpha = 0.42f * (1.0f - smoothstep(f.radius - soft, f.radius + soft, d));
    return p;
}

// The faint violet halo round the top tier's medal: strongest at the rim, fading out within a tenth of the radius.
// It is clear 1.5 px inside the frame on every side, so the frame's edge can never cut it (the Small frame has a
// 3 px margin, so its halo is thinner still).
Pixel glow_pixel(const MedalFrame& f, float px, float py) {
    const float d = std::sqrt((px - f.centre_x) * (px - f.centre_x) + (py - f.centre_y) * (py - f.centre_y));
    const float margin = std::min(std::min(f.centre_x, static_cast<float>(f.width) - f.centre_x), std::min(f.centre_y, static_cast<float>(f.height) - f.centre_y)) - f.radius;
    const float reach = std::max(0.0f, std::min(f.radius * 0.12f, margin - 1.5f));
    Pixel p;
    p.colour = {0.50f, 0.34f, 1.0f};
    p.alpha = 0.14f * (1.0f - smoothstep(f.radius - 2.0f, f.radius + reach, d));
    return p;
}

} // namespace

MedalFrame medal_frame(MedalSize size) { return size == MedalSize::Large ? kLarge : kSmall; }

MedalLook medal_look(Material material) {
    const Spec s = spec_of(material);
    return {s.icon_fill, s.emboss_light, s.emboss_dark, s.ribbon};
}

MedalPixels render_medal(Material material, MedalSize size) {
    const MedalFrame f = medal_frame(size);
    const Spec s = spec_of(material);
    const bool coarse = size == MedalSize::Small;
    MedalPixels out;
    out.width = f.width;
    out.height = f.height;
    out.rgba.assign(static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height) * 4U, 0);
    for (int y = 0; y < f.height; ++y) {
        for (int x = 0; x < f.width; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            Pixel pix = shadow_pixel(f, px, py, coarse);
            if (material == Material::Obsidian) {
                pix = over(glow_pixel(f, px, py), pix);
            }
            if (!coarse) {
                const float cx = f.centre_x;
                // Two straps, crossing behind the disc: they end well inside its top edge, so no end shows.
                const float tuck = f.centre_y - f.radius * 0.6f;
                pix = over(ribbon_strap(s, px, py, cx + 34.0f, 0.0f, cx + 2.0f, tuck, 46.0f, 36.0f), pix);
                pix = over(ribbon_strap(s, px, py, cx - 34.0f, 0.0f, cx - 2.0f, tuck, 46.0f, 36.0f), pix);
            }
            pix = over(disc_pixel(material, s, f, px, py, coarse), pix);
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(f.width) + static_cast<std::size_t>(x)) * 4U;
            out.rgba[at + 0] = static_cast<std::uint8_t>(std::lround(clamp01(pix.colour.x) * 255.0f));
            out.rgba[at + 1] = static_cast<std::uint8_t>(std::lround(clamp01(pix.colour.y) * 255.0f));
            out.rgba[at + 2] = static_cast<std::uint8_t>(std::lround(clamp01(pix.colour.z) * 255.0f));
            out.rgba[at + 3] = static_cast<std::uint8_t>(std::lround(clamp01(pix.alpha) * 255.0f));
        }
    }
    return out;
}

} // namespace etude
