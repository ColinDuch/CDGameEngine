#pragma once

#include <engine/renderer/Mesh.hpp>
#include <engine/renderer/AnimStateMachine.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>
#include <string>

class SceneObject {
public:
    SceneObject(std::shared_ptr<Mesh> mesh);

    // Transformation LOCALE (relative au parent).
    glm::mat4 getLocalMatrix() const;
    // Transformation MONDE : le cache, recalcule par Application::updateWorldMatrices().
    glm::mat4 getModelMatrix() const { return worldMatrix; }

    VkBuffer getVertexBufferHandle() const { return mesh->getVertexBufferHandle(); }
    VkBuffer getSkinBufferHandle() const { return mesh ? mesh->getSkinBufferHandle() : VK_NULL_HANDLE; }
    VkBuffer getIndexBufferHandle() const { return mesh->getIndexBufferHandle(); }
    uint32_t getIndexCount() const { return mesh->getIndexCount(); }
    VkDescriptorSet getDescriptorSetHandle() const { return mesh->getDescriptorSetHandle(); }

    // Sous-meshes a dessiner (chacun sa texture/damier + normal map).
    const std::vector<Mesh::SubMeshRender>& getSubMeshes() const { return mesh->getSubMeshes(); }

    // Mesh partage (necessaire au frustum culling : boite englobante).
    const std::shared_ptr<Mesh>& getMesh() const { return mesh; }

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};   // rotation = VÉRITÉ (pas de gimbal)
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    int meshIndex = 0;
    int parent = -1;                 // index dans sceneObjects (-1 = racine)
    glm::mat4 worldMatrix{1.0f};     // CACHE, recalcule une fois par frame
    std::string scriptPath;   // relatif au projet, ex "code/objet_0.lua" (vide = aucun)

    // Euler (degres) uniquement pour les sliders et la sauvegarde .scene.
    glm::vec3 getEulerDegrees() const;
    void setEulerDegrees(const glm::vec3& deg);

    float metallic  = 0.0f;
    float roughness = 0.5f;

    // > 0 : texture projetee depuis le monde (blockout), valeur = taille d'une tuile.
    float worldUvScale = 0.0f;

    int materialIndex = -1;   // -1 = aucun materiau assigne
    
    std::string name = "Object";

    // un identifiant unique et stable : ne change jamais, meme si l ordre de la scene change
    uint32_t id = 0;    // 0 = pas encore attribue

    // --- physique ---
    enum class BodyType { None, Static, Dynamic, Character }; // nouveaux types : toujours a la fin
    BodyType bodyType = BodyType::None;     // aucun corps par default : decor sans collision
    float mass = 1.0f;      // kg, pour un corps dynamique 
    // nom du type dans scene.json : un texte reste lisible et ne depend pas de l ordre de l enum
    static const char* bodyTypeName(BodyType t) {
        switch (t) {
            case BodyType::Static: return "static";
            case BodyType::Dynamic: return "dynamic";
            case BodyType::Character: return "character";
            default : return "none";
        }
    }
    static BodyType bodyTypeFromName(const std::string& n) {
        if (n == "static") return BodyType::Static;
        if (n == "dynamic") return BodyType::Dynamic;
        if (n == "character") return BodyType::Character;
        return BodyType::None;
    }

    int animClip = -1;
    float animTime = 0.0f;
    float animSpeed = 1.0f;
    bool animPlaying = true;
    bool animLoop = true;

    int animPrevClip = -1;
    float animPrevTime = 0.0f;
    float animBlend = 1.0f;
    float animBlendDuration = 0.2f;
    float animLastTime = 0.0f;  // temps de l image precedente 
    bool animJustEnded = false;
    AnimStateMachineInstance stateMachine;

    void playClip(int clip, float fade = 0.2f) {
        if (clip == animClip) return;
        if (animClip >= 0 && fade > 0.0f) {
            animPrevClip = animClip;
            animPrevTime = animTime;
            animBlend = 0.0f;
            animBlendDuration = fade;
        } else {
            animPrevClip = -1;
            animBlend = 1.0f;
        }
        animClip = clip;
        animTime = 0.0f;
        animLastTime = 0.0f;
        animJustEnded = false;
    }

private:
    std::shared_ptr<Mesh> mesh;
};