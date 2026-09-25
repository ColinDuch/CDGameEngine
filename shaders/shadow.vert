#version 450

// Passe d'ombre : on ne calcule QUE la profondeur vue depuis le soleil.
// Aucun fragment shader, aucun descriptor set : tout tient dans le push constant.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;

layout(push_constant) uniform ShadowPush {
    mat4 lightSpace;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.lightSpace * pc.model * vec4(inPosition, 1.0);
}
