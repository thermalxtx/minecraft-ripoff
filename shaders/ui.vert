#version 330 core
layout(location = 0) in vec2 aPos;    // pixels
layout(location = 1) in vec4 aUV;     // u, v, layer, mode (0 colour, 1 texture array, 2 font)
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec4 vUV;
out vec4 vColor;
void main() {
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
