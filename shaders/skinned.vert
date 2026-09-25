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
    float worldUvScale;
} pc;

// set 3 : matrices d'os. Au repos, toutes a l'identite.
layout(set = 3, binding = 0) uniform BoneMatrices {
    mat4 bones[256];
} skin;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 5) in ivec4 inBoneIds;
layout(location = 6) in vec4  inWeights;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) flat out int fragRenderMode;
layout(location = 4) out vec3 fragTangent;

void main() {
    // Somme des poids nulle = sommet non skinne : on garde la position brute.
    float total = inWeights.x + inWeights.y + inWeights.z + inWeights.w;

    mat4 skinMat = mat4(1.0);
    if (total > 0.0001) {
        skinMat = inWeights.x * skin.bones[inBoneIds.x]
                + inWeights.y * skin.bones[inBoneIds.y]
                + inWeights.z * skin.bones[inBoneIds.z]
                + inWeights.w * skin.bones[inBoneIds.w];
    }

    vec4 skinnedPos = skinMat * vec4(inPosition, 1.0);
    mat3 skinRot = mat3(skinMat);

    vec4 worldPos = pc.model * skinnedPos;
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    fragTangent  = mat3(pc.model) * (skinRot * inTangent);
    fragNormal   = mat3(transpose(inverse(pc.model))) * (skinRot * inNormal);
    fragRenderMode = pc.renderMode;
}