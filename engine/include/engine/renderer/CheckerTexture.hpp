#pragma once

#include <vector>
#include <cstdint>

// Genere une texture damier RGBA en memoire.
// size = cote en pixels, checks = nombre de cases par cote.
inline std::vector<unsigned char> generateCheckerboard(int size, int checks) {
    std::vector<unsigned char> pixels(static_cast<size_t>(size) * size * 4);
    int cellSize = size / checks;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool isLight = (((x / cellSize) + (y / cellSize)) % 2) == 0;
            unsigned char c = isLight ? 220 : 60;

            size_t idx = (static_cast<size_t>(y) * size + x) * 4;
            pixels[idx + 0] = c;   // R
            pixels[idx + 1] = c;   // G
            pixels[idx + 2] = c;   // B
            pixels[idx + 3] = 255; // A
        }
    }
    return pixels;
}