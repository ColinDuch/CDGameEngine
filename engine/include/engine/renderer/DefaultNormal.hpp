#pragma once

#include <vector>

// Normal map "plate" par defaut : (128,128,255) = normale (0,0,1) en tangent space.
// Utilisee quand un modele n'a pas de normal map (le binding 2 doit etre rempli).
inline std::vector<unsigned char> generateFlatNormal() {
    // Une petite texture 2x2 bleue unie (RGBA).
    std::vector<unsigned char> pixels(2 * 2 * 4);
    for (int i = 0; i < 4; i++) {
        pixels[i * 4 + 0] = 128;  // R
        pixels[i * 4 + 1] = 128;  // G
        pixels[i * 4 + 2] = 255;  // B
        pixels[i * 4 + 3] = 255;  // A
    }
    return pixels;
}