#pragma once

#include <engine/renderer/Vertex.hpp>
#include <vector>

// Genere les vertices d'une grille de lignes dans le plan XY (sol Z-up).
// halfSize = demi-etendue (grille de -halfSize a +halfSize), step = espacement des lignes.
inline std::vector<Vertex> generateGridVertices(int halfSize = 10, float step = 1.0f) {
    std::vector<Vertex> verts;

    auto pushLine = [&](glm::vec3 a, glm::vec3 b) {
        Vertex va{}; va.position = a; va.normal = {0.0f, 0.0f, 1.0f}; va.texCoord = {0.0f, 0.0f};
        Vertex vb{}; vb.position = b; vb.normal = {0.0f, 0.0f, 1.0f}; vb.texCoord = {0.0f, 0.0f};
        verts.push_back(va);
        verts.push_back(vb);
    };

    float extent = halfSize * step;

    // Lignes paralleles a l'axe X (varient en Y).
    for (int i = -halfSize; i <= halfSize; i++) {
        float y = i * step;
        pushLine({-extent, y, 0.0f}, {extent, y, 0.0f});
    }
    // Lignes paralleles a l'axe Y (varient en X).
    for (int i = -halfSize; i <= halfSize; i++) {
        float x = i * step;
        pushLine({x, -extent, 0.0f}, {x, extent, 0.0f});
    }

    return verts;
}