#version 450 

// l'ombre pour les meshes skinnes :
// meme calcul de profondeur 
// mais la position pas d'abord par
// les matrices d os 

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 5) in ivec4 inBoneIds;
layout(location = 6) in vec4 inWeights;

layout(push_constant) uniform ShadowPush {
    mat4 lightSpace;
    mat4 model;
} pc;

layout(set = 0, binding = 0) uniform BoneMatrices {
    mat4 bones[256];
} skin;

void main() {
    float total = inWeights.x + inWeights.y + inWeights.z + inWeights.w;

    mat4 skinMat = mat4(1.0);
    if (total > 0.0001) {
        skinMat = inWeights.x * skin.bones[inBoneIds.x]
                + inWeights.y * skin.bones[inBoneIds.y]
                + inWeights.z * skin.bones[inBoneIds.z]
                + inWeights.w * skin.bones[inBoneIds.w];
    }

    gl_Position = pc.lightSpace * pc.model * (skinMat * vec4(inPosition, 1.0));
}