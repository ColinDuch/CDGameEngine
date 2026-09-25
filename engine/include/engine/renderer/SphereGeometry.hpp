#pragma once

#include <engine/renderer/Vertex.hpp>
#include <cmath>
#include <vector>

// Sphere UV en Z-up, avec normales, tangentes et UV : sert d'apercu de materiau.
inline void generateSphere(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                           int segments = 64, int rings = 32, float radius = 1.0f) {
    vertices.clear();
    indices.clear();

    const float PI = 3.14159265359f;

    for (int y = 0; y <= rings; ++y) {
        float v = static_cast<float>(y) / rings;
        float phi = v * PI;                       // 0 = pole +Z
        for (int x = 0; x <= segments; ++x) {
            float u = static_cast<float>(x) / segments;
            float theta = u * 2.0f * PI;

            glm::vec3 n(std::sin(phi) * std::cos(theta),
                        std::sin(phi) * std::sin(theta),
                        std::cos(phi));

            Vertex vert{};
            vert.position = n * radius;
            vert.normal   = n;
            vert.tangent  = glm::vec3(-std::sin(theta), std::cos(theta), 0.0f);
            vert.texCoord = glm::vec2(u, v);
            vertices.push_back(vert);
        }
    }

    // Sens CCW vu de l'exterieur (cull BACK / frontFace CCW).
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t v00 = y * (segments + 1) + x;
            uint32_t v10 = v00 + 1;
            uint32_t v01 = v00 + (segments + 1);
            uint32_t v11 = v01 + 1;

            indices.push_back(v00); indices.push_back(v01); indices.push_back(v10);
            indices.push_back(v10); indices.push_back(v01); indices.push_back(v11);
        }
    }
}
