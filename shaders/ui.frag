#version 330 core
uniform sampler2DArray uTex;
uniform sampler2D uFont;
in vec4 vUV;
in vec4 vColor;
out vec4 frag;
void main() {
    vec4 c = vColor;
    if (vUV.w > 1.5) c.a *= texture(uFont, vUV.xy).r;
    else if (vUV.w > 0.5) c *= texture(uTex, vUV.xyz);
    if (c.a < 0.01) discard;
    frag = c;
}
