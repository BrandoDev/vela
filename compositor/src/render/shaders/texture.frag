#version 450
#extension GL_GOOGLE_include_directive : require

// Una texture (finestra, pannello, istantanea, cursore). I pixel delle app
// sono codificati sRGB e premoltiplicati: si riportano in spazio lineare
// (§7.5) togliendo e rimettendo l'alfa, così anche i bordi semitrasparenti
// sono giusti. La vista di destinazione è _SRGB: la GPU fonde in lineare e
// codifica scrivendo.

#include "push.glsl"
#include "shape.glsl"

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c)
{
    const bvec3 low = lessThanEqual(c, vec3(0.04045));
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, low);
}

// Ingrandimento di qualità (§3.3): Catmull-Rom, 16 texel pesati con 9
// letture bilineari. Più nitido del bilineare, senza i gradini del nearest.
vec4 catmullRom(vec2 coord)
{
    const vec2 size = vec2(textureSize(tex, 0));
    const vec2 pos = coord * size;
    const vec2 center = floor(pos - 0.5) + 0.5;
    const vec2 f = pos - center;
    const vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const vec2 w3 = f * f * (-0.5 + 0.5 * f);
    const vec2 w12 = w1 + w2;
    const vec2 p0 = (center - 1.0) / size;
    const vec2 p12 = (center + w2 / w12) / size;
    const vec2 p3 = (center + 2.0) / size;
    vec4 c = texture(tex, vec2(p0.x, p0.y)) * w0.x * w0.y;
    c += texture(tex, vec2(p12.x, p0.y)) * w12.x * w0.y;
    c += texture(tex, vec2(p3.x, p0.y)) * w3.x * w0.y;
    c += texture(tex, vec2(p0.x, p12.y)) * w0.x * w12.y;
    c += texture(tex, vec2(p12.x, p12.y)) * w12.x * w12.y;
    c += texture(tex, vec2(p3.x, p12.y)) * w3.x * w12.y;
    c += texture(tex, vec2(p0.x, p3.y)) * w0.x * w3.y;
    c += texture(tex, vec2(p12.x, p3.y)) * w12.x * w3.y;
    c += texture(tex, vec2(p3.x, p3.y)) * w3.x * w3.y;
    // I lobi negativi possono uscire dall'intervallo: si riporta dentro,
    // con il colore mai oltre l'alfa (premoltiplicato).
    c.a = clamp(c.a, 0.0, 1.0);
    c.rgb = clamp(c.rgb, 0.0, c.a);
    return c;
}

void main()
{
    vec4 c = (pc.flags & 1u) != 0u ? catmullRom(uv) : texture(tex, uv);
    if (c.a > 0.0) {
        c.rgb = srgbToLinear(clamp(c.rgb / c.a, 0.0, 1.0)) * c.a;
    } else {
        c.rgb = srgbToLinear(clamp(c.rgb, 0.0, 1.0)); // additivo: raro, ma lecito
    }
    outColor = c * (pc.alpha * shapeClip());
}
