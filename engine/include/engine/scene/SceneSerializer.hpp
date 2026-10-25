#pragma once

#include <engine/renderer/SceneObject.hpp>
#include <engine/renderer/Camera.hpp>
#include <memory>
#include <string>
#include <vector>

// un fichier d animation charge dasn le projet ainsi que son mesh de ref
struct AnimationRefData {
    std::string meshName;  //nom affiche du mesh
    std::string file;  //chemin du fbx relatif au projet
};

struct RemovedClipData {
    std::string meshName;
    std::string clipName;
};

struct NotifyData {
    std::string meshName;
    std::string clipName;
    float time = 0.0f;
    std::string name;
};

struct SceneObjectData {
    std::string modelPath;
    bool yUpToZUp = false;  // modele redresse a l import (y-up -> z-up)
    std::string texturePath;
    std::string scriptPath;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float worldUvScale = 0.0f;
    int materialIndex = -1;
    int parent = -1;
    std::string name;
    uint32_t id = 0;    // 0 = absent du fichier (ancienne scene)
    SceneObject::BodyType bodyType = SceneObject::BodyType::None;
    float mass = 1.0f;
    std::string animClipName; // nom du clip pas son index car ils peuvent bouger
    float animSpeed = 1.0f;
    bool animLoop = true;
    bool animPlaying= true;
    std::string stateMachine;   // nom de l asset, vide = aucune
};

struct TerrainData {
    bool  hasTerrain = false;
    int   resX = 128, resY = 128;
    float sizeX = 100.0f, sizeY = 100.0f;
    float uvTiling = 10.0f;
    int   renderMode = 5;
    glm::vec4 layers{0.0f, 5.0f, 10.0f, 0.6f};
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};        // euler degres
    glm::vec3 scale{1.0f};
    std::string texturePath;
    std::string heightmapFile = "terrain.bin";
    int layerMaterial[4] = { -1, -1, -1, -1 };
};

struct EnvironmentData {
    std::string envPath;                       // relatif au projet
    float exposure = 1.0f;
    float envIntensity = 1.0f;                 // ubo.shininess
    bool  showSky = true;
    glm::vec3 lightDirection{0.0f, 0.0f, -1.0f};
    glm::vec3 lightColor{1.0f};
    float lightIntensity = 1.0f;
    float ambientStrength = 0.2f;
};

struct MaterialData {
    std::string name;
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float aoStrength = 1.0f;
    float normalStrength = 1.0f;
    glm::vec4 emissive{0.0f, 0.0f, 0.0f, 1.0f};
    float heightScale = 0.05f;
    float alphaCutoff = 0.5f;
    float uvTiling = 1.0f;
    std::string mapPaths[8];
};

struct SceneData {
    std::vector<SceneObjectData> objects;
    glm::vec3 cameraPosition{0.0f};
    float cameraYaw = 0.0f;
    float cameraPitch = 0.0f;
    TerrainData terrain;
    EnvironmentData environment;
    std::vector<MaterialData> materials;
    std::vector<AnimationRefData> animations;
    std::vector<NotifyData> notifies;
    std::vector<RemovedClipData> removedClips;
    std::vector<RemovedClipData> inPlaceClips;   // meme forme : un clip designe par mesh + nom
};

namespace SceneSerializer {
    void save(const std::string& path,
              const std::vector<std::shared_ptr<SceneObject>>& sceneObjects,
              const std::vector<std::shared_ptr<Mesh>>& availableMeshes,
              const Camera& camera,
              const TerrainData& terrainData,
              const EnvironmentData& envData,
              const std::vector<MaterialData>& materialsData,
              const std::vector<AnimationRefData>& animationsData,
              const std::vector<NotifyData>& notifiesData,
              const std::vector<RemovedClipData>& removedClips,
              const std::vector<RemovedClipData>& inPlaceClips);

    SceneData load(const std::string& path);
}
