#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>



// Dessine la carte d'environnement en fond (triangle plein ecran).
class SkyRenderer {
public:
    SkyRenderer(VkDevice device, VkRenderPass renderPass, VkDescriptorSetLayout envSetLayout);
    ~SkyRenderer();

    SkyRenderer(const SkyRenderer&) = delete;
    SkyRenderer& operator=(const SkyRenderer&) = delete;

    struct SkyPush {
        glm::mat4 invViewProj;
        float exposure = 1.0f;
    };

    void record(VkCommandBuffer cmd, VkDescriptorSet envSet, const glm::mat4& invViewProj,
                VkExtent2D extent, float exposure);

private:
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readFile(const std::string& filename);

    VkDevice device;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};