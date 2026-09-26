#version 450
#extension GL_GOOGLE_include_directive : require

// Una texture (finestra, pannello, istantanea, cursore). I pixel delle app
// sono codificati sRGB e premoltiplicati: si riportano in spazio lineare
// (§7.5) togliendo e rimettendo l'alfa, così anche i bordi semitrasparenti
// sono giusti. La vista di destinazione è _SRGB: la GPU fonde in lineare e
// codifica scrivendo.

#include "push.glsl"

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c)
{
    const bvec3 low = lessThanEqual(c, vec3(0.04045));
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, low);
}

void main()
{
    vec4 c = texture(tex, uv);
    if (c.a > 0.0) {
        c.rgb = srgbToLinear(clamp(c.rgb / c.a, 0.0, 1.0)) * c.a;
    } else {
        c.rgb = srgbToLinear(clamp(c.rgb, 0.0, 1.0)); // additivo: raro, ma lecito
    }
    outColor = c * pc.alpha;
}
