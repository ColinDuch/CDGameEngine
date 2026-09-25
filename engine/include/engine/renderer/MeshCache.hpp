#pragma once

#include <engine/renderer/Vertex.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

// Un sous-mesh tel qu'il est stocke dans le cache binaire.
struct MeshCacheSubMesh {
    uint32_t  indexOffset = 0;
    uint32_t  indexCount = 0;
    glm::vec3 baseColor{1.0f, 1.0f, 1.0f};
    float     metallic = 0.0f;
    float     roughness = 0.5f;
    bool      referencesTexture = false;
    std::string diffusePath;
    std::string normalPath;
};

// Geometrie complete d'un Mesh, prete a etre ecrite ou relue.
struct MeshCacheData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshCacheSubMesh> subMeshes;
    bool hasUV = true;
};

namespace MeshCache {
    // Extension du format natif : ".emesh"
    bool isCacheFile(const std::string& path);

    void save(const std::string& path, const MeshCacheData& data);
    MeshCacheData load(const std::string& path);

    // --- Cache d'import (global, jetable) ---
    // Chemin du .emesh correspondant a un fichier source (.obj/.fbx/...).
    std::string cachePathFor(const std::string& sourcePath);
    // Vrai si ce cache existe et est plus recent que sa source.
    bool isFresh(const std::string& cachePath, const std::string& sourcePath);
    
}