#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/SceneObject.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Carte d'ombres pour la lumiere directionnelle.
// Rend la scene en PROFONDEUR SEULE depuis le soleil, dans sa propre passe.
// Le resultat est echantillonne par shader.frag / terrain.frag (set 1, binding 1).
class ShadowMap {
public:
    ShadowMap(const VulkanContext& ctx, uint32_t size = 2048);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Matrice lumiere : englobe une sphere de rayon `radius` centree sur `center`.
    static glm::mat4 computeLightSpace(const glm::vec3& lightDirection,
                                       const glm::vec3& center, float radius);

    // Enregistre la passe d'ombre. A appeler AVANT le render pass principal.
    void record(VkCommandBuffer cmd,
                const std::vector<std::shared_ptr<SceneObject>>& objects,
                const glm::mat4& lightSpace,
                VkBuffer terrainVB, VkBuffer terrainIB, uint32_t terrainIndexCount,
                const glm::mat4& terrainModel,
                const std::vector<VkDescriptorSet>& boneSets);

    VkImageView getImageView() const { return imageView; }
    VkSampler   getSampler()   const { return sampler; }

    void createSkinnedPipeline(VkDescriptorSetLayout boneLayout);

private:
    void createImage();
    void createRenderPass();
    void createFramebuffer();
    void createPipeline();
    void createPipelineInternal(const char* vertPath, VkDescriptorSetLayout boneLayout,
                                VkPipeline& outPipeline, VkPipelineLayout& outLayout);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    const VulkanContext& ctx;
    uint32_t size;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkPipeline skinnedPipeline =VK_NULL_HANDLE;
    VkPipelineLayout skinnedLayout = VK_NULL_HANDLE;

};
