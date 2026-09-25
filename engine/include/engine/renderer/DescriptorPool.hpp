#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class DescriptorPool {
public:
    DescriptorPool(VkDevice device, uint32_t maxSets);
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    VkDescriptorPool getHandle() const { return pool; }

private:
    VkDevice device;
    VkDescriptorPool pool;
};