#pragma once
#include <glm/glm.hpp>

// Donnees "push constant" partagees par tous les draws (objets + grille).
// DOIT rester synchronise avec le bloc layout(push_constant) des shaders
// (shader.vert ET shader.frag).
struct PushConstants {
    glm::mat4 model;                       // offset 0
    int renderMode;                        // offset 64
    int hasNormalMap;                      // offset 68
    alignas(16) glm::vec4 baseColor{1.0f}; // offset 80
    float metallic  = 0.0f;                // offset 96
    float roughness = 0.5f;                // offset 100
    float worldUvScale = 0.0f;             // offset 104 ; 0 = UV du maillage
};