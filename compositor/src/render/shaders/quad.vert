#version 450
// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#extension GL_GOOGLE_include_directive : require

// One quad per draw: four vertices as a triangle strip, no vertex buffer.
// Position and texture coordinates come from the push constants.

#include "push.glsl"

layout(location = 0) out vec2 uv;

void main()
{
    const vec2 corner = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1);
    const vec2 pos = pc.dst.xy + corner * pc.dst.zw;
    gl_Position = vec4(pos / pc.target * 2.0 - 1.0, 0.0, 1.0);
    uv = pc.uvOrigin + corner.x * pc.uvX + corner.y * pc.uvY;
}
