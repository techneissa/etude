// SPDX-License-Identifier: GPL-3.0-or-later
#version 450
// The Neon style's block glow, drawn over SDL's glow nine-slice mesh with additive blending.
// The mesh's texture coordinates are 0.5 along the block's own edge and 0 or 1 at the outer edge
// of the glow, so |uv - 0.5| * 2 is the distance out from the block, 0 at the block and 1 at the
// end of the glow, round at the corners. The old glow sprite (the soft wide falloff) stays as the
// halo; this adds a tight core that hugs the edge and turns toward white, the way a lit tube
// blooms: added on top of the colour it blows out to white at the block's edge and keeps the
// note's colour in the halo.
//
// Resources follow SDL's GPU renderer: the glow sprite is fragment sampler 0 (set 2), the shader's
// own numbers are fragment uniform buffer 0 (set 3), the inputs are the vertex colour and the
// texture coordinate that SDL's textured vertex shader passes on.

layout(set = 2, binding = 0) uniform sampler2D u_glow;

layout(set = 3, binding = 0) uniform Params {
    // x: gain of the core, y: gain of the halo (the sprite), z: how far the core turns white,
    // w: how tight the core is (larger hugs the edge closer).
    vec4 shape;
} fx;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec2 q = abs(v_uv - vec2(0.5)) * 2.0;
    float d = min(length(q), 1.0);
    float edge = 1.0 - d * d * (3.0 - 2.0 * d); // 0 at the end of the glow, smooth
    float core = exp(-fx.shape.w * d * d) * edge;
    float halo = texture(u_glow, v_uv).a;
    float a = fx.shape.x * core + fx.shape.y * halo;
    vec3 colour = mix(v_color.rgb, vec3(1.0), clamp(fx.shape.z * core, 0.0, 1.0));
    o_color = vec4(colour, v_color.a * a);
}
