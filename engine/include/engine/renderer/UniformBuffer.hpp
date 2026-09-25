#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <engine/renderer/VulkanContext.hpp>
#include <engine/renderer/UniformBufferObject.hpp>

class UniformBuffer {
public:
    UniformBuffer(const VulkanContext& ctx);
    ~UniformBuffer();

    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;

    void update(const UniformBufferObject& ubo);

    VkBuffer getHandle() const { return buffer; }

private:

    VkDevice device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mappedData;
};