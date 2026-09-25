#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>
#include <string>

#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/Vertex.hpp>
#include <engine/renderer/Texture.hpp>
#include <engine/renderer/DescriptorSet.hpp>

struct NoiseParams {
        float amplitude   = 15.0f;   // hauteur max du relief (unites monde)
        float scale       = 40.0f;   // taille des grandes formes (plus grand = plus large)
        int   octaves     = 5;       // niveaux de detail
        float persistence = 0.5f;    // decroissance d'amplitude par octave
        float lacunarity  = 2.0f;    // croissance de frequence par octave
        int   seed        = 1337;    // graine (change tout le relief)
        bool  add         = false;   // false = remplace, true = ajoute au relief actuel
    };

// Terrain heightfield : grille de sommets dans le plan XY (monde Z-up), hauteur = Z.
// Vertex buffer HOST_VISIBLE mappe en permanence -> editable (sculpt) sans staging.
// Index buffer statique : la topologie ne change jamais, seules les hauteurs bougent.
class Terrain {
public:
    Terrain(const VulkanContext& ctx,
            VkDescriptorPool pool, VkDescriptorSetLayout layout, VkBuffer uniformBuffer,
            int resX, int resY, float sizeX, float sizeY);
    ~Terrain();

    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    uint64_t getVersion() const { return version; }
    void markHeightsChanged() { ++version; }
    void setHeights(const std::vector<float>& h);

    // Reconstruit tout le terrain (nouvelle taille / resolution). Remet le relief a plat.
    void rebuild(int resX, int resY, float sizeX, float sizeY);

    // A appeler apres avoir modifie heights() : recalcule normales puis pousse au GPU.
    void recomputeNormals();
    void uploadVertices();

    void setUvTiling(float worldUnitsPerTile);
    void setTexture(const std::string& path); 
    float uvTiling = 10.0f;     // texture repetee tous les N unites monde
    bool  hasTexture = false;

    VkBuffer        getVertexBufferHandle()  const { return vertexBuffer; }
    VkBuffer        getIndexBufferHandle()   const { return indexBuffer; }
    uint32_t        getIndexCount()          const { return indexCount; }
    VkDescriptorSet getDescriptorSetHandle() const { return descriptorSet->getHandle(); }
    // Grille allegee dediee a la carte d'ombre.
    VkBuffer getShadowIndexBufferHandle() const { return shadowIndexBuffer; }
    uint32_t getShadowIndexCount()        const { return shadowIndexCount; }
    int      getShadowStride()            const { return shadowStride; }
    void     setShadowStride(int stride);

    int   getResX()  const { return resX; }
    int   getResY()  const { return resY; }
    float getSizeX() const { return sizeX; }
    float getSizeY() const { return sizeY; }

    float       getUvTiling() const { return uvTiling; }
    std::string getTexturePath() const { return texturePath; }

    void saveHeightmap(const std::string& path) const;
    void loadHeightmap(const std::string& path);

    bool loadHeightmapImage(const std::string& path, float minHeight, float maxHeight,
                            std::string& outError);

    float heightAtWorld(float worldX, float worldY) const;
    bool  raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDir, glm::vec3& outHit) const;

    void sculpt(const glm::vec3& center, float radius, float amount);
    void smooth(const glm::vec3& center, float radius, float strength);
    void flatten(const glm::vec3& center, float radius, float strength, float target);
    void generateNoise(const NoiseParams& p);

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    void setLayerTexture(int index, const std::string& path);
    VkDescriptorSetLayout getLayerLayout() const { return layerLayout; }
    VkDescriptorSet       getLayerSet()    const { return layerSet; }
    const std::string&    getLayerPath(int i) const { return layerPaths[i]; }

    glm::mat4 getModelMatrix() const;
    glm::vec3 getEulerDegrees() const;
    void      setEulerDegrees(const glm::vec3& deg);

    // Hauteurs modifiables (index = iy*resX + ix).
    std::vector<float>& heights() { return heightData; }

    int layerMaterial[4] = { -1, -1, -1, -1 };   // index dans la bibliotheque, -1 = defaut

private:
    void generateGeometry();
    void createGpuBuffers();
    void destroyGpuBuffers();

    const VulkanContext&  ctx;
    VkDescriptorPool      pool;
    VkDescriptorSetLayout layout;
    VkBuffer              uniformBuffer;

    int   resX = 0, resY = 0;         // nombre de sommets par axe (>= 2)
    float sizeX = 0.0f, sizeY = 0.0f;

    std::vector<float>    heightData;
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    uint32_t              indexCount = 0;

    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    // Staging HOST_VISIBLE mappe en permanence : c'est ici qu'on ecrit.
    // Le vertex buffer, lui, est DEVICE_LOCAL (lu par le GPU a pleine vitesse).
    VkBuffer       stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void*          stagingMapped = nullptr;

    VkBuffer       indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    std::vector<uint32_t> shadowIndices;
    VkBuffer       shadowIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory shadowIndexMemory = VK_NULL_HANDLE;
    uint32_t       shadowIndexCount = 0;
    int            shadowStride = 4;      // 1 = grille complete

    void buildShadowIndices();
    void createShadowIndexBuffer();
    void destroyShadowIndexBuffer();

    std::unique_ptr<Texture>       diffuseTex;
    std::unique_ptr<Texture>       normalTex;
    std::unique_ptr<DescriptorSet> descriptorSet;

    std::string texturePath;   // chemin de la texture chargee (pour la sauvegarde)

    void createLayerResources();
    void writeLayerSet();

    VkDescriptorSetLayout layerLayout = VK_NULL_HANDLE;
    VkDescriptorSet       layerSet = VK_NULL_HANDLE;
    std::unique_ptr<Texture> layers[4];
    std::string layerPaths[4];

    uint64_t version = 0;

};