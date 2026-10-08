// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

// The rounded rectangle (docs/renderer.md §8.1): the distance from the edge
// (SDF) computed for each physical pixel, with exactly one pixel of
// antialiasing. Straight sides fall on pixel boundaries and stay crisp.
float roundedCoverage(vec2 p, vec4 rect, float radius)
{
    const vec2 halfSize = rect.zw * 0.5;
    const float r = min(radius, min(halfSize.x, halfSize.y));
    const vec2 q = abs(p - (rect.xy + halfSize)) - halfSize + r;
    const float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
    return clamp(0.5 - dist, 0.0, 1.0);
}

// The shape's clip, if any (flags bit 1).
float shapeClip()
{
    return (pc.flags & 2u) != 0u ? roundedCoverage(gl_FragCoord.xy, pc.shapeRect, pc.shape.x) : 1.0;
}
