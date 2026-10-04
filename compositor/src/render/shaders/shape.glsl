// Il rettangolo arrotondato (docs/renderer.md §8.1): la distanza dal bordo
// (SDF) calcolata per ogni pixel fisico, con un pixel esatto di
// antialiasing. I lati dritti cadono sui bordi dei pixel e restano netti.
float roundedCoverage(vec2 p, vec4 rect, float radius)
{
    const vec2 halfSize = rect.zw * 0.5;
    const float r = min(radius, min(halfSize.x, halfSize.y));
    const vec2 q = abs(p - (rect.xy + halfSize)) - halfSize + r;
    const float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
    return clamp(0.5 - dist, 0.0, 1.0);
}

// Il ritaglio della forma, se c'è (flags bit 1).
float shapeClip()
{
    return (pc.flags & 2u) != 0u ? roundedCoverage(gl_FragCoord.xy, pc.shapeRect, pc.shape.x) : 1.0;
}
