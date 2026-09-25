#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class DescriptorSetLayout {
public:
    DescriptorSetLayout(VkDevice device);
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

    VkDescriptorSetLayout getHandle() const { return layout; }

private:
    VkDevice device;
    VkDescriptorSetLayout layout;
};