#pragma once

#include <cstdint>

// Constantes de configuration globales du moteur, regroupees ici pour
// eviter les valeurs magiques dispersees dans le code.
namespace Config {
    // Fenetre
    inline constexpr int WindowWidth = 800;
    inline constexpr int WindowHeight = 600;
    inline constexpr const char* WindowTitle = "GameEngine";

    // Rendu
    inline constexpr uint32_t DescriptorPoolCapacity = 256;

    // Scene
    inline constexpr const char* SceneExportPath = "scene_export.gltf";
    
}