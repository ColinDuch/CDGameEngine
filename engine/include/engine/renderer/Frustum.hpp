#pragma once

#include <glm/glm.hpp>
#include <array>

// Les 6 plans du frustum, extraits d'une matrice view-projection.
// Convention : un point est DEDANS si dot(plan.xyz, p) + plan.w >= 0.
struct Frustum {
    std::array<glm::vec4, 6> planes;

    // vp = projection * view (la projection deja Vulkan-flippee du moteur).
    static Frustum fromMatrix(const glm::mat4& vp) {
        Frustum f;
        // Lignes de la matrice (glm est column-major : m[colonne][ligne]).
        glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

        f.planes[0] = r3 + r0;   // gauche
        f.planes[1] = r3 - r0;   // droite
        f.planes[2] = r3 + r1;   // bas
        f.planes[3] = r3 - r1;   // haut
        f.planes[4] = r2;        // near  (Vulkan : profondeur 0..1)
        f.planes[5] = r3 - r2;   // far

        for (auto& p : f.planes) {
            float len = glm::length(glm::vec3(p));
            if (len > 0.0f) p /= len;
        }
        return f;
    }

    // Test d'une boite alignee sur les axes du MONDE.
    // Rejette uniquement ce qui est franchement dehors (pas de faux negatif).
    bool intersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const {
        for (const auto& p : planes) {
            // Sommet de la boite le plus "avance" dans la direction du plan.
            glm::vec3 farthestCorner(p.x >= 0.0f ? mx.x : mn.x,
                        p.y >= 0.0f ? mx.y : mn.y,
                        p.z >= 0.0f ? mx.z : mn.z);
            if (glm::dot(glm::vec3(p), farthestCorner) + p.w < 0.0f) return false;
        }
        return true;
    }
};