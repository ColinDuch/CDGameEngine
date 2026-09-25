#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class DescriptorSet {
public:
    DescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout,
                  VkBuffer uniformBuffer,
                  VkImageView textureImageView, VkSampler textureSampler,
                  VkImageView normalImageView, VkSampler normalSampler);

    VkDescriptorSet getHandle() const { return descriptorSet; }

private:
    VkDescriptorSet descriptorSet;
};