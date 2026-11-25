#version 330 core
uniform sampler2DArray uTex;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uAlphaTest;
in vec3 vUV;
in vec4 vColor;
in float vFogDist;
out vec4 frag;
void main() {
    vec4 c = texture(uTex, vUV) * vColor;
    if (c.a < uAlphaTest) discard;
    float f = clamp((vFogDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    c.rgb = mix(c.rgb, uFogColor, f);
    frag = c;
}
