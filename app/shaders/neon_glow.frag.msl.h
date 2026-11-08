// SPDX-License-Identifier: GPL-3.0-or-later
// Written by scripts/build-shaders.sh from app/shaders/neon_glow.frag. Do not edit.
#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr char kNeonGlowFragMsl[] = R"shader(#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Params
{
    float4 shape;
};

struct main0_out
{
    float4 o_color [[color(0)]];
};

struct main0_in
{
    float4 v_color [[user(locn0)]];
    float2 v_uv [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant Params& fx [[buffer(0)]], texture2d<float> u_glow [[texture(0)]], sampler u_glowSmplr [[sampler(0)]])
{
    main0_out out = {};
    float2 q = abs(in.v_uv - float2(0.5)) * 2.0;
    float d = fast::min(length(q), 1.0);
    float edge = 1.0 - ((d * d) * (3.0 - (2.0 * d)));
    float core = exp(((-fx.shape.w) * d) * d) * edge;
    float halo = u_glow.sample(u_glowSmplr, in.v_uv).w;
    float a = (fx.shape.x * core) + (fx.shape.y * halo);
    float3 colour = mix(in.v_color.xyz, float3(1.0), float3(fast::clamp(fx.shape.z * core, 0.0, 1.0)));
    out.o_color = float4(colour, in.v_color.w * a);
    return out;
}
)shader";
