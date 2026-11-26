#version 330 core
uniform sampler2DArray uTex;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uAlphaTest;
uniform float uAlphaMul;
uniform float uTime;
in vec3 vUV;
in vec3 vLight;
in float vFogDist;
in vec3 vWorld;
flat in uint vWater;
out vec4 frag;
void main() {
    vec4 c;
    if (vWater == 1u) {
        // water surface: blend two ripple layers at different scales, drifting in different
        // directions in world space, so the 16x16 tile never lines up into a visible grid
        vec2 p = vWorld.xz;
        float l = vUV.z;
        vec4 a = texture(uTex, vec3(p * 0.37 + vec2(uTime * 0.045, uTime * 0.02), l + floor(mod(uTime * 2.0, 4.0))));
        vec4 b = texture(uTex, vec3(vec2(p.y, -p.x) * 0.21 + vec2(-uTime * 0.03, uTime * 0.035), l + floor(mod(uTime * 2.0 + 2.0, 4.0))));
        vec4 d = texture(uTex, vec3(p * 0.11 + vec2(uTime * 0.01, -uTime * 0.012), l));
        c = a * 0.42 + b * 0.38 + d * 0.2;
        float sparkle = smoothstep(0.62, 0.8, max(a.b, b.b));
        c.rgb += vec3(0.1, 0.12, 0.14) * sparkle;
    } else c = texture(uTex, vUV);
    if (c.a < uAlphaTest) discard;
    c.rgb *= vLight;
    float f = clamp((vFogDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    c.rgb = mix(c.rgb, uFogColor, f);
    c.a *= uAlphaMul;
    frag = c;
}
