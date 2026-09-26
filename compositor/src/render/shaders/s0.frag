#version 450

// Scena di prova della tappa S0: lo sfumato del tema e una barra che
// attraversa lo schermo a velocità costante, guidata dal tempo previsto di
// presentazione. Se il ritmo dei frame è giusto, la barra scorre liscia a
// qualunque frequenza.

layout(push_constant) uniform Params {
    vec4 params; // x: tempo (s), y: larghezza (px fisici), z: altezza
} pc;

layout(location = 0) out vec4 color;

// sRGB -> lineare: la destinazione è una vista _SRGB, si scrive in lineare.
vec3 linear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main()
{
    const vec2 size = pc.params.yz;
    const vec2 uv = gl_FragCoord.xy / size;

    // Lo sfumato si calcola in lineare: niente bande scure a metà.
    const vec3 top = linear(vec3(0x06, 0x18, 0x2d) / 255.0);
    const vec3 bottom = linear(vec3(0x15, 0x3d, 0x57) / 255.0);
    vec3 c = mix(top, bottom, uv.y);

    // Un giro dello schermo ogni due secondi.
    const float barWidth = 64.0;
    const float x = fract(pc.params.x * 0.5) * (size.x + barWidth) - barWidth;
    if (gl_FragCoord.x >= x && gl_FragCoord.x < x + barWidth) {
        c = linear(vec3(0x5b, 0x8c, 0xff) / 255.0);
    }
    color = vec4(c, 1.0);
}
