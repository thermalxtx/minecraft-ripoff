#version 330 core
// Packed chunk vertex: 2 x uint32 (see mesher.cpp)
layout(location = 0) in uvec2 aData;
uniform mat4 uViewProj;
uniform vec3 uOffset;      // section origin relative to the camera
uniform vec3 uCamPos;      // camera world position (for animation phase)
uniform float uTime;
uniform float uDaylight;   // 0.2 (night) .. 1.0 (day)
uniform vec3 uSkyTint;
uniform float uBright;     // 0 = moody, 1 = bright (gamma lift)
out vec3 vUV;
out vec3 vLight;
out float vFogDist;
const float SHADE[7] = float[7](0.62, 0.62, 1.0, 0.5, 0.82, 0.82, 0.92);
const float AOF[4] = float[4](0.42, 0.62, 0.8, 1.0);
void main() {
    uint a = aData.x, b = aData.y;
    vec3 p = vec3(float(a & 511u), float((a >> 9) & 511u), float((a >> 18) & 511u)) / 16.0;
    uint n = (a >> 27) & 7u;
    uint ao = (a >> 30) & 3u;
    uint layer = b & 1023u;
    vec2 uv = vec2(float((b >> 10) & 31u), float((b >> 15) & 31u)) / 16.0;
    float sky = float((b >> 20) & 15u);
    float blk = float((b >> 24) & 15u);
    uint flags = (b >> 28) & 15u;
    vec3 wp = p + uOffset;
    vec3 world = wp + uCamPos;
    if ((flags & 1u) != 0u) {
        wp.x += sin(world.x * 0.7 + world.z * 0.4 + uTime * 1.8) * 0.06;
        wp.z += cos(world.z * 0.7 + world.x * 0.3 + uTime * 1.5) * 0.05;
    }
    if ((flags & 2u) != 0u) wp.y += (sin(world.x * 1.3 + uTime * 1.6) + cos(world.z * 1.1 + uTime * 1.3)) * 0.035 - 0.07;
    if ((flags & 4u) != 0u) layer += uint(mod(uTime * 5.0, 4.0));
    gl_Position = uViewProj * vec4(wp, 1.0);
    float bs = pow(0.8, 15.0 - sky) * uDaylight;
    float bb = pow(0.8, 15.0 - blk);
    vec3 light = max(vec3(bs) * uSkyTint, vec3(bb) * vec3(1.0, 0.88, 0.7));
    light = mix(light, sqrt(light), uBright * 0.6);
    light = max(light, vec3(0.035));
    if ((flags & 8u) != 0u) light = vec3(1.0);
    vLight = light * SHADE[n] * AOF[ao];
    vUV = vec3(uv, float(layer));
    vFogDist = length(wp);
}
