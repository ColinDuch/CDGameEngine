#pragma once

#include <engine/renderer/VulkanContext.hpp>
#include <glm/glm.hpp>
#include <vector>

// UBO des matrices d'os, avec son descriptor set (set 3 du pipeline skinne).
class BoneBuffer {
public:
    static constexpr uint32_t MaxBones = 256;

    static VkDescriptorSetLayout createLayout(VkDevice device);

    BoneBuffer(const VulkanContext& ctx, VkDescriptorPool pool, VkDescriptorSetLayout layout);
    ~BoneBuffer();

    BoneBuffer(const BoneBuffer&) = delete;
    BoneBuffer& operator=(const BoneBuffer&) = delete;

    void upload(const std::vector<glm::mat4>& mats);
    VkDescriptorSet getSet() const { return set; }

private:
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDescriptorSet set = VK_NULL_HANDLE;
};