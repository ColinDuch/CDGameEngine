#version 450

layout(location = 0) out vec2 outNdc;

void main() {
    // Triangle plein ecran, sans vertex buffer.
    vec2 pos[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    outNdc = pos[gl_VertexIndex];
    gl_Position = vec4(pos[gl_VertexIndex], 1.0, 1.0);
}