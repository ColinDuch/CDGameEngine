#pragma once

#include <engine/renderer/Vertex.hpp>
#include <engine/renderer/Skeleton.hpp>
#include <engine/renderer/Animation.hpp>

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

// Un materiau : couleur de base (Kd) + textures optionnelles.
struct Material {
    glm::vec3 baseColor{1.0f, 1.0f, 1.0f};
    std::string diffuseTexturePath;   // vide si pas de map_Kd
    std::string normalTexturePath;    // vide si pas de normal map
    bool referencesTexture = false;   // true si le .mtl reference une texture diffuse (meme fichier manquant)
    float metallic  = 0.0f;
    float roughness = 0.5f;
};

// Un sous-mesh : une plage d'indices qui utilise un materiau donne.
struct SubMesh {
    uint32_t indexOffset = 0;   // position de depart dans le tableau d'indices global
    uint32_t indexCount  = 0;   // nombre d'indices de ce sous-mesh
    int      materialIndex = 0; // index dans le tableau materials
};

class Model {
public:
    // yUpToZUp : redresse un modele exporte en Y-up pour notre monde en Z-up
    explicit Model(const std::string& filepath, bool yUpToZUp = false);

    const std::vector<Vertex>& getVertices() const { return vertices; }
    const std::vector<uint32_t>& getIndices() const { return indices; }
    bool getHasUV() const { return hasUV; }

    const Skeleton& getSkeleton() const { return skeleton; }
    const std::vector<VertexSkin>& getSkins() const { return skins; }
    bool isSkinned() const { return !skeleton.empty() && !skins.empty(); }

    // Nouvelle interface multi-materiaux.
    const std::vector<Material>& getMaterials() const { return materials; }
    const std::vector<SubMesh>& getSubMeshes() const { return submeshes; }

    // Anciens accesseurs (compat) : renvoient le 1er materiau.
    const std::string& getDiffuseTexturePath() const { return diffuseTexturePath; }
    const std::string& getNormalTexturePath() const { return normalTexturePath; }

    const std::vector<AnimationClip>& getClips() const { return clips; }

    /*
    charge une animation depuis un fichier séparer 
    en associant les canaux au squelette par leurs noms d'os
    */
    static std::vector<AnimationClip> loadAnimations(const std::string& path, const Skeleton& skeleton);


private:
    void loadModel(const std::string& filepath, bool yUpToZUp);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    bool hasUV = false;

    Skeleton skeleton;
    std::vector<VertexSkin> skins;

    std::vector<Material> materials;
    std::vector<SubMesh> submeshes;

    std::string diffuseTexturePath;
    std::string normalTexturePath;

    std::vector<AnimationClip> clips;
};