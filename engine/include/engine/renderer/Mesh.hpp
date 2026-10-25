#pragma once

#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/VertexBuffer.hpp>
#include <engine/renderer/IndexBuffer.hpp>
#include <engine/renderer/Model.hpp>
#include <engine/renderer/Texture.hpp>
#include <engine/renderer/DescriptorSet.hpp>
#include <engine/renderer/MeshCache.hpp>
#include <engine/renderer/Skeleton.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

class Mesh {
public:
    // Donnees de rendu d'un sous-mesh : une plage d'indices + son propre materiau.
    struct SubMeshRender {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        int renderMode = 0;
        bool hasNormalMap = false;
        float metallic  = 0.0f;
        float roughness = 0.5f;
        glm::vec3 baseColor{1.0f, 1.0f, 1.0f};
        std::shared_ptr<Texture> texture;
        std::shared_ptr<Texture> normalTexture;
        std::unique_ptr<DescriptorSet> descriptorSet;

        std::string diffusePath;
        std::string normalPath;
        bool referencesTexture = false;

        VkDescriptorSet getDescriptorSetHandle() const { return descriptorSet->getHandle(); }
    };

    // Constructeur ".obj" : charge le modele depuis un fichier.
    Mesh(const VulkanContext& ctx,
         const std::string& modelPath, const std::string& texturePath,
         VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
         VkBuffer uniformBuffer, bool yUpToZUp = false);

    // Constructeur "memoire" : recoit vertices/indices deja en memoire (import glTF).
    Mesh(const VulkanContext& ctx,
         const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
         const std::string& texturePath,
         std::string modelPath,
         VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
         VkBuffer uniformBuffer);
    
    ~Mesh();

    // Handles Vulkan bruts : la copie provoquerait une double liberation.
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    VkBuffer getVertexBufferHandle() const { return vertexBuffer->getHandle(); }
    VkBuffer getIndexBufferHandle() const { return indexBuffer->getHandle(); }
    uint32_t getIndexCount() const { return indexBuffer->getIndexCount(); }

    // Les sous-meshes a dessiner (chacun sa texture/damier + normal map).
    const std::vector<SubMeshRender>& getSubMeshes() const { return subMeshes; }

    // Compat : descriptor set du 1er sous-mesh (utilise par la grille).
    VkDescriptorSet getDescriptorSetHandle() const { return subMeshes[0].getDescriptorSetHandle(); }

    const std::string& getTexturePath() const { return texturePath; }
    const std::string& getModelPath() const { return modelPath; }
    bool isYUpToZUp() const { return yUpToZUp; }    // modele redresse a l'import 

    void setModelPath(const std::string& p) { modelPath = p; }

    const std::string& getBuiltinName() const { return builtinName; }
    void setBuiltinName(const std::string& n) { builtinName = n; }

    // Extrait la geometrie pour l'ecrire dans un .emesh.
    MeshCacheData toCacheData() const;

    const std::vector<Vertex>& getVerticesCPU() const { return verticesCPU; }
    const std::vector<uint32_t>& getIndicesCPU() const { return indicesCPU; }
    const std::vector<Vertex>& getVertices() const { return verticesCPU; }
    const std::vector<uint32_t>& getIndices() const { return indicesCPU; }
    bool getHasUV() const { return hasUV; }

    // Boite englobante en espace LOCAL du mesh.
    const glm::vec3& getBoundsMin() const { return boundsMin; }
    const glm::vec3& getBoundsMax() const { return boundsMax; }

    void setMaterialParams(float m, float r) {
        for (auto& s : subMeshes) { s.metallic = m; s.roughness = r; }
    }
    float getMetallic()  const { return subMeshes.empty() ? 0.0f : subMeshes[0].metallic; }
    float getRoughness() const { return subMeshes.empty() ? 0.5f : subMeshes[0].roughness; }

    const Skeleton& getSkeleton() const { return skeleton; }
    const std::vector<VertexSkin>& getSkins() const { return skins; }
    bool isSkinned() const { return !skeleton.empty() && !skins.empty(); }

    VkBuffer getSkinBufferHandle() const { return skinBuffer; }

    const std::vector<AnimationClip>& getClips() const { return clips; }
    void addClips(const std::vector<AnimationClip>& c) {clips.insert(clips.end(), c.begin(), c.end()); }

    std::vector<AnimationClip>& getClipsMutable() { return clips; }

private:
    void createGeometry(const VulkanContext& ctx,
                        const std::vector<Vertex>& verts,
                        const std::vector<uint32_t>& inds);

    void addSubMesh(const VulkanContext& ctx,
                    uint32_t indexOffset, uint32_t indexCount,
                    const std::string& diffusePath, const std::string& normalPath,
                    const glm::vec3& baseColor, bool hasUVSub, bool referencesTexture,
                    float metallic, float roughness,
                    VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
                    VkBuffer uniformBuffer);

    void buildFromCache(const VulkanContext& ctx,
                        const MeshCacheData& cached,
                        const std::string& cacheFilePath,
                        VkDescriptorPool descriptorPool,
                        VkDescriptorSetLayout descriptorSetLayout,
                        VkBuffer uniformBuffer);

    std::shared_ptr<VertexBuffer> vertexBuffer;
    std::shared_ptr<IndexBuffer> indexBuffer;
    std::vector<SubMeshRender> subMeshes;

    std::vector<Vertex> verticesCPU;
    std::vector<uint32_t> indicesCPU;
    std::string texturePath;
    std::string modelPath;
    bool yUpToZUp = false;
    bool hasUV = false;

    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    Skeleton skeleton;
    std::vector<VertexSkin> skins;

    VkBuffer       skinBuffer = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;   // memorise pour le destructeur
    VkDeviceMemory skinMemory = VK_NULL_HANDLE;
    void createSkinBuffer(const VulkanContext& ctx);

    std::vector<AnimationClip> clips;
    std::string builtinName;
};