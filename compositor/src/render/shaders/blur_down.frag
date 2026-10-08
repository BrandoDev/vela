#version 450
// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#extension GL_GOOGLE_include_directive : require

// Live blur (docs/renderer.md §8.3), dual Kawase: down to half resolution with
// five bilinear reads. The first reads the output (sRGB view: it arrives
// linear), the others the previous level (16 bit). shape.xy: half a source
// texel for the reach; shapeRect: where reading is allowed (texture
// coordinates), the rest of the image belongs to others.

#include "push.glsl"

layout(set = 0, binding = 0) uniform sampler2D source;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

vec4 tap(vec2 at)
{
    return texture(source, clamp(at, pc.shapeRect.xy, pc.shapeRect.zw));
}

void main()
{
    const vec2 h = pc.shape.xy;
    vec4 sum = tap(uv) * 4.0;
    sum += tap(uv - h);
    sum += tap(uv + h);
    sum += tap(uv + vec2(h.x, -h.y));
    sum += tap(uv - vec2(h.x, -h.y));
    outColor = sum / 8.0;
}
