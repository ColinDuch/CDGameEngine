#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPosition;
    float shininess;
    float exposure;
    mat4 lightSpace;
    float shadowStrength;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int renderMode;
    int hasNormalMap;
    vec4 baseColor;
    float metallic;
    float roughness;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) out vec3 fragTangent;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;

layout(location = 3) flat out int fragRenderMode;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    fragTangent = mat3(pc.model) * inTangent;

    // Transformation correcte de la normale (gere la mise a l'echelle non uniforme).
    fragNormal = mat3(transpose(inverse(pc.model))) * inNormal;
    fragRenderMode = pc.renderMode;
}