#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/Texture.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <string>

// Les 8 cartes d'un materiau PBR etendu. L'ordre fixe les bindings du shader
// (binding 1 = Albedo ... binding 8 = Opacity) : ne pas le changer sans
// mettre a jour shader.frag EN MEME TEMPS.
enum MapSlot {
    MAP_ALBEDO = 0,
    MAP_NORMAL,
    MAP_ROUGHNESS,
    MAP_METALLIC,
    MAP_AO,
    MAP_HEIGHT,
    MAP_EMISSIVE,
    MAP_OPACITY,
    MAP_COUNT
};

// Nom lisible de chaque carte (pour l'UI). Defini dans le .cpp.
extern const char* const MapSlotNames[MAP_COUNT];

// Facteurs du materiau, envoyes au shader dans un petit UBO (std140).
// Chaque bloc fait 16 octets : ne pas reordonner sans verifier l'alignement.
struct MaterialParams {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};   // rgb = teinte, a = opacite

    float metallic  = 0.0f;
    float roughness = 0.5f;
    float aoStrength = 1.0f;
    float normalStrength = 1.0f;

    glm::vec4 emissive{0.0f, 0.0f, 0.0f, 1.0f};    // rgb = couleur, a = intensite

    float heightScale = 0.05f;   // amplitude du parallax
    float alphaCutoff = 0.5f;    // seuil de decoupe (mode masque)
    float uvTiling    = 1.0f;    // repetition des UV
    int   mapFlags    = 0;       // bit i = la carte i est chargee
};

// Materiau PARTAGE : plusieurs SceneObject peuvent pointer le meme.
// Possede ses textures, son UBO de facteurs et son descriptor set (set 2).
class MaterialAsset {
public:
    MaterialAsset(const VulkanContext& ctx, VkDescriptorPool pool,
                  VkDescriptorSetLayout layout, const std::string& name);
    ~MaterialAsset();

    MaterialAsset(const MaterialAsset&) = delete;
    MaterialAsset& operator=(const MaterialAsset&) = delete;

    // Cree le layout partage par tous les materiaux (UBO 0 + 8 samplers).
    // A appeler UNE fois, avant de construire le moindre materiau.
    static VkDescriptorSetLayout createLayout(VkDevice device);

    void setMap(int slot, const std::string& path);   // charge une carte
    void clearMap(int slot);                          // revient a la valeur par defaut
    void uploadParams();                              // apres avoir modifie params()

    MaterialParams&       params()       { return paramData; }
    const MaterialParams& params() const { return paramData; }

    bool hasMap(int slot) const { return (paramData.mapFlags & (1 << slot)) != 0; }
    const std::string& getMapPath(int slot) const { return mapPaths[slot]; }

    const std::string& getName() const { return name; }
    void setName(const std::string& n) { name = n; }

    VkDescriptorSet getSet() const { return descriptorSet; }

private:
    void createDefaultMaps();
    void createParamBuffer();
    void writeSet();

    const VulkanContext&  ctx;
    VkDescriptorPool      pool;
    VkDescriptorSetLayout layout;
    VkDescriptorSet       descriptorSet = VK_NULL_HANDLE;

    std::string name;
    MaterialParams paramData;

    std::unique_ptr<Texture> maps[MAP_COUNT];       // toujours valides (defauts si absent)
    std::unique_ptr<Texture> defaults[MAP_COUNT];   // gardes pour pouvoir revenir en arriere
    std::string mapPaths[MAP_COUNT];

    VkBuffer       paramBuffer = VK_NULL_HANDLE;
    VkDeviceMemory paramMemory = VK_NULL_HANDLE;
    void*          paramMapped = nullptr;
};
