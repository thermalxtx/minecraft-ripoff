#version 330 core
uniform mat4 uInvViewProj;   // inverse of (projection * rotation-only view)
uniform vec3 uSunDir;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunset;        // sunrise/sunset glow colour (alpha in uSunsetAmt)
uniform float uSunsetAmt;
uniform float uStars;
uniform float uMoonPhase;
in vec2 vNdc;
out vec4 frag;
float hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
void main() {
    vec4 w = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(w.xyz / w.w);
    float up = dir.y;
    vec3 col = mix(uHorizon, uZenith, smoothstep(-0.05, 0.55, up));
    if (up < -0.05) col = mix(uHorizon, uHorizon * 0.55, smoothstep(-0.05, -0.5, up));
    // sunrise / sunset glow toward the sun
    float toward = max(dot(normalize(vec3(dir.x, 0.0, dir.z) + 1e-5), normalize(vec3(uSunDir.x, 0.0, uSunDir.z) + 1e-5)), 0.0);
    float glow = uSunsetAmt * pow(toward, 3.0) * (1.0 - smoothstep(0.0, 0.45, abs(up - 0.03)));
    col = mix(col, uSunset, clamp(glow, 0.0, 1.0));
    // stars (fixed to the sky, rotate with the sun)
    if (uStars > 0.0 && up > -0.1) {
        vec3 s = dir;
        float a = atan(uSunDir.y, uSunDir.x);
        float ca = cos(a), sa = sin(a);
        s = vec3(ca * s.x + sa * s.y, -sa * s.x + ca * s.y, s.z);
        vec3 cell = floor(s * 180.0);
        float h = hash(cell);
        if (h > 0.9975) col += vec3(0.9, 0.9, 1.0) * uStars * (0.5 + 0.5 * fract(h * 37.0));
    }
    // square pixel-art sun and moon
    vec3 sd = uSunDir;
    vec3 right = normalize(cross(sd, vec3(0.0, 0.0, 1.0)));
    vec3 upv = cross(right, sd);
    float d = dot(dir, sd);
    if (d > 0.0) {
        vec2 q = vec2(dot(dir, right), dot(dir, upv)) / d;
        float m = max(abs(q.x), abs(q.y));
        if (m < 0.075) {
            vec2 px = floor(q / 0.075 * 8.0);
            float edge = (abs(px.x) > 6.5 || abs(px.y) > 6.5) ? 0.85 : 1.0;
            col = mix(vec3(1.0, 0.86, 0.4), vec3(1.0, 1.0, 0.85), step(m, 0.045)) * edge;
        } else col += vec3(1.0, 0.8, 0.5) * 0.25 * pow(max(d, 0.0), 60.0);
    } else {
        float dm = -d;
        vec2 q = vec2(dot(dir, -right), dot(dir, upv)) / dm;
        float m = max(abs(q.x), abs(q.y));
        if (m < 0.055) {
            vec2 px = floor(q / 0.055 * 6.0);
            float crater = (hash(vec3(px, 3.0)) > 0.75) ? 0.75 : 1.0;
            float shadow = (px.x / 6.0 + 0.5) < uMoonPhase ? 0.25 : 1.0;
            col = mix(col, vec3(0.85, 0.88, 0.95) * crater, shadow);
        }
    }
    frag = vec4(col, 1.0);
}
