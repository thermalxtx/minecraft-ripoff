#version 330 core
// Dynamic geometry: entities, dropped items, particles, outlines, the first-person hand
layout(location = 0) in vec3 aPos;     // camera-relative position
layout(location = 1) in vec3 aUV;      // u, v, texture layer
layout(location = 2) in vec4 aColor;   // tint * light
uniform mat4 uViewProj;
out vec3 vUV;
out vec4 vColor;
out float vFogDist;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUV = aUV;
    vColor = aColor;
    vFogDist = length(aPos);
}
