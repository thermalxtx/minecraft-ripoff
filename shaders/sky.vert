#version 330 core
// Fullscreen triangle; the fragment shader reconstructs the view direction
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 0.9999, 1.0);
}
