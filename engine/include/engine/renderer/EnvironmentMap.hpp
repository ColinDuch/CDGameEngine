#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/Texture.hpp>

#include <memory>
#include <string>

// Carte d'environnement equirectangulaire (set 1, binding 0).
// Globale a la scene : un seul descriptor set, reecrit sur place au chargement.
class EnvironmentMap {
public:
    EnvironmentMap(const VulkanContext& ctx, VkDescriptorPool pool);
    ~EnvironmentMap();

    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    void setTexture(const std::string& path);
    void setShadowMap(VkImageView view, VkSampler samp) {
        shadowView = view; shadowSampler = samp; writeSet();
    }

    VkDescriptorSetLayout getLayout() const { return layout; }
    VkDescriptorSet       getSet()    const { return set; }
    const std::string&    getPath()   const { return path; }

private:
    void writeSet();

    const VulkanContext&  ctx;
    VkDescriptorPool      pool;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet       set    = VK_NULL_HANDLE;
    std::unique_ptr<Texture> texture;
    std::string path;
    VkImageView shadowView = VK_NULL_HANDLE;
    VkSampler   shadowSampler = VK_NULL_HANDLE;
};