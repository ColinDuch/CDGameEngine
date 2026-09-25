#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class CommandPool {
public:
    CommandPool(VkDevice device, uint32_t graphicsFamilyIndex);
    ~CommandPool();

    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;

    VkCommandPool getHandle() const { return commandPool; }

private:
    VkDevice device;
    VkCommandPool commandPool;
};