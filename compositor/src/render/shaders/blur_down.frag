#version 450
#extension GL_GOOGLE_include_directive : require

// Sfocatura dal vivo (docs/renderer.md §8.3), dual Kawase: riduzione a metà
// risoluzione con cinque letture bilineari. La prima legge lo schermo (vista
// sRGB: arriva in lineare), le altre il livello precedente (16 bit).
// shape.xy: mezzo texel della sorgente per l'ampiezza; shapeRect: dove si può
// leggere (coordinate texture), il resto dell'immagine è di altri.

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
