#version 330 core
uniform sampler2DArray uTex;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uAlphaTest;
uniform float uAlphaMul;
in vec3 vUV;
in vec3 vLight;
in float vFogDist;
out vec4 frag;
void main() {
    vec4 c = texture(uTex, vUV);
    if (c.a < uAlphaTest) discard;
    c.rgb *= vLight;
    float f = clamp((vFogDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    c.rgb = mix(c.rgb, uFogColor, f);
    c.a *= uAlphaMul;
    frag = c;
}
